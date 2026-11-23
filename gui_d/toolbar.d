module gui_d.toolbar;

import core.stdc.math : sinf;
import dlangui;
import dlangui.graphics.drawbuf : DrawBuf;
import dlangui.graphics.fonts : FontWeight;
import dlangui.widgets.widget : Visibility, Widget;
import gui_d.icons : DawIcon, drawDawIcon;
import gui_d.theme : adjustColorBrightness, dp, dpf, drawCard, drawCenteredText,
  drawRotaryKnob, drawThemeCard, drawThickLine,
  drawVCenteredText, FlPalette,
  formatBarBeat, formatTimecode, getDawFont, getFlPalette, safeWidgetFont,
  ScopedRetroAntialias, truncateLabel, uiScale;
import gui_d.transport_model : PlaybackMode, SnapMode, TimeDisplayMode, TransportModel;
import std.format : format;
import std.utf : toUTF32;

/// DPI-scaled geometry shared identically between `drawToolbarCanvas` and `FlToolbarWidget.onMouseEvent`.
struct ToolbarLayout {
  Rect masterPanel;
  int volKnobX;
  int pitchKnobX;
  Rect transPanel;
  Rect patSongRc;
  Rect playRc;
  Rect stopRc;
  Rect recRc;
  Rect loopRc;
  Rect bpmRc;
  Rect timeRc;
  Rect recOptsPanel;
  Rect[4] recToggles;
  Rect snapPanel;
  Rect snapRc;
  Rect undoRc;
  Rect redoRc;
  Rect patPanel;
  Rect pPrev;
  Rect pLbl;
  Rect pNext;
  Rect pAdd;
  bool hasWinPanel;
  Rect winPanel;
  Rect[6] winBtns;
  bool hasScopePanel;
  Rect scopePanel;
}

ToolbarLayout computeToolbarLayout(Rect rc) nothrow @safe {
  ToolbarLayout tl;
  const int top = rc.top + dp(5);
  const int bot = rc.bottom - dp(5);
  const int gap = dp(6);
  int x = rc.left + gap;

  // 1. Master Volume & Pitch knobs (no enclosing box)
  tl.masterPanel = Rect(x, top, x + dp(66), bot);
  tl.volKnobX = x + dp(17);
  tl.pitchKnobX = x + dp(49);
  x = tl.masterPanel.right + gap;

  // 2. Unified Transport + Tempo/Time cluster (zero internal dividers between Play/Stop/Rec/Loop)
  tl.transPanel = Rect(x, top, x + dp(354), bot);
  tl.patSongRc = Rect(x, top, x + dp(60), bot);
  tl.playRc = Rect(x + dp(60), top, x + dp(90), bot);
  tl.stopRc = Rect(x + dp(90), top, x + dp(118), bot);
  tl.recRc = Rect(x + dp(118), top, x + dp(146), bot);
  tl.loopRc = Rect(x + dp(146), top, x + dp(176), bot);
  tl.bpmRc = Rect(x + dp(180), top + dp(1), x + dp(248), bot - dp(1));
  tl.timeRc = Rect(x + dp(248), top + dp(1), x + dp(350), bot - dp(1));
  x = tl.transPanel.right + gap;

  // 3. Recording Helpers strip (Metronome, Countdown, Overdub, Typing Keyboard — flush icons)
  tl.recOptsPanel = Rect(x, top, x + dp(112), bot);
  foreach (i; 0 .. 4) {
    const int bx = x + cast(int)i * dp(28);
    tl.recToggles[i] = Rect(bx, top, bx + dp(28), bot);
  }
  x = tl.recOptsPanel.right + gap;

  // 4. Snap & History strip (Magnet + Snap label, Undo, Redo — flush)
  tl.snapPanel = Rect(x, top, x + dp(132), bot);
  tl.snapRc = Rect(x, top, x + dp(76), bot);
  tl.undoRc = Rect(x + dp(76), top, x + dp(104), bot);
  tl.redoRc = Rect(x + dp(104), top, x + dp(132), bot);
  x = tl.snapPanel.right + gap;

  // 5. Pattern Selector strip (< Pattern 1 > + — flush)
  tl.patPanel = Rect(x, top, x + dp(184), bot);
  tl.pPrev = Rect(x, top, x + dp(22), bot);
  tl.pLbl = Rect(x + dp(22), top + dp(1), x + dp(138), bot - dp(1));
  tl.pNext = Rect(x + dp(138), top, x + dp(160), bot);
  tl.pAdd = Rect(x + dp(160), top, x + dp(184), bot);
  x = tl.patPanel.right + gap;

  // 6. Workspace Window Switchers strip (Playlist, Rack, PianoRoll, Browser, Mixer, Plugin — flush icons)
  if (x + dp(168) <= rc.right - gap) {
    tl.hasWinPanel = true;
    tl.winPanel = Rect(x, top, x + dp(168), bot);
    foreach (i; 0 .. 6) {
      const int wx = x + cast(int)i * dp(28);
      tl.winBtns[i] = Rect(wx, top, wx + dp(28), bot);
    }
    x = tl.winPanel.right + gap;
  }

  // 7. Live Oscilloscope & CPU monitor
  if (rc.right - x > dp(80)) {
    tl.hasScopePanel = true;
    tl.scopePanel = Rect(x, top, rc.right - gap, bot);
  }
  return tl;
}

