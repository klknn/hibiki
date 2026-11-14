module gui_d.timeline_view;

import dlangui;
import dlangui.graphics.drawbuf : DrawBuf;
import dlangui.widgets.widget : Widget;
import gui_d.theme : Y2kColor;
import gui_d.transport_model : TimelineClipState, TransportModel;
import gui_d.widgets : drawBevelPanel, truncateLabel;
import std.format : format;

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

/// Custom Y2K DAW Timeline / Arrangement Canvas (`dlangui` `Widget`).
/// Renders track lanes, beat/bar grid, loop region, clips with waveform peaks, and playhead.
class TimelineCanvasWidget : Widget {
    private TransportModel* model_;
    private bool draggingPlayhead_ = false;

    double pixelsPerSec = 44.0;
    double scrollSec = 0.0;

    enum int HEADER_WIDTH = 136;
    enum int RULER_HEIGHT = 24;
    enum int LANE_HEIGHT = 52;

    void delegate(double seekSec) onSeek;
    void delegate(int trackIdx, int clipIdx) onSelectTrackClip;

    this(string id, TransportModel* model) {
        super(id);
        model_ = model;
        layoutWidth = FILL_PARENT;
        layoutHeight = FILL_PARENT;
        minWidth = 640;
        minHeight = 220;
    }

    private int effectiveTrackCount() const pure @safe {
        return (model_ && model_.tracks.length > 0) ? cast(int)model_.tracks.length : 4;
    }

    private Rect arrangementRect(Rect well) const @safe {
        return Rect(well.left + HEADER_WIDTH, well.top, well.right, well.bottom);
    }

    private void handleSeekAtX(int x, Rect arrRc) {
        if (x < arrRc.left) x = arrRc.left;
        double sec = pixelXToSec(x, pixelsPerSec, arrRc.left, scrollSec);
        if (model_) {
            model_.playheadPosSec = sec;
        }
        if (onSeek !is null) {
            onSeek(sec);
        }
        invalidate();
    }

    override bool onMouseEvent(MouseEvent event) {
        Rect well = Rect(_pos.left + 8, _pos.top + 8, _pos.right - 8, _pos.bottom - 8);
        Rect arrRc = arrangementRect(well);

        if (event.action == MouseAction.ButtonDown && event.button == MouseButton.Left) {
            if (event.y >= well.top + RULER_HEIGHT && event.y < well.bottom) {
                const int lane = (event.y - (well.top + RULER_HEIGHT)) / LANE_HEIGHT;
                if (model_ && lane >= 0 && lane < cast(int)model_.tracks.length) {
                    const int trackIdx = model_.tracks[lane].index;
                    int hitClip = -1;
                    if (arrRc.isPointInside(event.x, event.y)) {
                        const double clickSec = pixelXToSec(event.x, pixelsPerSec, arrRc.left, scrollSec);
                        foreach (const ref c; model_.tracks[lane].clips) {
                            if (clickSec >= c.startTimeSec && clickSec <= c.startTimeSec + c.durationSec) {
                                hitClip = c.clipIndex;
                                break;
                            }
                        }
                    }
                    model_.selectedTrack = trackIdx;
                    if (hitClip >= 0) model_.selectedClipIndex = hitClip;
                    if (onSelectTrackClip !is null) onSelectTrackClip(trackIdx, hitClip);
                }
            }
            if (arrRc.isPointInside(event.x, event.y)) {
                draggingPlayhead_ = true;
                handleSeekAtX(event.x, arrRc);
                return true;
            }
            return true;
        } else if (event.action == MouseAction.Move && draggingPlayhead_) {
            handleSeekAtX(event.x, arrRc);
            return true;
        } else if (event.action == MouseAction.ButtonUp && draggingPlayhead_) {
            draggingPlayhead_ = false;
            return true;
        }
        return super.onMouseEvent(event);
    }

    override void onDraw(DrawBuf buf) {
        if (visibility != Visibility.Visible) return;
        drawTimelineCanvas(buf, _pos, model_, pixelsPerSec, scrollSec, font);
    }
}

