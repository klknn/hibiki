module gui_d.channel_rack;

import dlangui;
import dlangui.graphics.drawbuf : DrawBuf;
import dlangui.graphics.fonts : FontWeight;
import dlangui.widgets.widget : Visibility, Widget;
import gui_d.theme : dp, dpf, drawCard, drawCenteredText, drawRotaryKnob, drawVCenteredText, FlPalette,
    getDawFont, getDawMonoFont, getFlPalette, safeWidgetFont, truncateLabel;
import gui_d.transport_model : ChannelFilterMode, ChannelState, GraphEditorMode, TransportModel;
import gui_d.workspace : activateParentWindow;
import std.format : format;
import std.utf : toUTF32;

/// DPI-scaled geometry shared identically between `drawChannelRackCanvas` and `ChannelRackWidget.onMouseEvent`.
struct ChannelRackMetrics {
    int toolbarH;
    int rowH;
    int leftControlsW;
    int graphH;
    int bodyTop;
    int graphAreaTop;
    int stepAreaLeft;
    int stepW;
    Rect filterRc;
    Rect graphBtnRc;
    Rect addBtnRc;
}

ChannelRackMetrics computeChannelRackMetrics(Rect rc, const(TransportModel)* model) nothrow @safe {
    ChannelRackMetrics m;
    m.toolbarH = dp(28);
    m.rowH = dp(34);
    m.leftControlsW = dp(248);
    m.graphH = dp(88);
    m.bodyTop = rc.top + m.toolbarH + dp(4);
    m.graphAreaTop = (model && model.graphEditorOpen) ? (rc.bottom - m.graphH) : rc.bottom;
    m.stepAreaLeft = rc.left + m.leftControlsW;
    const int stepAreaW = rc.right - m.stepAreaLeft - dp(10);
    m.stepW = (stepAreaW > dp(160)) ? (stepAreaW / 16) : dp(14);
    m.filterRc = Rect(rc.left + dp(8), rc.top + dp(4), rc.left + dp(68), rc.top + m.toolbarH - dp(4));
    m.graphBtnRc = Rect(rc.right - dp(134), rc.top + dp(4), rc.right - dp(68), rc.top + m.toolbarH - dp(4));
    m.addBtnRc = Rect(rc.right - dp(62), rc.top + dp(4), rc.right - dp(8), rc.top + m.toolbarH - dp(4));
    return m;
}

