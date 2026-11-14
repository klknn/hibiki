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

/// Custom Y2K DAW Mixer & Meter Canvas rendered via `dlangui`'s `DrawBuf`.
class MixerCanvasWidget : Widget {
    private TransportModel* model_;

    this(string id, TransportModel* model) {
        super(id);
        model_ = model;
        layoutWidth = FILL_PARENT;
        layoutHeight = FILL_PARENT;
        minWidth = 640;
        minHeight = 320;
    }

    override void onDraw(DrawBuf buf) {
        if (visibility != Visibility.Visible) return;
        Rect rc = _pos;
        buf.fillRect(rc, Y2kColor.BG_WORKSPACE);

        // Outer recessed mixer well
        Rect well = Rect(rc.left + 8, rc.top + 8, rc.right - 8, rc.bottom - 8);
        drawBevelPanel(buf, well, Y2kColor.BG_PANEL_DARK, true);

        const int stripCount = (model_ && model_.tracks.length > 0)
                                   ? cast(int)model_.tracks.length
                                   : 8;
        const int stripW = 88;
        const int gap = 4;
        int x = well.left + 8;
        const int y0 = well.top + 8;
        const int y1 = well.bottom - 8;

        foreach (i; 0 .. stripCount) {
            if (x + stripW > well.right - 8) break;
            Rect stripRc = Rect(x, y0, x + stripW, y1);
            drawBevelPanel(buf, stripRc, Y2kColor.BG_PANEL, false);

            // Header well
            Rect hdr = Rect(x + 4, y0 + 4, x + stripW - 4, y0 + 26);
            drawBevelPanel(buf, hdr, Y2kColor.BG_LCD, true);

            string title = (model_ && i < model_.tracks.length)
                               ? model_.tracks[i].name
                               : format("CH %02d", i + 1);
            float pL = (model_ && i < model_.tracks.length) ? model_.tracks[i].peakL : 0.0f;
            float pR = (model_ && i < model_.tracks.length) ? model_.tracks[i].peakR : 0.0f;

            // Segmented LED meter
            Rect meterRc = Rect(x + stripW - 30, y0 + 34, x + stripW - 8, y1 - 36);
            drawSegmentedMeter(buf, meterRc, pL, pR, 14);

            // Fader track groove
            Rect faderGroove = Rect(x + 22, y0 + 38, x + 28, y1 - 40);
            drawBevelPanel(buf, faderGroove, Y2kColor.BG_PANEL_DARK, true);

            // Fader cap (Y2K 3D beveled cap)
            float vol = (model_ && i < model_.tracks.length) ? model_.tracks[i].volume : 0.8f;
            if (vol < 0.0f) vol = 0.0f;
            if (vol > 1.0f) vol = 1.0f;
            int capY = faderGroove.bottom - cast(int)(vol * (faderGroove.height - 16)) - 16;
            Rect capRc = Rect(x + 12, capY, x + 38, capY + 16);
            drawBevelPanel(buf, capRc, 0xFF606874, false);

            // Draw channel label inside header
            auto fnt = font;
            if (fnt !is null) {
                import std.utf : toUTF32;
                dstring dtitle = toUTF32(title);
                fnt.drawText(buf, hdr.left + 4, hdr.top + 4, dtitle, Y2kColor.FG_LCD_CYAN);
            }

            x += stripW + gap;
        }
    }
}
