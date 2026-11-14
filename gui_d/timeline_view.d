module gui_d.timeline_view;

import dlangui;
import dlangui.graphics.drawbuf : DrawBuf;
import dlangui.widgets.widget : Visibility, Widget;
import gui_d.theme : drawCard, drawPlayheadTriangle, drawThickLine, FlPalette, getFlPalette, truncateLabel;
import gui_d.transport_model : AutomationPoint, EditorTool, PlaylistClipKind,
    TimelineClipState, TransportModel;
import gui_d.workspace : activateParentWindow;
import std.format : format;
import std.utf : toUTF32;

/// Converts timeline time (seconds) to horizontal pixel coordinate.
int secToPixelX(double sec, double pixelsPerSec, int originX, double scrollSec = 0.0) pure nothrow @safe {
    return originX + cast(int)((sec - scrollSec) * pixelsPerSec + 0.5);
}

/// Converts horizontal pixel coordinate back to timeline time (clamped >= 0).
double pixelXToSec(int x, double pixelsPerSec, int originX, double scrollSec = 0.0) pure nothrow @safe {
    if (pixelsPerSec <= 0.0) return 0.0;
    double sec = scrollSec + cast(double)(x - originX) / pixelsPerSec;
    return sec < 0.0 ? 0.0 : sec;
}

/// Returns duration of one quarter-note beat in seconds for `bpm`.
double beatDurationSec(double bpm) pure nothrow @safe {
    return (bpm > 0.0) ? (60.0 / bpm) : 0.5;
}

private enum int TOOLBAR_H = 28;
private enum int PICKER_W = 136;
private enum int HEADER_W = 132;
private enum int RULER_H = 26;
private enum int LANE_H = 50;

private struct ToolButtonDef {
    EditorTool tool;
    string shortLbl;
    string hint;
}

private immutable ToolButtonDef[8] PLAYLIST_TOOLS = [
    ToolButtonDef(EditorTool.DRAW,   "P", "Draw (P) — click to add/move clip"),
    ToolButtonDef(EditorTool.PAINT,  "B", "Paint (B) — click or drag to paint clips"),
    ToolButtonDef(EditorTool.DELETE, "D", "Delete (D) — click clip to remove"),
    ToolButtonDef(EditorTool.MUTE,   "T", "Mute (T) — click clip to mute/unmute"),
    ToolButtonDef(EditorTool.SLIP,   "S", "Slip (S) — slip clip content"),
    ToolButtonDef(EditorTool.SLICE,  "C", "Slice (C) — click clip to split in two"),
    ToolButtonDef(EditorTool.SELECT, "E", "Select (E) — select clips"),
    ToolButtonDef(EditorTool.ZOOM,   "Z", "Zoom (Z) — zoom arrangement"),
];

