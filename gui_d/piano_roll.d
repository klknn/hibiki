module gui_d.piano_roll;

import dlangui;
import dlangui.graphics.drawbuf : DrawBuf;
import dlangui.widgets.widget : Visibility, Widget;
import gui_d.theme : adjustColorBrightness, drawCard, drawFilledCircle, FlPalette,
    getFlPalette, truncateLabel;
import gui_d.transport_model : EditorTool, TransportModel;
import gui_d.workspace : activateParentWindow;
import pb.core : MidiEvent;
import std.format : format;
import std.utf : toUTF32;

private bool isBlackKey(int pitch) pure nothrow @safe {
    const int pc = ((pitch % 12) + 12) % 12;
    return pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10;
}

private bool isInAMinorScale(int pitch) pure nothrow @safe {
    const int pc = ((pitch % 12) + 12) % 12;
    return pc == 0 || pc == 2 || pc == 4 || pc == 5 || pc == 7 || pc == 9 || pc == 11;
}

private string noteName(int pitch) pure @safe {
    static immutable string[12] NAMES = [
        "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
    ];
    const int pc = ((pitch % 12) + 12) % 12;
    const int oct = (pitch / 12) - 1;
    return format("%s%d", NAMES[pc], oct);
}

private enum int PR_TOOLBAR_H = 28;
private enum int PR_RULER_H = 18;
private enum int PR_VEL_H = 68;

