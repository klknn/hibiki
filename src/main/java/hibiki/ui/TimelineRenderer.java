package hibiki.ui;

import java.awt.*;
import java.awt.geom.*;
import java.util.List;
import javax.swing.JPanel;

/**
 * Handles all rendering for the TimelineView: track backgrounds, grid lines, clip rectangles (with
 * MIDI/audio waveform previews), automation lanes, drag ghosts, and the playhead.
 */
class TimelineRenderer {
  private final TimelineView view;

  TimelineRenderer(TimelineView view) {
    this.view = view;
  }

  void drawTrackLabels(
      Graphics g,
      List<TimelineView.TrackTimeline> tracks,
      int selectedTrack,
      int trackHeight,
      int timeRulerHeight,
      int labelWidth) {
    Graphics2D g2 = (Graphics2D) g;
    g2.setRenderingHint(RenderingHints.KEY_ANTIALIASING, RenderingHints.VALUE_ANTIALIAS_ON);

    int scaleTimeRuler = Theme.getInstance().scale(timeRulerHeight);
    int scaleAutoLane = Theme.getInstance().scale(view.getAutomationLaneHeight());
    int scaleLabelWidth = Theme.getInstance().scale(labelWidth);

    g2.setColor(Theme.getInstance().BG_DARKER);
    g2.fillRect(0, 0, scaleLabelWidth, scaleTimeRuler);

    for (int i = 0; i < tracks.size(); i++) {
      if (tracks.get(i).hidden) continue;
      int scaleBaseTrack = Theme.getInstance().scale(view.getBaseTrackHeight(i));
      int y = scaleTimeRuler + Theme.getInstance().scale(view.getTrackY(i));

      // Main track label
      if (i == selectedTrack) {
        g2.setColor(Theme.getInstance().ACCENT_BLUE.darker());
      } else {
        g2.setColor(Theme.getInstance().TRACK_HEADER);
      }
      g2.fillRect(0, y, scaleLabelWidth, scaleBaseTrack - 1);

      TimelineView.TrackTimeline track = tracks.get(i);

      // Row 1: Track name
      g2.setColor(Theme.getInstance().TEXT_BRIGHT);
      g2.setFont(Theme.getInstance().FONT_UI_BOLD);
      String displayName = track.getDisplayName();
      if (displayName.length() > 14) displayName = displayName.substring(0, 13) + "…";
      g2.drawString(displayName, 5, y + 14);

      // Row 2: Plugin name
      g2.setFont(Theme.getInstance().FONT_UI.deriveFont(Theme.getInstance().scale(9.0f)));
      if (track.pluginName != null) {
        g2.setColor(
            track.isInstrument ? Theme.getInstance().ACCENT_ORANGE : Theme.getInstance().TEXT_DIM);
        String pname = track.pluginName;
        if (pname.length() > 14) pname = pname.substring(0, 13) + "…";
        g2.drawString(pname, 5, y + 27);
      } else {
        g2.setColor(Theme.getInstance().TEXT_DIM);
        g2.drawString("(no plugin)", 5, y + 27);
      }

      // Row 3: Input dropdown + MIDI/AUD toggle + ARM button (Ableton-style, always
      // visible)
      int row3Y = y + 33;
      int btnH = 18;
      int armW = 30;
      int modeW = 34;
      int gap = 3;
      int inputW = scaleLabelWidth - armW - modeW - gap * 4;

      // ── Input channel dropdown button ──
      g2.setColor(new Color(50, 50, 55));
      g2.fillRoundRect(gap, row3Y, inputW, btnH, 4, 4);
      g2.setColor(new Color(80, 80, 85));
      g2.drawRoundRect(gap, row3Y, inputW, btnH, 4, 4);
      String inLabel;
      if (track.inputStereo) {
        inLabel = "In " + (track.inputChannelStart + 1) + "-" + (track.inputChannelStart + 2);
      } else {
        inLabel = "In " + (track.inputChannelStart + 1);
      }
      g2.setColor(Theme.getInstance().TEXT_BRIGHT);
      g2.setFont(Theme.getInstance().FONT_UI.deriveFont(Theme.getInstance().scale(9.0f)));
      g2.drawString(inLabel, gap + 4, row3Y + 13);
      // Dropdown arrow ▼
      g2.setColor(Theme.getInstance().TEXT_DIM);
      int ax = gap + inputW - 12;
      int[] xPts = {ax, ax + 8, ax + 4};
      int[] yPts = {row3Y + 7, row3Y + 7, row3Y + 12};
      g2.fillPolygon(xPts, yPts, 3);

      // ── MIDI/AUD toggle button ──
      int modeX = gap + inputW + gap;
      if (track.midiRecordMode) {
        g2.setColor(new Color(40, 80, 140));
        g2.fillRoundRect(modeX, row3Y, modeW, btnH, 4, 4);
        g2.setColor(new Color(100, 160, 220));
      } else {
        g2.setColor(new Color(50, 50, 55));
        g2.fillRoundRect(modeX, row3Y, modeW, btnH, 4, 4);
        g2.setColor(new Color(80, 80, 85));
        g2.drawRoundRect(modeX, row3Y, modeW, btnH, 4, 4);
        g2.setColor(new Color(140, 140, 140));
      }
      g2.setFont(Theme.getInstance().FONT_UI_BOLD.deriveFont(Theme.getInstance().scale(8.0f)));
      String modeLabel = track.midiRecordMode ? "MIDI" : "AUD";
      FontMetrics mfm = g2.getFontMetrics();
      int mtw = mfm.stringWidth(modeLabel);
      g2.drawString(modeLabel, modeX + (modeW - mtw) / 2, row3Y + 13);

      // ── ARM button ──
      int armX = gap + inputW + gap + modeW + gap;
      if (track.recordArmed) {
        g2.setColor(new Color(200, 35, 35));
        g2.fillRoundRect(armX, row3Y, armW, btnH, 4, 4);
        g2.setColor(Color.WHITE);
      } else {
        g2.setColor(new Color(50, 50, 55));
        g2.fillRoundRect(armX, row3Y, armW, btnH, 4, 4);
        g2.setColor(new Color(80, 80, 85));
        g2.drawRoundRect(armX, row3Y, armW, btnH, 4, 4);
        g2.setColor(new Color(160, 60, 60));
      }
      g2.setFont(Theme.getInstance().FONT_UI_BOLD.deriveFont(Theme.getInstance().scale(9.0f)));
      FontMetrics fm = g2.getFontMetrics();
      int tw = fm.stringWidth("ARM");
      g2.drawString("ARM", armX + (armW - tw) / 2, row3Y + 13);

      // Row 4: VOL and PAN knobs + S/M buttons
      int row4Y = y + 55;
      int knobD = 18; // diameter
      int knobR = knobD / 2;

      // ── VOL knob ──
      int volX = 6;
      int volCX = volX + knobR;
      int volCY = row4Y + knobR;
      g2.setColor(new Color(45, 45, 50));
      g2.fillOval(volX, row4Y, knobD, knobD);
      g2.setColor(new Color(65, 65, 70));
      g2.drawOval(volX, row4Y, knobD, knobD);
      // Map gain to dB, then to normalized knob position (0-1)
      // dB range: -60 to +6 (66 dB span), gain=0 maps to 0
      float volDb;
      float volNorm;
      if (track.volume <= 0.001f) {
        volDb = -100; // effectively -inf
        volNorm = 0;
      } else {
        volDb = (float) (20.0 * Math.log10(track.volume));
        volNorm = Math.max(0, Math.min(1.0f, (volDb + 60.0f) / 66.0f));
      }
      int volSweep = (int) (volNorm * 270);
      if (volSweep > 0) {
        g2.setColor(new Color(80, 180, 80));
        g2.setStroke(
            new java.awt.BasicStroke(
                2.5f, java.awt.BasicStroke.CAP_ROUND, java.awt.BasicStroke.JOIN_ROUND));
        g2.drawArc(volX + 2, row4Y + 2, knobD - 4, knobD - 4, 225, -volSweep);
        g2.setStroke(new java.awt.BasicStroke(1.0f));
      }
      double volAngle = Math.toRadians(225 - volNorm * 270);
      int vix = volCX + (int) (Math.cos(volAngle) * (knobR - 3));
      int viy = volCY - (int) (Math.sin(volAngle) * (knobR - 3));
      g2.setColor(new Color(200, 200, 200));
      g2.drawLine(volCX, volCY, vix, viy);
      // dB label
      g2.setFont(Theme.getInstance().FONT_UI.deriveFont(Theme.getInstance().scale(7.5f)));
      g2.setColor(new Color(140, 140, 140));
      String volStr =
          track.volume <= 0.001f
              ? "-∞"
              : (volDb >= 0 ? String.format("+%.1f", volDb) : String.format("%.1f", volDb));
      g2.drawString(volStr, volX + knobD + 2, row4Y + 13);

      // ── PAN knob ──
      int panX = scaleLabelWidth / 2 + 4;
      int panCX = panX + knobR;
      int panCY = row4Y + knobR;
      g2.setColor(new Color(45, 45, 50));
      g2.fillOval(panX, row4Y, knobD, knobD);
      g2.setColor(new Color(65, 65, 70));
      g2.drawOval(panX, row4Y, knobD, knobD);
      // Pan value arc: -1..1 maps from center (12 o'clock) left or right
      float panNorm = (track.pan + 1.0f) / 2.0f; // 0..1
      int panSweep = (int) (panNorm * 270);
      if (panSweep > 0) {
        g2.setColor(new Color(80, 140, 220));
        g2.setStroke(
            new java.awt.BasicStroke(
                2.5f, java.awt.BasicStroke.CAP_ROUND, java.awt.BasicStroke.JOIN_ROUND));
        g2.drawArc(panX + 2, row4Y + 2, knobD - 4, knobD - 4, 225, -panSweep);
        g2.setStroke(new java.awt.BasicStroke(1.0f));
      }
      double panAngle = Math.toRadians(225 - panNorm * 270);
      int pix = panCX + (int) (Math.cos(panAngle) * (knobR - 3));
      int piy = panCY - (int) (Math.sin(panAngle) * (knobR - 3));
      g2.setColor(new Color(200, 200, 200));
      g2.drawLine(panCX, panCY, pix, piy);
      // Label
      g2.setFont(Theme.getInstance().FONT_UI.deriveFont(Theme.getInstance().scale(7.5f)));
      g2.setColor(new Color(140, 140, 140));
      String panStr =
          track.pan == 0
              ? "C"
              : (track.pan < 0
                  ? String.format("L%.0f", -track.pan * 100)
                  : String.format("R%.0f", track.pan * 100));
      g2.drawString(panStr, panX + knobD + 2, row4Y + 13);

      // ── S (Solo) and M (Mute) buttons on Row 4, after PAN ──
      int smW = 16;
      int smBtnH = 16;
      int smGap = 3;
      int smY = row4Y + 1;
      int soloX = panX + knobD + g2.getFontMetrics().stringWidth(panStr) + 6;
      if (track.soloed) {
        g2.setColor(new Color(200, 180, 40));
        g2.fillRoundRect(soloX, smY, smW, smBtnH, 4, 4);
        g2.setColor(Color.BLACK);
      } else {
        g2.setColor(new Color(50, 50, 55));
        g2.fillRoundRect(soloX, smY, smW, smBtnH, 4, 4);
        g2.setColor(new Color(80, 80, 85));
        g2.drawRoundRect(soloX, smY, smW, smBtnH, 4, 4);
        g2.setColor(new Color(160, 150, 60));
      }
      g2.setFont(Theme.getInstance().FONT_UI_BOLD.deriveFont(Theme.getInstance().scale(8.0f)));
      int stw = g2.getFontMetrics().stringWidth("S");
      g2.drawString("S", soloX + (smW - stw) / 2, smY + 12);

      int muteX = soloX + smW + smGap;
      if (track.muted) {
        g2.setColor(new Color(200, 60, 60));
        g2.fillRoundRect(muteX, smY, smW, smBtnH, 4, 4);
        g2.setColor(Color.WHITE);
      } else {
        g2.setColor(new Color(50, 50, 55));
        g2.fillRoundRect(muteX, smY, smW, smBtnH, 4, 4);
        g2.setColor(new Color(80, 80, 85));
        g2.drawRoundRect(muteX, smY, smW, smBtnH, 4, 4);
        g2.setColor(new Color(160, 60, 60));
      }
      int mtw2 = g2.getFontMetrics().stringWidth("M");
      g2.drawString("M", muteX + (smW - mtw2) / 2, smY + 12);

      // Automation expand/collapse indicator
      if (!track.automationLanes.isEmpty()) {
        String toggleSymbol = track.automationExpanded ? "▼" : "▶";
        g2.setColor(Theme.getInstance().ACCENT_BLUE);
        g2.setFont(Theme.getInstance().FONT_UI.deriveFont(Theme.getInstance().scale(9.0f)));
        g2.drawString(
            toggleSymbol + " Auto (" + track.automationLanes.size() + ")",
            5,
            y + scaleBaseTrack - 6);
      }

      // Level meter bars at right edge of track header
      int meterW = 3;
      int meterH = scaleBaseTrack - 8;
      int meterX = scaleLabelWidth - meterW * 2 - 4;
      int meterY = y + 4;
      float peakL = Math.min(1.0f, track.peakL);
      float peakR = Math.min(1.0f, track.peakR);
      // Background
      g2.setColor(new Color(20, 20, 20));
      g2.fillRect(meterX, meterY, meterW, meterH);
      g2.fillRect(meterX + meterW + 1, meterY, meterW, meterH);
      // Left meter fill
      int fillL = (int) (peakL * meterH);
      if (fillL > 0) {
        g2.setColor(
            peakL > 0.9f
                ? new Color(220, 50, 50)
                : peakL > 0.7f ? new Color(220, 180, 50) : new Color(50, 200, 80));
        g2.fillRect(meterX, meterY + meterH - fillL, meterW, fillL);
      }
      // Right meter fill
      int fillR = (int) (peakR * meterH);
      if (fillR > 0) {
        g2.setColor(
            peakR > 0.9f
                ? new Color(220, 50, 50)
                : peakR > 0.7f ? new Color(220, 180, 50) : new Color(50, 200, 80));
        g2.fillRect(meterX + meterW + 1, meterY + meterH - fillR, meterW, fillR);
      }

      g2.setColor(Theme.getInstance().BORDER);
      g2.drawLine(0, y + scaleBaseTrack - 1, scaleLabelWidth, y + scaleBaseTrack - 1);

      // Draw automation lane labels when expanded
      if (track.automationExpanded) {
        for (int j = 0; j < track.automationLanes.size(); j++) {
          int autoY = y + scaleBaseTrack + j * scaleAutoLane;
          g2.setColor(Theme.getInstance().BG_DARKER.brighter());
          g2.fillRect(0, autoY, scaleLabelWidth, scaleAutoLane - 1);

          TimelineView.AutomationLaneData lane = track.automationLanes.get(j);
          g2.setColor(Theme.getInstance().ACCENT_ORANGE);
          g2.setFont(Theme.getInstance().FONT_UI.deriveFont(Theme.getInstance().scale(9.0f)));
          String lName = lane.paramName;
          if (lName.length() > 14) lName = lName.substring(0, 13) + "…";
          g2.drawString(lName, 5, autoY + scaleAutoLane / 2 + 4);

          g2.setColor(Theme.getInstance().BORDER);
          g2.drawLine(0, autoY + scaleAutoLane - 1, scaleLabelWidth, autoY + scaleAutoLane - 1);
        }
      }
    }
  }

