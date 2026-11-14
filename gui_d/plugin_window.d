module gui_d.plugin_window;

import core.stdc.math : cosf, expf;
import dlangui;
import dlangui.graphics.drawbuf : DrawBuf;
import dlangui.widgets.widget : Visibility, Widget;
import gui_d.theme : drawCard, drawRotaryKnob, drawThickLine, FlPalette, getFlPalette, truncateLabel;
import gui_d.transport_model : ParamItemState, PluginSlotState, TransportModel;
import gui_d.workspace : activateParentWindow;
import std.format : format;
import std.utf : toUTF32;

/// Target source shown inside the Fruity Wrapper window (Channel Generator vs Mixer FX Slot).
enum WrapperTargetKind {
    CHANNEL_GENERATOR,
    MIXER_FX,
}

private enum int WRAP_HDR_H = 32;
private enum int SCOPE_H = 74;

/// Resolves the active `PluginSlotState*` being edited by the Plugin Wrapper.
PluginSlotState* resolveActiveWrapperPlugin(
    TransportModel* model,
    WrapperTargetKind kind,
    int trackOrChannelIdx,
    int fxSlotIdx
) @trusted {
    if (!model) return null;
    if (kind == WrapperTargetKind.CHANNEL_GENERATOR) {
        foreach (ref ch; model.channels) {
            if (ch.index == trackOrChannelIdx) return &ch.generatorPlugin;
        }
        if (model.channels.length > 0) return &model.channels[0].generatorPlugin;
        return null;
    }
    foreach (ref t; model.tracks) {
        if (t.index == trackOrChannelIdx) {
            foreach (ref p; t.plugins) {
                if (p.pluginIndex == fxSlotIdx) return &p;
            }
            if (t.plugins.length > 0) return &t.plugins[0];
        }
    }
    return null;
}