/// Returns a short display label for `SnapMode`.
string snapLabel(SnapMode s) pure nothrow @safe {
  final switch (s) {
  case SnapMode.LINE:
    return "Line";
  case SnapMode.CELL:
    return "Cell";
  case SnapMode.STEP_1_4:
    return "1/4 Step";
  case SnapMode.STEP_1_2:
    return "1/2 Step";
  case SnapMode.STEP:
    return "Step";
  case SnapMode.BEAT_1_4:
    return "1/4 Beat";
  case SnapMode.BEAT_1_2:
    return "1/2 Beat";
  case SnapMode.BEAT:
    return "Beat";
  case SnapMode.BAR:
    return "Bar";
  case SnapMode.NONE:
    return "None";
  }
}

/// Renders the FL Studio 2026 modular Transport / Pattern / Window / CPU Toolbar.
void drawToolbarCanvas(DrawBuf buf, Rect rc, const(TransportModel)* model,
    bool visPlaylist, bool visChannelRack,
    bool visPianoRoll, bool visBrowser, bool visMixer, bool visPlugin, FontRef fnt = FontRef
      .init) {
  const pal = model ? model.palette() : getFlPalette();
  auto aaGuard = ScopedRetroAntialias(pal.retroBevel);
  buf.fillRect(rc, pal.bgToolbar);
  buf.fillRect(Rect(rc.left, rc.bottom - dp(1), rc.right, rc.bottom), pal
      .borderStrong);
  if (!model || rc.width < dp(600) || rc.height < dp(42))
    return;

  auto tl = computeToolbarLayout(rc);
  FontRef uiFnt = getDawFont(11, fnt, FontWeight.Normal);
  FontRef patFnt = getDawFont(11, fnt, FontWeight.Normal);
  FontRef lcdFnt = getDawFont(12, fnt, FontWeight.Normal);

  // 1. Master Volume & Master Pitch knobs directly on toolbar surface
  drawRotaryKnob(buf, tl.volKnobX, tl.masterPanel.middley, dp(12),
      model.masterVolume, pal.bgLcd, pal.accentOrange, pal.bgControl, pal
        .borderSubtle);
  drawRotaryKnob(buf, tl.pitchKnobX, tl.masterPanel.middley, dp(11),
      (model.masterPitch + 1.0f) * 0.5f, pal.bgLcd, pal.accentCyan,
      pal.bgControl, pal.borderSubtle, true);

  // 2. Unified Transport & Time cluster (single container, zero borders between buttons)
  drawThemeCard(buf, tl.transPanel, pal.bgControl, COLOR_TRANSPARENT, pal, 2.0f, false);

  const bool isPat = model.playbackMode == PlaybackMode.PAT;
  drawThemeCard(buf, tl.patSongRc, isPat ? pal.accentOrange : pal.accentGreen,
      COLOR_TRANSPARENT, pal, 2.0f, false);
  Rect psIconRc = Rect(tl.patSongRc.left + dp(3), tl.patSongRc.top + dp(9),
      tl.patSongRc.left + dp(21), tl.patSongRc.bottom - dp(9));
  drawDawIcon(buf, psIconRc, isPat ? DawIcon.PAT_MODE : DawIcon.SONG_MODE, pal
      .bgLcd);
  drawVCenteredText(buf, uiFnt, psIconRc.right + dp(2), tl.patSongRc, isPat ?
      "PAT"d : "SONG"d, pal.bgLcd);

  if (model.isPlaying) {
    drawThemeCard(buf, tl.playRc, pal.accentGreen, COLOR_TRANSPARENT, pal, 1.5f, pal
        .retroBevel);
  }
  if (model.isRecording) {
    drawThemeCard(buf, tl.recRc, pal.accentRed, COLOR_TRANSPARENT, pal, 1.5f, pal
        .retroBevel);
  }
  if (model.loopEnabled) {
    drawThemeCard(buf, tl.loopRc, pal.accentAmber, COLOR_TRANSPARENT, pal, 1.5f, pal
        .retroBevel);
  }

  drawDawIcon(buf, Rect(tl.playRc.left + dp(5), tl.playRc.top + dp(9),
      tl.playRc.right - dp(5), tl.playRc.bottom - dp(9)), DawIcon.PLAY,
      model.isPlaying ? pal.bgLcd : pal.accentGreen);
  drawDawIcon(buf, Rect(tl.stopRc.left + dp(4), tl.stopRc.top + dp(9),
      tl.stopRc.right - dp(4), tl.stopRc.bottom - dp(9)), DawIcon.STOP, pal
      .fgText);
  drawDawIcon(buf, Rect(tl.recRc.left + dp(4), tl.recRc.top + dp(9),
      tl.recRc.right - dp(4), tl.recRc.bottom - dp(9)), DawIcon.RECORD,
      model.isRecording ? pal.bgLcd : pal.accentRed);
  drawDawIcon(buf, Rect(tl.loopRc.left + dp(5), tl.loopRc.top + dp(9),
      tl.loopRc.right - dp(5), tl.loopRc.bottom - dp(9)), DawIcon.LOOP,
      model.loopEnabled ? pal.bgLcd : pal.fgMuted);

  // Unified BPM + Time LCD well inside Transport cluster (cohesive tone with toolbar)
  Rect lcdGroup = Rect(tl.bpmRc.left, tl.bpmRc.top, tl.timeRc.right, tl.timeRc
      .bottom);
  drawThemeCard(buf, lcdGroup, pal.bgLcd, COLOR_TRANSPARENT, pal, 1.5f, true);
  if (!pal.retroBevel) {
    buf.fillRect(Rect(lcdGroup.left + dp(1), lcdGroup.top,
        lcdGroup.right - dp(1), lcdGroup.top + dp(1)), adjustColorBrightness(pal
        .bgLcd, -6));
  }
  buf.fillRect(Rect(tl.bpmRc.right, lcdGroup.top + dp(6), tl.bpmRc.right + dp(1),
      lcdGroup.bottom - dp(6)), pal.retroBevel ? pal.borderSubtle
      : adjustColorBrightness(pal.bgLcd, 14));
  drawCenteredText(buf, lcdFnt, tl.bpmRc, toUTF32(format("%.1f", model.bpm)), pal
      .fgText);
  const string timeStr = (model.timeDisplayMode == TimeDisplayMode.TIME_MS) ? formatTimecode(
      model.playheadPosSec) : formatBarBeat(model.playheadPosSec, model.bpm);
  drawCenteredText(buf, lcdFnt, tl.timeRc, toUTF32(timeStr), pal.fgText);

  // 3. Recording Helpers strip (single seamless strip of pictorial icons)
  drawThemeCard(buf, tl.recOptsPanel, pal.bgControl, COLOR_TRANSPARENT, pal, 2.0f,
      false);
  struct IconToggleSpec {
    DawIcon icon;
    bool on;
    uint col;
  }

  IconToggleSpec[4] specs = [
    IconToggleSpec(DawIcon.METRONOME, model.metronomeEnabled,
        pal.accentOrange),
    IconToggleSpec(DawIcon.COUNTDOWN, model.countdownEnabled,
        pal.accentAmber),
    IconToggleSpec(DawIcon.OVERDUB, model.overdubEnabled, pal.accentGreen),
    IconToggleSpec(DawIcon.TYPING_KB, model.typingKeyboardEnabled, pal.accentCyan),
  ];
  foreach (i, sp; specs) {
    Rect tRc = tl.recToggles[i];
    if (sp.on) {
      drawThemeCard(buf, tRc, sp.col, COLOR_TRANSPARENT, pal, 1.5f, pal
          .retroBevel);
    }
    drawDawIcon(buf, Rect(tRc.left + dp(4), tRc.top + dp(9), tRc.right - dp(4), tRc
        .bottom - dp(9)), sp.icon, sp.on ? pal.bgLcd : pal.fgMuted, sp.on ?
        adjustColorBrightness(pal.bgLcd, 28) : pal.fgDim);
  }

  // 4. Snap & History strip (Magnet + Snap label, Undo, Redo — flush)
  drawThemeCard(buf, tl.snapPanel, pal.bgControl, COLOR_TRANSPARENT, pal, 2.0f, false);
  Rect snapIconRc = Rect(tl.snapRc.left + dp(4), tl.snapRc.top + dp(10),
      tl.snapRc.left + dp(22), tl.snapRc.bottom - dp(10));
  drawDawIcon(buf, snapIconRc, DawIcon.SNAP_MAGNET, pal.accentCyan, pal.fgText);
  drawVCenteredText(buf, patFnt, snapIconRc.right + dp(3), tl.snapRc,
      toUTF32(snapLabel(model.snapMode)), pal.fgText);
  drawDawIcon(buf, Rect(tl.undoRc.left + dp(4), tl.undoRc.top + dp(9),
      tl.undoRc.right - dp(4), tl.undoRc.bottom - dp(9)), DawIcon.UNDO, pal
      .fgText);
  drawDawIcon(buf, Rect(tl.redoRc.left + dp(4), tl.redoRc.top + dp(9),
      tl.redoRc.right - dp(4), tl.redoRc.bottom - dp(9)), DawIcon.REDO, pal
      .fgText);

  // 5. Pattern Selector strip (< Pattern 1 > + — flush)
  drawThemeCard(buf, tl.patPanel, pal.bgControl, COLOR_TRANSPARENT, pal, 2.0f, false);
  drawThemeCard(buf, tl.pLbl, pal.bgLcd, COLOR_TRANSPARENT, pal, 1.5f, true);
  string patName = "Pattern 1";
  if (model.selectedPatternIndex >= 0 && model.selectedPatternIndex < cast(int)model
      .patterns.length) {
    patName = model.patterns[model.selectedPatternIndex].name;
  }
  drawDawIcon(buf, Rect(tl.pPrev.left + dp(3), tl.pPrev.top + dp(10),
      tl.pPrev.right - dp(3), tl.pPrev.bottom - dp(10)), DawIcon.CHEVRON_LEFT, pal
      .fgText);
  drawVCenteredText(buf, patFnt, tl.pLbl.left + dp(6), tl.pLbl,
      toUTF32(truncateLabel(patName, 14)), pal.fgText);
  drawDawIcon(buf, Rect(tl.pNext.left + dp(3), tl.pNext.top + dp(10),
      tl.pNext.right - dp(3), tl.pNext.bottom - dp(10)), DawIcon.CHEVRON_RIGHT, pal
      .fgText);
  drawDawIcon(buf, Rect(tl.pAdd.left + dp(4), tl.pAdd.top + dp(10),
      tl.pAdd.right - dp(4), tl.pAdd.bottom - dp(10)), DawIcon.PLUS, pal
      .accentGreen);

  // 6. Workspace Window Switchers strip (Playlist, Channel Rack, Piano Roll, Browser, Mixer, Plugin)
  if (tl.hasWinPanel) {
    drawThemeCard(buf, tl.winPanel, pal.bgControl, COLOR_TRANSPARENT, pal, 2.0f, false);
    struct WinIconSpec {
      DawIcon icon;
      bool on;
    }

    WinIconSpec[6] wspecs = [
      WinIconSpec(DawIcon.WIN_PLAYLIST, visPlaylist),
      WinIconSpec(DawIcon.WIN_CHANNEL_RACK, visChannelRack),
      WinIconSpec(DawIcon.WIN_PIANO_ROLL, visPianoRoll),
      WinIconSpec(DawIcon.WIN_BROWSER, visBrowser),
      WinIconSpec(DawIcon.WIN_MIXER, visMixer),
      WinIconSpec(DawIcon.WIN_PLUGIN, visPlugin),
    ];
    foreach (i, ws; wspecs) {
      Rect wRc = tl.winBtns[i];
      if (ws.on) {
        drawThemeCard(buf, wRc, pal.bgControlHover, COLOR_TRANSPARENT, pal, 0.0f, pal
            .retroBevel);
        buf.fillRect(Rect(wRc.left + dp(3), wRc.bottom - dp(3),
            wRc.right - dp(3), wRc.bottom - dp(1)), pal.accentOrange);
      }
      drawDawIcon(buf, Rect(wRc.left + dp(4), wRc.top + dp(9), wRc.right - dp(4), wRc
          .bottom - dp(9)), ws.icon, ws.on ? pal.fgText : pal.fgMuted, ws.on ?
          pal.accentOrange : pal.fgDim);
    }
  }

  // 7. Live Oscilloscope & CPU monitor (cohesive slate surface & smooth AA trace)
  if (tl.hasScopePanel) {
    Rect scopePanel = tl.scopePanel;
    drawThemeCard(buf, scopePanel, pal.bgLcd, COLOR_TRANSPARENT, pal, 1.5f, true);
    if (!pal.retroBevel) {
      buf.fillRect(Rect(scopePanel.left + dp(1), scopePanel.top,
          scopePanel.right - dp(1), scopePanel.top + dp(1)), adjustColorBrightness(
          pal.bgLcd, -6));
    }
    const int sw = scopePanel.width - dp(12);
    const int midY = scopePanel.middley + dp(3);
    buf.fillRect(Rect(scopePanel.left + dp(6), midY, scopePanel.right - dp(6), midY + dp(
        1)), (pal.borderSubtle & 0x00FFFFFF) | 0xA8000000);
    if (sw > dp(12)) {
      float prevX = scopePanel.left + dp(6);
      float prevY = midY;
      const bool activeAudio = model.isPlaying || model.oscilloscopeSamples
        .length > 0;
      const uint scopeTraceCol = activeAudio ? pal.fgText : pal.fgMuted;
      const uint scopeFillCol = (scopeTraceCol & 0x00FFFFFF) | 0xD2000000;
      const size_t pts = model.oscilloscopeSamples.length > 0 ? model
        .oscilloscopeSamples.length : 56;
      const float amp = dpf(14.0f);
      foreach (i; 0 .. pts) {
        const float t = cast(float)(i + 1) / cast(float)pts;
        const float sx = scopePanel.left + dp(6) + t * sw;
        float sample = 0.0f;
        if (model.oscilloscopeSamples.length > 0) {
          sample = model.oscilloscopeSamples[i];
          if (sample > 1.0f)
            sample = 1.0f;
          if (sample < -1.0f)
            sample = -1.0f;
        }
        const float sy = midY - sample * amp;
        const int fx0 = cast(int)prevX;
        const int fx1 = cast(int)(sx + 0.5f);
        const int fy = cast(int)((prevY + sy) * 0.5f + 0.5f);
        if (fx1 > fx0 && model.oscilloscopeSamples.length > 0) {
          const int fTop = fy < midY ? fy : midY;
          const int fBot = fy > midY ? fy : midY + 1;
          if (fBot > fTop) {
            buf.fillRect(Rect(fx0, fTop, fx1, fBot), scopeFillCol);
          }
        }
        drawThickLine(buf, prevX, prevY, sx, sy, dpf(1.4f), scopeTraceCol);
        prevX = sx;
        prevY = sy;
      }
    }
    drawVCenteredText(buf, getDawFont(10, fnt, FontWeight.Normal),
        scopePanel.left + dp(6), Rect(scopePanel.left, scopePanel.top + dp(2),
          scopePanel.right, scopePanel.top + dp(15)),
        toUTF32(format("CPU %d%%", model.cpuPercent)), pal.fgMuted);
    if (scopePanel.width >= dp(106)) {
      const int barW = dp(38);
      const int barH = dp(4);
      Rect cpuBg = Rect(scopePanel.right - dp(8) - barW, scopePanel.top + dp(6),
          scopePanel.right - dp(8), scopePanel.top + dp(6) + barH);
      buf.fillRect(cpuBg, pal.ledOff);
      int clampedCpu = model.cpuPercent;
      if (clampedCpu < 0)
        clampedCpu = 0;
      if (clampedCpu > 100)
        clampedCpu = 100;
      const int fillW = (barW * clampedCpu) / 100;
      if (fillW > 0) {
        const uint cpuCol = clampedCpu >= 75 ? pal.accentRed : (clampedCpu >= 45 ?
            pal.accentOrange : pal.accentCyan);
        buf.fillRect(Rect(cpuBg.left, cpuBg.top, cpuBg.left + fillW, cpuBg.bottom),
            cpuCol);
      }
    }
  }
}

