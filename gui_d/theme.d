module gui_d.theme;

import core.math : fabs;
import core.stdc.math : cosf, log10f, sinf, sqrtf;
import dlangui.core.config : ENABLE_FREETYPE;
import dlangui.core.types : BASE_DPI, Point, Rect, SCREEN_DPI;
import dlangui.graphics.colors : COLOR_TRANSPARENT;
import dlangui.graphics.drawbuf : DrawBuf;
import dlangui.graphics.fonts : FontFamily, FontManager, FontRef, FontWeight;
import dlangui.widgets.controls : Button;
import dlangui.widgets.widget : State, Visibility, Widget;
import std.file : exists, thisExePath;
import std.format : format;
import std.path : buildPath, dirName;
import std.process : environment;

/// Free MS-Gothic-inspired open fonts (Morisawa BIZ UDGothic / UDPGothic, SIL OFL 1.1).
enum string DAW_UI_FONT_FACE = "BIZ UDPGothic";
enum string DAW_MONO_FONT_FACE = "BIZ UDGothic";

private __gshared string activeUiFontFace_ = null;
private __gshared string activeMonoFontFace_ = null;

/// Searches Bazel runfiles and working tree for bundled `BIZ UDGothic` TTF fonts.
string findDawFontDirectory() {
    string[] dirs;
    const string rfDir = environment.get("RUNFILES_DIR", "");
    if (rfDir.length > 0) {
        dirs ~= buildPath(rfDir, "+http_archive+biz_ud_gothic");
        dirs ~= buildPath(rfDir, "_main", "external", "+http_archive+biz_ud_gothic");
    }
    try {
        const string exeDir = dirName(thisExePath());
        dirs ~= buildPath(exeDir, "hibiki_gui.runfiles", "+http_archive+biz_ud_gothic");
        dirs ~= buildPath(exeDir, "..", "+http_archive+biz_ud_gothic");
    } catch (Exception) {
    }
    dirs ~= [
        buildPath("..", "+http_archive+biz_ud_gothic"),
        buildPath("external", "+http_archive+biz_ud_gothic"),
        "+http_archive+biz_ud_gothic",
    ];
    foreach (dir; dirs) {
        if (exists(buildPath(dir, "BIZUDGothic-Regular.ttf")) &&
            exists(buildPath(dir, "BIZUDPGothic-Regular.ttf"))) {
            return dir;
        }
    }
    return "";
}

/// Registers bundled MS-Gothic-style (`BIZ UDGothic` / `BIZ UDPGothic`) fonts into `FontManager`.
bool registerBundledDawFonts() {
    static if (ENABLE_FREETYPE) {
        import dlangui.graphics.ftfonts : FreeTypeFontManager;
        auto ft = cast(FreeTypeFontManager)FontManager.instance;
        if (ft is null) return false;
        const string dir = findDawFontDirectory();
        if (dir.length == 0) return false;

        bool ok = true;
        ok &= ft.registerFont(buildPath(dir, "BIZUDPGothic-Regular.ttf"),
                              FontFamily.SansSerif, DAW_UI_FONT_FACE, false, FontWeight.Normal, true);
        ok &= ft.registerFont(buildPath(dir, "BIZUDPGothic-Bold.ttf"),
                              FontFamily.SansSerif, DAW_UI_FONT_FACE, false, FontWeight.Bold, true);
        ok &= ft.registerFont(buildPath(dir, "BIZUDGothic-Regular.ttf"),
                              FontFamily.MonoSpace, DAW_MONO_FONT_FACE, false, FontWeight.Normal, true);
        ok &= ft.registerFont(buildPath(dir, "BIZUDGothic-Bold.ttf"),
                              FontFamily.MonoSpace, DAW_MONO_FONT_FACE, false, FontWeight.Bold, true);
        if (ok) {
            activeUiFontFace_ = DAW_UI_FONT_FACE;
            activeMonoFontFace_ = DAW_MONO_FONT_FACE;
        }
        return ok;
    } else {
        return false;
    }
}

