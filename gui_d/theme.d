module gui_d.theme;

import core.math : fabs;
import std.format : format;

/// FL Studio 2026-inspired dark slate palette (`0x00RRGGBB` opaque format for `dlangui` `ColorDrawBuf`).
enum Y2kColor : uint {
    BG_WORKSPACE   = 0x001E2328,
    BG_PANEL       = 0x002B3139,
    BG_PANEL_DARK  = 0x00191D22,
    BG_LCD         = 0x0010161C,
    FG_LCD_AMBER   = 0x00FF9E3D,
    FG_LCD_CYAN    = 0x004DE8D4,
    FG_TEXT        = 0x00E6EAF0,
    FG_MUTED       = 0x008E98A4,
    BEVEL_LIGHT    = 0x00444C56,
    BEVEL_DARK     = 0x00121519,
    LED_GREEN      = 0x002CE5A7,
    LED_AMBER      = 0x00FF9E3D,
    LED_RED        = 0x00FF4B4B,
    LED_OFF        = 0x001A1E24,
    ACCENT_PLAY    = 0x002CE5A7,
    ACCENT_REC     = 0x00FF4B4B,
    ACCENT_SOLO    = 0x00FFD60A,
    ACCENT_MUTE    = 0x00FF8C2A,
    STEP_EVEN      = 0x00373E47,
    STEP_ODD       = 0x004B535E,
    STEP_ACTIVE    = 0x00FF8C2A,
    CLIP_PATTERN   = 0x003D5A73,
    CLIP_AUDIO     = 0x002D5F5D,
    CLIP_AUTO      = 0x005B4570,
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
    import core.stdc.math : log10f;
    const float db = 20.0f * log10f(mag);
    if (db <= -60.0f) return 0.0f;
    if (db >= 0.0f) return 1.0f;
    return (db + 60.0f) / 60.0f;
}

/// Maps linear peak amplitude into `[0..totalSegments]` LED meter segments.
int peakToMeterSegments(float linearPeak, int totalSegments) @safe {
    if (totalSegments <= 0) return 0;
    const int lit = cast(int)(peakToMeterNormalized(linearPeak) * totalSegments + 0.5f);
    if (lit < 0) return 0;
    return lit > totalSegments ? totalSegments : lit;
}

/// Returns the LED color for segment `idx` out of `totalSegments` (green -> amber -> red).
uint segmentColor(int idx, int totalSegments) pure nothrow @safe {
    if (totalSegments <= 0) return Y2kColor.LED_GREEN;
    const double frac = cast(double)idx / totalSegments;
    if (frac >= 0.85) return Y2kColor.LED_RED;
    if (frac >= 0.65) return Y2kColor.LED_AMBER;
    return Y2kColor.LED_GREEN;
}

unittest {
    assert(formatTimecode(0.0) == "00:00.000");
    assert(formatTimecode(65.432) == "01:05.432");
    assert(formatBarBeat(0.0, 120.0) == "001:1:01");
    assert(formatBarBeat(2.0, 120.0) == "002:1:01");
    assert(peakToMeterNormalized(0.0f) == 0.0f);
    assert(peakToMeterNormalized(1.0f) == 1.0f);
    assert(peakToMeterNormalized(0.1f) > 0.6f && peakToMeterNormalized(0.1f) < 0.7f);
}
