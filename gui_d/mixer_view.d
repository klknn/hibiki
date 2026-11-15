module gui_d.mixer_view;

import core.stdc.math : cosf;
import dlangui;
import dlangui.graphics.drawbuf : DrawBuf;
import dlangui.graphics.fonts : FontWeight;
import dlangui.widgets.widget : Visibility, Widget;
import gui_d.icons : DawIcon, drawDawIcon;
import gui_d.theme : adjustColorBrightness, dp, dpf, drawBeveledTrackCard, drawCard, drawCenteredText,
    drawFilledCircle, drawLevelMeter, drawRotaryKnob, drawThickLine, drawVCenteredText, FlPalette,
    getDawFont, getFlPalette, safeWidgetFont, truncateLabel;
import gui_d.transport_model : PluginSlotState, TrackStripState, TransportModel;
import gui_d.workspace : activateParentWindow;
import std.format : format;
import std.utf : toUTF32;

private enum int FX_SLOT_COUNT = 10;

/// DPI-scaled layout geometry shared between `drawMixerCanvas` and `MixerCanvasWidget.onMouseEvent`.
struct MixerMetrics {
    int toolbarH;
    int stripW;
    int currentW;
    int fxRackW;
    int stripGap;
    int bodyTop;
    int bodyBot;
    bool showFx;
    int stripsRight;
    Rect scrLeftRc;
    Rect scrRightRc;
    Rect fxToggleRc;
    Rect curRc;
    Rect masterRc;
    int insStartX;
    Rect fxRc;
    int eqH;
    int slotsTop;
    int slotsBot;
    int slotH;
}

MixerMetrics computeMixerMetrics(Rect rc, const(TransportModel)* model) nothrow @safe {
    MixerMetrics m;
    m.toolbarH = dp(26);
    m.stripW = dp(66);
    m.currentW = dp(48);
    m.fxRackW = dp(222);
    m.stripGap = 0;
    m.bodyTop = rc.top + m.toolbarH + dp(4);
    m.bodyBot = rc.bottom - dp(4);
    m.showFx = (model ? model.showMixerFxRack : true) && rc.width > m.fxRackW + dp(280);
    m.stripsRight = m.showFx ? (rc.right - m.fxRackW) : (rc.right - dp(4));
    m.scrLeftRc = Rect(rc.right - dp(126), rc.top + dp(4), rc.right - dp(100), rc.top + m.toolbarH - dp(4));
    m.scrRightRc = Rect(rc.right - dp(100), rc.top + dp(4), rc.right - dp(74), rc.top + m.toolbarH - dp(4));
    m.fxToggleRc = Rect(rc.right - dp(74), rc.top + dp(4), rc.right - dp(8), rc.top + m.toolbarH - dp(4));
    m.curRc = Rect(rc.left + dp(4), m.bodyTop, rc.left + dp(4) + m.currentW, m.bodyBot);
    m.masterRc = Rect(m.curRc.right + dp(4), m.bodyTop, m.curRc.right + dp(4) + m.stripW, m.bodyBot);
    m.insStartX = m.masterRc.right + dp(8);
    m.fxRc = Rect(m.stripsRight + dp(2), m.bodyTop, rc.right - dp(4), m.bodyBot);
    m.eqH = dp(92);
    m.slotsTop = m.fxRc.top + dp(26);
    m.slotsBot = m.fxRc.bottom - m.eqH - dp(4);
    m.slotH = (m.slotsBot - m.slotsTop) / FX_SLOT_COUNT;
    return m;
}

struct StripMetrics {
    Rect muteRc;
    Rect soloRc;
    int panX;
    int sepX;
    int knobY;
    int knobBandTop;
    int knobBandBot;
    int faderTop;
    int faderBot;
    Rect meterRc;
    Rect grooveRc;
    Rect routeRc;
}

StripMetrics computeStripMetrics(Rect stripRc) nothrow @safe {
    StripMetrics sm;
    sm.muteRc = Rect(stripRc.left + dp(6), stripRc.top + dp(38), stripRc.left + dp(32), stripRc.top + dp(50));
    sm.soloRc = Rect(stripRc.left + dp(32), stripRc.top + dp(38), stripRc.left + dp(58), stripRc.top + dp(50));
    sm.panX = stripRc.left + dp(18);
    sm.sepX = stripRc.left + dp(46);
    sm.knobY = stripRc.top + dp(66);
    sm.knobBandTop = stripRc.top + dp(52);
    sm.knobBandBot = stripRc.top + dp(80);
    sm.faderTop = stripRc.top + dp(84);
    sm.faderBot = stripRc.bottom - dp(24);
    sm.meterRc = Rect(stripRc.right - dp(22), sm.faderTop, stripRc.right - dp(6), sm.faderBot);
    sm.grooveRc = Rect(stripRc.left + dp(17), sm.faderTop + dp(4), stripRc.left + dp(23), sm.faderBot - dp(4));
    sm.routeRc = Rect(stripRc.left + dp(6), stripRc.bottom - dp(20), stripRc.right - dp(6), stripRc.bottom - dp(4));
    return sm;
}

