package hibiki.ui;

import hibiki.BackendManager;
import hibiki.pb.commands.*;
import hibiki.pb.core.EntityRef;
import java.awt.*;
import java.awt.event.*;
import java.awt.geom.*;
import javax.swing.*;
import javax.swing.event.ChangeEvent;
import javax.swing.event.ChangeListener;

/**
 * Reverb device panel with early-reflections/decay visualization and arc-knob controls. Shows
 * pre-delay gap, early reflection spikes (comb filter positions), and an exponential decay envelope
 * controlled by room size and damping.
 */
public class ReverbDevicePanel extends JPanel {
  private static final int PARAM_ROOM_SIZE = 0;
  private static final int PARAM_DAMPING = 1;
  private static final int PARAM_MIX = 2;
  private static final int PARAM_PRE_DELAY = 3;
  private static final int PARAM_HP_FREQ = 4;
  private static final int PARAM_LP_FREQ = 5;
  private static final int PARAM_WIDTH = 6;
  private static final int PARAM_ENABLE = 7;
  private static final int TOTAL_PARAMS = 8;

  // Freeverb comb filter tunings (in samples @ 44.1kHz) — used for visualization
  private static final int[] COMB_TUNINGS = {1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617};

  private final int trackIndex;
  private final int pluginIndex;
  private final double[] params = new double[TOTAL_PARAMS];
  private boolean enabled = true;
  private final KnobPanel[] knobs = new KnobPanel[7];
  private boolean updatingFromBackend = false;
  private final ReverbCanvas reverbCanvas;
  private float inputDb = -100, outputDb = -100;

  public Runnable modToggleCallback;

  public ReverbDevicePanel(int trackIndex, int pluginIndex) {
    this.trackIndex = trackIndex;
    this.pluginIndex = pluginIndex;

    params[PARAM_ROOM_SIZE] = 0.5;
    params[PARAM_DAMPING] = 0.5;
    params[PARAM_MIX] = 0.3;
    params[PARAM_PRE_DELAY] = 0.1;
    params[PARAM_HP_FREQ] = 0.15;
    params[PARAM_LP_FREQ] = 0.85;
    params[PARAM_WIDTH] = 1.0;
    params[PARAM_ENABLE] = 1.0;

    Theme theme = Theme.getInstance();
    setLayout(new BorderLayout());
    setPreferredSize(new Dimension(theme.scale(440), theme.scale(240)));
    setMaximumSize(new Dimension(theme.scale(440), Short.MAX_VALUE));
    setBackground(theme.BG_MEDIUM);
    setBorder(BorderFactory.createLineBorder(theme.BORDER));

    // Header
    JPanel header = new JPanel(new BorderLayout());
    header.setBackground(new Color(0x4B3D8F));
    header.setBorder(BorderFactory.createEmptyBorder(2, 5, 2, 5));

    JLabel nameLabel = new JLabel("Reverb");
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

    JToggleButton enableBtn = new JToggleButton("On", enabled);
    enableBtn.setFont(theme.FONT_UI.deriveFont(theme.scale(9.0f)));
    enableBtn.setFocusPainted(false);
    enableBtn.addActionListener(
        e -> {
          enabled = enableBtn.isSelected();
          sendParam(PARAM_ENABLE, enabled ? 1.0 : 0.0);
        });
    btnPanel.add(enableBtn);

    JButton delBtn = new JButton("\u274C");
    delBtn.addActionListener(e -> sendRemove());
    btnPanel.add(delBtn);
    header.add(btnPanel, BorderLayout.EAST);
    add(header, BorderLayout.NORTH);

    // Reverb visualization
    reverbCanvas = new ReverbCanvas();
    add(reverbCanvas, BorderLayout.CENTER);

    // Knob row
    JPanel knobRow = new JPanel(new GridLayout(1, 7, 4, 0));
    knobRow.setBackground(theme.BG_DARK);
    knobRow.setBorder(BorderFactory.createEmptyBorder(6, 6, 6, 6));
    knobRow.setPreferredSize(new Dimension(0, theme.scale(80)));

    String[] names = {"Room", "Damp", "Mix", "Pre-Dly", "HP", "LP", "Width"};
    int[] paramIds = {
      PARAM_ROOM_SIZE,
      PARAM_DAMPING,
      PARAM_MIX,
      PARAM_PRE_DELAY,
      PARAM_HP_FREQ,
      PARAM_LP_FREQ,
      PARAM_WIDTH
    };
    for (int i = 0; i < 7; i++) {
      final int pi = paramIds[i];
      final int idx = i;
      knobs[i] = new KnobPanel(names[i], params[pi], pi);
      knobs[i].addChangeListener(
          e -> {
            if (!updatingFromBackend) {
              sendParam(pi, knobs[idx].getValue());
              reverbCanvas.repaint();
            }
          });
      knobRow.add(knobs[i]);
    }

    add(knobRow, BorderLayout.SOUTH);
  }