/// Renders the FL Studio 2026 Piano Roll (Toolbar, Keyboard, Scale-Highlighted Grid, Ghost Notes, Velocity Lane).
void drawPianoRollCanvas(
    DrawBuf buf,
    Rect rc,
    const(TransportModel)* model,
    int pitchLow = 48,
    int pitchHigh = 72,
    double pixelsPerBeat = 64.0,
    int numBeats = 16,
    int keyWidth = 54,
    int rowHeight = 14,
    FontRef fnt = FontRef.init,
    double scrollBeats = 0.0
) {
    const pal = model ? model.palette() : getFlPalette();
    buf.fillRect(rc, pal.bgWindow);
    if (rc.width < 180 || rc.height < 90) return;

    // 1. Top Piano Roll Toolbar
    Rect tbRc = Rect(rc.left, rc.top, rc.right, rc.top + PR_TOOLBAR_H);
    buf.fillRect(tbRc, pal.bgPanelAlt);
    buf.fillRect(Rect(tbRc.left, tbRc.bottom - 1, tbRc.right, tbRc.bottom), pal.borderStrong);

    static immutable string[6] TOOLS = ["P", "B", "D", "C", "E", "Z"];
    static immutable EditorTool[6] TOOL_ENUMS = [
        EditorTool.DRAW, EditorTool.PAINT, EditorTool.DELETE,
        EditorTool.SLICE, EditorTool.SELECT, EditorTool.ZOOM
    ];
    foreach (i, lbl; TOOLS) {
        const int bx0 = tbRc.left + 6 + cast(int)i * 24;
        Rect bRc = Rect(bx0, tbRc.top + 4, bx0 + 21, tbRc.bottom - 4);
        const bool active = model && model.pianoRollTool == TOOL_ENUMS[i];
        drawCard(buf, bRc, active ? pal.accentOrange : pal.bgControl, pal.borderSubtle, 3.0f);
        if (!fnt.isNull) {
            fnt.drawText(buf, bRc.left + 7, bRc.top + 2, toUTF32(lbl), active ? pal.bgLcd : pal.fgText);
        }
    }

    // Target Channel pill
    string targetName = "Acid Bass";
    if (model && model.activeMidi.channelIndex >= 0 &&
        model.activeMidi.channelIndex < cast(int)model.channels.length) {
        targetName = model.channels[model.activeMidi.channelIndex].name;
    }
    Rect tgtRc = Rect(tbRc.left + 158, tbRc.top + 4, tbRc.left + 290, tbRc.bottom - 4);
    drawCard(buf, tgtRc, pal.bgControl, pal.borderSubtle, 3.0f);
    if (!fnt.isNull) {
        fnt.drawText(buf, tgtRc.left + 8, tgtRc.top + 2,
                     toUTF32("< " ~ truncateLabel(targetName, 12) ~ " >"), pal.accentOrange);
    }

    // Ghost Notes & Velocity Lane toggles
    const bool ghostOn = model ? model.ghostNotesEnabled : true;
    const bool velOn = model ? model.showVelocityLane : true;
    Rect ghostRc = Rect(tbRc.right - 104, tbRc.top + 4, tbRc.right - 54, tbRc.bottom - 4);
    Rect velRc = Rect(tbRc.right - 50, tbRc.top + 4, tbRc.right - 6, tbRc.bottom - 4);
    drawCard(buf, ghostRc, ghostOn ? pal.accentCyan : pal.bgControl, pal.borderSubtle, 3.0f);
    drawCard(buf, velRc, velOn ? pal.accentGreen : pal.bgControl, pal.borderSubtle, 3.0f);
    if (!fnt.isNull) {
        fnt.drawText(buf, ghostRc.left + 5, ghostRc.top + 2, "GHOST"d, ghostOn ? pal.bgLcd : pal.fgText);
        fnt.drawText(buf, velRc.left + 9, velRc.top + 2, "VEL"d, velOn ? pal.bgLcd : pal.fgText);
    }

    // 2. Beat Ruler & Piano Grid Geometry
    const int gridLeft = rc.left + keyWidth;
    const int gridRight = rc.right;
    const int rulerTop = tbRc.bottom;
    const int gridTop = rulerTop + PR_RULER_H;
    const int velTop = (velOn && rc.bottom - gridTop > PR_VEL_H + 60) ? (rc.bottom - PR_VEL_H) : rc.bottom;

    buf.fillRect(Rect(rc.left, rulerTop, gridLeft, gridTop), pal.bgPanel);
    buf.fillRect(Rect(gridLeft, rulerTop, gridRight, gridTop), pal.bgLcd);
    buf.fillRect(Rect(rc.left, gridTop - 1, rc.right, gridTop), pal.borderStrong);

    const int numRows = pitchHigh - pitchLow + 1;
    foreach (r; 0 .. numRows) {
        const int pitch = pitchHigh - r;
        const int y0 = gridTop + r * rowHeight;
        int y1 = y0 + rowHeight;
        if (y0 >= velTop) break;
        if (y1 > velTop) y1 = velTop;

        const bool black = isBlackKey(pitch);
        const bool inScale = isInAMinorScale(pitch);

        // Piano key on the left (white key base + shorter black key overlay)
        const uint whiteBg = 0x00D8DCE3;
        const uint blackBg = 0x00222730;
        buf.fillRect(Rect(rc.left, y0, gridLeft - 1, y1 - 1), whiteBg);
        buf.fillRect(Rect(rc.left, y1 - 1, gridLeft, y1), pal.borderStrong);
        if (black) {
            buf.fillRect(Rect(rc.left, y0 + 1, gridLeft - 18, y1 - 1), blackBg);
        } else if (!fnt.isNull && ((pitch % 12) + 12) % 12 == 0 && (y1 - y0) >= 10) {
            fnt.drawText(buf, rc.left + 6, y0 + 1, toUTF32(noteName(pitch)), pal.bgLcd);
        }

        // Scale-highlighted row lane
        const uint laneBg = black
            ? adjustColorBrightness(pal.bgPanelAlt, -6)
            : (inScale ? pal.bgWindow : pal.bgPanelAlt);
        buf.fillRect(Rect(gridLeft, y0, gridRight, y1 - 1), laneBg);
        buf.fillRect(Rect(gridLeft, y1 - 1, gridRight, y1), pal.borderStrong);
    }

    // 3. Vertical Beat & 16th Subdivisions + Ruler Numbers
    const int totalSubdivs = numBeats * 4;
    foreach (s; 0 .. totalSubdivs + 1) {
        const double beat = cast(double)s * 0.25;
        const int x = gridLeft + cast(int)((beat - scrollBeats) * pixelsPerBeat + 0.5);
        if (x < gridLeft || x >= gridRight) continue;
        const bool isBar = (s % 16 == 0);
        const bool isBeat = (s % 4 == 0);
        const uint col = isBar ? pal.borderSubtle : (isBeat ? pal.bgControl : pal.borderStrong);
        buf.fillRect(Rect(x, gridTop, x + 1, velTop), col);
        if (isBar && !fnt.isNull && x + 14 < gridRight) {
            fnt.drawText(buf, x + 3, rulerTop + 2, toUTF32(format("%d", (s / 16) + 1)), pal.fgText);
        }
    }

    // 4. Ghost Notes (notes from other channels in the active pattern)
    const int res = (model && model.activeMidi.resolution > 0) ? model.activeMidi.resolution : 480;
    const double resF = cast(double)res;
    if (ghostOn && model) {
        foreach (ci, const ref ch; model.channels) {
            if (cast(int)ci == model.activeMidi.channelIndex) continue;
            foreach (const ref gev; ch.notes) {
                if (gev.cc_number != 0 || gev.velocity == 0) continue;
                const int gp = cast(int)gev.pitch;
                if (gp < pitchLow || gp > pitchHigh) continue;
                const int row = pitchHigh - gp;
                const int y0 = gridTop + row * rowHeight + 2;
                const int y1 = y0 + rowHeight - 4;
                if (y1 <= gridTop || y0 >= velTop) continue;
                const double startBeat = cast(double)gev.tick / resF;
                const double durBeat = (gev.duration_ticks > 0) ? (cast(double)gev.duration_ticks / resF) : 0.25;
                int gx0 = gridLeft + cast(int)((startBeat - scrollBeats) * pixelsPerBeat + 0.5);
                int gx1 = gridLeft + cast(int)((startBeat + durBeat - scrollBeats) * pixelsPerBeat + 0.5);
                if (gx1 > gridLeft && gx0 < gridRight) {
                    if (gx0 < gridLeft) gx0 = gridLeft;
                    if (gx1 > gridRight) gx1 = gridRight;
                    buf.fillRect(Rect(gx0, y0, gx1, y1), pal.noteGhost);
                }
            }
        }
    }

    // 5. Active MIDI Note Bars
    const(MidiEvent)[] events = model ? model.activeMidi.events : null;
    foreach (const ref ev; events) {
        if (ev.cc_number != 0 || ev.velocity == 0) continue;
        const int p = cast(int)ev.pitch;
        if (p < pitchLow || p > pitchHigh) continue;

        const int row = pitchHigh - p;
        const int y0 = gridTop + row * rowHeight + 1;
        const int y1 = y0 + rowHeight - 2;
        if (y1 <= gridTop || y0 >= velTop) continue;

        const double startBeat = cast(double)ev.tick / resF;
        const double durBeat = (ev.duration_ticks > 0) ? (cast(double)ev.duration_ticks / resF) : 0.25;
        int x0 = gridLeft + cast(int)((startBeat - scrollBeats) * pixelsPerBeat + 0.5);
        int x1 = gridLeft + cast(int)((startBeat + durBeat - scrollBeats) * pixelsPerBeat + 0.5);
        if (x1 <= x0 + 4) x1 = x0 + 6;
        if (x1 <= gridLeft || x0 >= gridRight) continue;
        if (x0 < gridLeft) x0 = gridLeft;
        if (x1 > gridRight) x1 = gridRight;

        Rect noteRc = Rect(x0, y0, x1, y1);
        drawCard(buf, noteRc, pal.noteFill, pal.borderStrong, 2.5f);
        // Brighter left attack bar
        buf.fillRect(Rect(x0 + 1, y0 + 1, x0 + 4 < x1 ? x0 + 4 : x1, y1 - 1), pal.accentGreen);
        if (!fnt.isNull && (x1 - x0) >= 24 && (y1 - y0) >= 11) {
            fnt.drawText(buf, x0 + 5, y0 + 1, toUTF32(noteName(p)), pal.bgLcd);
        }
    }

    // Playhead cap & vertical line in Piano Roll ruler & grid
    const double bpm = (model && model.bpm > 0.0) ? model.bpm : 140.0;
    const double phBeat = (model ? model.playheadPosSec : 0.0) * (bpm / 60.0);
    const int phX = gridLeft + cast(int)((phBeat - scrollBeats) * pixelsPerBeat + 0.5);
    if (phX >= gridLeft && phX < gridRight) {
        buf.fillRect(Rect(phX - 3, rulerTop + 2, phX + 4, rulerTop + 7), pal.accentOrange);
        buf.fillRect(Rect(phX, rulerTop + 4, phX + 2, velTop), pal.accentOrange);
    }

    // 6. Bottom Velocity / Event Lane
    if (velOn && velTop < rc.bottom) {
        Rect laneHdr = Rect(rc.left, velTop, gridLeft, rc.bottom);
        Rect laneBg = Rect(gridLeft, velTop, gridRight, rc.bottom);
        buf.fillRect(laneHdr, pal.bgPanel);
        buf.fillRect(laneBg, pal.bgPanelAlt);
        buf.fillRect(Rect(rc.left, velTop, rc.right, velTop + 1), pal.borderSubtle);
        if (!fnt.isNull) {
            fnt.drawText(buf, laneHdr.left + 6, velTop + 8, "Velocity"d, pal.fgMuted);
        }

        const int stemBot = rc.bottom - 5;
        const int stemTopMax = velTop + 8;
        const int stemSpan = stemBot - stemTopMax;
        if (stemSpan > 8) {
            foreach (const ref ev; events) {
                if (ev.cc_number != 0 || ev.velocity == 0) continue;
                const double startBeat = cast(double)ev.tick / resF;
                const int sx = gridLeft + cast(int)((startBeat - scrollBeats) * pixelsPerBeat + 0.5) + 4;
                if (sx < gridLeft + 2 || sx >= gridRight - 2) continue;
                const float normVel = cast(float)ev.velocity / 127.0f;
                const int sy = stemBot - cast(int)(normVel * stemSpan);
                buf.fillRect(Rect(sx - 1, sy, sx + 1, stemBot), pal.accentGreen);
                drawFilledCircle(buf, sx, sy, 3.2f, pal.noteFill, pal.bgLcd);
            }
        }
    }
}

