module gui_d.plugin_window;

import core.stdc.math : cosf, expf;
import dlangui;
import dlangui.graphics.drawbuf : ColorDrawBuf, DrawBuf;
import dlangui.graphics.fonts : FontWeight;
import dlangui.widgets.widget : Visibility, Widget;
import gui_d.device_panels : buildBuiltinDeviceLayout, BuiltinDeviceUiState, drawBuiltinDevicePanel,
    handleBuiltinDeviceMouse, hasCustomBuiltinDeviceUi;
import gui_d.icons : DawIcon, drawDawIcon;
import gui_d.theme : adjustColorBrightness, dp, dpf, drawCard, drawCenteredText, drawRotaryKnob,
    drawThemeCard, drawThickLine, drawVCenteredText, FlPalette, getDawFont, getDawMonoFont,
    getFlPalette, safeWidgetFont, ScopedRetroAntialias, truncateLabel;
import gui_d.transport_model : DrumPadSlotState, ParamItemState, PluginSlotState, TransportModel;
import gui_d.workspace : activateParentWindow;
import std.algorithm : canFind, startsWith;
import std.format : format;
import std.string : toLower;
import std.utf : toUTF32;

/// Target source shown inside the Fruity Wrapper window (Channel Generator vs Mixer FX Slot).
enum WrapperTargetKind {
    CHANNEL_GENERATOR,
    MIXER_FX,
}

private enum int WRAP_HDR_H = 32;
private enum int SCOPE_H = 74;

struct PluginWrapperMetrics {
    int hdrH;
    int scopeH;
    Rect hdrRc;
    Rect prevRc;
    Rect presRc;
    Rect nextRc;
    Rect guiRc;
    Rect bypRc;
    Rect scopeRc;
    Rect sampleLoadRc;
    Rect editorDstRc;
    int gridTop;
    int gridH;
}

PluginWrapperMetrics computePluginWrapperMetrics(Rect rc) nothrow @safe {
    PluginWrapperMetrics m;
    m.hdrH = dp(WRAP_HDR_H);
    m.scopeH = dp(SCOPE_H);
    m.hdrRc = Rect(rc.left, rc.top, rc.right, rc.top + m.hdrH);
    m.prevRc = Rect(m.hdrRc.left + dp(150), m.hdrRc.top + dp(5), m.hdrRc.left + dp(172), m.hdrRc.bottom - dp(5));
    m.presRc = Rect(m.hdrRc.left + dp(172), m.hdrRc.top + dp(5), m.hdrRc.left + dp(288), m.hdrRc.bottom - dp(5));
    m.nextRc = Rect(m.hdrRc.left + dp(288), m.hdrRc.top + dp(5), m.hdrRc.left + dp(310), m.hdrRc.bottom - dp(5));
    m.guiRc = Rect(m.hdrRc.right - dp(86), m.hdrRc.top + dp(5), m.hdrRc.right - dp(28), m.hdrRc.bottom - dp(5));
    m.bypRc = Rect(m.hdrRc.right - dp(28), m.hdrRc.top + dp(5), m.hdrRc.right - dp(8), m.hdrRc.bottom - dp(5));
    m.scopeRc = Rect(rc.left + dp(10), m.hdrRc.bottom + dp(8), rc.right - dp(10), m.hdrRc.bottom + dp(8) + m.scopeH);
    m.sampleLoadRc = Rect(m.scopeRc.right - dp(86), m.scopeRc.top + dp(4), m.scopeRc.right - dp(4), m.scopeRc.top + dp(22));
    m.editorDstRc = Rect(rc.left + dp(8), m.hdrRc.bottom + dp(6), rc.right - dp(8), rc.bottom - dp(8));
    m.gridTop = m.scopeRc.bottom + dp(10);
    m.gridH = rc.bottom - m.gridTop - dp(8);
    return m;
}

/// Computes aspect-fit destination rectangle for a remote VST3 framebuffer inside `availRc`.
Rect computeRemoteEditorFitRect(Rect availRc, int srcW, int srcH) nothrow @safe {
    if (srcW <= 0 || srcH <= 0 || availRc.width <= 0 || availRc.height <= 0) return availRc;
    const double sx = cast(double)availRc.width / cast(double)srcW;
    const double sy = cast(double)availRc.height / cast(double)srcH;
    const double s = sx < sy ? sx : sy;
    const int dw = cast(int)(srcW * s);
    const int dh = cast(int)(srcH * s);
    const int x0 = availRc.left + (availRc.width - dw) / 2;
    const int y0 = availRc.top + (availRc.height - dh) / 2;
    return Rect(x0, y0, x0 + dw, y0 + dh);
}

/// Applies a deterministic preset parameter snapshot when cycling Presets `< >`.
void applyPluginPresetValues(ref PluginSlotState plug, int presetIdx) @safe {
    if (plug.params.length == 0) return;
    const int pMod = ((presetIdx % 5) + 5) % 5;
    foreach (i, ref pm; plug.params) {
        final switch (pMod) {
            case 0: // Default
                pm.currentValue = pm.defaultValue;
                break;
            case 1: // Warm Analog
                pm.currentValue = (i % 2 == 0) ? 0.38f : 0.62f;
                break;
            case 2: // Punchy Club
                pm.currentValue = (i % 2 == 0) ? 0.78f : 0.48f;
                break;
            case 3: // Wide Stereo
                pm.currentValue = (i % 3 == 0) ? 0.85f : 0.55f;
                break;
            case 4: // Init
                pm.currentValue = 0.50f;
                break;
        }
    }
}

private bool isSamplerPlugin(const(PluginSlotState)* plug) pure nothrow @safe {
    if (!plug) return false;
    foreach (c; ["sampler", "Sampler"]) {
        if (plug.uri.length > 0 && canFind(plug.uri, c)) return true;
        if (plug.name.length > 0 && canFind(plug.name, c)) return true;
    }
    return false;
}

private bool isDrumPlugin(const(PluginSlotState)* plug) pure nothrow @safe {
    if (!plug) return false;
    if (plug.drumPads.length > 0) return true;
    return canFind(plug.uri, "drum_machine") || canFind(plug.name, "Drum");
}

