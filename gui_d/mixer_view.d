module gui_d.mixer_view;

import dlangui;
import dlangui.graphics.drawbuf : DrawBuf;
import dlangui.widgets.widget : Visibility, Widget;
import gui_d.theme : drawCard, drawLevelMeter, drawRotaryKnob, drawThickLine, FlPalette, getFlPalette, truncateLabel;
import gui_d.transport_model : PluginSlotState, TrackStripState, TransportModel;
import gui_d.workspace : activateParentWindow;
import std.format : format;
import std.utf : toUTF32;

private enum int MIX_TOOLBAR_H = 26;
private enum int STRIP_W = 66;
private enum int CURRENT_W = 48;
private enum int FX_RACK_W = 222;
private enum int FX_SLOT_COUNT = 10;

/// Renders a single FL Studio Mixer Insert strip inside `stripRc`.
private void drawMixerStrip(
    DrawBuf buf,
    Rect stripRc,
    const(TrackStripState)* trk,
    int displayIdx,
    bool isMaster,
    bool isSelected,
    const ref FlPalette pal,
    FontRef fnt
) {
    const uint bg = isSelected ? pal.bgControl : (isMaster ? pal.bgPanelAlt : pal.bgPanel);
    drawCard(buf, stripRc, bg, isSelected ? pal.accentOrange : pal.borderStrong, 3.0f);

    const uint trkCol = trk ? trk.color : pal.clipPattern;
    buf.fillRect(Rect(stripRc.left + 2, stripRc.top + 2, stripRc.right - 2, stripRc.top + 6), trkCol);

    const string label = trk ? trk.name : (isMaster ? "Master" : format("Ins %d", displayIdx));
    if (!fnt.isNull) {
        const string numStr = isMaster ? "M" : format("%d", displayIdx);
        fnt.drawText(buf, stripRc.left + 5, stripRc.top + 8, toUTF32(numStr),
                     isSelected ? pal.accentOrange : pal.fgMuted);
        fnt.drawText(buf, stripRc.left + 4, stripRc.top + 22,
                     toUTF32(truncateLabel(label, 8)), pal.fgText);
    }

    const bool muted = trk ? trk.muted : false;
    const bool soloed = trk ? trk.soloed : false;

    // Mute LED & Solo LED
    Rect muteRc = Rect(stripRc.left + 6, stripRc.top + 38, stripRc.left + 28, stripRc.top + 50);
    Rect soloRc = Rect(stripRc.left + 32, stripRc.top + 38, stripRc.left + 58, stripRc.top + 50);
    drawCard(buf, muteRc, muted ? pal.ledOff : pal.accentGreen, pal.borderStrong, 3.0f);
    drawCard(buf, soloRc, soloed ? pal.accentAmber : pal.bgControl, pal.borderStrong, 3.0f);
    if (!fnt.isNull) {
        fnt.drawText(buf, soloRc.left + 8, soloRc.top, "S"d, soloed ? pal.bgLcd : pal.fgMuted);
    }

    // Pan knob (left) & Stereo Separation knob (right)
    const float pan = trk ? trk.pan : 0.0f;
    const float sep = trk ? trk.stereoSep : 0.0f;
    drawRotaryKnob(buf, stripRc.left + 18, stripRc.top + 66, 9, (pan + 1.0f) * 0.5f,
                   pal.bgLcd, pal.accentCyan, pal.bgControl, pal.borderSubtle, true);
    drawRotaryKnob(buf, stripRc.left + 46, stripRc.top + 66, 9, (sep + 1.0f) * 0.5f,
                   pal.bgLcd, pal.accentPurple, pal.bgControl, pal.borderSubtle, true);

    // Fader groove (left) & Stereo Meter (right)
    const int faderTop = stripRc.top + 84;
    const int faderBot = stripRc.bottom - 24;
    if (faderBot - faderTop > 36) {
        Rect meterRc = Rect(stripRc.right - 22, faderTop, stripRc.right - 6, faderBot);
        const float pL = trk ? trk.peakL : 0.0f;
        const float pR = trk ? trk.peakR : 0.0f;
        drawLevelMeter(buf, meterRc, pL, pR, pal, 16);

        Rect grooveRc = Rect(stripRc.left + 17, faderTop + 4, stripRc.left + 23, faderBot - 4);
        drawCard(buf, grooveRc, pal.bgLcd, pal.borderStrong, 2.0f);

        // Unity 0 dB tick mark (~80%)
        const int zeroY = grooveRc.bottom - 8 - cast(int)(0.8f * (grooveRc.height - 16));
        buf.fillRect(Rect(stripRc.left + 8, zeroY, stripRc.left + 32, zeroY + 1), pal.fgDim);

        float vol = trk ? trk.volume : 0.8f;
        if (vol < 0.0f) vol = 0.0f;
        if (vol > 1.0f) vol = 1.0f;
        const int capY = grooveRc.bottom - cast(int)(vol * (grooveRc.height - 16)) - 16;
        Rect capRc = Rect(stripRc.left + 10, capY, stripRc.left + 30, capY + 16);
        drawCard(buf, capRc, 0x008A94A3, pal.borderStrong, 3.0f);
        buf.fillRect(Rect(capRc.left + 2, capRc.middley, capRc.right - 2, capRc.middley + 2),
                     pal.bgLcd);
    }

    // Bottom routing badge
    Rect routeRc = Rect(stripRc.left + 6, stripRc.bottom - 20, stripRc.right - 6, stripRc.bottom - 4);
    drawCard(buf, routeRc, pal.bgLcd, pal.borderSubtle, 2.0f);
    if (!fnt.isNull) {
        fnt.drawText(buf, routeRc.left + 12, routeRc.top + 1,
                     isMaster ? "OUT"d : "-> M"d, pal.accentOrange);
    }
}

