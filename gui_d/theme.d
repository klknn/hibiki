module gui_d.theme;

import core.math : fabs;
import core.stdc.math : cosf, log10f, sinf, sqrtf;
import dlangui.core.types : Point, Rect;
import dlangui.graphics.colors : COLOR_TRANSPARENT;
import dlangui.graphics.drawbuf : DrawBuf;
import std.format : format;

/// Available FL Studio 2026 UI theme presets (accessible via Options > Theme).
enum FlThemeId {
    DEFAULT_DARK,
    ULTRAMARINE,
    MIDNIGHT_MINT,
    WARM_AMBER,
}

/// FL Studio 2026 color palette (`0x00RRGGBB` opaque, `0xAARRGGBB` translucent in `dlangui`).
struct FlPalette {
    string name;
    uint bgDesktop;
    uint bgToolbar;
    uint bgToolbarWell;
    uint bgWindow;
    uint bgWindowHeader;
    uint bgWindowHeaderActive;
    uint bgPanel;
    uint bgPanelAlt;
    uint bgControl;
    uint bgControlHover;
    uint bgLcd;

    uint borderSubtle;
    uint borderStrong;
    uint borderAccent;

    uint fgText;
    uint fgMuted;
    uint fgDim;

    uint accentOrange;
    uint accentGreen;
    uint accentRed;
    uint accentCyan;
    uint accentAmber;
    uint accentPurple;
    uint accentBlue;

    uint stepGroupA;
    uint stepGroupB;
    uint stepActive;
    uint stepActiveLed;
    uint ledOff;

    uint clipPattern;
    uint clipAudio;
    uint clipAutomation;
    uint noteFill;
    uint noteGhost;
}

/// Returns the FL Studio 2026 palette for `id`.
FlPalette getFlPalette(FlThemeId id = FlThemeId.DEFAULT_DARK) pure nothrow @safe {
    final switch (id) {
        case FlThemeId.DEFAULT_DARK:
            return FlPalette(
                "FL Default Dark",
                0x001B1F24, // bgDesktop
                0x002D323A, // bgToolbar
                0x00181C21, // bgToolbarWell
                0x002A2F37, // bgWindow
                0x00242930, // bgWindowHeader
                0x00343B45, // bgWindowHeaderActive
                0x00313740, // bgPanel
                0x00272C34, // bgPanelAlt
                0x003A414C, // bgControl
                0x00464E5B, // bgControlHover
                0x0013171C, // bgLcd
                0x00424A56, // borderSubtle
                0x0015181C, // borderStrong
                0x00FF8F2D, // borderAccent
                0x00E8ECF2, // fgText
                0x0096A0AC, // fgMuted
                0x0066707C, // fgDim
                0x00FF8F2D, // accentOrange
                0x004CE07A, // accentGreen
                0x00F24C4C, // accentRed
                0x003ED6C4, // accentCyan
                0x00FFBE3B, // accentAmber
                0x009B72CF, // accentPurple
                0x005294E2, // accentBlue
                0x00424954, // stepGroupA
                0x005C494E, // stepGroupB (classic FL Studio warm reddish slate)
                0x00D8DCE4, // stepActive
                0x00FF8F2D, // stepActiveLed
                0x001D2127, // ledOff
                0x00385770, // clipPattern
                0x00295B55, // clipAudio
                0x0059436D, // clipAutomation
                0x006CE086, // noteFill (classic FL Piano Roll mint green)
                0x805C6874  // noteGhost
            );
        case FlThemeId.ULTRAMARINE:
            return FlPalette(
                "Ultramarine",
                0x00151926,
                0x0022283A,
                0x00111521,
                0x00202636,
                0x001A2030,
                0x002B344D,
                0x00262D40,
                0x001D2333,
                0x00313A52,
                0x003E4966,
                0x000D111B,
                0x003B4661,
                0x000E121B,
                0x005BA4FF,
                0x00E6ECFA,
                0x008F9BB8,
                0x005D6882,
                0x00FF9640,
                0x0045E09C,
                0x00FF5768,
                0x004CE0F0,
                0x00FFC845,
                0x00A67EFF,
                0x005BA4FF,
                0x00353F59,
                0x004C4461,
                0x00DCE4F5,
                0x005BA4FF,
                0x00161B29,
                0x00335280,
                0x00265E68,
                0x005D4280,
                0x006AE3B0,
                0x80556280
            );
        case FlThemeId.MIDNIGHT_MINT:
            return FlPalette(
                "Midnight Mint",
                0x00141C1B,
                0x00202B29,
                0x00101716,
                0x001E2826,
                0x00182220,
                0x00273633,
                0x0024302E,
                0x001B2523,
                0x002D3C39,
                0x00384A46,
                0x000C1211,
                0x00374A46,
                0x000E1413,
                0x003EE6B5,
                0x00E4F2EE,
                0x008BA69E,
                0x00576E67,
                0x00FF9F43,
                0x003EE6B5,
                0x00FF5C5C,
                0x0048D9E8,
                0x00FFD154,
                0x009B7CE6,
                0x004FA8E8,
                0x0030423E,
                0x00444D42,
                0x00D8EDE6,
                0x003EE6B5,
                0x00151E1C,
                0x002D575A,
                0x00255E4E,
                0x004C456E,
                0x0052E8A8,
                0x804F6660
            );
        case FlThemeId.WARM_AMBER:
            return FlPalette(
                "Warm Amber",
                0x001E1B18,
                0x002F2A25,
                0x00171412,
                0x002C2722,
                0x0025211D,
                0x0038312A,
                0x00332D27,
                0x0029241F,
                0x003F3831,
                0x004C433B,
                0x0014110E,
                0x004D453C,
                0x0014110F,
                0x00FFA43B,
                0x00F2ECE4,
                0x00A69B8E,
                0x0070675D,
                0x00FFA43B,
                0x0065DB75,
                0x00F25244,
                0x0052CFD6,
                0x00FFD24C,
                0x00A678C9,
                0x006498DB,
                0x00453E36,
                0x005C443B,
                0x00EDE4D8,
                0x00FFA43B,
                0x001F1B17,
                0x005E4D38,
                0x00385949,
                0x005C4461,
                0x007DE072,
                0x806B6054
            );
    }
}

