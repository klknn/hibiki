module gui_d.timeline_view;

import dlangui;
import dlangui.graphics.drawbuf : DrawBuf;
import dlangui.graphics.fonts : FontWeight;
import dlangui.widgets.widget : Visibility, Widget;
import gui_d.icons : DawIcon, drawDawIcon;
import gui_d.theme : adjustColorBrightness, dp, dpf, drawBeveledTrackCard, drawCard, drawCenteredText,
    drawPlayheadTriangle, drawThickLine, drawVCenteredText, FlPalette, getDawFont, getFlPalette,
    safeWidgetFont, truncateLabel, uiScale;
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

/// DPI-scaled Playlist layout shared identically between `drawTimelineCanvas` and `TimelineCanvasWidget`.
struct TimelineMetrics {
    int toolbarH;
    int pickerW;
    int headerW;
    int rulerH;
    int laneH;
    int contentTop;
    int lanesTop;
    bool hasPicker;
    Rect pkrRc;
    int tracksLeft;
    Rect arrRc;
    Rect pickToggleRc;
    Rect[8] toolBtns;
    Rect zoomOutRc;
    Rect zoomInRc;
}

TimelineMetrics computeTimelineMetrics(Rect rc, const(TransportModel)* model) nothrow @safe {
    TimelineMetrics m;
    m.toolbarH = dp(TOOLBAR_H);
    m.pickerW = dp(PICKER_W);
    m.headerW = dp(HEADER_W);
    m.rulerH = dp(RULER_H);
    m.laneH = dp(LANE_H);
    m.contentTop = rc.top + m.toolbarH;
    m.lanesTop = m.contentTop + m.rulerH;

    const bool showPicker = model ? model.showClipPicker : true;
    m.hasPicker = showPicker && rc.width > m.pickerW + m.headerW + dp(140);
    m.pkrRc = Rect(rc.left, m.contentTop, rc.left + m.pickerW, rc.bottom);
    m.tracksLeft = m.hasPicker ? m.pkrRc.right : rc.left;
    m.arrRc = Rect(m.tracksLeft + m.headerW, m.contentTop, rc.right, rc.bottom);

    m.pickToggleRc = Rect(rc.left + dp(6), rc.top + dp(4), rc.left + dp(32), m.contentTop - dp(4));
    foreach (i; 0 .. 8) {
        const int bx0 = rc.left + dp(38 + cast(int)i * 24);
        m.toolBtns[i] = Rect(bx0, rc.top + dp(4), bx0 + dp(24), m.contentTop - dp(4));
    }
    m.zoomOutRc = Rect(rc.right - dp(54), rc.top + dp(4), rc.right - dp(30), m.contentTop - dp(4));
    m.zoomInRc = Rect(rc.right - dp(30), rc.top + dp(4), rc.right - dp(6), m.contentTop - dp(4));
    return m;
}

private struct ToolButtonDef {
    EditorTool tool;
    DawIcon icon;
    string hint;
}