/// Renders the complete FL Studio 2026 Mixer (Current, Master, Insert Strips, 10-Slot FX Rack, 3-Band EQ).
void drawMixerCanvas(
    DrawBuf buf,
    Rect rc,
    const(TransportModel)* model,
    FontRef fnt = FontRef.init
) {
    const pal = model ? model.palette() : getFlPalette();
    buf.fillRect(rc, pal.bgWindow);
    if (rc.width < 260 || rc.height < 140) return;

    // 1. Top Mixer Toolbar
    Rect tbRc = Rect(rc.left, rc.top, rc.right, rc.top + MIX_TOOLBAR_H);
    buf.fillRect(tbRc, pal.bgPanelAlt);
    buf.fillRect(Rect(tbRc.left, tbRc.bottom - 1, tbRc.right, tbRc.bottom), pal.borderStrong);

    const int selIns = model ? model.selectedMixerInsert : 1;
    string selName = format("Insert %d", selIns);
    if (model) {
        foreach (const ref t; model.tracks) {
            if (t.index == selIns) {
                selName = (selIns == 0) ? "Master" : format("Insert %d: %s", selIns, t.name);
                break;
            }
        }
    }
    if (!fnt.isNull) {
        fnt.drawText(buf, tbRc.left + 10, tbRc.top + 5,
                     toUTF32("MIXER  |  " ~ selName), pal.accentOrange);
    }

    const bool showFx = model ? model.showMixerFxRack : true;
    Rect fxToggleRc = Rect(tbRc.right - 74, tbRc.top + 4, tbRc.right - 8, tbRc.bottom - 4);
    drawCard(buf, fxToggleRc, showFx ? pal.accentOrange : pal.bgControl, pal.borderSubtle, 3.0f);
    if (!fnt.isNull) {
        fnt.drawText(buf, fxToggleRc.left + 10, fxToggleRc.top + 2, "FX RACK"d,
                     showFx ? pal.bgLcd : pal.fgText);
    }

    const int bodyTop = tbRc.bottom + 4;
    const int bodyBot = rc.bottom - 4;
    const int stripsRight = (showFx && rc.width > FX_RACK_W + 280) ? (rc.right - FX_RACK_W) : rc.right - 4;

    // Find selected insert pointer & master pointer
    const(TrackStripState)* masterTrk = (model && model.tracks.length > 0) ? &model.tracks[0] : null;
    const(TrackStripState)* selTrk = masterTrk;
    if (model) {
        foreach (const ref t; model.tracks) {
            if (t.index == selIns) {
                selTrk = &t;
                break;
            }
        }
    }

    // 2. Left Docked "C" (Current) big meter + "M" (Master) strip
    Rect curRc = Rect(rc.left + 4, bodyTop, rc.left + 4 + CURRENT_W, bodyBot);
    drawCard(buf, curRc, pal.bgPanelAlt, pal.borderStrong, 3.0f);
    if (!fnt.isNull) {
        fnt.drawText(buf, curRc.left + 18, curRc.top + 6, "C"d, pal.accentOrange);
    }
    if (curRc.height > 60) {
        drawLevelMeter(buf, Rect(curRc.left + 8, curRc.top + 28, curRc.right - 8, curRc.bottom - 12),
                       selTrk ? selTrk.peakL : 0.0f, selTrk ? selTrk.peakR : 0.0f, pal, 24);
    }

    Rect masterRc = Rect(curRc.right + 4, bodyTop, curRc.right + 4 + STRIP_W, bodyBot);
    drawMixerStrip(buf, masterRc, masterTrk, 0, true, selIns == 0, pal, fnt);

    // Dock divider
    const int divX = masterRc.right + 4;
    buf.fillRect(Rect(divX, bodyTop, divX + 2, bodyBot), pal.borderSubtle);

    // 3. Insert Strips 1..N
    const int insStartX = divX + 6;
    const int insCount = (model && model.tracks.length > 1) ? cast(int)model.tracks.length - 1 : 8;
    foreach (i; 0 .. insCount) {
        const int x0 = insStartX + i * (STRIP_W + 3);
        const int x1 = x0 + STRIP_W;
        if (x1 > stripsRight - 2) break;
        const(TrackStripState)* tPtr = (model && i + 1 < cast(int)model.tracks.length)
            ? &model.tracks[i + 1]
            : null;
        const int tIdx = tPtr ? tPtr.index : (i + 1);
        drawMixerStrip(buf, Rect(x0, bodyTop, x1, bodyBot), tPtr, tIdx, false,
                       selIns == tIdx, pal, fnt);
    }

    // 4. Right Docked 10-Slot FX Rack + 3-Band EQ
    if (showFx && rc.width > FX_RACK_W + 280) {
        Rect fxRc = Rect(stripsRight + 2, bodyTop, rc.right - 4, bodyBot);
        drawCard(buf, fxRc, pal.bgPanelAlt, pal.borderStrong, 4.0f);
        if (!fnt.isNull) {
            fnt.drawText(buf, fxRc.left + 8, fxRc.top + 6,
                         toUTF32(format("FX SLOTS - %s", truncateLabel(selTrk ? selTrk.name : "Insert", 12))),
                         pal.accentOrange);
        }

        const int eqH = 92;
        const int slotsTop = fxRc.top + 26;
        const int slotsBot = fxRc.bottom - eqH - 4;
        const int slotH = (slotsBot - slotsTop) / FX_SLOT_COUNT;

        if (slotH >= 14) {
            foreach (s; 0 .. FX_SLOT_COUNT) {
                const int sy0 = slotsTop + s * slotH;
                const int sy1 = sy0 + slotH - 2;
                Rect sRc = Rect(fxRc.left + 6, sy0, fxRc.right - 6, sy1);

                const(PluginSlotState)* plug = null;
                if (selTrk) {
                    foreach (const ref p; selTrk.plugins) {
                        if (p.pluginIndex == s) {
                            plug = &p;
                            break;
                        }
                    }
                }
                const bool hasPlug = plug !is null;
                const bool selSlot = model && model.selectedFxSlot == s;
                drawCard(buf, sRc, hasPlug ? pal.bgControl : pal.bgLcd,
                         selSlot ? pal.accentOrange : pal.borderStrong, 3.0f);

                // Slot index & name
                if (!fnt.isNull) {
                    const string sLbl = hasPlug
                        ? format("%d  %s", s + 1, truncateLabel(plug.name, 14))
                        : format("%d  (empty)", s + 1);
                    fnt.drawText(buf, sRc.left + 6, sRc.top + 2, toUTF32(sLbl),
                                 hasPlug ? pal.fgText : pal.fgDim);
                }

                // Mix knob & Enable LED on right of slot
                if (hasPlug) {
                    drawRotaryKnob(buf, sRc.right - 28, sRc.middley, 6, plug.mix,
                                   pal.bgLcd, pal.accentCyan, pal.bgPanel, pal.borderSubtle);
                    Rect enLed = Rect(sRc.right - 14, sRc.top + 4, sRc.right - 4, sRc.bottom - 4);
                    drawCard(buf, enLed, plug.enabled ? pal.accentGreen : pal.ledOff, pal.borderStrong, 2.0f);
                }
            }
        }

        // 3-Band Insert EQ Panel at bottom of FX Rack
        if (fxRc.height > 150) {
            Rect eqRc = Rect(fxRc.left + 6, fxRc.bottom - eqH, fxRc.right - 6, fxRc.bottom - 6);
            drawCard(buf, eqRc, pal.bgLcd, pal.borderSubtle, 3.0f);
            const float low = selTrk ? selTrk.eqLow : 0.5f;
            const float mid = selTrk ? selTrk.eqMid : 0.5f;
            const float high = selTrk ? selTrk.eqHigh : 0.5f;

            // Mini EQ curve graphic
            const int cy = eqRc.top + 24;
            const float yL = cy + (0.5f - low) * 26.0f;
            const float yM = cy + (0.5f - mid) * 26.0f;
            const float yH = cy + (0.5f - high) * 26.0f;
            buf.fillRect(Rect(eqRc.left + 6, cy, eqRc.right - 6, cy + 1), pal.borderSubtle);
            drawThickLine(buf, eqRc.left + 8, yL, eqRc.middlex, yM, 2.0f, pal.accentCyan);
            drawThickLine(buf, eqRc.middlex, yM, eqRc.right - 8, yH, 2.0f, pal.accentOrange);

            // Low / Mid / High rotary knobs
            drawRotaryKnob(buf, eqRc.left + 34,  eqRc.bottom - 20, 10, low,
                           pal.bgPanelAlt, pal.accentCyan, pal.bgControl, pal.borderSubtle, true);
            drawRotaryKnob(buf, eqRc.middlex,    eqRc.bottom - 20, 10, mid,
                           pal.bgPanelAlt, pal.accentGreen, pal.bgControl, pal.borderSubtle, true);
            drawRotaryKnob(buf, eqRc.right - 34, eqRc.bottom - 20, 10, high,
                           pal.bgPanelAlt, pal.accentOrange, pal.bgControl, pal.borderSubtle, true);
        }
    }
}

