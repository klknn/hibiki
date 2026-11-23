module gui_d.piano_roll;

import dlangui;
import dlangui.graphics.drawbuf : DrawBuf;
import dlangui.graphics.fonts : FontWeight;
import dlangui.widgets.widget : Visibility, Widget;
import gui_d.icons : DawIcon, drawDawIcon;
import gui_d.theme : adjustColorBrightness, contrastTextColor, dp, dpf,
  drawCard, drawCenteredText, drawFilledCircle, drawPlayheadTriangle,
  drawRightVCenteredText, drawThemeCard,
  drawThickLine, drawVCenteredText, FlPalette, getDawFont, getFlPalette,
  safeWidgetFont, ScopedRetroAntialias, truncateLabel, uiScale;
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

/// DPI-scaled layout metrics shared between `drawPianoRollCanvas` and `PianoRollCanvasWidget`.
struct PianoRollMetrics {
  int toolbarH;
  int rulerH;
  int velH;
  int gridLeft;
  int rulerTop;
  int gridTop;
  int velTop;
  Rect[6] toolBtns;
  Rect tgtRc;
  Rect ghostRc;
  Rect velRc;
}

PianoRollMetrics computePianoRollMetrics(Rect rc, int effKeyWidth, bool velOn) nothrow @safe {
  PianoRollMetrics m;
  m.toolbarH = dp(PR_TOOLBAR_H);
  m.rulerH = dp(PR_RULER_H);
  m.velH = dp(PR_VEL_H);
  m.gridLeft = rc.left + effKeyWidth;
  m.rulerTop = rc.top + m.toolbarH;
  m.gridTop = m.rulerTop + m.rulerH;
  m.velTop = (velOn && rc.bottom - m.gridTop > m.velH + dp(60)) ? (rc.bottom - m
      .velH) : rc.bottom;
  foreach (i; 0 .. 6) {
    const int bx0 = rc.left + dp(6 + cast(int)i * 24);
    m.toolBtns[i] = Rect(bx0, rc.top + dp(4), bx0 + dp(24), m.rulerTop - dp(4));
  }
  m.tgtRc = Rect(rc.left + dp(158), rc.top + dp(4), rc.left + dp(290), m.rulerTop - dp(
      4));
  m.ghostRc = Rect(rc.right - dp(58), rc.top + dp(4), rc.right - dp(32), m
      .rulerTop - dp(4));
  m.velRc = Rect(rc.right - dp(32), rc.top + dp(4), rc.right - dp(6), m.rulerTop - dp(
      4));
  return m;
}