/// Interactive FL Studio 2026 Piano Roll Widget (Draw/Delete/Move/Resize Notes + Velocity Lane).
class PianoRollCanvasWidget : Widget {
    private TransportModel* model_;

    private enum DragMode { NONE, MOVE_NOTE, RESIZE_NOTE, VELOCITY }
    private DragMode dragMode_ = DragMode.NONE;
    private ptrdiff_t dragNoteIdx_ = -1;
    private long dragOffsetTick_ = 0;
    private bool dirtyDrag_ = false;

    int pitchLow = 48;
    int pitchHigh = 72;
    double pixelsPerBeat = 64.0;
    double scrollBeats = 0.0;
    int numBeats = 16;

    enum int KEY_WIDTH = 54;
    enum int ROW_HEIGHT = 14;

    void delegate(const(MidiEvent)[] updatedEvents) onNotesChanged;
    void delegate(uint pitch) onPreviewNote;
    void delegate(int channelIdx) onTargetChannelChanged;

    this(string id, TransportModel* model) {
        super(id);
        model_ = model;
        layoutWidth = FILL_PARENT;
        layoutHeight = FILL_PARENT;
    }

    private void syncActiveNotesToChannel() {
        if (!model_) return;
        const int ch = model_.activeMidi.channelIndex;
        if (ch >= 0 && ch < cast(int)model_.channels.length) {
            model_.channels[ch].notes = model_.activeMidi.events.dup;
        }
    }

