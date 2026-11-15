module gui_d.toolbar;

import core.stdc.math : sinf;
import dlangui;
import dlangui.graphics.drawbuf : DrawBuf;
import dlangui.graphics.fonts : FontWeight;
import dlangui.widgets.widget : Visibility, Widget;
import gui_d.theme : dp, dpf, drawCard, drawCenteredText, drawFilledCircle, drawRotaryKnob, drawThickLine,
    drawVCenteredText, FlPalette, formatBarBeat, formatTimecode, getDawFont, getDawMonoFont,
    getFlPalette, safeWidgetFont, truncateLabel, uiScale;
import gui_d.transport_model : PlaybackMode, SnapMode, TimeDisplayMode, TransportModel;
import std.format : format;
import std.utf : toUTF32;

/// DPI-scaled geometry shared identically between `drawToolbarCanvas` and `FlToolbarWidget.onMouseEvent`.
struct ToolbarLayout {
    Rect masterPanel;
    int volKnobX;
    int pitchKnobX;
    Rect transPanel;
    Rect patSongRc;
    Rect playRc;
    Rect stopRc;
    Rect recRc;
    Rect loopRc;
    Rect bpmRc;
    Rect timeRc;
    Rect recOptsPanel;
    Rect[4] recToggles;
    Rect snapPanel;
    Rect snapRc;
    Rect undoRc;
    Rect redoRc;
    Rect patPanel;
    Rect pPrev;
    Rect pLbl;
    Rect pNext;
    Rect pAdd;
    bool hasWinPanel;
    Rect winPanel;
    Rect[6] winBtns;
    bool hasScopePanel;
    Rect scopePanel;
}

