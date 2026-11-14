module gui_d.toolbar;

import core.stdc.math : sinf;
import dlangui;
import dlangui.graphics.drawbuf : DrawBuf;
import dlangui.widgets.widget : Visibility, Widget;
import gui_d.theme : drawCard, drawFilledCircle, drawRotaryKnob, drawThickLine, FlPalette,
    formatBarBeat, formatTimecode, getFlPalette, truncateLabel;
import gui_d.transport_model : PlaybackMode, SnapMode, TimeDisplayMode, TransportModel;
import std.format : format;
import std.utf : toUTF32;

/// Returns a short display label for `SnapMode`.
string snapLabel(SnapMode s) pure nothrow @safe {
    final switch (s) {
        case SnapMode.LINE:     return "Line";
        case SnapMode.CELL:     return "Cell";
        case SnapMode.STEP_1_4: return "1/4 Step";
        case SnapMode.STEP_1_2: return "1/2 Step";
        case SnapMode.STEP:     return "Step";
        case SnapMode.BEAT_1_4: return "1/4 Beat";
        case SnapMode.BEAT_1_2: return "1/2 Beat";
        case SnapMode.BEAT:     return "Beat";
        case SnapMode.BAR:      return "Bar";
        case SnapMode.NONE:     return "None";
    }
}