/// Draws a flat-modern FL Studio card/panel with subtle 1px border and clipped corners.
/// Implemented with `fillRect` so it renders identically on both `ColorDrawBuf` and `GLDrawBuf`.
void drawCard(DrawBuf buf, Rect rc, uint fillColor, uint borderColor, float radius = 4.0f) {
    if (rc.width <= 0 || rc.height <= 0) return;
    const int r = (radius >= 3.0f && rc.width >= 10 && rc.height >= 10) ? 2 :
                  ((radius >= 1.5f && rc.width >= 6 && rc.height >= 6) ? 1 : 0);

    if (r == 0) {
        buf.fillRect(rc, fillColor);
        if (borderColor != COLOR_TRANSPARENT && rc.width >= 2 && rc.height >= 2) {
            buf.fillRect(Rect(rc.left, rc.top, rc.right, rc.top + 1), borderColor);
            buf.fillRect(Rect(rc.left, rc.bottom - 1, rc.right, rc.bottom), borderColor);
            buf.fillRect(Rect(rc.left, rc.top, rc.left + 1, rc.bottom), borderColor);
            buf.fillRect(Rect(rc.right - 1, rc.top, rc.right, rc.bottom), borderColor);
        }
        return;
    }

    // Fill body with rounded corner insets
    buf.fillRect(Rect(rc.left + r, rc.top, rc.right - r, rc.bottom), fillColor);
    buf.fillRect(Rect(rc.left, rc.top + r, rc.left + r, rc.bottom - r), fillColor);
    buf.fillRect(Rect(rc.right - r, rc.top + r, rc.right, rc.bottom - r), fillColor);
    if (r == 2) {
        buf.fillRect(Rect(rc.left + 1, rc.top + 1, rc.right - 1, rc.top + 2), fillColor);
        buf.fillRect(Rect(rc.left + 1, rc.bottom - 2, rc.right - 1, rc.bottom - 1), fillColor);
    }

    if (borderColor != COLOR_TRANSPARENT) {
        buf.fillRect(Rect(rc.left + r, rc.top, rc.right - r, rc.top + 1), borderColor);
        buf.fillRect(Rect(rc.left + r, rc.bottom - 1, rc.right - r, rc.bottom), borderColor);
        buf.fillRect(Rect(rc.left, rc.top + r, rc.left + 1, rc.bottom - r), borderColor);
        buf.fillRect(Rect(rc.right - 1, rc.top + r, rc.right, rc.bottom - r), borderColor);
        if (r == 2) {
            buf.drawPixel(rc.left + 1, rc.top + 1, borderColor);
            buf.drawPixel(rc.right - 2, rc.top + 1, borderColor);
            buf.drawPixel(rc.left + 1, rc.bottom - 2, borderColor);
            buf.drawPixel(rc.right - 2, rc.bottom - 2, borderColor);
        }
    }
}