/// Resolves the active `PluginSlotState*` being edited by the Plugin Wrapper.
PluginSlotState* resolveActiveWrapperPlugin(
    TransportModel* model,
    WrapperTargetKind kind,
    int trackOrChannelIdx,
    int fxSlotIdx
) @trusted {
    if (!model) return null;
    if (kind == WrapperTargetKind.CHANNEL_GENERATOR) {
        foreach (ref ch; model.channels) {
            if (ch.index == trackOrChannelIdx) return &ch.generatorPlugin;
        }
        if (model.channels.length > 0) return &model.channels[0].generatorPlugin;
        return null;
    }
    foreach (ref t; model.tracks) {
        if (t.index == trackOrChannelIdx) {
            foreach (ref p; t.plugins) {
                if (p.pluginIndex == fxSlotIdx) return &p;
            }
            if (t.plugins.length > 0) return &t.plugins[0];
        }
    }
    return null;
}

/// Maps `dB` value into normalized `[0..1]` range across `[minDb .. maxDb]`.
private float normalizeDb(float db, float minDb, float maxDb) pure nothrow @safe {
    if (maxDb <= minDb) return 0.0f;
    float n = (db - minDb) / (maxDb - minDb);
    if (n < 0.0f) n = 0.0f;
    if (n > 1.0f) n = 1.0f;
    return n;
}

private void drawSpectrumCurve(
    DrawBuf buf,
    Rect scopeRc,
    int pad,
    const(float)[] mags,
    uint col
) {
    if (mags.length < 2) return;
    const int sw = scopeRc.width - pad * 2;
    const int sh = scopeRc.height - pad * 2;
    if (sw <= dp(8) || sh <= dp(8)) return;
    float prevX = scopeRc.left + pad;
    float prevY = scopeRc.bottom - pad - normalizeDb(mags[0], -60.0f, 12.0f) * sh;
    foreach (i; 1 .. mags.length) {
        const float t = cast(float)i / cast(float)(mags.length - 1);
        const float x = scopeRc.left + pad + t * sw;
        const float y = scopeRc.bottom - pad - normalizeDb(mags[i], -60.0f, 12.0f) * sh;
        drawThickLine(buf, prevX, prevY, x, y, dpf(1.75f), col);
        prevX = x;
        prevY = y;
    }
}

/// Renders the FL Studio 2026 Fruity Wrapper Plugin Editor canvas.
private bool hasActiveRemoteEditorFrame(const(PluginSlotState)* plug) pure nothrow @safe {
    return plug !is null && plug.remoteEditorOpen && plug.editorW > 0 && plug.editorH > 0 &&
           plug.editorRgba.length >= cast(size_t)(plug.editorW * plug.editorH * 4);
}