/// Returns active UI font face name (`BIZ UDPGothic` when bundled fonts are loaded, else `null`).
string activeDawUiFontFace() nothrow @trusted {
    return activeUiFontFace_;
}

/// Returns the active display scale factor relative to 96 DPI (e.g. 1.0 at 96 DPI, 2.0 on macOS Retina).
float uiScale() nothrow @trusted {
    try {
        const float s = cast(float)SCREEN_DPI / cast(float)BASE_DPI;
        return s > 1.0f ? s : 1.0f;
    } catch (Throwable) {
        return 1.0f;
    }
}

/// Scales a logical 1x design pixel measurement to physical surface pixels.
int dp(int px) nothrow @safe {
    return cast(int)(cast(float)px * uiScale() + (px >= 0 ? 0.5f : -0.5f));
}

/// Scales a logical 1x floating-point measurement to physical surface pixels.
float dpf(float px) nothrow @safe {
    return px * uiScale();
}

/// Returns a DPI-scaled DAW UI font cleanly falling back to `fallback` in headless mode.
FontRef getDawFont(int basePx, FontRef fallback = FontRef.init, int weight = FontWeight.Normal, bool mono = false) {
    const int px = dp(basePx);
    if (FontManager.instance !is null) {
        FontFamily fam = mono ? FontFamily.MonoSpace : FontFamily.SansSerif;
        string face = null;
        if (mono && activeMonoFontFace_.length > 0) {
            face = activeMonoFontFace_;
        } else if (!mono && activeUiFontFace_.length > 0) {
            face = activeUiFontFace_;
        } else if (!mono && !fallback.isNull) {
            face = fallback.face;
        }
        auto f = FontManager.instance.getFont(px, weight, false, fam, face);
        if (!f.isNull) return f;
    }
    return fallback;
}

/// Returns a DPI-scaled monospace font for numeric/timecode/dB readouts.
FontRef getDawMonoFont(int basePx, FontRef fallback = FontRef.init, int weight = FontWeight.Normal) {
    return getDawFont(basePx, fallback, weight, true);
}

/// Safely resolves `w.font` without dereferencing a null `FontManager.instance` in headless tests.
FontRef safeWidgetFont(Widget w) {
    return (FontManager.instance !is null && w !is null) ? w.font : FontRef.init;
}

/// Draws horizontally & vertically centered text inside `rc`.
void drawCenteredText(DrawBuf buf, FontRef fnt, Rect rc, dstring text, uint color) {
    if (fnt.isNull || text.length == 0 || rc.width <= 0 || rc.height <= 0) return;
    const Point sz = fnt.textSize(text);
    const int x = rc.left + (rc.width - sz.x) / 2;
    const int y = rc.top + (rc.height - sz.y) / 2;
    fnt.drawText(buf, x, y, text, color);
}

/// Draws vertically centered text inside `rc` starting at `x`.
void drawVCenteredText(DrawBuf buf, FontRef fnt, int x, Rect rc, dstring text, uint color) {
    if (fnt.isNull || text.length == 0 || rc.height <= 0) return;
    const Point sz = fnt.textSize(text);
    const int y = rc.top + (rc.height - sz.y) / 2;
    fnt.drawText(buf, x, y, text, color);
}

/// Draws right-aligned vertically centered text inside `rc` with `rightPad`.
void drawRightVCenteredText(DrawBuf buf, FontRef fnt, Rect rc, dstring text, uint color, int rightPad = 0) {
    if (fnt.isNull || text.length == 0 || rc.height <= 0) return;
    const Point sz = fnt.textSize(text);
    const int x = rc.right - rightPad - sz.x;
    const int y = rc.top + (rc.height - sz.y) / 2;
    fnt.drawText(buf, x, y, text, color);
}

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

