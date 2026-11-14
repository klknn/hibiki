module gui_d.widgets;

import dlangui;
import dlangui.graphics.drawbuf : DrawBuf;
import dlangui.widgets.widget : Widget;
import gui_d.theme : peakToMeterSegments, segmentColor, Y2kColor;
import gui_d.transport_model : TransportModel;
import std.format : format;

/// Draws a crisp 1px Y2K raised or sunken bevel panel.
void drawBevelPanel(DrawBuf buf, Rect rc, uint fillColor, bool sunken = false) {
    if (rc.width <= 0 || rc.height <= 0) return;
    buf.fillRect(rc, fillColor);
    const uint topLeft = sunken ? Y2kColor.BEVEL_DARK : Y2kColor.BEVEL_LIGHT;
    const uint bottomRight = sunken ? Y2kColor.BEVEL_LIGHT : Y2kColor.BEVEL_DARK;
    buf.fillRect(Rect(rc.left, rc.top, rc.right, rc.top + 1), topLeft);
    buf.fillRect(Rect(rc.left, rc.top, rc.left + 1, rc.bottom), topLeft);
    buf.fillRect(Rect(rc.left, rc.bottom - 1, rc.right, rc.bottom), bottomRight);
    buf.fillRect(Rect(rc.right - 1, rc.top, rc.right, rc.bottom), bottomRight);
}

/// Draws a vertical stereo segmented LED meter inside `rc`.
void drawSegmentedMeter(DrawBuf buf, Rect rc, float peakL, float peakR, int segments = 14) {
    drawBevelPanel(buf, rc, Y2kColor.BG_PANEL_DARK, true);
    if (rc.width < 8 || rc.height < segments * 3) return;
    const int innerLeft = rc.left + 2;
    const int innerW = rc.width - 4;
    const int innerH = rc.height - 4;
    const int colW = (innerW - 2) / 2;
    const int segH = innerH / segments;
    const int litL = peakToMeterSegments(peakL, segments);
    const int litR = peakToMeterSegments(peakR, segments);

    foreach (s; 0 .. segments) {
        const int y0 = rc.bottom - 2 - (s + 1) * segH + 1;
        const int y1 = rc.bottom - 2 - s * segH - 1;
        if (y1 <= y0) continue;
        const uint onCol = segmentColor(s, segments);
        buf.fillRect(Rect(innerLeft, y0, innerLeft + colW, y1),
                     s < litL ? onCol : Y2kColor.LED_OFF);
        buf.fillRect(Rect(innerLeft + colW + 2, y0, innerLeft + colW * 2 + 2, y1),
                     s < litR ? onCol : Y2kColor.LED_OFF);
    }
}

/// Truncates `label` to `maxChars` characters so it fits inside a mixer strip header.
string truncateLabel(string label, size_t maxChars = 10) pure @safe {
    if (label.length <= maxChars) return label;
    if (maxChars <= 2) return label[0 .. maxChars];
    return label[0 .. maxChars - 2] ~ "..";
}

/// Custom Y2K DAW Mixer & Meter Canvas rendered via `dlangui`'s `DrawBuf`.
class MixerCanvasWidget : Widget {
    private TransportModel* model_;
    private int draggingStrip_ = -1;

    void delegate(int trackIdx, float volume) onVolumeChange;
    void delegate(int trackIdx, bool muted) onMuteToggle;
    void delegate(int trackIdx, bool soloed) onSoloToggle;

    this(string id, TransportModel* model) {
        super(id);
        model_ = model;
        layoutWidth = FILL_PARENT;
        layoutHeight = FILL_PARENT;
        minWidth = 640;
        minHeight = 320;
    }

    private Rect stripRect(Rect well, int i) const @safe {
        enum int stripW = 88;
        enum int gap = 4;
        int x = well.left + 8 + i * (stripW + gap);
        return Rect(x, well.top + 8, x + stripW, well.bottom - 8);
    }

    private Rect faderGrooveRect(Rect stripRc) const @safe {
        return Rect(stripRc.left + 22, stripRc.top + 38, stripRc.left + 28, stripRc.bottom - 40);
    }