/// Renders a single FL Studio Mixer Insert strip inside `stripRc`.
private void drawMixerStrip(
    DrawBuf buf,
    Rect stripRc,
    const(TrackStripState)* trk,
    int displayIdx,
    bool isMaster,
    bool isSelected,
    const ref FlPalette pal,
    FontRef numFnt,
    FontRef lblFnt,
    FontRef smallFnt
) {
    const uint bg = isSelected ? pal.bgControl : ((displayIdx % 2 == 1) || isMaster ? pal.bgPanelAlt : pal.bgPanel);
    drawBeveledTrackCard(buf, stripRc, bg, pal.bgToolbarWell, 3.0f);
    if (isSelected) {
        buf.fillRect(Rect(stripRc.left, stripRc.top, stripRc.right - dp(3), stripRc.top + dp(2)), pal.accentOrange);
    }

    const uint trkCol = trk ? trk.color : pal.clipPattern;
    buf.fillRect(Rect(stripRc.left + dp(2), stripRc.top + dp(2), stripRc.right - dp(2), stripRc.top + dp(6)), trkCol);

    const string label = trk ? trk.name : (isMaster ? "Master" : format("Ins %d", displayIdx));
    const string numStr = isMaster ? "M" : format("%d", displayIdx);
    drawVCenteredText(buf, numFnt, stripRc.left + dp(5),
                      Rect(stripRc.left, stripRc.top + dp(7), stripRc.right, stripRc.top + dp(21)),
                      toUTF32(numStr), isSelected ? pal.accentOrange : pal.fgMuted);
    drawVCenteredText(buf, lblFnt, stripRc.left + dp(4),
                      Rect(stripRc.left, stripRc.top + dp(21), stripRc.right, stripRc.top + dp(35)),
                      toUTF32(truncateLabel(label, 8)), pal.fgText);

    const bool muted = trk ? trk.muted : false;
    const bool soloed = trk ? trk.soloed : false;
    const sm = computeStripMetrics(stripRc);

    // Mute LED & Solo LED
    drawCard(buf, sm.muteRc, muted ? pal.ledOff : pal.accentGreen, COLOR_TRANSPARENT, 1.5f);
    drawCard(buf, sm.soloRc, soloed ? pal.accentAmber : pal.bgControl, COLOR_TRANSPARENT, 1.5f);
    drawCenteredText(buf, smallFnt, sm.soloRc, "S"d, soloed ? pal.bgLcd : pal.fgMuted);

    // Pan knob (left) & Stereo Separation knob (right)
    const float pan = trk ? trk.pan : 0.0f;
    const float sep = trk ? trk.stereoSep : 0.0f;
    drawRotaryKnob(buf, sm.panX, sm.knobY, dp(9), (pan + 1.0f) * 0.5f,
                   pal.bgLcd, pal.accentCyan, pal.bgControl, pal.borderSubtle, true);
    drawRotaryKnob(buf, sm.sepX, sm.knobY, dp(9), (sep + 1.0f) * 0.5f,
                   pal.bgLcd, pal.accentPurple, pal.bgControl, pal.borderSubtle, true);

    // Fader groove (left) & Stereo Meter (right)
    if (sm.faderBot - sm.faderTop > dp(36)) {
        const float pL = trk ? trk.peakL : 0.0f;
        const float pR = trk ? trk.peakR : 0.0f;
        drawLevelMeter(buf, sm.meterRc, pL, pR, pal, 16);

        drawCard(buf, sm.grooveRc, pal.bgLcd, COLOR_TRANSPARENT, 1.5f);

        const int capH = dp(16);
        const int capHalf = dp(8);
        const int trackTravel = sm.grooveRc.height - capH;
        // Subtle FL Studio dB scale ticks along fader track (unity 0 dB at 0.80 highlighted)
        static immutable float[5] TICK_POS = [0.95f, 0.80f, 0.60f, 0.40f, 0.20f];
        foreach (ti, normT; TICK_POS) {
            const int ty = sm.grooveRc.bottom - capHalf - cast(int)(normT * trackTravel);
            const bool isUnity = (ti == 1);
            const int tSpan = isUnity ? dp(10) : dp(7);
            const uint tickCol = isUnity ? pal.fgMuted : pal.borderSubtle;
            buf.fillRect(Rect(sm.grooveRc.left - tSpan, ty, sm.grooveRc.left - dp(1), ty + dp(1)), tickCol);
            buf.fillRect(Rect(sm.grooveRc.right + dp(1), ty, sm.grooveRc.right + tSpan, ty + dp(1)), tickCol);
        }

        float vol = trk ? trk.volume : 0.8f;
        if (vol < 0.0f) vol = 0.0f;
        if (vol > 1.0f) vol = 1.0f;
        const int capY = sm.grooveRc.bottom - cast(int)(vol * trackTravel) - capH;
        Rect capRc = Rect(stripRc.left + dp(10), capY, stripRc.left + dp(30), capY + capH);
        const uint capBase = 0x008A94A3;
        drawCard(buf, capRc, capBase, pal.borderStrong, 2.0f);
        if (capRc.height >= dp(8)) {
            buf.fillRect(Rect(capRc.left + dp(2), capRc.top + dp(1), capRc.right - dp(2), capRc.top + dp(3)),
                         adjustColorBrightness(capBase, 22));
            buf.fillRect(Rect(capRc.left + dp(2), capRc.middley - dp(1), capRc.right - dp(2), capRc.middley + dp(2)),
                         pal.bgLcd);
            buf.fillRect(Rect(capRc.left + dp(3), capRc.middley, capRc.right - dp(3), capRc.middley + dp(1)),
                         isSelected ? pal.accentOrange : pal.fgText);
            buf.fillRect(Rect(capRc.left + dp(2), capRc.bottom - dp(3), capRc.right - dp(2), capRc.bottom - dp(1)),
                         adjustColorBrightness(capBase, -20));
        }
    }

    // Bottom routing badge + subtle active status dot
    drawCard(buf, sm.routeRc, pal.bgLcd, COLOR_TRANSPARENT, 1.5f);
    drawFilledCircle(buf, sm.routeRc.left + dpf(7.5f), cast(float)sm.routeRc.middley + 0.5f,
                     dpf(2.2f), isMaster ? pal.accentCyan : pal.accentOrange);
    drawCenteredText(buf, smallFnt, sm.routeRc, isMaster ? "OUT"d : "-> M"d, pal.accentOrange);
}