/// Renders the FL Studio 2026 Channel Rack, Pan/Vol knobs, 16-Step Sequencer, and Graph Editor.
void drawChannelRackCanvas(
    DrawBuf buf,
    Rect rc,
    const(TransportModel)* model,
    FontRef fnt = FontRef.init,
    int scrollRow = 0
) {
    const pal = model ? model.palette() : getFlPalette();
    buf.fillRect(rc, pal.bgWindow);
    if (!model || rc.width < dp(300) || rc.height < dp(64)) return;

    const m = computeChannelRackMetrics(rc, model);
    FontRef uiFnt = getDawFont(11, fnt, FontWeight.Normal);
    FontRef boldFnt = getDawFont(11, fnt, FontWeight.Bold);
    FontRef smallFnt = getDawFont(10, fnt, FontWeight.Normal);
    FontRef monoFnt = getDawMonoFont(10, fnt, FontWeight.Bold);

    // 1. Top Channel Rack Toolbar
    Rect tbRc = Rect(rc.left, rc.top, rc.right, rc.top + m.toolbarH);
    buf.fillRect(tbRc, pal.bgPanelAlt);
    buf.fillRect(Rect(tbRc.left, tbRc.bottom - dp(1), tbRc.right, tbRc.bottom), pal.borderStrong);

    string filterLbl = "All";
    if (model.channelFilter == ChannelFilterMode.DRUMS) filterLbl = "Drums";
    else if (model.channelFilter == ChannelFilterMode.SYNTHS) filterLbl = "Synths";
    else if (model.channelFilter == ChannelFilterMode.AUDIO) filterLbl = "Audio";

    drawCard(buf, m.filterRc, pal.bgControl, pal.borderSubtle, 3.0f);
    drawCenteredText(buf, boldFnt, m.filterRc, toUTF32(filterLbl), pal.fgText);

    // Swing rotary knob
    drawRotaryKnob(buf, tbRc.left + dp(92), tbRc.middley, dp(9), model.swingAmount,
                   pal.bgLcd, pal.accentOrange, pal.bgControl, pal.borderSubtle);
    drawVCenteredText(buf, smallFnt, tbRc.left + dp(106), tbRc,
                      toUTF32(format("SWING %d%%", cast(int)(model.swingAmount * 100.0f + 0.5f))),
                      pal.fgMuted);

    // Graph Editor toggle button
    drawCard(buf, m.graphBtnRc, model.graphEditorOpen ? pal.accentOrange : pal.bgControl,
             pal.borderSubtle, 3.0f);
    drawCenteredText(buf, boldFnt, m.graphBtnRc, "GRAPH"d,
                     model.graphEditorOpen ? pal.bgLcd : pal.fgText);

    // + Add Channel button
    drawCard(buf, m.addBtnRc, pal.bgControl, pal.borderSubtle, 3.0f);
    drawCenteredText(buf, boldFnt, m.addBtnRc, "+ ADD"d, pal.accentGreen);

    // Playhead step indicator strip
    const int activeStep = model.currentStepIndex();
    if (activeStep >= 0 && activeStep < 16) {
        const int sx0 = m.stepAreaLeft + activeStep * m.stepW + dp(2);
        buf.fillRect(Rect(sx0, tbRc.bottom + dp(1), sx0 + m.stepW - dp(4), tbRc.bottom + dp(4)), pal.accentGreen);
    }

    // 2. Channel Rows
    int skipped = 0;
    int visRow = 0;
    foreach (const ref ch; model.channels) {
        if (model.channelFilter == ChannelFilterMode.DRUMS && ch.category != "Drums") continue;
        if (model.channelFilter == ChannelFilterMode.SYNTHS && ch.category != "Synths") continue;
        if (model.channelFilter == ChannelFilterMode.AUDIO && ch.category != "Audio") continue;
        if (skipped < scrollRow) {
            skipped++;
            continue;
        }

        const int y0 = m.bodyTop + visRow * m.rowH;
        const int y1 = y0 + m.rowH - dp(4);
        if (y1 >= m.graphAreaTop - dp(4)) break;
        visRow++;

        const bool isSel = (ch.index == model.selectedChannelIndex);

        // Mute / Solo LED pill
        Rect ledRc = Rect(rc.left + dp(8), y0 + dp(9), rc.left + dp(20), y0 + dp(21));
        const uint ledCol = ch.soloed ? pal.accentAmber : (ch.muted ? pal.ledOff : pal.accentGreen);
        drawCard(buf, ledRc, ledCol, pal.borderStrong, 3.0f);

        // Pan Knob (bipolar)
        drawRotaryKnob(buf, rc.left + dp(36), y0 + dp(15), dp(10), (ch.pan + 1.0f) * 0.5f,
                       pal.bgLcd, pal.accentCyan, pal.bgControl, pal.borderSubtle, true);

        // Volume Knob (unipolar)
        drawRotaryKnob(buf, rc.left + dp(64), y0 + dp(15), dp(10), ch.volume,
                       pal.bgLcd, pal.accentGreen, pal.bgControl, pal.borderSubtle, false);

        // Mixer Target Insert LCD
        Rect mixRc = Rect(rc.left + dp(82), y0 + dp(4), rc.left + dp(112), y1 - dp(3));
        drawCard(buf, mixRc, pal.bgLcd, pal.borderSubtle, 3.0f);
        drawCenteredText(buf, monoFnt, mixRc,
                         toUTF32(ch.mixerInsert == 0 ? "M" : format("%2d", ch.mixerInsert)),
                         pal.accentCyan);

        // Channel Name Pill
        Rect pillRc = Rect(rc.left + dp(118), y0 + dp(2), m.stepAreaLeft - dp(8), y1 - dp(1));
        drawCard(buf, pillRc, ch.color, isSel ? pal.accentOrange : pal.borderStrong, 4.0f);
        if (isSel) {
            buf.fillRect(Rect(pillRc.left + dp(1), pillRc.top + dp(2), pillRc.left + dp(4), pillRc.bottom - dp(2)),
                         pal.accentOrange);
        }
        drawVCenteredText(buf, boldFnt, pillRc.left + dp(9), pillRc,
                          toUTF32(truncateLabel(ch.name, 14)), pal.fgText);

        // 16-Step Sequencer Pads (4 groups of 4: cool slate vs warm muted slate)
        foreach (s; 0 .. 16) {
            const int sx0 = m.stepAreaLeft + s * m.stepW + dp(1);
            const int sx1 = sx0 + m.stepW - dp(3);
            const bool oddGroup = ((s / 4) % 2) == 1;
            const bool active = ch.steps[s];
            const uint padCol = active
                ? pal.stepActive
                : (oddGroup ? pal.stepGroupB : pal.stepGroupA);
            const uint padBorder = active ? pal.stepActiveLed : pal.borderStrong;
            Rect padRc = Rect(sx0, y0 + dp(3), sx1, y1 - dp(2));
            drawCard(buf, padRc, padCol, padBorder, 3.0f);
            if (active) {
                buf.fillRect(Rect(padRc.left + dp(3), padRc.bottom - dp(5), padRc.right - dp(3), padRc.bottom - dp(2)),
                             pal.stepActiveLed);
            }
            if (s == activeStep) {
                buf.fillRect(Rect(padRc.left + dp(2), padRc.top + dp(2), padRc.right - dp(2), padRc.top + dp(5)),
                             pal.accentGreen);
            }
        }
    }

    // 3. Optional Bottom Graph Editor Drawer (Velocity / Pitch / Pan / Shift per step)
    if (model.graphEditorOpen && m.graphAreaTop < rc.bottom - dp(20)) {
        Rect gRc = Rect(rc.left, m.graphAreaTop, rc.right, rc.bottom);
        buf.fillRect(gRc, pal.bgPanelAlt);
        buf.fillRect(Rect(gRc.left, gRc.top, gRc.right, gRc.top + dp(1)), pal.borderSubtle);

        // Mode tabs on left side
        static immutable string[4] MODE_LABELS = ["VEL", "PITCH", "PAN", "SHIFT"];
        foreach (modeIdx; 0 .. 4) {
            Rect mRc = Rect(gRc.left + dp(8 + modeIdx * 56), gRc.top + dp(4),
                            gRc.left + dp(60 + modeIdx * 56), gRc.top + dp(22));
            const bool mAct = (cast(int)model.graphEditorMode == modeIdx);
            drawCard(buf, mRc, mAct ? pal.accentOrange : pal.bgControl, pal.borderSubtle, 3.0f);
            drawCenteredText(buf, smallFnt, mRc, toUTF32(MODE_LABELS[modeIdx]),
                             mAct ? pal.bgLcd : pal.fgText);
        }

        // Selected channel's 16 vertical parameter bars aligned with steps above
        const(ChannelState)* selCh = null;
        foreach (const ref c; model.channels) {
            if (c.index == model.selectedChannelIndex) {
                selCh = &c;
                break;
            }
        }
        if (selCh !is null) {
            const int barsTop = gRc.top + dp(26);
            const int barsBot = gRc.bottom - dp(6);
            const int barsH = barsBot - barsTop;
            if (barsH > dp(8)) {
                foreach (s; 0 .. 16) {
                    const int sx0 = m.stepAreaLeft + s * m.stepW + dp(2);
                    const int sx1 = sx0 + m.stepW - dp(5);
                    drawCard(buf, Rect(sx0, barsTop, sx1, barsBot), pal.bgLcd, pal.borderStrong, 2.0f);

                    float val = 0.5f;
                    uint barCol = pal.accentOrange;
                    final switch (model.graphEditorMode) {
                        case GraphEditorMode.VELOCITY:
                            val = selCh.stepVelocities[s];
                            barCol = pal.accentOrange;
                            break;
                        case GraphEditorMode.PITCH:
                            val = (selCh.stepPitches[s] + 1.0f) * 0.5f;
                            barCol = pal.accentCyan;
                            break;
                        case GraphEditorMode.PAN:
                            val = (selCh.stepPans[s] + 1.0f) * 0.5f;
                            barCol = pal.accentGreen;
                            break;
                        case GraphEditorMode.SHIFT:
                            val = selCh.stepShifts[s];
                            barCol = pal.accentPurple;
                            break;
                    }
                    if (val < 0.0f) val = 0.0f;
                    if (val > 1.0f) val = 1.0f;
                    const int bh = cast(int)(val * (barsH - dp(2)));
                    const int inset = dp(1);
                    if (bh > 0 && selCh.steps[s]) {
                        buf.fillRect(Rect(sx0 + inset, barsBot - inset - bh, sx1 - inset, barsBot - inset), barCol);
                    } else if (bh > 0) {
                        buf.fillRect(Rect(sx0 + inset, barsBot - inset - bh, sx1 - inset, barsBot - inset), pal.fgDim);
                    }
                }
            }
        }
    }
}