/// Renders the FL Studio 2026 Fruity Wrapper Plugin Editor canvas.
void drawPluginWrapperCanvas(
    DrawBuf buf,
    Rect rc,
    const(TransportModel)* model,
    const(PluginSlotState)* plug,
    FontRef fnt = FontRef.init
) {
    const pal = model ? model.palette() : getFlPalette();
    buf.fillRect(rc, pal.bgWindow);
    if (rc.width < 260 || rc.height < 160) return;

    // 1. Wrapper Header Bar (< Preset >, VST3 GUI, Mix, Bypass)
    Rect hdrRc = Rect(rc.left, rc.top, rc.right, rc.top + WRAP_HDR_H);
    buf.fillRect(hdrRc, pal.bgPanelAlt);
    buf.fillRect(Rect(hdrRc.left, hdrRc.bottom - 1, hdrRc.right, hdrRc.bottom), pal.borderStrong);

    const string plugTitle = plug ? plug.name : "No Plugin Loaded";
    if (!fnt.isNull) {
        fnt.drawText(buf, hdrRc.left + 10, hdrRc.top + 8,
                     toUTF32(truncateLabel(plugTitle, 18)), pal.accentOrange);
    }

    // Preset selector < Preset >
    Rect prevRc = Rect(hdrRc.left + 150, hdrRc.top + 5, hdrRc.left + 172, hdrRc.bottom - 5);
    Rect presRc = Rect(hdrRc.left + 174, hdrRc.top + 5, hdrRc.left + 286, hdrRc.bottom - 5);
    Rect nextRc = Rect(hdrRc.left + 288, hdrRc.top + 5, hdrRc.left + 310, hdrRc.bottom - 5);
    drawCard(buf, prevRc, pal.bgControl, pal.borderSubtle, 3.0f);
    drawCard(buf, presRc, pal.bgLcd, pal.borderSubtle, 3.0f);
    drawCard(buf, nextRc, pal.bgControl, pal.borderSubtle, 3.0f);

    string presetName = "Default";
    if (plug && plug.presets.length > 0) {
        const size_t pi = plug.presetIndex % plug.presets.length;
        presetName = plug.presets[pi];
    }
    if (!fnt.isNull) {
        fnt.drawText(buf, prevRc.left + 7, prevRc.top + 3, "<"d, pal.fgText);
        fnt.drawText(buf, presRc.left + 6, presRc.top + 3,
                     toUTF32(truncateLabel(presetName, 14)), pal.accentCyan);
        fnt.drawText(buf, nextRc.left + 7, nextRc.top + 3, ">"d, pal.fgText);
    }

    // VST3 Native GUI button + Bypass LED
    Rect guiRc = Rect(hdrRc.right - 92, hdrRc.top + 5, hdrRc.right - 34, hdrRc.bottom - 5);
    Rect bypRc = Rect(hdrRc.right - 28, hdrRc.top + 7, hdrRc.right - 8, hdrRc.bottom - 7);
    drawCard(buf, guiRc, pal.bgControl, pal.borderSubtle, 3.0f);
    drawCard(buf, bypRc, (plug && plug.enabled) ? pal.accentGreen : pal.ledOff, pal.borderStrong, 3.0f);
    if (!fnt.isNull) {
        fnt.drawText(buf, guiRc.left + 8, guiRc.top + 3, "EDITOR"d, pal.fgText);
    }

    if (!plug) return;

    // 2. DSP Visual Response Curve Box
    Rect scopeRc = Rect(rc.left + 10, hdrRc.bottom + 8, rc.right - 10, hdrRc.bottom + 8 + SCOPE_H);
    drawCard(buf, scopeRc, pal.bgLcd, pal.borderSubtle, 4.0f);

    const float p0 = (plug.params.length > 0) ? plug.params[0].currentValue : 0.65f;
    const float p1 = (plug.params.length > 1) ? plug.params[1].currentValue : 0.35f;
    const float p2 = (plug.params.length > 2) ? plug.params[2].currentValue : 0.50f;
    const int sw = scopeRc.width - 16;
    const int sh = scopeRc.height - 16;
    if (sw > 24 && sh > 16) {
        const float cutoffX = scopeRc.left + 8 + p0 * sw;
        float prevX = scopeRc.left + 8;
        float prevY = scopeRc.bottom - 8 - sh * 0.55f;
        enum int STEPS = 48;
        foreach (i; 1 .. STEPS + 1) {
            const float t = cast(float)i / cast(float)STEPS;
            const float x = scopeRc.left + 8 + t * sw;
            float resp;
            if (plug.isInstrument) {
                // Resonant low-pass filter response curve
                const float dist = (x - cutoffX) / (sw * 0.14f);
                const float pass = (t < p0) ? 0.55f : 0.55f * expf(-(t - p0) * 7.5f);
                const float resPeak = p1 * 0.42f * expf(-dist * dist);
                resp = pass + resPeak;
            } else {
                // Smooth FX / EQ spectral curve
                resp = 0.48f + (p0 - 0.5f) * 0.35f * cosf(t * 4.5f + p1 * 3.14f) +
                       (p2 - 0.5f) * 0.2f;
            }
            if (resp < 0.04f) resp = 0.04f;
            if (resp > 0.96f) resp = 0.96f;
            const float y = scopeRc.bottom - 8 - resp * sh;
            drawThickLine(buf, prevX, prevY, x, y, 2.0f,
                          plug.isInstrument ? pal.accentOrange : pal.accentCyan);
            prevX = x;
            prevY = y;
        }
    }

    // 3. Interactive Rotary Knob Grid (up to 8 params in 2 rows x 4 cols)
    const int gridTop = scopeRc.bottom + 10;
    const int gridH = rc.bottom - gridTop - 8;
    const int cols = 4;
    const int cellW = (rc.width - 20) / cols;
    const int cellH = gridH / 2;
    const size_t count = plug.params.length > 8 ? 8 : plug.params.length;

    foreach (i; 0 .. count) {
        const int col = cast(int)i % cols;
        const int row = cast(int)i / cols;
        const int cx = rc.left + 10 + col * cellW + cellW / 2;
        const int cy = gridTop + row * cellH + cellH / 2 - 6;
        const auto pm = plug.params[i];
        const uint arcCol = (i % 2 == 0) ? pal.accentOrange : pal.accentCyan;

        drawRotaryKnob(buf, cx, cy, 17, pm.currentValue,
                       pal.bgLcd, arcCol, pal.bgControl, pal.borderSubtle, false);

        if (!fnt.isNull) {
            const string lbl = truncateLabel(pm.name, 11);
            const string valStr = format("%d%%", cast(int)(pm.currentValue * 100.0f + 0.5f));
            fnt.drawText(buf, cx - 28, cy + 20, toUTF32(lbl), pal.fgText);
            fnt.drawText(buf, cx - 14, cy - 30, toUTF32(valStr), pal.fgMuted);
        }
    }
}

/// Interactive Floating FL Studio Fruity Wrapper Window Widget.
class PluginWrapperWidget : Widget {
    private TransportModel* model_;
    WrapperTargetKind targetKind = WrapperTargetKind.CHANNEL_GENERATOR;
    int targetIndex = 3; // Acid Bass by default
    int targetFxSlot = 0;

    private int dragParamIdx_ = -1;
    private int dragStartY_ = 0;
    private float dragStartVal_ = 0.0f;

    void delegate(int trackIdx, int pluginIdx, uint paramId, float value) onSetParam;
    void delegate(int trackIdx, int pluginIdx) onShowNativeGui;

    this(string id, TransportModel* model) {
        super(id);
        model_ = model;
        layoutWidth = FILL_PARENT;
        layoutHeight = FILL_PARENT;
    }

    void setTargetChannel(int channelIdx) {
        targetKind = WrapperTargetKind.CHANNEL_GENERATOR;
        targetIndex = channelIdx;
        invalidate();
    }

    void setTargetMixerFx(int mixerInsert, int slotIdx) {
        targetKind = WrapperTargetKind.MIXER_FX;
        targetIndex = mixerInsert;
        targetFxSlot = slotIdx;
        invalidate();
    }

    override void onDraw(DrawBuf buf) {
        if (visibility != Visibility.Visible) return;
        auto plug = resolveActiveWrapperPlugin(model_, targetKind, targetIndex, targetFxSlot);
        drawPluginWrapperCanvas(buf, _pos, model_, plug, font);
    }