  void drawTimeline(
      Graphics g,
      JPanel contentPanel,
      List<TimelineView.TrackTimeline> tracks,
      int selectedTrack,
      float bpm,
      GridMode gridMode,
      float playheadPos,
      TimelineView.DragMode dragMode,
      TimelineView.ClipRect draggingClip,
      int dragSourceTrack,
      float dragOriginalStartTime,
      int dragCurrentY,
      int creatingTrackIdx,
      TimelineView.ClipRect creatingClipRect,
      int creatingAutoLaneIdx,
      int trackHeight,
      int timeRulerHeight) {
    Graphics2D g2 = (Graphics2D) g;
    g2.setRenderingHint(RenderingHints.KEY_ANTIALIASING, RenderingHints.VALUE_ANTIALIAS_ON);

    int scaleTimeRuler = Theme.getInstance().scale(timeRulerHeight);
    int scaleAutoLane = Theme.getInstance().scale(view.getAutomationLaneHeight());
    int scaleLabelWidth = 0;
    float pps = view.getPixelsPerSecond();
    float secondsPerBeat = 60.0f / bpm;

    // Draw tracks background
    for (int i = 0; i < tracks.size(); i++) {
      if (tracks.get(i).hidden) continue;
      int scaleBaseTrack = Theme.getInstance().scale(view.getBaseTrackHeight(i));
      int y = scaleTimeRuler + Theme.getInstance().scale(view.getTrackY(i));
      // Main clip area
      if (i == selectedTrack) {
        g2.setColor(Theme.getInstance().ACCENT_BLUE.darker().darker());
      } else {
        g2.setColor(i % 2 == 0 ? Theme.getInstance().BG_DARK : Theme.getInstance().BG_DARKER);
      }
      g2.fillRect(0, y, contentPanel.getWidth(), scaleBaseTrack);
      g2.setColor(Theme.getInstance().PANEL_BG_LIGHT.darker());
      g2.drawLine(0, y + scaleBaseTrack - 1, contentPanel.getWidth(), y + scaleBaseTrack - 1);

      // Automation lane backgrounds (when expanded)
      TimelineView.TrackTimeline track = tracks.get(i);
      if (track.automationExpanded) {
        int scaleBaseTrackForAuto = Theme.getInstance().scale(view.getBaseTrackHeight(i));
        for (int j = 0; j < track.automationLanes.size(); j++) {
          int autoY = y + scaleBaseTrackForAuto + j * scaleAutoLane;
          g2.setColor(new Color(30, 30, 45));
          g2.fillRect(0, autoY, contentPanel.getWidth(), scaleAutoLane);
          g2.setColor(Theme.getInstance().BORDER);
          g2.drawLine(
              0, autoY + scaleAutoLane - 1, contentPanel.getWidth(), autoY + scaleAutoLane - 1);
        }
      }
    }

    // Draw grid lines
    int trackAreaBottom = scaleTimeRuler + Theme.getInstance().scale(view.getTotalTracksHeight());
    float secondsPerBar = secondsPerBeat * 4;
    float gridSeconds = view.getGridSnapSeconds(gridMode, secondsPerBeat);

    drawGridLines(
        g2,
        contentPanel,
        scaleTimeRuler,
        trackAreaBottom,
        scaleLabelWidth,
        pps,
        gridSeconds,
        secondsPerBeat,
        secondsPerBar);

    boolean isDragging = (dragMode == TimelineView.DragMode.MOVE_CLIP);
    boolean creatingClip = (dragMode == TimelineView.DragMode.CREATE_CLIP);

    // Draw ghost shadow of dragged clip
    if (isDragging && draggingClip != null && dragSourceTrack >= 0) {
      int ghostY = scaleTimeRuler + Theme.getInstance().scale(view.getTrackY(dragSourceTrack));
      int ghostBaseTrack = Theme.getInstance().scale(view.getBaseTrackHeight(dragSourceTrack));
      drawDragGhost(
          g2,
          ghostY,
          ghostBaseTrack,
          scaleLabelWidth,
          pps,
          dragOriginalStartTime,
          draggingClip.duration);
    }

    // Draw clips
    drawClips(g2, tracks, scaleTimeRuler, scaleLabelWidth, pps, isDragging, draggingClip);

    // Draw automation curves (when expanded)
    for (int i = 0; i < tracks.size(); i++) {
      if (tracks.get(i).hidden) continue;
      TimelineView.TrackTimeline track = tracks.get(i);
      if (!track.automationExpanded || track.automationLanes.isEmpty()) continue;
      int trackY = scaleTimeRuler + Theme.getInstance().scale(view.getTrackY(i));
      int trackBaseH = Theme.getInstance().scale(view.getBaseTrackHeight(i));
      for (int j = 0; j < track.automationLanes.size(); j++) {
        int autoY = trackY + trackBaseH + j * scaleAutoLane;
        drawAutomationCurve(
            g2,
            track.automationLanes.get(j),
            autoY,
            scaleAutoLane,
            scaleLabelWidth,
            pps,
            secondsPerBeat);
      }
    }

    // Draw dragging clip at cursor position
    if (isDragging && draggingClip != null) {
      int targetTrackIdx = getTrackAtY(dragCurrentY - scaleTimeRuler, tracks);
      int targetY = scaleTimeRuler + Theme.getInstance().scale(view.getTrackY(targetTrackIdx));
      int targetBaseH = Theme.getInstance().scale(view.getBaseTrackHeight(targetTrackIdx));
      drawClipAt(
          g2,
          draggingClip,
          targetY + 5,
          scaleLabelWidth,
          pps,
          targetBaseH,
          0.8f,
          Theme.getInstance().ACCENT_BLUE.brighter());
    }

    // Draw clip being created
    if (creatingClip && creatingClipRect != null && creatingClipRect.duration > 0) {
      int x = scaleLabelWidth + (int) (creatingClipRect.startTime * pps);
      int w = (int) (creatingClipRect.duration * pps);
      if (creatingAutoLaneIdx >= 0) {
        int creatingBaseH = Theme.getInstance().scale(view.getBaseTrackHeight(creatingTrackIdx));
        int y =
            scaleTimeRuler
                + Theme.getInstance().scale(view.getTrackY(creatingTrackIdx))
                + creatingBaseH
                + creatingAutoLaneIdx * scaleAutoLane
                + 4;
        int h = scaleAutoLane - 8;
        g2.setComposite(AlphaComposite.getInstance(AlphaComposite.SRC_OVER, 0.6f));
        g2.setColor(Theme.getInstance().ACCENT_ORANGE.brighter());
        g2.fillRect(x, y, w, h);
        g2.setColor(Color.WHITE);
        g2.drawRect(x, y, w, h);
      } else {
        int creatingBaseH2 = Theme.getInstance().scale(view.getBaseTrackHeight(creatingTrackIdx));
        int y = scaleTimeRuler + Theme.getInstance().scale(view.getTrackY(creatingTrackIdx)) + 5;
        int h = creatingBaseH2 - 10;
        g2.setComposite(AlphaComposite.getInstance(AlphaComposite.SRC_OVER, 0.6f));
        g2.setColor(new Color(100, 200, 100));
        g2.fillRoundRect(x, y, w, h, 8, 8);
        g2.setColor(new Color(150, 255, 150));
        g2.drawRoundRect(x, y, w, h, 8, 8);
        g2.setColor(Color.WHITE);
        g2.setFont(
            Theme.getInstance().FONT_UI.deriveFont(Font.BOLD, Theme.getInstance().scale(10.0f)));
        g2.drawString("New Clip", x + 5, y + 15);
      }
      g2.setComposite(AlphaComposite.SrcOver);
    }

    // Draw time ruler
    drawTimeRuler(g2, contentPanel, scaleTimeRuler, scaleLabelWidth, pps, bpm, gridMode);

    // Draw playhead
    int px = scaleLabelWidth + (int) (playheadPos * pps);
    g2.setColor(Color.RED);
    g2.setStroke(new BasicStroke(2.0f));
    g2.drawLine(px, 0, px, contentPanel.getHeight());
  }