void drawPluginWrapperCanvas(
    DrawBuf buf,
    Rect rc,
    const(TransportModel)* model,
    const(PluginSlotState)* plug,
    FontRef fnt = FontRef.init,
    DrawBuf remoteFrameBuf = null,
    const(BuiltinDeviceUiState)* builtinUi = null,
    bool showRawParams = false
) {
    const pal = model ? model.palette() : getFlPalette();
    auto aaGuard = ScopedRetroAntialias(pal.retroBevel);
    buf.fillRect(rc, pal.bgWindow);
    if (rc.width < dp(260) || rc.height < dp(160)) return;

    const m = computePluginWrapperMetrics(rc);
    FontRef boldFnt = getDawFont(11, fnt, FontWeight.Normal);
    FontRef uiFnt = getDawFont(10, fnt, FontWeight.Normal);
    FontRef valFnt = getDawMonoFont(10, fnt, FontWeight.Normal);

    // 1. Wrapper Header Bar (< Preset >, EDITOR toggle, Bypass)
    buf.fillRect(m.hdrRc, pal.bgPanelAlt);
    buf.fillRect(Rect(m.hdrRc.left, m.hdrRc.bottom - dp(1), m.hdrRc.right, m.hdrRc.bottom), pal.borderStrong);

    const string plugTitle = plug ? plug.name : "No Plugin Loaded";
    drawVCenteredText(buf, boldFnt, m.hdrRc.left + dp(10), m.hdrRc,
                      toUTF32(truncateLabel(plugTitle, 18)), pal.accentOrange);

    // Unified flush Preset selector < Preset >
    drawThemeCard(buf, Rect(m.prevRc.left, m.prevRc.top, m.nextRc.right, m.nextRc.bottom),
                  pal.bgControl, COLOR_TRANSPARENT, pal, 1.5f, false);
    drawThemeCard(buf, m.presRc, pal.bgLcd, COLOR_TRANSPARENT, pal, 0.0f, true);

    string presetName = "Default";
    if (plug && plug.presets.length > 0) {
        const size_t pi = ((plug.presetIndex % cast(int)plug.presets.length) + cast(int)plug.presets.length) %
                          cast(int)plug.presets.length;
        presetName = plug.presets[pi];
    }
    drawDawIcon(buf, Rect(m.prevRc.left + dp(3), m.prevRc.top + dp(3), m.prevRc.right - dp(3), m.prevRc.bottom - dp(3)),
                DawIcon.CHEVRON_LEFT, pal.fgText);
    drawVCenteredText(buf, uiFnt, m.presRc.left + dp(6), m.presRc,
                      toUTF32(truncateLabel(presetName, 14)), pal.accentCyan);
    drawDawIcon(buf, Rect(m.nextRc.left + dp(3), m.nextRc.top + dp(3), m.nextRc.right - dp(3), m.nextRc.bottom - dp(3)),
                DawIcon.CHEVRON_RIGHT, pal.fgText);

    // Unified flush VST3 EDITOR / PARAMS + Bypass strip
    const bool customBuiltin = hasCustomBuiltinDeviceUi(plug);
    const bool edOpen = plug && plug.remoteEditorOpen;
    const bool toggleLit = customBuiltin ? showRawParams : edOpen;
    dstring guiBtnLbl = customBuiltin ? "PARAMS"d : "EDITOR"d;
    drawThemeCard(buf, Rect(m.guiRc.left, m.guiRc.top, m.bypRc.right, m.bypRc.bottom),
                  toggleLit ? pal.accentOrange : pal.bgControl, COLOR_TRANSPARENT, pal, 1.5f, false);
    drawThemeCard(buf, m.bypRc, (plug && plug.enabled) ? pal.accentGreen : pal.ledOff,
                  COLOR_TRANSPARENT, pal, 1.5f, pal.retroBevel);
    drawCenteredText(buf, uiFnt, m.guiRc, guiBtnLbl, toggleLit ? pal.bgWindow : pal.fgText);

    if (!plug) return;

    // 1b. Remote VST3 Offscreen Editor Framebuffer mode
    if (hasActiveRemoteEditorFrame(plug)) {
        drawThemeCard(buf, m.editorDstRc, pal.bgLcd, pal.borderSubtle, pal, 2.0f, true);
        if (remoteFrameBuf !is null) {
            const dstFit = computeRemoteEditorFitRect(m.editorDstRc, plug.editorW, plug.editorH);
            buf.drawRescaled(dstFit, remoteFrameBuf, Rect(0, 0, plug.editorW, plug.editorH));
        }
        return;
    }

    // 1c. Builtin Swing-ported custom device UI (shown directly without extra EDITOR click)
    if (customBuiltin && !showRawParams) {
        BuiltinDeviceUiState fallbackUi;
        const auto uiPtr = builtinUi ? builtinUi : &fallbackUi;
        drawBuiltinDevicePanel(buf, Rect(rc.left, m.hdrRc.bottom, rc.right, rc.bottom),
                               model, plug, *uiPtr, fnt);
        return;
    }

    // 2. DSP Visual Response / Telemetry Box
    Rect scopeRc = m.scopeRc;
    drawThemeCard(buf, scopeRc, pal.bgLcd, pal.borderSubtle, pal, 2.0f, true);

    const int scopePad = dp(8);
    const int sw = scopeRc.width - scopePad * 2;
    const int sh = scopeRc.height - scopePad * 2;

    if (plug.specOutDb.length > 1 || plug.specInDb.length > 1) {
        if (plug.specInDb.length > 1) {
            drawSpectrumCurve(buf, scopeRc, scopePad, plug.specInDb, pal.fgMuted);
        }
        if (plug.specOutDb.length > 1) {
            drawSpectrumCurve(buf, scopeRc, scopePad, plug.specOutDb, pal.accentCyan);
        }
        drawVCenteredText(buf, uiFnt, scopeRc.left + dp(8),
                          Rect(scopeRc.left, scopeRc.top + dp(2), scopeRc.left + dp(120), scopeRc.top + dp(16)),
                          "FFT SPECTRUM"d, pal.fgMuted);
    } else if (plug.scopeL.length > 1 && plug.scopeR.length > 1) {
        const size_t n = plug.scopeL.length < plug.scopeR.length ? plug.scopeL.length : plug.scopeR.length;
        const float midY = scopeRc.top + scopePad + sh * 0.5f;
        foreach (i; 1 .. n) {
            const float x0 = scopeRc.left + scopePad + cast(float)(i - 1) / cast(float)(n - 1) * sw;
            const float x1 = scopeRc.left + scopePad + cast(float)i / cast(float)(n - 1) * sw;
            float l0 = plug.scopeL[i - 1];
            float l1 = plug.scopeL[i];
            if (l0 < -1.0f) l0 = -1.0f; else if (l0 > 1.0f) l0 = 1.0f;
            if (l1 < -1.0f) l1 = -1.0f; else if (l1 > 1.0f) l1 = 1.0f;
            drawThickLine(buf, x0, midY - l0 * sh * 0.44f, x1, midY - l1 * sh * 0.44f,
                          dpf(1.5f), pal.accentCyan);
            float r0 = plug.scopeR[i - 1];
            float r1 = plug.scopeR[i];
            if (r0 < -1.0f) r0 = -1.0f; else if (r0 > 1.0f) r0 = 1.0f;
            if (r1 < -1.0f) r1 = -1.0f; else if (r1 > 1.0f) r1 = 1.0f;
            drawThickLine(buf, x0, midY - r0 * sh * 0.44f, x1, midY - r1 * sh * 0.44f,
                          dpf(1.2f), pal.accentOrange);
        }
    } else if (isSamplerPlugin(plug)) {
        // Real Sampler waveform + [LOAD WAV] button
        if (plug.sampleWaveform.length > 0 && sw > dp(16)) {
            const int midY = scopeRc.top + scopeRc.height / 2;
            const int halfH = sh / 2;
            foreach (i, amp; plug.sampleWaveform) {
                const int px = scopeRc.left + scopePad + cast(int)(cast(double)i / plug.sampleWaveform.length * sw);
                float a = amp < 0.0f ? -amp : amp;
                if (a > 1.0f) a = 1.0f;
                int bh = cast(int)(a * halfH);
                if (bh < 1) bh = 1;
                buf.fillRect(Rect(px, midY - bh, px + dp(2), midY + bh), pal.accentCyan);
            }
        }
        const string sLbl = plug.sampleName.length > 0
            ? format("%s (%.2fs)", plug.sampleName, plug.sampleDurationSec)
            : "No Sample Loaded — Click LOAD WAV";
        drawVCenteredText(buf, uiFnt, scopeRc.left + dp(8),
                          Rect(scopeRc.left, scopeRc.top + dp(2), scopeRc.right - dp(92), scopeRc.top + dp(20)),
                          toUTF32(truncateLabel(sLbl, 28)), pal.fgText);
        drawThemeCard(buf, m.sampleLoadRc, pal.accentOrange, pal.borderSubtle, pal, 1.5f, false);
        drawCenteredText(buf, uiFnt, m.sampleLoadRc, "LOAD WAV"d, pal.bgWindow);
    } else if (isDrumPlugin(plug)) {
        // 8-pad trigger strip directly inside scopeRc
        const int padCount = 8;
        const int pGap = dp(4);
        const int pw = (sw - (padCount - 1) * pGap) / padCount;
        foreach (pi; 0 .. padCount) {
            const int px0 = scopeRc.left + scopePad + pi * (pw + pGap);
            Rect prc = Rect(px0, scopeRc.top + scopePad, px0 + pw, scopeRc.bottom - scopePad);
            bool active = false;
            string pName = format("P%d", pi + 1);
            if (pi < cast(int)plug.drumPads.length) {
                active = plug.drumPads[pi].activeFlash;
                if (plug.drumPads[pi].sampleName.length > 0) {
                    pName = truncateLabel(plug.drumPads[pi].sampleName, 5);
                }
            }
            drawThemeCard(buf, prc, active ? pal.accentOrange : pal.bgControl,
                          pal.borderSubtle, pal, 2.0f, pal.retroBevel);
            drawCenteredText(buf, uiFnt, prc, toUTF32(pName), active ? pal.bgWindow : pal.fgText);
        }
    } else if (plug.hasMetering) {
        // Compressor / Limiter / HOTT live dB meters (IN, OUT, GR)
        const float inNorm = normalizeDb(plug.meterInDb, -60.0f, 0.0f);
        const float outNorm = normalizeDb(plug.meterOutDb, -60.0f, 0.0f);
        const float grNorm = normalizeDb(-plug.meterGrDb, 0.0f, 24.0f);
        const int barX = scopeRc.left + dp(44);
        const int barW = scopeRc.width - dp(104);
        const int rowH = (scopeRc.height - dp(12)) / 3;
        static immutable string[3] LBLS = ["IN", "OUT", "GR"];
        float[3] vals = [inNorm, outNorm, grNorm];
        float[3] dbs = [plug.meterInDb, plug.meterOutDb, plug.meterGrDb];
        uint[3] cols = [pal.accentCyan, pal.accentGreen, pal.accentOrange];
        foreach (ri; 0 .. 3) {
            const int y0 = scopeRc.top + dp(6) + ri * rowH;
            Rect lblR = Rect(scopeRc.left + dp(8), y0, barX, y0 + rowH - dp(2));
            Rect trkR = Rect(barX, y0 + dp(3), barX + barW, y0 + rowH - dp(5));
            Rect valR = Rect(barX + barW + dp(6), y0, scopeRc.right - dp(6), y0 + rowH - dp(2));
            drawVCenteredText(buf, valFnt, lblR.left, lblR, toUTF32(LBLS[ri]), pal.fgMuted);
            buf.fillRect(trkR, pal.bgWindow);
            const int fillW = cast(int)(vals[ri] * trkR.width);
            if (fillW > 0) buf.fillRect(Rect(trkR.left, trkR.top, trkR.left + fillW, trkR.bottom), cols[ri]);
            drawVCenteredText(buf, valFnt, valR.left, valR, toUTF32(format("%.1fdB", dbs[ri])), pal.fgText);
        }
    } else if (sw > dp(24) && sh > dp(16)) {
        const float p0 = (plug.params.length > 0) ? plug.params[0].currentValue : 0.65f;
        const float p1 = (plug.params.length > 1) ? plug.params[1].currentValue : 0.35f;
        const float p2 = (plug.params.length > 2) ? plug.params[2].currentValue : 0.50f;
        const float cutoffX = scopeRc.left + scopePad + p0 * sw;
        float prevX = scopeRc.left + scopePad;
        float prevY = scopeRc.bottom - scopePad - sh * 0.55f;
        enum int STEPS = 48;
        foreach (i; 1 .. STEPS + 1) {
            const float t = cast(float)i / cast(float)STEPS;
            const float x = scopeRc.left + scopePad + t * sw;
            float resp;
            if (plug.isInstrument) {
                const float dist = (x - cutoffX) / (sw * 0.14f);
                const float pass = (t < p0) ? 0.55f : 0.55f * expf(-(t - p0) * 7.5f);
                const float resPeak = p1 * 0.42f * expf(-dist * dist);
                resp = pass + resPeak;
            } else {
                resp = 0.48f + (p0 - 0.5f) * 0.35f * cosf(t * 4.5f + p1 * 3.14f) +
                       (p2 - 0.5f) * 0.2f;
            }
            if (resp < 0.04f) resp = 0.04f;
            if (resp > 0.96f) resp = 0.96f;
            const float y = scopeRc.bottom - scopePad - resp * sh;
            drawThickLine(buf, prevX, prevY, x, y, dpf(2.0f),
                          plug.isInstrument ? pal.accentOrange : pal.accentCyan);
            prevX = x;
            prevY = y;
        }
    }

    // 3. Interactive Rotary Knob Grid (up to 8 params in 2 rows x 4 cols)
    const int cols = 4;
    const int cellW = (rc.width - dp(20)) / cols;
    const int cellH = m.gridH / 2;
    const size_t count = plug.params.length > 8 ? 8 : plug.params.length;
    const int knobR = (cellH >= dp(68))
        ? dp(17)
        : ((cellH - dp(26)) / 2 > dp(10) ? (cellH - dp(26)) / 2 : dp(10));

    foreach (i; 0 .. count) {
        const int col = cast(int)i % cols;
        const int row = cast(int)i / cols;
        const int cellLeft = rc.left + dp(10) + col * cellW;
        const int cx = cellLeft + cellW / 2;
        const int cy = m.gridTop + row * cellH + cellH / 2 - (cellH >= dp(68) ? dp(2) : dp(5));
        const auto pm = plug.params[i];
        const uint arcCol = (i % 2 == 0) ? pal.accentOrange : pal.accentCyan;

        drawRotaryKnob(buf, cx, cy, knobR, pm.currentValue,
                       pal.bgLcd, arcCol, pal.bgControl, pal.borderSubtle, false);

        const string lbl = truncateLabel(pm.name, 11);
        const string valStr = format("%d%%", cast(int)(pm.currentValue * 100.0f + 0.5f));
        Rect lblRc = Rect(cellLeft, cy + knobR + dp(2), cellLeft + cellW, cy + knobR + dp(18));
        drawCenteredText(buf, uiFnt, lblRc, toUTF32(lbl), pal.fgText);
        if (cellH >= dp(68)) {
            Rect valRc = Rect(cellLeft, cy - knobR - dp(16), cellLeft + cellW, cy - knobR - dp(1));
            drawCenteredText(buf, valFnt, valRc, toUTF32(valStr), pal.fgMuted);
        } else {
            Rect valRc = Rect(cx + knobR + dp(3), cy - dp(8), cellLeft + cellW, cy + dp(8));
            drawVCenteredText(buf, valFnt, cx + knobR + dp(3), valRc, toUTF32(valStr), pal.fgMuted);
        }
    }
}