/// Renders the FL Studio 2026 Playlist (Tool Palette, Clip Picker, Ruler/Markers, Lanes & Clips).
void drawTimelineCanvas(
    DrawBuf buf,
    Rect rc,
    const(TransportModel)* model,
    double pixelsPerSec = 52.0,
    double scrollSec = 0.0,
    FontRef fnt = FontRef.init
) {
    const pal = model ? model.palette() : getFlPalette();
    buf.fillRect(rc, pal.bgWindow);
    if (rc.width < 220 || rc.height < 90) return;

    // 1. Top Playlist Toolbar
    Rect tbRc = Rect(rc.left, rc.top, rc.right, rc.top + TOOLBAR_H);
    buf.fillRect(tbRc, pal.bgPanelAlt);
    buf.fillRect(Rect(tbRc.left, tbRc.bottom - 1, tbRc.right, tbRc.bottom), pal.borderStrong);

    const bool showPicker = model ? model.showClipPicker : true;
    Rect pickToggleRc = Rect(tbRc.left + 6, tbRc.top + 4, tbRc.left + 50, tbRc.bottom - 4);
    drawCard(buf, pickToggleRc, showPicker ? pal.accentOrange : pal.bgControl, pal.borderSubtle, 3.0f);
    if (!fnt.isNull) {
        fnt.drawText(buf, pickToggleRc.left + 7, pickToggleRc.top + 2, "PICK"d,
                     showPicker ? pal.bgLcd : pal.fgText);
    }

    foreach (i, const ref td; PLAYLIST_TOOLS) {
        const int bx0 = tbRc.left + 58 + cast(int)i * 25;
        Rect bRc = Rect(bx0, tbRc.top + 4, bx0 + 22, tbRc.bottom - 4);
        const bool active = model && model.playlistTool == td.tool;
        drawCard(buf, bRc, active ? pal.accentOrange : pal.bgControl, pal.borderSubtle, 3.0f);
        if (!fnt.isNull) {
            fnt.drawText(buf, bRc.left + 7, bRc.top + 2, toUTF32(td.shortLbl),
                         active ? pal.bgLcd : pal.fgText);
        }
    }

    // Zoom +/- buttons on right
    Rect zoomOutRc = Rect(tbRc.right - 56, tbRc.top + 4, tbRc.right - 32, tbRc.bottom - 4);
    Rect zoomInRc  = Rect(tbRc.right - 30, tbRc.top + 4, tbRc.right - 6,  tbRc.bottom - 4);
    drawCard(buf, zoomOutRc, pal.bgControl, pal.borderSubtle, 3.0f);
    drawCard(buf, zoomInRc,  pal.bgControl, pal.borderSubtle, 3.0f);
    if (!fnt.isNull) {
        fnt.drawText(buf, zoomOutRc.left + 8, zoomOutRc.top + 2, "-"d, pal.fgText);
        fnt.drawText(buf, zoomInRc.left + 7,  zoomInRc.top + 2,  "+"d, pal.fgText);
    }

    // 2. Optional Left Clip Picker Panel (Pattern / Audio / Automation)
    const int contentTop = tbRc.bottom;
    int tracksLeft = rc.left;
    if (showPicker && rc.width > PICKER_W + HEADER_W + 140) {
        Rect pkrRc = Rect(rc.left, contentTop, rc.left + PICKER_W, rc.bottom);
        buf.fillRect(pkrRc, pal.bgPanelAlt);
        buf.fillRect(Rect(pkrRc.right - 1, pkrRc.top, pkrRc.right, pkrRc.bottom), pal.borderStrong);

        // 3 Kind Tabs: PAT | AUD | AUT
        static immutable string[3] K_LBLS = ["PAT", "AUD", "AUT"];
        static immutable PlaylistClipKind[3] KINDS = [
            PlaylistClipKind.PATTERN, PlaylistClipKind.AUDIO, PlaylistClipKind.AUTOMATION
        ];
        const int tabW = (PICKER_W - 12) / 3;
        foreach (k; 0 .. 3) {
            Rect ktRc = Rect(pkrRc.left + 4 + k * (tabW + 2), pkrRc.top + 4,
                             pkrRc.left + 4 + k * (tabW + 2) + tabW, pkrRc.top + 24);
            const bool kAct = model && model.activePickerKind == KINDS[k];
            drawCard(buf, ktRc, kAct ? pal.accentOrange : pal.bgControl, pal.borderSubtle, 3.0f);
            if (!fnt.isNull) {
                fnt.drawText(buf, ktRc.left + 7, ktRc.top + 2, toUTF32(K_LBLS[k]),
                             kAct ? pal.bgLcd : pal.fgText);
            }
        }

        // Picker entries
        const PlaylistClipKind curKind = model ? model.activePickerKind : PlaylistClipKind.PATTERN;
        const int curIdx = model ? model.activePickerIndex : 0;
        if (curKind == PlaylistClipKind.PATTERN && model) {
            foreach (i, const ref pat; model.patterns) {
                const int py = pkrRc.top + 30 + cast(int)i * 26;
                if (py + 22 > pkrRc.bottom) break;
                Rect itemRc = Rect(pkrRc.left + 6, py, pkrRc.right - 6, py + 22);
                const bool sel = (cast(int)i == curIdx);
                drawCard(buf, itemRc, pat.color, sel ? pal.accentOrange : pal.borderStrong, 3.0f);
                if (!fnt.isNull) {
                    fnt.drawText(buf, itemRc.left + 6, itemRc.top + 3,
                                 toUTF32(truncateLabel(pat.name, 14)), pal.fgText);
                }
            }
        } else if (curKind == PlaylistClipKind.AUDIO) {
            static immutable string[3] AUD_NAMES = ["loop140.wav", "bb140.wav", "808_sub.wav"];
            foreach (i, nm; AUD_NAMES) {
                const int py = pkrRc.top + 30 + cast(int)i * 26;
                Rect itemRc = Rect(pkrRc.left + 6, py, pkrRc.right - 6, py + 22);
                const bool sel = (cast(int)i == curIdx);
                drawCard(buf, itemRc, pal.clipAudio, sel ? pal.accentCyan : pal.borderStrong, 3.0f);
                if (!fnt.isNull) {
                    fnt.drawText(buf, itemRc.left + 6, itemRc.top + 3, toUTF32(nm), pal.fgText);
                }
            }
        } else {
            static immutable string[2] AUT_NAMES = ["Acid Cutoff Env", "Master Volume Env"];
            foreach (i, nm; AUT_NAMES) {
                const int py = pkrRc.top + 30 + cast(int)i * 26;
                Rect itemRc = Rect(pkrRc.left + 6, py, pkrRc.right - 6, py + 22);
                const bool sel = (cast(int)i == curIdx);
                drawCard(buf, itemRc, pal.clipAutomation, sel ? pal.accentPurple : pal.borderStrong, 3.0f);
                if (!fnt.isNull) {
                    fnt.drawText(buf, itemRc.left + 6, itemRc.top + 3, toUTF32(nm), pal.fgText);
                }
            }
        }
        tracksLeft = pkrRc.right;
    }

    // 3. Playlist Header Column & Arrangement Ruler
    Rect arrRc = Rect(tracksLeft + HEADER_W, contentTop, rc.right, rc.bottom);
    if (arrRc.width < 40) return;

    Rect cornerRc = Rect(tracksLeft, contentTop, arrRc.left, contentTop + RULER_H);
    buf.fillRect(cornerRc, pal.bgPanel);
    buf.fillRect(Rect(cornerRc.left, cornerRc.bottom - 1, cornerRc.right, cornerRc.bottom), pal.borderStrong);
    if (!fnt.isNull) {
        fnt.drawText(buf, cornerRc.left + 8, cornerRc.top + 6, "Arrangement 1"d, pal.fgMuted);
    }

    Rect rulerRc = Rect(arrRc.left, contentTop, arrRc.right, contentTop + RULER_H);
    buf.fillRect(rulerRc, pal.bgLcd);
    buf.fillRect(Rect(rulerRc.left, rulerRc.bottom - 1, rulerRc.right, rulerRc.bottom), pal.borderStrong);

    // Loop region highlight on ruler
    if (model && model.loopEnabled && model.loopEndSec > model.loopStartSec) {
        int lx0 = secToPixelX(model.loopStartSec, pixelsPerSec, arrRc.left, scrollSec);
        int lx1 = secToPixelX(model.loopEndSec, pixelsPerSec, arrRc.left, scrollSec);
        if (lx0 < rulerRc.left) lx0 = rulerRc.left;
        if (lx1 > rulerRc.right) lx1 = rulerRc.right;
        if (lx1 > lx0) {
            buf.fillRect(Rect(lx0, rulerRc.top + 1, lx1, rulerRc.top + 6), pal.accentRed);
        }
    }

    // 4. Beat & Bar Grid + Alternating 4-Bar Blocks
    const double bpm = (model && model.bpm > 0.0) ? model.bpm : 140.0;
    const double beatSec = beatDurationSec(bpm);
    const int trackCount = (model && model.tracks.length > 1) ? cast(int)model.tracks.length - 1 : 6;
    const int lanesTop = contentTop + RULER_H;

    // 4-bar alternating background shading
    for (int block = 0; ; ++block) {
        const double t0 = block * 16.0 * beatSec;
        const double t1 = (block + 1) * 16.0 * beatSec;
        int bx0 = secToPixelX(t0, pixelsPerSec, arrRc.left, scrollSec);
        int bx1 = secToPixelX(t1, pixelsPerSec, arrRc.left, scrollSec);
        if (bx0 >= arrRc.right) break;
        if (bx1 <= arrRc.left) continue;
        if (bx0 < arrRc.left) bx0 = arrRc.left;
        if (bx1 > arrRc.right) bx1 = arrRc.right;
        const uint bg = (block % 2 == 0) ? pal.bgWindow : pal.bgPanelAlt;
        buf.fillRect(Rect(bx0, lanesTop, bx1, rc.bottom), bg);
    }

    // Track headers + lane dividers + clips
    foreach (laneIdx; 0 .. trackCount) {
        const int tIdx = laneIdx + 1; // Skip 0 (Master) in Playlist lanes
        const int y0 = lanesTop + laneIdx * LANE_H;
        int y1 = y0 + LANE_H;
        if (y0 >= rc.bottom - 4) break;
        if (y1 > rc.bottom) y1 = rc.bottom;

        // Track Header
        Rect hdrRc = Rect(tracksLeft, y0, arrRc.left, y1);
        buf.fillRect(hdrRc, pal.bgPanel);
        buf.fillRect(Rect(hdrRc.left, hdrRc.bottom - 1, hdrRc.right, hdrRc.bottom), pal.borderStrong);
        buf.fillRect(Rect(hdrRc.right - 1, hdrRc.top, hdrRc.right, hdrRc.bottom), pal.borderStrong);

        const bool hasTrack = (model && tIdx < cast(int)model.tracks.length);
        const uint trkCol = hasTrack ? model.tracks[tIdx].color : pal.clipPattern;
        const string title = hasTrack ? model.tracks[tIdx].name : format("Track %d", tIdx);
        const bool muted = hasTrack ? model.tracks[tIdx].muted : false;
        const bool soloed = hasTrack ? model.tracks[tIdx].soloed : false;

        // Track Color Bar on left
        buf.fillRect(Rect(hdrRc.left, hdrRc.top, hdrRc.left + 4, hdrRc.bottom - 1), trkCol);

        // Mute LED on right of header
        Rect mLed = Rect(hdrRc.right - 20, hdrRc.top + 8, hdrRc.right - 8, hdrRc.top + 20);
        drawCard(buf, mLed, soloed ? pal.accentAmber : (muted ? pal.ledOff : pal.accentGreen),
                 pal.borderStrong, 3.0f);

        if (!fnt.isNull) {
            fnt.drawText(buf, hdrRc.left + 10, hdrRc.top + 8,
                         toUTF32(truncateLabel(title, 12)),
                         muted ? pal.fgDim : pal.fgText);
        }

        // Lane bottom divider
        buf.fillRect(Rect(arrRc.left, y1 - 1, arrRc.right, y1), pal.borderStrong);
    }

    // Vertical Bar/Beat Grid Lines + Ruler Labels
    for (int beatIdx = 0; ; ++beatIdx) {
        const double t = beatIdx * beatSec;
        const int gx = secToPixelX(t, pixelsPerSec, arrRc.left, scrollSec);
        if (gx >= arrRc.right) break;
        if (gx < arrRc.left) continue;
        const bool isBar = (beatIdx % 4) == 0;
        const uint gridCol = isBar ? pal.borderSubtle : 0x40FFFFFF;
        buf.fillRect(Rect(gx, lanesTop, gx + 1, rc.bottom), isBar ? pal.borderSubtle : pal.borderStrong);
        buf.fillRect(Rect(gx, isBar ? rulerRc.top + 8 : rulerRc.bottom - 6, gx + 1, rulerRc.bottom),
                     isBar ? pal.accentCyan : pal.fgDim);
        if (isBar && !fnt.isNull && gx + 16 < rulerRc.right) {
            fnt.drawText(buf, gx + 3, rulerRc.top + 7,
                         toUTF32(format("%d", (beatIdx / 4) + 1)), pal.fgText);
        }
    }

    // Time Markers on Ruler
    if (model) {
        foreach (const ref mk; model.timeMarkers) {
            const int mx = secToPixelX(mk.timeSec, pixelsPerSec, arrRc.left, scrollSec);
            if (mx < arrRc.left || mx >= arrRc.right - 20) continue;
            buf.fillRect(Rect(mx, rulerRc.top + 1, mx + 2, rulerRc.bottom), mk.color);
            if (!fnt.isNull) {
                fnt.drawText(buf, mx + 4, rulerRc.top + 1, toUTF32(mk.label), mk.color);
            }
        }
    }

    // Draw Clips on Lanes
    if (model) {
        foreach (laneIdx; 0 .. trackCount) {
            const int tIdx = laneIdx + 1;
            if (tIdx >= cast(int)model.tracks.length) break;
            const int y0 = lanesTop + laneIdx * LANE_H;
            int y1 = y0 + LANE_H;
            if (y0 >= rc.bottom - 4) break;
            if (y1 > rc.bottom) y1 = rc.bottom;
            foreach (const ref clip; model.tracks[tIdx].clips) {
                drawPlaylistClip(buf, arrRc, y0 + 3, y1 - 3, clip, pal, pixelsPerSec, scrollSec, fnt);
            }
        }
    }

    // Playhead cap & vertical line
    const double phSec = model ? model.playheadPosSec : 0.0;
    const int phX = secToPixelX(phSec, pixelsPerSec, arrRc.left, scrollSec);
    if (phX >= arrRc.left && phX < arrRc.right) {
        drawPlayheadTriangle(buf, phX, rulerRc.top + 2, 6, 9, pal.accentOrange);
        buf.fillRect(Rect(phX, rulerRc.top + 8, phX + 2, rc.bottom), pal.accentOrange);
    }
}