/// Renders the FL Studio 2026 modular Transport / Pattern / Window / CPU Toolbar.
void drawToolbarCanvas(
    DrawBuf buf,
    Rect rc,
    const(TransportModel)* model,
    bool visPlaylist,
    bool visChannelRack,
    bool visPianoRoll,
    bool visBrowser,
    bool visMixer,
    bool visPlugin,
    FontRef fnt = FontRef.init
) {
    const pal = model ? model.palette() : getFlPalette();
    buf.fillRect(rc, pal.bgToolbar);
    buf.fillRect(Rect(rc.left, rc.bottom - 1, rc.right, rc.bottom), pal.borderStrong);
    if (!model || rc.width < 600 || rc.height < 42) return;

    const int top = rc.top + 4;
    const int bot = rc.bottom - 4;
    int x = rc.left + 6;

    // 1. Master Volume & Master Pitch Panel
    Rect masterPanel = Rect(x, top, x + 76, bot);
    drawCard(buf, masterPanel, pal.bgToolbarWell, pal.borderSubtle, 4.0f);
    drawRotaryKnob(buf, x + 22, masterPanel.middley, 12, model.masterVolume,
                   pal.bgLcd, pal.accentOrange, pal.bgControl, pal.borderSubtle);
    drawRotaryKnob(buf, x + 54, masterPanel.middley, 11, (model.masterPitch + 1.0f) * 0.5f,
                   pal.bgLcd, pal.accentCyan, pal.bgControl, pal.borderSubtle, true);
    x = masterPanel.right + 6;

    // 2. Transport Panel (PAT/SONG, Play, Stop, Rec, Loop, BPM LCD, Time LCD)
    Rect transPanel = Rect(x, top, x + 372, bot);
    drawCard(buf, transPanel, pal.bgToolbarWell, pal.borderSubtle, 4.0f);

    // PAT / SONG vertical switch
    Rect patSongRc = Rect(x + 6, top + 4, x + 50, bot - 4);
    const bool isPat = model.playbackMode == PlaybackMode.PAT;
    drawCard(buf, patSongRc, isPat ? pal.accentOrange : pal.accentGreen, pal.borderStrong, 3.0f);
    if (!fnt.isNull) {
        fnt.drawText(buf, patSongRc.left + 8, patSongRc.middley - 7,
                     isPat ? "PAT"d : "SONG"d, pal.bgLcd);
    }

    // Play / Stop / Rec / Loop
    Rect playRc = Rect(x + 56,  top + 5, x + 88,  bot - 5);
    Rect stopRc = Rect(x + 92,  top + 5, x + 122, bot - 5);
    Rect recRc  = Rect(x + 126, top + 5, x + 156, bot - 5);
    Rect loopRc = Rect(x + 160, top + 5, x + 194, bot - 5);

    drawCard(buf, playRc, model.isPlaying ? pal.accentGreen : pal.bgControl, pal.borderSubtle, 3.0f);
    drawCard(buf, stopRc, pal.bgControl, pal.borderSubtle, 3.0f);
    drawCard(buf, recRc, model.isRecording ? pal.accentRed : pal.bgControl, pal.borderSubtle, 3.0f);
    drawCard(buf, loopRc, model.loopEnabled ? pal.accentAmber : pal.bgControl, pal.borderSubtle, 3.0f);

    // Vector transport glyphs
    const uint playCol = model.isPlaying ? pal.bgLcd : pal.accentGreen;
    foreach (px; 0 .. 10) {
        const int halfH = ((10 - px) * 6) / 10;
        buf.fillRect(Rect(playRc.middlex - 4 + px, playRc.middley - halfH,
                          playRc.middlex - 3 + px, playRc.middley + halfH + 1), playCol);
    }
    buf.fillRect(Rect(stopRc.middlex - 5, stopRc.middley - 5, stopRc.middlex + 5, stopRc.middley + 5),
                 pal.fgText);
    const uint recCol = model.isRecording ? pal.bgLcd : pal.accentRed;
    drawFilledCircle(buf, recRc.middlex, recRc.middley, 5.0f, recCol);
    if (!fnt.isNull) {
        fnt.drawText(buf, loopRc.left + 4, loopRc.middley - 7, "LOOP"d,
                     model.loopEnabled ? pal.bgLcd : pal.fgMuted);
    }

    // BPM LCD (drag up/down)
    Rect bpmRc = Rect(x + 200, top + 4, x + 268, bot - 4);
    drawCard(buf, bpmRc, pal.bgLcd, pal.borderStrong, 3.0f);
    if (!fnt.isNull) {
        fnt.drawText(buf, bpmRc.left + 7, bpmRc.middley - 7,
                     toUTF32(format("%6.1f", model.bpm)), pal.accentOrange);
    }

    // Time / Bar:Beat LCD
    Rect timeRc = Rect(x + 272, top + 4, x + 366, bot - 4);
    drawCard(buf, timeRc, pal.bgLcd, pal.borderStrong, 3.0f);
    if (!fnt.isNull) {
        const string timeStr = (model.timeDisplayMode == TimeDisplayMode.TIME_MS)
            ? formatTimecode(model.playheadPosSec)
            : formatBarBeat(model.playheadPosSec, model.bpm);
        fnt.drawText(buf, timeRc.left + 8, timeRc.middley - 7, toUTF32(timeStr), pal.accentCyan);
    }
    x = transPanel.right + 6;

    // 3. Recording / Metronome / Typing Keyboard Panel
    Rect recOptsPanel = Rect(x, top, x + 150, bot);
    drawCard(buf, recOptsPanel, pal.bgToolbarWell, pal.borderSubtle, 4.0f);
    struct ToggleSpec { string lbl; bool on; uint col; }
    ToggleSpec[4] specs = [
        ToggleSpec("MET", model.metronomeEnabled, pal.accentOrange),
        ToggleSpec("321", model.countdownEnabled, pal.accentAmber),
        ToggleSpec("OVD", model.overdubEnabled,   pal.accentGreen),
        ToggleSpec("KB",  model.typingKeyboardEnabled, pal.accentCyan),
    ];
    foreach (i, sp; specs) {
        Rect tRc = Rect(x + 5 + cast(int)i * 35, top + 5, x + 37 + cast(int)i * 35, bot - 5);
        drawCard(buf, tRc, sp.on ? sp.col : pal.bgControl, pal.borderSubtle, 3.0f);
        if (!fnt.isNull) {
            fnt.drawText(buf, tRc.left + 4, tRc.middley - 7, toUTF32(sp.lbl),
                         sp.on ? pal.bgLcd : pal.fgMuted);
        }
    }
    x = recOptsPanel.right + 6;

    // 4. Snap & Undo/Redo Panel
    Rect snapPanel = Rect(x, top, x + 148, bot);
    drawCard(buf, snapPanel, pal.bgToolbarWell, pal.borderSubtle, 4.0f);
    Rect snapRc = Rect(x + 5, top + 5, x + 76, bot - 5);
    Rect undoRc = Rect(x + 80, top + 5, x + 111, bot - 5);
    Rect redoRc = Rect(x + 114, top + 5, x + 143, bot - 5);
    drawCard(buf, snapRc, pal.bgControl, pal.borderSubtle, 3.0f);
    drawCard(buf, undoRc, pal.bgControl, pal.borderSubtle, 3.0f);
    drawCard(buf, redoRc, pal.bgControl, pal.borderSubtle, 3.0f);
    if (!fnt.isNull) {
        fnt.drawText(buf, snapRc.left + 6, snapRc.middley - 7,
                     toUTF32(snapLabel(model.snapMode)), pal.fgText);
        fnt.drawText(buf, undoRc.left + 5, undoRc.middley - 7, "UND"d, pal.fgText);
        fnt.drawText(buf, redoRc.left + 5, redoRc.middley - 7, "RED"d, pal.fgText);
    }
    x = snapPanel.right + 6;

    // 5. Pattern Selector Panel (< Pattern 1 > +)
    Rect patPanel = Rect(x, top, x + 196, bot);
    drawCard(buf, patPanel, pal.bgToolbarWell, pal.borderSubtle, 4.0f);
    Rect pPrev = Rect(x + 5, top + 5, x + 26, bot - 5);
    Rect pLbl  = Rect(x + 28, top + 5, x + 144, bot - 5);
    Rect pNext = Rect(x + 146, top + 5, x + 167, bot - 5);
    Rect pAdd  = Rect(x + 170, top + 5, x + 191, bot - 5);
    drawCard(buf, pPrev, pal.bgControl, pal.borderSubtle, 3.0f);
    drawCard(buf, pLbl, pal.bgLcd, pal.borderStrong, 3.0f);
    drawCard(buf, pNext, pal.bgControl, pal.borderSubtle, 3.0f);
    drawCard(buf, pAdd, pal.bgControl, pal.borderSubtle, 3.0f);
    string patName = "Pattern 1";
    if (model.selectedPatternIndex >= 0 && model.selectedPatternIndex < cast(int)model.patterns.length) {
        patName = model.patterns[model.selectedPatternIndex].name;
    }
    if (!fnt.isNull) {
        fnt.drawText(buf, pPrev.left + 6, pPrev.middley - 7, "<"d, pal.fgText);
        fnt.drawText(buf, pLbl.left + 6, pLbl.middley - 7,
                     toUTF32(truncateLabel(patName, 14)), pal.accentOrange);
        fnt.drawText(buf, pNext.left + 6, pNext.middley - 7, ">"d, pal.fgText);
        fnt.drawText(buf, pAdd.left + 6, pAdd.middley - 7, "+"d, pal.accentGreen);
    }
    x = patPanel.right + 6;

    // 6. Window View Toggles Panel (PL, CR, PR, BR, MIX, PLUG)
    if (x + 206 <= rc.right - 6) {
        Rect winPanel = Rect(x, top, x + 204, bot);
        drawCard(buf, winPanel, pal.bgToolbarWell, pal.borderSubtle, 4.0f);
        struct WinSpec { string lbl; bool on; }
        WinSpec[6] wspecs = [
            WinSpec("PL", visPlaylist),
            WinSpec("CR", visChannelRack),
            WinSpec("PR", visPianoRoll),
            WinSpec("BR", visBrowser),
            WinSpec("MX", visMixer),
            WinSpec("FX", visPlugin),
        ];
        foreach (i, ws; wspecs) {
            Rect wRc = Rect(x + 5 + cast(int)i * 32, top + 5, x + 34 + cast(int)i * 32, bot - 5);
            drawCard(buf, wRc, ws.on ? pal.bgControlHover : pal.bgControl,
                     ws.on ? pal.accentOrange : pal.borderStrong, 3.0f);
            if (ws.on) {
                buf.fillRect(Rect(wRc.left + 3, wRc.bottom - 3, wRc.right - 3, wRc.bottom - 1),
                             pal.accentOrange);
            }
            if (!fnt.isNull) {
                fnt.drawText(buf, wRc.left + 6, wRc.middley - 7, toUTF32(ws.lbl),
                             ws.on ? pal.fgText : pal.fgMuted);
            }
        }
        x = winPanel.right + 6;
    }

    // 7. Live Oscilloscope & CPU Panel (fills remaining width on right if room)
    if (rc.right - x > 90) {
        Rect scopePanel = Rect(x, top, rc.right - 6, bot);
        drawCard(buf, scopePanel, pal.bgLcd, pal.borderSubtle, 4.0f);
        const int sw = scopePanel.width - 12;
        const int midY = scopePanel.middley;
        const float amp = model.isPlaying ? 9.0f : 2.0f;
        const float phase = cast(float)(model.playheadPosSec * 14.0);
        float prevX = scopePanel.left + 6;
        float prevY = midY;
        enum int PTS = 24;
        foreach (i; 1 .. PTS + 1) {
            const float t = cast(float)i / cast(float)PTS;
            const float sx = scopePanel.left + 6 + t * sw;
            const float sy = midY + sinf(t * 12.56f + phase) * amp * sinf(t * 3.14159f);
            drawThickLine(buf, prevX, prevY, sx, sy, 1.5f, pal.accentCyan);
            prevX = sx;
            prevY = sy;
        }
        if (!fnt.isNull) {
            fnt.drawText(buf, scopePanel.left + 6, scopePanel.top + 3,
                         toUTF32(format("CPU %d%%", model.cpuPercent)), pal.fgMuted);
        }
    }
}