/// Interactive FL Studio 2026 Channel Rack & 16-Step Sequencer Widget.
class ChannelRackWidget : Widget {
    private TransportModel* model_;

    private enum DragTarget { NONE, SWING, CH_PAN, CH_VOL, STEP_PAINT, GRAPH_BAR }
    private DragTarget dragTarget_ = DragTarget.NONE;
    private int dragChannelArrIdx_ = -1;
    private int dragStartY_ = 0;
    private float dragStartVal_ = 0.0f;
    private bool dragStepPaintVal_ = true;
    int scrollRow = 0;

    void delegate(int channelIdx, int stepIdx, bool active) onStepToggle;
    void delegate(int channelIdx) onSelectChannel;
    void delegate(int channelIdx) onOpenPluginWrapper;
    void delegate(int channelIdx) onOpenPianoRoll;
    void delegate(int channelIdx, int mixerInsert) onRouteMixer;
    void delegate(int channelIdx, bool muted) onMuteChannel;
    void delegate() onAddChannelPrompt;

    this(string id, TransportModel* model) {
        super(id);
        model_ = model;
        layoutWidth = FILL_PARENT;
        layoutHeight = FILL_PARENT;
    }

    override void onDraw(DrawBuf buf) {
        if (visibility != Visibility.Visible) return;
        drawChannelRackCanvas(buf, _pos, model_, safeWidgetFont(this), scrollRow);
    }