/// Draws a 1px or 2px line using `DrawBuf.drawLine` (works on both `ColorDrawBuf` and `GLDrawBuf`).
void drawThickLine(DrawBuf buf, float x0, float y0, float x1, float y1, float width, uint color) {
    const int ix0 = cast(int)(x0 + 0.5f);
    const int iy0 = cast(int)(y0 + 0.5f);
    const int ix1 = cast(int)(x1 + 0.5f);
    const int iy1 = cast(int)(y1 + 0.5f);
    buf.drawLine(Point(ix0, iy0), Point(ix1, iy1), color);
    if (width >= 1.8f) {
        if (fabs(x1 - x0) >= fabs(y1 - y0)) {
            buf.drawLine(Point(ix0, iy0 + 1), Point(ix1, iy1 + 1), color);
        } else {
            buf.drawLine(Point(ix0 + 1, iy0), Point(ix1 + 1, iy1), color);
        }
    }
}

/// Draws an open circular arc stroke between `startDeg` and `endDeg` (in degrees, 0 = right, 90 = down).
void drawOpenArc(
    DrawBuf buf,
    float cx,
    float cy,
    float radius,
    float startDeg,
    float endDeg,
    float lineWidth,
    uint color,
    int segments = 18
) {
    if (radius <= 0.5f || segments < 2) return;
    enum float DEG2RAD = 3.14159265f / 180.0f;
    const float a0 = startDeg * DEG2RAD;
    const float a1 = endDeg * DEG2RAD;
    const float step = (a1 - a0) / cast(float)segments;
    float prevX = cx + cosf(a0) * radius;
    float prevY = cy + sinf(a0) * radius;
    foreach (i; 1 .. segments + 1) {
        const float a = a0 + step * cast(float)i;
        const float nx = cx + cosf(a) * radius;
        const float ny = cy + sinf(a) * radius;
        drawThickLine(buf, prevX, prevY, nx, ny, lineWidth, color);
        prevX = nx;
        prevY = ny;
    }
}

/// Draws a filled circle via horizontal `fillRect` scanlines (universal across `ColorDrawBuf` & `GLDrawBuf`).
void drawFilledCircle(DrawBuf buf, float cx, float cy, float radius, uint fillColor, uint borderColor = COLOR_TRANSPARENT) {
    if (radius <= 0.5f) return;
    const int icx = cast(int)(cx + 0.5f);
    const int icy = cast(int)(cy + 0.5f);
    const int r = cast(int)(radius + 0.5f);
    const float r2 = radius * radius;

    if (fillColor != COLOR_TRANSPARENT) {
        foreach (dy; -r .. r + 1) {
            const float rem = r2 - cast(float)(dy * dy);
            if (rem < 0.0f) continue;
            const int dx = cast(int)sqrtf(rem);
            buf.fillRect(Rect(icx - dx, icy + dy, icx + dx + 1, icy + dy + 1), fillColor);
        }
    }
    if (borderColor != COLOR_TRANSPARENT) {
        drawOpenArc(buf, cx, cy, radius, 0.0f, 360.0f, 1.0f, borderColor, 18);
    }
}