/// Interactive FL Studio 2026 Mixer Widget (Faders, Pan/Stereo Sep Knobs, M/S, 10-Slot FX Rack, 3-Band EQ).
class MixerCanvasWidget : Widget {
    private TransportModel* model_;

    private enum DragMode { NONE, FADER, PAN, STEREO_SEP, EQ_LOW, EQ_MID, EQ_HIGH, FX_MIX }
    private DragMode dragMode_ = DragMode.NONE;
    private int dragTrackIdx_ = 0;
    private int dragSlotIdx_ = 0;
    private int dragStartY_ = 0;
    private float dragStartVal_ = 0.0f;

    void delegate(int trackIdx, float volume) onVolumeChange;
    void delegate(int trackIdx, bool muted) onMuteToggle;
    void delegate(int trackIdx, bool soloed) onSoloToggle;
    void delegate(int trackIdx, int slotIdx, string uri) onLoadFxSlot;
    void delegate(int trackIdx, int slotIdx) onOpenFxPlugin;

    this(string id, TransportModel* model) {
        super(id);
        model_ = model;
        layoutWidth = FILL_PARENT;
        layoutHeight = FILL_PARENT;
    }

    override void onDraw(DrawBuf buf) {
        if (visibility != Visibility.Visible) return;
        drawMixerCanvas(buf, _pos, model_, font);
    }

