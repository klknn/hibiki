module gui_d.channel_rack;

import dlangui;
import dlangui.graphics.drawbuf : DrawBuf;
import dlangui.widgets.widget : Widget;
import gui_d.theme : Y2kColor;
import gui_d.transport_model : ChannelState, TransportModel;
import gui_d.widgets : drawBevelPanel, truncateLabel;
import std.format : format;
import std.utf : toUTF32;

/// Renders the FL Studio Channel Rack & 16-Step Sequencer into `buf` within `rc`.
/// Separated from `ChannelRackWidget.onDraw` for headless `ColorDrawBuf` testing.
void drawChannelRackCanvas(
    DrawBuf buf,
    Rect rc,
    const(TransportModel)* model,
    FontRef fnt = FontRef.init
) {
    buf.fillRect(rc, Y2kColor.BG_WORKSPACE);
    Rect well = Rect(rc.left + 4, rc.top + 4, rc.right - 4, rc.bottom - 4);
    drawBevelPanel(buf, well, Y2kColor.BG_PANEL_DARK, true);
    if (!model || well.width < 220 || well.height < 48) return;

    enum int HEADER_H = 22;
    enum int ROW_H = 30;
    enum int LEFT_CONTROLS_W = 190;

    // Header bar
    Rect hdrRc = Rect(well.left + 1, well.top + 1, well.right - 1, well.top + HEADER_H);
    buf.fillRect(hdrRc, Y2kColor.BG_PANEL);
    if (!fnt.isNull) {
        const string patName = (model.selectedPatternIndex >= 0 &&
                                model.selectedPatternIndex < cast(int)model.patterns.length)
            ? model.patterns[model.selectedPatternIndex].name
            : "Pattern 1";
        fnt.drawText(buf, hdrRc.left + 8, hdrRc.top + 4,
                     toUTF32(format("CHANNEL RACK  [%s]", patName)),
                     Y2kColor.FG_LCD_AMBER);
    }

    const int stepAreaLeft = well.left + LEFT_CONTROLS_W;
    const int stepAreaW = well.right - stepAreaLeft - 8;
    const int stepW = (stepAreaW > 160) ? (stepAreaW / 16) : 14;
    const int activeStep = model.currentStepIndex();

    // Playhead step indicator bar
    if (activeStep >= 0 && activeStep < 16) {
        const int sx0 = stepAreaLeft + activeStep * stepW + 2;
        buf.fillRect(Rect(sx0, hdrRc.bottom - 4, sx0 + stepW - 4, hdrRc.bottom - 1), Y2kColor.LED_GREEN);
    }

    foreach (i, const ref ch; model.channels) {
        const int y0 = well.top + HEADER_H + 4 + cast(int)i * ROW_H;
        const int y1 = y0 + ROW_H - 3;
        if (y1 >= well.bottom - 4) break;

        const bool isSel = (ch.index == model.selectedChannelIndex);

        // Mute LED
        Rect ledRc = Rect(well.left + 8, y0 + 8, well.left + 18, y0 + 18);
        buf.fillRect(ledRc, ch.muted ? Y2kColor.LED_OFF : Y2kColor.LED_GREEN);

        // Mixer Target Badge
        Rect mixRc = Rect(well.left + 24, y0 + 3, well.left + 50, y1 - 2);
        drawBevelPanel(buf, mixRc, Y2kColor.BG_PANEL, true);
        if (!fnt.isNull) {
            fnt.drawText(buf, mixRc.left + 6, mixRc.top + 3,
                         toUTF32(ch.mixerInsert == 0 ? "M" : format("%d", ch.mixerInsert)),
                         Y2kColor.FG_LCD_CYAN);
        }

        // Channel Name Pill
        Rect pillRc = Rect(well.left + 56, y0 + 2, stepAreaLeft - 8, y1 - 1);
        drawBevelPanel(buf, pillRc, ch.color, isSel);
        if (isSel) {
            buf.fillRect(Rect(pillRc.left, pillRc.top, pillRc.left + 3, pillRc.bottom), Y2kColor.FG_LCD_AMBER);
        }
        if (!fnt.isNull) {
            fnt.drawText(buf, pillRc.left + 8, pillRc.top + 4,
                         toUTF32(truncateLabel(ch.name, 14)), Y2kColor.FG_TEXT);
        }

        // 16-Step Sequencer Buttons (4 groups of 4)
        foreach (s; 0 .. 16) {
            const int sx0 = stepAreaLeft + s * stepW + 1;
            const int sx1 = sx0 + stepW - 3;
            const bool oddGroup = ((s / 4) % 2) == 1;
            uint stepCol = ch.steps[s]
                ? Y2kColor.STEP_ACTIVE
                : (oddGroup ? Y2kColor.STEP_ODD : Y2kColor.STEP_EVEN);
            drawBevelPanel(buf, Rect(sx0, y0 + 3, sx1, y1 - 2), stepCol, ch.steps[s]);
            if (s == activeStep) {
                buf.fillRect(Rect(sx0 + 2, y0 + 4, sx1 - 2, y0 + 6), Y2kColor.LED_GREEN);
            }
        }
    }
}