private void drawPlaylistClip(
    DrawBuf buf,
    Rect arrRc,
    int topY,
    int botY,
    ref const(TimelineClipState) clip,
    const ref FlPalette pal,
    double pixelsPerSec,
    double scrollSec,
    FontRef fnt
) {
    int cx0 = secToPixelX(clip.startTimeSec, pixelsPerSec, arrRc.left, scrollSec);
    int cx1 = secToPixelX(clip.startTimeSec + clip.durationSec, pixelsPerSec, arrRc.left, scrollSec);
    if (cx1 <= arrRc.left || cx0 >= arrRc.right) return;
    if (cx0 < arrRc.left) cx0 = arrRc.left;
    if (cx1 > arrRc.right) cx1 = arrRc.right;
    if (cx1 - cx0 < 6 || botY - topY < 12) return;

    uint baseCol = clip.color;
    if (baseCol == 0) {
        if (clip.kind == PlaylistClipKind.PATTERN) baseCol = pal.clipPattern;
        else if (clip.kind == PlaylistClipKind.AUDIO) baseCol = pal.clipAudio;
        else baseCol = pal.clipAutomation;
    }
    if (clip.muted) baseCol = pal.bgPanelAlt;

    Rect clipRc = Rect(cx0, topY, cx1, botY);
    drawCard(buf, clipRc, baseCol, clip.muted ? pal.fgDim : pal.borderSubtle, 4.0f);

    // Clip header bar
    const int hdrBot = (topY + 15 < botY) ? (topY + 15) : botY;
    buf.fillRect(Rect(cx0 + 2, topY + 1, cx1 - 2, hdrBot),
                 clip.muted ? pal.bgPanel : (baseCol + 0x00121518));
    if (!fnt.isNull && cx1 - cx0 > 28) {
        fnt.drawText(buf, cx0 + 5, topY + 1, toUTF32(truncateLabel(clip.name, 16)),
                     clip.muted ? pal.fgDim : pal.fgText);
    }

    const int bodyTop = hdrBot + 2;
    const int bodyBot = botY - 3;
    const int bodyW = cx1 - cx0 - 6;
    if (bodyBot - bodyTop < 6 || bodyW < 8) return;

    if (clip.kind == PlaylistClipKind.AUDIO && clip.waveform.length > 0) {
        const int midY = (bodyTop + bodyBot) / 2;
        const int halfH = (bodyBot - bodyTop) / 2;
        foreach (px; 0 .. bodyW) {
            const size_t bucket = (cast(size_t)px * clip.waveform.length) / cast(size_t)bodyW;
            float peak = clip.waveform[bucket];
            if (peak < 0.0f) peak = -peak;
            if (peak > 1.0f) peak = 1.0f;
            int barH = cast(int)(peak * halfH);
            if (barH < 1) barH = 1;
            buf.fillRect(Rect(cx0 + 3 + px, midY - barH, cx0 + 4 + px, midY + barH),
                         clip.muted ? pal.fgDim : pal.accentCyan);
        }
    } else if (clip.kind == PlaylistClipKind.AUTOMATION) {
        // Draw smooth automation envelope curve + control nodes
        const int h = bodyBot - bodyTop;
        if (clip.autoPoints.length >= 2) {
            foreach (i; 0 .. clip.autoPoints.length - 1) {
                const auto p0 = clip.autoPoints[i];
                const auto p1 = clip.autoPoints[i + 1];
                const float x0 = cx0 + 3 + cast(float)(p0.normTime * bodyW);
                const float y0 = bodyBot - p0.value * h;
                const float x1 = cx0 + 3 + cast(float)(p1.normTime * bodyW);
                const float y1 = bodyBot - p1.value * h;
                drawThickLine(buf, x0, y0, x1, y1, 1.8f,
                              clip.muted ? pal.fgDim : pal.accentAmber);
                buf.fillRect(Rect(cast(int)x0 - 2, cast(int)y0 - 2, cast(int)x0 + 3, cast(int)y0 + 3),
                             pal.fgText);
            }
            const auto plast = clip.autoPoints[$ - 1];
            const int lx = cx0 + 3 + cast(int)(plast.normTime * bodyW);
            const int ly = bodyBot - cast(int)(plast.value * h);
            buf.fillRect(Rect(lx - 2, ly - 2, lx + 3, ly + 3), pal.fgText);
        }
    } else if (clip.kind == PlaylistClipKind.PATTERN) {
        // Mini piano-roll / step preview bars inside pattern clip
        const uint noteCol = clip.muted ? pal.fgDim : pal.noteFill;
        foreach (k; 0 .. 8) {
            const int nx0 = cx0 + 4 + (k * bodyW) / 8;
            const int nx1 = nx0 + (bodyW / 12) + 2;
            const int ny = bodyTop + ((k * 5 + clip.patternIndex * 3) % (bodyBot - bodyTop - 3));
            if (nx1 < cx1 - 2) {
                buf.fillRect(Rect(nx0, ny, nx1, ny + 3), noteCol);
            }
        }
    }
}

