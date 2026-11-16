module gui_d.device_panels;

import core.stdc.math : atanf, cosf, expf, log10f, logf, powf, sinf;
import dlangui.core.events : MouseAction, MouseButton, MouseEvent, MouseFlag;
import dlangui.core.types : Rect;
import dlangui.graphics.colors : COLOR_TRANSPARENT;
import dlangui.graphics.drawbuf : DrawBuf;
import dlangui.graphics.fonts : FontRef, FontWeight;
import gui_d.theme : adjustColorBrightness, contrastTextColor, dp, dpf, drawCenteredText,
    drawFilledCircle, drawOpenArc, drawRotaryKnob, drawThemeCard, drawThickLine, drawVCenteredText,
    FlPalette, FlThemeId, getDawFont, getDawMonoFont, getFlPalette, ScopedRetroAntialias,
    truncateLabel;
import gui_d.transport_model : ParamItemState, PluginSlotState, TransportModel;
import std.algorithm : canFind;
import std.format : format;
import std.string : toLower;
import std.utf : toUTF32;

/// Identifies which custom builtin device UI should be rendered inside the Fruity Wrapper.
enum BuiltinDeviceKind {
    GENERIC,
    EQ,
    COMPRESSOR,
    LIMITER,
    HOTT,
    MAXIM,
    DELAY,
    REVERB,
    PHASER,
    CHORUS,
    BITCRUSHER,
    ENVELOPE_SHAPER,
    CONVOLVER,
    VOCODEY,
    STEREO_WIDTH,
    ACID_BASS,
    THREE_OSC,
    SAMPLER,
    EPIANO,
    ORGAN,
    DRUM_MACHINE,
    DR8_MODULE,
}

/// Maps a plugin URI/name to its custom builtin device UI variant (`GENERIC` if none).
BuiltinDeviceKind resolveBuiltinDeviceKind(const(PluginSlotState)* plug) pure nothrow @safe {
    if (!plug) return BuiltinDeviceKind.GENERIC;
    const string u = plug.uri;
    if (u == "builtin://eq") return BuiltinDeviceKind.EQ;
    if (u == "builtin://compressor") return BuiltinDeviceKind.COMPRESSOR;
    if (u == "builtin://limiter") return BuiltinDeviceKind.LIMITER;
    if (u == "builtin://hott") return BuiltinDeviceKind.HOTT;
    if (u == "builtin://maxim") return BuiltinDeviceKind.MAXIM;
    if (u == "builtin://delay") return BuiltinDeviceKind.DELAY;
    if (u == "builtin://reverb") return BuiltinDeviceKind.REVERB;
    if (u == "builtin://phaser") return BuiltinDeviceKind.PHASER;
    if (u == "builtin://chorus") return BuiltinDeviceKind.CHORUS;
    if (u == "builtin://bitcrusher") return BuiltinDeviceKind.BITCRUSHER;
    if (u == "builtin://envelope_shaper") return BuiltinDeviceKind.ENVELOPE_SHAPER;
    if (u == "builtin://convolver") return BuiltinDeviceKind.CONVOLVER;
    if (u == "builtin://vocodey") return BuiltinDeviceKind.VOCODEY;
    if (u == "builtin://stereo_width") return BuiltinDeviceKind.STEREO_WIDTH;
    if (u == "builtin://acid_bass") return BuiltinDeviceKind.ACID_BASS;
    if (u == "builtin://3xosc") return BuiltinDeviceKind.THREE_OSC;
    if (u == "builtin://sampler") return BuiltinDeviceKind.SAMPLER;
    if (u == "builtin://epiano") return BuiltinDeviceKind.EPIANO;
    if (u == "builtin://organ") return BuiltinDeviceKind.ORGAN;
    if (u == "builtin://drum_machine") return BuiltinDeviceKind.DRUM_MACHINE;
    if (u.length >= 14 && u[0 .. 14] == "builtin://dr8_") return BuiltinDeviceKind.DR8_MODULE;
    return BuiltinDeviceKind.GENERIC;
}

/// Returns true when the builtin device provides a dedicated Swing-ported custom panel.
bool hasCustomBuiltinDeviceUi(const(PluginSlotState)* plug) pure nothrow @safe {
    return resolveBuiltinDeviceKind(plug) != BuiltinDeviceKind.GENERIC;
}

/// Interactive state for custom builtin device UIs (band selection, sub-tabs, drawbar/graph drag).
struct BuiltinDeviceUiState {
    int eqSelectedBand = 0;
    int eqDragBand = -1;
    int eqDragStartY = 0;
    float eqDragStartQ = 0.35f;

    int maximBandTab = 3; // 0=Low,1=Mid,2=High,3=Master,4=Xover
    int hottTab = 0;      // 0=Time,1=Below,2=Above
    int hottDragBand = -1;
    int hottDragMode = 0; // 0=downThresh, 1=upThresh, 2=attack/upRatio, 3=release/downRatio
    int drumSelPad = 0;
    int envDragPoint = -1;
    int acidActiveNote = -1;

    int activeDragParamId = -1;
    int dragStartY = 0;
    float dragStartVal = 0.0f;
    float dragDefaultVal = 0.5f;
    Rect activeDragSliderRc;
    bool dragIsVerticalSlider = false;
}

/// Interactive knob descriptor built during device layout for uniform rendering and hit-testing.
struct DeviceKnobSpec {
    uint paramId;
    string label;
    float defaultVal;
    uint accent;
    Rect cellRc;
    int cx;
    int cy;
    int r;
    string formattedVal;
    int knobStyle = 0; // 0=FlPalette dark knob, 1=TB-303 brushed silver+red ring, 2=light text on dark
    bool lightBg = false;
}

/// Interactive pill/toggle button descriptor built during layout.
struct DevicePillSpec {
    Rect rc;
    string label;
    bool active;
    uint activeColor;
    int actionKind; // 0=set param to targetVal, 1=toggle param 0/1, 2=ui tab, 3=virtual note, 4=load wav, 5=step param delta
    uint paramId;
    float targetVal;
    int uiTabVal;
    bool visibleButton = true;
}

private float clamp01(float v) pure nothrow @safe {
    if (v < 0.0f) return 0.0f;
    if (v > 1.0f) return 1.0f;
    return v;
}

float readPlugParam(const(PluginSlotState)* plug, uint paramId, float fallback = 0.5f) pure nothrow @safe {
    if (!plug) return fallback;
    foreach (const ref pm; plug.params) {
        if (pm.id == paramId) return clamp01(pm.currentValue);
    }
    if (paramId < plug.params.length && plug.params[paramId].id == paramId) {
        return clamp01(plug.params[paramId].currentValue);
    }
    return fallback;
}

void writePlugParam(PluginSlotState* plug, uint paramId, float value, float defaultVal = 0.5f,
                    string label = null) @safe {
    if (!plug) return;
    const float v = clamp01(value);
    foreach (ref pm; plug.params) {
        if (pm.id == paramId) {
            pm.currentValue = v;
            return;
        }
    }
    plug.params ~= ParamItemState(paramId, label.length > 0 ? label : format("P%d", paramId), clamp01(defaultVal), v);
}

private float normalizeDb(float db, float minDb, float maxDb) pure nothrow @safe {
    if (maxDb <= minDb) return 0.0f;
    return clamp01((db - minDb) / (maxDb - minDb));
}

// =========================================================================
// EQ Eight Math (mirrors EqDevicePanel.java & BuiltinEq)
// =========================================================================

private float eqNormToFreq(float norm) nothrow @safe {
    return 20.0f * powf(1000.0f, clamp01(norm));
}

private float eqFreqToNorm(float freq) nothrow @safe {
    if (freq <= 20.0f) return 0.0f;
    if (freq >= 20000.0f) return 1.0f;
    return log10f(freq / 20.0f) / 3.0f;
}

private float eqNormToQ(float norm) nothrow @safe {
    return 0.1f * powf(100.0f, clamp01(norm));
}

private int eqNormToTypeIdx(float norm) pure nothrow @safe {
    if (norm < 0.1f) return 0; // Off
    if (norm < 0.3f) return 1; // LP
    if (norm < 0.5f) return 2; // HP
    if (norm < 0.7f) return 3; // LS
    if (norm < 0.9f) return 4; // HS
    return 5;                  // Bell
}

private float eqTypeIdxToNorm(int idx) pure nothrow @safe {
    static immutable float[6] TYPE_NORMS = [0.0f, 0.2f, 0.4f, 0.6f, 0.8f, 1.0f];
    if (idx < 0 || idx >= 6) return 0.0f;
    return TYPE_NORMS[idx];
}

private float eqBandGainDbAtFreq(const(PluginSlotState)* plug, int b, float freq) nothrow @safe {
    const int typeIdx = eqNormToTypeIdx(readPlugParam(plug, cast(uint)b, 0.0f));
    if (typeIdx == 0) return 0.0f;
    static immutable float[8] DEF_FREQS = [30, 80, 250, 700, 2000, 5000, 10000, 16000];
    const float bandFreq = eqNormToFreq(readPlugParam(plug, cast(uint)(8 + b), eqFreqToNorm(DEF_FREQS[b])));
    const float gainDb = (readPlugParam(plug, cast(uint)(16 + b), 0.5f) - 0.5f) * 48.0f;
    const float q = eqNormToQ(readPlugParam(plug, cast(uint)(24 + b), 0.425f));
    const float ratio = freq / (bandFreq > 1.0f ? bandFreq : 1.0f);
    const float logR = log10f(ratio > 1e-4f ? ratio : 1e-4f);

    switch (typeIdx) {
        case 1: // LP
            if (ratio <= 1.0f) return 0.0f;
            return -12.0f * (logR / 0.30103f);
        case 2: // HP
            if (ratio >= 1.0f) return 0.0f;
            return 12.0f * (logR / 0.30103f);
        case 3: { // Low Shelf
            const float t = 1.0f / (1.0f + powf(ratio, 2.0f * (q > 0.5f ? q : 0.5f)));
            return gainDb * t;
        }
        case 4: { // High Shelf
            const float t = powf(ratio, 2.0f * (q > 0.5f ? q : 0.5f)) /
                            (1.0f + powf(ratio, 2.0f * (q > 0.5f ? q : 0.5f)));
            return gainDb * t;
        }
        default: { // Bell
            const float bw = 0.5f / (q > 0.2f ? q : 0.2f);
            return gainDb * expf(-(logR * logR) / (2.0f * bw * bw));
        }
    }
}

private float eqCompositeDbAtFreq(const(PluginSlotState)* plug, float freq) nothrow @safe {
    float total = 0.0f;
    foreach (b; 0 .. 8) {
        total += eqBandGainDbAtFreq(plug, b, freq);
    }
    return total;
}

private string formatDb(float db) @safe {
    return format("%+.1fdB", db);
}

private string formatFreq(float hz) @safe {
    return hz >= 1000.0f ? format("%.1fk", hz / 1000.0f) : format("%.0fHz", hz);
}

private string formatPercent(float n) @safe {
    return format("%d%%", cast(int)(clamp01(n) * 100.0f + 0.5f));
}

private string formatMsFromExp(float n, float minMs, float maxRatio) @safe {
    const float ms = minMs * powf(maxRatio, clamp01(n));
    return ms >= 1000.0f ? format("%.2fs", ms / 1000.0f) : format("%.0fms", ms);
}

private string formatRatio(float n) @safe {
    const float r = 1.0f + clamp01(n) * 19.0f;
    return r >= 19.5f ? "inf:1" : format("%.1f:1", r);
}

// =========================================================================
// Layout Builder for Controls per Builtin Device Kind
// =========================================================================

struct DeviceLayout {
    BuiltinDeviceKind kind = BuiltinDeviceKind.GENERIC;
    Rect canvasRc;
    Rect footerRc;
    Rect auxRc;
    DeviceKnobSpec[] knobs;
    DevicePillSpec[] pills;
    Rect[] drawbarRcs; // Organ drawbars 0..8
    Rect[] padRcs;     // Drum Machine pads 0..15
    Rect[] bandMeterRcs;
    Rect[3] hottBarRcs;
    Rect[3] hottInspectorRcs;
}

private void appendKnobRow(
    ref DeviceKnobSpec[] dst,
    Rect area,
    const(PluginSlotState)* plug,
    const(uint)[] paramIds,
    const(string)[] labels,
    const(float)[] defaults,
    const(string)[] formattedVals,
    const(uint)[] accents,
    const ref FlPalette pal,
    int knobStyle = 0,
    bool lightBg = false
) @safe {
    const size_t n = paramIds.length;
    if (n == 0 || area.width < dp(24) || area.height < dp(24)) return;
    const int cw = area.width / cast(int)n;
    const int maxR = (area.height - dp(24)) / 2;
    int r = (cw - dp(12)) / 2;
    if (maxR < r) r = maxR;
    if (r > dp(16)) r = dp(16);
    if (r < dp(8)) r = dp(8);
    const int cy = area.top + dp(11) + r;
    foreach (i; 0 .. n) {
        const int x0 = area.left + cast(int)i * cw;
        const int cx = x0 + cw / 2;
        const uint col = (i < accents.length) ? accents[i] : ((i % 2 == 0) ? pal.accentOrange : pal.accentCyan);
        const float dv = (i < defaults.length) ? defaults[i] : 0.5f;
        const string fv = (i < formattedVals.length && formattedVals[i].length > 0)
            ? formattedVals[i]
            : formatPercent(readPlugParam(plug, paramIds[i], dv));
        dst ~= DeviceKnobSpec(paramIds[i], labels[i], dv, col,
                              Rect(x0, area.top, x0 + cw, area.bottom), cx, cy, r, fv, knobStyle, lightBg);
    }
}