  /** Draw an automation curve inside its lane sub-row based on individual clips. */
  private void drawAutomationCurve(
      Graphics2D g2,
      TimelineView.AutomationLaneData lane,
      int y,
      int h,
      int xOff,
      float pps,
      float secPerBeat) {
    AutomationRenderer.drawAutomationCurve(g2, lane, y, h, xOff, pps, secPerBeat);
  }

  /** Find which track index corresponds to a given Y offset from time ruler. */
  private int getTrackAtY(int yFromRuler, List<TimelineView.TrackTimeline> tracks) {
    int cumY = 0;
    for (int i = 0; i < tracks.size(); i++) {
      if (tracks.get(i).hidden) continue;
      int th = Theme.getInstance().scale(view.getTotalTrackHeight(i));
      if (yFromRuler < cumY + th) return i;
      cumY += th;
    }
    return Math.max(0, tracks.size() - 1);
  }

  private void drawGridLines(
      Graphics2D g2,
      JPanel contentPanel,
      int scaleTimeRuler,
      int trackAreaBottom,
      int scaleLabelWidth,
      float pps,
      float gridSeconds,
      float secondsPerBeat,
      float secondsPerBar) {
    if (gridSeconds > 0) {
      float gridWidth = gridSeconds * pps;
      if (gridWidth >= 2) {
        g2.setColor(new Color(255, 255, 255, 15));
        for (float t = 0; t * pps < contentPanel.getWidth(); t += gridSeconds) {
          int x = scaleLabelWidth + (int) (t * pps);
          g2.drawLine(x, scaleTimeRuler, x, trackAreaBottom);
        }
      }
    }
    float beatWidth = secondsPerBeat * pps;
    if (beatWidth >= 4 && gridSeconds < secondsPerBeat) {
      g2.setColor(new Color(255, 255, 255, 25));
      for (float t = 0; t * pps < contentPanel.getWidth(); t += secondsPerBeat) {
        int x = scaleLabelWidth + (int) (t * pps);
        g2.drawLine(x, scaleTimeRuler, x, trackAreaBottom);
      }
    }
    float barWidth = secondsPerBar * pps;
    if (barWidth >= 4) {
      g2.setColor(new Color(255, 255, 255, 40));
      for (float t = 0; t * pps < contentPanel.getWidth(); t += secondsPerBar) {
        int x = scaleLabelWidth + (int) (t * pps);
        g2.drawLine(x, scaleTimeRuler, x, trackAreaBottom);
      }
    }
  }

