module gui_d.piano_roll;

import dlangui;
import dlangui.graphics.drawbuf : DrawBuf;
import dlangui.widgets.widget : Widget;
import gui_d.theme : Y2kColor;
import gui_d.transport_model : TransportModel;
import gui_d.widgets : drawBevelPanel;
import pb.core : MidiEvent;
import std.format : format;

private bool isBlackKey(int pitch) pure nothrow @safe {
    const int pc = ((pitch % 12) + 12) % 12;
    return pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10;
}

/// Renders the Y2K Piano Roll keyboard, beat/16th grid, and MIDI note bars onto `buf`.
void drawPianoRollCanvas(DrawBuf buf, Rect rc, int pitchLow, int pitchHigh,
                         int resolution, const(MidiEvent)[] events,
                         double pixelsPerBeat = 64.0, int numBeats = 16,
                         int keyWidth = 48, int rowHeight = 14) {
    buf.fillRect(rc, Y2kColor.BG_WORKSPACE);

    const int numRows = pitchHigh - pitchLow + 1;
    const int gridLeft = rc.left + keyWidth;
    const int gridRight = rc.right;

    foreach (r; 0 .. numRows) {
        const int pitch = pitchHigh - r;
        const int y0 = rc.top + r * rowHeight;
        const int y1 = y0 + rowHeight;
        if (y0 >= rc.bottom) break;

        // Piano key on the left
        const bool black = isBlackKey(pitch);
        const uint keyBg = black ? 0x00242830 : 0x00B8BEC8;
        buf.fillRect(Rect(rc.left, y0, gridLeft - 1, y1 - 1), keyBg);
        buf.fillRect(Rect(rc.left, y1 - 1, gridLeft, y1), Y2kColor.BEVEL_DARK);

        // Horizontal lane background
        const uint rowBg = black ? 0x00181C22 : 0x0020262E;
        buf.fillRect(Rect(gridLeft, y0, gridRight, y1 - 1), rowBg);
        buf.fillRect(Rect(gridLeft, y1 - 1, gridRight, y1), Y2kColor.BEVEL_DARK);
    }

    // Vertical beat & 16th subdivisions
    const int totalSubdivs = numBeats * 4;
    foreach (s; 0 .. totalSubdivs + 1) {
        const double beat = cast(double)s * 0.25;
        const int x = gridLeft + cast(int)(beat * pixelsPerBeat + 0.5);
        if (x < gridLeft || x >= gridRight) continue;
        const uint col = (s % 16 == 0) ? Y2kColor.BEVEL_LIGHT :
                         (s % 4 == 0) ? Y2kColor.BEVEL_LIGHT : Y2kColor.BEVEL_DARK;
        buf.fillRect(Rect(x, rc.top, x + 1, rc.bottom), col);
    }

    // MIDI Note bars
    const double res = (resolution > 0) ? cast(double)resolution : 480.0;
    foreach (const ref ev; events) {
        if (ev.cc_number != 0 || ev.velocity == 0) continue;
        const int p = cast(int)ev.pitch;
        if (p < pitchLow || p > pitchHigh) continue;

        const int row = pitchHigh - p;
        const int y0 = rc.top + row * rowHeight + 1;
        const int y1 = y0 + rowHeight - 2;
        if (y1 <= rc.top || y0 >= rc.bottom) continue;

        const double startBeat = cast(double)ev.tick / res;
        const double durBeat = (ev.duration_ticks > 0) ? (cast(double)ev.duration_ticks / res) : 0.25;
        int x0 = gridLeft + cast(int)(startBeat * pixelsPerBeat + 0.5);
        int x1 = gridLeft + cast(int)((startBeat + durBeat) * pixelsPerBeat + 0.5);
        if (x1 <= x0 + 3) x1 = x0 + 4;
        if (x1 <= gridLeft || x0 >= gridRight) continue;
        if (x0 < gridLeft) x0 = gridLeft;
        if (x1 > gridRight) x1 = gridRight;

        drawBevelPanel(buf, Rect(x0, y0, x1, y1), Y2kColor.LED_GREEN, false);
    }
}

/// Interactive Y2K Piano Roll widget: left-click empty cell adds a note, click note deletes it.
class PianoRollCanvasWidget : Widget {
    private TransportModel* model_;

    int pitchLow = 48;   // C3
    int pitchHigh = 72;  // C5
    double pixelsPerBeat = 64.0;
    int numBeats = 16;

    enum int KEY_WIDTH = 48;
    enum int ROW_HEIGHT = 14;

    void delegate(const(MidiEvent)[] updatedEvents) onNotesChanged;
    void delegate(uint pitch) onPreviewNote;

    this(string id, TransportModel* model) {
        super(id);
        model_ = model;
        layoutWidth = FILL_PARENT;
        layoutHeight = FILL_PARENT;
        minWidth = 520;
        minHeight = 200;
    }