/// Renders the complete FL Studio 2026 Mixer (Current, Master, Insert Strips, 10-Slot FX Rack, 3-Band EQ).
void drawMixerCanvas(
    DrawBuf buf,
    Rect rc,
    const(TransportModel)* model,
    FontRef fnt = FontRef.init,
    int scrollInsert = 0
) {
    const pal = model ? model.palette() : getFlPalette();
    buf.fillRect(rc, pal.bgWindow);
    if (rc.width < dp(260) || rc.height < dp(140)) return;

    const m = computeMixerMetrics(rc, model);
    FontRef hdrFnt = getDawFont(11, fnt, FontWeight.Normal);
    FontRef lblFnt = getDawFont(10, fnt, FontWeight.Normal);
    FontRef smallFnt = getDawFont(10, fnt, FontWeight.Normal);

    // 1. Top Mixer Toolbar
    Rect tbRc = Rect(rc.left, rc.top, rc.right, rc.top + m.toolbarH);
    buf.fillRect(tbRc, pal.bgPanelAlt);
    buf.fillRect(Rect(tbRc.left, tbRc.bottom - dp(1), tbRc.right, tbRc.bottom), pal.borderStrong);

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
    drawVCenteredText(buf, hdrFnt, tbRc.left + dp(10), tbRc,
                      toUTF32("MIXER  |  " ~ selName), pal.accentOrange);

    // Unified flush scroll (< >) + FX Rack toggle strip
    const bool showFx = model ? model.showMixerFxRack : true;
    Rect mxToolStrip = Rect(m.scrLeftRc.left, m.scrLeftRc.top, m.fxToggleRc.right, m.fxToggleRc.bottom);
    drawCard(buf, mxToolStrip, pal.bgControl, COLOR_TRANSPARENT, 1.5f);
    drawDawIcon(buf, Rect(m.scrLeftRc.left + dp(4), m.scrLeftRc.top + dp(2),
                          m.scrLeftRc.right - dp(4), m.scrLeftRc.bottom - dp(2)),
                DawIcon.CHEVRON_LEFT, pal.fgText);
    drawDawIcon(buf, Rect(m.scrRightRc.left + dp(4), m.scrRightRc.top + dp(2),
                          m.scrRightRc.right - dp(4), m.scrRightRc.bottom - dp(2)),
                DawIcon.CHEVRON_RIGHT, pal.fgText);
    if (showFx) {
        drawCard(buf, m.fxToggleRc, pal.accentOrange, COLOR_TRANSPARENT, 1.5f);
    }
    Rect fxIconRc = Rect(m.fxToggleRc.left + dp(4), m.fxToggleRc.top + dp(2),
                         m.fxToggleRc.left + dp(20), m.fxToggleRc.bottom - dp(2));
    drawDawIcon(buf, fxIconRc, DawIcon.FX_RACK, showFx ? pal.bgLcd : pal.fgText);
    drawVCenteredText(buf, smallFnt, fxIconRc.right + dp(3), m.fxToggleRc, "RACK"d,
                      showFx ? pal.bgLcd : pal.fgText);

    // Find selected insert pointer & master pointer
    const(TrackStripState)* masterTrk = null;
    const(TrackStripState)* selTrk = null;
    size_t[] insTrackIndices;
    if (model) {
        foreach (idx, const ref t; model.tracks) {
            if (t.index == 0) masterTrk = &t;
            else insTrackIndices ~= idx;
            if (t.index == selIns) selTrk = &t;
        }
        if (selTrk is null) selTrk = masterTrk;
    }

    // 2. Left Docked "C" (Current) big meter + "M" (Master) strip
    drawCard(buf, m.curRc, pal.bgPanelAlt, pal.borderSubtle, 2.0f);
    drawCenteredText(buf, hdrFnt,
                     Rect(m.curRc.left, m.curRc.top + dp(4), m.curRc.right, m.curRc.top + dp(22)),
                     "C"d, pal.accentOrange);
    if (m.curRc.height > dp(60)) {
        drawLevelMeter(buf, Rect(m.curRc.left + dp(8), m.curRc.top + dp(28), m.curRc.right - dp(8), m.curRc.bottom - dp(12)),
                       selTrk ? selTrk.peakL : 0.0f, selTrk ? selTrk.peakR : 0.0f, pal, 24);
    }

    drawMixerStrip(buf, m.masterRc, masterTrk, 0, true, selIns == 0, pal, hdrFnt, lblFnt, smallFnt);

    // Dock divider
    const int divX = m.masterRc.right + dp(4);
    buf.fillRect(Rect(divX, m.bodyTop, divX + dp(2), m.bodyBot), pal.borderSubtle);

    // 3. Insert Strips (scrolled starting from scrollInsert)
    const int insCount = (insTrackIndices.length > 0) ? cast(int)insTrackIndices.length : 8;
    int startIns = scrollInsert < 0 ? 0 : scrollInsert;
    if (startIns >= insCount) startIns = insCount > 0 ? insCount - 1 : 0;
    int visCol = 0;
    for (int i = startIns; i < insCount; ++i, ++visCol) {
        const int x0 = m.insStartX + visCol * (m.stripW + m.stripGap);
        const int x1 = x0 + m.stripW;
        if (x1 > m.stripsRight - dp(2)) break;
        const(TrackStripState)* tPtr = (model && i < cast(int)insTrackIndices.length)
            ? &model.tracks[insTrackIndices[i]]
            : null;
        const int tIdx = tPtr ? tPtr.index : (i + 1);
        drawMixerStrip(buf, Rect(x0, m.bodyTop, x1, m.bodyBot), tPtr, tIdx, false,
                       selIns == tIdx, pal, hdrFnt, lblFnt, smallFnt);
    }

    // 4. Right Docked 10-Slot FX Rack + 3-Band EQ
    if (m.showFx) {
        Rect fxRc = m.fxRc;
        drawCard(buf, fxRc, pal.bgPanelAlt, pal.borderSubtle, 2.0f);
        drawVCenteredText(buf, hdrFnt, fxRc.left + dp(8),
                          Rect(fxRc.left, fxRc.top + dp(2), fxRc.right, fxRc.top + dp(24)),
                          toUTF32(format("FX SLOTS - %s", truncateLabel(selTrk ? selTrk.name : "Insert", 12))),
                          pal.accentOrange);

        if (m.slotH >= dp(14)) {
            foreach (s; 0 .. FX_SLOT_COUNT) {
                const int sy0 = m.slotsTop + s * m.slotH;
                const int sy1 = sy0 + m.slotH - dp(2);
                Rect sRc = Rect(fxRc.left + dp(6), sy0, fxRc.right - dp(6), sy1);

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
                         selSlot ? pal.accentOrange : COLOR_TRANSPARENT, 1.5f);

                // Slot index & name
                const string sLbl = hasPlug
                    ? format("%d  %s", s + 1, truncateLabel(plug.name, 14))
                    : format("%d  (empty)", s + 1);
                drawVCenteredText(buf, lblFnt, sRc.left + dp(6), sRc, toUTF32(sLbl),
                                  hasPlug ? pal.fgText : pal.fgDim);

                // Mix knob & Enable LED on right of slot
                if (hasPlug) {
                    drawRotaryKnob(buf, sRc.right - dp(28), sRc.middley, dp(6), plug.mix,
                                   pal.bgLcd, pal.accentCyan, pal.bgPanel, pal.borderSubtle);
                    Rect enLed = Rect(sRc.right - dp(14), sRc.top + dp(4), sRc.right - dp(4), sRc.bottom - dp(4));
                    drawCard(buf, enLed, plug.enabled ? pal.accentGreen : pal.ledOff, COLOR_TRANSPARENT, 1.5f);
                }
            }
        }

        // 3-Band Insert EQ Panel at bottom of FX Rack
        if (fxRc.height > dp(150)) {
            Rect eqRc = Rect(fxRc.left + dp(6), fxRc.bottom - m.eqH, fxRc.right - dp(6), fxRc.bottom - dp(6));
            drawCard(buf, eqRc, pal.bgLcd, pal.borderSubtle, 2.0f);
            const float low = selTrk ? selTrk.eqLow : 0.5f;
            const float mid = selTrk ? selTrk.eqMid : 0.5f;
            const float high = selTrk ? selTrk.eqHigh : 0.5f;

            // Smooth cosine-interpolated Parametric 3-Band EQ curve + translucent area fill
            const int cy = eqRc.top + dp(25);
            const float ampPx = dpf(24.0f);
            const float xL = eqRc.left + dpf(10.0f);
            const float xM = cast(float)eqRc.middlex;
            const float xH = eqRc.right - dpf(10.0f);
            const float yL = cy + (0.5f - low) * ampPx;
            const float yM = cy + (0.5f - mid) * ampPx;
            const float yH = cy + (0.5f - high) * ampPx;

            // Subtle +/- dB and center grid lines
            const uint gridCol = (pal.borderSubtle & 0x00FFFFFF) | 0x80000000;
            buf.fillRect(Rect(eqRc.left + dp(6), cy - dp(12), eqRc.right - dp(6), cy - dp(11)), gridCol);
            buf.fillRect(Rect(eqRc.left + dp(6), cy + dp(12), eqRc.right - dp(6), cy + dp(13)), gridCol);
            buf.fillRect(Rect(eqRc.left + dp(6), cy, eqRc.right - dp(6), cy + dp(1)), pal.borderSubtle);
            buf.fillRect(Rect(eqRc.middlex, eqRc.top + dp(6), eqRc.middlex + dp(1), cy + dp(16)), gridCol);

            enum int EQ_STEPS = 24;
            float evalEqY(float tNorm) {
                if (tNorm <= 0.5f) {
                    const float u = tNorm * 2.0f;
                    const float ease = 0.5f - 0.5f * cosf(u * 3.14159265f);
                    return yL + (yM - yL) * ease;
                }
                const float u = (tNorm - 0.5f) * 2.0f;
                const float ease = 0.5f - 0.5f * cosf(u * 3.14159265f);
                return yM + (yH - yM) * ease;
            }

            float prevEx = xL;
            float prevEy = evalEqY(0.0f);
            foreach (s; 1 .. EQ_STEPS + 1) {
                const float tNorm = cast(float)s / cast(float)EQ_STEPS;
                const float ex = xL + tNorm * (xH - xL);
                const float ey = evalEqY(tNorm);
                const uint segCol = (tNorm <= 0.38f)
                    ? pal.accentCyan
                    : (tNorm <= 0.66f ? pal.accentGreen : pal.accentOrange);
                const uint areaCol = (segCol & 0x00FFFFFF) | 0xC6000000;
                const int ix0 = cast(int)prevEx;
                const int ix1 = cast(int)(ex + 0.5f);
                const int iyMid = cast(int)((prevEy + ey) * 0.5f + 0.5f);
                if (ix1 > ix0) {
                    const int fTop = iyMid < cy ? iyMid : cy;
                    const int fBot = iyMid > cy ? iyMid : cy + 1;
                    if (fBot > fTop) {
                        buf.fillRect(Rect(ix0, fTop, ix1, fBot), areaCol);
                    }
                }
                drawThickLine(buf, prevEx, prevEy, ex, ey, dpf(1.8f), segCol);
                prevEx = ex;
                prevEy = ey;
            }
            drawFilledCircle(buf, xL, yL, dpf(2.8f), pal.accentCyan, pal.bgLcd);
            drawFilledCircle(buf, xM, yM, dpf(2.8f), pal.accentGreen, pal.bgLcd);
            drawFilledCircle(buf, xH, yH, dpf(2.8f), pal.accentOrange, pal.bgLcd);

            // Low / Mid / High rotary knobs
            drawRotaryKnob(buf, eqRc.left + dp(34), eqRc.bottom - dp(20), dp(10), low,
                           pal.bgPanelAlt, pal.accentCyan, pal.bgControl, pal.borderSubtle, true);
            drawRotaryKnob(buf, eqRc.middlex, eqRc.bottom - dp(20), dp(10), mid,
                           pal.bgPanelAlt, pal.accentGreen, pal.bgControl, pal.borderSubtle, true);
            drawRotaryKnob(buf, eqRc.right - dp(34), eqRc.bottom - dp(20), dp(10), high,
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
    int scrollInsert = 0;

    void delegate(int trackIdx, float volume) onVolumeChange;
    void delegate(int trackIdx, float pan) onPanChange;
    void delegate(int trackIdx, bool muted) onMuteToggle;
    void delegate(int trackIdx, bool soloed) onSoloToggle;
    void delegate(int trackIdx, int slotIdx, bool enabled) onFxBypass;
    void delegate(int trackIdx, int slotIdx, string uri) onLoadFxSlot;
    void delegate(int trackIdx, int slotIdx) onPromptLoadFx;
    void delegate(int trackIdx, int slotIdx) onRemoveFxSlot;
    void delegate(int trackIdx, int slotIdx) onOpenFxPlugin;

    this(string id, TransportModel* model) {
        super(id);
        model_ = model;
        layoutWidth = FILL_PARENT;
        layoutHeight = FILL_PARENT;
    }

    override void onDraw(DrawBuf buf) {
        if (visibility != Visibility.Visible) return;
        drawMixerCanvas(buf, _pos, model_, safeWidgetFont(this), scrollInsert);
    }

    private size_t[] collectInsertIndices() const @safe {
        size_t[] insTrackIndices;
        if (!model_) return insTrackIndices;
        foreach (idx, const ref t; model_.tracks) {
            if (t.index != 0) insTrackIndices ~= idx;
        }
        return insTrackIndices;
    }

    override bool onMouseEvent(MouseEvent event) {
        if (event.action == MouseAction.FocusOut) {
            return dragMode_ != DragMode.NONE;
        }
        if (!model_) return super.onMouseEvent(event);

        const m = computeMixerMetrics(_pos, model_);
        const auto insTrackIndices = collectInsertIndices();
        const int insCount = (insTrackIndices.length > 0) ? cast(int)insTrackIndices.length : 8;

        if (event.action == MouseAction.Wheel) {
            scrollInsert -= (event.wheelDelta > 0) ? 1 : -1;
            if (scrollInsert < 0) scrollInsert = 0;
            if (scrollInsert >= insCount) scrollInsert = insCount > 0 ? insCount - 1 : 0;
            invalidate();
            return true;
        }

        if (event.action == MouseAction.ButtonDown) {
            activateParentWindow(this);

            // 1. Toolbar scroll (< >) & FX Rack toggle
            if (event.y >= _pos.top && event.y < _pos.top + m.toolbarH) {
                if (m.scrLeftRc.isPointInside(event.x, event.y)) {
                    if (scrollInsert > 0) scrollInsert--;
                    invalidate();
                } else if (m.scrRightRc.isPointInside(event.x, event.y)) {
                    if (scrollInsert + 1 < insCount) scrollInsert++;
                    invalidate();
                } else if (m.fxToggleRc.isPointInside(event.x, event.y)) {
                    model_.showMixerFxRack = !model_.showMixerFxRack;
                    invalidate();
                }
                return true;
            }

            // 2. Right FX Rack & 3-Band EQ clicks
            if (m.showFx && event.x >= m.stripsRight + dp(2)) {
                Rect fxRc = m.fxRc;

                // 3-Band EQ knobs
                if (event.y >= fxRc.bottom - m.eqH) {
                    const size_t tArr = model_.ensureTrackIndex(model_.selectedMixerInsert);
                    auto trk = &model_.tracks[tArr];
                    const int midX = fxRc.middlex;
                    if (event.x < midX - dp(24)) {
                        if (event.button == MouseButton.Right) trk.eqLow = 0.5f;
                        else { dragMode_ = DragMode.EQ_LOW; dragStartVal_ = trk.eqLow; }
                    } else if (event.x > midX + dp(24)) {
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
                if (m.slotH >= dp(14) && event.y >= m.slotsTop && event.y < m.slotsBot) {
                    const int slotIdx = (event.y - m.slotsTop) / m.slotH;
                    if (slotIdx >= 0 && slotIdx < FX_SLOT_COUNT) {
                        model_.selectedFxSlot = slotIdx;
                        const size_t tArr = model_.ensureTrackIndex(model_.selectedMixerInsert);
                        auto trk = &model_.tracks[tArr];
                        ptrdiff_t plugArrIdx = -1;
                        foreach (pi, const ref p; trk.plugins) {
                            if (p.pluginIndex == slotIdx) {
                                plugArrIdx = cast(ptrdiff_t)pi;
                                break;
                            }
                        }
                        if (plugArrIdx >= 0) {
                            auto plug = &trk.plugins[plugArrIdx];
                            if (event.button == MouseButton.Right) {
                                const string rName = plug.name;
                                trk.plugins = trk.plugins[0 .. plugArrIdx] ~ trk.plugins[plugArrIdx + 1 .. $];
                                model_.hintText = format("Removed FX slot %d (%s)", slotIdx + 1, rName);
                                if (onRemoveFxSlot !is null) onRemoveFxSlot(model_.selectedMixerInsert, slotIdx);
                            } else if (event.x >= fxRc.right - dp(22)) {
                                plug.enabled = !plug.enabled;
                                model_.hintText = format("%s: %s", plug.name, plug.enabled ? "Active" : "Bypassed");
                                if (onFxBypass !is null) onFxBypass(model_.selectedMixerInsert, slotIdx, plug.enabled);
                            } else if (event.x >= fxRc.right - dp(44)) {
                                dragMode_ = DragMode.FX_MIX;
                                dragTrackIdx_ = model_.selectedMixerInsert;
                                dragSlotIdx_ = slotIdx;
                                dragStartY_ = event.y;
                                dragStartVal_ = plug.mix;
                            } else {
                                if (onOpenFxPlugin !is null) onOpenFxPlugin(model_.selectedMixerInsert, slotIdx);
                            }
                        } else {
                            if (onPromptLoadFx !is null) {
                                onPromptLoadFx(model_.selectedMixerInsert, slotIdx);
                            } else {
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
                        }
                        invalidate();
                    }
                    return true;
                }
                return true;
            }

            // 3. Master & Insert Strips hit-test
            bool handleStripClick(Rect stripRc, int trackIdx) {
                if (!stripRc.isPointInside(event.x, event.y)) return false;
                model_.selectedMixerInsert = trackIdx;
                const size_t arrIdx = model_.ensureTrackIndex(trackIdx);
                auto trk = &model_.tracks[arrIdx];
                const sm = computeStripMetrics(stripRc);

                if (sm.muteRc.isPointInside(event.x, event.y)) {
                    trk.muted = !trk.muted;
                    if (onMuteToggle !is null) onMuteToggle(trackIdx, trk.muted);
                    invalidate();
                    return true;
                }
                if (sm.soloRc.isPointInside(event.x, event.y)) {
                    trk.soloed = !trk.soloed;
                    if (onSoloToggle !is null) onSoloToggle(trackIdx, trk.soloed);
                    invalidate();
                    return true;
                }

                // Pan / Stereo Separation knobs
                if (event.y >= sm.knobBandTop && event.y <= sm.knobBandBot) {
                    dragTrackIdx_ = trackIdx;
                    dragStartY_ = event.y;
                    if (event.x < stripRc.middlex) {
                        if (event.button == MouseButton.Right) {
                            trk.pan = 0.0f;
                            model_.hintText = format("%s Pan: Center", trk.name);
                            if (onPanChange !is null) onPanChange(trackIdx, 0.0f);
                        } else {
                            dragMode_ = DragMode.PAN;
                            dragStartVal_ = trk.pan;
                        }
                    } else {
                        if (event.button == MouseButton.Right) {
                            trk.stereoSep = 0.0f;
                            model_.hintText = format("%s Stereo Sep: 0%%", trk.name);
                        } else {
                            dragMode_ = DragMode.STEREO_SEP;
                            dragStartVal_ = trk.stereoSep;
                        }
                    }
                    invalidate();
                    return true;
                }

                // Fader groove region
                if (event.y >= sm.faderTop && event.y <= sm.faderBot && event.x < stripRc.right - dp(22)) {
                    if (event.button == MouseButton.Right) {
                        trk.volume = 0.8f;
                        if (onVolumeChange !is null) onVolumeChange(trackIdx, trk.volume);
                    } else {
                        dragMode_ = DragMode.FADER;
                        dragTrackIdx_ = trackIdx;
                        updateFaderFromY(trackIdx, event.y, sm.grooveRc.top, sm.grooveRc.bottom);
                    }
                    invalidate();
                    return true;
                }
                invalidate();
                return true;
            }

            if (handleStripClick(m.masterRc, 0)) return true;

            int startIns = scrollInsert < 0 ? 0 : scrollInsert;
            if (startIns >= insCount) startIns = insCount > 0 ? insCount - 1 : 0;
            int visCol = 0;
            for (int i = startIns; i < insCount; ++i, ++visCol) {
                const int x0 = m.insStartX + visCol * (m.stripW + m.stripGap);
                const int x1 = x0 + m.stripW;
                if (x1 > m.stripsRight - dp(2)) break;
                const int tIdx = (i < cast(int)insTrackIndices.length)
                    ? model_.tracks[insTrackIndices[i]].index
                    : (i + 1);
                if (handleStripClick(Rect(x0, m.bodyTop, x1, m.bodyBot), tIdx)) return true;
            }
            return true;
        }

        if (event.action == MouseAction.Move && dragMode_ != DragMode.NONE) {
            const float delta = cast(float)(dragStartY_ - event.y) / dpf(90.0f);
            const size_t arrIdx = model_.ensureTrackIndex(dragTrackIdx_);
            auto trk = &model_.tracks[arrIdx];
            const sm = computeStripMetrics(m.masterRc);

            final switch (dragMode_) {
                case DragMode.NONE:
                    break;
                case DragMode.FADER:
                    updateFaderFromY(dragTrackIdx_, event.y, sm.grooveRc.top, sm.grooveRc.bottom);
                    break;
                case DragMode.PAN:
                    float p = dragStartVal_ + delta * 2.0f;
                    if (p < -1.0f) p = -1.0f;
                    if (p > 1.0f) p = 1.0f;
                    trk.pan = p;
                    model_.hintText = format("%s Pan: %+d%%", trk.name, cast(int)(p * 100.0f));
                    if (onPanChange !is null) onPanChange(dragTrackIdx_, p);
                    break;
                case DragMode.STEREO_SEP:
                    float sp = dragStartVal_ + delta * 2.0f;
                    if (sp < -1.0f) sp = -1.0f;
                    if (sp > 1.0f) sp = 1.0f;
                    trk.stereoSep = sp;
                    model_.hintText = format("%s Stereo Sep: %+d%%", trk.name, cast(int)(sp * 100.0f));
                    break;
                case DragMode.EQ_LOW:
                    float vL = dragStartVal_ + delta;
                    trk.eqLow = vL < 0.0f ? 0.0f : (vL > 1.0f ? 1.0f : vL);
                    model_.hintText = format("%s EQ Low: %d%%", trk.name, cast(int)(trk.eqLow * 100.0f + 0.5f));
                    break;
                case DragMode.EQ_MID:
                    float vM = dragStartVal_ + delta;
                    trk.eqMid = vM < 0.0f ? 0.0f : (vM > 1.0f ? 1.0f : vM);
                    model_.hintText = format("%s EQ Mid: %d%%", trk.name, cast(int)(trk.eqMid * 100.0f + 0.5f));
                    break;
                case DragMode.EQ_HIGH:
                    float vH = dragStartVal_ + delta;
                    trk.eqHigh = vH < 0.0f ? 0.0f : (vH > 1.0f ? 1.0f : vH);
                    model_.hintText = format("%s EQ High: %d%%", trk.name, cast(int)(trk.eqHigh * 100.0f + 0.5f));
                    break;
                case DragMode.FX_MIX:
                    foreach (ref plug; trk.plugins) {
                        if (plug.pluginIndex == dragSlotIdx_) {
                            float mx = dragStartVal_ + delta;
                            plug.mix = mx < 0.0f ? 0.0f : (mx > 1.0f ? 1.0f : mx);
                            model_.hintText = format("%s Mix: %d%%", plug.name, cast(int)(plug.mix * 100.0f + 0.5f));
                            break;
                        }
                    }
                    break;
            }
            invalidate();
            return true;
        }

        if ((event.action == MouseAction.ButtonUp || event.action == MouseAction.Cancel) &&
            dragMode_ != DragMode.NONE) {
            dragMode_ = DragMode.NONE;
            return true;
        }

        return super.onMouseEvent(event);
    }

    private void updateFaderFromY(int trackIdx, int y, int grooveTop, int grooveBot) {
        const int capH = dp(16);
        const int capHalf = dp(8);
        if (grooveBot - grooveTop <= capH) return;
        const size_t idx = model_.ensureTrackIndex(trackIdx);
        float norm = cast(float)(grooveBot - capHalf - y) / cast(float)(grooveBot - grooveTop - capH);
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

    // Regression 1: Horizontal wheel scroll & toolbar > button allow selecting higher inserts
    import dlangui.widgets.styles : currentTheme, createDefaultTheme;
    import gui_d.theme : destroyWidgetTree;
    if (currentTheme is null) currentTheme = createDefaultTheme();
    auto mx = new MixerCanvasWidget("mx", &model);
    scope (exit) destroyWidgetTree(mx);
    mx.measure(560, 320);
    mx.layout(Rect(0, 0, 560, 320));
    foreach (_; 0 .. 12) {
        auto wh = new MouseEvent(MouseAction.Wheel, MouseButton.None, 0, 200, 150, -1);
        mx.onMouseEvent(wh);
    }
    // Click first visible insert strip after Master (x ~ 160) -> should no longer be Insert 1
    mx.onMouseEvent(new MouseEvent(MouseAction.ButtonDown, MouseButton.Left, MouseFlag.LButton, 160, 150));
    assert(model.selectedMixerInsert > 1,
           "Expected horizontal scroll in Mixer to advance visible inserts past Insert 1");

    // Clicking < toolbar button scrolls back left cleanly
    mx.onMouseEvent(new MouseEvent(MouseAction.ButtonDown, MouseButton.Left, MouseFlag.LButton, 560 - 118, 14));
    assert(mx.scrollInsert < cast(int)model.tracks.length - 1);

    // Regression 2: Pan drag fires onPanChange and MouseAction.Cancel ends drag
    int panTrack = -1;
    mx.onPanChange = (int t, float p) { panTrack = t; };
    mx.onMouseEvent(new MouseEvent(MouseAction.ButtonDown, MouseButton.Left, MouseFlag.LButton, 68, 95));
    mx.onMouseEvent(new MouseEvent(MouseAction.Move, MouseButton.None, MouseFlag.LButton, 68, 70));
    assert(panTrack == 0, "Expected Master Pan drag to trigger onPanChange");
    mx.onMouseEvent(new MouseEvent(MouseAction.Cancel, MouseButton.None, 0, 68, 70));
    panTrack = -1;
    mx.onMouseEvent(new MouseEvent(MouseAction.Move, MouseButton.None, 0, 68, 40));
    assert(panTrack == -1, "Expected MouseAction.Cancel to clear Pan drag");
}