/// Renders the Y2K DAW Timeline / Arrangement canvas into `buf` within `rc`.
/// Separated from `TimelineCanvasWidget.onDraw` so headless `ColorDrawBuf` tests can run without `Platform.instance`.
void drawTimelineCanvas(
    DrawBuf buf,
    Rect rc,
    const(TransportModel)* model,
    double pixelsPerSec = 44.0,
    double scrollSec = 0.0,
    FontRef fnt = FontRef.init
) {
    buf.fillRect(rc, Y2kColor.BG_WORKSPACE);

    Rect well = Rect(rc.left + 8, rc.top + 8, rc.right - 8, rc.bottom - 8);
    drawBevelPanel(buf, well, Y2kColor.BG_PANEL_DARK, true);
    if (well.width <= TimelineCanvasWidget.HEADER_WIDTH + 16 ||
        well.height <= TimelineCanvasWidget.RULER_HEIGHT + 16) {
        return;
    }

    Rect arrRc = Rect(well.left + TimelineCanvasWidget.HEADER_WIDTH, well.top, well.right, well.bottom);

    // Top-left corner header & ruler bar
    Rect cornerRc = Rect(well.left + 1, well.top + 1, arrRc.left, well.top + TimelineCanvasWidget.RULER_HEIGHT);
    drawBevelPanel(buf, cornerRc, Y2kColor.BG_PANEL, false);
    Rect rulerRc = Rect(arrRc.left, well.top + 1, well.right - 1, well.top + TimelineCanvasWidget.RULER_HEIGHT);
    drawBevelPanel(buf, rulerRc, Y2kColor.BG_LCD, true);

    // Loop region highlight on ruler
    if (model && model.loopEnabled && model.loopEndSec > model.loopStartSec) {
        int lx0 = secToPixelX(model.loopStartSec, pixelsPerSec, arrRc.left, scrollSec);
        int lx1 = secToPixelX(model.loopEndSec, pixelsPerSec, arrRc.left, scrollSec);
        if (lx0 < rulerRc.left) lx0 = rulerRc.left;
        if (lx1 > rulerRc.right) lx1 = rulerRc.right;
        if (lx1 > lx0) {
            buf.fillRect(Rect(lx0, rulerRc.top + 2, lx1, rulerRc.top + 8), Y2kColor.ACCENT_PLAY);
        }
    }

    // Beat & bar grid
    const double bpm = (model && model.bpm > 0.0) ? model.bpm : 140.0;
    const double beatSec = beatDurationSec(bpm);
    const int trackCount = (model && model.tracks.length > 0) ? cast(int)model.tracks.length : 4;
    const int lanesBottom = well.top + TimelineCanvasWidget.RULER_HEIGHT + trackCount * TimelineCanvasWidget.LANE_HEIGHT;
    const int gridBottom = lanesBottom < well.bottom - 1 ? lanesBottom : well.bottom - 1;

    for (int beatIdx = 0; ; ++beatIdx) {
        double t = beatIdx * beatSec;
        int gx = secToPixelX(t, pixelsPerSec, arrRc.left, scrollSec);
        if (gx >= arrRc.right - 1) break;
        if (gx < arrRc.left) continue;
        const bool isBar = (beatIdx % 4) == 0;
        const uint gridCol = isBar ? 0x003A404C : 0x00282C34;
        buf.fillRect(Rect(gx, rulerRc.bottom, gx + 1, gridBottom), gridCol);
        buf.fillRect(Rect(gx, isBar ? rulerRc.top + 8 : rulerRc.bottom - 6, gx + 1, rulerRc.bottom),
                     isBar ? Y2kColor.FG_LCD_CYAN : Y2kColor.FG_MUTED);
        if (isBar && fnt !is null && gx + 20 < rulerRc.right) {
            import std.utf : toUTF32;
            fnt.drawText(buf, gx + 3, rulerRc.top + 5, toUTF32(format("%d", (beatIdx / 4) + 1)),
                         Y2kColor.FG_LCD_CYAN);
        }
    }

    // Track headers + lanes + clips
    foreach (i; 0 .. trackCount) {
        int y0 = well.top + TimelineCanvasWidget.RULER_HEIGHT + i * TimelineCanvasWidget.LANE_HEIGHT;
        int y1 = y0 + TimelineCanvasWidget.LANE_HEIGHT;
        if (y0 >= well.bottom - 4) break;
        if (y1 > well.bottom - 1) y1 = well.bottom - 1;

        Rect hdrRc = Rect(well.left + 1, y0, arrRc.left, y1);
        drawBevelPanel(buf, hdrRc, Y2kColor.BG_PANEL, false);

        const bool hasTrack = (model && i < model.tracks.length);
        string title = hasTrack ? model.tracks[i].name : format("Track %d", i + 1);
        if (fnt !is null) {
            import std.utf : toUTF32;
            fnt.drawText(buf, hdrRc.left + 8, hdrRc.top + 8,
                         toUTF32(truncateLabel(title, 14)), Y2kColor.FG_TEXT);
        }

        // Lane background
        const uint laneBg = (i % 2 == 0) ? 0x001E2228 : 0x0023272F;
        buf.fillRect(Rect(arrRc.left, y0, arrRc.right - 1, y1 - 1), laneBg);
        buf.fillRect(Rect(arrRc.left, y1 - 1, arrRc.right - 1, y1), Y2kColor.BEVEL_DARK);

        if (hasTrack) {
            foreach (ref clip; model.tracks[i].clips) {
                drawTimelineClip(buf, arrRc, y0 + 4, y1 - 4, clip, pixelsPerSec, scrollSec, fnt);
            }
        }
    }

    // Playhead line + cap
    const double phSec = model ? model.playheadPosSec : 0.0;
    int phX = secToPixelX(phSec, pixelsPerSec, arrRc.left, scrollSec);
    if (phX >= arrRc.left && phX < arrRc.right - 1) {
        buf.fillRect(Rect(phX - 4, rulerRc.top + 1, phX + 5, rulerRc.top + 10), Y2kColor.FG_LCD_AMBER);
        buf.fillRect(Rect(phX, rulerRc.top + 1, phX + 2, well.bottom - 1), Y2kColor.FG_LCD_AMBER);
    }
}