/// Interactive FL Studio 2026 Modular Transport & Workspace Toolbar Widget.
class FlToolbarWidget : Widget {
    private TransportModel* model_;

    private enum DragMode { NONE, MASTER_VOL, MASTER_PITCH, BPM }
    private DragMode dragMode_ = DragMode.NONE;
    private int dragStartY_ = 0;
    private double dragStartVal_ = 0.0;

    bool delegate() queryPlaylistVisible;
    bool delegate() queryChannelRackVisible;
    bool delegate() queryPianoRollVisible;
    bool delegate() queryBrowserVisible;
    bool delegate() queryMixerVisible;
    bool delegate() queryPluginVisible;

    void delegate() onPatSongToggle;
    void delegate() onPlayToggle;
    void delegate() onStop;
    void delegate() onRecordToggle;
    void delegate() onLoopToggle;
    void delegate(double bpm) onBpmChanged;
    void delegate(bool redo) onUndoRedo;
    void delegate(int delta) onPatternStep;
    void delegate() onPatternAdd;
    void delegate(string winKey) onToggleWindow;

    this(string id, TransportModel* model) {
        super(id);
        model_ = model;
        layoutWidth = FILL_PARENT;
        layoutHeight = 48;
        minHeight = 48;
    }

    override void onDraw(DrawBuf buf) {
        if (visibility != Visibility.Visible) return;
        const bool vPl = queryPlaylistVisible !is null ? queryPlaylistVisible() : true;
        const bool vCr = queryChannelRackVisible !is null ? queryChannelRackVisible() : true;
        const bool vPr = queryPianoRollVisible !is null ? queryPianoRollVisible() : true;
        const bool vBr = queryBrowserVisible !is null ? queryBrowserVisible() : true;
        const bool vMx = queryMixerVisible !is null ? queryMixerVisible() : true;
        const bool vPg = queryPluginVisible !is null ? queryPluginVisible() : false;
        drawToolbarCanvas(buf, _pos, model_, vPl, vCr, vPr, vBr, vMx, vPg, font);
    }