    private int[] visibleChannelIndices() const @safe {
        int[] res;
        if (!model_) return res;
        foreach (i, const ref ch; model_.channels) {
            if (model_.channelFilter == ChannelFilterMode.DRUMS && ch.category != "Drums") continue;
            if (model_.channelFilter == ChannelFilterMode.SYNTHS && ch.category != "Synths") continue;
            if (model_.channelFilter == ChannelFilterMode.AUDIO && ch.category != "Audio") continue;
            res ~= cast(int)i;
        }
        return res;
    }

    override bool onMouseEvent(MouseEvent event) {
        if (event.action == MouseAction.FocusOut) {
            return dragTarget_ != DragTarget.NONE;
        }
        if (!model_) return super.onMouseEvent(event);

        const m = computeChannelRackMetrics(_pos, model_);

        if (event.action == MouseAction.Wheel) {
            const auto visIdx = visibleChannelIndices();
            if (visIdx.length > 0) {
                scrollRow -= (event.wheelDelta > 0) ? 1 : -1;
                if (scrollRow < 0) scrollRow = 0;
                if (scrollRow >= cast(int)visIdx.length) scrollRow = cast(int)visIdx.length - 1;
                invalidate();
            }
            return true;
        }

        if (event.action == MouseAction.ButtonDown) {
            activateParentWindow(this);

            // 1. Toolbar hit testing
            if (event.y >= _pos.top && event.y < _pos.top + m.toolbarH) {
                if (m.filterRc.isPointInside(event.x, event.y)) {
                    model_.channelFilter = cast(ChannelFilterMode)((cast(int)model_.channelFilter + 1) % 4);
                    scrollRow = 0;
                    static immutable string[4] F_NAMES = ["All", "Drums", "Synths", "Audio"];
                    model_.hintText = "Channel Filter: " ~ F_NAMES[cast(int)model_.channelFilter];
                    invalidate();
                    return true;
                }
                if (event.x >= _pos.left + dp(76) && event.x <= _pos.left + dp(170)) {
                    if (event.button == MouseButton.Right) {
                        model_.swingAmount = 0.0f;
                    } else {
                        dragTarget_ = DragTarget.SWING;
                        dragStartY_ = event.y;
                        dragStartVal_ = model_.swingAmount;
                    }
                    invalidate();
                    return true;
                }
                if (m.graphBtnRc.isPointInside(event.x, event.y)) {
                    model_.graphEditorOpen = !model_.graphEditorOpen;
                    model_.hintText = model_.graphEditorOpen
                        ? "Graph Editor opened — drag bars to edit per-step Velocity / Pitch / Pan / Shift"
                        : "Graph Editor closed";
                    invalidate();
                    return true;
                }
                if (m.addBtnRc.isPointInside(event.x, event.y)) {
                    if (onAddChannelPrompt !is null) onAddChannelPrompt();
                    return true;
                }
                return true;
            }

            // 2. Graph Editor hit testing
            if (model_.graphEditorOpen && event.y >= m.graphAreaTop) {
                if (event.y < m.graphAreaTop + dp(24)) {
                    foreach (modeIdx; 0 .. 4) {
                        Rect mRc = Rect(_pos.left + dp(8 + modeIdx * 56), m.graphAreaTop + dp(4),
                                        _pos.left + dp(60 + modeIdx * 56), m.graphAreaTop + dp(22));
                        if (mRc.isPointInside(event.x, event.y)) {
                            model_.graphEditorMode = cast(GraphEditorMode)modeIdx;
                            invalidate();
                            return true;
                        }
                    }
                    return true;
                }
                dragTarget_ = DragTarget.GRAPH_BAR;
                updateGraphBarAt(event.x, event.y, m.stepAreaLeft, m.stepW, m.graphAreaTop);
                return true;
            }

            // 3. Channel Row hit testing
            if (event.y < m.bodyTop) return true;
            const int visRow = scrollRow + (event.y - m.bodyTop) / m.rowH;
            const auto visIdx = visibleChannelIndices();
            if (visRow < 0 || visRow >= cast(int)visIdx.length) return true;
            auto ch = &model_.channels[visIdx[visRow]];

            // Mute/Solo LED
            if (event.x >= _pos.left + dp(8) && event.x <= _pos.left + dp(22)) {
                if (event.button == MouseButton.Right) {
                    ch.soloed = !ch.soloed;
                } else {
                    ch.muted = !ch.muted;
                    if (onMuteChannel !is null) onMuteChannel(ch.index, ch.muted);
                }
                invalidate();
                return true;
            }

            // Pan knob
            if (event.x >= _pos.left + dp(24) && event.x <= _pos.left + dp(49)) {
                if (event.button == MouseButton.Right) {
                    ch.pan = 0.0f;
                    model_.hintText = format("%s Pan: Center", ch.name);
                } else {
                    dragTarget_ = DragTarget.CH_PAN;
                    dragChannelArrIdx_ = visIdx[visRow];
                    dragStartY_ = event.y;
                    dragStartVal_ = ch.pan;
                }
                invalidate();
                return true;
            }

            // Volume knob
            if (event.x >= _pos.left + dp(50) && event.x <= _pos.left + dp(78)) {
                if (event.button == MouseButton.Right) {
                    ch.volume = 0.78f;
                    model_.hintText = format("%s Volume: 78%%", ch.name);
                } else {
                    dragTarget_ = DragTarget.CH_VOL;
                    dragChannelArrIdx_ = visIdx[visRow];
                    dragStartY_ = event.y;
                    dragStartVal_ = ch.volume;
                }
                invalidate();
                return true;
            }

            // Mixer Target Insert
            if (event.x >= _pos.left + dp(80) && event.x <= _pos.left + dp(114)) {
                if (event.button == MouseButton.Right) {
                    ch.mixerInsert = (ch.mixerInsert > 0) ? ch.mixerInsert - 1 : 12;
                } else {
                    ch.mixerInsert = (ch.mixerInsert + 1) % 13;
                }
                model_.hintText = format("Routed '%s' -> Mixer Insert %d", ch.name, ch.mixerInsert);
                if (onRouteMixer !is null) onRouteMixer(ch.index, ch.mixerInsert);
                invalidate();
                return true;
            }

            // Channel Name Pill: single-click selects, double-click opens Plugin Wrapper, right-click opens Piano Roll
            if (event.x < m.stepAreaLeft) {
                model_.selectPianoRollChannel(visIdx[visRow]);
                if (event.button == MouseButton.Right) {
                    if (onSelectChannel !is null) onSelectChannel(ch.index);
                    if (onOpenPianoRoll !is null) onOpenPianoRoll(ch.index);
                } else if (event.doubleClick) {
                    if (onSelectChannel !is null) onSelectChannel(ch.index);
                    if (onOpenPluginWrapper !is null) onOpenPluginWrapper(ch.index);
                } else {
                    model_.hintText = format("Selected Channel: %s (double-click for Wrapper, right-click for Piano Roll)", ch.name);
                    if (onSelectChannel !is null) onSelectChannel(ch.index);
                }
                invalidate();
                return true;
            }

            // 16-Step Sequencer Pad
            const int stepIdx = (event.x - m.stepAreaLeft) / m.stepW;
            if (stepIdx >= 0 && stepIdx < 16) {
                dragTarget_ = DragTarget.STEP_PAINT;
                dragChannelArrIdx_ = visIdx[visRow];
                if (event.button == MouseButton.Right) {
                    dragStepPaintVal_ = false;
                } else {
                    dragStepPaintVal_ = !ch.steps[stepIdx];
                }
                ch.steps[stepIdx] = dragStepPaintVal_;
                model_.hintText = format("%s Step %d: %s (drag to paint steps)",
                                         ch.name, stepIdx + 1, dragStepPaintVal_ ? "ON" : "OFF");
                if (onStepToggle !is null) onStepToggle(ch.index, stepIdx, dragStepPaintVal_);
                invalidate();
                return true;
            }
            return true;
        }

        if (event.action == MouseAction.Move && dragTarget_ != DragTarget.NONE) {
            const float delta = cast(float)(dragStartY_ - event.y) / dpf(100.0f);
            final switch (dragTarget_) {
                case DragTarget.NONE:
                    break;
                case DragTarget.SWING:
                    float v = dragStartVal_ + delta;
                    if (v < 0.0f) v = 0.0f;
                    if (v > 1.0f) v = 1.0f;
                    model_.swingAmount = v;
                    model_.hintText = format("Global Swing: %d%%", cast(int)(v * 100.0f + 0.5f));
                    invalidate();
                    return true;
                case DragTarget.CH_PAN:
                    if (dragChannelArrIdx_ >= 0 && dragChannelArrIdx_ < cast(int)model_.channels.length) {
                        float p = dragStartVal_ + delta * 2.0f;
                        if (p < -1.0f) p = -1.0f;
                        if (p > 1.0f) p = 1.0f;
                        model_.channels[dragChannelArrIdx_].pan = p;
                        model_.hintText = format("%s Pan: %+d%%",
                                                 model_.channels[dragChannelArrIdx_].name,
                                                 cast(int)(p * 100.0f));
                        invalidate();
                    }
                    return true;
                case DragTarget.CH_VOL:
                    if (dragChannelArrIdx_ >= 0 && dragChannelArrIdx_ < cast(int)model_.channels.length) {
                        float v = dragStartVal_ + delta;
                        if (v < 0.0f) v = 0.0f;
                        if (v > 1.0f) v = 1.0f;
                        model_.channels[dragChannelArrIdx_].volume = v;
                        model_.hintText = format("%s Volume: %d%%",
                                                 model_.channels[dragChannelArrIdx_].name,
                                                 cast(int)(v * 100.0f + 0.5f));
                        invalidate();
                    }
                    return true;
                case DragTarget.STEP_PAINT:
                    if (dragChannelArrIdx_ >= 0 && dragChannelArrIdx_ < cast(int)model_.channels.length) {
                        const int stepIdx = (event.x - m.stepAreaLeft) / m.stepW;
                        if (stepIdx >= 0 && stepIdx < 16 &&
                            model_.channels[dragChannelArrIdx_].steps[stepIdx] != dragStepPaintVal_) {
                            model_.channels[dragChannelArrIdx_].steps[stepIdx] = dragStepPaintVal_;
                            if (onStepToggle !is null) {
                                onStepToggle(model_.channels[dragChannelArrIdx_].index,
                                             stepIdx, dragStepPaintVal_);
                            }
                            invalidate();
                        }
                    }
                    return true;
                case DragTarget.GRAPH_BAR:
                    updateGraphBarAt(event.x, event.y, m.stepAreaLeft, m.stepW, m.graphAreaTop);
                    return true;
            }
        }

        if ((event.action == MouseAction.ButtonUp || event.action == MouseAction.Cancel) &&
            dragTarget_ != DragTarget.NONE) {
            dragTarget_ = DragTarget.NONE;
            dragChannelArrIdx_ = -1;
            return true;
        }

        return super.onMouseEvent(event);
    }