/// Computes widget sub-rectangles, knob specs, and interactive pills inside `bodyRc`.
DeviceLayout buildBuiltinDeviceLayout(
    Rect bodyRc,
    const(PluginSlotState)* plug,
    const ref BuiltinDeviceUiState ui,
    const FlPalette pal
) @safe {
    DeviceLayout dl;
    dl.kind = resolveBuiltinDeviceKind(plug);
    if (bodyRc.width < dp(180) || bodyRc.height < dp(120)) return dl;

    const int pad = dp(8);
    Rect inner = Rect(bodyRc.left + pad, bodyRc.top + dp(6), bodyRc.right - pad, bodyRc.bottom - dp(6));

    switch (dl.kind) {
        case BuiltinDeviceKind.EQ: {
            static immutable uint[8] BAND_COLS = [
                0x00E04040, 0x00E07020, 0x00D0C020, 0x0040B040,
                0x0030A0A0, 0x004080E0, 0x008060D0, 0x00C050A0,
            ];
            static immutable string[6] TYPE_NAMES = ["Off", "LP", "HP", "LS", "HS", "Bell"];
            static immutable float[8] DEF_FREQS = [30, 80, 250, 700, 2000, 5000, 10000, 16000];

            const int botH = dp(40);
            const int inspW = (inner.width > dp(340)) ? dp(96) : dp(82);
            Rect topRc = Rect(inner.left, inner.top, inner.right, inner.bottom - botH - dp(4));
            dl.canvasRc = Rect(topRc.left, topRc.top, topRc.right - inspW - dp(5), topRc.bottom);
            dl.auxRc = Rect(dl.canvasRc.right + dp(5), topRc.top, topRc.right, topRc.bottom);
            dl.footerRc = Rect(inner.left, topRc.bottom + dp(4), inner.right, inner.bottom);

            // Bottom 8 rainbow band strip columns (rendered cleanly like EqDevicePanel.java)
            const int bw = dl.footerRc.width / 8;
            foreach (b; 0 .. 8) {
                const int bx = dl.footerRc.left + b * bw;
                const int ti = eqNormToTypeIdx(readPlugParam(plug, cast(uint)b, 0.0f));
                const bool sel = (ui.eqSelectedBand == b);
                dl.pills ~= DevicePillSpec(
                    Rect(bx + dp(1), dl.footerRc.top, bx + bw - dp(1), dl.footerRc.bottom),
                    TYPE_NAMES[ti],
                    sel || ti > 0, sel ? BAND_COLS[b] : adjustColorBrightness(BAND_COLS[b], -24),
                    2, cast(uint)b, 0.0f, b, false
                );
            }

            // Right selected-band inspector: Type cycler pill + 3 knobs (Freq, Gain, Q)
            const int sb = (ui.eqSelectedBand >= 0 && ui.eqSelectedBand < 8) ? ui.eqSelectedBand : 0;
            const int curType = eqNormToTypeIdx(readPlugParam(plug, cast(uint)sb, 0.0f));
            dl.pills ~= DevicePillSpec(
                Rect(dl.auxRc.left + dp(3), dl.auxRc.top + dp(18),
                     dl.auxRc.right - dp(3), dl.auxRc.top + dp(36)),
                "Type: " ~ TYPE_NAMES[curType], curType > 0, BAND_COLS[sb],
                0, cast(uint)sb, eqTypeIdxToNorm((curType + 1) % 6), sb, true
            );

            const float fNorm = readPlugParam(plug, cast(uint)(8 + sb), eqFreqToNorm(DEF_FREQS[sb]));
            const float gNorm = readPlugParam(plug, cast(uint)(16 + sb), 0.5f);
            const float qNorm = readPlugParam(plug, cast(uint)(24 + sb), 0.425f);
            Rect kZone = Rect(dl.auxRc.left + dp(2), dl.auxRc.top + dp(40), dl.auxRc.right - dp(2), dl.auxRc.bottom);
            const uint[3] pids = [cast(uint)(8 + sb), cast(uint)(16 + sb), cast(uint)(24 + sb)];
            const string[3] klbls = ["Freq", "Gain", "Q"];
            const float[3] kdefs = [eqFreqToNorm(DEF_FREQS[sb]), 0.5f, 0.425f];
            const string[3] kvals = [
                formatFreq(eqNormToFreq(fNorm)),
                formatDb((gNorm - 0.5f) * 48.0f),
                format("%.2f", eqNormToQ(qNorm))
            ];
            if (kZone.height >= dp(148)) {
                const int kh = kZone.height / 3;
                foreach (k; 0 .. 3) {
                    appendKnobRow(dl.knobs,
                        Rect(kZone.left, kZone.top + k * kh, kZone.right, kZone.top + (k + 1) * kh),
                        plug, [pids[k]], [klbls[k]], [kdefs[k]], [kvals[k]], [BAND_COLS[sb]], pal);
                }
            } else {
                const int row1H = kZone.height / 2;
                appendKnobRow(dl.knobs,
                    Rect(kZone.left, kZone.top, kZone.right, kZone.top + row1H),
                    plug, [pids[0], pids[1]], [klbls[0], klbls[1]], [kdefs[0], kdefs[1]], [kvals[0], kvals[1]],
                    [BAND_COLS[sb], BAND_COLS[sb]], pal);
                appendKnobRow(dl.knobs,
                    Rect(kZone.left, kZone.top + row1H, kZone.right, kZone.bottom),
                    plug, [pids[2]], [klbls[2]], [kdefs[2]], [kvals[2]], [BAND_COLS[sb]], pal);
            }
            break;
        }
        case BuiltinDeviceKind.COMPRESSOR: {
            const int knobH = dp(68);
            dl.canvasRc = Rect(inner.left, inner.top, inner.right, inner.bottom - knobH - dp(4));
            Rect kArea = Rect(inner.left, dl.canvasRc.bottom + dp(4), inner.right, inner.bottom);
            const float th = readPlugParam(plug, 0, 1.0f);
            const float rt = readPlugParam(plug, 1, 0.0f);
            const float at = readPlugParam(plug, 2, 0.3f);
            const float rl = readPlugParam(plug, 3, 0.3f);
            const float kn = readPlugParam(plug, 4, 0.0f);
            const float mk = readPlugParam(plug, 5, 0.0f);
            const float ut = readPlugParam(plug, 7, 0.0f);
            const float ur = readPlugParam(plug, 8, 0.0f);
            appendKnobRow(dl.knobs, kArea, plug,
                [0u, 1u, 2u, 3u, 4u, 5u, 7u, 8u],
                ["Thresh", "Ratio", "Attack", "Release", "Knee", "Makeup", "UpThr", "UpRat"],
                [1.0f, 0.0f, 0.3f, 0.3f, 0.0f, 0.0f, 0.0f, 0.0f],
                [formatDb((th - 1.0f) * 60.0f), formatRatio(rt), formatMsFromExp(at, 0.1f, 1000.0f),
                 formatMsFromExp(rl, 10.0f, 100.0f), format("%.0fdB", kn * 24.0f),
                 format("+%.1fdB", mk * 24.0f), formatDb((ut - 1.0f) * 60.0f), formatRatio(ur)],
                [pal.accentAmber, pal.accentOrange, pal.accentCyan, pal.accentCyan,
                 pal.fgMuted, pal.accentGreen, pal.accentPurple, pal.accentPurple], pal);
            break;
        }
        case BuiltinDeviceKind.LIMITER: {
            const int knobH = dp(68);
            dl.canvasRc = Rect(inner.left, inner.top, inner.right, inner.bottom - knobH - dp(4));
            Rect kArea = Rect(inner.left, dl.canvasRc.bottom + dp(4), inner.right, inner.bottom);
            const float ce = readPlugParam(plug, 0, 1.0f);
            const float rl = readPlugParam(plug, 1, 0.3f);
            const float la = readPlugParam(plug, 2, 0.5f);
            const float gn = readPlugParam(plug, 3, 0.0f);
            appendKnobRow(dl.knobs, kArea, plug,
                [0u, 1u, 2u, 3u],
                ["Ceiling", "Release", "Lookahead", "Gain"],
                [1.0f, 0.3f, 0.5f, 0.0f],
                [formatDb(ce * 12.0f - 12.0f), formatMsFromExp(rl, 10.0f, 100.0f),
                 format("%.1fms", la * 10.0f), format("+%.1fdB", gn * 24.0f)],
                [pal.accentRed, pal.accentCyan, pal.accentAmber, pal.accentOrange], pal);
            break;
        }
        case BuiltinDeviceKind.HOTT: {
            // Authentic 3-column Xfer OTT / Swing HottDevicePanel layout:
            // Top banner + [Left: Split Freqs + SoftKnee/RMS] [Center: 3-Band Up/Down Comp Meters + T/B/A] [Right: 2x3 Out/Depth]
            const int hdrH = dp(18);
            dl.footerRc = Rect(inner.left, inner.top, inner.right, inner.top + hdrH);
            Rect body = Rect(inner.left, dl.footerRc.bottom + dp(4), inner.right, inner.bottom);

            const int leftW = (body.width >= dp(440)) ? dp(78) : dp(64);
            const int rightW = (body.width >= dp(440)) ? dp(164) : dp(136);
            Rect leftCol = Rect(body.left, body.top, body.left + leftW, body.bottom);
            Rect rightCol = Rect(body.right - rightW, body.top, body.right, body.bottom);
            dl.auxRc = leftCol;
            dl.canvasRc = Rect(leftCol.right + dp(4), body.top, rightCol.left - dp(4), body.bottom);

            // Left column: HiFreq, LoFreq knobs + Soft Knee, RMS toggles
            const float hiF = readPlugParam(plug, 1, 0.436f);
            const float loF = readPlugParam(plug, 0, 0.461f);
            const float hiHz = 500.0f * powf(40.0f, hiF);
            const float loHz = 20.0f * powf(25.0f, loF);
            const int btnAreaH = dp(38);
            const int splH = (leftCol.height - dp(14) - btnAreaH) / 2;
            appendKnobRow(dl.knobs,
                Rect(leftCol.left, leftCol.top + dp(12), leftCol.right, leftCol.top + dp(12) + splH),
                plug, [1u], ["HiFreq"], [0.436f], [format("%.2fk", hiHz / 1000.0f)], [0x0000D4FF], pal);
            appendKnobRow(dl.knobs,
                Rect(leftCol.left, leftCol.top + dp(12) + splH, leftCol.right, leftCol.bottom - btnAreaH - dp(2)),
                plug, [0u], ["LoFreq"], [0.461f], [format("%.0fHz", loHz)], [0x0000D4FF], pal);

            const bool softKnee = readPlugParam(plug, 15, 1.0f) >= 0.5f;
            const bool rmsMode = readPlugParam(plug, 16, 1.0f) >= 0.5f;
            dl.pills ~= DevicePillSpec(
                Rect(leftCol.left + dp(2), leftCol.bottom - dp(36), leftCol.right - dp(2), leftCol.bottom - dp(19)),
                "Soft Knee", softKnee, 0x0000D4FF, 1, 15, 1.0f, 0
            );
            dl.pills ~= DevicePillSpec(
                Rect(leftCol.left + dp(2), leftCol.bottom - dp(17), leftCol.right - dp(2), leftCol.bottom),
                "RMS", rmsMode, 0x0010B981, 1, 16, 1.0f, 0
            );

            // Center meter panel rows & [T] [B] [A] tabs
            Rect c = dl.canvasRc;
            const int cPad = dp(6);
            const int tabH = dp(18);
            const int topRulerH = dp(12);
            const int inspW = (c.width >= dp(230)) ? dp(68) : dp(54);
            const int meterW = c.width - cPad * 2 - inspW - dp(4);
            const int bandsTop = c.top + topRulerH + dp(2);
            const int bandsBot = c.bottom - tabH - dp(2);
            const int bandGap = dp(4);
            const int bandH = ((bandsBot - bandsTop) - bandGap * 2) / 3;
            foreach (bi; 0 .. 3) {
                const int ry0 = bandsTop + bi * (bandH + bandGap);
                const int ry1 = ry0 + bandH;
                dl.hottBarRcs[bi] = Rect(c.left + cPad, ry0, c.left + cPad + meterW, ry1);
                dl.hottInspectorRcs[bi] = Rect(c.left + cPad + meterW + dp(4), ry0, c.right - cPad, ry1);
            }

            static immutable string[3] TABS = ["T", "B", "A"];
            const int tbW = dp(20);
            const int tbGap = dp(2);
            const int tabX0 = c.right - cPad - 3 * tbW - 2 * tbGap;
            const int tabY0 = c.bottom - tabH;
            foreach (ti; 0 .. 3) {
                const int x0 = tabX0 + ti * (tbW + tbGap);
                dl.pills ~= DevicePillSpec(
                    Rect(x0, tabY0, x0 + tbW, c.bottom - dp(2)),
                    TABS[ti], ui.hottTab == ti, 0x0000D4FF, 2, 0, 0.0f, ti
                );
            }

            // Right column: 2x3 OTT cyan knobs (HiOut/MidOut/LoOut + Output/Time/Depth)
            const int rHalf = rightCol.height / 2;
            const float lo = readPlugParam(plug, 5, 0.715f);
            const float md = readPlugParam(plug, 6, 0.619f);
            const float hi = readPlugParam(plug, 7, 0.715f);
            const float outV = readPlugParam(plug, 4, 0.5f);
            const float tm = readPlugParam(plug, 3, 1.0f);
            const float amt = readPlugParam(plug, 2, 1.0f);
            appendKnobRow(dl.knobs,
                Rect(rightCol.left, rightCol.top, rightCol.right, rightCol.top + rHalf),
                plug, [7u, 6u, 5u], ["Hi Out", "Mid Out", "Lo Out"],
                [0.715f, 0.619f, 0.715f],
                [formatDb(hi * 48.0f - 24.0f), formatDb(md * 48.0f - 24.0f), formatDb(lo * 48.0f - 24.0f)],
                [0x0000D4FF, 0x0000D4FF, 0x0000D4FF], pal);
            appendKnobRow(dl.knobs,
                Rect(rightCol.left, rightCol.top + rHalf, rightCol.right, rightCol.bottom),
                plug, [4u, 3u, 2u], ["Output", "Time", "Amount"],
                [0.5f, 1.0f, 1.0f],
                [formatDb(outV * 48.0f - 24.0f), formatPercent(tm), formatPercent(amt)],
                [0x0000D4FF, 0x0000D4FF, 0x0000D4FF], pal);
            break;
        }
        case BuiltinDeviceKind.MAXIM: {
            const int tabH = dp(20);
            const int knobH = dp(66);
            Rect tabRc = Rect(inner.left, inner.top, inner.right, inner.top + tabH);
            static immutable string[5] BTABS = ["LOW", "MID", "HIGH", "MASTER", "XOVER"];
            static immutable uint[5] BCOLS = [0x00E25822, 0x0010B981, 0x003B82F6, 0x00F59E0B, 0x008B5CF6];
            const int bw = tabRc.width / 5;
            foreach (ti; 0 .. 5) {
                dl.pills ~= DevicePillSpec(
                    Rect(tabRc.left + ti * bw, tabRc.top, tabRc.left + (ti + 1) * bw - dp(2), tabRc.bottom),
                    BTABS[ti], ui.maximBandTab == ti, BCOLS[ti], 2, 0, 0.0f, ti
                );
            }
            dl.canvasRc = Rect(inner.left, tabRc.bottom + dp(4), inner.right, inner.bottom - knobH - dp(4));
            Rect kArea = Rect(inner.left, dl.canvasRc.bottom + dp(4), inner.right, inner.bottom);
            if (ui.maximBandTab == 4) {
                appendKnobRow(dl.knobs, kArea, plug,
                    [0u, 1u, 2u],
                    ["Low Xover", "High Xover", "Lookahead"],
                    [0.461f, 0.436f, 0.2f], null,
                    [BCOLS[0], BCOLS[2], BCOLS[4]], pal);
            } else {
                const uint base = cast(uint)(4 + ui.maximBandTab * 10);
                const uint col = BCOLS[ui.maximBandTab];
                appendKnobRow(dl.knobs, kArea, plug,
                    [base + 0, base + 1, base + 4, base + 5, base + 7, base + 8, base + 9],
                    ["Pre", "Post", "Thresh", "Ratio", "Attack", "Release", "Ceiling"],
                    [0.5f, 0.5f, 1.0f, 0.0f, 0.3f, 0.3f, ui.maximBandTab == 3 ? 0.975f : 1.0f],
                    null, [col, col, col, col, col, col, pal.accentRed], pal);
            }
            break;
        }
        case BuiltinDeviceKind.DELAY: {
            const int knobH = dp(66);
            dl.canvasRc = Rect(inner.left, inner.top, inner.right, inner.bottom - knobH - dp(4));
            const bool synced = readPlugParam(plug, 8, 1.0f) >= 0.5f;
            dl.pills ~= DevicePillSpec(
                Rect(dl.canvasRc.right - dp(56), dl.canvasRc.top + dp(4),
                     dl.canvasRc.right - dp(4), dl.canvasRc.top + dp(20)),
                synced ? "SYNC" : "FREE", synced, pal.accentCyan, 1, 8, 1.0f, 0
            );
            Rect kArea = Rect(inner.left, dl.canvasRc.bottom + dp(4), inner.right, inner.bottom);
            appendKnobRow(dl.knobs, kArea, plug,
                [0u, 1u, 2u, 3u, 4u, 5u, 6u],
                ["Time L", "Time R", "Feedbk", "Mix", "HPF", "LPF", "X-Talk"],
                [0.6f, 0.6f, 0.4f, 0.3f, 0.15f, 0.75f, 1.0f], null,
                [pal.accentCyan, pal.accentCyan, pal.accentOrange, pal.accentGreen,
                 pal.fgMuted, pal.fgMuted, pal.accentAmber], pal);
            break;
        }
        case BuiltinDeviceKind.REVERB: {
            const int knobH = dp(66);
            dl.canvasRc = Rect(inner.left, inner.top, inner.right, inner.bottom - knobH - dp(4));
            Rect kArea = Rect(inner.left, dl.canvasRc.bottom + dp(4), inner.right, inner.bottom);
            appendKnobRow(dl.knobs, kArea, plug,
                [0u, 1u, 2u, 3u, 4u, 5u, 6u],
                ["Size", "Damp", "Mix", "PreDly", "HPF", "LPF", "Width"],
                [0.5f, 0.5f, 0.3f, 0.1f, 0.15f, 0.85f, 1.0f], null,
                [pal.accentPurple, pal.accentPurple, pal.accentGreen, pal.accentCyan,
                 pal.fgMuted, pal.fgMuted, pal.accentOrange], pal);
            break;
        }
        case BuiltinDeviceKind.PHASER: {
            const int knobH = dp(66);
            dl.canvasRc = Rect(inner.left, inner.top, inner.right, inner.bottom - knobH - dp(4));
            const bool synced = readPlugParam(plug, 10, 0.0f) >= 0.5f;
            dl.pills ~= DevicePillSpec(
                Rect(dl.canvasRc.right - dp(56), dl.canvasRc.top + dp(4),
                     dl.canvasRc.right - dp(4), dl.canvasRc.top + dp(20)),
                synced ? "SYNC" : "FREE", synced, pal.accentCyan, 1, 10, 1.0f, 0
            );
            Rect kArea = Rect(inner.left, dl.canvasRc.bottom + dp(4), inner.right, inner.bottom);
            appendKnobRow(dl.knobs, kArea, plug,
                [0u, 1u, 2u, 3u, 4u, 5u, 8u, 9u],
                ["Rate", "Depth", "Feedbk", "Stages", "Mix", "Stereo", "Center", "Spread"],
                [0.3f, 0.7f, 0.4f, 0.33f, 0.5f, 0.5f, 0.5f, 0.3f], null,
                [pal.accentCyan, pal.accentOrange, pal.accentAmber, pal.accentPurple,
                 pal.accentGreen, pal.accentCyan, pal.fgMuted, pal.fgMuted], pal);
            break;
        }
        case BuiltinDeviceKind.CHORUS: {
            const int knobH = dp(68);
            dl.canvasRc = Rect(inner.left, inner.top, inner.right, inner.bottom - knobH - dp(4));
            Rect kArea = Rect(inner.left, dl.canvasRc.bottom + dp(4), inner.right, inner.bottom);
            appendKnobRow(dl.knobs, kArea, plug,
                [0u, 1u, 2u, 3u, 4u],
                ["Rate", "Depth", "Delay", "Feedbk", "Wet/Dry"],
                [0.25f, 0.5f, 0.4f, 0.25f, 0.5f], null,
                [pal.accentCyan, pal.accentOrange, pal.accentAmber, pal.accentPurple, pal.accentGreen], pal);
            break;
        }
        case BuiltinDeviceKind.BITCRUSHER: {
            const int knobH = dp(68);
            dl.canvasRc = Rect(inner.left, inner.top, inner.right, inner.bottom - knobH - dp(4));
            Rect kArea = Rect(inner.left, dl.canvasRc.bottom + dp(4), inner.right, inner.bottom);
            const float bits = 1.0f + readPlugParam(plug, 0, 0.5f) * 23.0f;
            const float sr = 20.0f * powf(2205.0f, readPlugParam(plug, 1, 0.8f));
            const float dr = readPlugParam(plug, 2, 0.2f);
            const float mx = readPlugParam(plug, 3, 1.0f);
            appendKnobRow(dl.knobs, kArea, plug,
                [0u, 1u, 2u, 3u],
                ["Bits", "SampleRt", "Drive", "Mix"],
                [0.5f, 0.8f, 0.2f, 1.0f],
                [format("%.1fb", bits), formatFreq(sr), format("+%.1fdB", dr * 24.0f), formatPercent(mx)],
                [pal.accentOrange, pal.accentCyan, pal.accentRed, pal.accentGreen], pal);
            break;
        }
        case BuiltinDeviceKind.ENVELOPE_SHAPER: {
            const int knobH = dp(66);
            dl.canvasRc = Rect(inner.left, inner.top, inner.right, inner.bottom - knobH - dp(4));
            const float mVal = readPlugParam(plug, 3, 0.0f);
            const int modeIdx = (mVal < 0.2f) ? 0 : (mVal < 0.45f ? 1 : (mVal < 0.75f ? 2 : 3));
            static immutable string[4] MODES = ["GAIN", "HPF", "BPF", "LPF"];
            static immutable float[4] M_NORMS = [0.0f, 0.33f, 0.5f, 1.0f];
            dl.pills ~= DevicePillSpec(
                Rect(dl.canvasRc.right - dp(62), dl.canvasRc.top + dp(4),
                     dl.canvasRc.right - dp(4), dl.canvasRc.top + dp(20)),
                MODES[modeIdx], true, pal.accentOrange, 0, 3, M_NORMS[(modeIdx + 1) % 4], 0
            );
            Rect kArea = Rect(inner.left, dl.canvasRc.bottom + dp(4), inner.right, inner.bottom);
            appendKnobRow(dl.knobs, kArea, plug,
                [0u, 1u, 2u],
                ["Mix", "Rate", "Smooth"],
                [1.0f, 0.5f, 0.5f], null,
                [pal.accentGreen, pal.accentCyan, pal.accentAmber], pal);
            break;
        }
        case BuiltinDeviceKind.CONVOLVER: {
            const int knobH = dp(68);
            dl.canvasRc = Rect(inner.left, inner.top, inner.right, inner.bottom - knobH - dp(4));
            Rect kArea = Rect(inner.left, dl.canvasRc.bottom + dp(4), inner.right, inner.bottom);
            appendKnobRow(dl.knobs, kArea, plug,
                [0u, 1u, 2u],
                ["Dry", "Wet", "PreDelay"],
                [0.0f, 1.0f, 0.0f], null,
                [pal.fgMuted, pal.accentCyan, pal.accentOrange], pal);
            break;
        }
        case BuiltinDeviceKind.VOCODEY: {
            const int knobH = dp(66);
            dl.canvasRc = Rect(inner.left, inner.top, inner.right, inner.bottom - knobH - dp(4));
            Rect kArea = Rect(inner.left, dl.canvasRc.bottom + dp(4), inner.right, inner.bottom);
            appendKnobRow(dl.knobs, kArea, plug,
                [0u, 1u, 2u, 3u, 4u, 5u, 6u, 7u],
                ["Atk", "Dec", "BW", "Bleed", "Detune", "Dry", "Wet", "Vol"],
                [0.15f, 0.25f, 0.35f, 0.3f, 0.15f, 0.0f, 1.0f, 0.7f], null, null, pal);
            break;
        }
        case BuiltinDeviceKind.STEREO_WIDTH: {
            const int knobH = dp(68);
            dl.canvasRc = Rect(inner.left, inner.top, inner.right, inner.bottom - knobH - dp(4));
            const bool rightCh = readPlugParam(plug, 1, 0.0f) >= 0.5f;
            dl.pills ~= DevicePillSpec(
                Rect(dl.canvasRc.right - dp(68), dl.canvasRc.top + dp(4),
                     dl.canvasRc.right - dp(4), dl.canvasRc.top + dp(20)),
                rightCh ? "Ch: R" : "Ch: L", rightCh, pal.accentCyan, 1, 1, 1.0f, 0
            );
            Rect kArea = Rect(inner.left, dl.canvasRc.bottom + dp(4), inner.right, inner.bottom);
            appendKnobRow(dl.knobs, kArea, plug,
                [0u, 2u, 3u],
                ["Delay", "MonoHz", "Width"],
                [0.2f, 0.25f, 0.5f], null,
                [pal.accentCyan, pal.accentAmber, pal.accentOrange], pal);
            break;
        }
        case BuiltinDeviceKind.ACID_BASS: {
            // Authentic Roland TB-303 brushed silver face:
            // Top charcoal badge | 8 silver vertical bays (Waveform + 7 dials) | Lower keyboard/step bay
            const int hdrH = dp(16);
            dl.auxRc = Rect(inner.left, inner.top, inner.right, inner.top + hdrH);
            const int remH = inner.bottom - dl.auxRc.bottom;
            const int upperH = (remH * 55) / 100;
            Rect mainRc = Rect(inner.left, dl.auxRc.bottom, inner.right, dl.auxRc.bottom + upperH);
            Rect kbdRc = Rect(inner.left, mainRc.bottom, inner.right, inner.bottom);
            dl.canvasRc = mainRc;
            dl.footerRc = kbdRc;

            // Col 0: WAVEFORM SAW/SQR switch; Cols 1..7: 7 TB-303 silver dials
            const int colW = mainRc.width / 8;
            const bool isSq = readPlugParam(plug, 0, 0.0f) >= 0.5f;
            dl.pills ~= DevicePillSpec(
                Rect(mainRc.left + dp(4), mainRc.top + dp(18), mainRc.left + colW - dp(4), mainRc.bottom - dp(16)),
                isSq ? "SQR" : "SAW", isSq, 0x00FF3333, 1, 0, 1.0f, 0, false
            );

            Rect kArea = Rect(mainRc.left + colW, mainRc.top + dp(4), mainRc.left + colW * 8, mainRc.bottom - dp(2));
            const float cut = readPlugParam(plug, 1, 0.3f);
            const float dec = readPlugParam(plug, 4, 0.2f);
            appendKnobRow(dl.knobs, kArea, plug,
                [1u, 2u, 3u, 4u, 5u, 6u, 7u],
                ["CUTOFF", "RESONANCE", "ENV MOD", "DECAY", "ACCENT", "OVERDRIVE", "VOLUME"],
                [0.3f, 0.6f, 0.5f, 0.2f, 0.0f, 0.1f, 0.7f],
                [formatFreq(100.0f * powf(30.0f, cut)), formatPercent(readPlugParam(plug, 2, 0.6f)),
                 formatPercent(readPlugParam(plug, 3, 0.5f)), formatMsFromExp(dec, 50.0f, 60.0f),
                 formatPercent(readPlugParam(plug, 5, 0.0f)), formatPercent(readPlugParam(plug, 6, 0.1f)),
                 formatPercent(readPlugParam(plug, 7, 0.7f))],
                [0x00FF3333, 0x00FF3333, 0x00FF3333, 0x00FF3333, 0x00FF3333, 0x00FF3333, 0x00FF3333],
                pal, 1, true);

            // Lower keyboard bay: Left Octave ◀/▶ | Center 12 staggered silver keys | Right SLIDE/ACCENT
            const int octVal = 1 + cast(int)(readPlugParam(plug, 8, 0.5f) * 4.0f + 0.5f);
            const uint baseMidi = cast(uint)(24 + octVal * 12);
            const int octW = dp(82);
            const int modW = dp(78);
            const int keySpanW = kbdRc.width - octW - modW;
            const int keyStart = kbdRc.left + octW + dp(4);
            const int wStep = (keySpanW - dp(8)) / 7;
            const int kw = (wStep > dp(18)) ? dp(14) : (wStep - dp(3));
            const int kh = (kbdRc.height >= dp(88)) ? dp(26) : dp(20);

            // Octave Down / Up circular buttons
            const int octBtnY = kbdRc.top + dp(34);
            dl.pills ~= DevicePillSpec(
                Rect(kbdRc.left + dp(10), octBtnY - dp(9), kbdRc.left + dp(28), octBtnY + dp(9)),
                "<", false, 0x00CCCCCC, 5, 8, -0.25f, 0, false
            );
            dl.pills ~= DevicePillSpec(
                Rect(kbdRc.left + dp(52), octBtnY - dp(9), kbdRc.left + dp(70), octBtnY + dp(9)),
                ">", false, 0x00CCCCCC, 5, 8, 0.25f, 0, false
            );

            // 5 Black keys (upper row: C#, D#, F#, G#, A#)
            static immutable int[5] BLK_OFF = [1, 3, 6, 8, 10];
            static immutable float[5] BLK_MULT = [0.5f, 1.5f, 3.5f, 4.5f, 5.5f];
            static immutable string[5] BLK_LBL = ["C#", "D#", "F#", "G#", "A#"];
            const int blkCenterY = kbdRc.top + (kbdRc.height * 32) / 100;
            foreach (bi; 0 .. 5) {
                const int kx = keyStart + wStep / 2 + cast(int)(BLK_MULT[bi] * wStep);
                dl.pills ~= DevicePillSpec(
                    Rect(kx - kw / 2, blkCenterY - kh / 2, kx + kw / 2, blkCenterY + kh / 2),
                    BLK_LBL[bi], true, 0x00111111, 3, baseMidi + BLK_OFF[bi], 1.0f, BLK_OFF[bi], false
                );
            }

            // 7 White keys (lower row: C, D, E, F, G, A, B)
            static immutable int[7] WHT_OFF = [0, 2, 4, 5, 7, 9, 11];
            static immutable string[7] WHT_LBL = ["C", "D", "E", "F", "G", "A", "B"];
            const int whtCenterY = kbdRc.top + (kbdRc.height * 68) / 100;
            foreach (wi; 0 .. 7) {
                const int kx = keyStart + wStep / 2 + wi * wStep;
                dl.pills ~= DevicePillSpec(
                    Rect(kx - kw / 2, whtCenterY - kh / 2, kx + kw / 2, whtCenterY + kh / 2),
                    WHT_LBL[wi], false, 0x00FAFAFA, 3, baseMidi + WHT_OFF[wi], 1.0f, WHT_OFF[wi], false
                );
            }

            // Right SLIDE & ACCENT brushed buttons with glowing Red LEDs
            const bool slideOn = readPlugParam(plug, 9, 0.0f) >= 0.5f;
            const bool accOn = readPlugParam(plug, 10, 0.0f) >= 0.5f;
            const int rModY = kbdRc.middley;
            const int slideCx = kbdRc.right - dp(54);
            const int accCx = kbdRc.right - dp(20);
            dl.pills ~= DevicePillSpec(
                Rect(slideCx - dp(10), rModY - dp(10), slideCx + dp(10), rModY + dp(10)),
                "SLIDE", slideOn, 0x00FF3333, 1, 9, 1.0f, 0, false
            );
            dl.pills ~= DevicePillSpec(
                Rect(accCx - dp(10), rModY - dp(10), accCx + dp(10), rModY + dp(10)),
                "ACCENT", accOn, 0x00FF3333, 1, 10, 1.0f, 0, false
            );
            break;
        }
        case BuiltinDeviceKind.THREE_OSC: {
            // 3 stacked Oscillator rows + compact bottom Envelope/Filter row
            const int rowH = (inner.height - dp(72)) / 3;
            static immutable string[4] WAVES = ["Sin", "Saw", "Sq", "Tri"];
            static immutable uint[3] OCOLS = [0x004080E0, 0x0040B040, 0x00E07020];
            foreach (osc; 0 .. 3) {
                const int ry0 = inner.top + osc * rowH;
                Rect rRc = Rect(inner.left, ry0, inner.right, ry0 + rowH - dp(3));
                const uint base = cast(uint)(osc * 5);
                const float wVal = readPlugParam(plug, base, 0.0f);
                const int wi = (wVal < 0.2f) ? 0 : (wVal < 0.5f ? 1 : (wVal < 0.85f ? 2 : 3));
                static immutable float[4] WNORMS = [0.0f, 0.33f, 0.67f, 1.0f];
                dl.pills ~= DevicePillSpec(
                    Rect(rRc.left + dp(4), rRc.top + dp(10), rRc.left + dp(68), rRc.bottom - dp(10)),
                    format("O%d:%s", osc + 1, WAVES[wi]), true, OCOLS[osc],
                    0, base, WNORMS[(wi + 1) % 4], osc
                );
                appendKnobRow(dl.knobs,
                    Rect(rRc.left + dp(72), rRc.top, rRc.right, rRc.bottom), plug,
                    [base + 1, base + 2, base + 3, base + 4],
                    ["Coarse", "Fine", "Vol", "Pan"],
                    [0.5f, 0.5f, osc == 0 ? 1.0f : 0.0f, 0.5f], null,
                    [OCOLS[osc], OCOLS[osc], OCOLS[osc], pal.accentCyan], pal);
            }
            Rect botRc = Rect(inner.left, inner.top + 3 * rowH + dp(2), inner.right, inner.bottom);
            dl.canvasRc = Rect(botRc.left, botRc.top, botRc.left + dp(96), botRc.bottom);
            appendKnobRow(dl.knobs,
                Rect(botRc.left + dp(100), botRc.top, botRc.right, botRc.bottom), plug,
                [15u, 17u, 18u, 20u, 21u, 27u],
                ["Atk", "Sus", "Rel", "Cutoff", "Res", "Vol"],
                [0.0f, 0.7f, 0.3f, 1.0f, 0.0f, 0.8f], null,
                [pal.accentGreen, pal.accentGreen, pal.accentGreen, pal.accentOrange, pal.accentOrange, pal.accentCyan], pal);
            break;
        }
        case BuiltinDeviceKind.SAMPLER: {
            const int knobH = dp(66);
            dl.canvasRc = Rect(inner.left, inner.top, inner.right, inner.bottom - knobH - dp(4));
            dl.pills ~= DevicePillSpec(
                Rect(dl.canvasRc.right - dp(78), dl.canvasRc.top + dp(4),
                     dl.canvasRc.right - dp(4), dl.canvasRc.top + dp(22)),
                "LOAD WAV", true, pal.accentOrange, 4, 0, 1.0f, 0
            );
            Rect kArea = Rect(inner.left, dl.canvasRc.bottom + dp(4), inner.right, inner.bottom);
            appendKnobRow(dl.knobs, kArea, plug,
                [0u, 1u, 3u, 4u, 6u, 8u, 9u, 15u],
                ["Start", "End", "Atk", "Dec", "Rel", "Cutoff", "Res", "Vol"],
                [0.0f, 1.0f, 0.0f, 0.0f, 0.3f, 1.0f, 0.0f, 0.8f], null, null, pal);
            break;
        }
        case BuiltinDeviceKind.EPIANO: {
            const int knobH = dp(66);
            dl.canvasRc = Rect(inner.left, inner.top, inner.right, inner.bottom - knobH - dp(4));
            Rect kArea = Rect(inner.left, dl.canvasRc.bottom + dp(4), inner.right, inner.bottom);
            appendKnobRow(dl.knobs, kArea, plug,
                [0u, 1u, 2u, 3u, 4u, 7u, 11u, 12u],
                ["Decay", "Rel", "Hard", "Treble", "Trem", "Width", "Drive", "Vol"],
                [0.5f, 0.4f, 0.5f, 0.5f, 0.3f, 0.5f, 0.1f, 0.8f], null, null, pal);
            break;
        }
        case BuiltinDeviceKind.ORGAN: {
            // 9 Hammond Drawbars + Rotary/Perc/Vol knobs at right
            const int rightW = dp(110);
            dl.canvasRc = Rect(inner.left, inner.top, inner.right - rightW - dp(6), inner.bottom);
            const int dw = dl.canvasRc.width / 9;
            foreach (d; 0 .. 9) {
                const int dx = dl.canvasRc.left + d * dw;
                dl.drawbarRcs ~= Rect(dx + dp(3), dl.canvasRc.top + dp(18),
                                      dx + dw - dp(3), dl.canvasRc.bottom - dp(18));
            }
            const bool percOn = readPlugParam(plug, 9, 1.0f) >= 0.5f;
            const bool fastOn = readPlugParam(plug, 11, 0.0f) >= 0.5f;
            Rect rCol = Rect(dl.canvasRc.right + dp(6), inner.top, inner.right, inner.bottom);
            dl.pills ~= DevicePillSpec(
                Rect(rCol.left, rCol.top + dp(4), rCol.right, rCol.top + dp(26)),
                "PERC", percOn, pal.accentOrange, 1, 9, 1.0f, 0
            );
            dl.pills ~= DevicePillSpec(
                Rect(rCol.left, rCol.top + dp(30), rCol.right, rCol.top + dp(52)),
                fastOn ? "LESLIE: FAST" : "LESLIE: SLOW", fastOn, pal.accentCyan, 1, 11, 1.0f, 0
            );
            appendKnobRow(dl.knobs, Rect(rCol.left, rCol.top + dp(58), rCol.right, rCol.bottom), plug,
                [10u, 12u], ["PercDec", "Vol"], [0.4f, 0.8f], null,
                [pal.accentAmber, pal.accentGreen], pal);
            break;
        }
        case BuiltinDeviceKind.DRUM_MACHINE: {
            // 4x2 (8 pad) grid on top + Pad preview / right-click Load WAV hint
            dl.canvasRc = Rect(inner.left, inner.top, inner.right, inner.top + dp(34));
            Rect gridRc = Rect(inner.left, dl.canvasRc.bottom + dp(6), inner.right, inner.bottom);
            const int cols = 4;
            const int rows = 2;
            const int gap = dp(5);
            const int pw = (gridRc.width - (cols - 1) * gap) / cols;
            const int ph = (gridRc.height - (rows - 1) * gap) / rows;
            foreach (pi; 0 .. 8) {
                const int col = pi % cols;
                const int row = pi / cols;
                const int x0 = gridRc.left + col * (pw + gap);
                const int y0 = gridRc.top + row * (ph + gap);
                dl.padRcs ~= Rect(x0, y0, x0 + pw, y0 + ph);
            }
            break;
        }
        case BuiltinDeviceKind.DR8_MODULE: {
            const int knobH = dp(68);
            dl.canvasRc = Rect(inner.left, inner.top, inner.right, inner.bottom - knobH - dp(4));
            dl.pills ~= DevicePillSpec(
                Rect(dl.canvasRc.right - dp(62), dl.canvasRc.top + dp(4),
                     dl.canvasRc.right - dp(4), dl.canvasRc.top + dp(22)),
                "HIT", true, pal.accentOrange, 3, 60, 1.0f, 0
            );
            Rect kArea = Rect(inner.left, dl.canvasRc.bottom + dp(4), inner.right, inner.bottom);
            const size_t cnt = plug ? (plug.params.length > 6 ? 6 : plug.params.length) : 0;
            uint[] ids;
            string[] lbls;
            float[] defs;
            foreach (i; 0 .. cnt) {
                ids ~= plug.params[i].id;
                lbls ~= plug.params[i].name;
                defs ~= plug.params[i].defaultValue;
            }
            appendKnobRow(dl.knobs, kArea, plug, ids, lbls, defs, null, null, pal);
            break;
        }
        default:
            break;
    }
    return dl;
}

