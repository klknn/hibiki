module gui_d.theme;

import core.math : fabs;
import core.stdc.math : atan2f, ceilf, cosf, floorf, fmaxf, fminf, log10f, sinf, sqrtf;
import dlangui.core.config : ENABLE_FREETYPE;
import dlangui.core.types : BASE_DPI, Point, Rect, SCREEN_DPI;
import dlangui.graphics.colors : COLOR_TRANSPARENT;
import dlangui.graphics.drawbuf : DrawBuf;
import dlangui.graphics.fonts : FontFamily, FontManager, FontRef, FontWeight, HintingMode;
import dlangui.widgets.controls : Button;
import dlangui.widgets.widget : State, Visibility, Widget;
import std.file : exists, getSize, mkdirRecurse, tempDir, thisExePath, write;
import std.format : format;
import std.path : buildPath, dirName;
import std.process : environment;

/// Free MS-Gothic-inspired open font unified across the entire UI (Morisawa BIZ UDPGothic, SIL OFL 1.1).
enum string DAW_UI_FONT_FACE = "BIZ UDPGothic";
enum string DAW_MONO_FONT_FACE = DAW_UI_FONT_FACE;

private immutable ubyte[] EMBEDDED_BIZ_UDP_REGULAR = cast(immutable(ubyte)[]) import("BIZUDPGothic-Regular.ttf");
private immutable ubyte[] EMBEDDED_BIZ_UDP_BOLD = cast(immutable(ubyte)[]) import("BIZUDPGothic-Bold.ttf");

private __gshared string activeUiFontFace_ = null;
private __gshared string activeMonoFontFace_ = null;

/// Searches Bazel runfiles and working tree for bundled `BIZ UDPGothic` TTF fonts.
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
        if (exists(buildPath(dir, "BIZUDPGothic-Regular.ttf")) &&
            exists(buildPath(dir, "BIZUDPGothic-Bold.ttf"))) {
            return dir;
        }
    }
    return "";
}

/// Ensures embedded `BIZ UDPGothic` TTFs exist on disk for standalone OS-independent loading.
string ensureEmbeddedDawFontDirectory(string targetDir = null) {
    const string dir = targetDir.length > 0 ? targetDir : buildPath(tempDir(), "hibiki_daw_fonts");
    try {
        mkdirRecurse(dir);
        static void syncFile(string path, immutable(ubyte)[] data) {
            if (!exists(path) || getSize(path) != data.length) {
                write(path, data);
            }
        }
        syncFile(buildPath(dir, "BIZUDPGothic-Regular.ttf"), EMBEDDED_BIZ_UDP_REGULAR);
        syncFile(buildPath(dir, "BIZUDPGothic-Bold.ttf"), EMBEDDED_BIZ_UDP_BOLD);
        return dir;
    } catch (Exception) {
        return "";
    }
}

