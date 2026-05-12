package hibiki.ui.panels.devices;

import hibiki.ui.PluginPane;
import hibiki.ui.Theme;
import hibiki.ui.panels.KnobPanel;
import java.awt.*;
import java.awt.event.*;
import java.awt.geom.*;
import javax.swing.*;

/**
 * Delay device panel with echo timeline visualization and arc-knob controls. The timeline shows L/R
 * echo taps decaying by feedback^n, with ping-pong mode.
 */
public class DelayDevicePanel extends AbstractDevicePanel {
  private static final Color ACCENT = new Color(0x4A9BD9);
  private static final int PARAM_TIME_L = 0;
  private static final int PARAM_TIME_R = 1;
  private static final int PARAM_FEEDBACK = 2;
  private static final int PARAM_MIX = 3;
  private static final int PARAM_HP_FREQ = 4;
  private static final int PARAM_LP_FREQ = 5;
  private static final int PARAM_PING_PONG = 6;
  private static final int PARAM_ENABLE = 7;
  private static final int PARAM_SYNC = 8;
  private static final int PARAM_SYNC_DIV = 9;
  private static final int TOTAL_PARAMS = 10;

  private final EchoCanvas echoCanvas;
  private boolean pingPong = false; // legacy, replaced by cross-talk knob
  private float inputDb = -100, outputDb = -100;

  public DelayDevicePanel(int trackIndex, int pluginIndex) {
    super(trackIndex, pluginIndex, TOTAL_PARAMS);

    params[PARAM_TIME_L] = 0.6;
    params[PARAM_TIME_R] = 0.6;
    params[PARAM_FEEDBACK] = 0.4;
    params[PARAM_MIX] = 0.3;
    params[PARAM_HP_FREQ] = 0.15;
    params[PARAM_LP_FREQ] = 0.75;
    params[PARAM_PING_PONG] = 1.0; // X-Talk: 100% = full ping-pong
    params[PARAM_ENABLE] = 1.0;
    params[PARAM_SYNC] = 1.0;
    params[PARAM_SYNC_DIV] = 0.6;

    Theme theme = Theme.getInstance();
    setLayout(new BorderLayout());
    setPreferredSize(new Dimension(theme.scale(440), theme.scale(240)));
    setMaximumSize(new Dimension(theme.scale(440), Short.MAX_VALUE));
    setBackground(theme.BG_MEDIUM);
    setBorder(BorderFactory.createLineBorder(theme.BORDER));

    // Header
    JPanel header = new JPanel(new BorderLayout());
    header.setBackground(new Color(0x2E5984));
    header.setBorder(BorderFactory.createEmptyBorder(2, 5, 2, 5));

    JLabel nameLabel = new JLabel("Delay");
    nameLabel.setForeground(Color.WHITE);
    nameLabel.setFont(theme.FONT_UI_BOLD);
    header.add(nameLabel, BorderLayout.CENTER);

    JPanel btnPanel = new JPanel(new FlowLayout(FlowLayout.RIGHT, 2, 0));
    btnPanel.setOpaque(false);

    JButton modBtn = new JButton("Mod");
    modBtn.addActionListener(
        e -> {
          if (modToggleCallback != null) modToggleCallback.run();
        });
    btnPanel.add(modBtn);

    JButton scBtn = new JButton("SC");
    scBtn.setToolTipText("Sidechain Source");
    scBtn.addActionListener(e -> PluginPane.showSidechainPopup(scBtn, trackIndex, pluginIndex));
    btnPanel.add(scBtn);

    JToggleButton enableBtn = new JToggleButton("On", params[PARAM_ENABLE] > 0.5);
    enableBtn.setFont(theme.FONT_UI.deriveFont(theme.scale(9.0f)));
    enableBtn.setFocusPainted(false);
    enableBtn.addActionListener(
        e -> {
          sendParam(PARAM_ENABLE, enableBtn.isSelected() ? 1.0 : 0.0);
        });
    btnPanel.add(enableBtn);

    JButton delBtn = new JButton("\u274C");
    delBtn.addActionListener(e -> sendRemove());
    btnPanel.add(delBtn);
    header.add(btnPanel, BorderLayout.EAST);
    add(header, BorderLayout.NORTH);

    // Echo visualization
    echoCanvas = new EchoCanvas();
    add(echoCanvas, BorderLayout.CENTER);

    // Knob row
    JPanel knobRow = new JPanel(new GridLayout(1, 9, 4, 0));
    knobRow.setBackground(theme.BG_DARK);
    knobRow.setBorder(BorderFactory.createEmptyBorder(6, 6, 6, 6));
    knobRow.setPreferredSize(new Dimension(0, theme.scale(80)));

    String[] names = {"Time L", "Time R", "Feedback", "Mix", "HP", "LP", "X-Talk"};
    int[] paramIds = {
      PARAM_TIME_L,
      PARAM_TIME_R,
      PARAM_FEEDBACK,
      PARAM_MIX,
      PARAM_HP_FREQ,
      PARAM_LP_FREQ,
      PARAM_PING_PONG
    };
    for (int i = 0; i < 7; i++) {
      final int pi = paramIds[i];
      KnobPanel knob = new KnobPanel(names[i], params[pi], makeFormatter(pi), ACCENT);
      knob.addChangeListener(
          e -> {
            if (!updatingFromBackend) {
              params[pi] = knob.getValue();
              sendParam(pi, params[pi]);
              echoCanvas.repaint();
            }
          });
      knobRow.add(knob);
    }

    // Sync toggle
    JToggleButton syncBtn = new JToggleButton("Sync", params[PARAM_SYNC] > 0.5);
    syncBtn.setFont(theme.FONT_UI.deriveFont(theme.scale(9.0f)));
    syncBtn.setFocusPainted(false);
    syncBtn.addActionListener(
        e -> {
          sendParam(PARAM_SYNC, syncBtn.isSelected() ? 1.0 : 0.0);
        });
    knobRow.add(syncBtn);

    add(knobRow, BorderLayout.SOUTH);
  }