// =========================================================================
// Rendering of Java Swing-Ported Device UIs in FL Studio 2026 Theme Flavor
// =========================================================================

private void drawPanelGrid(DrawBuf buf, Rect rc, const ref FlPalette pal) {
    buf.fillRect(Rect(rc.left, rc.middley, rc.right, rc.middley + dp(1)), pal.borderSubtle);
    buf.fillRect(Rect(rc.left + rc.width / 4, rc.top, rc.left + rc.width / 4 + dp(1), rc.bottom), pal.borderSubtle);
    buf.fillRect(Rect(rc.middlex, rc.top, rc.middlex + dp(1), rc.bottom), pal.borderSubtle);
    buf.fillRect(Rect(rc.left + (rc.width * 3) / 4, rc.top, rc.left + (rc.width * 3) / 4 + dp(1), rc.bottom), pal.borderSubtle);
}

private void drawDbMeterBar(DrawBuf buf, Rect rc, float db, float minDb, float maxDb,
                            uint col, string label, FontRef fnt, const ref FlPalette pal) {
    drawThemeCard(buf, rc, pal.bgWindow, pal.borderSubtle, pal, 1.5f, true);
    const float n = normalizeDb(db, minDb, maxDb);
    const int fh = cast(int)(n * (rc.height - dp(2)));
    if (fh > 0) {
        buf.fillRect(Rect(rc.left + dp(1), rc.bottom - dp(1) - fh, rc.right - dp(1), rc.bottom - dp(1)), col);
    }
    if (label.length > 0 && rc.width >= dp(14)) {
        drawCenteredText(buf, fnt, Rect(rc.left, rc.bottom - dp(12), rc.right, rc.bottom),
                         toUTF32(label), pal.fgMuted);
    }
}