/// Draws an FL Studio 2026 card/button surface.
/// When `radius == 0`, draws a pure sharp flat rectangle (no dividers when `borderColor == COLOR_TRANSPARENT`).
/// When `radius > 0`, slightly rounds the corners (`1-2dp`) and applies a subtle vertical edge gradient across
/// the corner width while keeping the main interior flat.
void drawCard(DrawBuf buf, Rect rc, uint fillColor, uint borderColor, float radius = 0.0f) {
    if (rc.width <= 0 || rc.height <= 0) return;
    const int bw = dp(1);
    const int cr = (radius >= 2.5f && rc.width >= dp(12) && rc.height >= dp(10))
        ? dp(2)
        : ((radius >= 1.2f && rc.width >= dp(8) && rc.height >= dp(6)) ? dp(1) : 0);

    if (cr == 0) {
        buf.fillRect(rc, fillColor);
        if (borderColor != COLOR_TRANSPARENT && rc.width >= bw * 2 && rc.height >= bw * 2) {
            buf.fillRect(Rect(rc.left, rc.top, rc.right, rc.top + bw), borderColor);
            buf.fillRect(Rect(rc.left, rc.bottom - bw, rc.right, rc.bottom), borderColor);
            buf.fillRect(Rect(rc.left, rc.top, rc.left + bw, rc.bottom), borderColor);
            buf.fillRect(Rect(rc.right - bw, rc.top, rc.right, rc.bottom), borderColor);
        }
        return;
    }

    // Flat main interior between the top/bottom rounded corner bands
    buf.fillRect(Rect(rc.left, rc.top + cr, rc.right, rc.bottom - cr), fillColor);

    // Subtle vertical gradient spanning only the rounded corner width
    const uint topOuterCol = adjustColorBrightness(fillColor, 10);
    const uint botOuterCol = adjustColorBrightness(fillColor, -10);
    if (cr > bw) {
        buf.fillRect(Rect(rc.left + cr, rc.top, rc.right - cr, rc.top + bw), topOuterCol);
        buf.fillRect(Rect(rc.left + bw, rc.top + bw, rc.right - bw, rc.top + cr),
                     adjustColorBrightness(fillColor, 4));
        buf.fillRect(Rect(rc.left + bw, rc.bottom - cr, rc.right - bw, rc.bottom - bw),
                     adjustColorBrightness(fillColor, -4));
        buf.fillRect(Rect(rc.left + cr, rc.bottom - bw, rc.right - cr, rc.bottom), botOuterCol);
    } else {
        buf.fillRect(Rect(rc.left + cr, rc.top, rc.right - cr, rc.top + cr), topOuterCol);
        buf.fillRect(Rect(rc.left + cr, rc.bottom - cr, rc.right - cr, rc.bottom), botOuterCol);
    }

    if (borderColor != COLOR_TRANSPARENT && rc.width >= bw * 3 && rc.height >= bw * 3) {
        buf.fillRect(Rect(rc.left + cr, rc.top, rc.right - cr, rc.top + bw), borderColor);
        buf.fillRect(Rect(rc.left + cr, rc.bottom - bw, rc.right - cr, rc.bottom), borderColor);
        buf.fillRect(Rect(rc.left, rc.top + cr, rc.left + bw, rc.bottom - cr), borderColor);
        buf.fillRect(Rect(rc.right - bw, rc.top + cr, rc.right, rc.bottom - cr), borderColor);
        if (cr > bw) {
            buf.fillRect(Rect(rc.left + bw, rc.top + bw, rc.left + cr, rc.top + cr), borderColor);
            buf.fillRect(Rect(rc.right - cr, rc.top + bw, rc.right - bw, rc.top + cr), borderColor);
            buf.fillRect(Rect(rc.left + bw, rc.bottom - cr, rc.left + cr, rc.bottom - bw), borderColor);
            buf.fillRect(Rect(rc.right - cr, rc.bottom - cr, rc.right - bw, rc.bottom - bw), borderColor);
            buf.fillRect(Rect(rc.left + cr, rc.top + bw, rc.right - cr, rc.top + cr),
                         adjustColorBrightness(fillColor, 8));
            buf.fillRect(Rect(rc.left + cr, rc.bottom - cr, rc.right - cr, rc.bottom - bw),
                         adjustColorBrightness(fillColor, -8));
        }
    }
}