  public void updateParam(int paramId, double value) {
    if (paramId >= 0 && paramId < TOTAL_PARAMS) {
      params[paramId] = value;
      updatingFromBackend = true;
      int[] ids = {
        PARAM_ROOM_SIZE,
        PARAM_DAMPING,
        PARAM_MIX,
        PARAM_PRE_DELAY,
        PARAM_HP_FREQ,
        PARAM_LP_FREQ,
        PARAM_WIDTH
      };
      for (int i = 0; i < ids.length; i++) {
        if (ids[i] == paramId) {
          knobs[i].setValue(value);
          break;
        }
      }
      updatingFromBackend = false;
      reverbCanvas.repaint();
    }
  }

  /** Update real-time input/output levels for wet signal display. */
  public void setInputOutputLevel(float inDb, float outDb) {
    this.inputDb = inDb;
    this.outputDb = outDb;
    reverbCanvas.repaint();
  }

  // ─── Reverb visualization ──────────────────────────────────────

  private class ReverbCanvas extends JPanel {
    private final Color REFLECTION_COLOR = new Color(0xA99AFF);
    private final Color GRID_COLOR = new Color(0x3A3A3A);
    private final Color LABEL_COLOR = new Color(0x808080);

    ReverbCanvas() {
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

      // Parameters
      double roomSize = params[PARAM_ROOM_SIZE];
      double damping = params[PARAM_DAMPING];
      double preDelayMs = params[PARAM_PRE_DELAY] * 100.0; // 0-100ms
      double mix = params[PARAM_MIX];

      // Determine timeline length based on room size (bigger room = longer tail)
      double rt60Ms = 200 + roomSize * 4800; // 200ms to 5s
      double totalMs = preDelayMs + rt60Ms * 1.2;
      totalMs = Math.max(500, Math.min(6000, totalMs));

      // Grid
      g2.setFont(Theme.getInstance().FONT_UI.deriveFont(Theme.getInstance().scale(7.0f)));

      // dB grid lines
      float[] dbLevels = {-6, -12, -18, -24};
      for (float db : dbLevels) {
        float amp = (float) Math.pow(10.0, db / 20.0);
        int y = plotB - (int) (amp * plotH);
        g2.setColor(GRID_COLOR);
        g2.drawLine(plotL, y, plotR, y);
        g2.setColor(LABEL_COLOR);
        g2.drawString(String.format("%.0f", db), pad, y + 3);
      }
      // 0dB line at top
      g2.setColor(GRID_COLOR);
      g2.drawLine(plotL, plotT, plotR, plotT);
      g2.setColor(LABEL_COLOR);
      g2.drawString("0", pad + 2, plotT + Theme.getInstance().scale(8));

      // Baseline
      g2.setColor(GRID_COLOR);
      g2.drawLine(plotL, plotB, plotR, plotB);

      // Time axis labels
      int numTimeLabels = 5;
      for (int i = 0; i <= numTimeLabels; i++) {
        double t = totalMs * i / numTimeLabels;
        int x = plotL + (int) (plotW * i / numTimeLabels);
        g2.setColor(GRID_COLOR);
        g2.drawLine(x, plotT, x, plotB);
        g2.setColor(LABEL_COLOR);
        String label = t >= 1000 ? String.format("%.1fs", t / 1000) : String.format("%.0fms", t);
        g2.drawString(label, x + 2, plotB + Theme.getInstance().scale(10));
      }

      // Pre-delay marker
      int preDelayX = plotL + (int) (preDelayMs / totalMs * plotW);
      if (preDelayMs > 0.5) {
        g2.setColor(new Color(0x5A5A5A));
        g2.fillRect(plotL, plotT, preDelayX - plotL, plotH);
        g2.setColor(LABEL_COLOR);
        g2.setFont(
            Theme.getInstance().FONT_UI.deriveFont(Font.ITALIC, Theme.getInstance().scale(7.0f)));
        g2.drawString("Pre-delay", plotL + 3, plotT + Theme.getInstance().scale(10));
        g2.setFont(Theme.getInstance().FONT_UI.deriveFont(Theme.getInstance().scale(7.0f)));
      }

      // Draw decay envelope (filled gradient)
      // RT60 decay: amplitude = e^(-6.9 * t / rt60)
      double decayRate = 6.9 / (rt60Ms / 1000.0); // per second
      // Damping makes high-freq decay faster, visually steepens the envelope
      double effectiveDecay = decayRate * (1.0 + damping * 0.5);

      // Build envelope path
      GeneralPath envelopePath = new GeneralPath();
      envelopePath.moveTo(preDelayX, plotB);
      int numPoints = plotW - (preDelayX - plotL);
      for (int px = 0; px <= numPoints; px++) {
        int x = preDelayX + px;
        double tSec = (px * totalMs / plotW) / 1000.0;
        double amp = mix * Math.exp(-effectiveDecay * tSec);
        int y = plotB - (int) (amp * plotH);
        y = Math.max(plotT, Math.min(plotB, y));
        if (px == 0) envelopePath.moveTo(x, plotB);
        envelopePath.lineTo(x, y);
      }
      envelopePath.lineTo(plotR, plotB);
      envelopePath.closePath();

      // Fill envelope with gradient
      GradientPaint envGrad =
          new GradientPaint(
              preDelayX,
              plotT,
              new Color(0x7B68EE, true),
              preDelayX,
              plotB,
              new Color(0x30, 0x20, 0x70, 40));
      g2.setPaint(envGrad);
      g2.fill(envelopePath);

      // Draw envelope outline
      g2.setColor(new Color(0x7B68EE));
      g2.setStroke(new BasicStroke(1.5f, BasicStroke.CAP_ROUND, BasicStroke.JOIN_ROUND));
      // Re-trace the top edge only
      GeneralPath topEdge = new GeneralPath();
      boolean first = true;
      for (int px = 0; px <= numPoints; px++) {
        int x = preDelayX + px;
        double tSec = (px * totalMs / plotW) / 1000.0;
        double amp = mix * Math.exp(-effectiveDecay * tSec);
        int y = plotB - (int) (amp * plotH);
        y = Math.max(plotT, Math.min(plotB, y));
        if (first) {
          topEdge.moveTo(x, y);
          first = false;
        } else topEdge.lineTo(x, y);
      }
      g2.draw(topEdge);

      // Early reflections: spikes at comb filter delay positions
      g2.setStroke(new BasicStroke(2.0f, BasicStroke.CAP_ROUND, BasicStroke.JOIN_ROUND));
      double sampleRate = 44100.0;
      for (int i = 0; i < COMB_TUNINGS.length; i++) {
        double combMs = preDelayMs + (COMB_TUNINGS[i] / sampleRate) * 1000.0;
        if (combMs > totalMs) continue;
        int x = plotL + (int) (combMs / totalMs * plotW);
        // Amplitude: scaled by room size and position (earlier = louder)
        double amp = mix * (0.5 + 0.5 * roomSize) * (1.0 - i * 0.08);
        amp *= (1.0 - damping * 0.3); // damping reduces reflection amplitude
        int barTop = plotB - (int) (amp * plotH);
        barTop = Math.max(plotT, barTop);

        // Color: brighter for early, dimmer for late
        int alpha = (int) (200 * (1.0 - i * 0.08));
        g2.setColor(
            new Color(
                REFLECTION_COLOR.getRed(),
                REFLECTION_COLOR.getGreen(),
                REFLECTION_COLOR.getBlue(),
                Math.max(60, alpha)));
        g2.drawLine(x, plotB, x, barTop); // spike line

        // Small dot at top
        g2.fillOval(x - 2, barTop - 2, 5, 5);
      }

      // Dry signal marker at t=0
      g2.setColor(new Color(0xCCCCCC));
      g2.setStroke(new BasicStroke(2.0f));
      int dryTop = plotB - (int) (plotH * 0.9);
      g2.drawLine(plotL, plotB, plotL, dryTop);
      g2.fillOval(plotL - 2, dryTop - 2, 5, 5);

      // Labels
      g2.setFont(
          Theme.getInstance().FONT_UI.deriveFont(Font.BOLD, Theme.getInstance().scale(8.0f)));
      g2.setColor(REFLECTION_COLOR);
      g2.drawString("Early", preDelayX + 4, plotT + Theme.getInstance().scale(10));
      g2.setColor(new Color(0x7B68EE));
      int decayLabelX = preDelayX + plotW / 3;
      g2.drawString("Decay", decayLabelX, plotT + Theme.getInstance().scale(10));

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

      // Output/wet meter (purple)
      float outAmp = (float) Math.pow(10.0, Math.max(-60, outputDb) / 20.0);
      int outH = (int) (outAmp * meterH);
      g2.setColor(new Color(0x7B68EE));
      g2.fillRect(meterX, meterTop + meterH - outH, meterW, outH);
      g2.setColor(REFLECTION_COLOR);
      g2.drawRect(meterX, meterTop, meterW, meterH);

      // Labels
      g2.setFont(Theme.getInstance().FONT_UI.deriveFont(Theme.getInstance().scale(6.0f)));
      g2.setColor(new Color(0x808080));
      g2.drawString("In", meterX - meterW - 2, meterTop - 2);
      g2.drawString("Out", meterX, meterTop - 2);

      g2.dispose();
    }
  }