private uint lerpColorRgb(uint a, uint b, float t) pure nothrow @safe {
    const float u = clamp01(t);
    const int ar = (a >> 16) & 0xFF, ag = (a >> 8) & 0xFF, ab = a & 0xFF;
    const int br = (b >> 16) & 0xFF, bg = (b >> 8) & 0xFF, bb = b & 0xFF;
    const uint r = cast(uint)(ar + (br - ar) * u + 0.5f) & 0xFF;
    const uint g = cast(uint)(ag + (bg - ag) * u + 0.5f) & 0xFF;
    const uint bl = cast(uint)(ab + (bb - ab) * u + 0.5f) & 0xFF;
    return (a & 0xFF000000u) | (r << 16) | (g << 8) | bl;
}

private void drawVGradientRect(DrawBuf buf, Rect rc, uint topCol, uint botCol) {
    if (rc.width <= 0 || rc.height <= 0) return;
    const int h = rc.height;
    const int step = (h > 48) ? 2 : 1;
    for (int dy = 0; dy < h; dy += step) {
        const float t = (h > 1) ? cast(float)dy / cast(float)(h - 1) : 0.0f;
        const uint col = lerpColorRgb(topCol, botCol, t);
        const int y1 = (dy + step < h) ? (rc.top + dy + step) : rc.bottom;
        buf.fillRect(Rect(rc.left, rc.top + dy, rc.right, y1), col);
    }
}

private void drawAcidLed(DrawBuf buf, float cx, float cy, float r, bool lit) {
    if (lit) {
        drawFilledCircle(buf, cx, cy, r * 1.9f, 0xA8FF3333, COLOR_TRANSPARENT);
        drawFilledCircle(buf, cx, cy, r, 0x00FF3333, 0x00555555);
        drawFilledCircle(buf, cx - r * 0.25f, cy - r * 0.25f, r * 0.35f, 0x40FFFFFF, COLOR_TRANSPARENT);
    } else {
        drawFilledCircle(buf, cx, cy, r, 0x00480808, 0x00666666);
        drawFilledCircle(buf, cx - r * 0.25f, cy - r * 0.25f, r * 0.28f, 0xC8FFFFFF, COLOR_TRANSPARENT);
    }
}

private void drawTb303Knob(DrawBuf buf, int cx, int cy, int r, float normVal) {
    if (r < 4) return;
    const float v = clamp01(normVal);
    // 11 radial tick marks around knob perimeter (135 deg -> 405 deg)
    foreach (i; 0 .. 11) {
        const float deg = 135.0f + cast(float)i * 27.0f;
        const float rad = deg * 0.0174532925f;
        const float cs = cosf(rad);
        const float sn = sinf(rad);
        drawThickLine(buf,
            cx + cs * (r + dpf(1.2f)), cy + sn * (r + dpf(1.2f)),
            cx + cs * (r + dpf(4.0f)), cy + sn * (r + dpf(4.0f)),
            dpf(1.1f), 0x00333333);
    }
    // Outer brushed silver aluminum skirt + dark inner cap + glowing red value arc + white pointer
    drawFilledCircle(buf, cx + dpf(1.0f), cy + dpf(1.5f), r, 0xB8000000, COLOR_TRANSPARENT);
    drawFilledCircle(buf, cx, cy, r, 0x00D0D4DA, 0x0044484E);
    const float innerR = r * 0.72f;
    drawFilledCircle(buf, cx, cy, innerR, 0x0022262C, COLOR_TRANSPARENT);
    if (v > 0.01f) {
        drawOpenArc(buf, cx, cy, innerR - dpf(1.6f), 135.0f, 135.0f + 270.0f * v, dpf(1.6f), 0x00FF3333);
    }
    const float ptrRad = (135.0f + 270.0f * v) * 0.0174532925f;
    drawThickLine(buf, cx, cy, cx + cosf(ptrRad) * (r - dpf(1.5f)), cy + sinf(ptrRad) * (r - dpf(1.5f)),
                  dpf(1.8f), 0x00FFFFFF);
}