/// Interactive FL Studio Channel Rack & 16-Step Sequencer Widget.
class ChannelRackWidget : Widget {
    private TransportModel* model_;

    void delegate(int channelIdx, int stepIdx, bool active) onStepToggle;
    void delegate(int channelIdx) onSelectChannel;
    void delegate(int channelIdx, int mixerInsert) onRouteMixer;
    void delegate(int channelIdx, bool muted) onMuteChannel;

    this(string id, TransportModel* model) {
        super(id);
        model_ = model;
        layoutWidth = FILL_PARENT;
        layoutHeight = FILL_PARENT;
        minWidth = 420;
        minHeight = 160;
    }

    override void onDraw(DrawBuf buf) {
        if (visibility != Visibility.Visible) return;
        drawChannelRackCanvas(buf, _pos, model_, font);
    }

    override bool onMouseEvent(MouseEvent event) {
        if (!model_ || event.action != MouseAction.ButtonDown) {
            return super.onMouseEvent(event);
        }
        Rect well = Rect(_pos.left + 4, _pos.top + 4, _pos.right - 4, _pos.bottom - 4);
        enum int HEADER_H = 22;
        enum int ROW_H = 30;
        enum int LEFT_CONTROLS_W = 190;

        const int row = (event.y - (well.top + HEADER_H + 4)) / ROW_H;
        if (row < 0 || row >= cast(int)model_.channels.length) return true;

        auto ch = &model_.channels[row];
        if (event.x >= well.left + 8 && event.x <= well.left + 20) {
            ch.muted = !ch.muted;
            model_.hintText = format("Channel '%s' %s", ch.name, ch.muted ? "muted" : "unmuted");
            if (onMuteChannel !is null) onMuteChannel(ch.index, ch.muted);
            invalidate();
            return true;
        }
        if (event.x >= well.left + 24 && event.x <= well.left + 52) {
            ch.mixerInsert = (ch.mixerInsert + 1) % 8;
            model_.hintText = format("Routed '%s' -> Mixer Insert %d", ch.name, ch.mixerInsert);
            if (onRouteMixer !is null) onRouteMixer(ch.index, ch.mixerInsert);
            invalidate();
            return true;
        }

        const int stepAreaLeft = well.left + LEFT_CONTROLS_W;
        if (event.x < stepAreaLeft) {
            model_.selectedChannelIndex = ch.index;
            model_.hintText = format("Selected Channel '%s' (%s)", ch.name, ch.uri);
            if (onSelectChannel !is null) onSelectChannel(ch.index);
            invalidate();
            return true;
        }

        const int stepAreaW = well.right - stepAreaLeft - 8;
        const int stepW = (stepAreaW > 160) ? (stepAreaW / 16) : 14;
        const int stepIdx = (event.x - stepAreaLeft) / stepW;
        if (stepIdx >= 0 && stepIdx < 16) {
            if (event.button == MouseButton.Right) {
                ch.steps[stepIdx] = false;
            } else {
                ch.steps[stepIdx] = !ch.steps[stepIdx];
            }
            model_.hintText = format("%s Step %d: %s", ch.name, stepIdx + 1,
                                     ch.steps[stepIdx] ? "ON" : "OFF");
            if (onStepToggle !is null) onStepToggle(ch.index, stepIdx, ch.steps[stepIdx]);
            invalidate();
        }
        return true;
    }
}

unittest {
    import dlangui.graphics.drawbuf : ColorDrawBuf;

    TransportModel model;
    model.initDefaultTemplate();
    auto buf = new ColorDrawBuf(620, 180);
    drawChannelRackCanvas(buf, Rect(0, 0, 620, 180), &model);

    // Step 0 of channel 0 (DR-8 Kick) is active -> should render STEP_ACTIVE
    // stepAreaLeft = 4 + 190 = 194, y0 = 4 + 22 + 4 = 30
    const uint c = buf.scanLine(36)[200];
    assert(c == Y2kColor.STEP_ACTIVE, "Expected active step sequencer button to use STEP_ACTIVE");
}