/// Renders the FL Studio 2026 Piano Roll (Toolbar, Keyboard, Scale-Highlighted Grid, Ghost Notes, Velocity Lane).
void drawPianoRollCanvas(DrawBuf buf, Rect rc, const(TransportModel)* model,
    int pitchLow = 48, int pitchHigh = 72, double pixelsPerBeat = 64.0,
    int numBeats = 16,
    int keyWidth = 54, int rowHeight = 14, FontRef fnt = FontRef.init,
    double scrollBeats = 0.0,
    const(size_t)[] selectedNotes = null, Rect marqueeRect = Rect.init) {
  const pal = model ? model.palette() : getFlPalette();
  auto aaGuard = ScopedRetroAntialias(pal.retroBevel);
  buf.fillRect(rc, pal.bgWindow);
  if (rc.width < dp(180) || rc.height < dp(90))
    return;

  const bool ghostOn = model ? model.ghostNotesEnabled : true;
  const bool velOn = model ? model.showVelocityLane : true;
  const m = computePianoRollMetrics(rc, keyWidth, velOn);

  FontRef boldFnt = getDawFont(11, fnt, FontWeight.Normal);
  FontRef smallFnt = getDawFont(10, fnt, FontWeight.Normal);
  FontRef noteFnt = getDawFont(10, fnt, FontWeight.Normal);

  // 1. Top Piano Roll Toolbar
  Rect tbRc = Rect(rc.left, rc.top, rc.right, m.rulerTop);
  buf.fillRect(tbRc, pal.bgPanelAlt);
  buf.fillRect(Rect(tbRc.left, tbRc.bottom - dp(1), tbRc.right, tbRc.bottom), pal
      .borderStrong);

  static immutable DawIcon[6] TOOL_ICONS = [
    DawIcon.TOOL_DRAW, DawIcon.TOOL_PAINT, DawIcon.TOOL_DELETE,
    DawIcon.TOOL_SLICE, DawIcon.TOOL_SELECT, DawIcon.TOOL_ZOOM
  ];
  static immutable EditorTool[6] TOOL_ENUMS = [
    EditorTool.DRAW, EditorTool.PAINT, EditorTool.DELETE, EditorTool.SLICE,
    EditorTool.SELECT, EditorTool.ZOOM
  ];
  Rect prToolsRc = Rect(m.toolBtns[0].left, m.toolBtns[0].top,
      m.toolBtns[5].right, m.toolBtns[5].bottom);
  drawThemeCard(buf, prToolsRc, pal.bgControl, COLOR_TRANSPARENT, pal, 1.5f, false);
  foreach (i, ic; TOOL_ICONS) {
    Rect bRc = m.toolBtns[i];
    const bool active = model && model.pianoRollTool == TOOL_ENUMS[i];
    if (active) {
      drawThemeCard(buf, bRc, pal.accentOrange, COLOR_TRANSPARENT, pal, 1.5f, pal
          .retroBevel);
    }
    drawDawIcon(buf, Rect(bRc.left + dp(2), bRc.top + dp(2), bRc.right - dp(2), bRc
        .bottom - dp(2)), ic, active ? pal.bgLcd : pal.fgText, active ?
        adjustColorBrightness(pal.bgLcd, 28) : pal.fgMuted);
  }

  // Target Channel pill (< Name >)
  string targetName = "MIDI Clip";
  if (model && model.activeMidi.channelIndex >= 0 &&
      model.activeMidi.channelIndex < cast(int)model.channels.length) {
    targetName = model.channels[model.activeMidi.channelIndex].name;
  } else if (model && model.channels.length > 0 && model.activeMidi.channelIndex >= 0) {
    targetName = model.channels[0].name;
  }
  drawThemeCard(buf, m.tgtRc, pal.bgControl, COLOR_TRANSPARENT, pal, 1.5f, false);
  drawDawIcon(buf, Rect(m.tgtRc.left + dp(3), m.tgtRc.top + dp(3),
      m.tgtRc.left + dp(17), m.tgtRc.bottom - dp(3)), DawIcon.CHEVRON_LEFT, pal
      .accentOrange);
  drawCenteredText(buf, boldFnt, m.tgtRc, toUTF32(truncateLabel(targetName,
      12)), pal.accentOrange);
  drawDawIcon(buf, Rect(m.tgtRc.right - dp(17), m.tgtRc.top + dp(3),
      m.tgtRc.right - dp(3), m.tgtRc.bottom - dp(3)), DawIcon.CHEVRON_RIGHT, pal
      .accentOrange);

  // Ghost Notes & Velocity Lane toggles (flush 2-icon strip)
  Rect viewStripRc = Rect(m.ghostRc.left, m.ghostRc.top, m.velRc.right, m.velRc
      .bottom);
  drawThemeCard(buf, viewStripRc, pal.bgControl, COLOR_TRANSPARENT, pal, 1.5f, false);
  if (ghostOn)
    drawThemeCard(buf, m.ghostRc, pal.accentCyan, COLOR_TRANSPARENT, pal, 1.5f, pal
        .retroBevel);
  if (velOn)
    drawThemeCard(buf, m.velRc, pal.accentGreen, COLOR_TRANSPARENT, pal, 1.5f, pal
        .retroBevel);
  drawDawIcon(buf, Rect(m.ghostRc.left + dp(2), m.ghostRc.top + dp(2),
      m.ghostRc.right - dp(2), m.ghostRc.bottom - dp(2)), DawIcon.GHOST_NOTES,
      ghostOn ? pal.bgLcd
      : pal.fgText, ghostOn ? adjustColorBrightness(pal.bgLcd, 28) : pal.fgMuted);
  drawDawIcon(buf, Rect(m.velRc.left + dp(2), m.velRc.top + dp(2),
      m.velRc.right - dp(2), m.velRc.bottom - dp(2)), DawIcon.VELOCITY_LANE,
      velOn ? pal.bgLcd : pal.fgText);

  // 2. Beat Ruler & Piano Grid Geometry
  const int gridLeft = m.gridLeft;
  const int gridRight = rc.right;
  const int rulerTop = m.rulerTop;
  const int gridTop = m.gridTop;
  const int velTop = m.velTop;
  const int bw = dp(1);

  buf.fillRect(Rect(rc.left, rulerTop, gridLeft, gridTop), pal.bgPanel);
  buf.fillRect(Rect(gridLeft, rulerTop, gridRight, gridTop), pal.bgLcd);
  buf.fillRect(Rect(rc.left, gridTop - bw, rc.right, gridTop), pal.borderStrong);

  const int numRows = pitchHigh - pitchLow + 1;
  foreach (r; 0 .. numRows) {
    const int pitch = pitchHigh - r;
    const int y0 = gridTop + r * rowHeight;
    int y1 = y0 + rowHeight;
    if (y0 >= velTop)
      break;
    if (y1 > velTop)
      y1 = velTop;

    const bool black = isBlackKey(pitch);
    const bool inScale = isInAMinorScale(pitch);

    // Piano key on the left (sculpted white keybed + beveled black key overlay)
    const uint whiteBg = 0x00DCE0E6;
    const uint blackBg = 0x00242933;
    buf.fillRect(Rect(rc.left, y0, gridLeft - bw, y1 - bw), whiteBg);
    if (y1 - y0 >= dp(5)) {
      buf.fillRect(Rect(rc.left, y0, gridLeft - bw, y0 + bw), adjustColorBrightness(
          whiteBg, 14));
    }
    buf.fillRect(Rect(gridLeft - dp(4), y0, gridLeft - bw, y1 - bw),
        adjustColorBrightness(whiteBg, -24));
    buf.fillRect(Rect(rc.left, y1 - bw, gridLeft, y1), pal.borderStrong);
    if (black) {
      Rect bkRc = Rect(rc.left, y0 + bw, gridLeft - dp(18), y1 - bw);
      buf.fillRect(bkRc, blackBg);
      if (bkRc.height >= dp(4) && bkRc.width >= dp(6)) {
        buf.fillRect(Rect(bkRc.left, bkRc.top, bkRc.right - dp(1),
            bkRc.top + bw), adjustColorBrightness(blackBg, 18));
        buf.fillRect(Rect(bkRc.right - dp(2), bkRc.top, bkRc.right,
            bkRc.bottom), adjustColorBrightness(blackBg, -18));
      }
    } else if (((pitch % 12) + 12) % 12 == 0 && (y1 - y0) >= dp(10)) {
      drawRightVCenteredText(buf, noteFnt, Rect(rc.left, y0, gridLeft, y1),
          toUTF32(noteName(pitch)), contrastTextColor(whiteBg, 0x001C2026, 0x00EAEFF5),
          dp(5));
    }

    // Scale-highlighted row lane
    const uint laneBg = black ? adjustColorBrightness(pal.bgPanelAlt, -6) : (
        inScale ? pal.bgWindow : pal.bgPanelAlt);
    buf.fillRect(Rect(gridLeft, y0, gridRight, y1 - bw), laneBg);
    buf.fillRect(Rect(gridLeft, y1 - bw, gridRight, y1), pal.borderStrong);
  }

  // 3. Vertical Beat & 16th Subdivisions + Ruler Numbers
  const int totalSubdivs = numBeats * 4;
  foreach (s; 0 .. totalSubdivs + 1) {
    const double beat = cast(double)s * 0.25;
    const int x = gridLeft + cast(int)((beat - scrollBeats) * pixelsPerBeat + 0.5);
    if (x < gridLeft || x >= gridRight)
      continue;
    const bool isBar = (s % 16 == 0);
    const bool isBeat = (s % 4 == 0);
    const uint col = isBar ? pal.borderSubtle : (isBeat ? pal.bgControl : pal
        .borderStrong);
    buf.fillRect(Rect(x, gridTop, x + bw, velTop), col);
    if (isBar && x + dp(14) < gridRight) {
      drawVCenteredText(buf, smallFnt, x + dp(3), Rect(x, rulerTop, gridRight,
          gridTop), toUTF32(format("%d", (s / 16) + 1)), pal.fgText);
    }
  }

  // 4. Ghost Notes (notes + step triggers from other channels in the active pattern)
  const int res = (model && model.activeMidi.resolution > 0) ? model.activeMidi
    .resolution : 480;
  const double resF = cast(double)res;
  const int patLen = model ? model.activePatternLengthSteps() : 16;
  if (ghostOn && model && model.activeMidi.channelIndex >= 0) {
    foreach (ci, const ref ch; model.channels) {
      if (cast(int)ci == model.activeMidi.channelIndex)
        continue;
      foreach (const ref gev; ch.notes) {
        if (gev.cc_number != 0 || gev.velocity == 0)
          continue;
        const int gp = cast(int)gev.pitch;
        if (gp < pitchLow || gp > pitchHigh)
          continue;
        const int row = pitchHigh - gp;
        const int y0 = gridTop + row * rowHeight + dp(2);
        const int y1 = y0 + rowHeight - dp(4);
        if (y1 <= gridTop || y0 >= velTop)
          continue;
        const double startBeat = cast(double)gev.tick / resF;
        const double durBeat = (gev.duration_ticks > 0) ? (cast(double)gev
            .duration_ticks / resF) : 0.25;
        int gx0 = gridLeft + cast(int)((startBeat - scrollBeats) * pixelsPerBeat + 0.5);
        int gx1 = gridLeft + cast(int)((startBeat + durBeat - scrollBeats) * pixelsPerBeat + 0.5);
        if (gx1 > gridLeft && gx0 < gridRight) {
          if (gx0 < gridLeft)
            gx0 = gridLeft;
          if (gx1 > gridRight)
            gx1 = gridRight;
          buf.fillRect(Rect(gx0, y0, gx1, y1), pal.noteGhost);
        }
      }
      if (ch.notes.length == 0) {
        foreach (s; 0 .. patLen) {
          if (s >= cast(int)ch.steps.length || !ch.steps[s])
            continue;
          const int gp = 60 + cast(int)(
              ch.stepPitches[s] * 12.0f + (ch.stepPitches[s] >= 0.0f ? 0.5f : -0.5f));
          if (gp < pitchLow || gp > pitchHigh)
            continue;
          const int row = pitchHigh - gp;
          const int y0 = gridTop + row * rowHeight + dp(2);
          const int y1 = y0 + rowHeight - dp(4);
          if (y1 <= gridTop || y0 >= velTop)
            continue;
          const double startBeat = s * 0.25;
          int gx0 = gridLeft + cast(int)((startBeat - scrollBeats) * pixelsPerBeat + 0.5);
          int gx1 = gridLeft + cast(int)((startBeat + 0.22 - scrollBeats) * pixelsPerBeat + 0.5);
          if (gx1 > gridLeft && gx0 < gridRight) {
            if (gx0 < gridLeft)
              gx0 = gridLeft;
            if (gx1 > gridRight)
              gx1 = gridRight;
            buf.fillRect(Rect(gx0, y0, gx1, y1), pal.noteGhost);
          }
        }
      }
    }
  }

  // 5. Active MIDI Note Bars (or active channel's step-sequencer triggers when notes[] is empty)
  const(MidiEvent)[] events = model ? model.activeMidi.events : null;
  if (events.length == 0 && model && model.activeMidi.channelIndex >= 0 &&
      model.activeMidi.channelIndex < cast(int)model.channels.length) {
    const ref actCh = model.channels[model.activeMidi.channelIndex];
    foreach (s; 0 .. patLen) {
      if (s >= cast(int)actCh.steps.length || !actCh.steps[s])
        continue;
      const int p = 60 + cast(int)(actCh.stepPitches[s] * 12.0f + (
          actCh.stepPitches[s] >= 0.0f ? 0.5f : -0.5f));
      if (p < pitchLow || p > pitchHigh)
        continue;
      const int row = pitchHigh - p;
      const int y0 = gridTop + row * rowHeight + bw;
      const int y1 = y0 + rowHeight - bw * 2;
      if (y1 <= gridTop || y0 >= velTop)
        continue;
      const double startBeat = s * 0.25;
      int x0 = gridLeft + cast(int)((startBeat - scrollBeats) * pixelsPerBeat + 0.5);
      int x1 = gridLeft + cast(int)((startBeat + 0.22 - scrollBeats) * pixelsPerBeat + 0.5);
      if (x1 <= x0 + dp(4))
        x1 = x0 + dp(6);
      if (x1 <= gridLeft || x0 >= gridRight)
        continue;
      if (x0 < gridLeft)
        x0 = gridLeft;
      if (x1 > gridRight)
        x1 = gridRight;
      Rect stepRc = Rect(x0, y0, x1, y1);
      drawThemeCard(buf, stepRc, adjustColorBrightness(pal.noteFill, -18),
          pal.accentCyan, pal, 2.0f, false);
    }
  }
  foreach (idx, const ref ev; events) {
    if (ev.cc_number != 0 || ev.velocity == 0)
      continue;
    const int p = cast(int)ev.pitch;
    if (p < pitchLow || p > pitchHigh)
      continue;

    const int row = pitchHigh - p;
    const int y0 = gridTop + row * rowHeight + bw;
    const int y1 = y0 + rowHeight - bw * 2;
    if (y1 <= gridTop || y0 >= velTop)
      continue;

    const double startBeat = cast(double)ev.tick / resF;
    const double durBeat = (ev.duration_ticks > 0) ? (cast(double)ev
        .duration_ticks / resF) : 0.25;
    int x0 = gridLeft + cast(int)((startBeat - scrollBeats) * pixelsPerBeat + 0.5);
    int x1 = gridLeft + cast(int)((startBeat + durBeat - scrollBeats) * pixelsPerBeat + 0.5);
    if (x1 <= x0 + dp(4))
      x1 = x0 + dp(6);
    if (x1 <= gridLeft || x0 >= gridRight)
      continue;
    if (x0 < gridLeft)
      x0 = gridLeft;
    if (x1 > gridRight)
      x1 = gridRight;

    bool isSelected = false;
    foreach (sIdx; selectedNotes) {
      if (sIdx == idx) {
        isSelected = true;
        break;
      }
    }
    Rect noteRc = Rect(x0, y0, x1, y1);
    drawThemeCard(buf, noteRc, pal.noteFill, isSelected ? pal.accentOrange
        : pal.borderStrong, pal, 2.5f, false);
    // Brighter left attack bar
    const int attackW = dp(4);
    buf.fillRect(Rect(x0 + bw, y0 + bw, x0 + attackW < x1 ? x0 + attackW : x1, y1 - bw),
        isSelected ? pal.accentOrange : pal.accentGreen);
    if ((x1 - x0) >= dp(24) && (y1 - y0) >= dp(11)) {
      drawVCenteredText(buf, noteFnt, x0 + dp(5), noteRc, toUTF32(noteName(p)),
          contrastTextColor(pal.noteFill));
    }
  }
  if (!marqueeRect.empty) {
    buf.fillRect(marqueeRect, (pal.accentOrange & 0x00FFFFFF) | 0xCC000000);
    buf.drawFrame(marqueeRect, pal.accentOrange, Rect(bw, bw, bw, bw), 0);
  }

  // Playhead cap & vertical line in Piano Roll ruler & grid
  const double bpm = (model && model.bpm > 0.0) ? model.bpm : 140.0;
  const double phBeat = (model ? model.playheadPosSec : 0.0) * (bpm / 60.0);
  const int phX = gridLeft + cast(int)((phBeat - scrollBeats) * pixelsPerBeat + 0.5);
  if (phX >= gridLeft && phX < gridRight) {
    drawPlayheadTriangle(buf, phX, rulerTop + dp(2), dp(5), dp(8), pal
        .accentOrange);
    buf.fillRect(Rect(phX, rulerTop + dp(7), phX + dp(2), velTop), pal
        .accentOrange);
  }

  // 6. Bottom Velocity / Event Lane
  if (velOn && velTop < rc.bottom) {
    Rect laneHdr = Rect(rc.left, velTop, gridLeft, rc.bottom);
    Rect laneBg = Rect(gridLeft, velTop, gridRight, rc.bottom);
    buf.fillRect(laneHdr, pal.bgPanel);
    buf.fillRect(laneBg, pal.bgPanelAlt);
    buf.fillRect(Rect(rc.left, velTop, rc.right, velTop + bw), pal.borderSubtle);
    drawVCenteredText(buf, smallFnt, laneHdr.left + dp(6), laneHdr, "Velocity"d, pal
        .fgMuted);

    const int stemBot = rc.bottom - dp(5);
    const int stemTopMax = velTop + dp(8);
    const int stemSpan = stemBot - stemTopMax;
    if (stemSpan > dp(8)) {
      const uint refCol = (pal.borderSubtle & 0x00FFFFFF) | 0xA0000000;
      buf.fillRect(Rect(gridLeft, stemTopMax, gridRight, stemTopMax + dp(1)), refCol);
      buf.fillRect(Rect(gridLeft, (stemTopMax + stemBot) / 2, gridRight,
          (stemTopMax + stemBot) / 2 + dp(1)), refCol);
      foreach (const ref ev; events) {
        if (ev.cc_number != 0 || ev.velocity == 0)
          continue;
        const double startBeat = cast(double)ev.tick / resF;
        const int sx = gridLeft + cast(int)((startBeat - scrollBeats) * pixelsPerBeat + 0.5) + dp(
            4);
        if (sx < gridLeft + dp(2) || sx >= gridRight - dp(2))
          continue;
        const float normVel = cast(float)ev.velocity / 127.0f;
        const int sy = stemBot - cast(int)(normVel * stemSpan);
        drawThickLine(buf, cast(float)sx + 0.5f, cast(float)sy,
            cast(float)sx + 0.5f, cast(float)stemBot, dpf(1.6f), pal.accentGreen);
        drawFilledCircle(buf, cast(float)sx + 0.5f, cast(float)sy + 0.5f,
            dpf(3.2f), pal.noteFill, pal.bgLcd);
      }
    }
  }
}