/// Draws an FL Studio 2026 rotary knob with outer value arc and pointer needle.
/// `normValue` is `[0..1]`. If `bipolar`, `0.5` is center (12 o'clock) and arc sweeps left/right.
void drawRotaryKnob(
    DrawBuf buf,
    int cx,
    int cy,
    int radius,
    float normValue,
    uint trackColor,
    uint arcColor,
    uint capColor,
    uint borderColor,
    bool bipolar = false
) {
    if (radius < 4) return;
    float v = normValue;
    if (v < 0.0f) v = 0.0f;
    if (v > 1.0f) v = 1.0f;

    // FL Studio rotary sweep: 135 deg (bottom-left) clockwise to 405 deg (bottom-right)
    enum float START_DEG = 135.0f;
    enum float END_DEG = 405.0f;
    enum float CENTER_DEG = 270.0f;
    const float valDeg = START_DEG + v * (END_DEG - START_DEG);

    const float arcR = cast(float)radius;
    drawOpenArc(buf, cx, cy, arcR, START_DEG, END_DEG, 2.0f, trackColor, 18);

    if (bipolar) {
        if (fabs(v - 0.5f) > 0.01f) {
            if (v >= 0.5f) {
                drawOpenArc(buf, cx, cy, arcR, CENTER_DEG, valDeg, 2.2f, arcColor, 12);
            } else {
                drawOpenArc(buf, cx, cy, arcR, valDeg, CENTER_DEG, 2.2f, arcColor, 12);
            }
        }
    } else if (v > 0.01f) {
        drawOpenArc(buf, cx, cy, arcR, START_DEG, valDeg, 2.2f, arcColor, 16);
    }

    const float capR = arcR - 3.0f;
    drawFilledCircle(buf, cx, cy, capR, capColor, borderColor);

    enum float DEG2RAD = 3.14159265f / 180.0f;
    const float rad = valDeg * DEG2RAD;
    const float x0 = cx + cosf(rad) * (capR * 0.25f);
    const float y0 = cy + sinf(rad) * (capR * 0.25f);
    const float x1 = cx + cosf(rad) * (capR - 1.0f);
    const float y1 = cy + sinf(rad) * (capR - 1.0f);
    drawThickLine(buf, x0, y0, x1, y1, 1.6f, arcColor);
}

/// Draws a modern FL Studio stereo peak meter inside `rc` (`-60 dB .. 0 dB`).
void drawLevelMeter(
    DrawBuf buf,
    Rect rc,
    float peakL,
    float peakR,
    const ref FlPalette pal,
    int segments = 18
) {
    drawCard(buf, rc, pal.bgLcd, pal.borderStrong, 2.0f);
    if (rc.width < 8 || rc.height < segments * 2) return;
    const int innerLeft = rc.left + 2;
    const int innerW = rc.width - 4;
    const int innerH = rc.height - 4;
    const int colW = (innerW - 2) / 2;
    const int segH = innerH / segments;
    const int litL = peakToMeterSegments(peakL, segments);
    const int litR = peakToMeterSegments(peakR, segments);

    foreach (s; 0 .. segments) {
        const int y0 = rc.bottom - 2 - (s + 1) * segH + 1;
        const int y1 = rc.bottom - 2 - s * segH - 1;
        if (y1 <= y0) continue;
        const uint onCol = segmentColor(s, segments, pal);
        buf.fillRect(Rect(innerLeft, y0, innerLeft + colW, y1),
                     s < litL ? onCol : pal.ledOff);
        buf.fillRect(Rect(innerLeft + colW + 2, y0, innerLeft + colW * 2 + 2, y1),
                     s < litR ? onCol : pal.ledOff);
    }
}

/// Draws the classic FL Studio downward-pointing playhead triangle in the timeline ruler.
void drawPlayheadTriangle(DrawBuf buf, int cx, int topY, int halfW, int height, uint color) {
    if (height <= 0 || halfW <= 0) return;
    foreach (dy; 0 .. height) {
        const int span = (halfW * (height - dy)) / height;
        buf.fillRect(Rect(cx - span, topY + dy, cx + span + 1, topY + dy + 1), color);
    }
}

/// Truncates `label` to `maxChars` characters cleanly.
string truncateLabel(string label, size_t maxChars = 14) pure @safe {
    if (label.length <= maxChars) return label;
    if (maxChars <= 2) return label[0 .. maxChars];
    return label[0 .. maxChars - 2] ~ "..";
}