/// Interactive FL Studio 2026 Modular Transport & Workspace Toolbar Widget.
class FlToolbarWidget : Widget {
  private TransportModel* model_;

  private enum DragMode {
    NONE,
    MASTER_VOL,
    MASTER_PITCH,
    BPM
  }

  private DragMode dragMode_ = DragMode.NONE;
  private int dragStartY_ = 0;
  private double dragStartVal_ = 0.0;

  bool delegate() queryPlaylistVisible;
  bool delegate() queryChannelRackVisible;
  bool delegate() queryPianoRollVisible;
  bool delegate() queryBrowserVisible;
  bool delegate() queryMixerVisible;
  bool delegate() queryPluginVisible;

  void delegate() onPatSongToggle;
  void delegate() onPlayToggle;
  void delegate() onStop;
  void delegate() onRecordToggle;
  void delegate() onLoopToggle;
  void delegate(bool enabled) onMetronomeToggle;
  void delegate(float volume) onMasterVolumeChanged;
  void delegate(float pitchNormalized) onMasterPitchChanged;
  void delegate(double bpm) onBpmChanged;
  void delegate(bool redo) onUndoRedo;
  void delegate(int delta) onPatternStep;
  void delegate() onPatternAdd;
  void delegate(string winKey) onToggleWindow;

