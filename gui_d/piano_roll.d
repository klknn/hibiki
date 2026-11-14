module gui_d.piano_roll;

import dlangui;
import dlangui.graphics.drawbuf : DrawBuf;
import dlangui.widgets.widget : Visibility, Widget;
import gui_d.theme : drawCard, drawFilledCircle, FlPalette, getFlPalette, truncateLabel;
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
    FontRef fnt = FontRef.init
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
            ? (pal.bgPanelAlt - 0x00040404)
            : (inScale ? pal.bgWindow : pal.bgPanelAlt);
        buf.fillRect(Rect(gridLeft, y0, gridRight, y1 - 1), laneBg);
        buf.fillRect(Rect(gridLeft, y1 - 1, gridRight, y1), pal.borderStrong);
    }

    // 3. Vertical Beat & 16th Subdivisions + Ruler Numbers
    const int totalSubdivs = numBeats * 4;
    foreach (s; 0 .. totalSubdivs + 1) {
        const double beat = cast(double)s * 0.25;
        const int x = gridLeft + cast(int)(beat * pixelsPerBeat + 0.5);
        if (x < gridLeft || x >= gridRight) continue;
        const bool isBar = (s % 16 == 0);
        const bool isBeat = (s % 4 == 0);
        const uint col = isBar ? pal.borderSubtle : (isBeat ? pal.bgControl : pal.borderStrong);
        buf.fillRect(Rect(x, gridTop, x + 1, velTop), col);
        if (isBar && !fnt.isNull && x + 14 < gridRight) {
            fnt.drawText(buf, x + 3, rulerTop + 2, toUTF32(format("%d", (s / 16) + 1)), pal.fgText);
        }
    }

    // 4. Ghost Notes (subtle reference chord/root notes from pattern)
    const int res = (model && model.activeMidi.resolution > 0) ? model.activeMidi.resolution : 480;
    if (ghostOn) {
        static immutable int[4] GHOST_PITCHES = [53, 57, 60, 64];
        foreach (idx, gp; GHOST_PITCHES) {
            if (gp < pitchLow || gp > pitchHigh) continue;
            const int row = pitchHigh - gp;
            const int y0 = gridTop + row * rowHeight + 2;
            const int y1 = y0 + rowHeight - 4;
            if (y1 <= gridTop || y0 >= velTop) continue;
            const int gx0 = gridLeft + cast(int)((idx * 1.0) * pixelsPerBeat);
            const int gx1 = gx0 + cast(int)(pixelsPerBeat * 0.9);
            if (gx1 > gridLeft && gx0 < gridRight) {
                buf.fillRect(Rect(gx0 < gridLeft ? gridLeft : gx0, y0,
                                  gx1 > gridRight ? gridRight : gx1, y1), pal.noteGhost);
            }
        }
    }

    // 5. Active MIDI Note Bars
    const(MidiEvent)[] events = model ? model.activeMidi.events : null;
    const double resF = cast(double)res;
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
        int x0 = gridLeft + cast(int)(startBeat * pixelsPerBeat + 0.5);
        int x1 = gridLeft + cast(int)((startBeat + durBeat) * pixelsPerBeat + 0.5);
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
                const int sx = gridLeft + cast(int)(startBeat * pixelsPerBeat + 0.5) + 4;
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

    int pitchLow = 48;
    int pitchHigh = 72;
    double pixelsPerBeat = 64.0;
    int numBeats = 16;

    enum int KEY_WIDTH = 54;
    enum int ROW_HEIGHT = 14;

    void delegate(const(MidiEvent)[] updatedEvents) onNotesChanged;
    void delegate(uint pitch) onPreviewNote;

    this(string id, TransportModel* model) {
        super(id);
        model_ = model;
        layoutWidth = FILL_PARENT;
        layoutHeight = FILL_PARENT;
    }

    override void onDraw(DrawBuf buf) {
        if (visibility != Visibility.Visible) return;
        drawPianoRollCanvas(buf, _pos, model_, pitchLow, pitchHigh,
                            pixelsPerBeat, numBeats, KEY_WIDTH, ROW_HEIGHT, font);
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
                    model_.activeMidi.channelIndex =
                        (model_.activeMidi.channelIndex + 1) % cast(int)model_.channels.length;
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
                const double beat = cast(double)(event.x - gridLeft) / pixelsPerBeat;
                const long clickTick = cast(long)(beat * res);
                ptrdiff_t bestIdx = -1;
                long bestDist = stepTicks * 2;
                foreach (i, const ref ev; model_.activeMidi.events) {
                    const long d = (ev.tick > clickTick) ? (ev.tick - clickTick) : (clickTick - ev.tick);
                    if (d < bestDist) {
                        bestDist = d;
                        bestIdx = cast(ptrdiff_t)i;
                    }
                }
                if (bestIdx >= 0) {
                    dragMode_ = DragMode.VELOCITY;
                    dragNoteIdx_ = bestIdx;
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

            const double beat = cast(double)(event.x - gridLeft) / pixelsPerBeat;
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
                    if (onNotesChanged !is null) onNotesChanged(model_.activeMidi.events);
                    invalidate();
                    return true;
                }
                const auto ev = model_.activeMidi.events[hitIdx];
                const int x1 = gridLeft + cast(int)((cast(double)(ev.tick + ev.duration_ticks) / res) * pixelsPerBeat);
                if (event.x >= x1 - 7) {
                    dragMode_ = DragMode.RESIZE_NOTE;
                } else {
                    dragMode_ = DragMode.MOVE_NOTE;
                    dragOffsetTick_ = clickTick - ev.tick;
                }
                dragNoteIdx_ = hitIdx;
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
                updateVelocityFromY(dragNoteIdx_, event.y, velTop);
                return true;
            }
            const double beat = cast(double)(event.x - gridLeft) / pixelsPerBeat;
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
                invalidate();
            } else if (dragMode_ == DragMode.RESIZE_NOTE) {
                long newDur = ((curTick - ev.tick + stepTicks - 1) / stepTicks) * stepTicks;
                if (newDur < stepTicks) newDur = stepTicks;
                ev.duration_ticks = newDur;
                invalidate();
            }
            return true;
        }

        if (event.action == MouseAction.ButtonUp && dragMode_ != DragMode.NONE) {
            if (onNotesChanged !is null) onNotesChanged(model_.activeMidi.events);
            dragMode_ = DragMode.NONE;
            dragNoteIdx_ = -1;
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
}