ToolbarLayout computeToolbarLayout(Rect rc) nothrow @safe {
    ToolbarLayout tl;
    const int top = rc.top + dp(4);
    const int bot = rc.bottom - dp(4);
    const int padY = dp(4);
    const int btnY = dp(5);
    const int gap = dp(6);
    int x = rc.left + gap;

    // 1. Master Volume & Master Pitch Panel
    tl.masterPanel = Rect(x, top, x + dp(76), bot);
    tl.volKnobX = x + dp(22);
    tl.pitchKnobX = x + dp(54);
    x = tl.masterPanel.right + gap;

    // 2. Transport Panel (PAT/SONG, Play, Stop, Rec, Loop, BPM LCD, Time LCD)
    tl.transPanel = Rect(x, top, x + dp(374), bot);
    tl.patSongRc = Rect(x + dp(6), top + padY, x + dp(50), bot - padY);
    tl.playRc = Rect(x + dp(56), top + btnY, x + dp(88), bot - btnY);
    tl.stopRc = Rect(x + dp(92), top + btnY, x + dp(122), bot - btnY);
    tl.recRc = Rect(x + dp(126), top + btnY, x + dp(156), bot - btnY);
    tl.loopRc = Rect(x + dp(160), top + btnY, x + dp(196), bot - btnY);
    tl.bpmRc = Rect(x + dp(200), top + padY, x + dp(268), bot - padY);
    tl.timeRc = Rect(x + dp(272), top + padY, x + dp(368), bot - padY);
    x = tl.transPanel.right + gap;

    // 3. Recording / Metronome / Typing Keyboard Panel
    tl.recOptsPanel = Rect(x, top, x + dp(150), bot);
    foreach (i; 0 .. 4) {
        const int bx = x + dp(5) + cast(int)i * dp(35);
        tl.recToggles[i] = Rect(bx, top + btnY, bx + dp(32), bot - btnY);
    }
    x = tl.recOptsPanel.right + gap;

    // 4. Snap & Undo/Redo Panel
    tl.snapPanel = Rect(x, top, x + dp(152), bot);
    tl.snapRc = Rect(x + dp(5), top + btnY, x + dp(78), bot - btnY);
    tl.undoRc = Rect(x + dp(82), top + btnY, x + dp(113), bot - btnY);
    tl.redoRc = Rect(x + dp(116), top + btnY, x + dp(147), bot - btnY);
    x = tl.snapPanel.right + gap;

    // 5. Pattern Selector Panel (< Pattern 1 > +)
    tl.patPanel = Rect(x, top, x + dp(196), bot);
    tl.pPrev = Rect(x + dp(5), top + btnY, x + dp(26), bot - btnY);
    tl.pLbl = Rect(x + dp(28), top + btnY, x + dp(144), bot - btnY);
    tl.pNext = Rect(x + dp(146), top + btnY, x + dp(167), bot - btnY);
    tl.pAdd = Rect(x + dp(170), top + btnY, x + dp(191), bot - btnY);
    x = tl.patPanel.right + gap;

    // 6. Window View Toggles Panel (PL, CR, PR, BR, MX, FX)
    if (x + dp(206) <= rc.right - gap) {
        tl.hasWinPanel = true;
        tl.winPanel = Rect(x, top, x + dp(204), bot);
        foreach (i; 0 .. 6) {
            const int wx = x + dp(5) + cast(int)i * dp(32);
            tl.winBtns[i] = Rect(wx, top + btnY, wx + dp(29), bot - btnY);
        }
        x = tl.winPanel.right + gap;
    }

    // 7. Live Oscilloscope & CPU Panel
    if (rc.right - x > dp(90)) {
        tl.hasScopePanel = true;
        tl.scopePanel = Rect(x, top, rc.right - gap, bot);
    }
    return tl;
}

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
    buf.fillRect(Rect(rc.left, rc.bottom - dp(1), rc.right, rc.bottom), pal.borderStrong);
    if (!model || rc.width < dp(600) || rc.height < dp(42)) return;

    auto tl = computeToolbarLayout(rc);
    FontRef uiFnt = getDawFont(10, fnt, FontWeight.Bold);
    FontRef patFnt = getDawFont(11, fnt, FontWeight.Normal);
    FontRef lcdFnt = getDawMonoFont(12, fnt, FontWeight.Bold);

    // 1. Master Volume & Master Pitch Panel
    drawCard(buf, tl.masterPanel, pal.bgToolbarWell, pal.borderSubtle, 4.0f);
    drawRotaryKnob(buf, tl.volKnobX, tl.masterPanel.middley, dp(12), model.masterVolume,
                   pal.bgLcd, pal.accentOrange, pal.bgControl, pal.borderSubtle);
    drawRotaryKnob(buf, tl.pitchKnobX, tl.masterPanel.middley, dp(11), (model.masterPitch + 1.0f) * 0.5f,
                   pal.bgLcd, pal.accentCyan, pal.bgControl, pal.borderSubtle, true);

    // 2. Transport Panel (PAT/SONG, Play, Stop, Rec, Loop, BPM LCD, Time LCD)
    drawCard(buf, tl.transPanel, pal.bgToolbarWell, pal.borderSubtle, 4.0f);

    const bool isPat = model.playbackMode == PlaybackMode.PAT;
    drawCard(buf, tl.patSongRc, isPat ? pal.accentOrange : pal.accentGreen, pal.borderStrong, 3.0f);
    drawCenteredText(buf, uiFnt, tl.patSongRc, isPat ? "PAT"d : "SONG"d, pal.bgLcd);

    drawCard(buf, tl.playRc, model.isPlaying ? pal.accentGreen : pal.bgControl, pal.borderSubtle, 3.0f);
    drawCard(buf, tl.stopRc, pal.bgControl, pal.borderSubtle, 3.0f);
    drawCard(buf, tl.recRc, model.isRecording ? pal.accentRed : pal.bgControl, pal.borderSubtle, 3.0f);
    drawCard(buf, tl.loopRc, model.loopEnabled ? pal.accentAmber : pal.bgControl, pal.borderSubtle, 3.0f);

    // Vector transport glyphs
    const uint playCol = model.isPlaying ? pal.bgLcd : pal.accentGreen;
    const int playW = dp(10);
    const int playMaxH = dp(6);
    foreach (px; 0 .. playW) {
        const int halfH = ((playW - px) * playMaxH) / playW;
        buf.fillRect(Rect(tl.playRc.middlex - dp(4) + px, tl.playRc.middley - halfH,
                          tl.playRc.middlex - dp(3) + px, tl.playRc.middley + halfH + dp(1)), playCol);
    }
    const int stopR = dp(5);
    buf.fillRect(Rect(tl.stopRc.middlex - stopR, tl.stopRc.middley - stopR,
                      tl.stopRc.middlex + stopR, tl.stopRc.middley + stopR),
                 pal.fgText);
    const uint recCol = model.isRecording ? pal.bgLcd : pal.accentRed;
    drawFilledCircle(buf, tl.recRc.middlex, tl.recRc.middley, dpf(5.0f), recCol);
    drawCenteredText(buf, uiFnt, tl.loopRc, "LOOP"d,
                     model.loopEnabled ? pal.bgLcd : pal.fgMuted);

    // BPM LCD (drag up/down)
    drawCard(buf, tl.bpmRc, pal.bgLcd, pal.borderStrong, 3.0f);
    drawCenteredText(buf, lcdFnt, tl.bpmRc, toUTF32(format("%.1f", model.bpm)), pal.accentOrange);

    // Time / Bar:Beat LCD
    drawCard(buf, tl.timeRc, pal.bgLcd, pal.borderStrong, 3.0f);
    const string timeStr = (model.timeDisplayMode == TimeDisplayMode.TIME_MS)
        ? formatTimecode(model.playheadPosSec)
        : formatBarBeat(model.playheadPosSec, model.bpm);
    drawCenteredText(buf, lcdFnt, tl.timeRc, toUTF32(timeStr), pal.accentCyan);

    // 3. Recording / Metronome / Typing Keyboard Panel
    drawCard(buf, tl.recOptsPanel, pal.bgToolbarWell, pal.borderSubtle, 4.0f);
    struct ToggleSpec { string lbl; bool on; uint col; }
    ToggleSpec[4] specs = [
        ToggleSpec("MET", model.metronomeEnabled, pal.accentOrange),
        ToggleSpec("321", model.countdownEnabled, pal.accentAmber),
        ToggleSpec("OVD", model.overdubEnabled, pal.accentGreen),
        ToggleSpec("KB", model.typingKeyboardEnabled, pal.accentCyan),
    ];
    foreach (i, sp; specs) {
        Rect tRc = tl.recToggles[i];
        drawCard(buf, tRc, sp.on ? sp.col : pal.bgControl, pal.borderSubtle, 3.0f);
        drawCenteredText(buf, uiFnt, tRc, toUTF32(sp.lbl), sp.on ? pal.bgLcd : pal.fgMuted);
    }

    // 4. Snap & Undo/Redo Panel
    drawCard(buf, tl.snapPanel, pal.bgToolbarWell, pal.borderSubtle, 4.0f);
    drawCard(buf, tl.snapRc, pal.bgControl, pal.borderSubtle, 3.0f);
    drawCard(buf, tl.undoRc, pal.bgControl, pal.borderSubtle, 3.0f);
    drawCard(buf, tl.redoRc, pal.bgControl, pal.borderSubtle, 3.0f);
    drawCenteredText(buf, uiFnt, tl.snapRc, toUTF32(snapLabel(model.snapMode)), pal.fgText);
    drawCenteredText(buf, uiFnt, tl.undoRc, "UND"d, pal.fgText);
    drawCenteredText(buf, uiFnt, tl.redoRc, "RED"d, pal.fgText);

    // 5. Pattern Selector Panel (< Pattern 1 > +)
    drawCard(buf, tl.patPanel, pal.bgToolbarWell, pal.borderSubtle, 4.0f);
    drawCard(buf, tl.pPrev, pal.bgControl, pal.borderSubtle, 3.0f);
    drawCard(buf, tl.pLbl, pal.bgLcd, pal.borderStrong, 3.0f);
    drawCard(buf, tl.pNext, pal.bgControl, pal.borderSubtle, 3.0f);
    drawCard(buf, tl.pAdd, pal.bgControl, pal.borderSubtle, 3.0f);
    string patName = "Pattern 1";
    if (model.selectedPatternIndex >= 0 && model.selectedPatternIndex < cast(int)model.patterns.length) {
        patName = model.patterns[model.selectedPatternIndex].name;
    }
    drawCenteredText(buf, uiFnt, tl.pPrev, "<"d, pal.fgText);
    drawVCenteredText(buf, patFnt, tl.pLbl.left + dp(6), tl.pLbl,
                      toUTF32(truncateLabel(patName, 14)), pal.accentOrange);
    drawCenteredText(buf, uiFnt, tl.pNext, ">"d, pal.fgText);
    drawCenteredText(buf, uiFnt, tl.pAdd, "+"d, pal.accentGreen);

    // 6. Window View Toggles Panel (PL, CR, PR, BR, MIX, PLUG)
    if (tl.hasWinPanel) {
        drawCard(buf, tl.winPanel, pal.bgToolbarWell, pal.borderSubtle, 4.0f);
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
            Rect wRc = tl.winBtns[i];
            drawCard(buf, wRc, ws.on ? pal.bgControlHover : pal.bgControl,
                     ws.on ? pal.accentOrange : pal.borderStrong, 3.0f);
            if (ws.on) {
                buf.fillRect(Rect(wRc.left + dp(3), wRc.bottom - dp(3), wRc.right - dp(3), wRc.bottom - dp(1)),
                             pal.accentOrange);
            }
            drawCenteredText(buf, uiFnt, wRc, toUTF32(ws.lbl), ws.on ? pal.fgText : pal.fgMuted);
        }
    }

    // 7. Live Oscilloscope & CPU Panel (fills remaining width on right if room)
    if (tl.hasScopePanel) {
        Rect scopePanel = tl.scopePanel;
        drawCard(buf, scopePanel, pal.bgLcd, pal.borderSubtle, 4.0f);
        const int sw = scopePanel.width - dp(12);
        const int midY = scopePanel.middley;
        const float amp = dpf(model.isPlaying ? 9.0f : 2.0f);
        const float phase = cast(float)(model.playheadPosSec * 14.0);
        float prevX = scopePanel.left + dp(6);
        float prevY = midY;
        enum int PTS = 24;
        foreach (i; 1 .. PTS + 1) {
            const float t = cast(float)i / cast(float)PTS;
            const float sx = scopePanel.left + dp(6) + t * sw;
            const float sy = midY + sinf(t * 12.56f + phase) * amp * sinf(t * 3.14159f);
            drawThickLine(buf, prevX, prevY, sx, sy, dpf(1.5f), pal.accentCyan);
            prevX = sx;
            prevY = sy;
        }
        drawVCenteredText(buf, getDawFont(10, fnt, FontWeight.Normal), scopePanel.left + dp(6),
                          Rect(scopePanel.left, scopePanel.top + dp(2), scopePanel.right, scopePanel.top + dp(16)),
                          toUTF32(format("CPU %d%%", model.cpuPercent)), pal.fgMuted);
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
        layoutHeight = dp(48);
        minHeight = dp(48);
    }

    override void measure(int parentWidth, int parentHeight) {
        const int h = dp(48);
        minHeight = h;
        layoutHeight = h;
        measuredContent(parentWidth, parentHeight, parentWidth, h);
    }

    override void onDraw(DrawBuf buf) {
        if (visibility != Visibility.Visible) return;
        const bool vPl = queryPlaylistVisible !is null ? queryPlaylistVisible() : true;
        const bool vCr = queryChannelRackVisible !is null ? queryChannelRackVisible() : true;
        const bool vPr = queryPianoRollVisible !is null ? queryPianoRollVisible() : true;
        const bool vBr = queryBrowserVisible !is null ? queryBrowserVisible() : true;
        const bool vMx = queryMixerVisible !is null ? queryMixerVisible() : true;
        const bool vPg = queryPluginVisible !is null ? queryPluginVisible() : false;
        drawToolbarCanvas(buf, _pos, model_, vPl, vCr, vPr, vBr, vMx, vPg, safeWidgetFont(this));
    }

    override bool onMouseEvent(MouseEvent event) {
        if (event.action == MouseAction.FocusOut) {
            return dragMode_ != DragMode.NONE;
        }
        if (!model_) return super.onMouseEvent(event);

        auto tl = computeToolbarLayout(_pos);

        if (event.action == MouseAction.ButtonDown) {
            // 1. Master knobs
            if (tl.masterPanel.isPointInside(event.x, event.y)) {
                if (event.x < tl.masterPanel.middlex) {
                    if (event.button == MouseButton.Right) model_.masterVolume = 0.82f;
                    else { dragMode_ = DragMode.MASTER_VOL; dragStartY_ = event.y; dragStartVal_ = model_.masterVolume; }
                } else {
                    if (event.button == MouseButton.Right) model_.masterPitch = 0.0f;
                    else { dragMode_ = DragMode.MASTER_PITCH; dragStartY_ = event.y; dragStartVal_ = model_.masterPitch; }
                }
                invalidate();
                return true;
            }

            // 2. Transport Panel
            if (tl.transPanel.isPointInside(event.x, event.y)) {
                if (tl.patSongRc.isPointInside(event.x, event.y)) {
                    model_.playbackMode = (model_.playbackMode == PlaybackMode.PAT)
                        ? PlaybackMode.SONG : PlaybackMode.PAT;
                    if (onPatSongToggle !is null) onPatSongToggle();
                } else if (tl.playRc.isPointInside(event.x, event.y)) {
                    if (onPlayToggle !is null) onPlayToggle();
                } else if (tl.stopRc.isPointInside(event.x, event.y)) {
                    if (onStop !is null) onStop();
                } else if (tl.recRc.isPointInside(event.x, event.y)) {
                    if (onRecordToggle !is null) onRecordToggle();
                } else if (tl.loopRc.isPointInside(event.x, event.y)) {
                    if (onLoopToggle !is null) onLoopToggle();
                } else if (tl.bpmRc.isPointInside(event.x, event.y)) {
                    if (event.button == MouseButton.Right) {
                        model_.bpm = 140.0;
                        if (onBpmChanged !is null) onBpmChanged(model_.bpm);
                    } else {
                        dragMode_ = DragMode.BPM;
                        dragStartY_ = event.y;
                        dragStartVal_ = model_.bpm;
                    }
                } else if (tl.timeRc.isPointInside(event.x, event.y)) {
                    model_.timeDisplayMode = (model_.timeDisplayMode == TimeDisplayMode.BAR_BEAT)
                        ? TimeDisplayMode.TIME_MS : TimeDisplayMode.BAR_BEAT;
                }
                invalidate();
                return true;
            }

            // 3. Rec / Metronome / Typing Keyboard
            if (tl.recOptsPanel.isPointInside(event.x, event.y)) {
                foreach (idx; 0 .. 4) {
                    if (tl.recToggles[idx].isPointInside(event.x, event.y)) {
                        if (idx == 0) model_.metronomeEnabled = !model_.metronomeEnabled;
                        else if (idx == 1) model_.countdownEnabled = !model_.countdownEnabled;
                        else if (idx == 2) model_.overdubEnabled = !model_.overdubEnabled;
                        else if (idx == 3) model_.typingKeyboardEnabled = !model_.typingKeyboardEnabled;
                        break;
                    }
                }
                invalidate();
                return true;
            }

            // 4. Snap & Undo/Redo
            if (tl.snapPanel.isPointInside(event.x, event.y)) {
                if (tl.snapRc.isPointInside(event.x, event.y)) {
                    model_.snapMode = cast(SnapMode)((cast(int)model_.snapMode + 1) % 10);
                    model_.hintText = "Global Snap: " ~ snapLabel(model_.snapMode);
                } else if (tl.undoRc.isPointInside(event.x, event.y)) {
                    if (onUndoRedo !is null) onUndoRedo(false);
                } else if (tl.redoRc.isPointInside(event.x, event.y)) {
                    if (onUndoRedo !is null) onUndoRedo(true);
                }
                invalidate();
                return true;
            }

            // 5. Pattern Selector
            if (tl.patPanel.isPointInside(event.x, event.y)) {
                if (tl.pPrev.isPointInside(event.x, event.y)) {
                    if (onPatternStep !is null) onPatternStep(-1);
                } else if (tl.pLbl.isPointInside(event.x, event.y)) {
                    if (onPatternStep !is null) onPatternStep(event.button == MouseButton.Right ? -1 : 1);
                } else if (tl.pNext.isPointInside(event.x, event.y)) {
                    if (onPatternStep !is null) onPatternStep(1);
                } else if (tl.pAdd.isPointInside(event.x, event.y)) {
                    if (onPatternAdd !is null) onPatternAdd();
                }
                invalidate();
                return true;
            }

            // 6. Window View Toggles
            if (tl.hasWinPanel && tl.winPanel.isPointInside(event.x, event.y)) {
                static immutable string[6] KEYS = [
                    "win_playlist", "win_channel_rack", "win_piano_roll",
                    "browser", "win_mixer", "win_plugin"
                ];
                foreach (idx; 0 .. 6) {
                    if (tl.winBtns[idx].isPointInside(event.x, event.y)) {
                        if (onToggleWindow !is null) onToggleWindow(KEYS[idx]);
                        break;
                    }
                }
                invalidate();
                return true;
            }
            return true;
        }

        if (event.action == MouseAction.Move && dragMode_ != DragMode.NONE) {
            const float dy = cast(float)(dragStartY_ - event.y) / uiScale();
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

        if ((event.action == MouseAction.ButtonUp || event.action == MouseAction.Cancel) &&
            dragMode_ != DragMode.NONE) {
            if (dragMode_ == DragMode.BPM && onBpmChanged !is null) {
                onBpmChanged(model_.bpm);
            }
            dragMode_ = DragMode.NONE;
            return true;
        }

        return super.onMouseEvent(event);
    }
}