/// Interactive Floating FL Studio Fruity Wrapper Window Widget.
class PluginWrapperWidget : Widget {
    private TransportModel* model_;
    WrapperTargetKind targetKind = WrapperTargetKind.CHANNEL_GENERATOR;
    int targetIndex = 3; // Acid Bass by default
    int targetFxSlot = 0;

    private int dragParamIdx_ = -1;
    private int dragStartY_ = 0;
    private float dragStartVal_ = 0.0f;
    private BuiltinDeviceUiState builtinUi_;
    private bool showRawParams_ = false;
    private ColorDrawBuf remoteBuf_;
    private const(PluginSlotState)* lastSyncedPlug_ = null;
    private uint lastSyncedRevision_ = 0;

    void delegate(int trackIdx, int pluginIdx, uint paramId, float value) onSetParam;
    void delegate(int trackIdx, int pluginIdx, bool enabled) onSetBypass;
    void delegate(int trackIdx, int pluginIdx) onShowNativeGui;
    void delegate(int trackIdx, int pluginIdx, bool open) onToggleRemoteEditor;
    void delegate(int trackIdx, int pluginIdx, int inputType, int x, int y, int btn, int key, int delta) onEditorInput;
    void delegate(int trackIdx, int pluginIdx) onLoadSample;
    void delegate(int trackIdx, uint notePitch) onTriggerPad;
    void delegate(int trackIdx, int pluginIdx, int padIdx) onPromptPadSample;