/// Draws a thickness-scaled line using `DrawBuf.drawLine` (works on both `ColorDrawBuf` and `GLDrawBuf`).
void drawThickLine(DrawBuf buf, float x0, float y0, float x1, float y1, float width, uint color) {
    const int ix0 = cast(int)(x0 + 0.5f);
    const int iy0 = cast(int)(y0 + 0.5f);
    const int ix1 = cast(int)(x1 + 0.5f);
    const int iy1 = cast(int)(y1 + 0.5f);
    buf.drawLine(Point(ix0, iy0), Point(ix1, iy1), color);
    if (width >= 1.8f) {
        const int thickness = cast(int)(width + 0.3f);
        const bool horizDominant = fabs(x1 - x0) >= fabs(y1 - y0);
        foreach (d; 1 .. thickness) {
            if (horizDominant) {
                buf.drawLine(Point(ix0, iy0 + d), Point(ix1, iy1 + d), color);
            } else {
                buf.drawLine(Point(ix0 + d, iy0), Point(ix1 + d, iy1), color);
            }
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
    const float trackW = dpf(1.8f);
    const float arcW = dpf(2.2f);
    drawOpenArc(buf, cx, cy, arcR, START_DEG, END_DEG, trackW, trackColor, 20);

    if (bipolar) {
        if (fabs(v - 0.5f) > 0.01f) {
            if (v >= 0.5f) {
                drawOpenArc(buf, cx, cy, arcR, CENTER_DEG, valDeg, arcW, arcColor, 14);
            } else {
                drawOpenArc(buf, cx, cy, arcR, valDeg, CENTER_DEG, arcW, arcColor, 14);
            }
        }
    } else if (v > 0.01f) {
        drawOpenArc(buf, cx, cy, arcR, START_DEG, valDeg, arcW, arcColor, 18);
    }

    const float gap = dpf(3.0f);
    const float capR = arcR > gap + 1.0f ? arcR - gap : arcR * 0.7f;
    drawFilledCircle(buf, cx, cy, capR, capColor, COLOR_TRANSPARENT);
    if (capR >= dpf(4.0f)) {
        const float rimR = capR - dpf(0.6f);
        drawOpenArc(buf, cx, cy, rimR, 195.0f, 345.0f, dpf(1.1f), adjustColorBrightness(capColor, 16), 12);
        drawOpenArc(buf, cx, cy, rimR, 15.0f, 165.0f, dpf(1.1f), adjustColorBrightness(capColor, -14), 12);
    }
    if (borderColor != COLOR_TRANSPARENT) {
        drawOpenArc(buf, cx, cy, capR, 0.0f, 360.0f, 1.0f, borderColor, 18);
    }

    enum float DEG2RAD = 3.14159265f / 180.0f;
    const float rad = valDeg * DEG2RAD;
    const float x0 = cx + cosf(rad) * (capR * 0.25f);
    const float y0 = cy + sinf(rad) * (capR * 0.25f);
    const float x1 = cx + cosf(rad) * (capR - dpf(1.0f));
    const float y1 = cy + sinf(rad) * (capR - dpf(1.0f));
    drawThickLine(buf, x0, y0, x1, y1, dpf(1.6f), arcColor);
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
    const int pad = dp(2);
    if (rc.width < pad * 4 || rc.height < segments * 2) return;
    const int innerLeft = rc.left + pad;
    const int innerW = rc.width - pad * 2;
    const int innerH = rc.height - pad * 2;
    const int midGap = dp(2);
    const int colW = (innerW - midGap) / 2;
    const int segH = innerH / segments;
    const int segGap = segH >= dp(4) ? dp(1) : 0;
    const int litL = peakToMeterSegments(peakL, segments);
    const int litR = peakToMeterSegments(peakR, segments);

    foreach (s; 0 .. segments) {
        const int y0 = rc.bottom - pad - (s + 1) * segH + segGap;
        const int y1 = rc.bottom - pad - s * segH - segGap;
        if (y1 <= y0) continue;
        const uint onCol = segmentColor(s, segments, pal);
        buf.fillRect(Rect(innerLeft, y0, innerLeft + colW, y1),
                     s < litL ? onCol : pal.ledOff);
        buf.fillRect(Rect(innerLeft + colW + midGap, y0, innerLeft + colW * 2 + midGap, y1),
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

/// Safely adjusts RGB channels of a `0xAARRGGBB` color by `delta`, clamping each channel to `[0..255]` and preserving alpha.
uint adjustColorBrightness(uint color, int delta) pure nothrow @safe {
    const uint alpha = color & 0xFF000000;
    int r = cast(int)((color >> 16) & 0xFF) + delta;
    int g = cast(int)((color >> 8) & 0xFF) + delta;
    int b = cast(int)(color & 0xFF) + delta;
    if (r < 0) r = 0; else if (r > 255) r = 255;
    if (g < 0) g = 0; else if (g > 255) g = 255;
    if (b < 0) b = 0; else if (b > 255) b = 255;
    return alpha | (cast(uint)r << 16) | (cast(uint)g << 8) | cast(uint)b;
}

/// Recursively removes and destroys a widget and its children prior to GC shutdown.
void destroyWidgetTree(Widget w) {
    if (w is null) return;
    Widget[] children;
    foreach (i; 0 .. w.childCount) {
        if (auto c = w.child(i)) {
            children ~= c;
        }
    }
    w.removeAllChildren(false);
    foreach_reverse (c; children) {
        destroyWidgetTree(c);
    }
    destroy(w);
}

/// Lightweight flat FL Studio 2026 vector button with exact DPI-scaled font and row height.
class FlCompactButton : Button {
    int baseFontPx = 11;
    int baseHeightPx = 22;
    bool centered = true;
    bool bold = false;
    float cornerRadius = 0.0f;
    uint customFillColor = COLOR_TRANSPARENT;
    uint customBorderColor = COLOR_TRANSPARENT;
    uint customTextColor = COLOR_TRANSPARENT;
    private uint fallbackBg_;
    private uint fallbackHover_;
    private uint fallbackBorder_;
    private uint fallbackFg_;

    this(string id, dstring label, const ref FlPalette pal,
         int baseFontPx = 11, int baseHeightPx = 22, bool centered = true, bool bold = false) {
        super(id, label);
        this.baseFontPx = baseFontPx;
        this.baseHeightPx = baseHeightPx;
        this.centered = centered;
        this.bold = bold;
        styleId = null;
        backgroundImageId = null;
        backgroundColor = COLOR_TRANSPARENT;
        minWidth = 0;
        minHeight = 0;
        focusable = false;
        updatePalette(pal);
        margins = Rect(dp(1), 0, dp(1), 0);
        padding = Rect(0, 0, 0, 0);
    }

    void updatePalette(const ref FlPalette pal) {
        fallbackBg_ = pal.bgControl;
        fallbackHover_ = pal.bgControlHover;
        fallbackBorder_ = COLOR_TRANSPARENT;
        fallbackFg_ = pal.fgText;
    }

    override void measure(int parentWidth, int parentHeight) {
        auto f = getDawFont(baseFontPx, safeWidgetFont(this), bold ? FontWeight.Bold : FontWeight.Normal);
        int textW = dp(20);
        if (!f.isNull && text.length > 0) {
            textW = f.textSize(text).x;
        }
        const int h = dp(baseHeightPx);
        const int w = textW + dp(12);
        measuredContent(parentWidth, parentHeight, w, h);
    }

    override void onDraw(DrawBuf buf) {
        if (visibility != Visibility.Visible) return;
        const bool hovered = (state & State.Hovered) != 0;
        const bool pressed = (state & State.Pressed) != 0;
        uint bg = (customFillColor != COLOR_TRANSPARENT) ? customFillColor : fallbackBg_;
        if (pressed) {
            bg = adjustColorBrightness(bg, -18);
        } else if (hovered) {
            bg = (customFillColor != COLOR_TRANSPARENT)
                ? adjustColorBrightness(bg, 16) : fallbackHover_;
        }
        const uint brd = (customBorderColor != COLOR_TRANSPARENT) ? customBorderColor : fallbackBorder_;
        const uint fg = (customTextColor != COLOR_TRANSPARENT) ? customTextColor : fallbackFg_;
        drawCard(buf, _pos, bg, brd, cornerRadius);
        auto f = getDawFont(baseFontPx, safeWidgetFont(this), bold ? FontWeight.Bold : FontWeight.Normal);
        if (centered) {
            drawCenteredText(buf, f, _pos, text, fg);
        } else {
            drawVCenteredText(buf, f, _pos.left + dp(6), _pos, text, fg);
        }
    }
}

/// Flattens `dlangui` built-in `currentTheme` widget styles (menus, edit boxes, scrollbars, fallback buttons)
/// so no skeuomorphic 3D or rounded 9-patch textures remain anywhere in the UI.
void applyFlatThemeOverrides(const ref FlPalette pal) {
    import dlangui.widgets.styles : currentTheme, STYLE_BUTTON, STYLE_BUTTON_NOMARGINS,
        STYLE_EDIT_BOX, STYLE_EDIT_LINE, STYLE_LIST_ITEM, STYLE_MAIN_MENU,
        STYLE_MAIN_MENU_ITEM, STYLE_MENU_ITEM, STYLE_POPUP_MENU, STYLE_SCROLLBAR,
        STYLE_SCROLLBAR_BUTTON, STYLE_SCROLLBAR_BUTTON_TRANSPARENT;

    if (currentTheme is null) return;
    if (activeUiFontFace_.length > 0) {
        currentTheme.fontFace = activeUiFontFace_;
    }

    foreach (sid; [STYLE_BUTTON, STYLE_BUTTON_NOMARGINS]) {
        auto st = currentTheme.createSubstyle(sid);
        st.backgroundImageId = null;
        st.backgroundColor = pal.bgControl;
        st.textColor = pal.fgText;
        st.getOrCreateState(State.Hovered, State.Hovered).backgroundColor = pal.bgControlHover;
        st.getOrCreateState(State.Pressed, State.Pressed).backgroundColor = adjustColorBrightness(pal.bgControl, -16);
    }
    foreach (sid; [STYLE_EDIT_LINE, STYLE_EDIT_BOX]) {
        auto st = currentTheme.createSubstyle(sid);
        st.backgroundImageId = null;
        st.backgroundColor = pal.bgLcd;
        st.textColor = pal.fgText;
        st.padding = Rect(dp(6), dp(3), dp(6), dp(3));
    }
    auto pop = currentTheme.createSubstyle(STYLE_POPUP_MENU);
    pop.backgroundImageId = null;
    pop.backgroundColor = pal.bgWindow;
    pop.padding = Rect(dp(2), dp(2), dp(2), dp(2));

    auto mm = currentTheme.createSubstyle(STYLE_MAIN_MENU);
    mm.backgroundImageId = null;
    mm.backgroundColor = pal.bgWindowHeader;

    foreach (sid; [STYLE_MENU_ITEM, STYLE_MAIN_MENU_ITEM, STYLE_LIST_ITEM]) {
        auto st = currentTheme.createSubstyle(sid);
        st.backgroundImageId = null;
        st.backgroundColor = COLOR_TRANSPARENT;
        st.textColor = pal.fgText;
        st.padding = Rect(dp(8), dp(4), dp(8), dp(4));
        st.getOrCreateState(State.Hovered, State.Hovered).backgroundColor = pal.bgControlHover;
        st.getOrCreateState(State.Selected, State.Selected).backgroundColor = pal.bgControlHover;
    }

    auto sb = currentTheme.createSubstyle(STYLE_SCROLLBAR);
    sb.backgroundImageId = null;
    sb.backgroundColor = pal.bgPanelAlt;
    foreach (sid; [STYLE_SCROLLBAR_BUTTON, STYLE_SCROLLBAR_BUTTON_TRANSPARENT]) {
        auto st = currentTheme.createSubstyle(sid);
        st.backgroundImageId = null;
        st.backgroundColor = pal.bgControl;
        st.getOrCreateState(State.Hovered, State.Hovered).backgroundColor = pal.bgControlHover;
    }
}

unittest {
    import dlangui.graphics.drawbuf : ColorDrawBuf;
    import dlangui.core.types : overrideScreenDPI;

    // Verify DPI scaling helper across 1x (96 DPI) and 2x (192 DPI)
    overrideScreenDPI = 96;
    assert(dp(12) == 12);
    assert(dp(-4) == -4);
    assert(dpf(1.5f) == 1.5f);
    overrideScreenDPI = 192;
    assert(dp(12) == 24);
    assert(dp(-4) == -8);
    assert(dpf(1.5f) == 3.0f);
    overrideScreenDPI = 0;

    const pal = getFlPalette(FlThemeId.DEFAULT_DARK);
    assert(pal.accentOrange == 0x00FF8F2D);
    assert(getFlPalette(FlThemeId.ULTRAMARINE).name == "Ultramarine");

    // Verify adjustColorBrightness clamps RGB without overflowing into adjacent bytes or alpha
    const uint clampedBright = adjustColorBrightness(0x00FF8F2D, 0x20);
    assert((clampedBright & 0xFF000000) == 0);
    assert(((clampedBright >> 16) & 0xFF) == 0xFF);
    const uint clampedDark = adjustColorBrightness(0x00050A02, -16);
    assert(clampedDark == 0x00000000);
    assert(adjustColorBrightness(0x80102030, 10) == 0x801A2A3A);

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
    drawCard(buf, Rect(10, 10, 100, 80), pal.bgPanel, pal.borderSubtle, 0.0f);
    assert(buf.scanLine(10)[10] == pal.borderSubtle, "Zero-radius card corners must be sharp rectangular");
    assert(buf.scanLine(40)[50] == pal.bgPanel);

    // Slightly rounded card (`radius = 2.5f`) clips outer corner pixels and applies faint edge gradient
    buf.fill(pal.bgDesktop);
    drawCard(buf, Rect(10, 10, 100, 80), pal.bgPanel, COLOR_TRANSPARENT, 2.5f);
    assert(buf.scanLine(10)[10] == pal.bgDesktop, "Rounded corner outer pixel should be transparent/unfilled");
    assert(buf.scanLine(10)[50] == adjustColorBrightness(pal.bgPanel, 10), "Top edge should have faint highlight");
    assert(buf.scanLine(40)[50] == pal.bgPanel, "Center interior should stay flat fillColor");
    assert(buf.scanLine(79)[50] == adjustColorBrightness(pal.bgPanel, -10), "Bottom edge should have faint shade");

    drawRotaryKnob(buf, 50, 50, 14, 0.75f, pal.bgLcd, pal.accentOrange, pal.bgControl, pal.borderSubtle);
    drawLevelMeter(buf, Rect(85, 10, 110, 110), 1.0f, 0.0f, pal, 14);

    // Exercise centered text helpers and FlCompactButton across 1x (96 DPI) and 2x (192 DPI)
    import dlangui.graphics.fonts : Font, Glyph;

    static class MockGlyphFont : Font {
        Glyph _g;
        int _sz;
        this(int sz) {
            _sz = sz;
            _g.widthPixels = 7;
            _g.widthScaled = 7 << 6;
            _g.blackBoxX = 6;
            _g.blackBoxY = 10;
            _g.originY = 10;
            _g.glyph = new ubyte[](_g.blackBoxX * _g.blackBoxY);
            _g.glyph[] = 0xFF;
        }
        override @property int size() { return _sz; }
        override @property int height() { return _sz + 2; }
        override @property int weight() { return 400; }
        override @property int baseline() { return _sz - 1; }
        override @property bool italic() { return false; }
        override @property string face() { return "mock"; }
        override @property FontFamily family() { return FontFamily.SansSerif; }
        override @property bool isNull() { return false; }
        override Glyph* getCharGlyph(dchar ch, bool withImage = true) { return &_g; }
        override void checkpoint() {}
        override void cleanup() {}
        override void clearGlyphCache() {}
    }

    auto mockFnt = FontRef(new MockGlyphFont(11));
    auto textBuf = new ColorDrawBuf(200, 100);
    textBuf.fill(pal.bgDesktop);
    drawCenteredText(textBuf, mockFnt, Rect(10, 10, 110, 40), "PLAY"d, pal.accentOrange);
    drawVCenteredText(textBuf, mockFnt, 12, Rect(10, 45, 110, 75), "BAR"d, pal.fgText);
    drawRightVCenteredText(textBuf, mockFnt, Rect(10, 45, 110, 75), "120"d, pal.accentCyan, 4);

    import dlangui.widgets.styles : createDefaultTheme, currentTheme;
    const bool hadTheme = currentTheme !is null;
    if (!hadTheme) {
        currentTheme = createDefaultTheme();
    }

    FlCompactButton compactBtn;
    const bool hadFontMgr = FontManager.instance !is null;
    scope (exit) {
        overrideScreenDPI = 0;
        destroyWidgetTree(compactBtn);
        compactBtn = null;
        if (!hadFontMgr) FontManager.instance = null;
        if (!hadTheme) currentTheme = null;
    }
    compactBtn = new FlCompactButton("cmp_btn", "FILE"d, pal, 11, 22, true, false);
    // Reproduces headless crash if Widget.font was read directly while FontManager.instance is null
    overrideScreenDPI = 96;
    compactBtn.measure(200, 100);
    assert(compactBtn.measuredHeight == 22);
    const int w1 = compactBtn.measuredWidth;
    overrideScreenDPI = 192;
    compactBtn.measure(400, 200);
    assert(compactBtn.measuredHeight == 44);
    assert(compactBtn.measuredWidth >= w1);
    compactBtn.layout(Rect(0, 0, compactBtn.measuredWidth, compactBtn.measuredHeight));
    compactBtn.onDraw(textBuf);

    static class MockFontMgr : FontManager {
        FontRef _f;
        this(FontRef f) { _f = f; }
        override ref FontRef getFont(int size, int weight, bool italic, FontFamily family, string face) {
            return _f;
        }
        override void checkpoint() {}
        override void cleanup() {}
    }

    if (!hadFontMgr) {
        FontManager.instance = new MockFontMgr(mockFnt);
    }
    overrideScreenDPI = 96;
    compactBtn.measure(200, 100);
    assert(compactBtn.measuredHeight == 22);
    assert(compactBtn.measuredWidth == 42);
    compactBtn.onDraw(textBuf);

    // Verify flat theme overrides strip 3D 9-patch textures on built-in styles
    import dlangui.widgets.styles : STYLE_EDIT_LINE, STYLE_POPUP_MENU, STYLE_SCROLLBAR_BUTTON;
    applyFlatThemeOverrides(pal);
    assert(currentTheme.createSubstyle(STYLE_EDIT_LINE).backgroundImageId is null);
    assert(currentTheme.createSubstyle(STYLE_EDIT_LINE).backgroundColor == pal.bgLcd);
    assert(currentTheme.createSubstyle(STYLE_POPUP_MENU).backgroundImageId is null);
    assert(currentTheme.createSubstyle(STYLE_SCROLLBAR_BUTTON).backgroundImageId is null);
}