void drawBuiltinDevicePanel(
    DrawBuf buf,
    Rect bodyRc,
    const(TransportModel)* model,
    const(PluginSlotState)* plug,
    const ref BuiltinDeviceUiState ui,
    FontRef fnt
) {
    if (!plug || bodyRc.width < dp(180) || bodyRc.height < dp(120)) return;
    const pal = model ? model.palette() : getFlPalette();
    auto aaGuard = ScopedRetroAntialias(pal.retroBevel);

    const dl = buildBuiltinDeviceLayout(bodyRc, plug, ui, pal);
    if (dl.kind == BuiltinDeviceKind.GENERIC) return;

    FontRef uiFnt = getDawFont(10, fnt, FontWeight.Normal);
    FontRef valFnt = getDawMonoFont(9, fnt, FontWeight.Normal);

    // Subtle device frame
    drawThemeCard(buf, Rect(bodyRc.left + dp(6), bodyRc.top + dp(3), bodyRc.right - dp(6), bodyRc.bottom - dp(4)),
                  pal.bgPanel, pal.borderStrong, pal, 3.0f, false);

    // 1. Device-specific visualizers
    switch (dl.kind) {
        case BuiltinDeviceKind.EQ: {
            static immutable uint[8] BAND_COLS = [
                0x00E04040, 0x00E07020, 0x00D0C020, 0x0040B040,
                0x0030A0A0, 0x004080E0, 0x008060D0, 0x00C050A0,
            ];
            static immutable string[6] TYPE_NAMES = ["Off", "LP", "HP", "LS", "HS", "Bell"];
            static immutable float[8] DEF_FREQS = [30, 80, 250, 700, 2000, 5000, 10000, 16000];

            // Left Main Fruity EQ 2 / EQ Eight Curve + FFT Heatmap Canvas
            Rect c = dl.canvasRc;
            drawThemeCard(buf, c, 0x000F131A, pal.borderSubtle, pal, 2.0f, true);
            const int pad = dp(8);
            const int sw = c.width - pad * 2;
            const int sh = c.height - pad * 2;
            if (sw > dp(20) && sh > dp(16)) {
                // Log frequency gridlines (30, 50, 100, 200, 500, 1k, 2k, 5k, 10k) & dB gridlines (-12, 0, +12)
                static immutable float[7] GRID_HZ = [50, 100, 200, 500, 1000, 5000, 10000];
                static immutable string[7] GRID_LBL = ["50", "100", "200", "500", "1k", "5k", "10k"];
                foreach (gi, hz; GRID_HZ) {
                    const int gx = c.left + pad + cast(int)(eqFreqToNorm(hz) * sw);
                    buf.fillRect(Rect(gx, c.top + dp(2), gx + dp(1), c.bottom - dp(2)), 0xE8FFFFFF);
                    if (gx + dp(20) < c.right) {
                        drawVCenteredText(buf, valFnt, gx + dp(2),
                                          Rect(gx, c.bottom - dp(13), gx + dp(26), c.bottom - dp(1)),
                                          toUTF32(GRID_LBL[gi]), 0x80FFFFFF);
                    }
                }
                const int yZero = c.top + pad + sh / 2;
                const int yP12 = c.top + pad + sh / 4;
                const int yM12 = c.top + pad + (sh * 3) / 4;
                buf.fillRect(Rect(c.left + dp(2), yP12, c.right - dp(2), yP12 + dp(1)), 0xECFFFFFF);
                buf.fillRect(Rect(c.left + dp(2), yZero, c.right - dp(2), yZero + dp(1)), 0xD0FFFFFF);
                buf.fillRect(Rect(c.left + dp(2), yM12, c.right - dp(2), yM12 + dp(1)), 0xECFFFFFF);

                // Fruity EQ 2 style FFT spectrum overlay (soft heat bars + smooth trace)
                void plotFftFill(const(float)[] mags, uint barCol, uint lineCol) {
                    if (mags.length < 2) return;
                    float px = c.left + pad;
                    float py = c.bottom - pad - normalizeDb(mags[0], -72.0f, 6.0f) * sh;
                    foreach (i; 1 .. mags.length) {
                        const float t = cast(float)i / cast(float)(mags.length - 1);
                        const float x = c.left + pad + t * sw;
                        const float y = c.bottom - pad - normalizeDb(mags[i], -72.0f, 6.0f) * sh;
                        if (cast(int)y < c.bottom - pad) {
                            const int bx0 = cast(int)px;
                            const int bx1 = (cast(int)x > bx0) ? cast(int)x : (bx0 + dp(1));
                            buf.fillRect(Rect(bx0, cast(int)y, bx1, c.bottom - pad), barCol);
                        }
                        drawThickLine(buf, px, py, x, y, dpf(1.3f), lineCol);
                        px = x;
                        py = y;
                    }
                }
                plotFftFill(plug.specInDb, 0xDF4080E0, 0x904080E0);
                plotFftFill(plug.specOutDb, 0xD4F59E0B, 0x48FFC040);

                // Per-band translucent response curves & fills toward 0dB
                enum int CURVE_N = 56;
                foreach (b; 0 .. 8) {
                    if (eqNormToTypeIdx(readPlugParam(plug, cast(uint)b, 0.0f)) == 0) continue;
                    const uint bc = BAND_COLS[b];
                    const uint bcFill = (bc & 0x00FFFFFF) | 0xDE000000;
                    const uint bcLine = (bc & 0x00FFFFFF) | 0x70000000;
                    float bpx = c.left + pad;
                    float bpy = yZero;
                    foreach (i; 0 .. CURVE_N + 1) {
                        const float t = cast(float)i / cast(float)CURVE_N;
                        const float hz = 20.0f * powf(1000.0f, t);
                        float bdb = eqBandGainDbAtFreq(plug, b, hz);
                        if (bdb < -25.0f) bdb = -25.0f; else if (bdb > 25.0f) bdb = 25.0f;
                        const float x = c.left + pad + t * sw;
                        const float y = c.top + pad + (1.0f - (bdb + 25.0f) / 50.0f) * sh;
                        if (i > 0) {
                            const int ry0 = (y < yZero) ? cast(int)y : yZero;
                            const int ry1 = (y > yZero) ? cast(int)y : yZero;
                            if (ry1 > ry0) {
                                const int rx1 = (cast(int)x > cast(int)bpx) ? cast(int)x : (cast(int)bpx + 1);
                                buf.fillRect(Rect(cast(int)bpx, ry0, rx1, ry1), bcFill);
                            }
                            drawThickLine(buf, bpx, bpy, x, y, dpf(1.2f), bcLine);
                        }
                        bpx = x;
                        bpy = y;
                    }
                }

                // Crisp white/cream composite EQ response curve
                float prevX = c.left + pad;
                float prevDb = eqCompositeDbAtFreq(plug, 20.0f);
                if (prevDb < -25.0f) prevDb = -25.0f; else if (prevDb > 25.0f) prevDb = 25.0f;
                float prevY = c.top + pad + (1.0f - (prevDb + 25.0f) / 50.0f) * sh;
                foreach (i; 1 .. CURVE_N + 1) {
                    const float t = cast(float)i / cast(float)CURVE_N;
                    const float hz = 20.0f * powf(1000.0f, t);
                    float db = eqCompositeDbAtFreq(plug, hz);
                    if (db < -25.0f) db = -25.0f; else if (db > 25.0f) db = 25.0f;
                    const float x = c.left + pad + t * sw;
                    const float y = c.top + pad + (1.0f - (db + 25.0f) / 50.0f) * sh;
                    drawThickLine(buf, prevX, prevY, x, y, dpf(2.2f), 0x00F4F7FC);
                    prevX = x;
                    prevY = y;
                }

                // 8 colored numbered Fruity EQ2 / EQ Eight band tokens ('1'..'8')
                foreach (b; 0 .. 8) {
                    const int ti = eqNormToTypeIdx(readPlugParam(plug, cast(uint)b, 0.0f));
                    if (ti == 0 && b != ui.eqSelectedBand) continue;
                    const float fn = readPlugParam(plug, cast(uint)(8 + b), eqFreqToNorm(DEF_FREQS[b]));
                    const float gn = readPlugParam(plug, cast(uint)(16 + b), 0.5f);
                    float db = (gn - 0.5f) * 48.0f;
                    if (db < -25.0f) db = -25.0f; else if (db > 25.0f) db = 25.0f;
                    const float bx = c.left + pad + fn * sw;
                    const float by = c.top + pad + (1.0f - (db + 25.0f) / 50.0f) * sh;
                    const bool isSel = (b == ui.eqSelectedBand);
                    if (isSel) {
                        buf.fillRect(Rect(cast(int)bx, c.top + dp(2), cast(int)bx + dp(1), c.bottom - dp(2)),
                                     (BAND_COLS[b] & 0x00FFFFFF) | 0xA8000000);
                        drawFilledCircle(buf, bx, by, dpf(9.0f), (BAND_COLS[b] & 0x00FFFFFF) | 0x90000000,
                                         0x00FFFFFF);
                    }
                    const uint tokFill = (ti == 0)
                        ? adjustColorBrightness(BAND_COLS[b], -28)
                        : BAND_COLS[b];
                    drawFilledCircle(buf, bx, by, dpf(isSel ? 7.2f : 6.0f), tokFill, 0x00FFFFFF);
                    drawCenteredText(buf, valFnt,
                                     Rect(cast(int)bx - dp(7), cast(int)by - dp(7), cast(int)bx + dp(7), cast(int)by + dp(7)),
                                     toUTF32(format("%d", b + 1)), 0x00FFFFFF);
                }

                // Selected band HUD badge at top-left
                const int sb = (ui.eqSelectedBand >= 0 && ui.eqSelectedBand < 8) ? ui.eqSelectedBand : 0;
                const int sbType = eqNormToTypeIdx(readPlugParam(plug, cast(uint)sb, 0.0f));
                const float sbFreq = eqNormToFreq(readPlugParam(plug, cast(uint)(8 + sb), eqFreqToNorm(DEF_FREQS[sb])));
                const float sbGain = (readPlugParam(plug, cast(uint)(16 + sb), 0.5f) - 0.5f) * 48.0f;
                const float sbQ = eqNormToQ(readPlugParam(plug, cast(uint)(24 + sb), 0.425f));
                const string hud = format("B%d %s · %s · %+.1fdB · Q%.2f",
                                          sb + 1, TYPE_NAMES[sbType], formatFreq(sbFreq), sbGain, sbQ);
                drawVCenteredText(buf, valFnt, c.left + dp(8),
                                  Rect(c.left + dp(4), c.top + dp(2), c.right - dp(4), c.top + dp(16)),
                                  toUTF32(hud), BAND_COLS[sb]);
            }

            // Right Inspector Card
            drawThemeCard(buf, dl.auxRc, pal.bgPanelAlt, pal.borderSubtle, pal, 2.0f, false);
            const int sbIdx = (ui.eqSelectedBand >= 0 && ui.eqSelectedBand < 8) ? ui.eqSelectedBand : 0;
            drawCenteredText(buf, uiFnt, Rect(dl.auxRc.left, dl.auxRc.top + dp(2), dl.auxRc.right, dl.auxRc.top + dp(17)),
                             toUTF32(format("BAND %d", sbIdx + 1)), BAND_COLS[sbIdx]);

            // Bottom 8 rainbow band strip cells (matching EqDevicePanel.java)
            foreach (b; 0 .. 8) {
                if (b >= dl.pills.length) break;
                Rect br = dl.pills[b].rc;
                const int ti = eqNormToTypeIdx(readPlugParam(plug, cast(uint)b, 0.0f));
                const bool sel = (b == ui.eqSelectedBand);
                const uint cellBg = sel ? adjustColorBrightness(pal.bgControlHover, 6) : pal.bgWindow;
                drawThemeCard(buf, br, cellBg, sel ? BAND_COLS[b] : pal.borderStrong, pal, 1.5f, false);
                buf.fillRect(Rect(br.left + dp(2), br.top + dp(2), br.right - dp(2), br.top + dp(5)),
                             ti > 0 ? BAND_COLS[b] : adjustColorBrightness(BAND_COLS[b], -26));
                drawFilledCircle(buf, br.left + dp(9), br.top + dp(14), dpf(3.2f), BAND_COLS[b], COLOR_TRANSPARENT);
                drawVCenteredText(buf, valFnt, br.left + dp(15), Rect(br.left + dp(14), br.top + dp(6), br.right, br.top + dp(22)),
                                  toUTF32(format("%d %s", b + 1, TYPE_NAMES[ti])),
                                  ti > 0 || sel ? pal.fgText : pal.fgMuted);
                const float bf = eqNormToFreq(readPlugParam(plug, cast(uint)(8 + b), eqFreqToNorm(DEF_FREQS[b])));
                drawCenteredText(buf, valFnt, Rect(br.left, br.top + dp(21), br.right, br.bottom - dp(2)),
                                 toUTF32(formatFreq(bf)), pal.fgMuted);
            }
            break;
        }
        case BuiltinDeviceKind.COMPRESSOR:
        case BuiltinDeviceKind.LIMITER: {
            Rect c = dl.canvasRc;
            const int grW = dp(24);
            Rect curveRc = Rect(c.left, c.top, c.right - grW - dp(4), c.bottom);
            Rect grRc = Rect(curveRc.right + dp(4), c.top, c.right, c.bottom);
            drawThemeCard(buf, curveRc, pal.bgLcd, pal.borderSubtle, pal, 2.0f, true);
            drawPanelGrid(buf, curveRc, pal);

            const int pad = dp(6);
            const int cw = curveRc.width - pad * 2;
            const int ch = curveRc.height - pad * 2;
            if (cw > dp(12) && ch > dp(12)) {
                // Unity diagonal reference
                drawThickLine(buf, curveRc.left + pad, curveRc.bottom - pad,
                              curveRc.right - pad, curveRc.top + pad, dpf(1.0f), pal.borderStrong);
                const float threshDb = (dl.kind == BuiltinDeviceKind.LIMITER)
                    ? (readPlugParam(plug, 0, 1.0f) * 12.0f - 12.0f)
                    : ((readPlugParam(plug, 0, 1.0f) - 1.0f) * 60.0f);
                const float ratio = (dl.kind == BuiltinDeviceKind.LIMITER)
                    ? 100.0f
                    : (1.0f + readPlugParam(plug, 1, 0.0f) * 19.0f);
                const float makeupDb = (dl.kind == BuiltinDeviceKind.LIMITER)
                    ? (readPlugParam(plug, 3, 0.0f) * 24.0f)
                    : (readPlugParam(plug, 5, 0.0f) * 24.0f);

                float prevX = curveRc.left + pad;
                float prevY = curveRc.bottom - pad;
                enum int STEPS = 32;
                foreach (i; 0 .. STEPS + 1) {
                    const float inDb = -60.0f + (cast(float)i / STEPS) * 60.0f;
                    float outDb = inDb;
                    if (inDb > threshDb) {
                        outDb = threshDb + (inDb - threshDb) / ratio;
                    }
                    outDb += makeupDb;
                    if (outDb < -60.0f) outDb = -60.0f; else if (outDb > 0.0f) outDb = 0.0f;
                    const float x = curveRc.left + pad + ((inDb + 60.0f) / 60.0f) * cw;
                    const float y = curveRc.bottom - pad - ((outDb + 60.0f) / 60.0f) * ch;
                    if (i > 0) {
                        drawThickLine(buf, prevX, prevY, x, y, dpf(2.0f),
                                      dl.kind == BuiltinDeviceKind.LIMITER ? pal.accentRed : pal.accentAmber);
                    }
                    prevX = x;
                    prevY = y;
                }
                if (plug.hasMetering && plug.meterInDb > -60.0f) {
                    const float inN = normalizeDb(plug.meterInDb, -60.0f, 0.0f);
                    const float outN = normalizeDb(plug.meterOutDb, -60.0f, 0.0f);
                    drawFilledCircle(buf, curveRc.left + pad + inN * cw,
                                     curveRc.bottom - pad - outN * ch, dpf(4.0f), pal.accentCyan, pal.bgLcd);
                }
            }
            drawDbMeterBar(buf, grRc, -plug.meterGrDb, 0.0f, 24.0f, pal.accentOrange, "GR", valFnt, pal);
            break;
        }
        case BuiltinDeviceKind.HOTT: {
            // Top Xfer OTT / Hott header banner
            drawThemeCard(buf, dl.footerRc, 0x001A6B8A, 0x0000A8CC, pal, 2.0f, false);
            drawVCenteredText(buf, uiFnt, dl.footerRc.left + dp(8), dl.footerRc,
                              "⚡ HOTT  ·  OTT MULTIBAND COMPRESSOR"d, 0x0000D4FF);

            // Left Split Freq panel card
            drawThemeCard(buf, dl.auxRc, pal.bgPanelAlt, pal.borderSubtle, pal, 2.0f, false);
            drawCenteredText(buf, valFnt, Rect(dl.auxRc.left, dl.auxRc.top + dp(1), dl.auxRc.right, dl.auxRc.top + dp(13)),
                             "SPLIT FREQ"d, pal.fgMuted);

            // Center Xfer OTT Band Meter Panel
            Rect c = dl.canvasRc;
            drawThemeCard(buf, c, 0x00151C24, pal.borderSubtle, pal, 2.0f, true);

            // dB gridlines across bands (-80 .. 0 dB) & top inspector header
            Rect b0 = dl.hottBarRcs[0];
            Rect b2 = dl.hottBarRcs[2];
            foreach (db; [-80, -70, -60, -50, -40, -30, -20, -10, 0]) {
                const float n = normalizeDb(cast(float)db, -80.0f, 0.0f);
                const int gx = b0.left + cast(int)(n * b0.width);
                buf.fillRect(Rect(gx, b0.top, gx + dp(1), b2.bottom), 0xDCFFFFFF);
            }
            foreach (db; [-80, -60, -40, -20, 0]) {
                const float n = normalizeDb(cast(float)db, -80.0f, 0.0f);
                const int gx = b0.left + cast(int)(n * b0.width);
                drawVCenteredText(buf, valFnt, gx - dp(4), Rect(gx - dp(6), c.bottom - dp(16), gx + dp(18), c.bottom - dp(2)),
                                  toUTF32(format("%d", db < 0 ? -db : 0)), 0x90FFFFFF);
            }
            static immutable string[3] HDR_TXT = ["Att/Rel", "Below", "Above"];
            drawVCenteredText(buf, valFnt, dl.hottInspectorRcs[0].left,
                              Rect(dl.hottInspectorRcs[0].left, c.top + dp(2), dl.hottInspectorRcs[0].right, c.top + dp(14)),
                              toUTF32(HDR_TXT[ui.hottTab]), 0x00A8B8C8);

            // 3 Band Horizontal Threshold/Compression Bars (High, Mid, Low)
            static immutable string[3] BNAMES = ["High", "Mid", "Low"];
            static immutable uint[3] DOWN_PIDS = [11u, 10u, 9u];
            static immutable uint[3] UP_PIDS   = [14u, 13u, 12u];
            static immutable uint[3] ATK_PIDS  = [19u, 18u, 17u];
            static immutable uint[3] REL_PIDS  = [22u, 21u, 20u];
            static immutable uint[3] DRAT_PIDS = [25u, 24u, 23u];
            static immutable uint[3] URAT_PIDS = [28u, 27u, 26u];
            static immutable float[3] DEF_DN   = [(-35.5f + 60.0f) / 60.0f, (-30.2f + 60.0f) / 60.0f, (-33.8f + 60.0f) / 60.0f];
            static immutable float[3] DEF_UP   = [(-40.8f + 60.0f) / 72.0f, (-41.8f + 60.0f) / 72.0f, (-40.8f + 60.0f) / 72.0f];
            static immutable float[3] DEF_ATK  = [0.710f, 0.783f, 0.893f];
            static immutable float[3] DEF_REL  = [0.560f, 0.725f, 0.725f];
            static immutable float[3] DEF_DR   = [0.999f, 0.985f, 0.985f];
            float[3] grs = [plug.meterHighGrDb, plug.meterMidGrDb, plug.meterLowGrDb];
            float[3] ins = [plug.meterHighInDb, plug.meterMidInDb, plug.meterLowInDb];

            foreach (bi; 0 .. 3) {
                Rect barRc = dl.hottBarRcs[bi];
                Rect inspRc = dl.hottInspectorRcs[bi];
                buf.fillRect(barRc, 0x0026323D);

                const float downDb = readPlugParam(plug, DOWN_PIDS[bi], DEF_DN[bi]) * 60.0f - 60.0f;
                const float upDb = readPlugParam(plug, UP_PIDS[bi], DEF_UP[bi]) * 72.0f - 60.0f;
                const int upX = barRc.left + cast(int)(normalizeDb(upDb, -80.0f, 0.0f) * barRc.width);
                const int downX = barRc.left + cast(int)(normalizeDb(downDb, -80.0f, 0.0f) * barRc.width);

                // Cyan upward zone & dark downward zone
                if (upX > barRc.left) {
                    buf.fillRect(Rect(barRc.left, barRc.top, upX, barRc.bottom), 0xB000D4FF);
                }
                if (downX < barRc.right) {
                    buf.fillRect(Rect(downX > barRc.left ? downX : barRc.left, barRc.top, barRc.right, barRc.bottom), 0x70121C26);
                }
                // Output bar & orange input tick
                const float bandInDb = (ins[bi] > -80.0f) ? ins[bi] : plug.meterOutDb;
                if (bandInDb > -80.0f) {
                    const float outN = normalizeDb(plug.meterOutDb > -80.0f ? plug.meterOutDb : bandInDb, -80.0f, 0.0f);
                    const int barH = dp(4);
                    const int by = barRc.middley - barH / 2;
                    buf.fillRect(Rect(barRc.left, by, barRc.left + cast(int)(outN * barRc.width), by + barH), 0x5500D4FF);
                    const int inX = barRc.left + cast(int)(normalizeDb(bandInDb, -80.0f, 0.0f) * barRc.width);
                    buf.fillRect(Rect(inX - dp(1), barRc.middley - dp(5), inX + dp(2), barRc.middley + dp(5)), 0x00FF8C00);
                }
                // Draggable thick cyan threshold lines (upX, downX)
                buf.fillRect(Rect(upX - dp(1), barRc.top, upX + dp(1), barRc.bottom), 0x0000D4FF);
                buf.fillRect(Rect(downX - dp(1), barRc.top, downX + dp(1), barRc.bottom), 0x0000D4FF);

                // Band label & threshold readout
                drawVCenteredText(buf, valFnt, barRc.right - dp(28),
                                  Rect(barRc.right - dp(30), barRc.top + dp(1), barRc.right - dp(2), barRc.top + dp(12)),
                                  toUTF32(BNAMES[bi]), 0x70FFFFFF);
                drawVCenteredText(buf, valFnt, (downX > barRc.left + dp(28)) ? (downX - dp(28)) : (barRc.left + dp(2)),
                                  Rect(barRc.left + dp(2), barRc.bottom - dp(13), barRc.right, barRc.bottom),
                                  toUTF32(format("%.1f", downDb)), 0x0000D4FF);

                // Right inspector cell per band (Att/Rel, Below Thresh/Ratio, or Above Thresh/Ratio)
                drawThemeCard(buf, inspRc, 0x001A242E, 0x002B3A48, pal, 1.0f, false);
                string topStr, botStr;
                if (ui.hottTab == 0) {
                    const float atkMs = 0.1f * powf(1000.0f, readPlugParam(plug, ATK_PIDS[bi], DEF_ATK[bi]));
                    const float relMs = 10.0f * powf(100.0f, readPlugParam(plug, REL_PIDS[bi], DEF_REL[bi]));
                    topStr = format("%.1f ms", atkMs);
                    botStr = format("%.0f ms", relMs);
                } else if (ui.hottTab == 1) {
                    const float urN = readPlugParam(plug, URAT_PIDS[bi], 0.76f);
                    const float uRat = (urN >= 0.999f) ? 1000.0f : (1.0f / (1.0f - urN));
                    topStr = format("%.1fdB", upDb);
                    botStr = format("1:%.2f", uRat);
                } else {
                    const float drN = readPlugParam(plug, DRAT_PIDS[bi], DEF_DR[bi]);
                    const float dRat = (drN >= 0.999f) ? 1000.0f : (1.0f / (1.0f - drN));
                    topStr = format("%.1fdB", downDb);
                    botStr = (dRat >= 999.0f) ? "inf:1" : format("%.1f:1", dRat);
                }
                drawVCenteredText(buf, valFnt, inspRc.left + dp(4),
                                  Rect(inspRc.left, inspRc.top, inspRc.right, inspRc.middley),
                                  toUTF32(topStr), 0x0000D4FF);
                drawVCenteredText(buf, valFnt, inspRc.left + dp(4),
                                  Rect(inspRc.left, inspRc.middley, inspRc.right, inspRc.bottom),
                                  toUTF32(botStr), 0x006CE5FF);
            }
            break;
        }
        case BuiltinDeviceKind.MAXIM: {
            Rect c = dl.canvasRc;
            drawThemeCard(buf, c, pal.bgLcd, pal.borderSubtle, pal, 2.0f, true);
            drawPanelGrid(buf, c, pal);
            static immutable uint[4] MCOLS = [0x00E25822, 0x0010B981, 0x003B82F6, 0x00F59E0B];
            const uint col = (ui.maximBandTab >= 0 && ui.maximBandTab < 4) ? MCOLS[ui.maximBandTab] : pal.accentPurple;
            if (ui.maximBandTab == 4) {
                const float lx = c.left + dp(8) + readPlugParam(plug, 0, 0.461f) * (c.width - dp(16));
                const float hx = c.left + dp(8) + readPlugParam(plug, 1, 0.436f) * (c.width - dp(16));
                drawThickLine(buf, lx, c.top + dp(6), lx, c.bottom - dp(6), dpf(2.0f), MCOLS[0]);
                drawThickLine(buf, hx, c.top + dp(6), hx, c.bottom - dp(6), dpf(2.0f), MCOLS[2]);
                drawCenteredText(buf, uiFnt, c, "3-Band Linkwitz-Riley Crossover"d, pal.fgMuted);
            } else {
                const uint base = cast(uint)(4 + ui.maximBandTab * 10);
                const float ceilN = readPlugParam(plug, base + 9, 0.975f);
                const float cy = c.bottom - dp(6) - ceilN * (c.height - dp(12));
                drawThickLine(buf, c.left + dp(6), c.bottom - dp(6), c.right - dp(6), cy, dpf(2.0f), col);
                drawThickLine(buf, c.left + dp(6), cy, c.right - dp(6), cy, dpf(1.2f), pal.accentRed);
            }
            break;
        }
        case BuiltinDeviceKind.DELAY: {
            Rect c = dl.canvasRc;
            drawThemeCard(buf, c, pal.bgLcd, pal.borderSubtle, pal, 2.0f, true);
            const float tl = readPlugParam(plug, 0, 0.6f);
            const float tr = readPlugParam(plug, 1, 0.6f);
            const float fb = readPlugParam(plug, 2, 0.4f);
            const int baseY = c.bottom - dp(8);
            const int spanH = c.height - dp(16);
            const int spanW = c.width - dp(20);
            float amp = 1.0f;
            foreach (tap; 0 .. 8) {
                const float tFrac = (tap + 1) * (0.04f + 0.08f * ((tap % 2 == 0) ? tl : tr));
                if (tFrac > 1.0f) break;
                const int tx = c.left + dp(10) + cast(int)(tFrac * spanW);
                const int th = cast(int)(amp * spanH);
                const uint col = (tap % 2 == 0) ? pal.accentCyan : pal.accentOrange;
                drawThickLine(buf, tx, baseY, tx, baseY - th, dpf(3.0f), col);
                amp *= fb;
            }
            break;
        }
        case BuiltinDeviceKind.REVERB:
        case BuiltinDeviceKind.CONVOLVER: {
            Rect c = dl.canvasRc;
            drawThemeCard(buf, c, pal.bgLcd, pal.borderSubtle, pal, 2.0f, true);
            const float room = (dl.kind == BuiltinDeviceKind.REVERB) ? readPlugParam(plug, 0, 0.5f) : readPlugParam(plug, 1, 1.0f);
            const float damp = (dl.kind == BuiltinDeviceKind.REVERB) ? readPlugParam(plug, 1, 0.5f) : 0.3f;
            const float pre = (dl.kind == BuiltinDeviceKind.REVERB) ? readPlugParam(plug, 3, 0.1f) : readPlugParam(plug, 2, 0.0f);
            const int pad = dp(8);
            const int x0 = c.left + pad + cast(int)(pre * 0.25f * (c.width - pad * 2));
            const int rw = c.right - pad - x0;
            const int rh = c.height - pad * 2;
            if (rw > dp(12) && rh > dp(10)) {
                float px = x0;
                float py = c.top + pad;
                const float rate = 1.5f + (1.0f - room) * 4.5f + damp * 1.5f;
                foreach (i; 1 .. 33) {
                    const float t = cast(float)i / 32.0f;
                    const float x = x0 + t * rw;
                    const float y = (c.bottom - pad) - expf(-t * rate) * rh;
                    drawThickLine(buf, px, py, x, y, dpf(2.0f),
                                  dl.kind == BuiltinDeviceKind.REVERB ? pal.accentPurple : pal.accentCyan);
                    px = x;
                    py = y;
                }
            }
            break;
        }
        case BuiltinDeviceKind.PHASER: {
            Rect c = dl.canvasRc;
            drawThemeCard(buf, c, pal.bgLcd, pal.borderSubtle, pal, 2.0f, true);
            const int pad = dp(8);
            const int sw = c.width - pad * 2;
            const int sh = c.height - pad * 2;
            const float midY = c.top + pad + sh * 0.5f;
            if (plug.scopeL.length > 1 && plug.scopeR.length > 1) {
                const size_t n = plug.scopeL.length < plug.scopeR.length ? plug.scopeL.length : plug.scopeR.length;
                foreach (i; 1 .. n) {
                    const float x0 = c.left + pad + cast(float)(i - 1) / (n - 1) * sw;
                    const float x1 = c.left + pad + cast(float)i / (n - 1) * sw;
                    drawThickLine(buf, x0, midY - plug.scopeL[i - 1] * sh * 0.44f,
                                  x1, midY - plug.scopeL[i] * sh * 0.44f, dpf(1.5f), pal.accentCyan);
                    drawThickLine(buf, x0, midY - plug.scopeR[i - 1] * sh * 0.44f,
                                  x1, midY - plug.scopeR[i] * sh * 0.44f, dpf(1.2f), pal.accentOrange);
                }
            } else {
                const float rate = 1.0f + readPlugParam(plug, 0, 0.3f) * 6.0f;
                const float depth = readPlugParam(plug, 1, 0.7f);
                float px = c.left + pad;
                float py = midY;
                foreach (i; 1 .. 41) {
                    const float t = cast(float)i / 40.0f;
                    const float x = c.left + pad + t * sw;
                    const float y = midY - sinf(t * 6.28318f * rate) * depth * sh * 0.42f;
                    drawThickLine(buf, px, py, x, y, dpf(1.8f), pal.accentCyan);
                    px = x;
                    py = y;
                }
            }
            break;
        }
        case BuiltinDeviceKind.BITCRUSHER: {
            Rect c = dl.canvasRc;
            drawThemeCard(buf, c, pal.bgLcd, pal.borderSubtle, pal, 2.0f, true);
            drawPanelGrid(buf, c, pal);
            const int steps = cast(int)(2 + readPlugParam(plug, 0, 0.5f) * 14.0f);
            const int pad = dp(8);
            const int sw = c.width - pad * 2;
            const int sh = c.height - pad * 2;
            float px = c.left + pad;
            float py = c.bottom - pad;
            foreach (i; 1 .. 49) {
                const float t = cast(float)i / 48.0f;
                const float q = cast(float)(cast(int)(t * steps)) / cast(float)steps;
                const float x = c.left + pad + t * sw;
                const float y = c.bottom - pad - q * sh;
                drawThickLine(buf, px, py, x, y, dpf(2.0f), pal.accentOrange);
                px = x;
                py = y;
            }
            break;
        }
        case BuiltinDeviceKind.CHORUS:
        case BuiltinDeviceKind.VOCODEY:
        case BuiltinDeviceKind.STEREO_WIDTH:
        case BuiltinDeviceKind.EPIANO: {
            Rect c = dl.canvasRc;
            drawThemeCard(buf, c, pal.bgLcd, pal.borderSubtle, pal, 2.0f, true);
            drawPanelGrid(buf, c, pal);
            const float p0 = readPlugParam(plug, 0, 0.4f);
            const float p1 = readPlugParam(plug, 1, 0.5f);
            const int pad = dp(8);
            const float midY = c.middley;
            const int sw = c.width - pad * 2;
            const int sh = c.height - pad * 2;
            float px = c.left + pad;
            float py = midY;
            foreach (i; 1 .. 41) {
                const float t = cast(float)i / 40.0f;
                const float x = c.left + pad + t * sw;
                const float y = midY - sinf(t * 6.28318f * (1.5f + p0 * 4.0f)) * (0.25f + p1 * 0.2f) * sh;
                drawThickLine(buf, px, py, x, y, dpf(1.8f), pal.accentCyan);
                px = x;
                py = y;
            }
            break;
        }
        case BuiltinDeviceKind.ENVELOPE_SHAPER: {
            Rect c = dl.canvasRc;
            drawThemeCard(buf, c, pal.bgLcd, pal.borderSubtle, pal, 2.0f, true);
            drawPanelGrid(buf, c, pal);
            const int pad = dp(8);
            const int sw = c.width - pad * 2;
            const int sh = c.height - pad * 2;
            int nPts = 2 + cast(int)(readPlugParam(plug, 21, 0.14f) * 14.0f + 0.5f);
            if (nPts < 2) nPts = 2;
            if (nPts > 16) nPts = 16;
            float px = c.left + pad;
            float py = c.bottom - pad - readPlugParam(plug, 5, 0.5f) * sh;
            foreach (pi; 0 .. nPts) {
                const float x = c.left + pad + (cast(float)pi / cast(float)(nPts - 1)) * sw;
                const float y = c.bottom - pad - readPlugParam(plug, cast(uint)(5 + pi), 0.5f) * sh;
                if (pi > 0) drawThickLine(buf, px, py, x, y, dpf(2.0f), pal.accentAmber);
                drawFilledCircle(buf, x, y, dpf(4.0f), pal.accentOrange, pal.bgLcd);
                px = x;
                py = y;
            }
            break;
        }
        case BuiltinDeviceKind.ACID_BASS: {
            // Iconic Roland TB-303 charcoal header + brushed silver metallic chassis + vertical dividers + red LEDs
            drawThemeCard(buf, dl.auxRc, 0x0033363B, 0x001D2024, pal, 0.0f, false);
            drawAcidLed(buf, dl.auxRc.left + dp(10), dl.auxRc.middley, dpf(3.5f), true);
            drawVCenteredText(buf, uiFnt, dl.auxRc.left + dp(20), dl.auxRc,
                              "ACID BASS  ·  TB-303 BASS LINE"d, 0x00EEEEEE);

            Rect c = dl.canvasRc;
            Rect kbd = dl.footerRc;
            drawVGradientRect(buf, c, 0x00EAEAEA, 0x00B5B5B5);
            drawVGradientRect(buf, kbd, 0x00E4E4E4, 0x00B0B0B0);
            buf.fillRect(Rect(c.left, c.top, c.right, c.top + dp(1)), 0x00222222);
            buf.fillRect(Rect(c.left, kbd.top, c.right, kbd.top + dp(2)), 0x00141414);

            // 8 vertical black partition lines across upper faceplate
            const int colW = c.width / 8;
            foreach (ci; 1 .. 8) {
                const int lx = c.left + ci * colW;
                buf.fillRect(Rect(lx, c.top, lx + dp(1), c.bottom), 0x002C2C2C);
            }

            // Col 0: WAVEFORM SAW / SQR switch + glowing Red LEDs
            drawCenteredText(buf, valFnt, Rect(c.left, c.top + dp(2), c.left + colW, c.top + dp(15)),
                             "WAVEFORM"d, 0x00222222);
            if (dl.pills.length > 0) {
                const auto wfPill = dl.pills[0];
                const bool isSq = (readPlugParam(plug, 0, 0.0f) >= 0.5f);
                drawAcidLed(buf, wfPill.rc.left + dp(8), wfPill.rc.middley - dp(8), dpf(2.8f), !isSq);
                drawCenteredText(buf, valFnt, Rect(wfPill.rc.left, wfPill.rc.middley, wfPill.rc.left + dp(18), wfPill.rc.bottom),
                                 "SAW"d, 0x00222222);
                Rect swBtn = Rect(wfPill.rc.middlex - dp(9), wfPill.rc.middley - dp(9),
                                  wfPill.rc.middlex + dp(9), wfPill.rc.middley + dp(9));
                drawVGradientRect(buf, swBtn, isSq ? 0x00CCCCCC : 0x00F6F6F6, isSq ? 0x00999999 : 0x00C8C8C8);
                buf.drawFrame(swBtn, 0x00333333, Rect(1, 1, 1, 1), 0);
                drawCenteredText(buf, uiFnt, swBtn, "~"d, 0x00111111);
                drawAcidLed(buf, wfPill.rc.right - dp(8), wfPill.rc.middley - dp(8), dpf(2.8f), isSq);
                drawCenteredText(buf, valFnt, Rect(wfPill.rc.right - dp(18), wfPill.rc.middley, wfPill.rc.right, wfPill.rc.bottom),
                                 "SQR"d, 0x00222222);
            }

            // Lower bay Left: OCTAVE ◀ ▶ buttons + digital Red LED C1..C5 box
            const int octVal = 1 + cast(int)(readPlugParam(plug, 8, 0.5f) * 4.0f + 0.5f);
            drawCenteredText(buf, valFnt, Rect(kbd.left + dp(4), kbd.top + dp(6), kbd.left + dp(76), kbd.top + dp(18)),
                             "OCTAVE"d, 0x00222222);
            if (dl.pills.length >= 3) {
                foreach (oi; 1 .. 3) {
                    Rect r = dl.pills[oi].rc;
                    drawFilledCircle(buf, r.middlex, r.middley, r.width * 0.5f, 0x00DEDEDE, 0x00333333);
                    drawCenteredText(buf, valFnt, r, oi == 1 ? "◀"d : "▶"d, 0x00111111);
                }
            }
            Rect dispRc = Rect(kbd.left + dp(20), kbd.top + dp(52), kbd.left + dp(60), kbd.top + dp(68));
            drawThemeCard(buf, dispRc, 0x001D1F22, 0x00444444, pal, 2.0f, true);
            drawCenteredText(buf, valFnt, dispRc, toUTF32(format("C%d", octVal)), 0x00FF3333);

            // Lower bay Center: 12 staggered TB-303 keys (dl.pills[3..15]) with glowing Red LEDs above each
            foreach (ki; 3 .. 15) {
                if (ki >= dl.pills.length) break;
                const auto kp = dl.pills[ki];
                const bool isBlk = kp.active;
                const bool lit = (ui.acidActiveNote == kp.uiTabVal);
                drawAcidLed(buf, kp.rc.middlex, kp.rc.top - dp(6), dpf(2.6f), lit);
                if (isBlk) {
                    Rect boxRc = Rect(kp.rc.left - dp(3), kp.rc.top - dp(2), kp.rc.right + dp(3), kp.rc.bottom + dp(2));
                    buf.fillRect(boxRc, 0x00101215);
                    buf.drawFrame(boxRc, 0x00444444, Rect(1, 1, 1, 1), 0);
                }
                drawVGradientRect(buf, kp.rc, lit ? 0x00D8D8D8 : 0x00FAFAFA, lit ? 0x00909090 : 0x00C6C6C6);
                buf.drawFrame(kp.rc, 0x00444444, Rect(1, 1, 1, 1), 0);
                drawCenteredText(buf, valFnt, Rect(kp.rc.left - dp(4), kp.rc.bottom + dp(1), kp.rc.right + dp(4), kp.rc.bottom + dp(12)),
                                 toUTF32(kp.label), 0x00222222);
            }

            // Lower bay Right: circular SLIDE & ACCENT brushed buttons + glowing Red LEDs
            foreach (mi; 15 .. 17) {
                if (mi >= dl.pills.length) break;
                const auto mp = dl.pills[mi];
                drawAcidLed(buf, mp.rc.middlex, mp.rc.top - dp(8), dpf(2.6f), mp.active);
                drawFilledCircle(buf, mp.rc.middlex, mp.rc.middley, mp.rc.width * 0.5f,
                                 mp.active ? 0x00D6D6D6 : 0x00EBEBEB, 0x00333333);
                drawCenteredText(buf, valFnt, Rect(mp.rc.left - dp(8), mp.rc.bottom + dp(2), mp.rc.right + dp(8), mp.rc.bottom + dp(13)),
                                 toUTF32(mp.label), 0x00222222);
            }
            break;
        }
        case BuiltinDeviceKind.THREE_OSC: {
            drawThemeCard(buf, dl.canvasRc, pal.bgLcd, pal.borderSubtle, pal, 2.0f, true);
            const int pad = dp(5);
            const int w = dl.canvasRc.width - pad * 2;
            const int h = dl.canvasRc.height - pad * 2;
            const float a = readPlugParam(plug, 15, 0.0f) * 0.25f;
            const float d = readPlugParam(plug, 16, 0.2f) * 0.25f;
            const float s = readPlugParam(plug, 17, 0.7f);
            const float r = readPlugParam(plug, 18, 0.3f) * 0.3f;
            const float x0 = dl.canvasRc.left + pad;
            const float yBase = dl.canvasRc.bottom - pad;
            const float x1 = x0 + a * w;
            const float x2 = x1 + d * w;
            const float x3 = dl.canvasRc.right - pad - r * w;
            const float x4 = dl.canvasRc.right - pad;
            drawThickLine(buf, x0, yBase, x1, dl.canvasRc.top + pad, dpf(1.6f), pal.accentGreen);
            drawThickLine(buf, x1, dl.canvasRc.top + pad, x2, yBase - s * h, dpf(1.6f), pal.accentGreen);
            drawThickLine(buf, x2, yBase - s * h, x3, yBase - s * h, dpf(1.6f), pal.accentGreen);
            drawThickLine(buf, x3, yBase - s * h, x4, yBase, dpf(1.6f), pal.accentGreen);
            break;
        }
        case BuiltinDeviceKind.SAMPLER: {
            Rect c = dl.canvasRc;
            drawThemeCard(buf, c, pal.bgLcd, pal.borderSubtle, pal, 2.0f, true);
            const int pad = dp(6);
            const int sw = c.width - pad * 2;
            const int sh = c.height - pad * 2;
            if (plug.sampleWaveform.length > 0 && sw > dp(16)) {
                const int midY = c.middley;
                const int halfH = sh / 2;
                foreach (i, amp; plug.sampleWaveform) {
                    const int px = c.left + pad + cast(int)(cast(double)i / plug.sampleWaveform.length * sw);
                    float a = amp < 0.0f ? -amp : amp;
                    if (a > 1.0f) a = 1.0f;
                    int bh = cast(int)(a * halfH);
                    if (bh < 1) bh = 1;
                    buf.fillRect(Rect(px, midY - bh, px + dp(2), midY + bh), pal.accentCyan);
                }
            }
            const int sx = c.left + pad + cast(int)(readPlugParam(plug, 0, 0.0f) * sw);
            const int ex = c.left + pad + cast(int)(readPlugParam(plug, 1, 1.0f) * sw);
            buf.fillRect(Rect(sx, c.top + dp(3), sx + dp(2), c.bottom - dp(3)), pal.accentGreen);
            buf.fillRect(Rect(ex - dp(2), c.top + dp(3), ex, c.bottom - dp(3)), pal.accentRed);
            const string sLbl = plug.sampleName.length > 0
                ? format("%s (%.2fs)", plug.sampleName, plug.sampleDurationSec)
                : "Sampler (No Sample Loaded)";
            drawVCenteredText(buf, uiFnt, c.left + dp(8), Rect(c.left, c.top + dp(2), c.right - dp(84), c.top + dp(20)),
                              toUTF32(truncateLabel(sLbl, 28)), pal.fgText);
            break;
        }
        case BuiltinDeviceKind.ORGAN: {
            drawThemeCard(buf, dl.canvasRc, pal.bgLcd, pal.borderSubtle, pal, 2.0f, true);
            static immutable string[9] DB_NAMES = ["16'", "5⅓", "8'", "4'", "2⅔", "2'", "1⅗", "1⅓", "1'"];
            foreach (d, rc; dl.drawbarRcs) {
                const float v = readPlugParam(plug, cast(uint)d, d == 2 ? 1.0f : 0.0f);
                drawThemeCard(buf, rc, pal.bgWindow, pal.borderSubtle, pal, 1.5f, true);
                const int fh = cast(int)(v * (rc.height - dp(4)));
                const uint barCol = (d == 0 || d == 2 || d == 3 || d == 5 || d == 8) ? pal.fgText : pal.accentAmber;
                if (fh > 0) {
                    buf.fillRect(Rect(rc.left + dp(2), rc.top + dp(2), rc.right - dp(2), rc.top + dp(2) + fh), barCol);
                }
                drawCenteredText(buf, valFnt, Rect(rc.left, rc.bottom + dp(2), rc.right, rc.bottom + dp(16)),
                                 toUTF32(DB_NAMES[d]), pal.fgMuted);
            }
            break;
        }
        case BuiltinDeviceKind.DRUM_MACHINE: {
            Rect c = dl.canvasRc;
            drawThemeCard(buf, c, pal.bgLcd, pal.borderSubtle, pal, 2.0f, true);
            const int sp = (ui.drumSelPad >= 0 && ui.drumSelPad < cast(int)plug.drumPads.length) ? ui.drumSelPad : 0;
            const string sName = (sp < cast(int)plug.drumPads.length && plug.drumPads[sp].sampleName.length > 0)
                ? plug.drumPads[sp].sampleName : format("Pad %d — Left-click trigger, Right-click load .wav", sp + 1);
            drawVCenteredText(buf, uiFnt, c.left + dp(8), c, toUTF32(truncateLabel(sName, 44)), pal.accentCyan);
            foreach (pi, prc; dl.padRcs) {
                bool active = false;
                string lbl = format("Pad %d", pi + 1);
                if (pi < plug.drumPads.length) {
                    active = plug.drumPads[pi].activeFlash;
                    if (plug.drumPads[pi].sampleName.length > 0) lbl = truncateLabel(plug.drumPads[pi].sampleName, 8);
                }
                const bool sel = (cast(int)pi == ui.drumSelPad);
                drawThemeCard(buf, prc, active ? pal.accentOrange : (sel ? pal.bgControlHover : pal.bgControl),
                              sel ? pal.accentOrange : pal.borderStrong, pal, 2.0f, pal.retroBevel);
                drawCenteredText(buf, uiFnt, prc, toUTF32(lbl), active ? pal.bgWindow : pal.fgText);
            }
            break;
        }
        case BuiltinDeviceKind.DR8_MODULE: {
            Rect c = dl.canvasRc;
            drawThemeCard(buf, c, pal.bgLcd, pal.borderSubtle, pal, 2.0f, true);
            const float p0 = readPlugParam(plug, 0, 0.5f);
            const float p1 = readPlugParam(plug, 1, 0.4f);
            const int pad = dp(8);
            const int sw = c.width - pad * 2;
            const int sh = c.height - pad * 2;
            const float midY = c.middley;
            float px = c.left + pad;
            float py = midY;
            foreach (i; 1 .. 49) {
                const float t = cast(float)i / 48.0f;
                const float env = expf(-t * (2.5f + (1.0f - p1) * 6.0f));
                const float y = midY - env * sinf(t * 28.0f * (0.4f + p0)) * sh * 0.44f;
                const float x = c.left + pad + t * sw;
                drawThickLine(buf, px, py, x, y, dpf(1.8f), pal.accentOrange);
                px = x;
                py = y;
            }
            break;
        }
        default:
            break;
    }

    // 2. Render interactive pill buttons cleanly
    foreach (const ref pl; dl.pills) {
        if (!pl.visibleButton) continue;
        const uint fill = pl.active ? pl.activeColor : pal.bgControl;
        drawThemeCard(buf, pl.rc, fill, pal.borderStrong, pal, 2.0f, pal.retroBevel);
        drawCenteredText(buf, uiFnt, pl.rc, toUTF32(pl.label),
                         pl.active ? contrastTextColor(fill) : pal.fgText);
    }

    // 3. Render rotary knobs cleanly (TB-303 silver brushed dials vs standard FlPalette knobs)
    foreach (const ref kn; dl.knobs) {
        const float val = readPlugParam(plug, kn.paramId, kn.defaultVal);
        if (kn.knobStyle == 1) {
            drawTb303Knob(buf, kn.cx, kn.cy, kn.r, val);
        } else {
            drawRotaryKnob(buf, kn.cx, kn.cy, kn.r, val, pal.bgLcd, kn.accent, pal.bgControl, pal.borderSubtle, false);
        }
        const uint lblCol = kn.lightBg ? 0x001E2126 : pal.fgText;
        const uint valCol = kn.lightBg ? 0x00333840 : pal.fgMuted;
        drawCenteredText(buf, valFnt, Rect(kn.cellRc.left, kn.cy + kn.r + dp(2), kn.cellRc.right, kn.cy + kn.r + dp(15)),
                         toUTF32(truncateLabel(kn.formattedVal, 9)), valCol);
        drawCenteredText(buf, valFnt, Rect(kn.cellRc.left, kn.cy - kn.r - dp(13), kn.cellRc.right, kn.cy - kn.r - dp(1)),
                         toUTF32(truncateLabel(kn.label, 9)), lblCol);
    }
}