    this(string id, TransportModel* model) {
        super(id);
        model_ = model;
        focusable = true;
        layoutWidth = FILL_PARENT;
        layoutHeight = FILL_PARENT;
    }

    void setTargetChannel(int channelIdx) {
        if (targetKind != WrapperTargetKind.CHANNEL_GENERATOR || targetIndex != channelIdx) {
            builtinUi_ = BuiltinDeviceUiState.init;
            showRawParams_ = false;
            dragParamIdx_ = -1;
        }
        targetKind = WrapperTargetKind.CHANNEL_GENERATOR;
        targetIndex = channelIdx;
        invalidate();
    }

    void setTargetMixerFx(int mixerInsert, int slotIdx) {
        if (targetKind != WrapperTargetKind.MIXER_FX || targetIndex != mixerInsert || targetFxSlot != slotIdx) {
            builtinUi_ = BuiltinDeviceUiState.init;
            showRawParams_ = false;
            dragParamIdx_ = -1;
        }
        targetKind = WrapperTargetKind.MIXER_FX;
        targetIndex = mixerInsert;
        targetFxSlot = slotIdx;
        invalidate();
    }

    int resolvedTrackIndex() const nothrow @safe {
        return resolveTargetMixerTrack();
    }

    private int resolveTargetMixerTrack() const nothrow @safe {
        if (!model_) return 0;
        if (targetKind == WrapperTargetKind.MIXER_FX) {
            return targetIndex;
        }
        if (targetIndex >= 0 && targetIndex < cast(int)model_.channels.length) {
            return 100 + targetIndex;
        }
        return model_.selectedTrack;
    }

    private void syncRemoteFrameBuffer(const(PluginSlotState)* plug) {
        if (!plug || !plug.remoteEditorOpen || plug.editorW <= 0 || plug.editorH <= 0 ||
            plug.editorRgba.length < cast(size_t)(plug.editorW * plug.editorH * 4)) {
            return;
        }
        if (lastSyncedPlug_ == plug && lastSyncedRevision_ == plug.editorRevision &&
            remoteBuf_ !is null && remoteBuf_.width == plug.editorW && remoteBuf_.height == plug.editorH) {
            return;
        }
        if (remoteBuf_ is null || remoteBuf_.width != plug.editorW || remoteBuf_.height != plug.editorH) {
            if (remoteBuf_ !is null) destroy(remoteBuf_);
            remoteBuf_ = new ColorDrawBuf(plug.editorW, plug.editorH);
        }
        foreach (y; 0 .. plug.editorH) {
            uint* row = remoteBuf_.scanLine(y);
            const size_t base = cast(size_t)y * cast(size_t)plug.editorW * 4;
            foreach (x; 0 .. plug.editorW) {
                const size_t off = base + cast(size_t)x * 4;
                const uint r = plug.editorRgba[off];
                const uint g = plug.editorRgba[off + 1];
                const uint b = plug.editorRgba[off + 2];
                row[x] = (r << 16) | (g << 8) | b;
            }
        }
        lastSyncedPlug_ = plug;
        lastSyncedRevision_ = plug.editorRevision;
    }

    override void onDraw(DrawBuf buf) {
        if (visibility != Visibility.Visible) return;
        auto plug = resolveActiveWrapperPlugin(model_, targetKind, targetIndex, targetFxSlot);
        syncRemoteFrameBuffer(plug);
        drawPluginWrapperCanvas(buf, _pos, model_, plug, safeWidgetFont(this), remoteBuf_, &builtinUi_,
                                showRawParams_);
    }

    private void forwardEditorMouse(const(PluginSlotState)* plug, int inputType, int wx, int wy, int btn, int delta) {
        if (!plug || plug.editorW <= 0 || plug.editorH <= 0 || onEditorInput is null) return;
        const m = computePluginWrapperMetrics(_pos);
        const dstFit = computeRemoteEditorFitRect(m.editorDstRc, plug.editorW, plug.editorH);
        if (dstFit.width <= 0 || dstFit.height <= 0) return;
        int rx = (wx - dstFit.left) * plug.editorW / dstFit.width;
        int ry = (wy - dstFit.top) * plug.editorH / dstFit.height;
        if (rx < 0) rx = 0; else if (rx >= plug.editorW) rx = plug.editorW - 1;
        if (ry < 0) ry = 0; else if (ry >= plug.editorH) ry = plug.editorH - 1;
        onEditorInput(resolveTargetMixerTrack(), plug.pluginIndex, inputType, rx, ry, btn, 0, delta);
    }

    override bool onKeyEvent(KeyEvent event) {
        if (!model_) return super.onKeyEvent(event);
        auto plug = resolveActiveWrapperPlugin(model_, targetKind, targetIndex, targetFxSlot);
        if (plug && plug.remoteEditorOpen && onEditorInput !is null) {
            const int it = (event.action == KeyAction.KeyDown) ? 3
                         : ((event.action == KeyAction.KeyUp) ? 4 : -1);
            if (it >= 0) {
                onEditorInput(resolveTargetMixerTrack(), plug.pluginIndex, it, 0, 0, 0, cast(int)event.keyCode, 0);
                return true;
            }
        }
        return super.onKeyEvent(event);
    }