  public void updateParam(int paramId, double value) {
    if (paramId < 0 || paramId >= params.length) return;
    updatingFromBackend = true;
    params[paramId] = value;
    updatingFromBackend = false;
    if (paramId == PARAM_PING_PONG) pingPong = value > 0.5;
    echoCanvas.repaint();
  }

  /** Update real-time input/output levels for wet signal display. */
  public void setInputOutputLevel(float inDb, float outDb) {
    this.inputDb = inDb;
    this.outputDb = outDb;
    echoCanvas.repaint();
  }

  // ─── Echo visualization ────────────────────────────────────────

  private class EchoCanvas extends JPanel {
    private final Color COLOR_L = new Color(0x4A9BD9);
    private final Color COLOR_R = new Color(0x7BC4FF);
    private final Color GRID_COLOR = new Color(0x3A3A3A);
    private final Color LABEL_COLOR = new Color(0x808080);
    private final int MAX_ECHOES = 16;

    EchoCanvas() {
      setOpaque(true);
      setBackground(new Color(0x1E1E1E));
    }

    @Override
    protected void paintComponent(Graphics g) {
      super.paintComponent(g);
      Graphics2D g2 = (Graphics2D) g.create();
      g2.setRenderingHint(RenderingHints.KEY_ANTIALIASING, RenderingHints.VALUE_ANTIALIAS_ON);

      int w = getWidth(), h = getHeight();
      int pad = Theme.getInstance().scale(4);
      int plotL = Theme.getInstance().scale(28);
      int plotR = w - pad;
      int plotT = pad;
      int plotB = h - Theme.getInstance().scale(14);
      int plotW = plotR - plotL;
      int plotH = plotB - plotT;
      int midY = plotT + plotH / 2;

      // Grid lines
      g2.setColor(GRID_COLOR);
      g2.setStroke(new BasicStroke(1.0f));
      g2.drawLine(plotL, midY, plotR, midY); // center line
      // dB grid: -6, -12, -18
      float[] dbLevels = {-6, -12, -18};
      g2.setFont(Theme.getInstance().FONT_UI.deriveFont(Theme.getInstance().scale(7.0f)));
      for (float db : dbLevels) {
        float amp = (float) Math.pow(10.0, db / 20.0);
        int yUp = midY - (int) (amp * plotH / 2);
        int yDn = midY + (int) (amp * plotH / 2);
        g2.setColor(GRID_COLOR);
        g2.drawLine(plotL, yUp, plotR, yUp);
        g2.drawLine(plotL, yDn, plotR, yDn);
        g2.setColor(LABEL_COLOR);
        g2.drawString(String.format("%.0f", db), pad, yUp + 3);
      }
      // 0 dB labels
      g2.setColor(LABEL_COLOR);
      g2.drawString("0", pad + 2, midY - 2);

      // Compute echo parameters
      double timeLMs = normToTimeMs(params[PARAM_TIME_L]);
      double timeRMs = normToTimeMs(params[PARAM_TIME_R]);
      double feedback = params[PARAM_FEEDBACK];
      double mix = params[PARAM_MIX];

      // Determine max time for the timeline
      double maxTimeMs = Math.max(timeLMs, timeRMs) * (MAX_ECHOES + 1);
      // Clamp to at least 100ms and at most 8000ms
      maxTimeMs = Math.max(200, Math.min(8000, maxTimeMs));

      // Draw time axis labels
      g2.setFont(Theme.getInstance().FONT_UI.deriveFont(Theme.getInstance().scale(7.0f)));
      int numTimeLabels = 5;
      for (int i = 0; i <= numTimeLabels; i++) {
        double t = maxTimeMs * i / numTimeLabels;
        int x = plotL + (int) (plotW * i / numTimeLabels);
        g2.setColor(GRID_COLOR);
        g2.drawLine(x, plotT, x, plotB);
        g2.setColor(LABEL_COLOR);
        String label = t >= 1000 ? String.format("%.1fs", t / 1000) : String.format("%.0fms", t);
        g2.drawString(label, x + 2, plotB + Theme.getInstance().scale(10));
      }

      // Draw echo bars
      int barWidth = Math.max(Theme.getInstance().scale(6), plotW / 40);

      if (pingPong) {
        // Ping-pong: alternating L and R
        double cumTimeMs = 0;
        boolean isLeft = true;
        for (int n = 0; n < MAX_ECHOES * 2; n++) {
          double dt = isLeft ? timeLMs : timeRMs;
          cumTimeMs += dt;
          if (cumTimeMs > maxTimeMs) break;
          double amp = mix * Math.pow(feedback, n + 1);
          if (amp < 0.01) break;

          int x = plotL + (int) (cumTimeMs / maxTimeMs * plotW) - barWidth / 2;
          int barH = (int) (amp * plotH / 2);
          x = Math.max(plotL, Math.min(plotR - barWidth, x));

          if (isLeft) {
            // L above center
            g2.setColor(
                new Color(
                    COLOR_L.getRed(),
                    COLOR_L.getGreen(),
                    COLOR_L.getBlue(),
                    (int) (180 * Math.min(1.0, amp * 2))));
            g2.fillRect(x, midY - barH, barWidth, barH);
            g2.setColor(COLOR_L);
            g2.drawRect(x, midY - barH, barWidth, barH);
          } else {
            // R below center
            g2.setColor(
                new Color(
                    COLOR_R.getRed(),
                    COLOR_R.getGreen(),
                    COLOR_R.getBlue(),
                    (int) (180 * Math.min(1.0, amp * 2))));
            g2.fillRect(x, midY, barWidth, barH);
            g2.setColor(COLOR_R);
            g2.drawRect(x, midY, barWidth, barH);
          }
          isLeft = !isLeft;
        }
      } else {
        // Normal: independent L and R
        for (int n = 1; n <= MAX_ECHOES; n++) {
          double amp = mix * Math.pow(feedback, n);
          if (amp < 0.01) break;

          // L bars (above center)
          double tL = timeLMs * n;
          if (tL <= maxTimeMs) {
            int x = plotL + (int) (tL / maxTimeMs * plotW) - barWidth / 2;
            int barH = (int) (amp * plotH / 2);
            x = Math.max(plotL, Math.min(plotR - barWidth, x));
            g2.setColor(
                new Color(
                    COLOR_L.getRed(),
                    COLOR_L.getGreen(),
                    COLOR_L.getBlue(),
                    (int) (180 * Math.min(1.0, amp * 2))));
            g2.fillRect(x, midY - barH, barWidth, barH);
            g2.setColor(COLOR_L);
            g2.drawRect(x, midY - barH, barWidth, barH);
          }

          // R bars (below center)
          double tR = timeRMs * n;
          if (tR <= maxTimeMs) {
            int x = plotL + (int) (tR / maxTimeMs * plotW) - barWidth / 2;
            int barH = (int) (amp * plotH / 2);
            x = Math.max(plotL, Math.min(plotR - barWidth, x));
            g2.setColor(
                new Color(
                    COLOR_R.getRed(),
                    COLOR_R.getGreen(),
                    COLOR_R.getBlue(),
                    (int) (180 * Math.min(1.0, amp * 2))));
            g2.fillRect(x, midY, barWidth, barH);
            g2.setColor(COLOR_R);
            g2.drawRect(x, midY, barWidth, barH);
          }
        }
      }

      // Draw dry signal marker at t=0
      g2.setColor(new Color(0xCCCCCC));
      int dryH = (int) (mix * plotH / 2);
      g2.fillRect(plotL, midY - dryH, barWidth, dryH);
      g2.fillRect(plotL, midY, barWidth, dryH);

      // Channel labels
      g2.setFont(
          Theme.getInstance().FONT_UI.deriveFont(Font.BOLD, Theme.getInstance().scale(8.0f)));
      g2.setColor(COLOR_L);
      g2.drawString(
          "L", plotR - Theme.getInstance().scale(14), plotT + Theme.getInstance().scale(10));
      g2.setColor(COLOR_R);
      g2.drawString(
          "R", plotR - Theme.getInstance().scale(14), plotB - Theme.getInstance().scale(3));

      // ── Wet signal level meters ──
      int meterW = Theme.getInstance().scale(6);
      int meterX = plotR - meterW - Theme.getInstance().scale(2);
      int meterH = plotH;
      int meterTop = plotT;

      // Input meter (dim gray)
      float inAmp = (float) Math.pow(10.0, Math.max(-60, inputDb) / 20.0);
      int inH = (int) (inAmp * meterH);
      g2.setColor(new Color(0x555555));
      g2.fillRect(meterX - meterW - 2, meterTop + meterH - inH, meterW, inH);
      g2.setColor(new Color(0x777777));
      g2.drawRect(meterX - meterW - 2, meterTop, meterW, meterH);

      // Output/wet meter (blue gradient)
      float outAmp = (float) Math.pow(10.0, Math.max(-60, outputDb) / 20.0);
      int outH = (int) (outAmp * meterH);
      g2.setColor(new Color(0x4A9BD9));
      g2.fillRect(meterX, meterTop + meterH - outH, meterW, outH);
      g2.setColor(COLOR_L);
      g2.drawRect(meterX, meterTop, meterW, meterH);

      // Labels
      g2.setFont(Theme.getInstance().FONT_UI.deriveFont(Theme.getInstance().scale(6.0f)));
      g2.setColor(LABEL_COLOR);
      g2.drawString("In", meterX - meterW - 2, meterTop - 2);
      g2.drawString("Out", meterX, meterTop - 2);

      g2.dispose();
    }
  }