/// Handles mouse interactions inside custom builtin device UIs; returns true if handled.
bool handleBuiltinDeviceMouse(
    MouseEvent event,
    Rect bodyRc,
    TransportModel* model,
    PluginSlotState* plug,
    ref BuiltinDeviceUiState ui,
    int trkIdx,
    void delegate(int trackIdx, int pluginIdx, uint paramId, float value) onSetParam,
    void delegate(int trackIdx, uint notePitch) onTriggerPad,
    void delegate(int trackIdx, int pluginIdx, int padIdx) onPromptPadSample,
    void delegate(int trackIdx, int pluginIdx) onLoadSample,
    void delegate(string msg) onHint
) {
    if (!plug) return false;
    const pal = model ? model.palette() : getFlPalette();
    const dl = buildBuiltinDeviceLayout(bodyRc, plug, ui, pal);
    if (dl.kind == BuiltinDeviceKind.GENERIC) return false;

    void emitParam(uint pid, float val, float defVal, string lbl) {
        const float nv = clamp01(val);
        writePlugParam(plug, pid, nv, defVal, lbl);
        if (onSetParam !is null) onSetParam(trkIdx, plug.pluginIndex, pid, nv);
        if (onHint !is null) onHint(format("%s — %s: %d%%", plug.name, lbl, cast(int)(nv * 100.0f + 0.5f)));
    }

    if (event.action == MouseAction.Wheel) {
        if (dl.kind == BuiltinDeviceKind.EQ &&
            (dl.canvasRc.isPointInside(event.x, event.y) || dl.footerRc.isPointInside(event.x, event.y))) {
            const int b = (ui.eqSelectedBand >= 0 && ui.eqSelectedBand < 8) ? ui.eqSelectedBand : 0;
            const float curQ = readPlugParam(plug, cast(uint)(24 + b), 0.425f);
            const float dq = (event.wheelDelta > 0) ? 0.04f : -0.04f;
            emitParam(cast(uint)(24 + b), curQ + dq, 0.425f, format("Band %d Q", b + 1));
            return true;
        }
        return false;
    }

    if (event.action == MouseAction.ButtonDown) {
        // 1. Check pills first
        foreach (const ref pl; dl.pills) {
            if (!pl.rc.isPointInside(event.x, event.y)) continue;
            if (pl.actionKind == 0) {
                emitParam(pl.paramId, pl.targetVal, 0.5f, pl.label);
            } else if (pl.actionKind == 1) {
                const float cur = readPlugParam(plug, pl.paramId, 0.0f);
                emitParam(pl.paramId, cur >= 0.5f ? 0.0f : 1.0f, 0.0f, pl.label);
            } else if (pl.actionKind == 2) {
                if (dl.kind == BuiltinDeviceKind.EQ) {
                    if (ui.eqSelectedBand == pl.uiTabVal && event.doubleClick) {
                        const int curT = eqNormToTypeIdx(readPlugParam(plug, cast(uint)pl.uiTabVal, 0.0f));
                        emitParam(cast(uint)pl.uiTabVal, eqTypeIdxToNorm((curT + 1) % 6), 0.0f, "Band Type");
                    }
                    ui.eqSelectedBand = pl.uiTabVal;
                } else if (dl.kind == BuiltinDeviceKind.HOTT) {
                    ui.hottTab = pl.uiTabVal;
                } else if (dl.kind == BuiltinDeviceKind.MAXIM) {
                    ui.maximBandTab = pl.uiTabVal;
                }
            } else if (pl.actionKind == 3) {
                if (dl.kind == BuiltinDeviceKind.ACID_BASS) {
                    ui.acidActiveNote = pl.uiTabVal;
                }
                if (onTriggerPad !is null) onTriggerPad(trkIdx, pl.paramId);
            } else if (pl.actionKind == 4) {
                if (onLoadSample !is null) onLoadSample(trkIdx, plug.pluginIndex);
            } else if (pl.actionKind == 5) {
                const float cur = readPlugParam(plug, pl.paramId, 0.5f);
                emitParam(pl.paramId, cur + pl.targetVal, 0.5f, pl.label);
            }
            return true;
        }

        // 2. Rotary knobs
        foreach (const ref kn; dl.knobs) {
            if (!kn.cellRc.isPointInside(event.x, event.y)) continue;
            if (event.button == MouseButton.Right) {
                emitParam(kn.paramId, kn.defaultVal, kn.defaultVal, kn.label);
            } else {
                ui.activeDragParamId = cast(int)kn.paramId;
                ui.dragStartY = event.y;
                ui.dragStartVal = readPlugParam(plug, kn.paramId, kn.defaultVal);
                ui.dragDefaultVal = kn.defaultVal;
                ui.dragIsVerticalSlider = false;
            }
            return true;
        }

        // 3. Organ drawbars
        if (dl.kind == BuiltinDeviceKind.ORGAN) {
            foreach (d, rc; dl.drawbarRcs) {
                if (!rc.isPointInside(event.x, event.y)) continue;
                const float v = clamp01(cast(float)(event.y - rc.top) / cast(float)(rc.height > 1 ? rc.height : 1));
                emitParam(cast(uint)d, v, d == 2 ? 1.0f : 0.0f, format("Drawbar %d", cast(int)d + 1));
                ui.activeDragParamId = cast(int)d;
                ui.activeDragSliderRc = rc;
                ui.dragIsVerticalSlider = true;
                return true;
            }
        }

        // 4. Drum Machine pads
        if (dl.kind == BuiltinDeviceKind.DRUM_MACHINE) {
            foreach (pi, prc; dl.padRcs) {
                if (!prc.isPointInside(event.x, event.y)) continue;
                ui.drumSelPad = cast(int)pi;
                if (event.button == MouseButton.Right) {
                    if (onPromptPadSample !is null) onPromptPadSample(trkIdx, plug.pluginIndex, cast(int)pi);
                } else {
                    uint notePitch = cast(uint)(36 + pi);
                    if (pi < plug.drumPads.length) {
                        plug.drumPads[pi].activeFlash = true;
                        if (plug.drumPads[pi].triggerNote > 0) notePitch = plug.drumPads[pi].triggerNote;
                    }
                    if (onTriggerPad !is null) onTriggerPad(trkIdx, notePitch);
                }
                return true;
            }
        }

        // 4b. HOTT interactive 3-band OTT thresholds & Att/Rel drag
        if (dl.kind == BuiltinDeviceKind.HOTT) {
            static immutable uint[3] DOWN_PIDS = [11u, 10u, 9u];
            static immutable uint[3] UP_PIDS   = [14u, 13u, 12u];
            static immutable uint[3] ATK_PIDS  = [19u, 18u, 17u];
            static immutable uint[3] REL_PIDS  = [22u, 21u, 20u];
            static immutable uint[3] DRAT_PIDS = [25u, 24u, 23u];
            static immutable uint[3] URAT_PIDS = [28u, 27u, 26u];
            static immutable float[3] DEF_DN   = [(-35.5f + 60.0f) / 60.0f, (-30.2f + 60.0f) / 60.0f, (-33.8f + 60.0f) / 60.0f];
            static immutable float[3] DEF_UP   = [(-40.8f + 60.0f) / 72.0f, (-41.8f + 60.0f) / 72.0f, (-40.8f + 60.0f) / 72.0f];
            foreach (bi; 0 .. 3) {
                Rect barRc = dl.hottBarRcs[bi];
                Rect inspRc = dl.hottInspectorRcs[bi];
                if (barRc.isPointInside(event.x, event.y)) {
                    ui.hottDragBand = bi;
                    const float downDb = readPlugParam(plug, DOWN_PIDS[bi], DEF_DN[bi]) * 60.0f - 60.0f;
                    const float upDb = readPlugParam(plug, UP_PIDS[bi], DEF_UP[bi]) * 72.0f - 60.0f;
                    if (ui.hottTab == 1) {
                        ui.hottDragMode = 1;
                    } else if (ui.hottTab == 2) {
                        ui.hottDragMode = 0;
                    } else {
                        const int upX = barRc.left + cast(int)(normalizeDb(upDb, -80.0f, 0.0f) * barRc.width);
                        const int downX = barRc.left + cast(int)(normalizeDb(downDb, -80.0f, 0.0f) * barRc.width);
                        const int dUp = event.x > upX ? (event.x - upX) : (upX - event.x);
                        const int dDn = event.x > downX ? (event.x - downX) : (downX - event.x);
                        ui.hottDragMode = (dUp < dDn) ? 1 : 0;
                    }
                    const float db = -80.0f + clamp01(cast(float)(event.x - barRc.left) / (barRc.width > 1 ? barRc.width : 1)) * 80.0f;
                    if (ui.hottDragMode == 1) {
                        emitParam(UP_PIDS[bi], (db + 60.0f) / 72.0f, DEF_UP[bi], "Up Thresh");
                    } else {
                        emitParam(DOWN_PIDS[bi], (db + 60.0f) / 60.0f, DEF_DN[bi], "Down Thresh");
                    }
                    return true;
                }
                if (inspRc.isPointInside(event.x, event.y)) {
                    ui.hottDragBand = bi;
                    const bool topHalf = event.y < inspRc.middley;
                    ui.hottDragMode = topHalf ? 2 : 3;
                    const float frac = clamp01(cast(float)(event.x - inspRc.left) / (inspRc.width > 1 ? inspRc.width : 1));
                    if (ui.hottTab == 0) {
                        emitParam(topHalf ? ATK_PIDS[bi] : REL_PIDS[bi], frac, 0.7f, topHalf ? "Attack" : "Release");
                    } else if (ui.hottTab == 1) {
                        emitParam(URAT_PIDS[bi], frac * 0.999f, 0.76f, "Up Ratio");
                    } else {
                        emitParam(DRAT_PIDS[bi], frac * 0.999f, 0.985f, "Down Ratio");
                    }
                    return true;
                }
            }
        }

        // 5. EQ interactive band handle drag
        if (dl.kind == BuiltinDeviceKind.EQ && dl.canvasRc.isPointInside(event.x, event.y)) {
            const int pad = dp(8);
            const int sw = dl.canvasRc.width - pad * 2;
            const int sh = dl.canvasRc.height - pad * 2;
            if (sw > dp(8) && sh > dp(8)) {
                static immutable float[8] DEF_FREQS = [30, 80, 250, 700, 2000, 5000, 10000, 16000];
                int bestB = -1;
                double bestD = dp(16);
                foreach (b; 0 .. 8) {
                    if (eqNormToTypeIdx(readPlugParam(plug, cast(uint)b, 0.0f)) == 0) continue;
                    const float fn = readPlugParam(plug, cast(uint)(8 + b), eqFreqToNorm(DEF_FREQS[b]));
                    const float gn = readPlugParam(plug, cast(uint)(16 + b), 0.5f);
                    const double bx = dl.canvasRc.left + pad + fn * sw;
                    const double by = dl.canvasRc.top + pad + (1.0 - gn) * sh;
                    const double dx = event.x - bx;
                    const double dy = event.y - by;
                    const double dist = dx * dx + dy * dy;
                    if (dist < bestD * bestD) {
                        bestD = dist;
                        bestB = b;
                    }
                }
                if (bestB < 0 && event.doubleClick) {
                    foreach (b; 0 .. 8) {
                        if (eqNormToTypeIdx(readPlugParam(plug, cast(uint)b, 0.0f)) == 0) {
                            bestB = b;
                            emitParam(cast(uint)b, 1.0f, 0.0f, format("Band %d Enable", b + 1));
                            break;
                        }
                    }
                }
                if (bestB >= 0) {
                    ui.eqSelectedBand = bestB;
                    if (event.button == MouseButton.Right) {
                        emitParam(cast(uint)bestB, 0.0f, 0.0f, format("Band %d Off", bestB + 1));
                        return true;
                    }
                    ui.eqDragBand = bestB;
                    ui.eqDragStartY = event.y;
                    ui.eqDragStartQ = readPlugParam(plug, cast(uint)(24 + bestB), 0.425f);
                    if ((event.flags & MouseFlag.Shift) == 0) {
                        const float fn = clamp01(cast(float)(event.x - (dl.canvasRc.left + pad)) / sw);
                        const float gn = clamp01(1.0f - cast(float)(event.y - (dl.canvasRc.top + pad)) / sh);
                        emitParam(cast(uint)(8 + bestB), fn, eqFreqToNorm(DEF_FREQS[bestB]), "Freq");
                        emitParam(cast(uint)(16 + bestB), gn, 0.5f, "Gain");
                    }
                    return true;
                }
            }
        }

        // 6. Envelope Shaper draggable point handles
        if (dl.kind == BuiltinDeviceKind.ENVELOPE_SHAPER && dl.canvasRc.isPointInside(event.x, event.y)) {
            const int pad = dp(8);
            const int sw = dl.canvasRc.width - pad * 2;
            const int sh = dl.canvasRc.height - pad * 2;
            int nPts = 2 + cast(int)(readPlugParam(plug, 21, 0.14f) * 14.0f + 0.5f);
            if (nPts < 2) nPts = 2;
            if (nPts > 16) nPts = 16;
            const float normX = clamp01(cast(float)(event.x - (dl.canvasRc.left + pad)) / (sw > 1 ? sw : 1));
            const int nearIdx = cast(int)(normX * (nPts - 1) + 0.5f);
            if (nearIdx >= 0 && nearIdx < nPts) {
                ui.envDragPoint = nearIdx;
                const float yVal = clamp01(1.0f - cast(float)(event.y - (dl.canvasRc.top + pad)) / (sh > 1 ? sh : 1));
                emitParam(cast(uint)(5 + nearIdx), yVal, 0.5f, format("Pt %d", nearIdx + 1));
                return true;
            }
        }

        return true;
    }

    if (event.action == MouseAction.Move) {
        if (ui.hottDragBand >= 0 && ui.hottDragBand < 3 && dl.kind == BuiltinDeviceKind.HOTT) {
            static immutable uint[3] DOWN_PIDS = [11u, 10u, 9u];
            static immutable uint[3] UP_PIDS   = [14u, 13u, 12u];
            static immutable uint[3] ATK_PIDS  = [19u, 18u, 17u];
            static immutable uint[3] REL_PIDS  = [22u, 21u, 20u];
            static immutable uint[3] DRAT_PIDS = [25u, 24u, 23u];
            static immutable uint[3] URAT_PIDS = [28u, 27u, 26u];
            const int bi = ui.hottDragBand;
            if (ui.hottDragMode <= 1) {
                Rect barRc = dl.hottBarRcs[bi];
                const float db = -80.0f + clamp01(cast(float)(event.x - barRc.left) / (barRc.width > 1 ? barRc.width : 1)) * 80.0f;
                if (ui.hottDragMode == 1) {
                    emitParam(UP_PIDS[bi], (db + 60.0f) / 72.0f, 0.25f, "Up Thresh");
                } else {
                    emitParam(DOWN_PIDS[bi], (db + 60.0f) / 60.0f, 0.45f, "Down Thresh");
                }
            } else {
                Rect inspRc = dl.hottInspectorRcs[bi];
                const float frac = clamp01(cast(float)(event.x - inspRc.left) / (inspRc.width > 1 ? inspRc.width : 1));
                const bool topHalf = (ui.hottDragMode == 2);
                if (ui.hottTab == 0) {
                    emitParam(topHalf ? ATK_PIDS[bi] : REL_PIDS[bi], frac, 0.7f, topHalf ? "Attack" : "Release");
                } else if (ui.hottTab == 1) {
                    emitParam(URAT_PIDS[bi], frac * 0.999f, 0.76f, "Up Ratio");
                } else {
                    emitParam(DRAT_PIDS[bi], frac * 0.999f, 0.985f, "Down Ratio");
                }
            }
            return true;
        }
        if (ui.eqDragBand >= 0 && dl.kind == BuiltinDeviceKind.EQ) {
            const int pad = dp(8);
            const int sw = dl.canvasRc.width - pad * 2;
            const int sh = dl.canvasRc.height - pad * 2;
            if (sw > dp(8) && sh > dp(8)) {
                const int b = ui.eqDragBand;
                if ((event.flags & MouseFlag.Shift) != 0) {
                    const float dq = cast(float)(ui.eqDragStartY - event.y) / dpf(110.0f);
                    emitParam(cast(uint)(24 + b), ui.eqDragStartQ + dq, 0.425f, "Q");
                } else {
                    const float fn = clamp01(cast(float)(event.x - (dl.canvasRc.left + pad)) / sw);
                    const float gn = clamp01(1.0f - cast(float)(event.y - (dl.canvasRc.top + pad)) / sh);
                    emitParam(cast(uint)(8 + b), fn, 0.5f, "Freq");
                    emitParam(cast(uint)(16 + b), gn, 0.5f, "Gain");
                }
            }
            return true;
        }
        if (ui.envDragPoint >= 0 && dl.kind == BuiltinDeviceKind.ENVELOPE_SHAPER) {
            const int pad = dp(8);
            const int sh = dl.canvasRc.height - pad * 2;
            const float yVal = clamp01(1.0f - cast(float)(event.y - (dl.canvasRc.top + pad)) / (sh > 1 ? sh : 1));
            emitParam(cast(uint)(5 + ui.envDragPoint), yVal, 0.5f, format("Pt %d", ui.envDragPoint + 1));
            return true;
        }
        if (ui.activeDragParamId >= 0) {
            float v;
            if (ui.dragIsVerticalSlider && ui.activeDragSliderRc.height > 1) {
                v = clamp01(cast(float)(event.y - ui.activeDragSliderRc.top) / cast(float)ui.activeDragSliderRc.height);
            } else {
                v = clamp01(ui.dragStartVal + cast(float)(ui.dragStartY - event.y) / dpf(110.0f));
            }
            emitParam(cast(uint)ui.activeDragParamId, v, ui.dragDefaultVal, "Param");
            return true;
        }
    }

    if (event.action == MouseAction.ButtonUp || event.action == MouseAction.Cancel) {
        ui.eqDragBand = -1;
        ui.hottDragBand = -1;
        ui.envDragPoint = -1;
        ui.acidActiveNote = -1;
        ui.activeDragParamId = -1;
        ui.dragIsVerticalSlider = false;
        return true;
    }

    return false;
}