    override void onDraw(DrawBuf buf) {
        if (visibility != Visibility.Visible) return;
        drawPianoRollCanvas(buf, _pos, model_, pitchLow, pitchHigh,
                            pixelsPerBeat, numBeats, KEY_WIDTH, ROW_HEIGHT, font, scrollBeats);
    }

    override bool onMouseEvent(MouseEvent event) {
        if (event.action == MouseAction.FocusOut) {
            return dragMode_ != DragMode.NONE;
        }
        if (!model_) return super.onMouseEvent(event);

        const int gridLeft = _pos.left + KEY_WIDTH;
        const int rulerTop = _pos.top + PR_TOOLBAR_H;
        const int gridTop = rulerTop + PR_RULER_H;
        const bool velOn = model_.showVelocityLane;
        const int velTop = (velOn && _pos.bottom - gridTop > PR_VEL_H + 60) ? (_pos.bottom - PR_VEL_H) : _pos.bottom;
        const int res = (model_.activeMidi.resolution > 0) ? model_.activeMidi.resolution : 480;
        const long stepTicks = res / 4;

        if (event.action == MouseAction.Wheel) {
            if ((event.flags & MouseFlag.Control) != 0) {
                pixelsPerBeat *= (event.wheelDelta > 0) ? 1.15 : 0.87;
                if (pixelsPerBeat < 24.0) pixelsPerBeat = 24.0;
                if (pixelsPerBeat > 180.0) pixelsPerBeat = 180.0;
            } else if ((event.flags & MouseFlag.Shift) != 0) {
                scrollBeats -= event.wheelDelta * 0.5;
                if (scrollBeats < 0.0) scrollBeats = 0.0;
                if (scrollBeats > cast(double)(numBeats - 2)) scrollBeats = cast(double)(numBeats - 2);
            } else {
                const int span = pitchHigh - pitchLow;
                const int delta = (event.wheelDelta > 0) ? 2 : -2;
                int newLow = pitchLow + delta;
                if (newLow < 24) newLow = 24;
                if (newLow + span > 108) newLow = 108 - span;
                pitchLow = newLow;
                pitchHigh = newLow + span;
            }
            invalidate();
            return true;
        }

        if (event.action == MouseAction.ButtonDown) {
            activateParentWindow(this);

            // 1. Toolbar clicks
            if (event.y >= _pos.top && event.y < rulerTop) {
                static immutable EditorTool[6] TOOL_ENUMS = [
                    EditorTool.DRAW, EditorTool.PAINT, EditorTool.DELETE,
                    EditorTool.SLICE, EditorTool.SELECT, EditorTool.ZOOM
                ];
                foreach (i, t; TOOL_ENUMS) {
                    const int bx0 = _pos.left + 6 + cast(int)i * 24;
                    if (event.x >= bx0 && event.x <= bx0 + 21) {
                        model_.pianoRollTool = t;
                        invalidate();
                        return true;
                    }
                }
                Rect tgtRc = Rect(_pos.left + 158, _pos.top + 4, _pos.left + 290, rulerTop - 4);
                if (tgtRc.isPointInside(event.x, event.y) && model_.channels.length > 0) {
                    const int dir = (event.x < tgtRc.left + tgtRc.width / 4) ? -1 : 1;
                    model_.selectPianoRollChannel(model_.activeMidi.channelIndex + dir);
                    if (onTargetChannelChanged !is null) {
                        onTargetChannelChanged(model_.activeMidi.channelIndex);
                    }
                    invalidate();
                    return true;
                }
                Rect ghostRc = Rect(_pos.right - 104, _pos.top + 4, _pos.right - 54, rulerTop - 4);
                Rect velRc = Rect(_pos.right - 50, _pos.top + 4, _pos.right - 6, rulerTop - 4);
                if (ghostRc.isPointInside(event.x, event.y)) {
                    model_.ghostNotesEnabled = !model_.ghostNotesEnabled;
                    invalidate();
                    return true;
                }
                if (velRc.isPointInside(event.x, event.y)) {
                    model_.showVelocityLane = !model_.showVelocityLane;
                    invalidate();
                    return true;
                }
                return true;
            }

            // 2. Bottom Velocity Lane edit
            if (velOn && event.y >= velTop && event.x >= gridLeft) {
                const double beat = scrollBeats + cast(double)(event.x - gridLeft) / pixelsPerBeat;
                const long clickTick = cast(long)(beat * res);
                ptrdiff_t bestIdx = -1;
                long bestDist = stepTicks * 2;
                foreach (i, const ref ev; model_.activeMidi.events) {
                    if (ev.cc_number != 0 || ev.velocity == 0) continue;
                    const long d = (ev.tick > clickTick) ? (ev.tick - clickTick) : (clickTick - ev.tick);
                    if (d < bestDist) {
                        bestDist = d;
                        bestIdx = cast(ptrdiff_t)i;
                    }
                }
                if (bestIdx >= 0) {
                    dragMode_ = DragMode.VELOCITY;
                    dragNoteIdx_ = bestIdx;
                    dirtyDrag_ = true;
                    updateVelocityFromY(bestIdx, event.y, velTop);
                    return true;
                }
                return true;
            }

            // 3. Piano Keys / Note Grid
            if (event.y < gridTop || event.y >= velTop) return true;
            const int row = (event.y - gridTop) / ROW_HEIGHT;
            const int pitch = pitchHigh - row;
            if (pitch < pitchLow || pitch > pitchHigh) return true;

            if (event.x < gridLeft) {
                if (onPreviewNote !is null) onPreviewNote(cast(uint)pitch);
                return true;
            }

            if (model_.pianoRollTool == EditorTool.ZOOM) {
                pixelsPerBeat *= (event.button == MouseButton.Right) ? 0.8 : 1.25;
                if (pixelsPerBeat < 24.0) pixelsPerBeat = 24.0;
                if (pixelsPerBeat > 180.0) pixelsPerBeat = 180.0;
                invalidate();
                return true;
            }

            const double beat = scrollBeats + cast(double)(event.x - gridLeft) / pixelsPerBeat;
            if (beat < 0.0) return true;
            const long clickTick = cast(long)(beat * res);

            // Hit-test existing note
            ptrdiff_t hitIdx = -1;
            foreach (i, const ref ev; model_.activeMidi.events) {
                if (ev.cc_number != 0 || ev.velocity == 0) continue;
                const long dur = (ev.duration_ticks > 0) ? ev.duration_ticks : stepTicks;
                if (cast(int)ev.pitch == pitch && clickTick >= ev.tick && clickTick <= ev.tick + dur) {
                    hitIdx = cast(ptrdiff_t)i;
                    break;
                }
            }

            if (hitIdx >= 0) {
                if (event.button == MouseButton.Right || model_.pianoRollTool == EditorTool.DELETE) {
                    model_.activeMidi.events = model_.activeMidi.events[0 .. hitIdx] ~
                                               model_.activeMidi.events[hitIdx + 1 .. $];
                    syncActiveNotesToChannel();
                    if (onNotesChanged !is null) onNotesChanged(model_.activeMidi.events);
                    invalidate();
                    return true;
                }
                if (model_.pianoRollTool == EditorTool.SLICE) {
                    const auto ev = model_.activeMidi.events[hitIdx];
                    const long dur = (ev.duration_ticks > 0) ? ev.duration_ticks : stepTicks;
                    const long splitTick = ((clickTick + stepTicks / 2) / stepTicks) * stepTicks;
                    if (splitTick > ev.tick && splitTick < ev.tick + dur) {
                        MidiEvent rightEv = ev;
                        rightEv.tick = splitTick;
                        rightEv.duration_ticks = (ev.tick + dur) - splitTick;
                        model_.activeMidi.events[hitIdx].duration_ticks = splitTick - ev.tick;
                        model_.activeMidi.events ~= rightEv;
                        syncActiveNotesToChannel();
                        if (onNotesChanged !is null) onNotesChanged(model_.activeMidi.events);
                        invalidate();
                    }
                    return true;
                }
                const auto ev = model_.activeMidi.events[hitIdx];
                const int x1 = gridLeft + cast(int)((cast(double)(ev.tick + ev.duration_ticks) / res - scrollBeats) * pixelsPerBeat);
                if (event.x >= x1 - 7) {
                    dragMode_ = DragMode.RESIZE_NOTE;
                } else {
                    dragMode_ = DragMode.MOVE_NOTE;
                    dragOffsetTick_ = clickTick - ev.tick;
                }
                dragNoteIdx_ = hitIdx;
                dirtyDrag_ = false;
                if (onPreviewNote !is null) onPreviewNote(cast(uint)pitch);
                return true;
            }

            // Insert new snapped note
            if (event.button == MouseButton.Left &&
                (model_.pianoRollTool == EditorTool.DRAW || model_.pianoRollTool == EditorTool.PAINT)) {
                const long snappedTick = (clickTick / stepTicks) * stepTicks;
                MidiEvent ne;
                ne.tick = snappedTick;
                ne.pitch = cast(uint)pitch;
                ne.duration_ticks = res / 2;
                ne.velocity = 104;
                model_.activeMidi.events ~= ne;
                dragMode_ = DragMode.RESIZE_NOTE;
                dragNoteIdx_ = cast(ptrdiff_t)model_.activeMidi.events.length - 1;
                dirtyDrag_ = false;
                syncActiveNotesToChannel();
                if (onPreviewNote !is null) onPreviewNote(cast(uint)pitch);
                if (onNotesChanged !is null) onNotesChanged(model_.activeMidi.events);
                invalidate();
                return true;
            }
            return true;
        }

        if (event.action == MouseAction.Move && dragMode_ != DragMode.NONE &&
            dragNoteIdx_ >= 0 && dragNoteIdx_ < cast(ptrdiff_t)model_.activeMidi.events.length) {
            if (dragMode_ == DragMode.VELOCITY) {
                dirtyDrag_ = true;
                updateVelocityFromY(dragNoteIdx_, event.y, velTop);
                return true;
            }
            const double beat = scrollBeats + cast(double)(event.x - gridLeft) / pixelsPerBeat;
            const long curTick = cast(long)(beat * res);
            auto ev = &model_.activeMidi.events[dragNoteIdx_];

            if (dragMode_ == DragMode.MOVE_NOTE) {
                const int row = (event.y - gridTop) / ROW_HEIGHT;
                int newPitch = pitchHigh - row;
                if (newPitch < pitchLow) newPitch = pitchLow;
                if (newPitch > pitchHigh) newPitch = pitchHigh;
                long newTick = ((curTick - dragOffsetTick_) / stepTicks) * stepTicks;
                if (newTick < 0) newTick = 0;
                ev.pitch = cast(uint)newPitch;
                ev.tick = newTick;
                dirtyDrag_ = true;
                invalidate();
            } else if (dragMode_ == DragMode.RESIZE_NOTE) {
                long newDur = ((curTick - ev.tick + stepTicks - 1) / stepTicks) * stepTicks;
                if (newDur < stepTicks) newDur = stepTicks;
                ev.duration_ticks = newDur;
                dirtyDrag_ = true;
                invalidate();
            }
            return true;
        }

        if ((event.action == MouseAction.ButtonUp || event.action == MouseAction.Cancel) &&
            dragMode_ != DragMode.NONE) {
            if (dirtyDrag_) {
                syncActiveNotesToChannel();
                if (onNotesChanged !is null) onNotesChanged(model_.activeMidi.events);
            }
            dragMode_ = DragMode.NONE;
            dragNoteIdx_ = -1;
            dirtyDrag_ = false;
            return true;
        }

        return super.onMouseEvent(event);
    }

