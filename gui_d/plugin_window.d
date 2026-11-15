module gui_d.plugin_window;

import core.stdc.math : cosf, expf;
import dlangui;
import dlangui.graphics.drawbuf : DrawBuf;
import dlangui.graphics.fonts : FontWeight;
import dlangui.widgets.widget : Visibility, Widget;
import gui_d.icons : DawIcon, drawDawIcon;
import gui_d.theme : dp, dpf, drawCard, drawCenteredText, drawRotaryKnob, drawThickLine,
    drawVCenteredText, FlPalette, getDawFont, getDawMonoFont, getFlPalette, safeWidgetFont, truncateLabel;
import gui_d.transport_model : ParamItemState, PluginSlotState, TransportModel;
import gui_d.workspace : activateParentWindow;
import std.format : format;
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
    m.gridTop = m.scopeRc.bottom + dp(10);
    m.gridH = rc.bottom - m.gridTop - dp(8);
    return m;
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

/// Renders the FL Studio 2026 Fruity Wrapper Plugin Editor canvas.
void drawPluginWrapperCanvas(
    DrawBuf buf,
    Rect rc,
    const(TransportModel)* model,
    const(PluginSlotState)* plug,
    FontRef fnt = FontRef.init
) {
    const pal = model ? model.palette() : getFlPalette();
    buf.fillRect(rc, pal.bgWindow);
    if (rc.width < dp(260) || rc.height < dp(160)) return;

    const m = computePluginWrapperMetrics(rc);
    FontRef boldFnt = getDawFont(11, fnt, FontWeight.Bold);
    FontRef uiFnt = getDawFont(10, fnt, FontWeight.Normal);
    FontRef valFnt = getDawMonoFont(9, fnt, FontWeight.Normal);

    // 1. Wrapper Header Bar (< Preset >, VST3 GUI, Mix, Bypass)
    buf.fillRect(m.hdrRc, pal.bgPanelAlt);
    buf.fillRect(Rect(m.hdrRc.left, m.hdrRc.bottom - dp(1), m.hdrRc.right, m.hdrRc.bottom), pal.borderStrong);

    const string plugTitle = plug ? plug.name : "No Plugin Loaded";
    drawVCenteredText(buf, boldFnt, m.hdrRc.left + dp(10), m.hdrRc,
                      toUTF32(truncateLabel(plugTitle, 18)), pal.accentOrange);

    // Unified flush Preset selector < Preset >
    drawCard(buf, Rect(m.prevRc.left, m.prevRc.top, m.nextRc.right, m.nextRc.bottom),
             pal.bgControl, COLOR_TRANSPARENT, 1.5f);
    drawCard(buf, m.presRc, pal.bgLcd, COLOR_TRANSPARENT, 0.0f);

    string presetName = "Default";
    if (plug && plug.presets.length > 0) {
        const size_t pi = plug.presetIndex % plug.presets.length;
        presetName = plug.presets[pi];
    }
    drawDawIcon(buf, Rect(m.prevRc.left + dp(3), m.prevRc.top + dp(3), m.prevRc.right - dp(3), m.prevRc.bottom - dp(3)),
                DawIcon.CHEVRON_LEFT, pal.fgText);
    drawVCenteredText(buf, uiFnt, m.presRc.left + dp(6), m.presRc,
                      toUTF32(truncateLabel(presetName, 14)), pal.accentCyan);
    drawDawIcon(buf, Rect(m.nextRc.left + dp(3), m.nextRc.top + dp(3), m.nextRc.right - dp(3), m.nextRc.bottom - dp(3)),
                DawIcon.CHEVRON_RIGHT, pal.fgText);

    // Unified flush VST3 GUI + Bypass strip
    drawCard(buf, Rect(m.guiRc.left, m.guiRc.top, m.bypRc.right, m.bypRc.bottom),
             pal.bgControl, COLOR_TRANSPARENT, 1.5f);
    drawCard(buf, m.bypRc, (plug && plug.enabled) ? pal.accentGreen : pal.ledOff, COLOR_TRANSPARENT, 1.5f);
    drawCenteredText(buf, uiFnt, m.guiRc, "EDITOR"d, pal.fgText);

    if (!plug) return;

    // 2. DSP Visual Response Curve Box
    Rect scopeRc = m.scopeRc;
    drawCard(buf, scopeRc, pal.bgLcd, pal.borderSubtle, 2.0f);

    const float p0 = (plug.params.length > 0) ? plug.params[0].currentValue : 0.65f;
    const float p1 = (plug.params.length > 1) ? plug.params[1].currentValue : 0.35f;
    const float p2 = (plug.params.length > 2) ? plug.params[2].currentValue : 0.50f;
    const int scopePad = dp(8);
    const int sw = scopeRc.width - scopePad * 2;
    const int sh = scopeRc.height - scopePad * 2;
    if (sw > dp(24) && sh > dp(16)) {
        const float cutoffX = scopeRc.left + scopePad + p0 * sw;
        float prevX = scopeRc.left + scopePad;
        float prevY = scopeRc.bottom - scopePad - sh * 0.55f;
        enum int STEPS = 48;
        foreach (i; 1 .. STEPS + 1) {
            const float t = cast(float)i / cast(float)STEPS;
            const float x = scopeRc.left + scopePad + t * sw;
            float resp;
            if (plug.isInstrument) {
                // Resonant low-pass filter response curve
                const float dist = (x - cutoffX) / (sw * 0.14f);
                const float pass = (t < p0) ? 0.55f : 0.55f * expf(-(t - p0) * 7.5f);
                const float resPeak = p1 * 0.42f * expf(-dist * dist);
                resp = pass + resPeak;
            } else {
                // Smooth FX / EQ spectral curve
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

    void delegate(int trackIdx, int pluginIdx, uint paramId, float value) onSetParam;
    void delegate(int trackIdx, int pluginIdx, bool enabled) onSetBypass;
    void delegate(int trackIdx, int pluginIdx) onShowNativeGui;

    this(string id, TransportModel* model) {
        super(id);
        model_ = model;
        layoutWidth = FILL_PARENT;
        layoutHeight = FILL_PARENT;
    }

    void setTargetChannel(int channelIdx) {
        targetKind = WrapperTargetKind.CHANNEL_GENERATOR;
        targetIndex = channelIdx;
        invalidate();
    }

    void setTargetMixerFx(int mixerInsert, int slotIdx) {
        targetKind = WrapperTargetKind.MIXER_FX;
        targetIndex = mixerInsert;
        targetFxSlot = slotIdx;
        invalidate();
    }

    private int resolveTargetMixerTrack() const nothrow @safe {
        if (!model_) return 0;
        if (targetKind == WrapperTargetKind.MIXER_FX) {
            return targetIndex;
        }
        if (targetIndex >= 0 && targetIndex < cast(int)model_.channels.length) {
            return model_.channels[targetIndex].mixerInsert;
        }
        return model_.selectedTrack;
    }

    override void onDraw(DrawBuf buf) {
        if (visibility != Visibility.Visible) return;
        auto plug = resolveActiveWrapperPlugin(model_, targetKind, targetIndex, targetFxSlot);
        drawPluginWrapperCanvas(buf, _pos, model_, plug, safeWidgetFont(this));
    }

    override bool onMouseEvent(MouseEvent event) {
        if (event.action == MouseAction.FocusOut) {
            return dragParamIdx_ >= 0;
        }
        if (!model_) return super.onMouseEvent(event);
        auto plug = resolveActiveWrapperPlugin(model_, targetKind, targetIndex, targetFxSlot);
        if (!plug) return super.onMouseEvent(event);

        const m = computePluginWrapperMetrics(_pos);

        if (event.action == MouseAction.ButtonDown) {
            activateParentWindow(this);

            // Header bar buttons
            if (event.y >= m.hdrRc.top && event.y < m.hdrRc.bottom) {
                if (m.prevRc.isPointInside(event.x, event.y) && plug.presets.length > 0) {
                    plug.presetIndex = (plug.presetIndex + cast(int)plug.presets.length - 1) % cast(int)plug.presets.length;
                    invalidate();
                    return true;
                }
                if (m.nextRc.isPointInside(event.x, event.y) && plug.presets.length > 0) {
                    plug.presetIndex = (plug.presetIndex + 1) % cast(int)plug.presets.length;
                    invalidate();
                    return true;
                }
                if (m.guiRc.isPointInside(event.x, event.y)) {
                    const int trk = resolveTargetMixerTrack();
                    if (onShowNativeGui !is null) onShowNativeGui(trk, plug.pluginIndex);
                    return true;
                }
                if (m.bypRc.isPointInside(event.x, event.y)) {
                    plug.enabled = !plug.enabled;
                    const int trk = resolveTargetMixerTrack();
                    if (onSetBypass !is null) onSetBypass(trk, plug.pluginIndex, plug.enabled);
                    invalidate();
                    return true;
                }
                return true;
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

    TransportModel model;
    model.initDefaultTemplate();
    auto plug = resolveActiveWrapperPlugin(&model, WrapperTargetKind.CHANNEL_GENERATOR, 3, 0);
    assert(plug !is null && plug.name == "Acid Bass");

    auto buf = new ColorDrawBuf(440, 260);
    drawPluginWrapperCanvas(buf, Rect(0, 0, 440, 260), &model, plug);
    assert(buf.scanLine(12)[200] == model.palette().bgLcd);

    // Regression: CHANNEL_GENERATOR param edits must target channel's mixerInsert (4), not model.selectedTrack (0)
    import dlangui.widgets.styles : currentTheme, createDefaultTheme;
    import gui_d.theme : destroyWidgetTree;
    if (currentTheme is null) currentTheme = createDefaultTheme();

    model.selectedTrack = 0; // Master
    auto pw = new PluginWrapperWidget("pw", &model);
    scope (exit) destroyWidgetTree(pw);
    pw.setTargetChannel(3); // Acid Bass -> mixerInsert = 4
    pw.measure(440, 280);
    pw.layout(Rect(0, 0, 440, 280));
    int sentTrack = -1;
    pw.onSetParam = (int trkIdx, int plugIdx, uint pid, float v) {
        sentTrack = trkIdx;
    };
    // Click and drag param 0 knob
    const int gridTop = WRAP_HDR_H + 8 + SCOPE_H + 10;
    pw.onMouseEvent(new MouseEvent(MouseAction.ButtonDown, MouseButton.Left, MouseFlag.LButton, cast(short)60, cast(short)(gridTop + 20)));
    pw.onMouseEvent(new MouseEvent(MouseAction.Move, MouseButton.None, MouseFlag.LButton, cast(short)60, cast(short)(gridTop + 5)));
    assert(sentTrack == 4, "Expected CHANNEL_GENERATOR param change to target channel's mixerInsert (4)");

    // Cancel ends knob drag cleanly
    pw.onMouseEvent(new MouseEvent(MouseAction.Cancel, MouseButton.None, 0, cast(short)60, cast(short)(gridTop + 5)));
    sentTrack = -1;
    pw.onMouseEvent(new MouseEvent(MouseAction.Move, MouseButton.None, 0, cast(short)60, cast(short)(gridTop + 40)));
    assert(sentTrack == -1, "Expected MouseAction.Cancel to clear knob drag");
}