/// Interactive FL Studio 2026 Playlist Widget (Tool Palette, Clip Picker, Drag/Resize/Slice/Seek).
class TimelineCanvasWidget : Widget {
    private TransportModel* model_;

    private enum DragMode { NONE, PLAYHEAD, MOVE_CLIP, RESIZE_CLIP }
    private DragMode dragMode_ = DragMode.NONE;
    private int dragTrackArrIdx_ = -1;
    private int dragClipArrIdx_ = -1;
    private double dragOffsetSec_ = 0.0;

    double pixelsPerSec = 52.0;
    double scrollSec = 0.0;

    void delegate(double seekSec) onSeek;
    void delegate(int trackIdx, int clipIdx) onSelectTrackClip;
    void delegate(int trackIdx, double startSec) onPaintClip;
    void delegate(int trackIdx, int clipIdx) onDeleteClip;

    this(string id, TransportModel* model) {
        super(id);
        model_ = model;
        layoutWidth = FILL_PARENT;
        layoutHeight = FILL_PARENT;
    }

    private int tracksLeftX() const @safe {
        const bool showPicker = model_ ? model_.showClipPicker : true;
        return (showPicker && _pos.width > PICKER_W + HEADER_W + 140)
            ? (_pos.left + PICKER_W)
            : _pos.left;
    }

