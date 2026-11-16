module gui_d.device_panels;

import core.stdc.math : atanf, cosf, expf, log10f, logf, powf, sinf;
import dlangui.core.events : MouseAction, MouseButton, MouseEvent, MouseFlag;
import dlangui.core.types : Rect;
import dlangui.graphics.colors : COLOR_TRANSPARENT;
import dlangui.graphics.drawbuf : DrawBuf;
import dlangui.graphics.fonts : FontRef, FontWeight;
import gui_d.theme : adjustColorBrightness, contrastTextColor, dp, dpf, drawCenteredText,
    drawFilledCircle, drawRotaryKnob, drawThemeCard, drawThickLine, drawVCenteredText,
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
    int drumSelPad = 0;
    int envDragPoint = -1;

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
}

/// Interactive pill/toggle button descriptor built during layout.
struct DevicePillSpec {
    Rect rc;
    string label;
    bool active;
    uint activeColor;
    int actionKind; // 0=set param to targetVal, 1=toggle param 0/1, 2=ui tab, 3=virtual note, 4=load wav
    uint paramId;
    float targetVal;
    int uiTabVal;
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
    DeviceKnobSpec[] knobs;
    DevicePillSpec[] pills;
    Rect[] drawbarRcs; // Organ drawbars 0..8
    Rect[] padRcs;     // Drum Machine pads 0..15
    Rect[] bandMeterRcs;
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
    const ref FlPalette pal
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
                              Rect(x0, area.top, x0 + cw, area.bottom), cx, cy, r, fv);
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
            const int pillH = dp(20);
            const int knobH = dp(64);
            dl.canvasRc = Rect(inner.left, inner.top, inner.right, inner.bottom - pillH - knobH - dp(8));
            Rect stripRc = Rect(inner.left, dl.canvasRc.bottom + dp(4), inner.right, dl.canvasRc.bottom + dp(4) + pillH);
            const int bw = stripRc.width / 8;
            static immutable uint[8] BAND_COLS = [
                0x00E04040, 0x00E07020, 0x00D0C020, 0x0040B040,
                0x0030A0A0, 0x004080E0, 0x008060D0, 0x00C050A0,
            ];
            static immutable string[6] TYPE_NAMES = ["Off", "LP", "HP", "LS", "HS", "Bell"];
            foreach (b; 0 .. 8) {
                const int bx = stripRc.left + b * bw;
                const int ti = eqNormToTypeIdx(readPlugParam(plug, cast(uint)b, 0.0f));
                const bool sel = (ui.eqSelectedBand == b);
                dl.pills ~= DevicePillSpec(
                    Rect(bx + dp(1), stripRc.top, bx + bw - dp(1), stripRc.bottom),
                    format("%d:%s", b + 1, TYPE_NAMES[ti]),
                    sel || ti > 0, sel ? BAND_COLS[b] : adjustColorBrightness(BAND_COLS[b], -24),
                    2, cast(uint)b, 0.0f, b
                );
            }
            const int sb = (ui.eqSelectedBand >= 0 && ui.eqSelectedBand < 8) ? ui.eqSelectedBand : 0;
            const int curType = eqNormToTypeIdx(readPlugParam(plug, cast(uint)sb, 0.0f));
            Rect botRc = Rect(inner.left, stripRc.bottom + dp(4), inner.right, inner.bottom);
            // Type cycle pill on left + 3 knobs (Freq, Gain, Q)
            const int typeW = dp(66);
            dl.pills ~= DevicePillSpec(
                Rect(botRc.left, botRc.top + dp(18), botRc.left + typeW, botRc.top + dp(42)),
                "Type: " ~ TYPE_NAMES[curType], curType > 0, BAND_COLS[sb],
                0, cast(uint)sb, eqTypeIdxToNorm((curType + 1) % 6), sb
            );
            static immutable float[8] DEF_FREQS = [30, 80, 250, 700, 2000, 5000, 10000, 16000];
            const float fNorm = readPlugParam(plug, cast(uint)(8 + sb), eqFreqToNorm(DEF_FREQS[sb]));
            const float gNorm = readPlugParam(plug, cast(uint)(16 + sb), 0.5f);
            const float qNorm = readPlugParam(plug, cast(uint)(24 + sb), 0.425f);
            Rect kArea = Rect(botRc.left + typeW + dp(6), botRc.top, botRc.right, botRc.bottom);
            appendKnobRow(dl.knobs, kArea, plug,
                [cast(uint)(8 + sb), cast(uint)(16 + sb), cast(uint)(24 + sb)],
                ["Freq", "Gain", "Q"],
                [eqFreqToNorm(DEF_FREQS[sb]), 0.5f, 0.425f],
                [formatFreq(eqNormToFreq(fNorm)), formatDb((gNorm - 0.5f) * 48.0f), format("%.2f", eqNormToQ(qNorm))],
                [BAND_COLS[sb], BAND_COLS[sb], BAND_COLS[sb]], pal);
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
            const int knobH = dp(66);
            const int tabH = dp(20);
            dl.canvasRc = Rect(inner.left, inner.top, inner.right, inner.bottom - tabH - knobH - dp(6));
            Rect tabRc = Rect(inner.left, dl.canvasRc.bottom + dp(3), inner.right, dl.canvasRc.bottom + dp(3) + tabH);
            const int bw = tabRc.width / 5;
            static immutable string[3] TABS = ["T: Time", "B: Below", "A: Above"];
            foreach (ti; 0 .. 3) {
                dl.pills ~= DevicePillSpec(
                    Rect(tabRc.left + ti * bw, tabRc.top, tabRc.left + (ti + 1) * bw - dp(2), tabRc.bottom),
                    TABS[ti], ui.hottTab == ti, pal.accentOrange, 2, 0, 0.0f, ti
                );
            }
            const bool softKnee = readPlugParam(plug, 15, 1.0f) >= 0.5f;
            const bool rmsMode = readPlugParam(plug, 16, 1.0f) >= 0.5f;
            dl.pills ~= DevicePillSpec(
                Rect(tabRc.left + 3 * bw, tabRc.top, tabRc.left + 4 * bw - dp(2), tabRc.bottom),
                "Soft", softKnee, pal.accentCyan, 1, 15, 1.0f, 0
            );
            dl.pills ~= DevicePillSpec(
                Rect(tabRc.left + 4 * bw, tabRc.top, tabRc.right, tabRc.bottom),
                "RMS", rmsMode, pal.accentGreen, 1, 16, 1.0f, 0
            );
            Rect kArea = Rect(inner.left, tabRc.bottom + dp(3), inner.right, inner.bottom);
            const float amt = readPlugParam(plug, 2, 1.0f);
            const float tm = readPlugParam(plug, 3, 1.0f);
            const float outV = readPlugParam(plug, 4, 0.5f);
            const float lo = readPlugParam(plug, 5, 0.715f);
            const float md = readPlugParam(plug, 6, 0.619f);
            const float hi = readPlugParam(plug, 7, 0.715f);
            appendKnobRow(dl.knobs, kArea, plug,
                [2u, 3u, 4u, 5u, 6u, 7u],
                ["Depth", "Time", "Output", "Low Out", "Mid Out", "High Out"],
                [1.0f, 1.0f, 0.5f, 0.715f, 0.619f, 0.715f],
                [formatPercent(amt), formatPercent(tm), formatDb((outV - 0.5f) * 24.0f),
                 formatPercent(lo), formatPercent(md), formatPercent(hi)],
                [pal.accentOrange, pal.accentCyan, pal.accentAmber, 0x00E25822, 0x0010B981, 0x003B82F6], pal);
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
            // TB-303 layout: Top waveform switch + 7 dials + Slide/Accent switches; Bottom 13-key virtual keyboard
            const int keysH = dp(40);
            const int switchW = dp(52);
            Rect mainRc = Rect(inner.left, inner.top, inner.right, inner.bottom - keysH - dp(4));
            dl.canvasRc = mainRc;
            const bool isSq = readPlugParam(plug, 0, 0.0f) >= 0.5f;
            dl.pills ~= DevicePillSpec(
                Rect(mainRc.left + dp(6), mainRc.top + dp(8), mainRc.left + switchW, mainRc.top + dp(30)),
                isSq ? "SQR" : "SAW", isSq, pal.accentOrange, 1, 0, 1.0f, 0
            );
            const bool slideOn = readPlugParam(plug, 9, 0.0f) >= 0.5f;
            const bool accOn = readPlugParam(plug, 10, 0.0f) >= 0.5f;
            dl.pills ~= DevicePillSpec(
                Rect(mainRc.left + dp(6), mainRc.top + dp(34), mainRc.left + switchW, mainRc.top + dp(52)),
                "SLIDE", slideOn, pal.accentRed, 1, 9, 1.0f, 0
            );
            dl.pills ~= DevicePillSpec(
                Rect(mainRc.left + dp(6), mainRc.top + dp(56), mainRc.left + switchW, mainRc.top + dp(74)),
                "ACCNT", accOn, pal.accentRed, 1, 10, 1.0f, 0
            );
            Rect kArea = Rect(mainRc.left + switchW + dp(6), mainRc.top + dp(4), mainRc.right - dp(4), mainRc.bottom - dp(4));
            const float cut = readPlugParam(plug, 1, 0.3f);
            const float dec = readPlugParam(plug, 4, 0.2f);
            appendKnobRow(dl.knobs, kArea, plug,
                [1u, 2u, 3u, 4u, 5u, 6u, 7u],
                ["Cutoff", "Reso", "EnvMod", "Decay", "Accent", "Drive", "Vol"],
                [0.3f, 0.6f, 0.5f, 0.2f, 0.0f, 0.1f, 0.7f],
                [formatFreq(100.0f * powf(30.0f, cut)), formatPercent(readPlugParam(plug, 2, 0.6f)),
                 formatPercent(readPlugParam(plug, 3, 0.5f)), formatMsFromExp(dec, 50.0f, 60.0f),
                 formatPercent(readPlugParam(plug, 5, 0.0f)), formatPercent(readPlugParam(plug, 6, 0.1f)),
                 formatPercent(readPlugParam(plug, 7, 0.7f))],
                [pal.accentOrange, pal.accentOrange, pal.accentCyan, pal.accentCyan,
                 pal.accentRed, pal.accentRed, pal.accentGreen], pal);

            // 13-key TB-303 trigger bar at bottom (C3=48 .. C4=60)
            Rect kbdRc = Rect(inner.left, mainRc.bottom + dp(4), inner.right, inner.bottom);
            static immutable string[13] KEY_LBL = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B", "C+"];
            const int kw = kbdRc.width / 13;
            foreach (ki; 0 .. 13) {
                const bool blk = (ki == 1 || ki == 3 || ki == 6 || ki == 8 || ki == 10);
                dl.pills ~= DevicePillSpec(
                    Rect(kbdRc.left + ki * kw, kbdRc.top, kbdRc.left + (ki + 1) * kw - dp(1), kbdRc.bottom),
                    KEY_LBL[ki], !blk, blk ? pal.bgPanelAlt : pal.accentCyan,
                    3, cast(uint)(48 + ki), 1.0f, ki
                );
            }
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
            Rect c = dl.canvasRc;
            drawThemeCard(buf, c, pal.bgLcd, pal.borderSubtle, pal, 2.0f, true);
            drawPanelGrid(buf, c, pal);

            const int pad = dp(6);
            const int sw = c.width - pad * 2;
            const int sh = c.height - pad * 2;
            if (sw > dp(20) && sh > dp(16)) {
                // Real-time FFT spectrum overlay
                void plotSpec(const(float)[] mags, uint col) {
                    if (mags.length < 2) return;
                    float px = c.left + pad;
                    float py = c.bottom - pad - normalizeDb(mags[0], -60.0f, 12.0f) * sh;
                    foreach (i; 1 .. mags.length) {
                        const float t = cast(float)i / cast(float)(mags.length - 1);
                        const float x = c.left + pad + t * sw;
                        const float y = c.bottom - pad - normalizeDb(mags[i], -60.0f, 12.0f) * sh;
                        drawThickLine(buf, px, py, x, y, dpf(1.4f), col);
                        px = x;
                        py = y;
                    }
                }
                plotSpec(plug.specInDb, pal.fgDim);
                plotSpec(plug.specOutDb, pal.accentCyan);

                // Composite EQ frequency response curve
                enum int CURVE_N = 48;
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
                    drawThickLine(buf, prevX, prevY, x, y, dpf(2.0f), pal.fgText);
                    prevX = x;
                    prevY = y;
                }

                // 8 draggable colored band handles
                static immutable uint[8] BAND_COLS = [
                    0x00E04040, 0x00E07020, 0x00D0C020, 0x0040B040,
                    0x0030A0A0, 0x004080E0, 0x008060D0, 0x00C050A0,
                ];
                foreach (b; 0 .. 8) {
                    if (eqNormToTypeIdx(readPlugParam(plug, cast(uint)b, 0.0f)) == 0 &&
                        b != ui.eqSelectedBand) continue;
                    static immutable float[8] DEF_FREQS = [30, 80, 250, 700, 2000, 5000, 10000, 16000];
                    const float fn = readPlugParam(plug, cast(uint)(8 + b), eqFreqToNorm(DEF_FREQS[b]));
                    const float gn = readPlugParam(plug, cast(uint)(16 + b), 0.5f);
                    float db = (gn - 0.5f) * 48.0f;
                    if (db < -25.0f) db = -25.0f; else if (db > 25.0f) db = 25.0f;
                    const float bx = c.left + pad + fn * sw;
                    const float by = c.top + pad + (1.0f - (db + 25.0f) / 50.0f) * sh;
                    drawFilledCircle(buf, bx, by, dpf(b == ui.eqSelectedBand ? 6.0f : 4.5f),
                                     BAND_COLS[b], b == ui.eqSelectedBand ? pal.fgText : pal.bgLcd);
                }
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
            Rect c = dl.canvasRc;
            drawThemeCard(buf, c, pal.bgLcd, pal.borderSubtle, pal, 2.0f, true);
            const int rowH = (c.height - dp(8)) / 3;
            static immutable string[3] BNAMES = ["HIGH", "MID", "LOW"];
            static immutable uint[3] BCOLS = [0x003B82F6, 0x0010B981, 0x00E25822];
            float[3] grs = [plug.meterHighGrDb, plug.meterMidGrDb, plug.meterLowGrDb];
            float[3] ins = [plug.meterHighInDb, plug.meterMidInDb, plug.meterLowInDb];
            foreach (bi; 0 .. 3) {
                const int ry = c.top + dp(4) + bi * rowH;
                Rect lRc = Rect(c.left + dp(6), ry, c.left + dp(44), ry + rowH - dp(2));
                drawVCenteredText(buf, uiFnt, lRc.left, lRc, toUTF32(BNAMES[bi]), BCOLS[bi]);
                Rect barRc = Rect(lRc.right + dp(4), ry + dp(4), c.right - dp(54), ry + rowH - dp(6));
                buf.fillRect(barRc, pal.bgWindow);
                const float inN = normalizeDb(ins[bi], -60.0f, 0.0f);
                const float grN = normalizeDb(-grs[bi], 0.0f, 24.0f);
                if (inN > 0.0f) {
                    buf.fillRect(Rect(barRc.left, barRc.top,
                                      barRc.left + cast(int)(inN * barRc.width), barRc.bottom),
                                 adjustColorBrightness(BCOLS[bi], -10));
                }
                if (grN > 0.0f) {
                    const int gw = cast(int)(grN * barRc.width);
                    buf.fillRect(Rect(barRc.right - gw, barRc.top, barRc.right, barRc.bottom), pal.accentOrange);
                }
                drawVCenteredText(buf, valFnt, barRc.right + dp(4), Rect(barRc.right + dp(4), ry, c.right - dp(4), ry + rowH),
                                  toUTF32(format("%.1fdB", grs[bi])), pal.fgMuted);
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
            Rect c = dl.canvasRc;
            const uint faceBg = pal.retroBevel ? pal.bgPanel : adjustColorBrightness(pal.bgPanelAlt, 10);
            drawThemeCard(buf, c, faceBg, pal.borderStrong, pal, 2.0f, false);
            drawVCenteredText(buf, uiFnt, c.left + dp(8), Rect(c.left, c.bottom - dp(18), c.left + dp(90), c.bottom - dp(2)),
                              "TB-303 ACID"d, pal.accentOrange);
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
        const uint fill = pl.active ? pl.activeColor : pal.bgControl;
        drawThemeCard(buf, pl.rc, fill, pal.borderStrong, pal, 2.0f, pal.retroBevel);
        drawCenteredText(buf, uiFnt, pl.rc, toUTF32(pl.label),
                         pl.active ? contrastTextColor(fill) : pal.fgText);
    }

    // 3. Render rotary knobs cleanly
    foreach (const ref kn; dl.knobs) {
        const float val = readPlugParam(plug, kn.paramId, kn.defaultVal);
        drawRotaryKnob(buf, kn.cx, kn.cy, kn.r, val, pal.bgLcd, kn.accent, pal.bgControl, pal.borderSubtle, false);
        drawCenteredText(buf, uiFnt, Rect(kn.cellRc.left, kn.cy + kn.r + dp(1), kn.cellRc.right, kn.cy + kn.r + dp(15)),
                         toUTF32(truncateLabel(kn.label, 9)), pal.fgText);
        drawCenteredText(buf, valFnt, Rect(kn.cellRc.left, kn.cy - kn.r - dp(12), kn.cellRc.right, kn.cy - kn.r),
                         toUTF32(truncateLabel(kn.formattedVal, 9)), pal.fgMuted);
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
                if (onTriggerPad !is null) onTriggerPad(trkIdx, pl.paramId);
            } else if (pl.actionKind == 4) {
                if (onLoadSample !is null) onLoadSample(trkIdx, plug.pluginIndex);
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

        // 5. EQ interactive band handle drag
        if (dl.kind == BuiltinDeviceKind.EQ && dl.canvasRc.isPointInside(event.x, event.y)) {
            const int pad = dp(6);
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
        if (ui.eqDragBand >= 0 && dl.kind == BuiltinDeviceKind.EQ) {
            const int pad = dp(6);
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
        ui.envDragPoint = -1;
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
    uint triggeredNote = 0;
    // Last pill in Acid Bass is the bottom 13th key (C4 = note 60)
    const auto c4Pill = dlAcid.pills[$ - 1];
    handleBuiltinDeviceMouse(
        new MouseEvent(MouseAction.ButtonDown, MouseButton.Left, MouseFlag.LButton,
                       cast(short)c4Pill.rc.middlex, cast(short)c4Pill.rc.middley),
        bodyRc, &model, &acidSlot, acidUi, 103, null,
        (int t, uint note) { triggeredNote = note; }, null, null, null);
    assert(triggeredNote == 60, "Expected Acid Bass bottom virtual keyboard click to trigger MIDI note 60");
}