    override bool onMouseEvent(MouseEvent event) {
        if (event.action == MouseAction.FocusOut) {
            return dragMode_ != DragMode.NONE;
        }
        if (!model_) return super.onMouseEvent(event);

        const int top = _pos.top + 4;
        const int bot = _pos.bottom - 4;
        int x = _pos.left + 6;

        if (event.action == MouseAction.ButtonDown) {
            // 1. Master knobs
            Rect masterPanel = Rect(x, top, x + 76, bot);
            if (masterPanel.isPointInside(event.x, event.y)) {
                if (event.x < x + 38) {
                    if (event.button == MouseButton.Right) model_.masterVolume = 0.82f;
                    else { dragMode_ = DragMode.MASTER_VOL; dragStartY_ = event.y; dragStartVal_ = model_.masterVolume; }
                } else {
                    if (event.button == MouseButton.Right) model_.masterPitch = 0.0f;
                    else { dragMode_ = DragMode.MASTER_PITCH; dragStartY_ = event.y; dragStartVal_ = model_.masterPitch; }
                }
                invalidate();
                return true;
            }
            x = masterPanel.right + 6;

            // 2. Transport Panel
            Rect transPanel = Rect(x, top, x + 372, bot);
            if (transPanel.isPointInside(event.x, event.y)) {
                if (Rect(x + 6, top + 4, x + 50, bot - 4).isPointInside(event.x, event.y)) {
                    model_.playbackMode = (model_.playbackMode == PlaybackMode.PAT)
                        ? PlaybackMode.SONG : PlaybackMode.PAT;
                    if (onPatSongToggle !is null) onPatSongToggle();
                } else if (Rect(x + 56, top + 5, x + 88, bot - 5).isPointInside(event.x, event.y)) {
                    if (onPlayToggle !is null) onPlayToggle();
                } else if (Rect(x + 92, top + 5, x + 122, bot - 5).isPointInside(event.x, event.y)) {
                    if (onStop !is null) onStop();
                } else if (Rect(x + 126, top + 5, x + 156, bot - 5).isPointInside(event.x, event.y)) {
                    if (onRecordToggle !is null) onRecordToggle();
                } else if (Rect(x + 160, top + 5, x + 194, bot - 5).isPointInside(event.x, event.y)) {
                    if (onLoopToggle !is null) onLoopToggle();
                } else if (Rect(x + 200, top + 4, x + 268, bot - 4).isPointInside(event.x, event.y)) {
                    if (event.button == MouseButton.Right) {
                        model_.bpm = 140.0;
                        if (onBpmChanged !is null) onBpmChanged(model_.bpm);
                    } else {
                        dragMode_ = DragMode.BPM;
                        dragStartY_ = event.y;
                        dragStartVal_ = model_.bpm;
                    }
                } else if (Rect(x + 272, top + 4, x + 366, bot - 4).isPointInside(event.x, event.y)) {
                    model_.timeDisplayMode = (model_.timeDisplayMode == TimeDisplayMode.BAR_BEAT)
                        ? TimeDisplayMode.TIME_MS : TimeDisplayMode.BAR_BEAT;
                }
                invalidate();
                return true;
            }
            x = transPanel.right + 6;

            // 3. Rec / Metronome / Typing Keyboard
            Rect recOptsPanel = Rect(x, top, x + 150, bot);
            if (recOptsPanel.isPointInside(event.x, event.y)) {
                const int idx = (event.x - (x + 5)) / 35;
                if (idx == 0) model_.metronomeEnabled = !model_.metronomeEnabled;
                else if (idx == 1) model_.countdownEnabled = !model_.countdownEnabled;
                else if (idx == 2) model_.overdubEnabled = !model_.overdubEnabled;
                else if (idx == 3) model_.typingKeyboardEnabled = !model_.typingKeyboardEnabled;
                invalidate();
                return true;
            }
            x = recOptsPanel.right + 6;

            // 4. Snap & Undo/Redo
            Rect snapPanel = Rect(x, top, x + 148, bot);
            if (snapPanel.isPointInside(event.x, event.y)) {
                if (Rect(x + 5, top + 5, x + 76, bot - 5).isPointInside(event.x, event.y)) {
                    model_.snapMode = cast(SnapMode)((cast(int)model_.snapMode + 1) % 10);
                    model_.hintText = "Global Snap: " ~ snapLabel(model_.snapMode);
                } else if (Rect(x + 80, top + 5, x + 111, bot - 5).isPointInside(event.x, event.y)) {
                    if (onUndoRedo !is null) onUndoRedo(false);
                } else if (Rect(x + 114, top + 5, x + 143, bot - 5).isPointInside(event.x, event.y)) {
                    if (onUndoRedo !is null) onUndoRedo(true);
                }
                invalidate();
                return true;
            }
            x = snapPanel.right + 6;

            // 5. Pattern Selector
            Rect patPanel = Rect(x, top, x + 196, bot);
            if (patPanel.isPointInside(event.x, event.y)) {
                if (Rect(x + 5, top + 5, x + 26, bot - 5).isPointInside(event.x, event.y)) {
                    if (onPatternStep !is null) onPatternStep(-1);
                } else if (Rect(x + 146, top + 5, x + 167, bot - 5).isPointInside(event.x, event.y)) {
                    if (onPatternStep !is null) onPatternStep(1);
                } else if (Rect(x + 170, top + 5, x + 191, bot - 5).isPointInside(event.x, event.y)) {
                    if (onPatternAdd !is null) onPatternAdd();
                }
                invalidate();
                return true;
            }
            x = patPanel.right + 6;

            // 6. Window View Toggles
            if (x + 206 <= _pos.right - 6) {
                Rect winPanel = Rect(x, top, x + 204, bot);
                if (winPanel.isPointInside(event.x, event.y)) {
                    static immutable string[6] KEYS = [
                        "win_playlist", "win_channel_rack", "win_piano_roll",
                        "browser", "win_mixer", "win_plugin"
                    ];
                    const int idx = (event.x - (x + 5)) / 32;
                    if (idx >= 0 && idx < 6 && onToggleWindow !is null) {
                        onToggleWindow(KEYS[idx]);
                    }
                    invalidate();
                    return true;
                }
            }
            return true;
        }

        if (event.action == MouseAction.Move && dragMode_ != DragMode.NONE) {
            const int dy = dragStartY_ - event.y;
            if (dragMode_ == DragMode.MASTER_VOL) {
                float v = cast(float)dragStartVal_ + cast(float)dy / 100.0f;
                model_.masterVolume = v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
                model_.hintText = format("Master Volume: %d%%", cast(int)(model_.masterVolume * 100.0f + 0.5f));
            } else if (dragMode_ == DragMode.MASTER_PITCH) {
                float p = cast(float)dragStartVal_ + cast(float)dy / 60.0f;
                model_.masterPitch = p < -1.0f ? -1.0f : (p > 1.0f ? 1.0f : p);
                model_.hintText = format("Master Pitch: %+d cents", cast(int)(model_.masterPitch * 1200.0f));
            } else if (dragMode_ == DragMode.BPM) {
                double newBpm = dragStartVal_ + cast(double)dy * 0.5;
                if (newBpm < 40.0) newBpm = 40.0;
                if (newBpm > 300.0) newBpm = 300.0;
                model_.bpm = newBpm;
                model_.hintText = format("Tempo: %.1f BPM", newBpm);
                if (onBpmChanged !is null) onBpmChanged(newBpm);
            }
            invalidate();
            return true;
        }

        if (event.action == MouseAction.ButtonUp && dragMode_ != DragMode.NONE) {
            dragMode_ = DragMode.NONE;
            return true;
        }

        return super.onMouseEvent(event);
    }
}

unittest {
    import dlangui.graphics.drawbuf : ColorDrawBuf;

    TransportModel model;
    model.initDefaultTemplate();
    model.isPlaying = true;
    auto buf = new ColorDrawBuf(1280, 48);
    drawToolbarCanvas(buf, Rect(0, 0, 1280, 48), &model, true, true, true, true, true, false);
    assert(buf.scanLine(10)[100] == model.palette().bgToolbarWell ||
           buf.scanLine(10)[100] == model.palette().accentOrange);
}