private immutable ToolButtonDef[8] PLAYLIST_TOOLS = [
    ToolButtonDef(EditorTool.DRAW,   DawIcon.TOOL_DRAW,   "Draw (P) — click to add/move clip"),
    ToolButtonDef(EditorTool.PAINT,  DawIcon.TOOL_PAINT,  "Paint (B) — click or drag to paint clips"),
    ToolButtonDef(EditorTool.DELETE, DawIcon.TOOL_DELETE, "Delete (D) — click clip to remove"),
    ToolButtonDef(EditorTool.MUTE,   DawIcon.TOOL_MUTE,   "Mute (T) — click clip to mute/unmute"),
    ToolButtonDef(EditorTool.SLIP,   DawIcon.TOOL_SLIP,   "Slip (S) — slip clip content"),
    ToolButtonDef(EditorTool.SLICE,  DawIcon.TOOL_SLICE,  "Slice (C) — click clip to split in two"),
    ToolButtonDef(EditorTool.SELECT, DawIcon.TOOL_SELECT, "Select (E) — select clips"),
    ToolButtonDef(EditorTool.ZOOM,   DawIcon.TOOL_ZOOM,   "Zoom (Z) — zoom arrangement"),
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
    if (rc.width < dp(220) || rc.height < dp(90)) return;

    const m = computeTimelineMetrics(rc, model);
    const int bw = dp(1);
    FontRef boldFnt = getDawFont(11, fnt, FontWeight.Normal);
    FontRef uiFnt = getDawFont(10, fnt, FontWeight.Normal);
    FontRef smallFnt = getDawFont(10, fnt, FontWeight.Normal);

    // 1. Top Playlist Toolbar
    Rect tbRc = Rect(rc.left, rc.top, rc.right, m.contentTop);
    buf.fillRect(tbRc, pal.bgPanelAlt);
    buf.fillRect(Rect(tbRc.left, tbRc.bottom - bw, tbRc.right, tbRc.bottom), pal.borderStrong);

    const bool showPicker = model ? model.showClipPicker : true;
    drawCard(buf, m.pickToggleRc, showPicker ? pal.accentOrange : pal.bgControl, COLOR_TRANSPARENT, 1.5f);
    drawDawIcon(buf, Rect(m.pickToggleRc.left + dp(2), m.pickToggleRc.top + dp(2),
                          m.pickToggleRc.right - dp(2), m.pickToggleRc.bottom - dp(2)),
                DawIcon.CLIP_PICKER, showPicker ? pal.bgLcd : pal.fgText,
                showPicker ? adjustColorBrightness(pal.bgLcd, 28) : pal.fgMuted);

    // Unified flush 8-tool icon strip (zero gaps between tools)
    Rect toolStripRc = Rect(m.toolBtns[0].left, m.toolBtns[0].top,
                            m.toolBtns[7].right, m.toolBtns[7].bottom);
    drawCard(buf, toolStripRc, pal.bgControl, COLOR_TRANSPARENT, 1.5f);
    foreach (i, const ref td; PLAYLIST_TOOLS) {
        Rect bRc = m.toolBtns[i];
        const bool active = model && model.playlistTool == td.tool;
        if (active) {
            drawCard(buf, bRc, pal.accentOrange, COLOR_TRANSPARENT, 1.5f);
        }
        drawDawIcon(buf, Rect(bRc.left + dp(2), bRc.top + dp(2), bRc.right - dp(2), bRc.bottom - dp(2)),
                    td.icon, active ? pal.bgLcd : pal.fgText,
                    active ? adjustColorBrightness(pal.bgLcd, 28) : pal.fgMuted);
    }

    // Unified Zoom -/+ strip on right
    Rect zoomStripRc = Rect(m.zoomOutRc.left, m.zoomOutRc.top, m.zoomInRc.right, m.zoomInRc.bottom);
    drawCard(buf, zoomStripRc, pal.bgControl, COLOR_TRANSPARENT, 1.5f);
    drawDawIcon(buf, Rect(m.zoomOutRc.left + dp(2), m.zoomOutRc.top + dp(2), m.zoomOutRc.right - dp(2), m.zoomOutRc.bottom - dp(2)),
                DawIcon.MINUS, pal.fgText);
    drawDawIcon(buf, Rect(m.zoomInRc.left + dp(2), m.zoomInRc.top + dp(2), m.zoomInRc.right - dp(2), m.zoomInRc.bottom - dp(2)),
                DawIcon.PLUS, pal.fgText);

    // 2. Optional Left Clip Picker Panel (Pattern / Audio / Automation)
    const int contentTop = m.contentTop;
    const int tracksLeft = m.tracksLeft;
    if (m.hasPicker) {
        Rect pkrRc = m.pkrRc;
        buf.fillRect(pkrRc, pal.bgPanelAlt);
        buf.fillRect(Rect(pkrRc.right - bw, pkrRc.top, pkrRc.right, pkrRc.bottom), pal.borderStrong);

        // 3 Kind Tabs: Pattern / Audio / Automation (flush icon strip)
        static immutable DawIcon[3] K_ICONS = [
            DawIcon.KIND_PATTERN, DawIcon.KIND_AUDIO, DawIcon.KIND_AUTOMATION
        ];
        static immutable PlaylistClipKind[3] KINDS = [
            PlaylistClipKind.PATTERN, PlaylistClipKind.AUDIO, PlaylistClipKind.AUTOMATION
        ];
        const int tabW = (m.pickerW - dp(8)) / 3;
        Rect ktStrip = Rect(pkrRc.left + dp(4), pkrRc.top + dp(4),
                            pkrRc.left + dp(4) + tabW * 3, pkrRc.top + dp(24));
        drawCard(buf, ktStrip, pal.bgControl, COLOR_TRANSPARENT, 1.5f);
        foreach (k; 0 .. 3) {
            Rect ktRc = Rect(pkrRc.left + dp(4) + k * tabW, pkrRc.top + dp(4),
                             pkrRc.left + dp(4) + (k + 1) * tabW, pkrRc.top + dp(24));
            const bool kAct = model && model.activePickerKind == KINDS[k];
            if (kAct) {
                drawCard(buf, ktRc, pal.accentOrange, COLOR_TRANSPARENT, 1.5f);
            }
            drawDawIcon(buf, Rect(ktRc.left + dp(2), ktRc.top + dp(2), ktRc.right - dp(2), ktRc.bottom - dp(2)),
                        K_ICONS[k], kAct ? pal.bgLcd : pal.fgText);
        }

        // Picker entries
        const PlaylistClipKind curKind = model ? model.activePickerKind : PlaylistClipKind.PATTERN;
        const int curIdx = model ? model.activePickerIndex : 0;
        const int rowStep = dp(26);
        const int itemH = dp(22);
        if (curKind == PlaylistClipKind.PATTERN && model) {
            foreach (i, const ref pat; model.patterns) {
                const int py = pkrRc.top + dp(30) + cast(int)i * rowStep;
                if (py + itemH > pkrRc.bottom) break;
                Rect itemRc = Rect(pkrRc.left + dp(6), py, pkrRc.right - dp(6), py + itemH);
                const bool sel = (cast(int)i == curIdx);
                drawCard(buf, itemRc, pat.color, sel ? pal.accentOrange : COLOR_TRANSPARENT, 2.0f);
                drawVCenteredText(buf, uiFnt, itemRc.left + dp(6), itemRc,
                                  toUTF32(truncateLabel(pat.name, 14)), pal.fgText);
            }
        } else if (curKind == PlaylistClipKind.AUDIO) {
            static immutable string[3] AUD_NAMES = ["loop140.wav", "bb140.wav", "808_sub.wav"];
            foreach (i, nm; AUD_NAMES) {
                const int py = pkrRc.top + dp(30) + cast(int)i * rowStep;
                Rect itemRc = Rect(pkrRc.left + dp(6), py, pkrRc.right - dp(6), py + itemH);
                const bool sel = (cast(int)i == curIdx);
                drawCard(buf, itemRc, pal.clipAudio, sel ? pal.accentCyan : COLOR_TRANSPARENT, 2.0f);
                drawVCenteredText(buf, uiFnt, itemRc.left + dp(6), itemRc, toUTF32(nm), pal.fgText);
            }
        } else {
            static immutable string[2] AUT_NAMES = ["Acid Cutoff Env", "Master Volume Env"];
            foreach (i, nm; AUT_NAMES) {
                const int py = pkrRc.top + dp(30) + cast(int)i * rowStep;
                Rect itemRc = Rect(pkrRc.left + dp(6), py, pkrRc.right - dp(6), py + itemH);
                const bool sel = (cast(int)i == curIdx);
                drawCard(buf, itemRc, pal.clipAutomation, sel ? pal.accentPurple : COLOR_TRANSPARENT, 2.0f);
                drawVCenteredText(buf, uiFnt, itemRc.left + dp(6), itemRc, toUTF32(nm), pal.fgText);
            }
        }
    }

    // 3. Playlist Header Column & Arrangement Ruler
    Rect arrRc = m.arrRc;
    if (arrRc.width < dp(40)) return;

    Rect cornerRc = Rect(tracksLeft, contentTop, arrRc.left, m.lanesTop);
    buf.fillRect(cornerRc, pal.bgPanel);
    buf.fillRect(Rect(cornerRc.left, cornerRc.bottom - bw, cornerRc.right, cornerRc.bottom), pal.borderStrong);
    drawVCenteredText(buf, smallFnt, cornerRc.left + dp(8), cornerRc, "Arrangement 1"d, pal.fgMuted);

    Rect rulerRc = Rect(arrRc.left, contentTop, arrRc.right, m.lanesTop);
    buf.fillRect(rulerRc, pal.bgLcd);
    buf.fillRect(Rect(rulerRc.left, rulerRc.bottom - bw, rulerRc.right, rulerRc.bottom), pal.borderStrong);

    // Loop region highlight on ruler
    if (model && model.loopEnabled && model.loopEndSec > model.loopStartSec) {
        int lx0 = secToPixelX(model.loopStartSec, pixelsPerSec, arrRc.left, scrollSec);
        int lx1 = secToPixelX(model.loopEndSec, pixelsPerSec, arrRc.left, scrollSec);
        if (lx0 < rulerRc.left) lx0 = rulerRc.left;
        if (lx1 > rulerRc.right) lx1 = rulerRc.right;
        if (lx1 > lx0) {
            buf.fillRect(Rect(lx0, rulerRc.top + bw, lx1, rulerRc.top + dp(6)), pal.accentRed);
        }
    }

    // 4. Beat & Bar Grid + Alternating 4-Bar Blocks
    const double bpm = (model && model.bpm > 0.0) ? model.bpm : 140.0;
    const double beatSec = beatDurationSec(bpm);
    size_t[] laneTracks;
    if (model) {
        foreach (idx, ref t; model.tracks) {
            if (t.index != 0) laneTracks ~= idx;
        }
    }
    const int trackCount = laneTracks.length > 0 ? cast(int)laneTracks.length : 6;
    const int lanesTop = m.lanesTop;

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

    // Track headers + lane dividers
    foreach (laneIdx; 0 .. trackCount) {
        const int y0 = lanesTop + laneIdx * m.laneH;
        int y1 = y0 + m.laneH;
        if (y0 >= rc.bottom - dp(4)) break;
        if (y1 > rc.bottom) y1 = rc.bottom;

        // Track Header (zero gap between tracks; top-right/bottom-left rounded + bevel gradient separation)
        Rect hdrRc = Rect(tracksLeft, y0, arrRc.left, y1);
        drawBeveledTrackCard(buf, hdrRc, pal.bgPanel, pal.bgToolbarWell, 3.0f);

        const bool hasTrack = (model !is null && laneIdx < cast(int)laneTracks.length);
        const size_t tIdx = hasTrack ? laneTracks[laneIdx] : 0;
        const uint trkCol = hasTrack ? model.tracks[tIdx].color : pal.clipPattern;
        const string title = hasTrack ? model.tracks[tIdx].name : format("Track %d", laneIdx + 1);
        const bool muted = hasTrack ? model.tracks[tIdx].muted : false;
        const bool soloed = hasTrack ? model.tracks[tIdx].soloed : false;

        // Track Color Bar on left (inset inside top/bottom bevel)
        buf.fillRect(Rect(hdrRc.left + dp(1), hdrRc.top + dp(3), hdrRc.left + dp(5), hdrRc.bottom - dp(4)), trkCol);

        // Mute LED on right of header
        Rect mLed = Rect(hdrRc.right - dp(20), hdrRc.top + dp(8), hdrRc.right - dp(8), hdrRc.top + dp(20));
        drawCard(buf, mLed, soloed ? pal.accentAmber : (muted ? pal.ledOff : pal.accentGreen),
                 COLOR_TRANSPARENT, 1.5f);

        drawVCenteredText(buf, uiFnt, hdrRc.left + dp(10), hdrRc,
                          toUTF32(truncateLabel(title, 12)),
                          muted ? pal.fgDim : pal.fgText);

        // Subtle low-contrast lane seam (zero gap between lanes)
        buf.fillRect(Rect(arrRc.left, y1 - bw, arrRc.right, y1), adjustColorBrightness(pal.bgWindow, -5));
    }

    // Vertical Bar/Beat Grid Lines + Ruler Labels
    for (int beatIdx = 0; ; ++beatIdx) {
        const double t = beatIdx * beatSec;
        const int gx = secToPixelX(t, pixelsPerSec, arrRc.left, scrollSec);
        if (gx >= arrRc.right) break;
        if (gx < arrRc.left) continue;
        const bool isBar = (beatIdx % 4) == 0;
        buf.fillRect(Rect(gx, lanesTop, gx + bw, rc.bottom), isBar ? pal.borderSubtle : pal.borderStrong);
        buf.fillRect(Rect(gx, isBar ? rulerRc.top + dp(8) : rulerRc.bottom - dp(6), gx + bw, rulerRc.bottom),
                     isBar ? pal.accentCyan : pal.fgDim);
        if (isBar && gx + dp(16) < rulerRc.right) {
            drawVCenteredText(buf, smallFnt, gx + dp(3), rulerRc,
                              toUTF32(format("%d", (beatIdx / 4) + 1)), pal.fgText);
        }
    }

    // Time Markers on Ruler
    if (model) {
        foreach (const ref mk; model.timeMarkers) {
            const int mx = secToPixelX(mk.timeSec, pixelsPerSec, arrRc.left, scrollSec);
            if (mx < arrRc.left || mx >= arrRc.right - dp(20)) continue;
            buf.fillRect(Rect(mx, rulerRc.top + bw, mx + dp(2), rulerRc.bottom), mk.color);
            drawVCenteredText(buf, smallFnt, mx + dp(4),
                              Rect(mx, rulerRc.top, arrRc.right, rulerRc.top + dp(14)),
                              toUTF32(mk.label), mk.color);
        }
    }

    // Draw Clips on Lanes
    if (model) {
        foreach (laneIdx, tIdx; laneTracks) {
            const int y0 = lanesTop + cast(int)laneIdx * m.laneH;
            int y1 = y0 + m.laneH;
            if (y0 >= rc.bottom - dp(4)) break;
            if (y1 > rc.bottom) y1 = rc.bottom;
            foreach (const ref clip; model.tracks[tIdx].clips) {
                drawPlaylistClip(buf, arrRc, y0, y1, clip, pal, pixelsPerSec, scrollSec, smallFnt);
            }
        }
    }

    // Playhead cap & vertical line
    const double phSec = model ? model.playheadPosSec : 0.0;
    const int phX = secToPixelX(phSec, pixelsPerSec, arrRc.left, scrollSec);
    if (phX >= arrRc.left && phX < arrRc.right) {
        drawPlayheadTriangle(buf, phX, rulerRc.top + dp(2), dp(6), dp(9), pal.accentOrange);
        buf.fillRect(Rect(phX, rulerRc.top + dp(8), phX + dp(2), rc.bottom), pal.accentOrange);
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
    if (cx1 - cx0 < dp(6) || botY - topY < dp(12)) return;

    uint baseCol = clip.color;
    if (baseCol == 0) {
        if (clip.kind == PlaylistClipKind.PATTERN) baseCol = pal.clipPattern;
        else if (clip.kind == PlaylistClipKind.AUDIO) baseCol = pal.clipAudio;
        else baseCol = pal.clipAutomation;
    }
    if (clip.muted) baseCol = pal.bgPanelAlt;

    Rect clipRc = Rect(cx0, topY, cx1, botY);
    const uint clipBodyCol = (baseCol & 0x00FFFFFF) | 0x62000000;
    const uint clipBorderCol = clip.muted
        ? ((pal.fgDim & 0x00FFFFFF) | 0x48000000)
        : ((adjustColorBrightness(baseCol, -18) & 0x00FFFFFF) | 0x48000000);
    drawCard(buf, clipRc, clipBodyCol, clipBorderCol, 2.5f);

    // Opaque clip header bar for high readability while body reveals background grid
    const int hdrH = dp(15);
    const int hdrBot = (topY + hdrH < botY) ? (topY + hdrH) : botY;
    Rect hdrBarRc = Rect(cx0 + dp(1), topY + dp(1), cx1 - dp(1), hdrBot);
    const uint hdrCol = (clip.muted ? pal.bgPanel : adjustColorBrightness(baseCol, 18)) & 0x00FFFFFF;
    buf.fillRect(hdrBarRc, hdrCol);
    if (cx1 - cx0 > dp(28)) {
        drawVCenteredText(buf, fnt, cx0 + dp(5), hdrBarRc,
                          toUTF32(truncateLabel(clip.name, 16)),
                          clip.muted ? pal.fgDim : pal.fgText);
    }

    const int bodyTop = hdrBot + dp(2);
    const int bodyBot = botY - dp(3);
    const int padX = dp(3);
    const int bodyW = cx1 - cx0 - padX * 2;
    if (bodyBot - bodyTop < dp(6) || bodyW < dp(8)) return;

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
            buf.fillRect(Rect(cx0 + padX + px, midY - barH, cx0 + padX + px + 1, midY + barH),
                         clip.muted ? pal.fgDim : pal.accentCyan);
        }
    } else if (clip.kind == PlaylistClipKind.AUTOMATION) {
        // Draw smooth automation envelope curve + control nodes
        const int h = bodyBot - bodyTop;
        const int nodeR = dp(2);
        if (clip.autoPoints.length >= 2) {
            foreach (i; 0 .. clip.autoPoints.length - 1) {
                const auto p0 = clip.autoPoints[i];
                const auto p1 = clip.autoPoints[i + 1];
                const float x0 = cx0 + padX + cast(float)(p0.normTime * bodyW);
                const float y0 = bodyBot - p0.value * h;
                const float x1 = cx0 + padX + cast(float)(p1.normTime * bodyW);
                const float y1 = bodyBot - p1.value * h;
                drawThickLine(buf, x0, y0, x1, y1, dpf(1.8f),
                              clip.muted ? pal.fgDim : pal.accentAmber);
                buf.fillRect(Rect(cast(int)x0 - nodeR, cast(int)y0 - nodeR,
                                  cast(int)x0 + nodeR + 1, cast(int)y0 + nodeR + 1), pal.fgText);
            }
            const auto plast = clip.autoPoints[$ - 1];
            const int lx = cx0 + padX + cast(int)(plast.normTime * bodyW);
            const int ly = bodyBot - cast(int)(plast.value * h);
            buf.fillRect(Rect(lx - nodeR, ly - nodeR, lx + nodeR + 1, ly + nodeR + 1), pal.fgText);
        }
    } else if (clip.kind == PlaylistClipKind.PATTERN) {
        // Mini piano-roll / step preview bars inside pattern clip
        const uint noteCol = clip.muted ? pal.fgDim : pal.noteFill;
        const int spanY = bodyBot - bodyTop - dp(3);
        if (spanY > 0) {
            foreach (k; 0 .. 8) {
                const int nx0 = cx0 + dp(4) + (k * bodyW) / 8;
                const int nx1 = nx0 + (bodyW / 12) + dp(2);
                const int ny = bodyTop + (dp(k * 5 + clip.patternIndex * 3) % spanY);
                if (nx1 < cx1 - dp(2)) {
                    buf.fillRect(Rect(nx0, ny, nx1, ny + dp(3)), noteCol);
                }
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

    private double effectivePxPerSec() const @safe {
        return pixelsPerSec * uiScale();
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
        double sec = pixelXToSec(x, effectivePxPerSec(), arrRc.left, scrollSec);
        if (model_) model_.playheadPosSec = sec;
        if (onSeek !is null) onSeek(sec);
        invalidate();
    }

    override bool onMouseEvent(MouseEvent event) {
        if (event.action == MouseAction.FocusOut) {
            return dragMode_ != DragMode.NONE;
        }
        if (!model_) return super.onMouseEvent(event);

        const m = computeTimelineMetrics(_pos, model_);
        Rect arrRc = m.arrRc;
        const int contentTop = m.contentTop;
        const int lanesTop = m.lanesTop;
        const double effPxPerSec = effectivePxPerSec();

        if (event.action == MouseAction.Wheel) {
            if ((event.flags & MouseFlag.Control) != 0) {
                pixelsPerSec *= (event.wheelDelta > 0) ? 1.15 : 0.87;
                if (pixelsPerSec < 16.0) pixelsPerSec = 16.0;
                if (pixelsPerSec > 220.0) pixelsPerSec = 220.0;
            } else {
                scrollSec -= event.wheelDelta * 0.45;
                if (scrollSec < 0.0) scrollSec = 0.0;
            }
            invalidate();
            return true;
        }

        if (event.action == MouseAction.ButtonDown) {
            activateParentWindow(this);

            // 1. Toolbar clicks
            if (event.y >= _pos.top && event.y < contentTop) {
                if (m.pickToggleRc.isPointInside(event.x, event.y)) {
                    model_.showClipPicker = !model_.showClipPicker;
                    invalidate();
                    return true;
                }
                foreach (i, const ref td; PLAYLIST_TOOLS) {
                    if (m.toolBtns[i].isPointInside(event.x, event.y)) {
                        model_.playlistTool = td.tool;
                        model_.hintText = "Playlist Tool: " ~ td.hint;
                        invalidate();
                        return true;
                    }
                }
                if (m.zoomOutRc.isPointInside(event.x, event.y)) {
                    pixelsPerSec = (pixelsPerSec > 20.0) ? pixelsPerSec * 0.8 : 20.0;
                    invalidate();
                    return true;
                }
                if (m.zoomInRc.isPointInside(event.x, event.y)) {
                    pixelsPerSec = (pixelsPerSec < 180.0) ? pixelsPerSec * 1.25 : 180.0;
                    invalidate();
                    return true;
                }
                return true;
            }

            // 2. Left Clip Picker clicks (flush 3-icon strip)
            if (m.hasPicker && event.x < m.tracksLeft) {
                const int tabW = (m.pickerW - dp(8)) / 3;
                if (event.y < contentTop + dp(26)) {
                    static immutable PlaylistClipKind[3] KINDS = [
                        PlaylistClipKind.PATTERN, PlaylistClipKind.AUDIO, PlaylistClipKind.AUTOMATION
                    ];
                    foreach (k; 0 .. 3) {
                        const int x0 = _pos.left + dp(4) + k * tabW;
                        if (event.x >= x0 && event.x < x0 + tabW) {
                            model_.activePickerKind = KINDS[k];
                            model_.activePickerIndex = 0;
                            invalidate();
                            return true;
                        }
                    }
                    return true;
                }
                const int itemIdx = (event.y - (contentTop + dp(30))) / dp(26);
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
                size_t[] laneTracks;
                foreach (idx, ref t; model_.tracks) {
                    if (t.index != 0) laneTracks ~= idx;
                }
                const int laneIdx = (event.y - lanesTop) / m.laneH;
                if (laneIdx < 0 || laneIdx >= cast(int)laneTracks.length) return true;
                const int tIdx = cast(int)laneTracks[laneIdx];
                auto trk = &model_.tracks[tIdx];

                // Track header mute LED
                if (event.x < arrRc.left) {
                    Rect mLed = Rect(arrRc.left - dp(22), lanesTop + laneIdx * m.laneH + dp(6),
                                     arrRc.left - dp(6), lanesTop + laneIdx * m.laneH + dp(22));
                    if (mLed.isPointInside(event.x, event.y)) {
                        if (event.button == MouseButton.Right) trk.soloed = !trk.soloed;
                        else trk.muted = !trk.muted;
                    } else {
                        model_.selectedTrack = trk.index;
                    }
                    invalidate();
                    return true;
                }

                if (model_.playlistTool == EditorTool.ZOOM) {
                    pixelsPerSec *= (event.button == MouseButton.Right) ? 0.8 : 1.25;
                    if (pixelsPerSec < 16.0) pixelsPerSec = 16.0;
                    if (pixelsPerSec > 220.0) pixelsPerSec = 220.0;
                    invalidate();
                    return true;
                }

                // Arrangement lane click
                const double clickSec = pixelXToSec(event.x, effPxPerSec, arrRc.left, scrollSec);
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
                            rightHalf.clipIndex = model_.nextClipIndex(*trk);
                            rightHalf.startTimeSec = splitSec;
                            rightHalf.durationSec = (clip.startTimeSec + clip.durationSec) - splitSec;
                            clip.durationSec = splitSec - clip.startTimeSec;
                            trk.clips ~= rightHalf;
                            model_.hintText = "Sliced clip cleanly";
                            invalidate();
                        }
                        return true;
                    }
                    // Check right-edge resize (within dp(8) of clip right edge)
                    const int cx1 = secToPixelX(clip.startTimeSec + clip.durationSec, effPxPerSec, arrRc.left, scrollSec);
                    if (event.x >= cx1 - dp(8)) {
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
                        nc.clipIndex = model_.nextClipIndex(*trk);
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
                const double curSec = pixelXToSec(event.x, effPxPerSec, arrRc.left, scrollSec);
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

        if ((event.action == MouseAction.ButtonUp || event.action == MouseAction.Cancel) &&
            dragMode_ != DragMode.NONE) {
            dragMode_ = DragMode.NONE;
            dragTrackArrIdx_ = -1;
            dragClipArrIdx_ = -1;
            return true;
        }

        return super.onMouseEvent(event);
    }

    override void onDraw(DrawBuf buf) {
        if (visibility != Visibility.Visible) return;
        drawTimelineCanvas(buf, _pos, model_, effectivePxPerSec(), scrollSec, safeWidgetFont(this));
    }
}

unittest {
    import dlangui.graphics.drawbuf : ColorDrawBuf;
    import dlangui.widgets.styles : currentTheme, createDefaultTheme;

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

    // Regression 1: Bright clip color 0x00FF8F2D must not overflow into alpha byte (0x01...)
    model.tracks[1].clips[0].color = 0x00FF8F2D;
    drawTimelineCanvas(buf, Rect(0, 0, 900, 360), &model, 52.0, 0.0);
    const int clipHdrY = TOOLBAR_H + RULER_H + 6;
    const uint hdrPx = buf.scanLine(clipHdrY)[PICKER_W + HEADER_W + 10];
    assert((hdrPx >> 24) == 0, "Expected opaque clip header pixel (alpha == 0 in dlangui), no overflow");

    // Regression 2: Sparse tracks (e.g. after clear_project + ensureTrackIndex(3)) render & click on lane 0 without OOB
    model.tracks.length = 0;
    const idx3 = model.ensureTrackIndex(3);
    model.tracks[idx3].name = "Lead";
    TimelineClipState sparseClip;
    sparseClip.clipIndex = 0;
    sparseClip.name = "SparsePat";
    sparseClip.color = pal.clipPattern;
    sparseClip.startTimeSec = 0.0;
    sparseClip.durationSec = 4.0;
    model.tracks[idx3].clips ~= sparseClip;
    drawTimelineCanvas(buf, Rect(0, 0, 900, 360), &model, 52.0, 0.0);
    const uint sparsePx = buf.scanLine(TOOLBAR_H + RULER_H + 25)[PICKER_W + HEADER_W + 80];
    assert(sparsePx != pal.bgWindow, "Expected sparse track 3 clip to render on lane 0");
    // Beat 3 grid line at beat3X (~66..78px, clear of text & playhead) shines through translucent clip body
    const int beat3X = secToPixelX(3.0 * beatDurationSec(model.bpm), 52.0, PICKER_W + HEADER_W);
    const uint gridThroughBody = buf.scanLine(TOOLBAR_H + RULER_H + 25)[beat3X];
    assert(gridThroughBody != sparsePx, "Expected beat grid line to remain visible through translucent clip body");
    // Meanwhile opaque clip header completely covers underlying grid lines for legible text
    const uint hdrOverBg = buf.scanLine(TOOLBAR_H + RULER_H + 6)[PICKER_W + HEADER_W + 80];
    const uint hdrOverGrid = buf.scanLine(TOOLBAR_H + RULER_H + 6)[beat3X];
    assert(hdrOverBg == hdrOverGrid && (hdrOverBg >> 24) == 0,
           "Expected clip header bar to stay opaque across grid lines");

    // Widget interaction: SLICE creates unique clipIndex, ZOOM zooms, Cancel ends drag
    import gui_d.theme : destroyWidgetTree;
    if (currentTheme is null) currentTheme = createDefaultTheme();
    auto tl = new TimelineCanvasWidget("tl", &model);
    scope (exit) destroyWidgetTree(tl);
    tl.measure(900, 360);
    tl.layout(Rect(0, 0, 900, 360));
    model.playlistTool = EditorTool.SLICE;
    const int sliceX = secToPixelX(2.0, tl.pixelsPerSec, PICKER_W + HEADER_W);
    const int lane0Y = TOOLBAR_H + RULER_H + 20;
    tl.onMouseEvent(new MouseEvent(MouseAction.ButtonDown, MouseButton.Left, MouseFlag.LButton, cast(short)sliceX, cast(short)lane0Y));
    assert(model.tracks[idx3].clips.length == 2, "Expected SLICE tool to split clip into two");
    assert(model.tracks[idx3].clips[0].clipIndex != model.tracks[idx3].clips[1].clipIndex,
           "Expected sliced halves to have distinct clipIndex");

    const double prevZoom = tl.pixelsPerSec;
    model.playlistTool = EditorTool.ZOOM;
    tl.onMouseEvent(new MouseEvent(MouseAction.ButtonDown, MouseButton.Left, MouseFlag.LButton, cast(short)sliceX, cast(short)lane0Y));
    assert(tl.pixelsPerSec > prevZoom, "Expected EditorTool.ZOOM click to zoom in");
}
