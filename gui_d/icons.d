module gui_d.icons;

import core.stdc.math : ceilf, floorf;
import dlangui.core.types : Rect;
import dlangui.graphics.colors : COLOR_TRANSPARENT;
import dlangui.graphics.drawbuf : DrawBuf;
import gui_d.theme : dpf, drawFilledCircle, drawOpenArc, drawThickLine;
import std.ascii : isDigit, isWhite;
import std.conv : to;

/// Crisp SVG-path vector icon catalog replacing cryptic text abbreviations across FL Studio 2026 UI.
enum DawIcon {
    PAT_MODE,
    SONG_MODE,
    PLAY,
    STOP,
    RECORD,
    LOOP,
    METRONOME,
    COUNTDOWN,
    OVERDUB,
    TYPING_KB,
    SNAP_MAGNET,
    UNDO,
    REDO,
    CHEVRON_LEFT,
    CHEVRON_RIGHT,
    PLUS,
    MINUS,
    WIN_PLAYLIST,
    WIN_CHANNEL_RACK,
    WIN_PIANO_ROLL,
    WIN_BROWSER,
    WIN_MIXER,
    WIN_PLUGIN,
    TOOL_DRAW,
    TOOL_PAINT,
    TOOL_DELETE,
    TOOL_MUTE,
    TOOL_SLIP,
    TOOL_SLICE,
    TOOL_SELECT,
    TOOL_ZOOM,
    CLIP_PICKER,
    KIND_PATTERN,
    KIND_AUDIO,
    KIND_AUTOMATION,
    GHOST_NOTES,
    VELOCITY_LANE,
    GRAPH_EDITOR,
    FX_RACK,
    WINDOW_MENU,
}

private struct SvgPt {
    float x;
    float y;
}

/// Computes uniform square transform mapping `[0..viewBox] x [0..viewBox]` centered inside `box`.
private void computeSvgTransform(Rect box, float viewBox, out float ox, out float oy, out float scale) pure nothrow @safe {
    const int bw = box.right - box.left;
    const int bh = box.bottom - box.top;
    const int side = bw < bh ? bw : bh;
    ox = cast(float)box.left + cast(float)(bw - side) * 0.5f;
    oy = cast(float)box.top + cast(float)(bh - side) * 0.5f;
    scale = (side > 0 && viewBox > 0.0f) ? (cast(float)side / viewBox) : 1.0f;
}