  private void drawDragGhost(
      Graphics2D g2,
      int ghostTrackY,
      int scaleBaseTrack,
      int scaleLabelWidth,
      float pps,
      float originalStartTime,
      float duration) {
    int ghostY = ghostTrackY + 5;
    int ghostX = scaleLabelWidth + (int) (originalStartTime * pps);
    int ghostW = (int) (duration * pps);
    int ghostH = scaleBaseTrack - 10;

    Composite oldComposite = g2.getComposite();
    g2.setComposite(AlphaComposite.getInstance(AlphaComposite.SRC_OVER, 0.3f));
    g2.setColor(Theme.getInstance().ACCENT_BLUE.darker());
    g2.fillRoundRect(ghostX, ghostY, ghostW, ghostH, 8, 8);
    g2.setComposite(oldComposite);

    g2.setColor(Theme.getInstance().ACCENT_BLUE);
    Stroke oldStroke = g2.getStroke();
    g2.setStroke(
        new BasicStroke(
            1.5f, BasicStroke.CAP_BUTT, BasicStroke.JOIN_BEVEL, 0, new float[] {4, 4}, 0));
    g2.drawRoundRect(ghostX, ghostY, ghostW, ghostH, 8, 8);
    g2.setStroke(oldStroke);
  }

  private static final int CLIP_HEADER_H = TimelineConstants.CLIP_HEADER_HEIGHT;