  this(string id, TransportModel* model) {
    super(id);
    model_ = model;
    layoutWidth = FILL_PARENT;
    layoutHeight = dp(48);
    minHeight = dp(48);
  }

  override void measure(int parentWidth, int parentHeight) {
    const int h = dp(48);
    minHeight = h;
    layoutHeight = h;
    measuredContent(parentWidth, parentHeight, parentWidth, h);
  }

  override void onDraw(DrawBuf buf) {
    if (visibility != Visibility.Visible)
      return;
    const bool vPl = queryPlaylistVisible !is null ? queryPlaylistVisible() : true;
    const bool vCr = queryChannelRackVisible !is null ? queryChannelRackVisible() : true;
    const bool vPr = queryPianoRollVisible !is null ? queryPianoRollVisible() : true;
    const bool vBr = queryBrowserVisible !is null ? queryBrowserVisible() : true;
    const bool vMx = queryMixerVisible !is null ? queryMixerVisible() : true;
    const bool vPg = queryPluginVisible !is null ? queryPluginVisible() : false;
    drawToolbarCanvas(buf, _pos, model_, vPl, vCr, vPr, vBr, vMx, vPg, safeWidgetFont(
        this));
  }

  override bool onMouseEvent(MouseEvent event) {
    if (event.action == MouseAction.FocusOut) {
      return dragMode_ != DragMode.NONE;
    }
    if (!model_)
      return super.onMouseEvent(event);

    auto tl = computeToolbarLayout(_pos);

    if (event.action == MouseAction.ButtonDown) {
      // 1. Master knobs
      if (tl.masterPanel.isPointInside(event.x, event.y)) {
        if (event.x < tl.masterPanel.middlex) {
          if (event.button == MouseButton.Right) {
            model_.masterVolume = 0.82f;
            if (onMasterVolumeChanged !is null)
              onMasterVolumeChanged(model_.masterVolume);
          } else {
            dragMode_ = DragMode.MASTER_VOL;
            dragStartY_ = event.y;
            dragStartVal_ = model_.masterVolume;
          }
        } else {
          if (event.button == MouseButton.Right) {
            model_.masterPitch = 0.0f;
            if (onMasterPitchChanged !is null)
              onMasterPitchChanged(model_.masterPitch);
          } else {
            dragMode_ = DragMode.MASTER_PITCH;
            dragStartY_ = event.y;
            dragStartVal_ = model_.masterPitch;
          }
        }
        invalidate();
        return true;
      }

      // 2. Transport Panel
      if (tl.transPanel.isPointInside(event.x, event.y)) {
        if (tl.patSongRc.isPointInside(event.x, event.y)) {
          model_.playbackMode = (model_.playbackMode == PlaybackMode.PAT) ?
            PlaybackMode.SONG : PlaybackMode.PAT;
          if (onPatSongToggle !is null)
            onPatSongToggle();
        } else if (tl.playRc.isPointInside(event.x, event.y)) {
          if (onPlayToggle !is null)
            onPlayToggle();
        } else if (tl.stopRc.isPointInside(event.x, event.y)) {
          if (onStop !is null)
            onStop();
        } else if (tl.recRc.isPointInside(event.x, event.y)) {
          if (onRecordToggle !is null)
            onRecordToggle();
        } else if (tl.loopRc.isPointInside(event.x, event.y)) {
          if (onLoopToggle !is null)
            onLoopToggle();
        } else if (tl.bpmRc.isPointInside(event.x, event.y)) {
          if (event.button == MouseButton.Right) {
            model_.bpm = 140.0;
            if (onBpmChanged !is null)
              onBpmChanged(model_.bpm);
          } else {
            dragMode_ = DragMode.BPM;
            dragStartY_ = event.y;
            dragStartVal_ = model_.bpm;
          }
        } else if (tl.timeRc.isPointInside(event.x, event.y)) {
          model_.timeDisplayMode = (
              model_.timeDisplayMode == TimeDisplayMode.BAR_BEAT) ?
            TimeDisplayMode.TIME_MS : TimeDisplayMode.BAR_BEAT;
        }
        invalidate();
        return true;
      }

      // 3. Rec / Metronome / Typing Keyboard
      if (tl.recOptsPanel.isPointInside(event.x, event.y)) {
        foreach (idx; 0 .. 4) {
          if (tl.recToggles[idx].isPointInside(event.x, event.y)) {
            if (idx == 0) {
              model_.metronomeEnabled = !model_.metronomeEnabled;
              if (onMetronomeToggle !is null)
                onMetronomeToggle(model_.metronomeEnabled);
            } else if (idx == 1)
              model_.countdownEnabled = !model_.countdownEnabled;
            else if (idx == 2)
              model_.overdubEnabled = !model_.overdubEnabled;
            else if (idx == 3)
              model_.typingKeyboardEnabled = !model_.typingKeyboardEnabled;
            break;
          }
        }
        invalidate();
        return true;
      }

      // 4. Snap & Undo/Redo
      if (tl.snapPanel.isPointInside(event.x, event.y)) {
        if (tl.snapRc.isPointInside(event.x, event.y)) {
          model_.snapMode = cast(SnapMode)((cast(int)model_.snapMode + 1) % 10);
          model_.hintText = "Global Snap: " ~ snapLabel(model_.snapMode);
        } else if (tl.undoRc.isPointInside(event.x, event.y)) {
          if (onUndoRedo !is null)
            onUndoRedo(false);
        } else if (tl.redoRc.isPointInside(event.x, event.y)) {
          if (onUndoRedo !is null)
            onUndoRedo(true);
        }
        invalidate();
        return true;
      }

      // 5. Pattern Selector
      if (tl.patPanel.isPointInside(event.x, event.y)) {
        if (tl.pPrev.isPointInside(event.x, event.y)) {
          if (onPatternStep !is null)
            onPatternStep(-1);
        } else if (tl.pLbl.isPointInside(event.x, event.y)) {
          if (onPatternStep !is null)
            onPatternStep(event.button == MouseButton.Right ? -1 : 1);
        } else if (tl.pNext.isPointInside(event.x, event.y)) {
          if (onPatternStep !is null)
            onPatternStep(1);
        } else if (tl.pAdd.isPointInside(event.x, event.y)) {
          if (onPatternAdd !is null)
            onPatternAdd();
        }
        invalidate();
        return true;
      }

      // 6. Window View Toggles
      if (tl.hasWinPanel && tl.winPanel.isPointInside(event.x, event.y)) {
        static immutable string[6] KEYS = [
          "win_playlist", "win_channel_rack", "win_piano_roll", "browser",
          "win_mixer", "win_plugin"
        ];
        foreach (idx; 0 .. 6) {
          if (tl.winBtns[idx].isPointInside(event.x, event.y)) {
            if (onToggleWindow !is null)
              onToggleWindow(KEYS[idx]);
            break;
          }
        }
        invalidate();
        return true;
      }
      return true;
    }

    if (event.action == MouseAction.Move && dragMode_ != DragMode.NONE) {
      const float dy = cast(float)(dragStartY_ - event.y) / uiScale();
      if (dragMode_ == DragMode.MASTER_VOL) {
        float v = cast(float)dragStartVal_ + cast(float)dy / 100.0f;
        model_.masterVolume = v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
        model_.hintText = format("Master Volume: %d%%",
            cast(int)(model_.masterVolume * 100.0f + 0.5f));
        if (onMasterVolumeChanged !is null)
          onMasterVolumeChanged(model_.masterVolume);
      } else if (dragMode_ == DragMode.MASTER_PITCH) {
        float p = cast(float)dragStartVal_ + cast(float)dy / 60.0f;
        model_.masterPitch = p < -1.0f ? -1.0f : (p > 1.0f ? 1.0f : p);
        model_.hintText = format("Master Pitch: %+d cents", cast(int)(model_
            .masterPitch * 1200.0f));
        if (onMasterPitchChanged !is null)
          onMasterPitchChanged(model_.masterPitch);
      } else if (dragMode_ == DragMode.BPM) {
        double newBpm = dragStartVal_ + cast(double)dy * 0.5;
        if (newBpm < 40.0)
          newBpm = 40.0;
        if (newBpm > 300.0)
          newBpm = 300.0;
        model_.bpm = newBpm;
        model_.hintText = format("Tempo: %.1f BPM", newBpm);
        if (onBpmChanged !is null)
          onBpmChanged(newBpm);
      }
      invalidate();
      return true;
    }

    if ((event.action == MouseAction.ButtonUp ||
        event.action == MouseAction.Cancel) && dragMode_ != DragMode.NONE) {
      if (dragMode_ == DragMode.BPM && onBpmChanged !is null) {
        onBpmChanged(model_.bpm);
      } else if (dragMode_ == DragMode.MASTER_VOL && onMasterVolumeChanged !is null) {
        onMasterVolumeChanged(model_.masterVolume);
      } else if (dragMode_ == DragMode.MASTER_PITCH && onMasterPitchChanged !is null) {
        onMasterPitchChanged(model_.masterPitch);
      }
      dragMode_ = DragMode.NONE;
      return true;
    }

    return super.onMouseEvent(event);
  }
}