    private void updateGraphBarAt(int mx, int my, int stepAreaLeft, int stepW, int graphAreaTop) {
        if (!model_) return;
        const int stepIdx = (mx - stepAreaLeft) / stepW;
        if (stepIdx < 0 || stepIdx >= 16) return;
        const int barsTop = graphAreaTop + dp(26);
        const int barsBot = _pos.bottom - dp(6);
        if (barsBot <= barsTop) return;
        float norm = cast(float)(barsBot - my) / cast(float)(barsBot - barsTop);
        if (norm < 0.0f) norm = 0.0f;
        if (norm > 1.0f) norm = 1.0f;

        foreach (ref ch; model_.channels) {
            if (ch.index == model_.selectedChannelIndex) {
                final switch (model_.graphEditorMode) {
                    case GraphEditorMode.VELOCITY:
                        ch.stepVelocities[stepIdx] = norm;
                        model_.hintText = format("%s Step %d Velocity: %d%%",
                                                 ch.name, stepIdx + 1, cast(int)(norm * 100.0f + 0.5f));
                        break;
                    case GraphEditorMode.PITCH:
                        ch.stepPitches[stepIdx] = norm * 2.0f - 1.0f;
                        model_.hintText = format("%s Step %d Pitch: %+d%%",
                                                 ch.name, stepIdx + 1, cast(int)(ch.stepPitches[stepIdx] * 100.0f));
                        break;
                    case GraphEditorMode.PAN:
                        ch.stepPans[stepIdx] = norm * 2.0f - 1.0f;
                        model_.hintText = format("%s Step %d Pan: %+d%%",
                                                 ch.name, stepIdx + 1, cast(int)(ch.stepPans[stepIdx] * 100.0f));
                        break;
                    case GraphEditorMode.SHIFT:
                        ch.stepShifts[stepIdx] = norm;
                        model_.hintText = format("%s Step %d Shift: %d%%",
                                                 ch.name, stepIdx + 1, cast(int)(norm * 100.0f + 0.5f));
                        break;
                }
                invalidate();
                break;
            }
        }
    }
}