    private Rect muteBtnRect(Rect stripRc) const @safe {
        return Rect(stripRc.left + 8, stripRc.bottom - 28, stripRc.left + 38, stripRc.bottom - 8);
    }

    private Rect soloBtnRect(Rect stripRc) const @safe {
        return Rect(stripRc.left + 44, stripRc.bottom - 28, stripRc.left + 74, stripRc.bottom - 8);
    }

    private int effectiveStripCount() const pure @safe {
        return (model_ && model_.tracks.length > 0) ? cast(int)model_.tracks.length : 8;
    }

    private void updateFaderFromY(int stripIdx, int y, Rect groove) {
        if (!model_ || groove.height <= 16) return;
        const size_t idx = model_.ensureTrackIndex(stripIdx);
        float norm = cast(float)(groove.bottom - 8 - y) / cast(float)(groove.height - 16);
        if (norm < 0.0f) norm = 0.0f;
        if (norm > 1.0f) norm = 1.0f;
        model_.tracks[idx].volume = norm;
        if (onVolumeChange !is null) {
            onVolumeChange(model_.tracks[idx].index, norm);
        }
        invalidate();
    }

    override bool onMouseEvent(MouseEvent event) {
        Rect rc = _pos;
        Rect well = Rect(rc.left + 8, rc.top + 8, rc.right - 8, rc.bottom - 8);
        const int count = effectiveStripCount();

        if (event.action == MouseAction.ButtonDown && event.button == MouseButton.Left) {
            foreach (i; 0 .. count) {
                Rect stripRc = stripRect(well, i);
                if (stripRc.right > well.right - 8) break;
                if (!stripRc.isPointInside(event.x, event.y)) continue;

                Rect mRc = muteBtnRect(stripRc);
                if (mRc.isPointInside(event.x, event.y) && model_) {
                    const size_t idx = model_.ensureTrackIndex(i);
                    model_.tracks[idx].muted = !model_.tracks[idx].muted;
                    if (onMuteToggle !is null) {
                        onMuteToggle(model_.tracks[idx].index, model_.tracks[idx].muted);
                    }
                    invalidate();
                    return true;
                }

                Rect sRc = soloBtnRect(stripRc);
                if (sRc.isPointInside(event.x, event.y) && model_) {
                    const size_t idx = model_.ensureTrackIndex(i);
                    model_.tracks[idx].soloed = !model_.tracks[idx].soloed;
                    if (onSoloToggle !is null) {
                        onSoloToggle(model_.tracks[idx].index, model_.tracks[idx].soloed);
                    }
                    invalidate();
                    return true;
                }

                Rect groove = faderGrooveRect(stripRc);
                Rect faderHit = Rect(stripRc.left + 8, groove.top - 4, stripRc.left + 44, groove.bottom + 4);
                if (faderHit.isPointInside(event.x, event.y)) {
                    draggingStrip_ = i;
                    updateFaderFromY(i, event.y, groove);
                    return true;
                }
            }
        } else if (event.action == MouseAction.Move && draggingStrip_ >= 0) {
            Rect stripRc = stripRect(well, draggingStrip_);
            updateFaderFromY(draggingStrip_, event.y, faderGrooveRect(stripRc));
            return true;
        } else if (event.action == MouseAction.ButtonUp && draggingStrip_ >= 0) {
            draggingStrip_ = -1;
            return true;
        }
        return super.onMouseEvent(event);
    }