unittest {
    import dlangui.graphics.drawbuf : ColorDrawBuf;
    import gui_d.transport_model : DrumPadSlotState;

    TransportModel model;
    model.initDefaultTemplate();
    auto buf = new ColorDrawBuf(440, 260);
    Rect bodyRc = Rect(0, 32, 440, 260);

    // 1. Test every builtin URI maps to non-GENERIC device UI & renders cleanly across modern & WIN95 themes
    static immutable string[20] URIS = [
        "builtin://eq", "builtin://compressor", "builtin://limiter", "builtin://hott",
        "builtin://maxim", "builtin://delay", "builtin://reverb", "builtin://phaser",
        "builtin://chorus", "builtin://bitcrusher", "builtin://envelope_shaper", "builtin://convolver",
        "builtin://vocodey", "builtin://stereo_width", "builtin://acid_bass", "builtin://3xosc",
        "builtin://sampler", "builtin://epiano", "builtin://organ", "builtin://dr8_kick"
    ];
    foreach (u; URIS) {
        PluginSlotState slot;
        slot.uri = u;
        slot.name = u;
        assert(hasCustomBuiltinDeviceUi(&slot), "Expected custom Swing-ported UI for " ~ u);
        BuiltinDeviceUiState ui;
        drawBuiltinDevicePanel(buf, bodyRc, &model, &slot, ui, FontRef.init);
    }

    // Also test DrumMachine + WIN95 retro theme
    model.themeId = FlThemeId.WIN95;
    PluginSlotState dm;
    dm.uri = "builtin://drum_machine";
    dm.name = "Drum Machine";
    dm.drumPads = [DrumPadSlotState(0, "Kick", "bd.wav"), DrumPadSlotState(1, "Snare", "sd.wav")];
    assert(hasCustomBuiltinDeviceUi(&dm));
    BuiltinDeviceUiState dmUi;
    drawBuiltinDevicePanel(buf, bodyRc, &model, &dm, dmUi, FontRef.init);

    // 2. Test interactive EQ band knob / pill & Acid Bass virtual key click
    model.themeId = FlThemeId.DEFAULT_DARK;
    PluginSlotState eqSlot;
    eqSlot.uri = "builtin://eq";
    eqSlot.name = "Parametric EQ";
    BuiltinDeviceUiState eqUi;
    int lastPid = -1;
    float lastVal = -1.0f;
    const dlEq = buildBuiltinDeviceLayout(bodyRc, &eqSlot, eqUi, model.palette());
    assert(dlEq.knobs.length == 3 && dlEq.pills.length >= 9);
    auto k0 = dlEq.knobs[0];
    handleBuiltinDeviceMouse(
        new MouseEvent(MouseAction.ButtonDown, MouseButton.Left, MouseFlag.LButton, cast(short)k0.cx, cast(short)k0.cy),
        bodyRc, &model, &eqSlot, eqUi, 1,
        (int t, int p, uint pid, float v) { lastPid = cast(int)pid; lastVal = v; },
        null, null, null, null);
    handleBuiltinDeviceMouse(
        new MouseEvent(MouseAction.Move, MouseButton.None, MouseFlag.LButton, cast(short)k0.cx, cast(short)(k0.cy - 22)),
        bodyRc, &model, &eqSlot, eqUi, 1,
        (int t, int p, uint pid, float v) { lastPid = cast(int)pid; lastVal = v; },
        null, null, null, null);
    assert(lastPid == 8 && lastVal > 0.0f, "Expected EQ Freq knob drag to emit param 8");

    PluginSlotState acidSlot;
    acidSlot.uri = "builtin://acid_bass";
    acidSlot.name = "Acid Bass";
    BuiltinDeviceUiState acidUi;
    const dlAcid = buildBuiltinDeviceLayout(bodyRc, &acidSlot, acidUi, model.palette());
    assert(dlAcid.knobs.length == 7 && dlAcid.pills.length == 17);
    uint triggeredNote = 0;
    // Pill index 8 in TB-303 Acid Bass is lower key 'C' (offset 0, C3 at default octave 3 = MIDI note 60)
    const auto c3Pill = dlAcid.pills[8];
    handleBuiltinDeviceMouse(
        new MouseEvent(MouseAction.ButtonDown, MouseButton.Left, MouseFlag.LButton,
                       cast(short)c3Pill.rc.middlex, cast(short)c3Pill.rc.middley),
        bodyRc, &model, &acidSlot, acidUi, 103, null,
        (int t, uint note) { triggeredNote = note; }, null, null, null);
    assert(triggeredNote == 60 && acidUi.acidActiveNote == 0,
           "Expected TB-303 Acid Bass C key click to trigger MIDI note 60 and light its LED");

    // 3. Test HOTT (OTT) interactive band threshold bar drag in center BandMeterPanel
    PluginSlotState hottSlot;
    hottSlot.uri = "builtin://hott";
    hottSlot.name = "Hott";
    BuiltinDeviceUiState hottUi;
    const dlHott = buildBuiltinDeviceLayout(Rect(0, 32, 520, 290), &hottSlot, hottUi, model.palette());
    assert(dlHott.knobs.length == 8);
    int hottPid = -1;
    handleBuiltinDeviceMouse(
        new MouseEvent(MouseAction.ButtonDown, MouseButton.Left, MouseFlag.LButton,
                       cast(short)dlHott.hottBarRcs[0].middlex, cast(short)dlHott.hottBarRcs[0].middley),
        Rect(0, 32, 520, 290), &model, &hottSlot, hottUi, 4,
        (int t, int p, uint pid, float v) { hottPid = cast(int)pid; },
        null, null, null, null);
    assert(hottPid == 11 || hottPid == 14,
           "Expected OTT High band bar click to adjust High Down/Up threshold param");
}