    private Rect arrangementRect() const @safe {
        return Rect(tracksLeftX() + HEADER_W, _pos.top + TOOLBAR_H, _pos.right, _pos.bottom);
    }

    private double snapSec(double sec) const @safe {
        if (!model_) return sec;
        const double beatSec = beatDurationSec(model_.bpm);
        const double sb = model_.snapBeats();
        if (sb <= 0.0) return sec < 0.0 ? 0.0 : sec;
        const double stepSec = sb * beatSec;
        const double snapped = (cast(long)((sec + stepSec * 0.5) / stepSec)) * stepSec;
        return snapped < 0.0 ? 0.0 : snapped;
    }

    private void handleSeekAtX(int x, Rect arrRc) {
        if (x < arrRc.left) x = arrRc.left;
        double sec = pixelXToSec(x, pixelsPerSec, arrRc.left, scrollSec);
        if (model_) model_.playheadPosSec = sec;
        if (onSeek !is null) onSeek(sec);
        invalidate();
    }

    override bool onMouseEvent(MouseEvent event) {
        if (event.action == MouseAction.FocusOut) {
            return dragMode_ != DragMode.NONE;
        }
        if (!model_) return super.onMouseEvent(event);

        Rect arrRc = arrangementRect();
        const int contentTop = _pos.top + TOOLBAR_H;
        const int lanesTop = contentTop + RULER_H;

        if (event.action == MouseAction.ButtonDown) {
            activateParentWindow(this);

            // 1. Toolbar clicks
            if (event.y >= _pos.top && event.y < contentTop) {
                Rect pickToggleRc = Rect(_pos.left + 6, _pos.top + 4, _pos.left + 50, contentTop - 4);
                if (pickToggleRc.isPointInside(event.x, event.y)) {
                    model_.showClipPicker = !model_.showClipPicker;
                    invalidate();
                    return true;
                }
                foreach (i, const ref td; PLAYLIST_TOOLS) {
                    const int bx0 = _pos.left + 58 + cast(int)i * 25;
                    Rect bRc = Rect(bx0, _pos.top + 4, bx0 + 22, contentTop - 4);
                    if (bRc.isPointInside(event.x, event.y)) {
                        model_.playlistTool = td.tool;
                        model_.hintText = "Playlist Tool: " ~ td.hint;
                        invalidate();
                        return true;
                    }
                }
                Rect zoomOutRc = Rect(_pos.right - 56, _pos.top + 4, _pos.right - 32, contentTop - 4);
                Rect zoomInRc  = Rect(_pos.right - 30, _pos.top + 4, _pos.right - 6,  contentTop - 4);
                if (zoomOutRc.isPointInside(event.x, event.y)) {
                    pixelsPerSec = (pixelsPerSec > 20.0) ? pixelsPerSec * 0.8 : 20.0;
                    invalidate();
                    return true;
                }
                if (zoomInRc.isPointInside(event.x, event.y)) {
                    pixelsPerSec = (pixelsPerSec < 180.0) ? pixelsPerSec * 1.25 : 180.0;
                    invalidate();
                    return true;
                }
                return true;
            }

            // 2. Left Clip Picker clicks
            if (model_.showClipPicker && event.x < tracksLeftX()) {
                const int tabW = (PICKER_W - 12) / 3;
                if (event.y < contentTop + 26) {
                    static immutable PlaylistClipKind[3] KINDS = [
                        PlaylistClipKind.PATTERN, PlaylistClipKind.AUDIO, PlaylistClipKind.AUTOMATION
                    ];
                    foreach (k; 0 .. 3) {
                        const int x0 = _pos.left + 4 + k * (tabW + 2);
                        if (event.x >= x0 && event.x <= x0 + tabW) {
                            model_.activePickerKind = KINDS[k];
                            model_.activePickerIndex = 0;
                            invalidate();
                            return true;
                        }
                    }
                    return true;
                }
                const int itemIdx = (event.y - (contentTop + 30)) / 26;
                if (itemIdx >= 0) {
                    model_.activePickerIndex = itemIdx;
                    if (model_.activePickerKind == PlaylistClipKind.PATTERN &&
                        itemIdx < cast(int)model_.patterns.length) {
                        model_.selectedPatternIndex = itemIdx;
                    }
                    model_.hintText = "Selected clip brush in Picker — click in Playlist lane to paint";
                    invalidate();
                }
                return true;
            }

            // 3. Ruler seek
            if (event.x >= arrRc.left && event.y >= contentTop && event.y < lanesTop) {
                if (event.button == MouseButton.Left) {
                    dragMode_ = DragMode.PLAYHEAD;
                    handleSeekAtX(event.x, arrRc);
                }
                return true;
            }

            // 4. Track Header & Lane clicks
            if (event.y >= lanesTop) {
                const int laneIdx = (event.y - lanesTop) / LANE_H;
                const int tIdx = laneIdx + 1;
                if (tIdx <= 0 || tIdx >= cast(int)model_.tracks.length) return true;
                auto trk = &model_.tracks[tIdx];

                // Track header mute LED
                if (event.x < arrRc.left) {
                    Rect mLed = Rect(arrRc.left - 22, lanesTop + laneIdx * LANE_H + 6,
                                     arrRc.left - 6, lanesTop + laneIdx * LANE_H + 22);
                    if (mLed.isPointInside(event.x, event.y)) {
                        if (event.button == MouseButton.Right) trk.soloed = !trk.soloed;
                        else trk.muted = !trk.muted;
                    } else {
                        model_.selectedTrack = trk.index;
                    }
                    invalidate();
                    return true;
                }

                // Arrangement lane click
                const double clickSec = pixelXToSec(event.x, pixelsPerSec, arrRc.left, scrollSec);
                int hitArrIdx = -1;
                foreach (ci, const ref c; trk.clips) {
                    if (clickSec >= c.startTimeSec && clickSec <= c.startTimeSec + c.durationSec) {
                        hitArrIdx = cast(int)ci;
                        break;
                    }
                }

                // Right-click or Delete tool removes clip
                if ((event.button == MouseButton.Right || model_.playlistTool == EditorTool.DELETE) &&
                    hitArrIdx >= 0) {
                    const int clipId = trk.clips[hitArrIdx].clipIndex;
                    trk.clips = trk.clips[0 .. hitArrIdx] ~ trk.clips[hitArrIdx + 1 .. $];
                    if (onDeleteClip !is null) onDeleteClip(trk.index, clipId);
                    invalidate();
                    return true;
                }

                if (hitArrIdx >= 0) {
                    auto clip = &trk.clips[hitArrIdx];
                    if (model_.playlistTool == EditorTool.MUTE) {
                        clip.muted = !clip.muted;
                        invalidate();
                        return true;
                    }
                    if (model_.playlistTool == EditorTool.SLICE) {
                        const double splitSec = snapSec(clickSec);
                        if (splitSec > clip.startTimeSec + 0.1 &&
                            splitSec < clip.startTimeSec + clip.durationSec - 0.1) {
                            TimelineClipState rightHalf = *clip;
                            rightHalf.clipIndex = cast(int)trk.clips.length + 10;
                            rightHalf.startTimeSec = splitSec;
                            rightHalf.durationSec = (clip.startTimeSec + clip.durationSec) - splitSec;
                            clip.durationSec = splitSec - clip.startTimeSec;
                            trk.clips ~= rightHalf;
                            model_.hintText = "Sliced clip cleanly";
                            invalidate();
                        }
                        return true;
                    }
                    // Check right-edge resize (within 8px of clip right edge)
                    const int cx1 = secToPixelX(clip.startTimeSec + clip.durationSec, pixelsPerSec, arrRc.left, scrollSec);
                    if (event.x >= cx1 - 8) {
                        dragMode_ = DragMode.RESIZE_CLIP;
                    } else {
                        dragMode_ = DragMode.MOVE_CLIP;
                        dragOffsetSec_ = clickSec - clip.startTimeSec;
                    }
                    dragTrackArrIdx_ = tIdx;
                    dragClipArrIdx_ = hitArrIdx;
                    model_.selectedTrack = trk.index;
                    model_.selectedClipIndex = clip.clipIndex;
                    if (onSelectTrackClip !is null) onSelectTrackClip(trk.index, clip.clipIndex);
                    invalidate();
                    return true;
                }

                // Empty lane click -> paint new clip from active Picker brush
                if (event.button == MouseButton.Left &&
                    (model_.playlistTool == EditorTool.DRAW || model_.playlistTool == EditorTool.PAINT)) {
                    const double barSec = 4.0 * beatDurationSec(model_.bpm);
                    const double startSec = (cast(long)(clickSec / barSec)) * barSec;
                    if (onPaintClip !is null) {
                        onPaintClip(trk.index, startSec);
                    } else {
                        TimelineClipState nc;
                        nc.clipIndex = cast(int)trk.clips.length;
                        nc.kind = model_.activePickerKind;
                        nc.patternIndex = model_.activePickerIndex;
                        nc.startTimeSec = startSec;
                        nc.durationSec = barSec * 2.0;
                        nc.name = (nc.kind == PlaylistClipKind.PATTERN &&
                                   nc.patternIndex < cast(int)model_.patterns.length)
                            ? model_.patterns[nc.patternIndex].name
                            : "Clip";
                        trk.clips ~= nc;
                    }
                    invalidate();
                    return true;
                }
            }
            return true;
        }

        if (event.action == MouseAction.Move && dragMode_ != DragMode.NONE) {
            if (dragMode_ == DragMode.PLAYHEAD) {
                handleSeekAtX(event.x, arrRc);
                return true;
            }
            if (dragTrackArrIdx_ >= 0 && dragTrackArrIdx_ < cast(int)model_.tracks.length &&
                dragClipArrIdx_ >= 0 && dragClipArrIdx_ < cast(int)model_.tracks[dragTrackArrIdx_].clips.length) {
                const double curSec = pixelXToSec(event.x, pixelsPerSec, arrRc.left, scrollSec);
                auto clip = &model_.tracks[dragTrackArrIdx_].clips[dragClipArrIdx_];
                if (dragMode_ == DragMode.MOVE_CLIP) {
                    clip.startTimeSec = snapSec(curSec - dragOffsetSec_);
                } else if (dragMode_ == DragMode.RESIZE_CLIP) {
                    const double minDur = beatDurationSec(model_.bpm) * 0.5;
                    double newDur = snapSec(curSec) - clip.startTimeSec;
                    if (newDur < minDur) newDur = minDur;
                    clip.durationSec = newDur;
                }
                invalidate();
            }
            return true;
        }

        if (event.action == MouseAction.ButtonUp && dragMode_ != DragMode.NONE) {
            dragMode_ = DragMode.NONE;
            dragTrackArrIdx_ = -1;
            dragClipArrIdx_ = -1;
            return true;
        }

        return super.onMouseEvent(event);
    }

    override void onDraw(DrawBuf buf) {
        if (visibility != Visibility.Visible) return;
        drawTimelineCanvas(buf, _pos, model_, pixelsPerSec, scrollSec, font);
    }
}

unittest {
    import dlangui.graphics.drawbuf : ColorDrawBuf;

    assert(secToPixelX(0.0, 40.0, 136) == 136);
    assert(secToPixelX(2.5, 40.0, 136) == 236);
    assert(pixelXToSec(236, 40.0, 136) == 2.5);
    assert(pixelXToSec(10, 40.0, 136) == 0.0);
    assert(beatDurationSec(120.0) == 0.5);

    TransportModel model;
    model.initDefaultTemplate();
    model.playheadPosSec = 1.0;

    auto buf = new ColorDrawBuf(900, 360);
    drawTimelineCanvas(buf, Rect(0, 0, 900, 360), &model, 52.0, 0.0);

    const pal = model.palette();
    // arrRc.left = PICKER_W(136) + HEADER_W(132) = 268
    const int phX = secToPixelX(1.0, 52.0, PICKER_W + HEADER_W);
    assert(buf.scanLine(TOOLBAR_H + 12)[phX] == pal.accentOrange,
           "Expected FL Studio orange playhead line in Playlist ruler");
}
