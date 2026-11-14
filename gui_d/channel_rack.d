module gui_d.channel_rack;

import dlangui;
import dlangui.graphics.drawbuf : DrawBuf;
import dlangui.widgets.widget : Visibility, Widget;
import gui_d.theme : drawCard, drawRotaryKnob, FlPalette, getFlPalette, truncateLabel;
import gui_d.transport_model : ChannelFilterMode, ChannelState, GraphEditorMode, TransportModel;
import gui_d.workspace : activateParentWindow;
import std.format : format;
import std.utf : toUTF32;

/// Renders the FL Studio 2026 Channel Rack, Pan/Vol knobs, 16-Step Sequencer, and Graph Editor.
void drawChannelRackCanvas(
    DrawBuf buf,
    Rect rc,
    const(TransportModel)* model,
    FontRef fnt = FontRef.init
) {
    const pal = model ? model.palette() : getFlPalette();
    buf.fillRect(rc, pal.bgWindow);
    if (!model || rc.width < 300 || rc.height < 64) return;

    enum int TOOLBAR_H = 28;
    enum int ROW_H = 34;
    enum int LEFT_CONTROLS_W = 248;
    enum int GRAPH_H = 88;

    // 1. Top Channel Rack Toolbar
    Rect tbRc = Rect(rc.left, rc.top, rc.right, rc.top + TOOLBAR_H);
    buf.fillRect(tbRc, pal.bgPanelAlt);
    buf.fillRect(Rect(tbRc.left, tbRc.bottom - 1, tbRc.right, tbRc.bottom), pal.borderStrong);

    string filterLbl = "All";
    if (model.channelFilter == ChannelFilterMode.DRUMS) filterLbl = "Drums";
    else if (model.channelFilter == ChannelFilterMode.SYNTHS) filterLbl = "Synths";
    else if (model.channelFilter == ChannelFilterMode.AUDIO) filterLbl = "Audio";

    Rect filterRc = Rect(tbRc.left + 8, tbRc.top + 4, tbRc.left + 68, tbRc.bottom - 4);
    drawCard(buf, filterRc, pal.bgControl, pal.borderSubtle, 3.0f);
    if (!fnt.isNull) {
        fnt.drawText(buf, filterRc.left + 8, filterRc.top + 2, toUTF32(filterLbl), pal.fgText);
    }

    // Swing rotary knob
    drawRotaryKnob(buf, tbRc.left + 92, tbRc.top + 14, 9, model.swingAmount,
                   pal.bgLcd, pal.accentOrange, pal.bgControl, pal.borderSubtle);
    if (!fnt.isNull) {
        fnt.drawText(buf, tbRc.left + 106, tbRc.top + 6,
                     toUTF32(format("SWING %d%%", cast(int)(model.swingAmount * 100.0f + 0.5f))),
                     pal.fgMuted);
    }

    // Graph Editor toggle button
    Rect graphBtnRc = Rect(tbRc.right - 134, tbRc.top + 4, tbRc.right - 68, tbRc.bottom - 4);
    drawCard(buf, graphBtnRc, model.graphEditorOpen ? pal.accentOrange : pal.bgControl,
             pal.borderSubtle, 3.0f);
    if (!fnt.isNull) {
        fnt.drawText(buf, graphBtnRc.left + 10, graphBtnRc.top + 2, "GRAPH"d,
                     model.graphEditorOpen ? pal.bgLcd : pal.fgText);
    }

    // + Add Channel button
    Rect addBtnRc = Rect(tbRc.right - 62, tbRc.top + 4, tbRc.right - 8, tbRc.bottom - 4);
    drawCard(buf, addBtnRc, pal.bgControl, pal.borderSubtle, 3.0f);
    if (!fnt.isNull) {
        fnt.drawText(buf, addBtnRc.left + 10, addBtnRc.top + 2, "+ ADD"d, pal.accentGreen);
    }

    // Compute visible area for channel rows vs optional Graph Editor drawer
    const int bodyTop = tbRc.bottom + 4;
    const int graphAreaTop = model.graphEditorOpen ? (rc.bottom - GRAPH_H) : rc.bottom;
    const int stepAreaLeft = rc.left + LEFT_CONTROLS_W;
    const int stepAreaW = rc.right - stepAreaLeft - 10;
    const int stepW = (stepAreaW > 160) ? (stepAreaW / 16) : 14;
    const int activeStep = model.currentStepIndex();

    // Playhead step indicator strip
    if (activeStep >= 0 && activeStep < 16) {
        const int sx0 = stepAreaLeft + activeStep * stepW + 2;
        buf.fillRect(Rect(sx0, tbRc.bottom + 1, sx0 + stepW - 4, tbRc.bottom + 4), pal.accentGreen);
    }

    // 2. Channel Rows
    int visRow = 0;
    foreach (const ref ch; model.channels) {
        if (model.channelFilter == ChannelFilterMode.DRUMS && ch.category != "Drums") continue;
        if (model.channelFilter == ChannelFilterMode.SYNTHS && ch.category != "Synths") continue;

        const int y0 = bodyTop + visRow * ROW_H;
        const int y1 = y0 + ROW_H - 4;
        if (y1 >= graphAreaTop - 4) break;
        visRow++;

        const bool isSel = (ch.index == model.selectedChannelIndex);

        // Mute / Solo LED pill
        Rect ledRc = Rect(rc.left + 8, y0 + 9, rc.left + 20, y0 + 21);
        const uint ledCol = ch.soloed ? pal.accentAmber : (ch.muted ? pal.ledOff : pal.accentGreen);
        drawCard(buf, ledRc, ledCol, pal.borderStrong, 3.0f);

        // Pan Knob (bipolar)
        drawRotaryKnob(buf, rc.left + 36, y0 + 15, 10, (ch.pan + 1.0f) * 0.5f,
                       pal.bgLcd, pal.accentCyan, pal.bgControl, pal.borderSubtle, true);

        // Volume Knob (unipolar)
        drawRotaryKnob(buf, rc.left + 64, y0 + 15, 10, ch.volume,
                       pal.bgLcd, pal.accentGreen, pal.bgControl, pal.borderSubtle, false);

        // Mixer Target Insert LCD
        Rect mixRc = Rect(rc.left + 82, y0 + 4, rc.left + 112, y1 - 3);
        drawCard(buf, mixRc, pal.bgLcd, pal.borderSubtle, 3.0f);
        if (!fnt.isNull) {
            fnt.drawText(buf, mixRc.left + 6, mixRc.top + 3,
                         toUTF32(ch.mixerInsert == 0 ? "M" : format("%2d", ch.mixerInsert)),
                         pal.accentCyan);
        }

        // Channel Name Pill
        Rect pillRc = Rect(rc.left + 118, y0 + 2, stepAreaLeft - 8, y1 - 1);
        drawCard(buf, pillRc, ch.color, isSel ? pal.accentOrange : pal.borderStrong, 4.0f);
        if (isSel) {
            buf.fillRect(Rect(pillRc.left + 1, pillRc.top + 2, pillRc.left + 4, pillRc.bottom - 2),
                         pal.accentOrange);
        }
        if (!fnt.isNull) {
            fnt.drawText(buf, pillRc.left + 9, pillRc.top + 5,
                         toUTF32(truncateLabel(ch.name, 14)), pal.fgText);
        }

        // 16-Step Sequencer Pads (4 groups of 4: cool slate vs warm muted slate)
        foreach (s; 0 .. 16) {
            const int sx0 = stepAreaLeft + s * stepW + 1;
            const int sx1 = sx0 + stepW - 3;
            const bool oddGroup = ((s / 4) % 2) == 1;
            const bool active = ch.steps[s];
            const uint padCol = active
                ? pal.stepActive
                : (oddGroup ? pal.stepGroupB : pal.stepGroupA);
            const uint padBorder = active ? pal.stepActiveLed : pal.borderStrong;
            Rect padRc = Rect(sx0, y0 + 3, sx1, y1 - 2);
            drawCard(buf, padRc, padCol, padBorder, 3.0f);
            if (active) {
                buf.fillRect(Rect(padRc.left + 3, padRc.bottom - 5, padRc.right - 3, padRc.bottom - 2),
                             pal.stepActiveLed);
            }
            if (s == activeStep) {
                buf.fillRect(Rect(padRc.left + 2, padRc.top + 2, padRc.right - 2, padRc.top + 5),
                             pal.accentGreen);
            }
        }
    }

    // 3. Optional Bottom Graph Editor Drawer (Velocity / Pitch / Pan / Shift per step)
    if (model.graphEditorOpen && graphAreaTop < rc.bottom - 20) {
        Rect gRc = Rect(rc.left, graphAreaTop, rc.right, rc.bottom);
        buf.fillRect(gRc, pal.bgPanelAlt);
        buf.fillRect(Rect(gRc.left, gRc.top, gRc.right, gRc.top + 1), pal.borderSubtle);

        // Mode tabs on left side
        static immutable string[4] MODE_LABELS = ["VEL", "PITCH", "PAN", "SHIFT"];
        foreach (m; 0 .. 4) {
            Rect mRc = Rect(gRc.left + 8 + m * 56, gRc.top + 4, gRc.left + 60 + m * 56, gRc.top + 22);
            const bool mAct = (cast(int)model.graphEditorMode == m);
            drawCard(buf, mRc, mAct ? pal.accentOrange : pal.bgControl, pal.borderSubtle, 3.0f);
            if (!fnt.isNull) {
                fnt.drawText(buf, mRc.left + 8, mRc.top + 2, toUTF32(MODE_LABELS[m]),
                             mAct ? pal.bgLcd : pal.fgText);
            }
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
            const int barsTop = gRc.top + 26;
            const int barsBot = gRc.bottom - 6;
            const int barsH = barsBot - barsTop;
            if (barsH > 8) {
                foreach (s; 0 .. 16) {
                    const int sx0 = stepAreaLeft + s * stepW + 2;
                    const int sx1 = sx0 + stepW - 5;
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
                    const int h = cast(int)(val * (barsH - 2));
                    if (h > 0 && selCh.steps[s]) {
                        buf.fillRect(Rect(sx0 + 1, barsBot - 1 - h, sx1 - 1, barsBot - 1), barCol);
                    } else if (h > 0) {
                        buf.fillRect(Rect(sx0 + 1, barsBot - 1 - h, sx1 - 1, barsBot - 1), pal.fgDim);
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
        drawChannelRackCanvas(buf, _pos, model_, font);
    }

    private int[] visibleChannelIndices() const @safe {
        int[] res;
        if (!model_) return res;
        foreach (i, const ref ch; model_.channels) {
            if (model_.channelFilter == ChannelFilterMode.DRUMS && ch.category != "Drums") continue;
            if (model_.channelFilter == ChannelFilterMode.SYNTHS && ch.category != "Synths") continue;
            res ~= cast(int)i;
        }
        return res;
    }

    override bool onMouseEvent(MouseEvent event) {
        if (event.action == MouseAction.FocusOut) {
            return dragTarget_ != DragTarget.NONE;
        }
        if (!model_) return super.onMouseEvent(event);

        enum int TOOLBAR_H = 28;
        enum int ROW_H = 34;
        enum int LEFT_CONTROLS_W = 248;
        enum int GRAPH_H = 88;

        const int stepAreaLeft = _pos.left + LEFT_CONTROLS_W;
        const int stepAreaW = _pos.right - stepAreaLeft - 10;
        const int stepW = (stepAreaW > 160) ? (stepAreaW / 16) : 14;
        const int graphAreaTop = model_.graphEditorOpen ? (_pos.bottom - GRAPH_H) : _pos.bottom;

        if (event.action == MouseAction.ButtonDown) {
            activateParentWindow(this);

            // 1. Toolbar hit testing
            if (event.y >= _pos.top && event.y < _pos.top + TOOLBAR_H) {
                Rect filterRc = Rect(_pos.left + 8, _pos.top + 4, _pos.left + 68, _pos.top + TOOLBAR_H - 4);
                if (filterRc.isPointInside(event.x, event.y)) {
                    model_.channelFilter = cast(ChannelFilterMode)((cast(int)model_.channelFilter + 1) % 3);
                    model_.hintText = "Channel Filter changed";
                    invalidate();
                    return true;
                }
                if (event.x >= _pos.left + 76 && event.x <= _pos.left + 165) {
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
                Rect graphBtnRc = Rect(_pos.right - 134, _pos.top + 4, _pos.right - 68, _pos.top + TOOLBAR_H - 4);
                if (graphBtnRc.isPointInside(event.x, event.y)) {
                    model_.graphEditorOpen = !model_.graphEditorOpen;
                    model_.hintText = model_.graphEditorOpen
                        ? "Graph Editor opened — drag bars to edit per-step Velocity / Pitch / Pan / Shift"
                        : "Graph Editor closed";
                    invalidate();
                    return true;
                }
                Rect addBtnRc = Rect(_pos.right - 62, _pos.top + 4, _pos.right - 8, _pos.top + TOOLBAR_H - 4);
                if (addBtnRc.isPointInside(event.x, event.y)) {
                    if (onAddChannelPrompt !is null) onAddChannelPrompt();
                    return true;
                }
                return true;
            }

            // 2. Graph Editor hit testing
            if (model_.graphEditorOpen && event.y >= graphAreaTop) {
                if (event.y < graphAreaTop + 24) {
                    foreach (m; 0 .. 4) {
                        Rect mRc = Rect(_pos.left + 8 + m * 56, graphAreaTop + 4,
                                        _pos.left + 60 + m * 56, graphAreaTop + 22);
                        if (mRc.isPointInside(event.x, event.y)) {
                            model_.graphEditorMode = cast(GraphEditorMode)m;
                            invalidate();
                            return true;
                        }
                    }
                    return true;
                }
                dragTarget_ = DragTarget.GRAPH_BAR;
                updateGraphBarAt(event.x, event.y, stepAreaLeft, stepW, graphAreaTop);
                return true;
            }

            // 3. Channel Row hit testing
            const int bodyTop = _pos.top + TOOLBAR_H + 4;
            const int visRow = (event.y - bodyTop) / ROW_H;
            const auto visIdx = visibleChannelIndices();
            if (visRow < 0 || visRow >= cast(int)visIdx.length) return true;
            auto ch = &model_.channels[visIdx[visRow]];

            // Mute/Solo LED
            if (event.x >= _pos.left + 8 && event.x <= _pos.left + 22) {
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
            if (event.x >= _pos.left + 24 && event.x <= _pos.left + 49) {
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
            if (event.x >= _pos.left + 50 && event.x <= _pos.left + 78) {
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
            if (event.x >= _pos.left + 80 && event.x <= _pos.left + 114) {
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

            // Channel Name Pill
            if (event.x < stepAreaLeft) {
                model_.selectedChannelIndex = ch.index;
                model_.activeMidi.channelIndex = ch.index;
                if (event.button == MouseButton.Right) {
                    if (onOpenPianoRoll !is null) onOpenPianoRoll(ch.index);
                } else if (event.doubleClick) {
                    if (onOpenPluginWrapper !is null) onOpenPluginWrapper(ch.index);
                } else {
                    if (onSelectChannel !is null) onSelectChannel(ch.index);
                    if (onOpenPluginWrapper !is null) onOpenPluginWrapper(ch.index);
                }
                invalidate();
                return true;
            }

            // 16-Step Sequencer Pad
            const int stepIdx = (event.x - stepAreaLeft) / stepW;
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
            const float delta = cast(float)(dragStartY_ - event.y) / 100.0f;
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
                        const int stepIdx = (event.x - stepAreaLeft) / stepW;
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
                    updateGraphBarAt(event.x, event.y, stepAreaLeft, stepW, graphAreaTop);
                    return true;
            }
        }

        if (event.action == MouseAction.ButtonUp && dragTarget_ != DragTarget.NONE) {
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
        const int barsTop = graphAreaTop + 26;
        const int barsBot = _pos.bottom - 6;
        if (barsBot <= barsTop) return;
        float norm = cast(float)(barsBot - my) / cast(float)(barsBot - barsTop);
        if (norm < 0.0f) norm = 0.0f;
        if (norm > 1.0f) norm = 1.0f;

        foreach (ref ch; model_.channels) {
            if (ch.index == model_.selectedChannelIndex) {
                final switch (model_.graphEditorMode) {
                    case GraphEditorMode.VELOCITY:
                        ch.stepVelocities[stepIdx] = norm;
                        break;
                    case GraphEditorMode.PITCH:
                        ch.stepPitches[stepIdx] = norm * 2.0f - 1.0f;
                        break;
                    case GraphEditorMode.PAN:
                        ch.stepPans[stepIdx] = norm * 2.0f - 1.0f;
                        break;
                    case GraphEditorMode.SHIFT:
                        ch.stepShifts[stepIdx] = norm;
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
}