/// Interactive FL Studio 2026 Piano Roll Widget (Draw/Delete/Move/Resize Notes + Velocity Lane).
class PianoRollCanvasWidget : Widget {
  private TransportModel* model_;

  private enum DragMode {
    NONE,
    MOVE_NOTE,
    RESIZE_NOTE,
    VELOCITY,
    SELECT_MARQUEE
  }

  private DragMode dragMode_ = DragMode.NONE;
  private ptrdiff_t dragNoteIdx_ = -1;
  private long dragOffsetTick_ = 0;
  private bool dirtyDrag_ = false;
  private size_t[] selectedNotes_;
  private int dragAnchorX_ = 0;
  private int dragAnchorY_ = 0;
  private Rect marqueeRc_;

  int pitchLow = 48;
  int pitchHigh = 72;
  double pixelsPerBeat = 64.0;
  double scrollBeats = 0.0;
  int numBeats = 16;

  enum int KEY_WIDTH = 54;
  enum int ROW_HEIGHT = 14;

  void delegate(const(MidiEvent)[] updatedEvents) onNotesChanged;
  void delegate(uint pitch) onPreviewNote;
  void delegate(int channelIdx) onTargetChannelChanged;

  this(string id, TransportModel* model) {
    super(id);
    model_ = model;
    focusable = true;
    layoutWidth = FILL_PARENT;
    layoutHeight = FILL_PARENT;
  }