  private void drawClips(
      Graphics2D g2,
      List<TimelineView.TrackTimeline> tracks,
      int scaleTimeRuler,
      int scaleLabelWidth,
      float pps,
      boolean isDragging,
      TimelineView.ClipRect draggingClip) {
    Color accentBlue = Theme.getInstance().ACCENT_BLUE;
    for (int i = 0; i < tracks.size(); i++) {
      if (tracks.get(i).hidden) continue;
      int scaleBaseTrack = Theme.getInstance().scale(view.getBaseTrackHeight(i));
      int y = scaleTimeRuler + Theme.getInstance().scale(view.getTrackY(i)) + 5;
      for (TimelineView.ClipRect clip : tracks.get(i).clips) {
        if (isDragging && clip == draggingClip) continue;
        int x = scaleLabelWidth + (int) (clip.startTime * pps);
        int w = (int) (clip.duration * pps);
        int h = scaleBaseTrack - 10;

        // Transparent content background
        g2.setColor(
            new Color(accentBlue.getRed(), accentBlue.getGreen(), accentBlue.getBlue(), 30));
        g2.fillRoundRect(x, y, w, h, 8, 8);

        // Clip header
        Shape oldClip = g2.getClip();
        g2.clipRect(x, y, w, h);
        g2.setColor(
            new Color(accentBlue.getRed(), accentBlue.getGreen(), accentBlue.getBlue(), 140));
        g2.fillRect(x, y, w, CLIP_HEADER_H);
        g2.setClip(oldClip);

        // Clip name in header
        if (w > 20) {
          oldClip = g2.getClip();
          g2.clipRect(x, y, w, CLIP_HEADER_H);
          g2.setColor(Color.WHITE);
          g2.setFont(
              Theme.getInstance().FONT_UI.deriveFont(Font.BOLD, Theme.getInstance().scale(10.0f)));
          g2.drawString(clip.name != null ? clip.name : "", x + 5, y + 11);
          g2.setClip(oldClip);
        }

        // Content area (below header)
        int contentY = y + CLIP_HEADER_H;
        int contentH = h - CLIP_HEADER_H;

        boolean isMidi =
            clip.path == null
                || clip.path.isEmpty()
                || clip.path.toLowerCase().endsWith(".mid")
                || clip.path.toLowerCase().endsWith(".midi");

        if (isMidi) {
          drawMidiPreview(g2, clip, x, contentY, w, contentH);
        } else {
          drawAudioWaveform(g2, clip, x, contentY, w, contentH);
        }

        // Border
        g2.setColor(accentBlue);
        g2.drawRoundRect(x, y, w, h, 8, 8);
      }
    }
  }