unittest {
    import dlangui.graphics.drawbuf : ColorDrawBuf;

    TransportModel model;
    model.initDefaultTemplate();
    model.graphEditorOpen = true;
    auto buf = new ColorDrawBuf(720, 320);
    drawChannelRackCanvas(buf, Rect(0, 0, 720, 320), &model);

    const pal = model.palette();
    // Step 0 of channel 0 (DR-8 Kick) is active -> should render stepActive
    // stepAreaLeft = 248, y0 = 28 + 4 = 32, padRc top = 35
    const uint c = buf.scanLine(42)[255];
    assert(c == pal.stepActive, "Expected active FL Studio step pad to render stepActive");

    // Regression 1: Cycling filter visits all 4 modes including AUDIO and filters Audio channels
    import dlangui.widgets.styles : currentTheme, createDefaultTheme;
    import gui_d.theme : destroyWidgetTree;
    if (currentTheme is null) currentTheme = createDefaultTheme();
    auto cr = new ChannelRackWidget("cr", &model);
    scope (exit) destroyWidgetTree(cr);
    cr.measure(720, 320);
    cr.layout(Rect(0, 0, 720, 320));
    foreach (_; 0 .. 3) {
        cr.onMouseEvent(new MouseEvent(MouseAction.ButtonDown, MouseButton.Left, MouseFlag.LButton, 20, 14));
    }
    assert(model.channelFilter == ChannelFilterMode.AUDIO,
           "Expected 3 clicks on filter button from ALL to reach ChannelFilterMode.AUDIO");
    assert(cr.visibleChannelIndices().length == 0,
           "Expected AUDIO filter to show 0 channels on default Drums/Synths template");
    ChannelState audioCh;
    audioCh.initDefaults();
    audioCh.index = 5;
    audioCh.name = "Vocal Loop";
    audioCh.category = "Audio";
    model.channels ~= audioCh;
    assert(cr.visibleChannelIndices().length == 1,
           "Expected AUDIO filter to show only the Audio channel");

    // Regression 2: Single-clicking channel pill selects without popping Plugin Wrapper; double-click opens it
    bool wrapperOpened = false;
    int selectedCh = -1;
    cr.onSelectChannel = (int chIdx) { selectedCh = chIdx; };
    cr.onOpenPluginWrapper = (int chIdx) { wrapperOpened = true; };
    model.channelFilter = ChannelFilterMode.ALL;
    cr.onMouseEvent(new MouseEvent(MouseAction.ButtonDown, MouseButton.Left, MouseFlag.LButton, 150, 44));
    assert(selectedCh == 0 && !wrapperOpened,
           "Expected single-click on channel pill to select channel without opening Plugin Wrapper");
    auto dblEvt = new MouseEvent(MouseAction.ButtonDown, MouseButton.Left, MouseFlag.LButton, 150, 44);
    dblEvt.lbutton.down(150, 44, MouseFlag.LButton);
    dblEvt.lbutton.up(150, 44, 0);
    dblEvt.lbutton.down(150, 44, MouseFlag.LButton);
    cr.onMouseEvent(dblEvt);
    assert(wrapperOpened, "Expected double-click on channel pill to open Plugin Wrapper");

    // Regression 3: MouseWheel scrolls channels and Cancel stops swing drag
    cr.onMouseEvent(new MouseEvent(MouseAction.Wheel, MouseButton.None, 0, 150, 100, -1));
    assert(cr.scrollRow == 1, "Expected wheel down in Channel Rack to scroll row offset");
    cr.onMouseEvent(new MouseEvent(MouseAction.ButtonDown, MouseButton.Left, MouseFlag.LButton, 92, 14));
    cr.onMouseEvent(new MouseEvent(MouseAction.Cancel, MouseButton.None, 0, 92, 14));
    const float swingBefore = model.swingAmount;
    cr.onMouseEvent(new MouseEvent(MouseAction.Move, MouseButton.None, 0, 92, 0));
    assert(model.swingAmount == swingBefore, "Expected Cancel to clear swing drag");
}
