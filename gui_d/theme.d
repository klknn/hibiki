module gui_d.theme;

import core.math : fabs;
import std.format : format;

/// Y2K Workstation (FL Studio 8 / Logic 5 / Pro Tools 5) color palette.
/// Note: `dlangui` uses `0xAARRGGBB` where `AA = 0x00` is fully opaque and `0xFF` is transparent.
enum Y2kColor : uint {
    BG_WORKSPACE   = 0x00282C32,
    BG_PANEL       = 0x00333840,
    BG_PANEL_DARK  = 0x0020242A,
    BG_LCD         = 0x000E1612,
    FG_LCD_AMBER   = 0x00FFB020,
    FG_LCD_CYAN    = 0x0044E0D0,
    FG_TEXT        = 0x00E2E6EC,
    FG_MUTED       = 0x00969EA8,
    BEVEL_LIGHT    = 0x00525864,
    BEVEL_DARK     = 0x0014171C,
    LED_GREEN      = 0x0032D74B,
    LED_AMBER      = 0x00FFCC00,
    LED_RED        = 0x00FF3B30,
    LED_OFF        = 0x001A1E24,
    ACCENT_PLAY    = 0x0034C759,
    ACCENT_REC     = 0x00FF3B30,
    ACCENT_SOLO    = 0x00FFD60A,
    ACCENT_MUTE    = 0x00FF9F0A,
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

/// Formats seconds and BPM as `BAR:BEAT:TICK` (4/4 time, 96 PPQ display).
string formatBarBeat(double seconds, double bpm) @safe {
    if (seconds < 0.0) seconds = 0.0;
    if (bpm <= 0.0) bpm = 120.0;
    const double totalBeats = seconds * (bpm / 60.0);
    const long beatIdx = cast(long)totalBeats;
    const long bar = (beatIdx / 4) + 1;
    const long beat = (beatIdx % 4) + 1;
    const long tick = cast(long)((totalBeats - cast(double)beatIdx) * 96.0);
    return format("%03d:%d:%02d", bar, beat, tick);
}

/// Maps a linear peak amplitude `[0, 1+]` to `0 .. numSegments` lit LED segments
/// using a `-48 dB .. 0 dB` logarithmic scale.
int peakToMeterSegments(float linearPeak, int numSegments = 12) nothrow @nogc @safe {
    import core.stdc.math : log10f;

    if (numSegments <= 0 || linearPeak <= 1e-4f) return 0;
    float db = 20.0f * log10f(cast(float)fabs(linearPeak));
    if (db <= -48.0f) return 0;
    if (db >= 0.0f) return numSegments;
    float norm = (db + 48.0f) / 48.0f;
    int lit = cast(int)(norm * numSegments + 0.5f);
    if (lit < 0) return 0;
    if (lit > numSegments) return numSegments;
    return lit;
}

/// Returns the LED color for segment `idx` out of `numSegments` (green -> amber -> red).
uint segmentColor(int idx, int numSegments) pure nothrow @nogc @safe {
    if (numSegments <= 0) return Y2kColor.LED_GREEN;
    float frac = cast(float)(idx + 1) / cast(float)numSegments;
    if (frac > 0.85f) return Y2kColor.LED_RED;
    if (frac > 0.65f) return Y2kColor.LED_AMBER;
    return Y2kColor.LED_GREEN;
}

unittest {
    assert(formatTimecode(0.0) == "00:00.000");
    assert(formatTimecode(65.125) == "01:05.125");
    assert(formatTimecode(-1.0) == "00:00.000");

    assert(formatBarBeat(0.0, 120.0) == "001:1:00");
    assert(formatBarBeat(2.0, 120.0) == "002:1:00"); // 4 beats at 120 BPM = 2s

    assert(peakToMeterSegments(0.0f, 12) == 0);
    assert(peakToMeterSegments(1.0f, 12) == 12);
    const int mid = peakToMeterSegments(0.25f, 12);
    assert(mid > 5 && mid < 12);

    assert(segmentColor(0, 12) == Y2kColor.LED_GREEN);
    assert(segmentColor(8, 12) == Y2kColor.LED_AMBER);
    assert(segmentColor(11, 12) == Y2kColor.LED_RED);
}