    override void onDraw(DrawBuf buf) {
        if (visibility != Visibility.Visible) return;
        Rect rc = _pos;
        buf.fillRect(rc, Y2kColor.BG_WORKSPACE);

        // Outer recessed mixer well
        Rect well = Rect(rc.left + 8, rc.top + 8, rc.right - 8, rc.bottom - 8);
        drawBevelPanel(buf, well, Y2kColor.BG_PANEL_DARK, true);

        const int stripCount = effectiveStripCount();

        foreach (i; 0 .. stripCount) {
            Rect stripRc = stripRect(well, i);
            if (stripRc.right > well.right - 8) break;
            drawBevelPanel(buf, stripRc, Y2kColor.BG_PANEL, false);

            // Header well
            Rect hdr = Rect(stripRc.left + 4, stripRc.top + 4, stripRc.right - 4, stripRc.top + 26);
            drawBevelPanel(buf, hdr, Y2kColor.BG_LCD, true);

            const bool hasTrack = (model_ && i < model_.tracks.length);
            string title = hasTrack ? model_.tracks[i].name : format("CH %02d", i + 1);
            float pL = hasTrack ? model_.tracks[i].peakL : 0.0f;
            float pR = hasTrack ? model_.tracks[i].peakR : 0.0f;
            bool muted = hasTrack ? model_.tracks[i].muted : false;
            bool soloed = hasTrack ? model_.tracks[i].soloed : false;

            // Segmented LED meter
            Rect meterRc = Rect(stripRc.right - 30, stripRc.top + 34, stripRc.right - 8, stripRc.bottom - 36);
            drawSegmentedMeter(buf, meterRc, pL, pR, 14);

            // Fader track groove
            Rect faderGroove = faderGrooveRect(stripRc);
            drawBevelPanel(buf, faderGroove, Y2kColor.BG_PANEL_DARK, true);

            // Fader cap (Y2K 3D beveled cap)
            float vol = hasTrack ? model_.tracks[i].volume : 0.8f;
            if (vol < 0.0f) vol = 0.0f;
            if (vol > 1.0f) vol = 1.0f;
            int capY = faderGroove.bottom - cast(int)(vol * (faderGroove.height - 16)) - 16;
            Rect capRc = Rect(stripRc.left + 12, capY, stripRc.left + 38, capY + 16);
            drawBevelPanel(buf, capRc, 0x00606874, false);

            // Mute (M) and Solo (S) Y2K buttons
            Rect mRc = muteBtnRect(stripRc);
            Rect sRc = soloBtnRect(stripRc);
            drawBevelPanel(buf, mRc, muted ? Y2kColor.ACCENT_MUTE : Y2kColor.BG_PANEL_DARK, muted);
            drawBevelPanel(buf, sRc, soloed ? Y2kColor.ACCENT_SOLO : Y2kColor.BG_PANEL_DARK, soloed);

            // Draw channel label inside header and M/S glyphs
            auto fnt = font;
            if (fnt !is null) {
                import std.utf : toUTF32;
                dstring dtitle = toUTF32(truncateLabel(title, 10));
                fnt.drawText(buf, hdr.left + 4, hdr.top + 4, dtitle, Y2kColor.FG_LCD_CYAN);
                fnt.drawText(buf, mRc.left + 10, mRc.top + 3, "M"d,
                             muted ? Y2kColor.BG_PANEL_DARK : Y2kColor.FG_TEXT);
                fnt.drawText(buf, sRc.left + 10, sRc.top + 3, "S"d,
                             soloed ? Y2kColor.BG_PANEL_DARK : Y2kColor.FG_TEXT);
            }
        }
    }
}

unittest {
    import dlangui.graphics.drawbuf : ColorDrawBuf;

    assert(truncateLabel("Bassline", 10) == "Bassline");
    assert(truncateLabel("VeryLongTrackName", 10) == "VeryLong..");

    // Headless DrawBuf rendering verification (without requiring Platform.instance)
    auto buf = new ColorDrawBuf(120, 200);
    drawBevelPanel(buf, Rect(0, 0, 120, 200), Y2kColor.BG_PANEL, false);
    assert(buf.scanLine(0)[0] == Y2kColor.BEVEL_LIGHT);
    assert(buf.scanLine(199)[119] == Y2kColor.BEVEL_DARK);
    assert(buf.scanLine(10)[10] == Y2kColor.BG_PANEL);

    drawSegmentedMeter(buf, Rect(10, 10, 40, 180), 1.0f, 0.0f, 14);
    // Sunken meter border should have BEVEL_DARK at top-left
    assert(buf.scanLine(10)[10] == Y2kColor.BEVEL_DARK);
}