  private void drawMidiPreview(
      Graphics2D g2, TimelineView.ClipRect clip, int x, int y, int w, int h) {
    if (clip.waveform == null || clip.waveform.length == 0) return;
    g2.setColor(new Color(255, 255, 255, 200));
    Shape oldClip = g2.getClip();
    g2.clipRect(x, y, w, h);

    // Waveform ratios are normalized to [0,1] based on content duration.
    // Compute content pixel width: if contentDuration > 0, use that; else fall back
    // to visible.
    float contentW;
    float trimOffsetPx = 0;
    if (clip.contentDuration > 0 && clip.duration > 0) {
      // Both contentDuration and duration are in seconds.
      // Content width = the pixel width the full content would occupy.
      contentW = (clip.contentDuration / clip.duration) * w;
      // Trim offset moves content left within the visible window.
      trimOffsetPx = (clip.trimStartSec / clip.contentDuration) * contentW;
    } else {
      contentW = w;
    }

    for (int nIdx = 0; nIdx + 2 < clip.waveform.length; nIdx += 3) {
      float startRatio = clip.waveform[nIdx];
      float pitch = clip.waveform[nIdx + 1];
      float durationRatio = clip.waveform[nIdx + 2];
      // Position notes relative to content width, shifted by trim offset.
      int nx = x + (int) (startRatio * contentW - trimOffsetPx);
      int nw = (int) (durationRatio * contentW);
      if (nw < 2) nw = 2;
      int minPitch = 21, maxPitch = 108;
      float normalizedPitch = (pitch - minPitch) / (float) (maxPitch - minPitch);
      normalizedPitch = Math.max(0, Math.min(1, normalizedPitch));
      int nh = Math.max(2, h / 40);
      int ny = y + h - (int) (normalizedPitch * (h - nh)) - nh;
      if (nx < x + w && nx + nw >= x) {
        int drawX = Math.max(x, nx);
        int drawW = Math.min(x + w - drawX, nx + nw - drawX);
        g2.fillRect(drawX, ny, drawW, nh);
      }
    }
    g2.setClip(oldClip);
  }