unittest {
    import dlangui.graphics.drawbuf : ColorDrawBuf;
    import dlangui.widgets.styles : currentTheme, createDefaultTheme;

    TransportModel model;
    model.initDefaultTemplate();
    model.isPlaying = true;
    auto buf = new ColorDrawBuf(1280, 48);
    drawToolbarCanvas(buf, Rect(0, 0, 1280, 48), &model, true, true, true, true, true, false);
    assert(buf.scanLine(10)[100] == model.palette().bgToolbarWell ||
           buf.scanLine(10)[100] == model.palette().accentOrange);

    import gui_d.theme : destroyWidgetTree;
    if (currentTheme is null) currentTheme = createDefaultTheme();
    auto tb = new FlToolbarWidget("tb", &model);
    scope (exit) destroyWidgetTree(tb);
    tb.measure(1280, 48);
    tb.layout(Rect(0, 0, 1280, 48));
    bool played = false;
    bool stopped = false;
    double changedBpm = 0.0;
    tb.onPlayToggle = () { played = true; };
    tb.onStop = () { stopped = true; };
    tb.onBpmChanged = (double b) { changedBpm = b; };

    // Play button is at x = 6 + 76 + 6 + 56..88 = 144..176
    tb.onMouseEvent(new MouseEvent(MouseAction.ButtonDown, MouseButton.Left, MouseFlag.LButton, 155, 24));
    assert(played, "Expected toolbar Play button click to trigger onPlayToggle");
    // BPM drag + Cancel ends drag cleanly and notifies onBpmChanged
    tb.onMouseEvent(new MouseEvent(MouseAction.ButtonDown, MouseButton.Left, MouseFlag.LButton, 300, 24));
    tb.onMouseEvent(new MouseEvent(MouseAction.Move, MouseButton.None, MouseFlag.LButton, 300, 14));
    tb.onMouseEvent(new MouseEvent(MouseAction.Cancel, MouseButton.None, 0, 300, 14));
    assert(changedBpm > 140.0, "Expected BPM drag + Cancel to commit updated BPM");
    const double lockedBpm = model.bpm;
    tb.onMouseEvent(new MouseEvent(MouseAction.Move, MouseButton.None, 0, 300, 0));
    assert(model.bpm == lockedBpm, "Expected MouseAction.Cancel to clear BPM drag");
}