  private void syncActiveNotesToChannel() {
    if (!model_)
      return;
    const int ch = model_.activeMidi.channelIndex;
    if (ch >= 0 && ch < cast(int)model_.channels.length) {
      model_.channels[ch].notes = model_.activeMidi.events.dup;
    }
  }

  void frameActiveNotes() {
    if (!model_ || model_.activeMidi.events.length == 0)
      return;
    int minP = 127;
    int maxP = 0;
    bool any = false;
    foreach (const ref ev; model_.activeMidi.events) {
      if (ev.cc_number != 0 || ev.velocity == 0)
        continue;
      const int p = cast(int)ev.pitch;
      if (p < minP)
        minP = p;
      if (p > maxP)
        maxP = p;
      any = true;
    }
    if (!any)
      return;
    const int span = pitchHigh - pitchLow;
    if (minP < pitchLow || maxP > pitchHigh) {
      int center = (minP + maxP) / 2;
      int nl = center - span / 2;
      if (nl < 24)
        nl = 24;
      if (nl + span > 108)
        nl = 108 - span;
      pitchLow = nl;
      pitchHigh = nl + span;
    }
  }

  override void onDraw(DrawBuf buf) {
    if (visibility != Visibility.Visible)
      return;
    drawPianoRollCanvas(buf, _pos, model_, pitchLow, pitchHigh,
        pixelsPerBeat * uiScale(),
        numBeats, dp(KEY_WIDTH), dp(ROW_HEIGHT), safeWidgetFont(this),
        scrollBeats, selectedNotes_,
        dragMode_ == DragMode.SELECT_MARQUEE ? marqueeRc_ : Rect.init);
  }