private void drawTimelineClip(
    DrawBuf buf,
    Rect arrRc,
    int topY,
    int botY,
    ref const(TimelineClipState) clip,
    double pixelsPerSec,
    double scrollSec,
    FontRef fnt
) {
    int cx0 = secToPixelX(clip.startTimeSec, pixelsPerSec, arrRc.left, scrollSec);
    int cx1 = secToPixelX(clip.startTimeSec + clip.durationSec, pixelsPerSec, arrRc.left, scrollSec);
    if (cx1 <= arrRc.left || cx0 >= arrRc.right - 1) return;
    if (cx0 < arrRc.left) cx0 = arrRc.left;
    if (cx1 > arrRc.right - 1) cx1 = arrRc.right - 1;
    if (cx1 - cx0 < 4 || botY - topY < 10) return;

    Rect clipRc = Rect(cx0, topY, cx1, botY);
    drawBevelPanel(buf, clipRc, 0x002C4A46, false);
    buf.fillRect(Rect(cx0 + 1, topY + 1, cx1 - 1, topY + 13), 0x003B6B64);

    if (fnt !is null && cx1 - cx0 > 24) {
        import std.utf : toUTF32;
        fnt.drawText(buf, cx0 + 4, topY + 1, toUTF32(truncateLabel(clip.name, 14)), Y2kColor.FG_TEXT);
    }

    // Waveform peak bars
    if (clip.waveform.length > 0 && botY - (topY + 14) >= 6) {
        const int wfTop = topY + 15;
        const int wfBot = botY - 2;
        const int midY = (wfTop + wfBot) / 2;
        const int halfH = (wfBot - wfTop) / 2;
        const int w = cx1 - cx0 - 2;
        foreach (px; 0 .. w) {
            size_t bucket = (cast(size_t)px * clip.waveform.length) / cast(size_t)w;
            float peak = clip.waveform[bucket];
            if (peak < 0.0f) peak = -peak;
            if (peak > 1.0f) peak = 1.0f;
            int barH = cast(int)(peak * halfH);
            if (barH < 1) barH = 1;
            buf.fillRect(Rect(cx0 + 1 + px, midY - barH, cx0 + 2 + px, midY + barH), Y2kColor.FG_LCD_CYAN);
        }
    }
}

unittest {
    import dlangui.graphics.drawbuf : ColorDrawBuf;

    // 1. Coordinate math tests
    assert(secToPixelX(0.0, 40.0, 136) == 136);
    assert(secToPixelX(2.5, 40.0, 136) == 236);
    assert(pixelXToSec(236, 40.0, 136) == 2.5);
    assert(pixelXToSec(10, 40.0, 136) == 0.0); // clamped >= 0
    assert(beatDurationSec(120.0) == 0.5);

    // 2. Headless DrawBuf rendering of TimelineCanvasWidget (grid, loop region, clip, playhead)
    TransportModel model;
    model.bpm = 120.0;
    model.playheadPosSec = 1.0;
    model.loopEnabled = true;
    model.loopStartSec = 0.5;
    model.loopEndSec = 2.5;
    auto tIdx = model.ensureTrackIndex(0);
    TimelineClipState clip;
    clip.clipIndex = 0;
    clip.name = "Lead";
    clip.startTimeSec = 0.0;
    clip.durationSec = 2.0;
    clip.waveform = [0.2f, 0.8f, 0.4f, 0.9f];
    model.tracks[tIdx].clips ~= clip;

    auto buf = new ColorDrawBuf(800, 300);
    drawTimelineCanvas(buf, Rect(0, 0, 800, 300), &model, 44.0, 0.0);

    // Playhead at t=1.0s => x = 8 + 136 + 44 = 188
    const int phX = secToPixelX(1.0, 44.0, 8 + TimelineCanvasWidget.HEADER_WIDTH);
    assert(buf.scanLine(12)[phX] == Y2kColor.FG_LCD_AMBER);
    // Track header panel should have BEVEL_LIGHT at top-left of lane 0 (8+1, 8+24)
    assert(buf.scanLine(32)[9] == Y2kColor.BEVEL_LIGHT);
}