    override bool onMouseEvent(MouseEvent event) {
        const bool hasCustomDrag = builtinUi_.activeDragParamId >= 0 ||
                                   builtinUi_.eqDragBand >= 0 ||
                                   builtinUi_.hottDragBand >= 0 ||
                                   builtinUi_.envDragPoint >= 0;
        if (event.action == MouseAction.FocusOut) {
            return dragParamIdx_ >= 0 || hasCustomDrag;
        }
        if (!model_) return super.onMouseEvent(event);
        auto plug = resolveActiveWrapperPlugin(model_, targetKind, targetIndex, targetFxSlot);
        if (!plug) return super.onMouseEvent(event);

        const m = computePluginWrapperMetrics(_pos);

        if (event.action == MouseAction.ButtonDown) {
            activateParentWindow(this);

            // Header bar buttons
            if (event.y >= m.hdrRc.top && event.y < m.hdrRc.bottom) {
                const int trk = resolveTargetMixerTrack();
                if (m.prevRc.isPointInside(event.x, event.y) && plug.presets.length > 0) {
                    plug.presetIndex = (plug.presetIndex + cast(int)plug.presets.length - 1) % cast(int)plug.presets.length;
                    applyPluginPresetValues(*plug, plug.presetIndex);
                    if (onSetParam !is null) {
                        foreach (const ref pm; plug.params) {
                            onSetParam(trk, plug.pluginIndex, pm.id, pm.currentValue);
                        }
                    }
                    model_.hintText = format("%s Preset: %s", plug.name, plug.presets[plug.presetIndex]);
                    invalidate();
                    return true;
                }
                if (m.nextRc.isPointInside(event.x, event.y) && plug.presets.length > 0) {
                    plug.presetIndex = (plug.presetIndex + 1) % cast(int)plug.presets.length;
                    applyPluginPresetValues(*plug, plug.presetIndex);
                    if (onSetParam !is null) {
                        foreach (const ref pm; plug.params) {
                            onSetParam(trk, plug.pluginIndex, pm.id, pm.currentValue);
                        }
                    }
                    model_.hintText = format("%s Preset: %s", plug.name, plug.presets[plug.presetIndex]);
                    invalidate();
                    return true;
                }
                if (m.guiRc.isPointInside(event.x, event.y)) {
                    if (hasCustomBuiltinDeviceUi(plug)) {
                        if (plug.remoteEditorOpen) {
                            plug.remoteEditorOpen = false;
                            if (onToggleRemoteEditor !is null) {
                                onToggleRemoteEditor(trk, plug.pluginIndex, false);
                            }
                            showRawParams_ = false;
                        } else {
                            showRawParams_ = !showRawParams_;
                        }
                        dragParamIdx_ = -1;
                        builtinUi_.activeDragParamId = -1;
                        builtinUi_.eqDragBand = -1;
                        builtinUi_.hottDragBand = -1;
                        builtinUi_.envDragPoint = -1;
                        model_.hintText = showRawParams_
                            ? format("%s: Raw parameter knobs", plug.name)
                            : format("%s: Custom device UI", plug.name);
                        invalidate();
                        return true;
                    }
                    if (plug.uri.startsWith("builtin://") || plug.uri.length == 0) {
                        if (plug.remoteEditorOpen) {
                            plug.remoteEditorOpen = false;
                            if (onToggleRemoteEditor !is null) {
                                onToggleRemoteEditor(trk, plug.pluginIndex, false);
                            }
                        } else {
                            model_.hintText = "Built-in plugin uses inline knobs";
                        }
                        invalidate();
                        return true;
                    }
                    const bool nextOpen = !plug.remoteEditorOpen;
                    plug.remoteEditorOpen = nextOpen;
                    if (onToggleRemoteEditor !is null) {
                        onToggleRemoteEditor(trk, plug.pluginIndex, nextOpen);
                    } else if (nextOpen && onShowNativeGui !is null) {
                        onShowNativeGui(trk, plug.pluginIndex);
                    }
                    invalidate();
                    return true;
                }
                if (m.bypRc.isPointInside(event.x, event.y)) {
                    plug.enabled = !plug.enabled;
                    if (onSetBypass !is null) onSetBypass(trk, plug.pluginIndex, plug.enabled);
                    invalidate();
                    return true;
                }
                return true;
            }

            // Remote VST3 editor framebuffer mouse down forwarding
            if (hasActiveRemoteEditorFrame(plug) && event.y >= m.hdrRc.bottom) {
                const int btn = (event.button == MouseButton.Right) ? 1 : 0;
                forwardEditorMouse(plug, 1, event.x, event.y, btn, 0);
                return true;
            }
        }

        // Builtin Swing-ported custom device UI mouse interactions
        if (!hasActiveRemoteEditorFrame(plug) && hasCustomBuiltinDeviceUi(plug) && !showRawParams_ &&
            (event.y >= m.hdrRc.bottom || hasCustomDrag)) {
            Rect bodyRc = Rect(_pos.left, m.hdrRc.bottom, _pos.right, _pos.bottom);
            const int trk = resolveTargetMixerTrack();
            const bool handled = handleBuiltinDeviceMouse(
                event, bodyRc, model_, plug, builtinUi_, trk,
                onSetParam, onTriggerPad, onPromptPadSample, onLoadSample,
                (string msg) { model_.hintText = msg; });
            if (handled) {
                invalidate();
                return true;
            }
            if (event.action == MouseAction.ButtonDown) {
                return true;
            }
        }

        if (event.action == MouseAction.ButtonDown) {
            // Sampler [LOAD WAV] button hit test
            if (isSamplerPlugin(plug) && m.sampleLoadRc.isPointInside(event.x, event.y)) {
                if (onLoadSample !is null) onLoadSample(resolveTargetMixerTrack(), plug.pluginIndex);
                return true;
            }

            // Drum Machine 8-pad trigger (left-click) / load sample (right-click) hit test inside scopeRc
            if (isDrumPlugin(plug) && m.scopeRc.isPointInside(event.x, event.y)) {
                const int scopePad = dp(8);
                const int sw = m.scopeRc.width - scopePad * 2;
                if (sw > 0) {
                    const int pIdx = (event.x - (m.scopeRc.left + scopePad)) * 8 / sw;
                    if (pIdx >= 0 && pIdx < 8) {
                        if (event.button == MouseButton.Right) {
                            if (onPromptPadSample !is null) {
                                onPromptPadSample(resolveTargetMixerTrack(), plug.pluginIndex, pIdx);
                            }
                        } else {
                            uint notePitch = cast(uint)(36 + pIdx);
                            if (pIdx < cast(int)plug.drumPads.length) {
                                plug.drumPads[pIdx].activeFlash = true;
                                if (plug.drumPads[pIdx].triggerNote > 0) notePitch = plug.drumPads[pIdx].triggerNote;
                            }
                            if (onTriggerPad !is null) onTriggerPad(resolveTargetMixerTrack(), notePitch);
                        }
                        invalidate();
                        return true;
                    }
                }
            }

            // Knob grid hit test
            if (event.y >= m.gridTop && m.gridH > dp(40) && _pos.width > dp(20)) {
                const int col = (event.x - (_pos.left + dp(10))) * 4 / (_pos.width - dp(20));
                const int row = (event.y - m.gridTop) * 2 / m.gridH;
                const int idx = row * 4 + col;
                if (col >= 0 && col < 4 && row >= 0 && row < 2 && idx < cast(int)plug.params.length) {
                    if (event.button == MouseButton.Right) {
                        plug.params[idx].currentValue = plug.params[idx].defaultValue;
                        const int trk = resolveTargetMixerTrack();
                        if (onSetParam !is null) {
                            onSetParam(trk, plug.pluginIndex, plug.params[idx].id, plug.params[idx].currentValue);
                        }
                        invalidate();
                    } else {
                        dragParamIdx_ = idx;
                        dragStartY_ = event.y;
                        dragStartVal_ = plug.params[idx].currentValue;
                    }
                    return true;
                }
            }
            return true;
        }

        if (hasActiveRemoteEditorFrame(plug) && event.y >= m.hdrRc.bottom) {
            if (event.action == MouseAction.Move) {
                forwardEditorMouse(plug, 0, event.x, event.y, 0, 0);
                return true;
            }
            if (event.action == MouseAction.ButtonUp) {
                const int btn = (event.button == MouseButton.Right) ? 1 : 0;
                forwardEditorMouse(plug, 2, event.x, event.y, btn, 0);
                return true;
            }
            if (event.action == MouseAction.Wheel) {
                forwardEditorMouse(plug, 5, event.x, event.y, 0, event.wheelDelta);
                return true;
            }
        }

        if (event.action == MouseAction.Move && dragParamIdx_ >= 0 &&
            dragParamIdx_ < cast(int)plug.params.length) {
            const float delta = cast(float)(dragStartY_ - event.y) / dpf(110.0f);
            float v = dragStartVal_ + delta;
            if (v < 0.0f) v = 0.0f;
            if (v > 1.0f) v = 1.0f;
            plug.params[dragParamIdx_].currentValue = v;
            model_.hintText = format("%s — %s: %d%%", plug.name, plug.params[dragParamIdx_].name,
                                     cast(int)(v * 100.0f + 0.5f));
            const int trk = resolveTargetMixerTrack();
            if (onSetParam !is null) {
                onSetParam(trk, plug.pluginIndex, plug.params[dragParamIdx_].id, v);
            }
            invalidate();
            return true;
        }

        if ((event.action == MouseAction.ButtonUp || event.action == MouseAction.Cancel) &&
            dragParamIdx_ >= 0) {
            dragParamIdx_ = -1;
            return true;
        }

        return super.onMouseEvent(event);
    }
}