  // ─── Knob panel ────────────────────────────────────────────────

  private class KnobPanel extends JPanel {
    private double value;
    private final int paramId;
    private final java.util.List<ChangeListener> listeners = new java.util.ArrayList<>();
    private int dragStartY;
    private final JLabel valLabel;

    KnobPanel(String name, double initialValue, int paramId) {
      this.value = initialValue;
      this.paramId = paramId;
      Theme theme = Theme.getInstance();
      setBackground(theme.BG_DARK);
      setLayout(new BorderLayout());

      JLabel nameLabel = new JLabel(name, SwingConstants.CENTER);
      nameLabel.setForeground(theme.TEXT_DIM);
      nameLabel.setFont(theme.FONT_UI.deriveFont(theme.scale(9.0f)));
      add(nameLabel, BorderLayout.NORTH);

      JPanel knobCanvas =
          new JPanel() {
            @Override
            protected void paintComponent(Graphics g) {
              super.paintComponent(g);
              paintArcKnob((Graphics2D) g.create(), getWidth(), getHeight(), value);
            }
          };
      knobCanvas.setOpaque(false);
      knobCanvas.setPreferredSize(new Dimension(theme.scale(32), theme.scale(32)));
      knobCanvas.setCursor(Cursor.getPredefinedCursor(Cursor.N_RESIZE_CURSOR));
      knobCanvas.addMouseListener(
          new MouseAdapter() {
            @Override
            public void mousePressed(MouseEvent e) {
              dragStartY = e.getY();
            }
          });
      knobCanvas.addMouseMotionListener(
          new MouseMotionAdapter() {
            @Override
            public void mouseDragged(MouseEvent e) {
              int dy = dragStartY - e.getY();
              dragStartY = e.getY();
              value = Math.max(0.0, Math.min(1.0, value + dy * 0.005));
              valLabel.setText(formatValue());
              knobCanvas.repaint();
              for (ChangeListener l : listeners) l.stateChanged(new ChangeEvent(KnobPanel.this));
            }
          });
      add(knobCanvas, BorderLayout.CENTER);

      valLabel = new JLabel(formatValue(), SwingConstants.CENTER);
      valLabel.setForeground(theme.TEXT_LIGHT);
      valLabel.setFont(theme.FONT_UI.deriveFont(theme.scale(8.0f)));
      add(valLabel, BorderLayout.SOUTH);
    }