/// Registers bundled `BIZ UDPGothic` Regular/Bold fonts across both UI and numeric faces with crisp light hinting.
bool registerBundledDawFonts() {
    static if (ENABLE_FREETYPE) {
        import dlangui.graphics.ftfonts : FreeTypeFontManager;
        FontManager.hintingMode = HintingMode.Light;
        FontManager.minAnitialiasedFontSize = 0;
        auto ft = cast(FreeTypeFontManager)FontManager.instance;
        if (ft is null) return false;
        string dir = findDawFontDirectory();
        if (dir.length == 0) {
            dir = ensureEmbeddedDawFontDirectory();
        }
        if (dir.length == 0) return false;

        const string regPath = buildPath(dir, "BIZUDPGothic-Regular.ttf");
        const string boldPath = buildPath(dir, "BIZUDPGothic-Bold.ttf");
        bool ok = true;
        ok &= ft.registerFont(regPath, FontFamily.SansSerif, DAW_UI_FONT_FACE, false, FontWeight.Normal, true);
        ok &= ft.registerFont(boldPath, FontFamily.SansSerif, DAW_UI_FONT_FACE, false, FontWeight.Bold, true);
        ok &= ft.registerFont(regPath, FontFamily.MonoSpace, DAW_MONO_FONT_FACE, false, FontWeight.Normal, true);
        ok &= ft.registerFont(boldPath, FontFamily.MonoSpace, DAW_MONO_FONT_FACE, false, FontWeight.Bold, true);
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
    ROYAL_VIOLET,
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
                0x00242931, // bgToolbarWell
                0x002A2F37, // bgWindow
                0x00242930, // bgWindowHeader
                0x00343B45, // bgWindowHeaderActive
                0x00313740, // bgPanel
                0x00272C34, // bgPanelAlt
                0x003A414C, // bgControl
                0x00464E5B, // bgControlHover
                0x00242931, // bgLcd (harmonized with toolbar/panel slate)
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
                0x001A2030,
                0x00202636,
                0x001A2030,
                0x002B344D,
                0x00262D40,
                0x001D2333,
                0x00313A52,
                0x003E4966,
                0x001A2030,
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
                0x00192321,
                0x001E2826,
                0x00182220,
                0x00273633,
                0x0024302E,
                0x001B2523,
                0x002D3C39,
                0x00384A46,
                0x00192321,
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
                0x0025211D,
                0x002C2722,
                0x0025211D,
                0x0038312A,
                0x00332D27,
                0x0029241F,
                0x003F3831,
                0x004C433B,
                0x0025211D,
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
        case FlThemeId.ROYAL_VIOLET:
            return FlPalette(
                "Royal Violet",
                0x00171421,
                0x00252034,
                0x001D1929,
                0x00221D30,
                0x001C1828,
                0x002E2742,
                0x0029233A,
                0x001F1A2C,
                0x00342C4A,
                0x0041385C,
                0x001D1929,
                0x003E3557,
                0x00110E18,
                0x00B47CFF,
                0x00EDEAF6,
                0x009B93B3,
                0x00655E7A,
                0x00FF944D,
                0x004FE08A,
                0x00F25574,
                0x0052D6EB,
                0x00FFC857,
                0x00B47CFF,
                0x006F8EFF,
                0x0037304D,
                0x004F3957,
                0x00E4DCF5,
                0x00B47CFF,
                0x00181422,
                0x00493B70,
                0x0033566B,
                0x006B4378,
                0x00A88BFF,
                0x805C5275
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

/// Draws an FL Studio flush track-header / mixer-strip slab with zero external gap:
/// top-right and bottom-left corners are subtly rounded and a soft vertical highlight/shadow
/// gradient is applied across the corner depth so adjacent rows/strips separate cleanly without dividers.
void drawBeveledTrackCard(DrawBuf buf, Rect rc, uint fillColor, uint cornerBgColor, float radius = 3.0f) {
    if (rc.width <= 0 || rc.height <= 0) return;
    int cr = cast(int)(dpf(radius) + 0.5f);
    if (cr < 1) cr = 1;
    if (cr * 3 > rc.height) cr = rc.height / 3;
    if (cr * 3 > rc.width) cr = rc.width / 3;
    if (cr <= 0) {
        buf.fillRect(rc, fillColor);
        return;
    }

    buf.fillRect(Rect(rc.left, rc.top + cr, rc.right, rc.bottom - cr), fillColor);

    const float crF = cast(float)cr;
    foreach (dy; 0 .. cr) {
        const float distFromInner = crF - cast(float)dy - 0.5f;
        const float chord = (distFromInner < crF) ? sqrtf(crF * crF - distFromInner * distFromInner) : crF;
        int cut = cr - cast(int)(chord + 0.5f);
        if (cut < 0) cut = 0;

        // Top bevel: rounded at top-right corner
        const int topBright = 11 - (8 * dy) / cr;
        if (cornerBgColor != COLOR_TRANSPARENT && cut > 0) {
            buf.fillRect(Rect(rc.right - cut, rc.top + dy, rc.right, rc.top + dy + 1), cornerBgColor);
        }
        buf.fillRect(Rect(rc.left, rc.top + dy, rc.right - cut, rc.top + dy + 1),
                     adjustColorBrightness(fillColor, topBright));

        // Bottom bevel: rounded at bottom-left corner
        const int botRow = rc.bottom - 1 - dy;
        const int botDark = -(11 - (8 * dy) / cr);
        if (cornerBgColor != COLOR_TRANSPARENT && cut > 0) {
            buf.fillRect(Rect(rc.left, botRow, rc.left + cut, botRow + 1), cornerBgColor);
        }
        buf.fillRect(Rect(rc.left + cut, botRow, rc.right, botRow + 1),
                     adjustColorBrightness(fillColor, botDark));
    }
}

/// Modulates `color`'s opacity by `cov` in `[0, 1]` using `dlangui` inverted-alpha ARGB (`0x00` = opaque, `0xFF` = transparent).
uint colorWithCoverage(uint color, float cov) pure nothrow @safe {
    if (color == COLOR_TRANSPARENT || cov <= 0.004f) return COLOR_TRANSPARENT;
    if (cov >= 0.996f) return color;
    const uint baseAlpha = (color >> 24) & 0xFF;
    const float baseOpacity = 1.0f - cast(float)baseAlpha / 255.0f;
    float outAlpha = 255.0f * (1.0f - baseOpacity * cov);
    if (outAlpha < 0.0f) outAlpha = 0.0f;
    if (outAlpha > 255.0f) outAlpha = 255.0f;
    const uint a = cast(uint)(outAlpha + 0.5f);
    if (a >= 255) return COLOR_TRANSPARENT;
    return (a << 24) | (color & 0x00FFFFFF);
}

/// Flushes a contiguous opaque horizontal pixel run `[runStart, px)` on row `py` if active.
private void flushOpaqueRun(DrawBuf buf, int py, ref int runStart, int px, uint color) {
    if (runStart >= 0) {
        if (px > runStart) {
            buf.fillRect(Rect(runStart, py, px, py + 1), color);
        }
        runStart = -1;
    }
}

/// Emits one pixel's coverage onto row `py`, coalescing contiguous opaque pixels into wider `fillRect` spans.
private void emitCoveragePixel(DrawBuf buf, int px, int py, float cov, uint color, ref int opaqueRunStart) {
    if (cov >= 0.996f) {
        if (opaqueRunStart < 0) opaqueRunStart = px;
        return;
    }
    flushOpaqueRun(buf, py, opaqueRunStart, px, color);
    const uint c = colorWithCoverage(color, cov);
    if (c != COLOR_TRANSPARENT) {
        buf.fillRect(Rect(px, py, px + 1, py + 1), c);
    }
}

/// Draws an anti-aliased thickness-scaled SDF capsule line using `DrawBuf.fillRect`
/// (works identically across headless `ColorDrawBuf` and `GLDrawBuf`).
void drawThickLine(DrawBuf buf, float x0, float y0, float x1, float y1, float width, uint color) {
    if (color == COLOR_TRANSPARENT || width <= 0.0f) return;
    const float dx = x1 - x0;
    const float dy = y1 - y0;
    const float len2 = dx * dx + dy * dy;

    // Fast path for exact crisp axis-aligned hairlines/bars
    if (len2 < 1e-4f) {
        drawFilledCircle(buf, x0, y0, width * 0.5f, color);
        return;
    }

    const float halfW = fmaxf(0.45f, width * 0.5f);
    const float reach = halfW + 0.65f;
    const float invLen2 = 1.0f / len2;

    const int py0 = cast(int)floorf(fminf(y0, y1) - reach);
    const int py1 = cast(int)ceilf(fmaxf(y0, y1) + reach) + 1;
    const float xMinAll = fminf(x0, x1) - reach;
    const float xMaxAll = fmaxf(x0, x1) + reach;
    const float absDy = fabs(dy);

    foreach (py; py0 .. py1) {
        const float sy = cast(float)py + 0.5f;
        float rowMinX = xMinAll;
        float rowMaxX = xMaxAll;
        // Narrow horizontal scan window along tilted lines while keeping rounded endcaps intact
        if (absDy >= 0.5f) {
            float tA = (sy - reach - y0) / dy;
            float tB = (sy + reach - y0) / dy;
            if (tA > tB) {
                const float tmp = tA;
                tA = tB;
                tB = tmp;
            }
            tA = fmaxf(0.0f, fminf(1.0f, tA));
            tB = fmaxf(0.0f, fminf(1.0f, tB));
            const float xA = x0 + tA * dx;
            const float xB = x0 + tB * dx;
            rowMinX = fmaxf(rowMinX, fminf(xA, xB) - reach);
            rowMaxX = fminf(rowMaxX, fmaxf(xA, xB) + reach);
        }
        const int px0 = cast(int)floorf(rowMinX);
        const int px1 = cast(int)ceilf(rowMaxX) + 1;
        if (px1 <= px0) continue;

        int runStart = -1;
        foreach (px; px0 .. px1) {
            const float sx = cast(float)px + 0.5f;
            float t = ((sx - x0) * dx + (sy - y0) * dy) * invLen2;
            if (t < 0.0f) t = 0.0f;
            else if (t > 1.0f) t = 1.0f;
            const float qx = sx - (x0 + t * dx);
            const float qy = sy - (y0 + t * dy);
            const float dist = sqrtf(qx * qx + qy * qy);
            const float cov = fminf(1.0f, fmaxf(0.0f, halfW + 0.5f - dist));
            emitCoveragePixel(buf, px, py, cov, color, runStart);
        }
        flushOpaqueRun(buf, py, runStart, px1, color);
    }
}

/// Evaluates clamped Catmull-Rom cubic spline interpolation over `wf` at continuous index `u`.
private float sampleWaveformCatmullRom(const(float)[] wf, float u) pure nothrow @safe {
    if (wf.length == 0) return 0.0f;
    if (wf.length == 1) return wf[0];
    const int n = cast(int)wf.length;
    int i1 = cast(int)floorf(u);
    if (i1 < 0) i1 = 0;
    if (i1 >= n - 1) i1 = n - 2;
    const float t = u - cast(float)i1;
    const float p0 = wf[(i1 > 0) ? (i1 - 1) : 0];
    const float p1 = wf[i1];
    const float p2 = wf[i1 + 1];
    const float p3 = wf[(i1 + 2 < n) ? (i1 + 2) : (n - 1)];
    const float t2 = t * t;
    const float t3 = t2 * t;
    float v = 0.5f * ((2.0f * p1) +
                      (-p0 + p2) * t +
                      (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t2 +
                      (-p0 + 3.0f * p1 - 3.0f * p2 + p3) * t3);
    if (v > 1.0f) v = 1.0f;
    if (v < -1.0f) v = -1.0f;
    return v;
}

/// Renders a smooth, anti-aliased high-resolution audio waveform with peak envelope, inner RMS core,
/// and sub-pixel edge anti-aliasing across `rc` (handles both signed `[-1, 1]` and unsigned `[0, 1]` data).
void drawDetailedWaveform(
    DrawBuf buf,
    Rect rc,
    const(float)[] wf,
    uint coreColor,
    uint peakBodyColor = COLOR_TRANSPARENT,
    uint centerLineColor = COLOR_TRANSPARENT
) {
    if (rc.width <= 1 || rc.height <= 2 || wf.length == 0) return;

    const float midY = rc.top + cast(float)rc.height * 0.5f;
    const float maxHalfH = cast(float)rc.height * 0.5f - dpf(1.0f);
    if (maxHalfH < 1.0f) return;

    if (centerLineColor != COLOR_TRANSPARENT) {
        const int cy = cast(int)(midY + 0.5f);
        buf.fillRect(Rect(rc.left, cy, rc.right, cy + 1), centerLineColor);
    }

    bool hasNegative = false;
    foreach (s; wf) {
        if (s < -0.001f) {
            hasNegative = true;
            break;
        }
    }

    const uint rgb = coreColor & 0x00FFFFFF;
    const uint baseAlpha = (coreColor >> 24) & 0xFF;
    const float baseCov = 1.0f - cast(float)baseAlpha / 255.0f;
    const uint bodyCol = (peakBodyColor != COLOR_TRANSPARENT)
        ? peakBodyColor
        : (rgb | (cast(uint)((1.0f - baseCov * 0.42f) * 255.0f) << 24));
    const uint contourCol = rgb | (cast(uint)((1.0f - baseCov * 0.85f) * 255.0f) << 24);

    uint aaPixelColor(float frac) {
        if (frac < 0.0f) frac = 0.0f;
        if (frac > 1.0f) frac = 1.0f;
        const float cov = baseCov * 0.72f * frac;
        const uint a = cast(uint)((1.0f - cov) * 255.0f + 0.5f);
        return rgb | ((a > 255 ? 255 : a) << 24);
    }

    const int w = rc.width;
    float prevTopY = midY;
    float prevBotY = midY;
    bool hasPrev = false;

    foreach (px; 0 .. w) {
        float posPeak = 0.0f;
        float negPeak = 0.0f;
        float rms = 0.0f;

        if (wf.length > cast(size_t)w) {
            const size_t i0 = (cast(size_t)px * wf.length) / cast(size_t)w;
            size_t i1 = (cast(size_t)(px + 1) * wf.length) / cast(size_t)w;
            if (i1 <= i0) i1 = i0 + 1;
            if (i1 > wf.length) i1 = wf.length;

            double sumSq = 0.0;
            foreach (i; i0 .. i1) {
                float s = wf[i];
                if (s > 1.0f) s = 1.0f;
                if (s < -1.0f) s = -1.0f;
                if (hasNegative) {
                    if (s > posPeak) posPeak = s;
                    if (-s > negPeak) negPeak = -s;
                } else {
                    const float a = fabs(s);
                    if (a > posPeak) posPeak = a;
                    if (a > negPeak) negPeak = a;
                }
                sumSq += cast(double)s * cast(double)s;
            }
            rms = sqrtf(cast(float)(sumSq / cast(double)(i1 - i0)));
            // Keep transient peaks visually crisp while preserving RMS contrast
            if (rms > posPeak && rms > negPeak) rms = posPeak > negPeak ? posPeak : negPeak;
        } else {
            // Smooth Catmull-Rom cubic interpolation across sub-sample columns
            const float denom = (w > 1) ? cast(float)(w - 1) : 1.0f;
            const float u = (cast(float)px / denom) * cast(float)(wf.length - 1);
            const float s0 = sampleWaveformCatmullRom(wf, u);
            const float sL = sampleWaveformCatmullRom(wf, u - 0.35f);
            const float sR = sampleWaveformCatmullRom(wf, u + 0.35f);
            if (hasNegative) {
                posPeak = fabs(s0) * 0.65f + (fabs(sL) > fabs(sR) ? fabs(sL) : fabs(sR)) * 0.35f;
                negPeak = posPeak * 0.96f;
                if (s0 > 0.0f) posPeak = (posPeak > s0) ? posPeak : s0;
                else negPeak = (negPeak > -s0) ? negPeak : -s0;
            } else {
                const float v = fabs(s0);
                posPeak = v;
                negPeak = v;
            }
            rms = ((posPeak > negPeak ? posPeak : negPeak)) * 0.56f;
        }

        if (posPeak > 1.0f) posPeak = 1.0f;
        if (negPeak > 1.0f) negPeak = 1.0f;
        if (rms > 1.0f) rms = 1.0f;

        float topF = midY - posPeak * maxHalfH;
        float botF = midY + negPeak * maxHalfH;
        if (botF - topF < 1.0f) {
            topF = midY - 0.5f;
            botF = midY + 0.5f;
        }

        const int x = rc.left + px;
        const int iy0 = cast(int)ceilf(topF);
        const int iy1 = cast(int)floorf(botF);

        // 1. Translucent peak envelope fill
        if (iy1 >= iy0) {
            buf.fillRect(Rect(x, iy0, x + 1, iy1 + 1), bodyCol);
        } else {
            const int cy = cast(int)(midY + 0.5f);
            buf.fillRect(Rect(x, cy, x + 1, cy + 1), bodyCol);
        }

        // 2. Smooth contour bridge between steep adjacent samples + anti-aliased tip pixels
        if (hasPrev) {
            const int cTop0 = cast(int)(prevTopY < topF ? prevTopY : topF);
            const int cTop1 = cast(int)(prevTopY > topF ? prevTopY : topF);
            if (cTop1 > cTop0) {
                buf.fillRect(Rect(x, cTop0, x + 1, cTop1 + 1), contourCol);
            }
            const int cBot0 = cast(int)(prevBotY < botF ? prevBotY : botF);
            const int cBot1 = cast(int)(prevBotY > botF ? prevBotY : botF);
            if (cBot1 > cBot0) {
                buf.fillRect(Rect(x, cBot0, x + 1, cBot1 + 1), contourCol);
            }
        }
        const float topFrac = cast(float)iy0 - topF;
        if (iy0 - 1 >= rc.top && topFrac > 0.04f) {
            buf.fillRect(Rect(x, iy0 - 1, x + 1, iy0), aaPixelColor(topFrac));
        }
        const float botFrac = botF - cast(float)iy1;
        if (iy1 + 1 < rc.bottom && botFrac > 0.04f) {
            buf.fillRect(Rect(x, iy1 + 1, x + 1, iy1 + 2), aaPixelColor(botFrac));
        }

        // 3. Crisp inner RMS core
        const float innerHalf = rms * maxHalfH * 0.78f;
        const int r0 = cast(int)(midY - innerHalf + 0.5f);
        const int r1 = cast(int)(midY + innerHalf + 0.5f);
        if (r1 > r0) {
            buf.fillRect(Rect(x, r0, x + 1, r1), coreColor);
        }

        prevTopY = topF;
        prevBotY = botF;
        hasPrev = true;
    }
}

/// Evaluates signed-distance coverage for an arc ring pixel `(dx, dy)` and emits it cleanly.
private void emitArcSpan(
    DrawBuf buf,
    int py,
    int px0,
    int px1,
    float cx,
    float cy,
    float radius,
    float halfW,
    bool fullRing,
    float a0,
    float aSpan,
    float e0x,
    float e0y,
    float e1x,
    float e1y,
    uint color
) {
    if (px1 <= px0) return;
    enum float TWO_PI = 6.283185307f;
    const float sy = cast(float)py + 0.5f;
    const float dy = sy - cy;
    int runStart = -1;
    foreach (px; px0 .. px1) {
        const float sx = cast(float)px + 0.5f;
        const float dx = sx - cx;
        const float r = sqrtf(dx * dx + dy * dy);
        float dist;
        if (fullRing) {
            dist = fabs(r - radius);
        } else {
            float rel = atan2f(dy, dx) - a0;
            while (rel < 0.0f) rel += TWO_PI;
            while (rel >= TWO_PI) rel -= TWO_PI;
            if (rel <= aSpan) {
                dist = fabs(r - radius);
            } else {
                const float dx0 = sx - e0x;
                const float dy0 = sy - e0y;
                const float dx1 = sx - e1x;
                const float dy1 = sy - e1y;
                const float d0 = sqrtf(dx0 * dx0 + dy0 * dy0);
                const float d1 = sqrtf(dx1 * dx1 + dy1 * dy1);
                dist = fminf(d0, d1);
            }
        }
        const float cov = fminf(1.0f, fmaxf(0.0f, halfW + 0.5f - dist));
        emitCoveragePixel(buf, px, py, cov, color, runStart);
    }
    flushOpaqueRun(buf, py, runStart, px1, color);
}

/// Draws an analytic SDF anti-aliased circular arc stroke with rounded endcaps between
/// `startDeg` and `endDeg` (in degrees, 0 = right, 90 = down).
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
    if (color == COLOR_TRANSPARENT || radius <= 0.3f || lineWidth <= 0.0f) return;
    const float sweep = endDeg - startDeg;
    if (fabs(sweep) < 0.05f) return;

    enum float DEG2RAD = 3.1415926535f / 180.0f;
    enum float TWO_PI = 6.283185307f;
    const bool fullRing = fabs(sweep) >= 359.5f;
    const float sDeg = sweep >= 0.0f ? startDeg : endDeg;
    const float eDeg = sweep >= 0.0f ? endDeg : startDeg;
    float a0 = sDeg * DEG2RAD;
    while (a0 < 0.0f) a0 += TWO_PI;
    while (a0 >= TWO_PI) a0 -= TWO_PI;
    const float aSpan = (eDeg - sDeg) * DEG2RAD;
    const float e0x = cx + cosf(a0) * radius;
    const float e0y = cy + sinf(a0) * radius;
    const float e1x = cx + cosf(a0 + aSpan) * radius;
    const float e1y = cy + sinf(a0 + aSpan) * radius;

    const float halfW = fmaxf(0.45f, lineWidth * 0.5f);
    const float rOut = radius + halfW + 0.65f;
    const float rInSkip = radius - halfW - 0.65f;
    const int py0 = cast(int)floorf(cy - rOut);
    const int py1 = cast(int)ceilf(cy + rOut) + 1;

    foreach (py; py0 .. py1) {
        const float dy = (cast(float)py + 0.5f) - cy;
        const float remOut = rOut * rOut - dy * dy;
        if (remOut <= 0.0f) continue;
        const float dxOut = sqrtf(remOut);
        const int pxMin = cast(int)floorf(cx - dxOut);
        const int pxMax = cast(int)ceilf(cx + dxOut) + 1;

        if (rInSkip > 0.5f && fabs(dy) < rInSkip) {
            const float dxIn = sqrtf(rInSkip * rInSkip - dy * dy);
            const int holeL = cast(int)ceilf(cx - dxIn);
            const int holeR = cast(int)floorf(cx + dxIn);
            if (holeR > holeL + 1) {
                emitArcSpan(buf, py, pxMin, holeL + 1, cx, cy, radius, halfW,
                            fullRing, a0, aSpan, e0x, e0y, e1x, e1y, color);
                emitArcSpan(buf, py, holeR, pxMax, cx, cy, radius, halfW,
                            fullRing, a0, aSpan, e0x, e0y, e1x, e1y, color);
                continue;
            }
        }
        emitArcSpan(buf, py, pxMin, pxMax, cx, cy, radius, halfW,
                    fullRing, a0, aSpan, e0x, e0y, e1x, e1y, color);
    }
}

/// Draws an analytic SDF anti-aliased filled circle (universal across `ColorDrawBuf` & `GLDrawBuf`).
void drawFilledCircle(DrawBuf buf, float cx, float cy, float radius, uint fillColor, uint borderColor = COLOR_TRANSPARENT) {
    if (radius <= 0.3f) return;

    if (fillColor != COLOR_TRANSPARENT) {
        const float rOut = radius + 0.65f;
        const float rSolid = radius - 0.6f;
        const int py0 = cast(int)floorf(cy - rOut);
        const int py1 = cast(int)ceilf(cy + rOut) + 1;

        foreach (py; py0 .. py1) {
            const float dy = (cast(float)py + 0.5f) - cy;
            const float remOut = rOut * rOut - dy * dy;
            if (remOut <= 0.0f) continue;
            const float dxOut = sqrtf(remOut);
            const int pxMin = cast(int)floorf(cx - dxOut);
            const int pxMax = cast(int)ceilf(cx + dxOut) + 1;

            int solidL = pxMax;
            int solidR = pxMax;
            if (rSolid > 0.0f && fabs(dy) < rSolid) {
                const float dxSolid = sqrtf(rSolid * rSolid - dy * dy);
                solidL = cast(int)ceilf(cx - dxSolid);
                solidR = cast(int)floorf(cx + dxSolid);
                if (solidR > solidL) {
                    buf.fillRect(Rect(solidL, py, solidR, py + 1), fillColor);
                } else {
                    solidL = pxMax;
                    solidR = pxMax;
                }
            }

            foreach (px; pxMin .. (solidL < pxMax ? solidL : pxMax)) {
                const float dx = (cast(float)px + 0.5f) - cx;
                const float cov = fminf(1.0f, fmaxf(0.0f, radius + 0.5f - sqrtf(dx * dx + dy * dy)));
                const uint c = colorWithCoverage(fillColor, cov);
                if (c != COLOR_TRANSPARENT) buf.fillRect(Rect(px, py, px + 1, py + 1), c);
            }
            if (solidR < pxMax) {
                foreach (px; solidR .. pxMax) {
                    const float dx = (cast(float)px + 0.5f) - cx;
                    const float cov = fminf(1.0f, fmaxf(0.0f, radius + 0.5f - sqrtf(dx * dx + dy * dy)));
                    const uint c = colorWithCoverage(fillColor, cov);
                    if (c != COLOR_TRANSPARENT) buf.fillRect(Rect(px, py, px + 1, py + 1), c);
                }
            }
        }
    }
    if (borderColor != COLOR_TRANSPARENT) {
        drawOpenArc(buf, cx, cy, radius, 0.0f, 360.0f, 1.0f, borderColor);
    }
}

/// Draws an FL Studio 2026 anti-aliased rotary knob with outer value arc, bevel cap, center pivot, and needle.
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

    const float fcx = cast(float)cx + 0.5f;
    const float fcy = cast(float)cy + 0.5f;
    const float arcR = cast(float)radius;
    const float trackW = dpf(1.7f);
    const float arcW = dpf(2.2f);
    drawOpenArc(buf, fcx, fcy, arcR, START_DEG, END_DEG, trackW, trackColor);

    if (bipolar) {
        // Subtle 12 o'clock zero detent tick on outer ring
        const float tickR0 = arcR + dpf(0.8f);
        const float tickR1 = arcR + dpf(2.2f);
        drawThickLine(buf, fcx, fcy - tickR0, fcx, fcy - tickR1, dpf(1.1f), adjustColorBrightness(trackColor, 26));
        if (fabs(v - 0.5f) > 0.01f) {
            if (v >= 0.5f) {
                drawOpenArc(buf, fcx, fcy, arcR, CENTER_DEG, valDeg, arcW, arcColor);
            } else {
                drawOpenArc(buf, fcx, fcy, arcR, valDeg, CENTER_DEG, arcW, arcColor);
            }
        }
    } else if (v > 0.01f) {
        drawOpenArc(buf, fcx, fcy, arcR, START_DEG, valDeg, arcW, arcColor);
    }

    const float gap = dpf(2.8f);
    const float capR = arcR > gap + 1.0f ? arcR - gap : arcR * 0.7f;
    drawFilledCircle(buf, fcx, fcy, capR, capColor, COLOR_TRANSPARENT);
    if (capR >= dpf(4.0f)) {
        const float rimR = capR - dpf(0.6f);
        drawOpenArc(buf, fcx, fcy, rimR, 200.0f, 340.0f, dpf(1.0f), adjustColorBrightness(capColor, 18));
        drawOpenArc(buf, fcx, fcy, rimR, 20.0f, 160.0f, dpf(1.0f), adjustColorBrightness(capColor, -14));
    }
    if (borderColor != COLOR_TRANSPARENT) {
        drawOpenArc(buf, fcx, fcy, capR, 0.0f, 360.0f, 0.9f, borderColor);
    }

    enum float DEG2RAD = 3.1415926535f / 180.0f;
    const float rad = valDeg * DEG2RAD;
    const float x0 = fcx + cosf(rad) * (capR * 0.22f);
    const float y0 = fcy + sinf(rad) * (capR * 0.22f);
    const float x1 = fcx + cosf(rad) * (capR - dpf(0.9f));
    const float y1 = fcy + sinf(rad) * (capR - dpf(0.9f));
    drawThickLine(buf, x0, y0, x1, y1, dpf(1.6f), arcColor);
    if (radius >= dp(9)) {
        drawFilledCircle(buf, fcx, fcy, dpf(1.2f), adjustColorBrightness(capColor, 20));
    }
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

/// Draws the classic FL Studio downward-pointing anti-aliased playhead triangle in the timeline ruler.
void drawPlayheadTriangle(DrawBuf buf, int cx, int topY, int halfW, int height, uint color) {
    if (height <= 0 || halfW <= 0 || color == COLOR_TRANSPARENT) return;
    const float fcx = cast(float)cx + 0.5f;
    foreach (dy; 0 .. height) {
        const float t = (cast(float)dy + 0.5f) / cast(float)height;
        const float span = cast(float)halfW * (1.0f - t);
        const float xLeft = fcx - span;
        const float xRight = fcx + span;
        const int ix0 = cast(int)floorf(xLeft);
        const int ix1 = cast(int)ceilf(xRight);
        const uint rowCol = (dy == 0) ? adjustColorBrightness(color, 18) : color;
        int runStart = -1;
        foreach (px; ix0 .. ix1) {
            const float cov = fminf(1.0f, fmaxf(0.0f, fminf(cast(float)(px + 1), xRight) - fmaxf(cast(float)px, xLeft)));
            emitCoveragePixel(buf, px, topY + dy, cov, rowCol, runStart);
        }
        flushOpaqueRun(buf, topY + dy, runStart, ix1, rowCol);
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
        margins = Rect(0, 0, 0, 0);
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
    currentTheme.fontSize = dp(11);
    currentTheme.fontWeight = FontWeight.Normal;
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
    const violetPal = getFlPalette(FlThemeId.ROYAL_VIOLET);
    assert(violetPal.name == "Royal Violet");
    assert(violetPal.bgLcd == violetPal.bgToolbarWell);
    assert((violetPal.borderAccent & 0x00FFFFFF) == 0x00B47CFF);

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

    // Verify drawBeveledTrackCard rounds top-right and bottom-left corners with bevel gradient
    buf.fill(pal.bgDesktop);
    drawBeveledTrackCard(buf, Rect(10, 10, 110, 50), pal.bgPanel, pal.bgToolbarWell, 3.0f);
    assert(buf.scanLine(10)[10] == adjustColorBrightness(pal.bgPanel, 11), "Top-left bevel stays square & highlighted");
    assert(buf.scanLine(10)[109] == pal.bgToolbarWell, "Top-right corner should be rounded/recessed");
    assert(buf.scanLine(49)[10] == pal.bgToolbarWell, "Bottom-left corner should be rounded/recessed");
    assert(buf.scanLine(49)[109] == adjustColorBrightness(pal.bgPanel, -11), "Bottom-right bevel stays square & shaded");
    assert(buf.scanLine(30)[60] == pal.bgPanel, "Track card center interior stays flat");

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
    assert(compactBtn.measuredWidth == 40);
    compactBtn.onDraw(textBuf);

    // Verify flat theme overrides strip 3D 9-patch textures on built-in styles
    import dlangui.widgets.styles : STYLE_EDIT_LINE, STYLE_POPUP_MENU, STYLE_SCROLLBAR_BUTTON;
    applyFlatThemeOverrides(pal);
    assert(currentTheme.createSubstyle(STYLE_EDIT_LINE).backgroundImageId is null);
    assert(currentTheme.createSubstyle(STYLE_EDIT_LINE).backgroundColor == pal.bgLcd);
    assert(currentTheme.createSubstyle(STYLE_POPUP_MENU).backgroundImageId is null);
    assert(currentTheme.createSubstyle(STYLE_SCROLLBAR_BUTTON).backgroundImageId is null);

    // Verify bundled BIZ UDPGothic TTFs are embedded in the binary and extract cleanly
    assert(DAW_UI_FONT_FACE == DAW_MONO_FONT_FACE);
    import std.file : rmdirRecurse;
    const string testFontDir = buildPath(tempDir(), "hibiki_daw_fonts_unit_test");
    scope (exit) {
        if (exists(testFontDir)) rmdirRecurse(testFontDir);
    }
    const string extractedDir = ensureEmbeddedDawFontDirectory(testFontDir);
    assert(extractedDir == testFontDir);
    assert(exists(buildPath(testFontDir, "BIZUDPGothic-Regular.ttf")));
    assert(exists(buildPath(testFontDir, "BIZUDPGothic-Bold.ttf")));
    assert(getSize(buildPath(testFontDir, "BIZUDPGothic-Regular.ttf")) > 100_000);

    // Verify drawDetailedWaveform smoothly interpolates sparse waveforms & renders peak+RMS layers
    auto wfBuf = new ColorDrawBuf(160, 48);
    wfBuf.fill(pal.bgWindow);
    static immutable float[6] sparseWf = [0.1f, 0.9f, -0.7f, 0.5f, -0.8f, 0.2f];
    drawDetailedWaveform(wfBuf, Rect(4, 4, 156, 44), sparseWf[], pal.accentCyan,
                         (pal.accentCyan & 0x00FFFFFF) | 0x80000000, pal.borderSubtle);
    // Midpoint (~x=44) between 0.1 and 0.9 must interpolate rather than stay flat
    assert(wfBuf.scanLine(24)[44] != pal.bgWindow, "Expected smooth Catmull-Rom waveform body");

    // Verify SDF anti-aliasing on diagonal lines, arcs, circles, and playhead triangle
    assert(colorWithCoverage(0x00FF8F2D, 0.0f) == COLOR_TRANSPARENT);
    assert(colorWithCoverage(0x00FF8F2D, 1.0f) == 0x00FF8F2D);
    assert(((colorWithCoverage(0x00FF8F2D, 0.5f) >> 24) & 0xFF) > 100);

    auto aaBuf = new ColorDrawBuf(48, 48);
    aaBuf.fill(pal.bgLcd);
    drawThickLine(aaBuf, 6.0f, 6.0f, 40.0f, 38.0f, 2.0f, pal.accentOrange);
    int aaBlended = 0;
    int aaSolid = 0;
    foreach (y; 0 .. 48) {
        auto row = aaBuf.scanLine(y);
        foreach (x; 0 .. 48) {
            if (row[x] == pal.accentOrange) aaSolid++;
            else if (row[x] != pal.bgLcd) aaBlended++;
        }
    }
    assert(aaSolid > 10 && aaBlended > 10, "Expected anti-aliased intermediate edge pixels on diagonal line");

    aaBuf.fill(pal.bgLcd);
    drawFilledCircle(aaBuf, 24.5f, 24.5f, 10.0f, pal.accentCyan);
    assert(aaBuf.scanLine(24)[24] == pal.accentCyan, "Circle center must be opaque fill");
    assert(aaBuf.scanLine(2)[2] == pal.bgLcd, "Far corner must stay untouched");

    aaBuf.fill(pal.bgLcd);
    drawPlayheadTriangle(aaBuf, 24, 4, 6, 10, pal.accentOrange);
    assert(aaBuf.scanLine(6)[24] == pal.accentOrange, "Playhead interior must be opaque");
}