  /** Maps normalized [0,1] to delay time in ms (1–2000ms exponential). */
  private static double normToTimeMs(double norm) {
    return 1.0 * Math.pow(2000.0, norm);
  }

  /** Maps normalized [0,1] to beat division label, mirroring C++ kDivisions table. */
  private static final String[] DIVISION_LABELS = {
    "1/32 T", "1/32", "1/16 T", "1/32 D", "1/16", "1/8 T", "1/16 D", "1/8",
    "1/4 T", "1/8 D", "1/4", "1/2 T", "1/4 D", "1/2", "1/2 D", "1 bar"
  };

  private static String getDivisionLabel(double norm) {
    int idx =
        Math.max(
            0,
            Math.min(
                DIVISION_LABELS.length - 1, (int) (norm * (DIVISION_LABELS.length - 1) + 0.5)));
    return DIVISION_LABELS[idx];
  }

  private ValueFormatter makeFormatter(int paramId) {
    return norm -> {
      if (paramId == PARAM_TIME_L || paramId == PARAM_TIME_R) {
        if (params[PARAM_SYNC] >= 0.5) return getDivisionLabel(norm);
        float ms = (float) normToTimeMs(norm);
        return ms < 100 ? String.format("%.1fms", ms) : String.format("%.0fms", ms);
      }
      if (paramId == PARAM_HP_FREQ) {
        float hz = 20.0f * (float) Math.pow(100.0f, norm);
        return hz >= 1000 ? String.format("%.1fk", hz / 1000) : String.format("%.0f", hz);
      }
      if (paramId == PARAM_LP_FREQ) {
        float hz = 1000.0f * (float) Math.pow(20.0f, norm);
        return hz >= 1000 ? String.format("%.1fk", hz / 1000) : String.format("%.0f", hz);
      }
      if (paramId == PARAM_FEEDBACK || paramId == PARAM_MIX)
        return String.format("%.0f%%", norm * 100);
      return String.format("%.2f", norm);
    };
  }
}