    override bool onMouseEvent(MouseEvent event) {
        if (event.action == MouseAction.FocusOut) {
            return dragMode_ != DragMode.NONE;
        }
        if (!model_) return super.onMouseEvent(event);

        const int bodyTop = _pos.top + MIX_TOOLBAR_H + 4;
        const int bodyBot = _pos.bottom - 4;
        const bool showFx = model_.showMixerFxRack && _pos.width > FX_RACK_W + 280;
        const int stripsRight = showFx ? (_pos.right - FX_RACK_W) : (_pos.right - 4);

        if (event.action == MouseAction.ButtonDown) {
            activateParentWindow(this);

            // 1. Toolbar FX Rack toggle
            if (event.y >= _pos.top && event.y < _pos.top + MIX_TOOLBAR_H) {
                Rect fxToggleRc = Rect(_pos.right - 74, _pos.top + 4, _pos.right - 8, _pos.top + MIX_TOOLBAR_H - 4);
                if (fxToggleRc.isPointInside(event.x, event.y)) {
                    model_.showMixerFxRack = !model_.showMixerFxRack;
                    invalidate();
                }
                return true;
            }

            // 2. Right FX Rack & 3-Band EQ clicks
            if (showFx && event.x >= stripsRight + 2) {
                Rect fxRc = Rect(stripsRight + 2, bodyTop, _pos.right - 4, bodyBot);
                const int eqH = 92;
                const int slotsTop = fxRc.top + 26;
                const int slotsBot = fxRc.bottom - eqH - 4;
                const int slotH = (slotsBot - slotsTop) / FX_SLOT_COUNT;

                // 3-Band EQ knobs
                if (event.y >= fxRc.bottom - eqH) {
                    const size_t tArr = model_.ensureTrackIndex(model_.selectedMixerInsert);
                    auto trk = &model_.tracks[tArr];
                    const int midX = (fxRc.left + 6 + fxRc.right - 6) / 2;
                    if (event.x < midX - 24) {
                        if (event.button == MouseButton.Right) trk.eqLow = 0.5f;
                        else { dragMode_ = DragMode.EQ_LOW; dragStartVal_ = trk.eqLow; }
                    } else if (event.x > midX + 24) {
                        if (event.button == MouseButton.Right) trk.eqHigh = 0.5f;
                        else { dragMode_ = DragMode.EQ_HIGH; dragStartVal_ = trk.eqHigh; }
                    } else {
                        if (event.button == MouseButton.Right) trk.eqMid = 0.5f;
                        else { dragMode_ = DragMode.EQ_MID; dragStartVal_ = trk.eqMid; }
                    }
                    dragTrackIdx_ = model_.selectedMixerInsert;
                    dragStartY_ = event.y;
                    invalidate();
                    return true;
                }

                // 10 FX Slots
                if (slotH >= 14 && event.y >= slotsTop && event.y < slotsBot) {
                    const int slotIdx = (event.y - slotsTop) / slotH;
                    if (slotIdx >= 0 && slotIdx < FX_SLOT_COUNT) {
                        model_.selectedFxSlot = slotIdx;
                        const size_t tArr = model_.ensureTrackIndex(model_.selectedMixerInsert);
                        auto trk = &model_.tracks[tArr];
                        PluginSlotState* plug = null;
                        foreach (ref p; trk.plugins) {
                            if (p.pluginIndex == slotIdx) {
                                plug = &p;
                                break;
                            }
                        }
                        if (plug !is null) {
                            if (event.x >= fxRc.right - 22) {
                                plug.mix = plug.mix; // keep
                                plug.enabled = !plug.enabled;
                            } else if (event.x >= fxRc.right - 44) {
                                dragMode_ = DragMode.FX_MIX;
                                dragTrackIdx_ = model_.selectedMixerInsert;
                                dragSlotIdx_ = slotIdx;
                                dragStartY_ = event.y;
                                dragStartVal_ = plug.mix;
                            } else {
                                if (onOpenFxPlugin !is null) onOpenFxPlugin(model_.selectedMixerInsert, slotIdx);
                            }
                        } else {
                            // Quick-load an effect onto the empty FL Mixer slot
                            static immutable string[6] QUICK_URIS = [
                                "builtin://eq", "builtin://compressor", "builtin://reverb",
                                "builtin://delay", "builtin://hott", "builtin://limiter"
                            ];
                            static immutable string[6] QUICK_NAMES = [
                                "Parametric EQ", "Compressor", "Stereo Reverb",
                                "Ping-Pong Delay", "HOTT Multiband", "Fruity Limiter"
                            ];
                            const size_t q = slotIdx % QUICK_URIS.length;
                            auto newPlug = TransportModel.makeDefaultPluginSlot(QUICK_NAMES[q], QUICK_URIS[q], false, slotIdx);
                            trk.plugins ~= newPlug;
                            if (onLoadFxSlot !is null) onLoadFxSlot(model_.selectedMixerInsert, slotIdx, QUICK_URIS[q]);
                            if (onOpenFxPlugin !is null) onOpenFxPlugin(model_.selectedMixerInsert, slotIdx);
                        }
                        invalidate();
                    }
                    return true;
                }
                return true;
            }

            // 3. Master & Insert Strips hit-test
            Rect curRc = Rect(_pos.left + 4, bodyTop, _pos.left + 4 + CURRENT_W, bodyBot);
            Rect masterRc = Rect(curRc.right + 4, bodyTop, curRc.right + 4 + STRIP_W, bodyBot);
            const int insStartX = masterRc.right + 12;

            bool handleStripClick(Rect stripRc, int trackIdx) {
                if (!stripRc.isPointInside(event.x, event.y)) return false;
                model_.selectedMixerInsert = trackIdx;
                const size_t arrIdx = model_.ensureTrackIndex(trackIdx);
                auto trk = &model_.tracks[arrIdx];

                Rect muteRc = Rect(stripRc.left + 6, stripRc.top + 38, stripRc.left + 28, stripRc.top + 50);
                Rect soloRc = Rect(stripRc.left + 32, stripRc.top + 38, stripRc.left + 58, stripRc.top + 50);
                if (muteRc.isPointInside(event.x, event.y)) {
                    trk.muted = !trk.muted;
                    if (onMuteToggle !is null) onMuteToggle(trackIdx, trk.muted);
                    invalidate();
                    return true;
                }
                if (soloRc.isPointInside(event.x, event.y)) {
                    trk.soloed = !trk.soloed;
                    if (onSoloToggle !is null) onSoloToggle(trackIdx, trk.soloed);
                    invalidate();
                    return true;
                }

                // Pan / Stereo Separation knobs
                if (event.y >= stripRc.top + 52 && event.y <= stripRc.top + 80) {
                    dragTrackIdx_ = trackIdx;
                    dragStartY_ = event.y;
                    if (event.x < stripRc.middlex) {
                        if (event.button == MouseButton.Right) trk.pan = 0.0f;
                        else { dragMode_ = DragMode.PAN; dragStartVal_ = trk.pan; }
                    } else {
                        if (event.button == MouseButton.Right) trk.stereoSep = 0.0f;
                        else { dragMode_ = DragMode.STEREO_SEP; dragStartVal_ = trk.stereoSep; }
                    }
                    invalidate();
                    return true;
                }

                // Fader groove region
                const int faderTop = stripRc.top + 84;
                const int faderBot = stripRc.bottom - 24;
                if (event.y >= faderTop && event.y <= faderBot && event.x < stripRc.right - 22) {
                    if (event.button == MouseButton.Right) {
                        trk.volume = 0.8f;
                        if (onVolumeChange !is null) onVolumeChange(trackIdx, trk.volume);
                    } else {
                        dragMode_ = DragMode.FADER;
                        dragTrackIdx_ = trackIdx;
                        updateFaderFromY(trackIdx, event.y, faderTop + 4, faderBot - 4);
                    }
                    invalidate();
                    return true;
                }
                invalidate();
                return true;
            }

            if (handleStripClick(masterRc, 0)) return true;

            const int insCount = (model_.tracks.length > 1) ? cast(int)model_.tracks.length - 1 : 8;
            foreach (i; 0 .. insCount) {
                const int x0 = insStartX + i * (STRIP_W + 3);
                const int x1 = x0 + STRIP_W;
                if (x1 > stripsRight - 2) break;
                const int tIdx = (i + 1 < cast(int)model_.tracks.length) ? model_.tracks[i + 1].index : (i + 1);
                if (handleStripClick(Rect(x0, bodyTop, x1, bodyBot), tIdx)) return true;
            }
            return true;
        }

        if (event.action == MouseAction.Move && dragMode_ != DragMode.NONE) {
            const float delta = cast(float)(dragStartY_ - event.y) / 90.0f;
            const size_t arrIdx = model_.ensureTrackIndex(dragTrackIdx_);
            auto trk = &model_.tracks[arrIdx];

            final switch (dragMode_) {
                case DragMode.NONE:
                    break;
                case DragMode.FADER:
                    updateFaderFromY(dragTrackIdx_, event.y, bodyTop + 88, bodyBot - 28);
                    break;
                case DragMode.PAN:
                    float p = dragStartVal_ + delta * 2.0f;
                    if (p < -1.0f) p = -1.0f;
                    if (p > 1.0f) p = 1.0f;
                    trk.pan = p;
                    break;
                case DragMode.STEREO_SEP:
                    float sp = dragStartVal_ + delta * 2.0f;
                    if (sp < -1.0f) sp = -1.0f;
                    if (sp > 1.0f) sp = 1.0f;
                    trk.stereoSep = sp;
                    break;
                case DragMode.EQ_LOW:
                    float vL = dragStartVal_ + delta;
                    trk.eqLow = vL < 0.0f ? 0.0f : (vL > 1.0f ? 1.0f : vL);
                    break;
                case DragMode.EQ_MID:
                    float vM = dragStartVal_ + delta;
                    trk.eqMid = vM < 0.0f ? 0.0f : (vM > 1.0f ? 1.0f : vM);
                    break;
                case DragMode.EQ_HIGH:
                    float vH = dragStartVal_ + delta;
                    trk.eqHigh = vH < 0.0f ? 0.0f : (vH > 1.0f ? 1.0f : vH);
                    break;
                case DragMode.FX_MIX:
                    foreach (ref plug; trk.plugins) {
                        if (plug.pluginIndex == dragSlotIdx_) {
                            float mx = dragStartVal_ + delta;
                            plug.mix = mx < 0.0f ? 0.0f : (mx > 1.0f ? 1.0f : mx);
                            break;
                        }
                    }
                    break;
            }
            invalidate();
            return true;
        }

        if (event.action == MouseAction.ButtonUp && dragMode_ != DragMode.NONE) {
            dragMode_ = DragMode.NONE;
            return true;
        }

        return super.onMouseEvent(event);
    }