unittest {
    import dlangui.graphics.drawbuf : ColorDrawBuf;
    import pb.notifications : DrumPadNotification, EditorFrameData, Notification,
        PluginMeteringData, PluginSampleData, PluginScopeData, PluginSpectrumData;

    TransportModel model;
    model.initDefaultTemplate();
    auto plug = resolveActiveWrapperPlugin(&model, WrapperTargetKind.CHANNEL_GENERATOR, 3, 0);
    assert(plug !is null && plug.name == "Acid Bass");

    auto buf = new ColorDrawBuf(440, 260);
    drawPluginWrapperCanvas(buf, Rect(0, 0, 440, 260), &model, plug);
    assert(buf.scanLine(12)[200] == model.palette().bgLcd);

    // Regression: CHANNEL_GENERATOR param edits must target generator track (100 + chIdx = 103), not model.selectedTrack (0)
    import dlangui.widgets.styles : currentTheme, createDefaultTheme;
    import gui_d.theme : destroyWidgetTree;
    if (currentTheme is null) currentTheme = createDefaultTheme();

    model.selectedTrack = 0; // Master
    auto pw = new PluginWrapperWidget("pw", &model);
    scope (exit) destroyWidgetTree(pw);
    pw.setTargetChannel(3); // Acid Bass -> engine track 103 (routed to Insert 4)
    pw.measure(440, 280);
    pw.layout(Rect(0, 0, 440, 280));
    int sentTrack = -1;
    int paramUpdates = 0;
    pw.onSetParam = (int trkIdx, int plugIdx, uint pid, float v) {
        sentTrack = trkIdx;
        ++paramUpdates;
    };
    // Phase 15: Builtin plugin opens custom Swing-ported UI directly without extra EDITOR click
    assert(!pw.showRawParams_ && hasCustomBuiltinDeviceUi(plug));
    const dlAcid = buildBuiltinDeviceLayout(Rect(0, WRAP_HDR_H, 440, 280), plug, pw.builtinUi_, model.palette());
    assert(dlAcid.knobs.length >= 1);
    const short acKx = cast(short)dlAcid.knobs[0].cx;
    const short acKy = cast(short)dlAcid.knobs[0].cy;
    pw.onMouseEvent(new MouseEvent(MouseAction.ButtonDown, MouseButton.Left, MouseFlag.LButton, acKx, acKy));
    pw.onMouseEvent(new MouseEvent(MouseAction.Move, MouseButton.None, MouseFlag.LButton, acKx, cast(short)(acKy - 18)));
    assert(sentTrack == 103, "Expected custom Acid Bass knob change to target generator track 103");
    pw.onMouseEvent(new MouseEvent(MouseAction.Cancel, MouseButton.None, 0, acKx, cast(short)(acKy - 18)));
    sentTrack = -1;
    pw.onMouseEvent(new MouseEvent(MouseAction.Move, MouseButton.None, 0, acKx, cast(short)(acKy + 25)));
    assert(sentTrack == -1, "Expected MouseAction.Cancel to clear custom builtin knob drag");

    // Toggle PARAMS button to verify raw knob fallback & reset behavior
    pw.onMouseEvent(new MouseEvent(MouseAction.ButtonDown, MouseButton.Left, MouseFlag.LButton, cast(short)(440 - 50), cast(short)16));
    assert(pw.showRawParams_ && !plug.remoteEditorOpen, "Expected PARAMS click on builtin plugin to show raw parameter knobs");

    // Click and drag param 0 knob in raw param view
    const int gridTop = WRAP_HDR_H + 8 + SCOPE_H + 10;
    pw.onMouseEvent(new MouseEvent(MouseAction.ButtonDown, MouseButton.Left, MouseFlag.LButton, cast(short)60, cast(short)(gridTop + 20)));
    pw.onMouseEvent(new MouseEvent(MouseAction.Move, MouseButton.None, MouseFlag.LButton, cast(short)60, cast(short)(gridTop + 5)));
    assert(sentTrack == 103, "Expected CHANNEL_GENERATOR param change to target generator track 103");

    // Cancel ends knob drag cleanly
    pw.onMouseEvent(new MouseEvent(MouseAction.Cancel, MouseButton.None, 0, cast(short)60, cast(short)(gridTop + 5)));
    sentTrack = -1;
    pw.onMouseEvent(new MouseEvent(MouseAction.Move, MouseButton.None, 0, cast(short)60, cast(short)(gridTop + 40)));
    assert(sentTrack == -1, "Expected MouseAction.Cancel to clear knob drag");

    // Toggle back to custom UI
    pw.onMouseEvent(new MouseEvent(MouseAction.ButtonDown, MouseButton.Left, MouseFlag.LButton, cast(short)(440 - 50), cast(short)16));
    assert(!pw.showRawParams_, "Expected second PARAMS click to return to custom device UI");

    // Phase 4: Preset '>' selector updates presetIndex & syncs params via onSetParam
    const int prevPreset = plug.presetIndex;
    paramUpdates = 0;
    pw.onMouseEvent(new MouseEvent(MouseAction.ButtonDown, MouseButton.Left, MouseFlag.LButton, cast(short)298, cast(short)16));
    assert(plug.presetIndex == prevPreset + 1 && paramUpdates == cast(int)plug.params.length,
           "Expected Preset '>' click to advance preset and emit param updates");

    // Phase 4: Spectrum, Metering, Scope, Sampler, DrumPad & Remote Editor Framebuffer telemetry
    Notification nSpec;
    PluginSpectrumData psd;
    psd.track_index = 103;
    psd.plugin_index = 0;
    psd.input_magnitudes = [-30.0f, -12.0f, -6.0f, -24.0f];
    psd.output_magnitudes = [-20.0f, -4.0f, -3.0f, -18.0f];
    nSpec.plugin_spectrum.set(psd);
    model.applyNotification(nSpec);
    assert(plug.specOutDb.length == 4);
    drawPluginWrapperCanvas(buf, Rect(0, 0, 440, 260), &model, plug);

    Notification nEf;
    EditorFrameData efd;
    efd.track_index = 103;
    efd.plugin_index = 0;
    efd.width = 4;
    efd.height = 4;
    efd.image_data = new ubyte[4 * 4 * 4];
    efd.image_data[] = 200;
    nEf.editor_frame_data.set(efd);
    model.applyNotification(nEf);
    assert(plug.remoteEditorOpen && plug.editorW == 4);
    int edInputs = 0;
    pw.onEditorInput = (int t, int p, int it, int x, int y, int b, int k, int d) { ++edInputs; };
    pw.onMouseEvent(new MouseEvent(MouseAction.ButtonDown, MouseButton.Left, MouseFlag.LButton, cast(short)120, cast(short)80));
    assert(edInputs == 1, "Expected mouse click over open remote editor to forward input");

    // Turn off remote editor via EDITOR button
    bool edState = true;
    pw.onToggleRemoteEditor = (int t, int p, bool open) { edState = open; };
    pw.onMouseEvent(new MouseEvent(MouseAction.ButtonDown, MouseButton.Left, MouseFlag.LButton, cast(short)(440 - 50), cast(short)16));
    assert(!edState && !plug.remoteEditorOpen, "Expected EDITOR click to toggle remote editor off");

    // Regression: Clicking PARAMS/EDITOR on a built-in plugin (builtin://...) must NOT activate remoteEditorOpen polling
    assert(plug.uri == "builtin://acid_bass");
    edState = false;
    pw.onMouseEvent(new MouseEvent(MouseAction.ButtonDown, MouseButton.Left, MouseFlag.LButton, cast(short)(440 - 50), cast(short)16));
    assert(!plug.remoteEditorOpen && !edState,
           "Expected PARAMS button on builtin:// plugin not to enter remoteEditorOpen polling state");
    pw.onMouseEvent(new MouseEvent(MouseAction.ButtonDown, MouseButton.Left, MouseFlag.LButton, cast(short)(440 - 50), cast(short)16));
    assert(!pw.showRawParams_);

    // External headless/GUI-less plugin falls back cleanly when remoteEditorOpen is off or editorW == 0
    PluginSlotState extPlug;
    extPlug.uri = "vst3://external-headless";
    extPlug.name = "Headless VST3";
    assert(!hasCustomBuiltinDeviceUi(&extPlug));
    drawPluginWrapperCanvas(buf, Rect(0, 0, 440, 260), &model, &extPlug);

    // Regression: syncRemoteFrameBuffer caches ColorDrawBuf across identical editorRevision frames
    plug.remoteEditorOpen = true;
    const uint revBefore = plug.editorRevision;
    pw.syncRemoteFrameBuffer(plug);
    auto firstBuf = pw.remoteBuf_;
    assert(firstBuf !is null && pw.lastSyncedRevision_ == revBefore);
    pw.syncRemoteFrameBuffer(plug);
    assert(pw.remoteBuf_ is firstBuf && pw.lastSyncedRevision_ == revBefore);
    model.applyNotification(nEf);
    assert(plug.editorRevision == revBefore + 1);
    pw.syncRemoteFrameBuffer(plug);
    assert(pw.lastSyncedRevision_ == revBefore + 1);
}