  private void drawAudioWaveform(
      Graphics2D g2, TimelineView.ClipRect clip, int x, int y, int w, int h) {
    if (clip.waveform == null || clip.waveform.length == 0) return;
    g2.setColor(new Color(255, 255, 255, 120));
    Shape oldClip = g2.getClip();
    g2.clipRect(x, y, w, h);
    int midY = y + h / 2;
    int halfH = h / 2 - 4;

    // Compute content pixel width for trim/pad support
    float contentW;
    float trimOffsetPx = 0;
    if (clip.contentDuration > 0 && clip.duration > 0) {
      contentW = (clip.contentDuration / clip.duration) * w;
      trimOffsetPx = (clip.trimStartSec / clip.contentDuration) * contentW;
    } else {
      contentW = w;
    }

    for (int px = 0; px < w; px++) {
      // Map pixel position to content position, accounting for trim offset
      float contentPx = px + trimOffsetPx;
      int wfIdx = (int) (contentPx / contentW * clip.waveform.length);
      if (wfIdx < 0 || wfIdx >= clip.waveform.length) continue; // padding = silence
      float amp = clip.waveform[wfIdx];
      int barH = (int) (amp * halfH);
      g2.drawLine(x + px, midY - barH, x + px, midY + barH);
    }
    g2.setClip(oldClip);
  }