  override bool onMouseEvent(MouseEvent event) {
    if (event.action == MouseAction.FocusOut) {
      return dragMode_ != DragMode.NONE;
    }
    if (!model_)
      return super.onMouseEvent(event);

    const int effKeyWidth = dp(KEY_WIDTH);
    const int effRowHeight = dp(ROW_HEIGHT);
    const double effPxPerBeat = pixelsPerBeat * uiScale();
    const bool velOn = model_.showVelocityLane;
    const m = computePianoRollMetrics(_pos, effKeyWidth, velOn);
    const int gridLeft = m.gridLeft;
    const int rulerTop = m.rulerTop;
    const int gridTop = m.gridTop;
    const int velTop = m.velTop;
    const int res = (model_.activeMidi.resolution > 0) ? model_.activeMidi
      .resolution : 480;
    const double snapB = model_.snapBeats();
    const long stepTicks = (snapB > 0.0) ? cast(long)(snapB * res + 0.5) : 1L;

    if (event.action == MouseAction.Wheel) {
      if ((event.flags & MouseFlag.Control) != 0) {
        pixelsPerBeat *= (event.wheelDelta > 0) ? 1.15 : 0.87;
        if (pixelsPerBeat < 24.0)
          pixelsPerBeat = 24.0;
        if (pixelsPerBeat > 180.0)
          pixelsPerBeat = 180.0;
      } else if ((event.flags & MouseFlag.Shift) != 0) {
        scrollBeats -= event.wheelDelta * 0.5;
        if (scrollBeats < 0.0)
          scrollBeats = 0.0;
        if (scrollBeats > cast(double)(numBeats - 2))
          scrollBeats = cast(double)(numBeats - 2);
      } else {
        const int span = pitchHigh - pitchLow;
        const int delta = (event.wheelDelta > 0) ? 2 : -2;
        int newLow = pitchLow + delta;
        if (newLow < 24)
          newLow = 24;
        if (newLow + span > 108)
          newLow = 108 - span;
        pitchLow = newLow;
        pitchHigh = newLow + span;
      }
      invalidate();
      return true;
    }

    if (event.action == MouseAction.ButtonDown) {
      activateParentWindow(this);

      // 1. Toolbar clicks
      if (event.y >= _pos.top && event.y < rulerTop) {
        static immutable EditorTool[6] TOOL_ENUMS = [
          EditorTool.DRAW, EditorTool.PAINT, EditorTool.DELETE,
          EditorTool.SLICE, EditorTool.SELECT, EditorTool.ZOOM
        ];
        foreach (i, t; TOOL_ENUMS) {
          if (m.toolBtns[i].isPointInside(event.x, event.y)) {
            model_.pianoRollTool = t;
            invalidate();
            return true;
          }
        }
        if (m.tgtRc.isPointInside(event.x, event.y) && model_.channels.length > 0) {
          const int dir = (event.x < m.tgtRc.left + m.tgtRc.width / 4) ? -1 : 1;
          model_.selectPianoRollChannel(model_.activeMidi.channelIndex + dir);
          if (onTargetChannelChanged !is null) {
            onTargetChannelChanged(model_.activeMidi.channelIndex);
          }
          invalidate();
          return true;
        }
        if (m.ghostRc.isPointInside(event.x, event.y)) {
          model_.ghostNotesEnabled = !model_.ghostNotesEnabled;
          invalidate();
          return true;
        }
        if (m.velRc.isPointInside(event.x, event.y)) {
          model_.showVelocityLane = !model_.showVelocityLane;
          invalidate();
          return true;
        }
        return true;
      }

      // 2. Bottom Velocity Lane edit
      if (velOn && event.y >= velTop && event.x >= gridLeft) {
        const double beat = scrollBeats + cast(double)(event.x - gridLeft) / effPxPerBeat;
        const long clickTick = cast(long)(beat * res);
        ptrdiff_t bestIdx = -1;
        long bestDist = stepTicks * 2;
        foreach (i, const ref ev; model_.activeMidi.events) {
          if (ev.cc_number != 0 || ev.velocity == 0)
            continue;
          const long d = (ev.tick > clickTick) ? (ev.tick - clickTick) : (clickTick - ev
              .tick);
          if (d < bestDist) {
            bestDist = d;
            bestIdx = cast(ptrdiff_t)i;
          }
        }
        if (bestIdx >= 0) {
          dragMode_ = DragMode.VELOCITY;
          dragNoteIdx_ = bestIdx;
          dirtyDrag_ = true;
          updateVelocityFromY(bestIdx, event.y, velTop);
          return true;
        }
        return true;
      }

      // 3. Piano Keys / Note Grid
      if (event.y < gridTop || event.y >= velTop)
        return true;
      const int row = (event.y - gridTop) / effRowHeight;
      const int pitch = pitchHigh - row;
      if (pitch < pitchLow || pitch > pitchHigh)
        return true;

      if (event.x < gridLeft) {
        if (onPreviewNote !is null)
          onPreviewNote(cast(uint)pitch);
        return true;
      }

      if (model_.pianoRollTool == EditorTool.ZOOM) {
        pixelsPerBeat *= (event.button == MouseButton.Right) ? 0.8 : 1.25;
        if (pixelsPerBeat < 24.0)
          pixelsPerBeat = 24.0;
        if (pixelsPerBeat > 180.0)
          pixelsPerBeat = 180.0;
        invalidate();
        return true;
      }

      const double beat = scrollBeats + cast(double)(event.x - gridLeft) / effPxPerBeat;
      if (beat < 0.0)
        return true;
      const long clickTick = cast(long)(beat * res);

      // Hit-test existing note
      ptrdiff_t hitIdx = -1;
      foreach (i, const ref ev; model_.activeMidi.events) {
        if (ev.cc_number != 0 || ev.velocity == 0)
          continue;
        const long dur = (ev.duration_ticks > 0) ? ev.duration_ticks : stepTicks;
        if (cast(int)ev.pitch == pitch && clickTick >= ev.tick && clickTick <= ev
            .tick + dur) {
          hitIdx = cast(ptrdiff_t)i;
          break;
        }
      }

      if (hitIdx >= 0) {
        if (event.button == MouseButton.Right || model_.pianoRollTool == EditorTool
            .DELETE) {
          model_.activeMidi.events = model_.activeMidi.events[0 .. hitIdx] ~
            model_.activeMidi.events[hitIdx + 1 .. $];
          syncActiveNotesToChannel();
          if (onNotesChanged !is null)
            onNotesChanged(model_.activeMidi.events);
          invalidate();
          return true;
        }
        if (model_.pianoRollTool == EditorTool.SLICE) {
          const auto ev = model_.activeMidi.events[hitIdx];
          const long dur = (ev.duration_ticks > 0) ? ev.duration_ticks : stepTicks;
          const long splitTick = ((clickTick + stepTicks / 2) / stepTicks) * stepTicks;
          if (splitTick > ev.tick && splitTick < ev.tick + dur) {
            MidiEvent rightEv = ev;
            rightEv.tick = splitTick;
            rightEv.duration_ticks = (ev.tick + dur) - splitTick;
            model_.activeMidi.events[hitIdx].duration_ticks = splitTick - ev.tick;
            model_.activeMidi.events ~= rightEv;
            syncActiveNotesToChannel();
            if (onNotesChanged !is null)
              onNotesChanged(model_.activeMidi.events);
            invalidate();
          }
          return true;
        }
        if (model_.pianoRollTool == EditorTool.SELECT) {
          const size_t hIdx = cast(size_t)hitIdx;
          if ((event.flags & MouseFlag.Shift) != 0) {
            bool existed = false;
            size_t[] kept;
            foreach (sIdx; selectedNotes_) {
              if (sIdx == hIdx)
                existed = true;
              else
                kept ~= sIdx;
            }
            if (!existed)
              kept ~= hIdx;
            selectedNotes_ = kept;
          } else {
            selectedNotes_ = [hIdx];
          }
          const auto ev = model_.activeMidi.events[hitIdx];
          dragMode_ = DragMode.MOVE_NOTE;
          dragOffsetTick_ = clickTick - ev.tick;
          dragNoteIdx_ = hitIdx;
          dirtyDrag_ = false;
          if (onPreviewNote !is null)
            onPreviewNote(cast(uint)pitch);
          invalidate();
          return true;
        }
        const auto ev = model_.activeMidi.events[hitIdx];
        const int x1 = gridLeft + cast(int)((cast(double)(
            ev.tick + ev.duration_ticks) / res - scrollBeats) * effPxPerBeat);
        if (event.x >= x1 - dp(7)) {
          dragMode_ = DragMode.RESIZE_NOTE;
        } else {
          dragMode_ = DragMode.MOVE_NOTE;
          dragOffsetTick_ = clickTick - ev.tick;
        }
        dragNoteIdx_ = hitIdx;
        dirtyDrag_ = false;
        if (onPreviewNote !is null)
          onPreviewNote(cast(uint)pitch);
        return true;
      }

      if (event.button == MouseButton.Left && model_.pianoRollTool == EditorTool
          .SELECT) {
        if ((event.flags & MouseFlag.Shift) == 0) {
          selectedNotes_.length = 0;
        }
        dragMode_ = DragMode.SELECT_MARQUEE;
        dragAnchorX_ = event.x;
        dragAnchorY_ = event.y;
        marqueeRc_ = Rect(event.x, event.y, event.x, event.y);
        invalidate();
        return true;
      }

      // Insert new snapped note
      if (event.button == MouseButton.Left &&
          (model_.pianoRollTool == EditorTool.DRAW ||
            model_.pianoRollTool == EditorTool.PAINT)) {
        const long snappedTick = (clickTick / stepTicks) * stepTicks;
        MidiEvent ne;
        ne.tick = snappedTick;
        ne.pitch = cast(uint)pitch;
        ne.duration_ticks = res / 2;
        ne.velocity = 104;
        model_.activeMidi.events ~= ne;
        dragMode_ = DragMode.RESIZE_NOTE;
        dragNoteIdx_ = cast(ptrdiff_t)model_.activeMidi.events.length - 1;
        dirtyDrag_ = false;
        syncActiveNotesToChannel();
        if (onPreviewNote !is null)
          onPreviewNote(cast(uint)pitch);
        if (onNotesChanged !is null)
          onNotesChanged(model_.activeMidi.events);
        invalidate();
        return true;
      }
      return true;
    }

    if (event.action == MouseAction.Move && dragMode_ == DragMode.SELECT_MARQUEE) {
      const int x0 = (dragAnchorX_ < event.x) ? dragAnchorX_ : event.x;
      const int x1 = (dragAnchorX_ > event.x) ? dragAnchorX_ : event.x;
      const int y0 = (dragAnchorY_ < event.y) ? dragAnchorY_ : event.y;
      const int y1 = (dragAnchorY_ > event.y) ? dragAnchorY_ : event.y;
      marqueeRc_ = Rect(x0, y0, x1, y1);
      selectedNotes_.length = 0;
      foreach (idx, const ref ev; model_.activeMidi.events) {
        if (ev.cc_number != 0 || ev.velocity == 0)
          continue;
        const int p = cast(int)ev.pitch;
        if (p < pitchLow || p > pitchHigh)
          continue;
        const int r = pitchHigh - p;
        const int ny0 = gridTop + r * effRowHeight;
        const int ny1 = ny0 + effRowHeight;
        const double sb = cast(double)ev.tick / cast(double)res;
        const double db = (ev.duration_ticks > 0) ? (cast(double)ev
            .duration_ticks / cast(double)res) : 0.25;
        const int nx0 = gridLeft + cast(int)((sb - scrollBeats) * effPxPerBeat + 0.5);
        const int nx1 = gridLeft + cast(int)((sb + db - scrollBeats) * effPxPerBeat + 0.5);
        if (nx1 >= x0 && nx0 <= x1 && ny1 >= y0 && ny0 <= y1) {
          selectedNotes_ ~= idx;
        }
      }
      invalidate();
      return true;
    }

    if (event.action == MouseAction.Move && dragMode_ != DragMode.NONE &&
        dragNoteIdx_ >= 0 &&
        dragNoteIdx_ < cast(ptrdiff_t)model_.activeMidi.events.length) {
      if (dragMode_ == DragMode.VELOCITY) {
        dirtyDrag_ = true;
        updateVelocityFromY(dragNoteIdx_, event.y, velTop);
        return true;
      }
      const double beat = scrollBeats + cast(double)(event.x - gridLeft) / effPxPerBeat;
      const long curTick = cast(long)(beat * res);
      auto ev = &model_.activeMidi.events[dragNoteIdx_];

      if (dragMode_ == DragMode.MOVE_NOTE) {
        const int row = (event.y - gridTop) / effRowHeight;
        int newPitch = pitchHigh - row;
        if (newPitch < pitchLow)
          newPitch = pitchLow;
        if (newPitch > pitchHigh)
          newPitch = pitchHigh;
        long newTick = ((curTick - dragOffsetTick_) / stepTicks) * stepTicks;
        if (newTick < 0)
          newTick = 0;
        ev.pitch = cast(uint)newPitch;
        ev.tick = newTick;
        dirtyDrag_ = true;
        invalidate();
      } else if (dragMode_ == DragMode.RESIZE_NOTE) {
        long newDur = ((curTick - ev.tick + stepTicks - 1) / stepTicks) * stepTicks;
        if (newDur < stepTicks)
          newDur = stepTicks;
        ev.duration_ticks = newDur;
        dirtyDrag_ = true;
        invalidate();
      }
      return true;
    }

    if ((event.action == MouseAction.ButtonUp ||
        event.action == MouseAction.Cancel) && dragMode_ != DragMode.NONE) {
      if (dirtyDrag_) {
        syncActiveNotesToChannel();
        if (onNotesChanged !is null)
          onNotesChanged(model_.activeMidi.events);
      }
      dragMode_ = DragMode.NONE;
      dragNoteIdx_ = -1;
      dirtyDrag_ = false;
      marqueeRc_ = Rect.init;
      invalidate();
      return true;
    }

    return super.onMouseEvent(event);
  }