    private void updateFaderFromY(int trackIdx, int y, int grooveTop, int grooveBot) {
        if (grooveBot - grooveTop <= 16) return;
        const size_t idx = model_.ensureTrackIndex(trackIdx);
        float norm = cast(float)(grooveBot - 8 - y) / cast(float)(grooveBot - grooveTop - 16);
        if (norm < 0.0f) norm = 0.0f;
        if (norm > 1.0f) norm = 1.0f;
        model_.tracks[idx].volume = norm;
        model_.hintText = format("%s Fader: %d%%", model_.tracks[idx].name, cast(int)(norm * 100.0f + 0.5f));
        if (onVolumeChange !is null) onVolumeChange(trackIdx, norm);
    }
}

unittest {
    import dlangui.graphics.drawbuf : ColorDrawBuf;

    TransportModel model;
    model.initDefaultTemplate();
    model.tracks[0].peakL = 0.9f;
    model.tracks[1].peakL = 0.7f;

    auto buf = new ColorDrawBuf(820, 320);
    drawMixerCanvas(buf, Rect(0, 0, 820, 320), &model);

    const pal = model.palette();
    // FX rack should render on the right side
    assert(buf.scanLine(40)[700] != 0, "Expected Mixer FX Rack to render cleanly");
    assert(pal.bgWindow != 0);
}