    override void onDraw(DrawBuf buf) {
        super.onDraw(buf);
        const Rect rc = pos;
        const int res = (model_ && model_.activeMidi.resolution > 0) ? model_.activeMidi.resolution : 480;
        const(MidiEvent)[] evs = model_ ? model_.activeMidi.events : null;
        drawPianoRollCanvas(buf, rc, pitchLow, pitchHigh, res, evs,
                            pixelsPerBeat, numBeats, KEY_WIDTH, ROW_HEIGHT);

        // Octave labels on C keys
        auto f = font;
        foreach (r; 0 .. (pitchHigh - pitchLow + 1)) {
            const int pitch = pitchHigh - r;
            if (((pitch % 12) + 12) % 12 == 0) {
                const int y0 = rc.top + r * ROW_HEIGHT;
                if (y0 + ROW_HEIGHT > rc.bottom) break;
                const int octave = (pitch / 12) - 1;
                const uint txtCol = Y2kColor.BEVEL_DARK;
                f.drawText(buf, rc.left + 4, y0 + 1, toUTF32(format("C%d", octave)), txtCol);
            }
        }
        // Clip title badge
        if (model_) {
            const string title = format("Track %d  Clip %d  (%d notes) - Click to Add/Remove Note",
                                        model_.activeMidi.trackIndex + 1,
                                        model_.activeMidi.clipIndex,
                                        cast(int)model_.activeMidi.events.length);
            f.drawText(buf, rc.left + KEY_WIDTH + 6, rc.top + 3, toUTF32(title), Y2kColor.FG_LCD_AMBER);
        }
    }

    override bool onMouseEvent(MouseEvent event) {
        if (!model_ || event.action != MouseAction.ButtonDown || event.button != MouseButton.Left) {
            return super.onMouseEvent(event);
        }
        const Rect rc = pos;
        const int row = (event.y - rc.top) / ROW_HEIGHT;
        const int pitch = pitchHigh - row;
        if (pitch < pitchLow || pitch > pitchHigh) return true;

        // Clicking piano key column just previews the pitch
        if (event.x < rc.left + KEY_WIDTH) {
            if (onPreviewNote !is null) onPreviewNote(cast(uint)pitch);
            return true;
        }

        const int res = (model_.activeMidi.resolution > 0) ? model_.activeMidi.resolution : 480;
        const long stepTicks = res / 4; // 16th-note snap
        const double beat = cast(double)(event.x - (rc.left + KEY_WIDTH)) / pixelsPerBeat;
        if (beat < 0.0) return true;
        const long clickTick = cast(long)(beat * res);

        // Check if clicking an existing note -> delete it
        foreach (i, const ref ev; model_.activeMidi.events) {
            if (ev.cc_number != 0 || ev.velocity == 0) continue;
            if (cast(int)ev.pitch == pitch &&
                clickTick >= ev.tick &&
                clickTick <= ev.tick + (ev.duration_ticks > 0 ? ev.duration_ticks : stepTicks)) {
                model_.activeMidi.events = model_.activeMidi.events[0 .. i] ~
                                           model_.activeMidi.events[i + 1 .. $];
                if (onNotesChanged !is null) onNotesChanged(model_.activeMidi.events);
                invalidate();
                return true;
            }
        }

        // Otherwise insert a snapped note
        const long snappedTick = (clickTick / stepTicks) * stepTicks;
        MidiEvent ne;
        ne.tick = snappedTick;
        ne.pitch = cast(uint)pitch;
        ne.duration_ticks = res / 2; // 8th note default
        ne.velocity = 100;
        model_.activeMidi.events ~= ne;
        if (onPreviewNote !is null) onPreviewNote(cast(uint)pitch);
        if (onNotesChanged !is null) onNotesChanged(model_.activeMidi.events);
        invalidate();
        return true;
    }

    private dstring toUTF32(string s) const {
        import std.utf : toUTF32;
        return toUTF32(s);
    }
}

unittest {
    import dlangui.graphics.drawbuf : ColorDrawBuf;

    auto buf = new ColorDrawBuf(320, 140);
    buf.fill(Y2kColor.BG_WORKSPACE);

    MidiEvent ev;
    ev.tick = 0;
    ev.pitch = 60;
    ev.duration_ticks = 480;
    ev.velocity = 100;
    drawPianoRollCanvas(buf, Rect(0, 0, 320, 140), 56, 64, 480, [ev], 64.0, 4, 48, 14);

    // Row for pitch 60 when pitchHigh=64 is r=4 -> y=56..70, x=48..112 should contain FG_LED_GREEN
    const uint c = buf.scanLine(62)[60];
    assert(c == Y2kColor.LED_GREEN, "Expected Piano Roll note pixel to be FG_LED_GREEN");
}