  override bool onKeyEvent(KeyEvent event) {
    if (model_ && event.action == KeyAction.KeyDown &&
        (event.keyCode == KeyCode.DEL ||
          event.keyCode == KeyCode.BACK) && selectedNotes_.length > 0) {
      MidiEvent[] kept;
      foreach (idx, const ref ev; model_.activeMidi.events) {
        bool sel = false;
        foreach (sIdx; selectedNotes_) {
          if (sIdx == idx) {
            sel = true;
            break;
          }
        }
        if (!sel)
          kept ~= ev;
      }
      model_.activeMidi.events = kept;
      selectedNotes_.length = 0;
      syncActiveNotesToChannel();
      if (onNotesChanged !is null)
        onNotesChanged(model_.activeMidi.events);
      invalidate();
      return true;
    }
    return super.onKeyEvent(event);
  }

  private void updateVelocityFromY(ptrdiff_t noteIdx, int my, int velTop) {
    const int stemBot = _pos.bottom - dp(5);
    const int stemTopMax = velTop + dp(8);
    if (stemBot <= stemTopMax)
      return;
    float norm = cast(float)(stemBot - my) / cast(float)(stemBot - stemTopMax);
    if (norm < 0.05f)
      norm = 0.05f;
    if (norm > 1.0f)
      norm = 1.0f;
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

  // Regression 1: Piano Roll must draw a playhead line when playheadPosSec > 0
  model.bpm = 120.0;
  model.playheadPosSec = 1.0; // beat 2 -> x = 54 + 2*64 = 182
  drawPianoRollCanvas(buf, Rect(0, 0, 360, 260), &model, 56, 64, 64.0, 4, 54, 14);
  assert(buf.scanLine(PR_TOOLBAR_H + 8)[182] == pal.accentOrange,
      "Expected Piano Roll playhead line at beat 2");

  // Regression 2: Switching target channels in Piano Roll preserves per-channel notes
  import dlangui.widgets.styles : currentTheme, createDefaultTheme;
  import gui_d.theme : destroyWidgetTree;

  if (currentTheme is null)
    currentTheme = createDefaultTheme();
  auto pr = new PianoRollCanvasWidget("pr", &model);
  scope (exit)
    destroyWidgetTree(pr);
  pr.measure(400, 300);
  pr.layout(Rect(0, 0, 400, 300));
  model.selectPattern(1); // Pattern 2 (Acid Bass) -> channel 3 with default Acid Bass notes
  model.channels[3].notes = [ev];
  model.activeMidi.events = [ev];
  // Click > side of target channel pill (158..290 -> x=270) -> switch to channel 4 (E-Piano)
  pr.onMouseEvent(new MouseEvent(MouseAction.ButtonDown, MouseButton.Left,
      MouseFlag.LButton, 270, 14));
  assert(model.activeMidi.channelIndex == 4);
  assert(model.activeMidi.events.length == 0, "Expected E-Piano channel to start with its own empty notes, not Acid Bass's notes");
  // Click < side of target channel pill (x=165) -> back to channel 3 retaining its note
  pr.onMouseEvent(new MouseEvent(MouseAction.ButtonDown, MouseButton.Left,
      MouseFlag.LButton, 165, 14));
  assert(model.activeMidi.channelIndex == 3);
  assert(model.activeMidi.events.length == 1 &&
      model.activeMidi.events[0].pitch == 60,
      "Expected Acid Bass notes to be restored when switching back");

  // Regression 3: SLICE splits note at beat boundary and ZOOM changes scale
  model.pianoRollTool = EditorTool.SLICE;
  const int noteMidX = 54 + 32; // beat 0.5 (tick 240)
  const int noteY = PR_TOOLBAR_H + PR_RULER_H + (72 - 60) * 14 + 6;
  pr.onMouseEvent(new MouseEvent(MouseAction.ButtonDown, MouseButton.Left,
      MouseFlag.LButton, cast(short)noteMidX, cast(short)noteY));
  assert(model.activeMidi.events.length == 2, "Expected SLICE in Piano Roll to split note");

  const double prevBeatPx = pr.pixelsPerBeat;
  model.pianoRollTool = EditorTool.ZOOM;
  pr.onMouseEvent(new MouseEvent(MouseAction.ButtonDown, MouseButton.Left,
      MouseFlag.LButton, cast(short)noteMidX, cast(short)noteY));
  assert(pr.pixelsPerBeat > prevBeatPx, "Expected ZOOM in Piano Roll to zoom in");

  // Regression 4: stepTicks follows model.snapMode dynamically (e.g. BEAT = 480 ticks vs STEP = 120)
  import gui_d.transport_model : SnapMode;

  model.snapMode = SnapMode.BEAT;
  model.pianoRollTool = EditorTool.DRAW;
  model.activeMidi.events = [];
  // Click at beat 1.75 (54 + 1.75 * 64 = 166) -> BEAT snap rounds down to beat 1 (tick 480)
  pr.pixelsPerBeat = 64.0;
  const int beat175X = 54 + cast(int)(1.75 * 64.0);
  pr.onMouseEvent(new MouseEvent(MouseAction.ButtonDown, MouseButton.Left,
      MouseFlag.LButton, cast(short)beat175X, cast(short)noteY));
  assert(model.activeMidi.events.length == 1 && model.activeMidi.events[0].tick == 480,
      "Expected BEAT snap mode to quantize drawn note to tick 480 instead of hardcoded res/4");
  model.snapMode = SnapMode.STEP;

  // Regression 5: EditorTool.SELECT selects note and KeyCode.DEL removes it
  model.pianoRollTool = EditorTool.SELECT;
  const int drawnNoteX = 54 + 64 + 8;
  pr.onMouseEvent(new MouseEvent(MouseAction.ButtonDown, MouseButton.Left,
      MouseFlag.LButton, cast(short)drawnNoteX, cast(short)noteY));
  pr.onMouseEvent(new MouseEvent(MouseAction.ButtonUp, MouseButton.Left, 0,
      cast(short)drawnNoteX, cast(short)noteY));
  assert(pr.selectedNotes_.length == 1 && pr.selectedNotes_[0] == 0);
  int cbNotes = -1;
  pr.onNotesChanged = (const(MidiEvent)[] evs) { cbNotes = cast(int)evs.length; };
  pr.onKeyEvent(new KeyEvent(KeyAction.KeyDown, KeyCode.DEL, 0));
  assert(model.activeMidi.events.length == 0 && cbNotes == 0, "Expected DEL key in Piano Roll SELECT mode to remove selected note and fire onNotesChanged");
}