  private void drawClipAt(
      Graphics2D g2,
      TimelineView.ClipRect clip,
      int y,
      int scaleLabelWidth,
      float pps,
      int scaleTrackHeight,
      float alpha,
      Color borderColor) {
    int x = scaleLabelWidth + (int) (clip.startTime * pps);
    int w = (int) (clip.duration * pps);
    int h = scaleTrackHeight - 10;
    Color accentBlue = Theme.getInstance().ACCENT_BLUE;
    g2.setComposite(AlphaComposite.getInstance(AlphaComposite.SRC_OVER, alpha));

    // Transparent body
    g2.setColor(new Color(accentBlue.getRed(), accentBlue.getGreen(), accentBlue.getBlue(), 30));
    g2.fillRoundRect(x, y, w, h, 8, 8);

    // Header
    Shape oldClip = g2.getClip();
    g2.clipRect(x, y, w, h);
    g2.setColor(new Color(accentBlue.getRed(), accentBlue.getGreen(), accentBlue.getBlue(), 140));
    g2.fillRect(x, y, w, CLIP_HEADER_H);
    g2.setClip(oldClip);

    // Name
    if (w > 20) {
      oldClip = g2.getClip();
      g2.clipRect(x, y, w, CLIP_HEADER_H);
      g2.setColor(Color.WHITE);
      g2.setFont(
          Theme.getInstance().FONT_UI.deriveFont(Font.BOLD, Theme.getInstance().scale(10.0f)));
      g2.drawString(clip.name != null ? clip.name : "", x + 5, y + 11);
      g2.setClip(oldClip);
    }

    // Border
    g2.setColor(borderColor);
    g2.drawRoundRect(x, y, w, h, 8, 8);
    g2.setComposite(AlphaComposite.SrcOver);
  }

  private void drawTimeRuler(
      Graphics2D g2,
      JPanel contentPanel,
      int scaleTimeRuler,
      int scaleLabelWidth,
      float pps,
      float bpm,
      GridMode gridMode) {
    g2.setColor(Theme.getInstance().BG_DARKER);
    g2.fillRect(scaleLabelWidth, 0, contentPanel.getWidth() - scaleLabelWidth, scaleTimeRuler);
    g2.setColor(Theme.getInstance().TEXT_DIM);

    if (gridMode == GridMode.SECONDS) {
      for (int s = 0; s < 600; s += 5) {
        int x = scaleLabelWidth + (int) (s * pps);
        if (x > contentPanel.getWidth()) break;
        g2.drawLine(x, scaleTimeRuler - 10, x, scaleTimeRuler);
        g2.drawString(s + "s", x + 2, scaleTimeRuler - 12);
      }
    } else {
      float rulerSecondsPerBar = (60.0f / bpm) * 4;
      for (int bar = 0; bar < 200; bar++) {
        int x = scaleLabelWidth + (int) (bar * rulerSecondsPerBar * pps);
        if (x > contentPanel.getWidth()) break;
        g2.drawLine(x, scaleTimeRuler - 10, x, scaleTimeRuler);
        g2.drawString(String.valueOf(bar + 1), x + 3, scaleTimeRuler - 12);
      }
    }
  }
}