    override bool onMouseEvent(MouseEvent event) {
        if (event.action == MouseAction.FocusOut) {
            return dragParamIdx_ >= 0;
        }
        if (!model_) return super.onMouseEvent(event);
        auto plug = resolveActiveWrapperPlugin(model_, targetKind, targetIndex, targetFxSlot);
        if (!plug) return super.onMouseEvent(event);

        if (event.action == MouseAction.ButtonDown) {
            activateParentWindow(this);

            // Header bar buttons
            if (event.y >= _pos.top && event.y < _pos.top + WRAP_HDR_H) {
                Rect prevRc = Rect(_pos.left + 150, _pos.top + 5, _pos.left + 172, _pos.top + WRAP_HDR_H - 5);
                Rect nextRc = Rect(_pos.left + 288, _pos.top + 5, _pos.left + 310, _pos.top + WRAP_HDR_H - 5);
                Rect guiRc = Rect(_pos.right - 92, _pos.top + 5, _pos.right - 34, _pos.top + WRAP_HDR_H - 5);
                Rect bypRc = Rect(_pos.right - 28, _pos.top + 7, _pos.right - 8, _pos.top + WRAP_HDR_H - 7);

                if (prevRc.isPointInside(event.x, event.y) && plug.presets.length > 0) {
                    plug.presetIndex = (plug.presetIndex + cast(int)plug.presets.length - 1) % cast(int)plug.presets.length;
                    invalidate();
                    return true;
                }
                if (nextRc.isPointInside(event.x, event.y) && plug.presets.length > 0) {
                    plug.presetIndex = (plug.presetIndex + 1) % cast(int)plug.presets.length;
                    invalidate();
                    return true;
                }
                if (guiRc.isPointInside(event.x, event.y)) {
                    const int trk = (targetKind == WrapperTargetKind.MIXER_FX) ? targetIndex : model_.selectedTrack;
                    if (onShowNativeGui !is null) onShowNativeGui(trk, plug.pluginIndex);
                    return true;
                }
                if (bypRc.isPointInside(event.x, event.y)) {
                    plug.enabled = !plug.enabled;
                    invalidate();
                    return true;
                }
                return true;
            }

            // Knob grid hit test
            const int gridTop = _pos.top + WRAP_HDR_H + 8 + SCOPE_H + 10;
            const int gridH = _pos.bottom - gridTop - 8;
            if (event.y >= gridTop && gridH > 40) {
                const int col = (event.x - (_pos.left + 10)) * 4 / (_pos.width - 20);
                const int row = (event.y - gridTop) * 2 / gridH;
                const int idx = row * 4 + col;
                if (col >= 0 && col < 4 && row >= 0 && row < 2 && idx < cast(int)plug.params.length) {
                    if (event.button == MouseButton.Right) {
                        plug.params[idx].currentValue = plug.params[idx].defaultValue;
                        const int trk = (targetKind == WrapperTargetKind.MIXER_FX) ? targetIndex : model_.selectedTrack;
                        if (onSetParam !is null) {
                            onSetParam(trk, plug.pluginIndex, plug.params[idx].id, plug.params[idx].currentValue);
                        }
                        invalidate();
                    } else {
                        dragParamIdx_ = idx;
                        dragStartY_ = event.y;
                        dragStartVal_ = plug.params[idx].currentValue;
                    }
                    return true;
                }
            }
            return true;
        }

        if (event.action == MouseAction.Move && dragParamIdx_ >= 0 &&
            dragParamIdx_ < cast(int)plug.params.length) {
            const float delta = cast(float)(dragStartY_ - event.y) / 110.0f;
            float v = dragStartVal_ + delta;
            if (v < 0.0f) v = 0.0f;
            if (v > 1.0f) v = 1.0f;
            plug.params[dragParamIdx_].currentValue = v;
            model_.hintText = format("%s — %s: %d%%", plug.name, plug.params[dragParamIdx_].name,
                                     cast(int)(v * 100.0f + 0.5f));
            const int trk = (targetKind == WrapperTargetKind.MIXER_FX) ? targetIndex : model_.selectedTrack;
            if (onSetParam !is null) {
                onSetParam(trk, plug.pluginIndex, plug.params[dragParamIdx_].id, v);
            }
            invalidate();
            return true;
        }

        if (event.action == MouseAction.ButtonUp && dragParamIdx_ >= 0) {
            dragParamIdx_ = -1;
            return true;
        }

        return super.onMouseEvent(event);
    }
}

unittest {
    import dlangui.graphics.drawbuf : ColorDrawBuf;

    TransportModel model;
    model.initDefaultTemplate();
    auto plug = resolveActiveWrapperPlugin(&model, WrapperTargetKind.CHANNEL_GENERATOR, 3, 0);
    assert(plug !is null && plug.name == "Acid Bass");

    auto buf = new ColorDrawBuf(440, 260);
    drawPluginWrapperCanvas(buf, Rect(0, 0, 440, 260), &model, plug);
    assert(buf.scanLine(12)[200] == model.palette().bgLcd);
}