    private void updateVelocityFromY(ptrdiff_t noteIdx, int my, int velTop) {
        const int stemBot = _pos.bottom - 5;
        const int stemTopMax = velTop + 8;
        if (stemBot <= stemTopMax) return;
        float norm = cast(float)(stemBot - my) / cast(float)(stemBot - stemTopMax);
        if (norm < 0.05f) norm = 0.05f;
        if (norm > 1.0f) norm = 1.0f;
        model_.activeMidi.events[noteIdx].velocity = cast(uint)(norm * 127.0f + 0.5f);
        invalidate();
    }
}

unittest {
    import dlangui.graphics.drawbuf : ColorDrawBuf;

    TransportModel model;
    model.initDefaultTemplate();
    model.activeMidi.events = [];
    MidiEvent ev;
    ev.tick = 0;
    ev.pitch = 60;
    ev.duration_ticks = 480;
    ev.velocity = 110;
    model.activeMidi.events ~= ev;

    auto buf = new ColorDrawBuf(360, 260);
    drawPianoRollCanvas(buf, Rect(0, 0, 360, 260), &model, 56, 64, 64.0, 4, 54, 14);

    const pal = model.palette();
    // Note pitch 60 when pitchHigh=64 is row r=4.
    // gridTop = PR_TOOLBAR_H(28) + PR_RULER_H(18) = 46 -> row 4 y = 46 + 4*14 = 102..116, x=54..118
    const uint c = buf.scanLine(109)[70];
    assert(c == pal.noteFill, "Expected FL Studio Piano Roll note pixel to be pal.noteFill");

    // Regression 1: Piano Roll must draw a playhead line when playheadPosSec > 0
    model.bpm = 120.0;
    model.playheadPosSec = 1.0; // beat 2 -> x = 54 + 2*64 = 182
    drawPianoRollCanvas(buf, Rect(0, 0, 360, 260), &model, 56, 64, 64.0, 4, 54, 14);
    assert(buf.scanLine(PR_TOOLBAR_H + 8)[182] == pal.accentOrange,
           "Expected Piano Roll playhead line at beat 2");

    // Regression 2: Switching target channels in Piano Roll preserves per-channel notes
    import dlangui.widgets.styles : currentTheme, createDefaultTheme;
    import gui_d.theme : destroyWidgetTree;
    if (currentTheme is null) currentTheme = createDefaultTheme();
    auto pr = new PianoRollCanvasWidget("pr", &model);
    scope (exit) destroyWidgetTree(pr);
    pr.measure(400, 300);
    pr.layout(Rect(0, 0, 400, 300));
    model.activeMidi.channelIndex = 3; // Acid Bass (has 1 note)
    // Click > side of target channel pill (158..290 -> x=270) -> switch to channel 4 (E-Piano)
    pr.onMouseEvent(new MouseEvent(MouseAction.ButtonDown, MouseButton.Left, MouseFlag.LButton, 270, 14));
    assert(model.activeMidi.channelIndex == 4);
    assert(model.activeMidi.events.length == 0,
           "Expected E-Piano channel to start with its own empty notes, not Acid Bass's notes");
    // Click < side of target channel pill (x=165) -> back to channel 3 retaining its note
    pr.onMouseEvent(new MouseEvent(MouseAction.ButtonDown, MouseButton.Left, MouseFlag.LButton, 165, 14));
    assert(model.activeMidi.channelIndex == 3);
    assert(model.activeMidi.events.length == 1 && model.activeMidi.events[0].pitch == 60,
           "Expected Acid Bass notes to be restored when switching back");

    // Regression 3: SLICE splits note at beat boundary and ZOOM changes scale
    model.pianoRollTool = EditorTool.SLICE;
    const int noteMidX = 54 + 32; // beat 0.5 (tick 240)
    const int noteY = PR_TOOLBAR_H + PR_RULER_H + (72 - 60) * 14 + 6;
    pr.onMouseEvent(new MouseEvent(MouseAction.ButtonDown, MouseButton.Left, MouseFlag.LButton, cast(short)noteMidX, cast(short)noteY));
    assert(model.activeMidi.events.length == 2, "Expected SLICE in Piano Roll to split note");

    const double prevBeatPx = pr.pixelsPerBeat;
    model.pianoRollTool = EditorTool.ZOOM;
    pr.onMouseEvent(new MouseEvent(MouseAction.ButtonDown, MouseButton.Left, MouseFlag.LButton, cast(short)noteMidX, cast(short)noteY));
    assert(pr.pixelsPerBeat > prevBeatPx, "Expected ZOOM in Piano Roll to zoom in");
}