/// Parses an SVG `d="..."` path supporting `M/m`, `L/l`, `H/h`, `V/v`, `Z/z` commands into subpaths.
private SvgPt[][] parseSvgSubpaths(string d, out bool[] closedFlags) pure @safe {
    SvgPt[][] subpaths;
    closedFlags = null;

    size_t i = 0;
    const size_t n = d.length;

    void skipSep() pure nothrow @safe {
        while (i < n && (isWhite(d[i]) || d[i] == ',')) i++;
    }

    bool hasNumber() pure nothrow @safe {
        skipSep();
        if (i >= n) return false;
        const char c = d[i];
        return isDigit(c) || c == '-' || c == '+' || c == '.';
    }

    float readFloat() pure @safe {
        skipSep();
        const size_t start = i;
        if (i < n && (d[i] == '-' || d[i] == '+')) i++;
        while (i < n && (isDigit(d[i]) || d[i] == '.')) i++;
        if (i == start) return 0.0f;
        return to!float(d[start .. i]);
    }

    SvgPt cur = SvgPt(0.0f, 0.0f);
    SvgPt subStart = SvgPt(0.0f, 0.0f);
    SvgPt[] curPath;
    bool curClosed = false;
    char cmd = 0;

    void flushSubpath() {
        if (curPath.length > 0) {
            subpaths ~= curPath;
            closedFlags ~= curClosed;
            curPath = null;
            curClosed = false;
        }
    }

    while (i < n) {
        skipSep();
        if (i >= n) break;
        const char ch = d[i];
        if ((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z')) {
            cmd = ch;
            i++;
            if (cmd == 'Z' || cmd == 'z') {
                curClosed = true;
                cur = subStart;
                flushSubpath();
                continue;
            }
        }
        if (cmd == 0) break;

        final switch (cmd) {
            case 'M':
            case 'm': {
                flushSubpath();
                float nx = readFloat();
                float ny = readFloat();
                if (cmd == 'm') {
                    nx += cur.x;
                    ny += cur.y;
                }
                cur = SvgPt(nx, ny);
                subStart = cur;
                curPath ~= cur;
                // Subsequent coordinate pairs after M/m are implicit L/l lines per SVG spec
                cmd = (cmd == 'm') ? 'l' : 'L';
                break;
            }
            case 'L':
            case 'l': {
                while (hasNumber()) {
                    float nx = readFloat();
                    float ny = readFloat();
                    if (cmd == 'l') {
                        nx += cur.x;
                        ny += cur.y;
                    }
                    cur = SvgPt(nx, ny);
                    curPath ~= cur;
                }
                break;
            }
            case 'H':
            case 'h': {
                while (hasNumber()) {
                    float nx = readFloat();
                    if (cmd == 'h') nx += cur.x;
                    cur.x = nx;
                    curPath ~= cur;
                }
                break;
            }
            case 'V':
            case 'v': {
                while (hasNumber()) {
                    float ny = readFloat();
                    if (cmd == 'v') ny += cur.y;
                    cur.y = ny;
                    curPath ~= cur;
                }
                break;
            }
        }
        if (!hasNumber()) continue;
    }
    flushSubpath();
    return subpaths;
}

/// Fills arbitrary closed SVG subpaths via even-odd scanline rasterization using `buf.fillRect`
/// so icons render pixel-identically across both headless `ColorDrawBuf` and `GLDrawBuf`.
private void fillSvgRings(DrawBuf buf, Rect clipBox, const(SvgPt[])[] rings,
                          float ox, float oy, float scale, uint color) {
    if (rings.length == 0 || color == COLOR_TRANSPARENT) return;

    // Transform vertices once and find vertical bounds
    SvgPt[][] trRings;
    float minY = 1e9f;
    float maxY = -1e9f;
    foreach (ring; rings) {
        if (ring.length < 3) continue;
        SvgPt[] tr;
        tr.length = ring.length;
        foreach (idx, pt; ring) {
            const float px = ox + pt.x * scale;
            const float py = oy + pt.y * scale;
            tr[idx] = SvgPt(px, py);
            if (py < minY) minY = py;
            if (py > maxY) maxY = py;
        }
        trRings ~= tr;
    }
    if (trRings.length == 0) return;

    int y0 = cast(int)floorf(minY);
    int y1 = cast(int)ceilf(maxY);
    if (y0 < clipBox.top) y0 = clipBox.top;
    if (y1 > clipBox.bottom) y1 = clipBox.bottom;
    if (y1 <= y0) return;

    float[] xs;
    foreach (py; y0 .. y1) {
        const float sy = cast(float)py + 0.5f;
        xs.length = 0;
        foreach (ring; trRings) {
            const size_t len = ring.length;
            foreach (k; 0 .. len) {
                const SvgPt a = ring[k];
                const SvgPt b = ring[(k + 1) % len];
                if ((a.y <= sy && b.y > sy) || (b.y <= sy && a.y > sy)) {
                    const float t = (sy - a.y) / (b.y - a.y);
                    xs ~= a.x + t * (b.x - a.x);
                }
            }
        }
        if (xs.length < 2) continue;
        // Sort intersections ascending (tiny counts, simple insertion sort avoids allocs)
        foreach (idx; 1 .. xs.length) {
            const float key = xs[idx];
            size_t j = idx;
            while (j > 0 && xs[j - 1] > key) {
                xs[j] = xs[j - 1];
                j--;
            }
            xs[j] = key;
        }
        for (size_t k = 0; k + 1 < xs.length; k += 2) {
            int ix0 = cast(int)floorf(xs[k] + 0.5f);
            int ix1 = cast(int)floorf(xs[k + 1] + 0.5f);
            if (ix0 < clipBox.left) ix0 = clipBox.left;
            if (ix1 > clipBox.right) ix1 = clipBox.right;
            if (ix1 <= ix0 && xs[k + 1] - xs[k] >= 0.35f) {
                // Ensure thin horizontal strokes still light at least 1px when inside bounds
                if (ix0 >= clipBox.left && ix0 < clipBox.right) ix1 = ix0 + 1;
            }
            if (ix1 > ix0) {
                buf.fillRect(Rect(ix0, py, ix1, py + 1), color);
            }
        }
    }
}

/// Renders an SVG `d="..."` path onto `buf` scaled to `box`.
/// When `strokeViewBoxWidth <= 0`, closed subpaths are even-odd filled; otherwise subpaths are stroked.
void drawSvgPath(
    DrawBuf buf,
    Rect box,
    string svgPath,
    uint color,
    float strokeViewBoxWidth = 0.0f,
    float viewBox = 16.0f
) {
    if (box.width <= 0 || box.height <= 0 || color == COLOR_TRANSPARENT) return;
    float ox, oy, scale;
    computeSvgTransform(box, viewBox, ox, oy, scale);

    bool[] closedFlags;
    auto subpaths = parseSvgSubpaths(svgPath, closedFlags);
    if (subpaths.length == 0) return;

    if (strokeViewBoxWidth <= 0.0f) {
        fillSvgRings(buf, box, subpaths, ox, oy, scale, color);
        return;
    }

    float lw = strokeViewBoxWidth * scale;
    if (lw < dpf(1.1f)) lw = dpf(1.1f);
    foreach (idx, sp; subpaths) {
        if (sp.length < 2) continue;
        foreach (k; 0 .. sp.length - 1) {
            drawThickLine(buf,
                          ox + sp[k].x * scale, oy + sp[k].y * scale,
                          ox + sp[k + 1].x * scale, oy + sp[k + 1].y * scale,
                          lw, color);
        }
        if (closedFlags[idx] && sp.length >= 3) {
            drawThickLine(buf,
                          ox + sp[$ - 1].x * scale, oy + sp[$ - 1].y * scale,
                          ox + sp[0].x * scale, oy + sp[0].y * scale,
                          lw, color);
        }
    }
}

/// Renders a crisp centered icon rect inside `box` using `[0..16]` SVG viewBox coordinates.
private Rect svgSquareBox(Rect box, int maxSideDp = 16) nothrow @safe {
    const int pad = box.width > 8 && box.height > 8 ? 2 : 0;
    Rect inner = Rect(box.left + pad, box.top + pad, box.right - pad, box.bottom - pad);
    int side = inner.width < inner.height ? inner.width : inner.height;
    const int maxPx = cast(int)(dpf(cast(float)maxSideDp) + 0.5f);
    if (maxSideDp > 0 && side > maxPx) side = maxPx;
    if (side < 4) side = 4;
    const int cx = box.middlex;
    const int cy = box.middley;
    return Rect(cx - side / 2, cy - side / 2, cx - side / 2 + side, cy - side / 2 + side);
}

/// Draws a pictorial vector icon (`DawIcon`) centered cleanly inside `box`.
void drawDawIcon(DrawBuf buf, Rect box, DawIcon icon, uint color, uint secondaryColor = COLOR_TRANSPARENT) {
    if (box.width < 4 || box.height < 4 || color == COLOR_TRANSPARENT) return;
    Rect sq = svgSquareBox(box, 16);
    float ox, oy, sc;
    computeSvgTransform(sq, 16.0f, ox, oy, sc);
    const uint sec = (secondaryColor != COLOR_TRANSPARENT) ? secondaryColor : color;

    final switch (icon) {
        case DawIcon.PAT_MODE:
            // Four horizontal pattern step blocks
            drawSvgPath(buf, sq, "M 1.5,5 H 4.2 V 11 H 1.5 Z M 5.2,5 H 7.9 V 11 H 5.2 Z M 8.9,5 H 11.6 V 11 H 8.9 Z M 12.6,5 H 15 V 11 H 12.6 Z", color);
            break;

        case DawIcon.SONG_MODE:
            // Staggered horizontal timeline clips
            drawSvgPath(buf, sq, "M 1.5,2.5 H 9 V 5.2 H 1.5 Z M 5.5,6.6 H 14.5 V 9.3 H 5.5 Z M 2.5,10.7 H 11 V 13.4 H 2.5 Z", color);
            break;

        case DawIcon.PLAY:
            drawSvgPath(buf, sq, "M 4,2.4 L 13.4,8 L 4,13.6 Z", color);
            break;

        case DawIcon.STOP:
            drawSvgPath(buf, sq, "M 3.5,3.5 H 12.5 V 12.5 H 3.5 Z", color);
            break;

        case DawIcon.RECORD:
            drawFilledCircle(buf, sq.middlex, sq.middley, 4.6f * sc, color);
            break;

        case DawIcon.LOOP:
            drawOpenArc(buf, ox + 8.0f * sc, oy + 8.0f * sc, 5.0f * sc, -145.0f, -15.0f, 1.5f * sc, color, 12);
            drawOpenArc(buf, ox + 8.0f * sc, oy + 8.0f * sc, 5.0f * sc, 35.0f, 165.0f, 1.5f * sc, color, 12);
            drawSvgPath(buf, sq, "M 10.6,6.8 L 14.2,6.8 L 13.4,3.2 Z M 5.4,9.2 L 1.8,9.2 L 2.6,12.8 Z", color);
            break;

        case DawIcon.METRONOME:
            // Classic metronome body + swinging pendulum wand
            drawSvgPath(buf, sq, "M 3.5,14 L 6.3,2.5 H 9.7 L 12.5,14 Z M 6.9,4.2 L 5.4,12 H 10.6 L 9.1,4.2 Z", color);
            drawSvgPath(buf, sq, "M 3.5,12 H 12.5 V 14 H 3.5 Z", color);
            drawSvgPath(buf, sq, "M 8,11.5 L 13.5,4", color, 1.5f);
            drawFilledCircle(buf, ox + 12.2f * sc, oy + 5.7f * sc, 1.3f * sc, color);
            break;

        case DawIcon.COUNTDOWN:
            // Stopwatch timer (3-2-1 wait countdown)
            drawSvgPath(buf, sq, "M 6.2,1.5 H 9.8 V 3 H 6.2 Z", color);
            drawOpenArc(buf, ox + 8.0f * sc, oy + 9.0f * sc, 4.8f * sc, 0.0f, 360.0f, 1.4f * sc, color, 16);
            drawSvgPath(buf, sq, "M 8,9 V 5.8 M 8,9 L 10.8,9", color, 1.4f);
            break;

        case DawIcon.OVERDUB:
            // Overlapping double layer notes (Blend / Overdub)
            drawSvgPath(buf, sq, "M 2,8 H 9.5 V 13.2 H 2 Z", sec);
            drawSvgPath(buf, sq, "M 6.2,2.8 H 14 V 8 H 6.2 Z M 11.4,10.2 H 14.5 V 11.8 H 11.4 Z M 12.2,9.2 H 13.7 V 12.8 H 12.2 Z", color);
            break;

        case DawIcon.TYPING_KB:
            // Mini piano / typing keyboard silhouette
            drawSvgPath(buf, sq, "M 1.5,3.5 H 14.5 V 12.5 H 1.5 Z M 2.7,4.7 V 11.3 H 5.0 V 4.7 Z M 5.8,4.7 V 11.3 H 7.6 V 4.7 Z M 8.4,4.7 V 11.3 H 10.2 V 4.7 Z M 11.0,4.7 V 11.3 H 13.3 V 4.7 Z", color);
            drawSvgPath(buf, sq, "M 4.1,4.5 H 6.3 V 8.6 H 4.1 Z M 9.7,4.5 H 11.9 V 8.6 H 9.7 Z", color);
            break;

        case DawIcon.SNAP_MAGNET:
            // U-shaped snap magnet
            drawOpenArc(buf, ox + 8.0f * sc, oy + 8.5f * sc, 4.4f * sc, 0.0f, 180.0f, 2.0f * sc, color, 12);
            drawSvgPath(buf, sq, "M 2.6,3 H 4.6 V 8.8 H 2.6 Z M 11.4,3 H 13.4 V 8.8 H 11.4 Z", color);
            drawSvgPath(buf, sq, "M 2.6,2 H 4.6 V 3.6 H 2.6 Z M 11.4,2 H 13.4 V 3.6 H 11.4 Z", sec);
            break;

        case DawIcon.UNDO:
            drawOpenArc(buf, ox + 9.0f * sc, oy + 10.2f * sc, 4.4f * sc, -135.0f, 20.0f, 1.7f * sc, color, 10);
            drawSvgPath(buf, sq, "M 6.8,3.2 L 2.2,6.8 L 6.8,10.4 Z", color);
            break;

        case DawIcon.REDO:
            drawOpenArc(buf, ox + 7.0f * sc, oy + 10.2f * sc, 4.4f * sc, 160.0f, 315.0f, 1.7f * sc, color, 10);
            drawSvgPath(buf, sq, "M 9.2,3.2 L 13.8,6.8 L 9.2,10.4 Z", color);
            break;

        case DawIcon.CHEVRON_LEFT:
            drawSvgPath(buf, sq, "M 10.5,3 L 5.2,8 L 10.5,13", color, 1.8f);
            break;

        case DawIcon.CHEVRON_RIGHT:
            drawSvgPath(buf, sq, "M 5.5,3 L 10.8,8 L 5.5,13", color, 1.8f);
            break;

        case DawIcon.PLUS:
            drawSvgPath(buf, sq, "M 7.1,3 H 8.9 V 13 H 7.1 Z M 3,7.1 H 13 V 8.9 H 3 Z", color);
            break;

        case DawIcon.MINUS:
            drawSvgPath(buf, sq, "M 3,7.1 H 13 V 8.9 H 3 Z", color);
            break;

        case DawIcon.WIN_PLAYLIST:
            // Stacked arrangement clip bars + track ticks
            drawSvgPath(buf, sq, "M 2,2.6 H 9.5 V 5.2 H 2 Z M 5.5,6.7 H 14 V 9.3 H 5.5 Z M 2,10.8 H 11.5 V 13.4 H 2 Z", color);
            break;

        case DawIcon.WIN_CHANNEL_RACK:
            // Two channel rows with step pads
            drawSvgPath(buf, sq, "M 1.5,4 H 4.5 V 6.8 H 1.5 Z M 5.6,4 H 7.6 V 6.8 H 5.6 Z M 8.3,4 H 10.3 V 6.8 H 8.3 Z M 11.0,4 H 13.0 V 6.8 H 11.0 Z M 13.7,4 H 15 V 6.8 H 13.7 Z", color);
            drawSvgPath(buf, sq, "M 1.5,9.2 H 4.5 V 12 H 1.5 Z M 5.6,9.2 H 7.6 V 12 H 5.6 Z M 8.3,9.2 H 10.3 V 12 H 8.3 Z M 11.0,9.2 H 13.0 V 12 H 11.0 Z M 13.7,9.2 H 15 V 12 H 13.7 Z", color);
            break;

        case DawIcon.WIN_PIANO_ROLL:
            // Piano keys left + stepped MIDI notes right
            drawSvgPath(buf, sq, "M 1.5,2.5 H 5.2 V 5.3 H 1.5 Z M 1.5,6.3 H 4.4 V 9.5 H 1.5 Z M 1.5,10.5 H 5.2 V 13.5 H 1.5 Z", sec);
            drawSvgPath(buf, sq, "M 6.8,10.5 H 11 V 12.8 H 6.8 Z M 9.5,6.8 H 14.5 V 9.1 H 9.5 Z M 7.2,3.1 H 12.8 V 5.4 H 7.2 Z", color);
            break;

        case DawIcon.WIN_BROWSER:
            // Folder outline + list lines
            drawSvgPath(buf, sq, "M 1.8,2.5 H 6.5 L 7.9,4.2 H 14.2 V 6.4 H 1.8 Z", color);
            drawSvgPath(buf, sq, "M 2.5,8.0 H 13.5 V 9.4 H 2.5 Z M 4.2,10.4 H 13.5 V 11.8 H 4.2 Z M 4.2,12.8 H 11.2 V 14.0 H 4.2 Z", color);
            break;

        case DawIcon.WIN_MIXER:
            // Three vertical mixer faders with caps
            drawSvgPath(buf, sq, "M 3.3,2 V 14 M 8,2 V 14 M 12.7,2 V 14", sec, 1.1f);
            drawSvgPath(buf, sq, "M 1.8,8.5 H 4.8 V 11 H 1.8 Z M 6.5,4 H 9.5 V 6.5 H 6.5 Z M 11.2,7 H 14.2 V 9.5 H 11.2 Z", color);
            break;

        case DawIcon.WIN_PLUGIN:
            // Audio plug module with two prongs & body
            drawSvgPath(buf, sq, "M 5.0,2 H 6.5 V 5.2 H 5.0 Z M 9.5,2 H 11.0 V 5.2 H 9.5 Z", sec);
            drawSvgPath(buf, sq, "M 3.2,5.2 H 12.8 V 9.0 L 10.0,12.0 H 6.0 L 3.2,9.0 Z M 7.2,12.0 H 8.8 V 14.5 H 7.2 Z", color);
            break;

        case DawIcon.TOOL_DRAW:
            // Pencil diagonal
            drawSvgPath(buf, sq, "M 10.6,2.2 L 13.8,5.4 L 6.0,13.2 L 2.8,10.0 Z M 2.8,10.0 L 6.0,13.2 L 1.8,14.2 Z", color);
            break;

        case DawIcon.TOOL_PAINT:
            // Paintbrush
            drawSvgPath(buf, sq, "M 12.4,2.0 L 14.0,3.6 L 8.2,9.4 L 6.6,7.8 Z", color);
            drawSvgPath(buf, sq, "M 5.6,7.6 L 8.6,10.6 L 5.6,13.6 L 2.0,14.0 L 2.4,10.4 Z", color);
            break;

        case DawIcon.TOOL_DELETE:
            // Tilted eraser block
            drawSvgPath(buf, sq, "M 8.6,2.4 L 13.6,7.4 L 8.2,12.8 L 3.2,7.8 Z", color);
            drawSvgPath(buf, sq, "M 3.2,7.8 L 8.2,12.8 L 6.2,13.8 H 3.2 L 1.8,11.8 Z", sec);
            break;

        case DawIcon.TOOL_MUTE:
            // Muted speaker
            drawSvgPath(buf, sq, "M 1.8,6.0 H 4.5 L 8.4,2.8 V 13.2 L 4.5,10.0 H 1.8 Z", color);
            drawSvgPath(buf, sq, "M 10.2,5.6 L 14.2,10.4 M 14.2,5.6 L 10.2,10.4", color, 1.5f);
            break;

        case DawIcon.TOOL_SLIP:
            // Bidirectional horizontal slip arrows
            drawSvgPath(buf, sq, "M 1.4,8 L 5.2,4.4 V 6.9 H 10.8 V 4.4 L 14.6,8 L 10.8,11.6 V 9.1 H 5.2 V 11.6 Z", color);
            break;

        case DawIcon.TOOL_SLICE:
            // Cutter razor blade + dashed slice line
            drawSvgPath(buf, sq, "M 11.4,2.0 L 13.8,4.4 L 6.0,12.2 L 3.6,9.8 Z", color);
            drawSvgPath(buf, sq, "M 8.0,2.2 V 4.6 M 8.0,11.4 V 14.0", sec, 1.2f);
            break;

        case DawIcon.TOOL_SELECT:
            // Marquee selection corner brackets
            drawSvgPath(buf, sq, "M 2.5,5.8 V 2.5 H 5.8 M 10.2,2.5 H 13.5 V 5.8 M 13.5,10.2 V 13.5 H 10.2 M 5.8,13.5 H 2.5 V 10.2", color, 1.5f);
            drawSvgPath(buf, sq, "M 5.6,5.6 H 10.4 V 10.4 H 5.6 Z", sec);
            break;

        case DawIcon.TOOL_ZOOM:
            // Magnifying glass
            drawOpenArc(buf, ox + 6.6f * sc, oy + 6.6f * sc, 4.1f * sc, 0.0f, 360.0f, 1.5f * sc, color, 14);
            drawSvgPath(buf, sq, "M 9.6,9.6 L 13.8,13.8", color, 1.9f);
            break;

        case DawIcon.CLIP_PICKER:
            // Left sidebar clip list + panel toggle
            drawSvgPath(buf, sq, "M 1.8,2.5 H 5.2 V 5.0 H 1.8 Z M 1.8,6.7 H 5.2 V 9.2 H 1.8 Z M 1.8,10.9 H 5.2 V 13.4 H 1.8 Z", color);
            drawSvgPath(buf, sq, "M 6.8,2.5 H 14.2 V 13.5 H 6.8 Z M 8.1,3.8 V 12.2 H 12.9 V 3.8 Z", sec);
            break;

        case DawIcon.KIND_PATTERN:
            // Stepped pattern notes
            drawSvgPath(buf, sq, "M 2,10.6 H 6.6 V 13 H 2 Z M 5.6,6.8 H 11.0 V 9.2 H 5.6 Z M 9.0,3.0 H 14.0 V 5.4 H 9.0 Z", color);
            break;

        case DawIcon.KIND_AUDIO:
            // Audio waveform spikes
            drawSvgPath(buf, sq, "M 2.0,6.5 H 3.3 V 9.5 H 2.0 Z M 4.3,4.2 H 5.6 V 11.8 H 4.3 Z M 6.6,2.0 H 7.9 V 14.0 H 6.6 Z M 8.9,3.5 H 10.2 V 12.5 H 8.9 Z M 11.2,5.0 H 12.5 V 11.0 H 11.2 Z M 13.4,6.8 H 14.6 V 9.2 H 13.4 Z", color);
            break;

        case DawIcon.KIND_AUTOMATION:
            // Envelope curve + control nodes
            drawSvgPath(buf, sq, "M 2.5,12.5 L 6.2,6.5 L 10.2,9.5 L 13.5,3.5", color, 1.5f);
            drawFilledCircle(buf, ox + 2.5f * sc, oy + 12.5f * sc, 1.3f * sc, color);
            drawFilledCircle(buf, ox + 6.2f * sc, oy + 6.5f * sc, 1.3f * sc, color);
            drawFilledCircle(buf, ox + 10.2f * sc, oy + 9.5f * sc, 1.3f * sc, color);
            drawFilledCircle(buf, ox + 13.5f * sc, oy + 3.5f * sc, 1.3f * sc, color);
            break;

        case DawIcon.GHOST_NOTES:
            // Ghost note behind active note
            drawSvgPath(buf, sq, "M 2,8.5 H 9 V 13 H 2 Z M 3.2,9.7 V 11.8 H 7.8 V 9.7 Z", sec);
            drawSvgPath(buf, sq, "M 6.5,3.2 H 14 V 7.5 H 6.5 Z", color);
            break;

        case DawIcon.VELOCITY_LANE:
            // Vertical velocity bars with tops
            drawSvgPath(buf, sq, "M 3.2,7.5 H 4.8 V 13.8 H 3.2 Z M 7.2,3.2 H 8.8 V 13.8 H 7.2 Z M 11.2,5.6 H 12.8 V 13.8 H 11.2 Z", color);
            drawFilledCircle(buf, ox + 4.0f * sc, oy + 7.2f * sc, 1.4f * sc, color);
            drawFilledCircle(buf, ox + 8.0f * sc, oy + 3.2f * sc, 1.4f * sc, color);
            drawFilledCircle(buf, ox + 12.0f * sc, oy + 5.6f * sc, 1.4f * sc, color);
            break;

        case DawIcon.GRAPH_EDITOR:
            // 4 step parameter bars
            drawSvgPath(buf, sq, "M 2.0,9.0 H 4.3 V 13.6 H 2.0 Z M 5.2,4.5 H 7.5 V 13.6 H 5.2 Z M 8.4,7.0 H 10.7 V 13.6 H 8.4 Z M 11.6,2.8 H 13.9 V 13.6 H 11.6 Z", color);
            break;

        case DawIcon.FX_RACK:
            // Stacked FX slot rows
            drawSvgPath(buf, sq, "M 2,2.6 H 14 V 5.4 H 2 Z M 2,6.6 H 14 V 9.4 H 2 Z M 2,10.6 H 14 V 13.4 H 2 Z", color);
            break;

        case DawIcon.WINDOW_MENU:
            drawSvgPath(buf, sq, "M 3.5,5.5 L 12.5,5.5 L 8.0,11.2 Z", color);
            break;
    }
}

unittest {
    import dlangui.graphics.drawbuf : ColorDrawBuf;
    import std.traits : EnumMembers;

    const uint fg = 0x00FF8F2D;
    const uint bg = 0x001F2329;

    // 1. Test SVG even-odd polygon fill with inner hole (M ... Z M ... Z)
    auto buf = new ColorDrawBuf(32, 32);
    buf.fill(bg);
    drawSvgPath(buf, Rect(0, 0, 32, 32), "M 2,2 H 14 V 14 H 2 Z M 5,5 V 11 H 11 V 5 Z", fg);
    assert(buf.scanLine(6)[6] == fg, "Outer ring pixel should be filled");
    assert(buf.scanLine(16)[16] == bg, "Inner cutout hole should remain background");

    // 2. Verify every DawIcon enum member draws inside bounds cleanly
    foreach (ic; EnumMembers!DawIcon) {
        buf.fill(bg);
        drawDawIcon(buf, Rect(4, 4, 28, 28), ic, fg, 0x0052CFD6);
        int lit = 0;
        foreach (y; 4 .. 28) {
            auto row = buf.scanLine(y);
            foreach (x; 4 .. 28) {
                if (row[x] != bg) lit++;
            }
        }
        assert(lit > 0, "Expected DawIcon to render visible pixels");
    }
}