unittest {
  import dlangui.graphics.drawbuf : ColorDrawBuf;
  import dlangui.widgets.styles : currentTheme, createDefaultTheme;

  TransportModel model;
  model.initDefaultTemplate();
  model.isPlaying = true;
  auto buf = new ColorDrawBuf(1280, 48);
  drawToolbarCanvas(buf, Rect(0, 0, 1280, 48), &model, true, true, true, true, true,
      false);
  assert(buf.scanLine(10)[100] == model.palette().bgToolbarWell ||
      buf.scanLine(10)[100] == model.palette().accentOrange ||
      buf.scanLine(10)[100] == model.palette().accentGreen);

  import gui_d.theme : destroyWidgetTree;

  if (currentTheme is null)
    currentTheme = createDefaultTheme();
  auto tb = new FlToolbarWidget("tb", &model);
  scope (exit)
    destroyWidgetTree(tb);
  tb.measure(1280, 48);
  tb.layout(Rect(0, 0, 1280, 48));
  bool played = false;
  bool stopped = false;
  double changedBpm = 0.0;
  tb.onPlayToggle = () { played = true; };
  tb.onStop = () { stopped = true; };
  tb.onBpmChanged = (double b) { changedBpm = b; };

  // Play button is at x = 6 + 76 + 6 + 56..88 = 144..176
  tb.onMouseEvent(new MouseEvent(MouseAction.ButtonDown, MouseButton.Left,
      MouseFlag.LButton, 155, 24));
  assert(played, "Expected toolbar Play button click to trigger onPlayToggle");
  // BPM drag + Cancel ends drag cleanly and notifies onBpmChanged
  tb.onMouseEvent(new MouseEvent(MouseAction.ButtonDown, MouseButton.Left,
      MouseFlag.LButton, 300, 24));
  tb.onMouseEvent(new MouseEvent(MouseAction.Move, MouseButton.None, MouseFlag
      .LButton, 300, 14));
  tb.onMouseEvent(new MouseEvent(MouseAction.Cancel, MouseButton.None, 0, 300, 14));
  assert(changedBpm > 140.0, "Expected BPM drag + Cancel to commit updated BPM");
  const double lockedBpm = model.bpm;
  tb.onMouseEvent(new MouseEvent(MouseAction.Move, MouseButton.None, 0, 300, 0));
  assert(model.bpm == lockedBpm, "Expected MouseAction.Cancel to clear BPM drag");
}