/// Formats seconds as `MM:SS.mmm` for the LCD transport readout.
string formatTimecode(double seconds) @safe {
    if (seconds < 0.0) seconds = 0.0;
    const long totalMs = cast(long)(seconds * 1000.0 + 0.5);
    const long mins = totalMs / 60_000;
    const long secs = (totalMs % 60_000) / 1000;
    const long ms = totalMs % 1000;
    return format("%02d:%02d.%03d", mins, secs, ms);
}

/// Formats seconds as `BBB:B:SS` (Bar:Beat:16th-Step) at `bpm` in 4/4 time.
string formatBarBeat(double seconds, double bpm) @safe {
    if (seconds < 0.0) seconds = 0.0;
    if (bpm <= 0.0) bpm = 140.0;
    const double totalBeats = seconds * (bpm / 60.0);
    const int bar = cast(int)(totalBeats / 4.0) + 1;
    const int beat = cast(int)(totalBeats) % 4 + 1;
    const int step = cast(int)((totalBeats - cast(int)totalBeats) * 4.0) + 1;
    return format("%03d:%d:%02d", bar, beat, step);
}

/// Converts a linear peak amplitude `[0..1+]` into a normalized `[0..1]` meter height (`-60 dB .. 0 dB`).
float peakToMeterNormalized(float linearPeak) @safe {
    const float mag = fabs(linearPeak);
    if (mag <= 0.001f) return 0.0f;
    const float db = 20.0f * log10f(mag);
    if (db <= -60.0f) return 0.0f;
    if (db >= 0.0f) return 1.0f;
    return (db + 60.0f) / 60.0f;
}

/// Maps linear peak amplitude into `[0..totalSegments]` meter segments.
int peakToMeterSegments(float linearPeak, int totalSegments) @safe {
    if (totalSegments <= 0) return 0;
    const int lit = cast(int)(peakToMeterNormalized(linearPeak) * totalSegments + 0.5f);
    if (lit < 0) return 0;
    return lit > totalSegments ? totalSegments : lit;
}

/// Returns the meter segment color (green -> amber -> red).
uint segmentColor(int idx, int totalSegments, const ref FlPalette pal) pure nothrow @safe {
    if (totalSegments <= 0) return pal.accentGreen;
    const double frac = cast(double)idx / totalSegments;
    if (frac >= 0.85) return pal.accentRed;
    if (frac >= 0.65) return pal.accentAmber;
    return pal.accentGreen;
}

unittest {
    import dlangui.graphics.drawbuf : ColorDrawBuf;

    const pal = getFlPalette(FlThemeId.DEFAULT_DARK);
    assert(pal.accentOrange == 0x00FF8F2D);
    assert(getFlPalette(FlThemeId.ULTRAMARINE).name == "Ultramarine");

    assert(formatTimecode(0.0) == "00:00.000");
    assert(formatTimecode(65.432) == "01:05.432");
    assert(formatBarBeat(0.0, 120.0) == "001:1:01");
    assert(formatBarBeat(2.0, 120.0) == "002:1:01");
    assert(peakToMeterNormalized(0.0f) == 0.0f);
    assert(peakToMeterNormalized(1.0f) == 1.0f);
    assert(peakToMeterNormalized(0.1f) > 0.6f && peakToMeterNormalized(0.1f) < 0.7f);
    assert(truncateLabel("Bassline", 10) == "Bassline");
    assert(truncateLabel("VeryLongTrackName", 10) == "VeryLong..");

    auto buf = new ColorDrawBuf(120, 120);
    buf.fill(pal.bgDesktop);
    drawCard(buf, Rect(10, 10, 100, 80), pal.bgPanel, pal.borderSubtle, 4.0f);
    assert(buf.scanLine(40)[50] == pal.bgPanel);

    drawRotaryKnob(buf, 50, 50, 14, 0.75f, pal.bgLcd, pal.accentOrange, pal.bgControl, pal.borderSubtle);
    drawLevelMeter(buf, Rect(85, 10, 110, 110), 1.0f, 0.0f, pal, 14);
}