    double getValue() {
      return value;
    }

    void setValue(double v) {
      value = v;
      valLabel.setText(formatValue());
      repaint();
    }

    void addChangeListener(ChangeListener l) {
      listeners.add(l);
    }

    private String formatValue() {
      if (paramId == PARAM_PRE_DELAY) {
        return String.format("%.1fms", value * 100.0);
      }
      if (paramId == PARAM_HP_FREQ) {
        float hz = 20.0f * (float) Math.pow(25.0f, value);
        return hz >= 1000 ? String.format("%.1fk", hz / 1000) : String.format("%.0f", hz);
      }
      if (paramId == PARAM_LP_FREQ) {
        float hz = 2000.0f * (float) Math.pow(10.0f, value);
        return hz >= 1000 ? String.format("%.1fk", hz / 1000) : String.format("%.0f", hz);
      }
      if (paramId == PARAM_MIX
          || paramId == PARAM_WIDTH
          || paramId == PARAM_ROOM_SIZE
          || paramId == PARAM_DAMPING) {
        return String.format("%.0f%%", value * 100);
      }
      return String.format("%.2f", value);
    }
  }

  private static void paintArcKnob(Graphics2D g2, int w, int h, double value) {
    g2.setRenderingHint(RenderingHints.KEY_ANTIALIASING, RenderingHints.VALUE_ANTIALIAS_ON);
    int sz = Math.min(w, h) - 4;
    int kx = (w - sz) / 2, ky = (h - sz) / 2;
    g2.setColor(new Color(0x3A3A3A));
    g2.setStroke(new BasicStroke(3.0f, BasicStroke.CAP_ROUND, BasicStroke.JOIN_ROUND));
    g2.drawArc(kx, ky, sz, sz, 225, -270);
    g2.setColor(new Color(0x7B68EE)); // purple for reverb
    g2.drawArc(kx, ky, sz, sz, 225, (int) (-270 * value));
    g2.setColor(new Color(0xEEEEEE));
    double angle = Math.toRadians(225 - 270 * value);
    int cx = kx + sz / 2 + (int) ((sz / 2 - 2) * Math.cos(angle));
    int cy = ky + sz / 2 - (int) ((sz / 2 - 2) * Math.sin(angle));
    g2.fillOval(cx - 2, cy - 2, 5, 5);
    g2.dispose();
  }

  // ─── Backend communication ─────────────────────────────────────

  private void sendParam(int paramId, double value) {
    BackendManager.getInstance()
        .sendRequest(
            Request.newBuilder()
                .setPlugin(
                    PluginCmd.newBuilder()
                        .setAction(PluginCmd.Action.ACTION_SET_PARAM)
                        .setTarget(
                            EntityRef.newBuilder()
                                .setTrackIndex(trackIndex)
                                .setPluginIndex(pluginIndex))
                        .setParamId(paramId)
                        .setParamValue((float) value))
                .build());
  }

  private void sendRemove() {
    BackendManager.getInstance()
        .sendRequest(
            Request.newBuilder()
                .setPlugin(
                    PluginCmd.newBuilder()
                        .setAction(PluginCmd.Action.ACTION_REMOVE)
                        .setTarget(
                            EntityRef.newBuilder()
                                .setTrackIndex(trackIndex)
                                .setPluginIndex(pluginIndex)))
                .build());
  }
}
