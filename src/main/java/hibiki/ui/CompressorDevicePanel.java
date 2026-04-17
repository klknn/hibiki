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
 * Ableton Live-style Compressor device panel. Shows a transfer curve (input vs output dB), gain
 * reduction meter, and parameter knobs.
 */
public class CompressorDevicePanel extends JPanel {
  // Parameter IDs (matching C++ BuiltinCompressor::ParamId)
  private static final int PARAM_THRESHOLD = 0;
  private static final int PARAM_RATIO = 1;
  private static final int PARAM_ATTACK = 2;
  private static final int PARAM_RELEASE = 3;
  private static final int PARAM_KNEE = 4;
  private static final int PARAM_MAKEUP = 5;
  private static final int PARAM_ENABLE = 6;
  private static final int PARAM_UP_THRESHOLD = 7;
  private static final int PARAM_UP_RATIO = 8;
  private static final int TOTAL_PARAMS = 9;

  private static final String[] PARAM_NAMES = {
    "Thresh", "Ratio", "Attack", "Release", "Knee", "Makeup", "Enable", "UpThresh", "UpRatio"
  };

  private final int trackIndex;
  private final int pluginIndex;
  private final double[] params = new double[TOTAL_PARAMS];
  private boolean enabled = true;
  private float gainReductionDb = 0;
  private final TransferCurvePanel curvePanel;
  private final GrMeterPanel grMeter;
  private final KnobPanel[] knobs = new KnobPanel[8]; // threshold..makeup + upthresh, upratio
  private boolean updatingFromBackend = false;

  /** Callback invoked when user clicks Mod button; set by PluginPane wrapper. */
  public Runnable modToggleCallback;

  // Real-time I/O levels for transfer curve dot
  private float inputLevelDb = -200;
  private float outputLevelDb = -200;

  public CompressorDevicePanel(int trackIndex, int pluginIndex) {
    this.trackIndex = trackIndex;
    this.pluginIndex = pluginIndex;

    // Defaults (matching C++)
    params[PARAM_THRESHOLD] = 1.0; // 0 dB
    params[PARAM_RATIO] = 0.0; // 1:1
    params[PARAM_ATTACK] = 0.3;
    params[PARAM_RELEASE] = 0.3;
    params[PARAM_KNEE] = 0.0;
    params[PARAM_MAKEUP] = 0.0;
    params[PARAM_ENABLE] = 1.0;
    params[PARAM_UP_THRESHOLD] = 0.0; // -60 dB (effectively off)
    params[PARAM_UP_RATIO] = 0.0;    // 1:1 (no upward comp)

    Theme theme = Theme.getInstance();
    setLayout(new BorderLayout());
    setPreferredSize(new Dimension(theme.scale(360), theme.scale(240)));
    setMaximumSize(new Dimension(theme.scale(360), Short.MAX_VALUE));
    setBackground(theme.BG_MEDIUM);
    setBorder(BorderFactory.createLineBorder(theme.BORDER));

    // Header
    JPanel header = new JPanel(new BorderLayout());
    header.setBackground(new Color(0x8B4513));
    header.setBorder(BorderFactory.createEmptyBorder(2, 5, 2, 5));

    JLabel nameLabel = new JLabel("Compressor");
    nameLabel.setForeground(Color.WHITE);
    nameLabel.setFont(theme.FONT_UI_BOLD);
    header.add(nameLabel, BorderLayout.CENTER);

    // Initialize curve panel early (before button listeners reference it)
    curvePanel = new TransferCurvePanel();
    grMeter = new GrMeterPanel();

    JPanel btnPanel = new JPanel(new FlowLayout(FlowLayout.RIGHT, 2, 0));
    btnPanel.setOpaque(false);

    JButton modBtn = new JButton("Mod");
    modBtn.addActionListener(
        e -> {
          if (modToggleCallback != null) modToggleCallback.run();
        });
    btnPanel.add(modBtn);

    JToggleButton enableBtn = new JToggleButton("On", enabled);
    enableBtn.setFont(theme.FONT_UI.deriveFont(theme.scale(9.0f)));
    enableBtn.setFocusPainted(false);
    enableBtn.addActionListener(
        e -> {
          enabled = enableBtn.isSelected();
          sendParam(PARAM_ENABLE, enabled ? 1.0 : 0.0);
          curvePanel.repaint();
        });
    btnPanel.add(enableBtn);

    JButton delBtn = new JButton("\u274C");
    delBtn.addActionListener(e -> sendRemove());
    btnPanel.add(delBtn);
    header.add(btnPanel, BorderLayout.EAST);
    add(header, BorderLayout.NORTH);

    // Center: transfer curve + GR meter
    JPanel centerPanel = new JPanel(new BorderLayout());
    centerPanel.setBackground(theme.BG_DARKER);
    centerPanel.add(curvePanel, BorderLayout.CENTER);
    grMeter.setPreferredSize(new Dimension(theme.scale(25), 0));
    centerPanel.add(grMeter, BorderLayout.EAST);

    add(centerPanel, BorderLayout.CENTER);

    // Bottom: knob row
    JPanel knobRow = new JPanel(new GridLayout(1, 8, theme.scale(2), 0));
    knobRow.setBackground(theme.BG_DARK);
    knobRow.setPreferredSize(new Dimension(0, theme.scale(68)));
    knobRow.setBorder(
        BorderFactory.createEmptyBorder(
            theme.scale(4), theme.scale(6), theme.scale(4), theme.scale(6)));

    // Knobs for params 0-5 (Thresh, Ratio, Attack, Release, Knee, Makeup)
    // plus 7-8 (UpThresh, UpRatio) — skip 6 (Enable, handled by toggle button)
    int[] knobParamIds = {0, 1, 2, 3, 4, 5, 7, 8};
    for (int k = 0; k < 8; k++) {
      final int paramId = knobParamIds[k];
      knobs[k] = new KnobPanel(PARAM_NAMES[paramId], params[paramId]);
      knobs[k].addChangeListener(
          e -> {
            if (updatingFromBackend) return;
            params[paramId] = knobs[findKnobIndex(paramId)].getValue();
            sendParam(paramId, params[paramId]);
            curvePanel.repaint();
          });
      knobRow.add(knobs[k]);
    }
    add(knobRow, BorderLayout.SOUTH);
  }

  /** Find the knob array index for a given param ID. */
  private int findKnobIndex(int paramId) {
    int[] knobParamIds = {0, 1, 2, 3, 4, 5, 7, 8};
    for (int i = 0; i < knobParamIds.length; i++) {
      if (knobParamIds[i] == paramId) return i;
    }
    return 0;
  }

  /** Update a parameter from backend notification. */
  public void updateParam(int paramId, float value) {
    if (paramId < 0 || paramId >= TOTAL_PARAMS) return;
    updatingFromBackend = true;
    params[paramId] = value;
    if (paramId == PARAM_ENABLE) {
      enabled = value >= 0.5;
    } else if (paramId != PARAM_ENABLE) {
      int ki = findKnobIndex(paramId);
      if (ki >= 0 && ki < knobs.length && knobs[ki] != null) {
        knobs[ki].setValue(value);
      }
    }
    updatingFromBackend = false;
    curvePanel.repaint();
    grMeter.repaint();
  }

  /** Update gain reduction meter from backend. */
  public void setGainReduction(float db) {
    gainReductionDb = db;
    grMeter.repaint();
  }

  /** Update real-time input/output levels for transfer curve dot. */
  public void setInputOutputLevel(float inDb, float outDb) {
    inputLevelDb = inDb;
    outputLevelDb = outDb;
    curvePanel.repaint();
  }

  // ─── Transfer curve panel ──────────────────────────────────────

  private class TransferCurvePanel extends JPanel {
    private static final float DB_MIN = -60, DB_MAX = 0;

    TransferCurvePanel() {
      setBackground(Theme.getInstance().BG_DARKER);
    }

    @Override
    protected void paintComponent(Graphics g) {
      super.paintComponent(g);
      Graphics2D g2 = (Graphics2D) g.create();
      g2.setRenderingHint(RenderingHints.KEY_ANTIALIASING, RenderingHints.VALUE_ANTIALIAS_ON);
      int w = getWidth(), h = getHeight();
      int pad = 2;
      int pw = w - pad * 2, ph = h - pad * 2;

      // Unity line (diagonal)
      g2.setColor(new Color(255, 255, 255, 30));
      g2.setStroke(new BasicStroke(0.5f));
      g2.drawLine(pad, pad, pad + pw, pad + ph);

      // Grid
      for (float db : new float[] {-48, -36, -24, -12}) {
        int x = dbToX(db, pw, pad);
        int y = dbToY(db, ph, pad);
        g2.setColor(new Color(255, 255, 255, 15));
        g2.drawLine(x, pad, x, pad + ph);
        g2.drawLine(pad, y, pad + pw, y);
      }

      if (!enabled) {
        g2.setColor(new Color(255, 255, 255, 40));
        g2.setFont(Theme.getInstance().FONT_UI_BOLD);
        g2.drawString("OFF", w / 2 - 12, h / 2);
        g2.dispose();
        return;
      }

      // Transfer curve
      float threshold = normToThreshold(params[PARAM_THRESHOLD]);
      float ratio = normToRatio(params[PARAM_RATIO]);
      float kneeDb = (float) (params[PARAM_KNEE] * 30.0);
      float makeup = (float) (params[PARAM_MAKEUP] * 30.0);

      g2.setColor(new Color(0xCD853F));
      g2.setStroke(new BasicStroke(2.0f));
      GeneralPath path = new GeneralPath();
      boolean first = true;
      float upThreshold = normToUpThreshold(params[PARAM_UP_THRESHOLD]);
      float upRatio = normToRatio(params[PARAM_UP_RATIO]);
      for (int px = 0; px <= pw; px++) {
        float inputDb = DB_MIN + (float) px / pw * (DB_MAX - DB_MIN);
        float outputDb = computeOutputDb(inputDb, threshold, ratio, kneeDb,
                                         upThreshold, upRatio) + makeup;
        outputDb = Math.max(DB_MIN, Math.min(DB_MAX, outputDb));
        int x = pad + px;
        int y = dbToY(outputDb, ph, pad);
        if (first) {
          path.moveTo(x, y);
          first = false;
        } else path.lineTo(x, y);
      }
      g2.draw(path);

      // Threshold marker
      int threshX = dbToX(threshold, pw, pad);
      g2.setColor(new Color(0xCD853F, true));
      g2.setStroke(
          new BasicStroke(
              1.0f, BasicStroke.CAP_BUTT, BasicStroke.JOIN_MITER, 10.0f, new float[] {4, 4}, 0));
      g2.drawLine(threshX, pad, threshX, pad + ph);

      // Labels
      g2.setFont(Theme.getInstance().FONT_UI.deriveFont(Theme.getInstance().scale(8.0f)));
      g2.setColor(new Color(255, 255, 255, 60));
      g2.drawString(String.format("%.0f dB", threshold), threshX + 2, pad + 10);
      String ratioStr = ratio > 100 ? "∞:1" : String.format("%.1f:1", ratio);
      g2.drawString(ratioStr, pad + 2, pad + ph - 3);

      // Real-time I/O dot on transfer curve
      if (inputLevelDb > -100 && outputLevelDb > -100) {
        float dotIn = Math.max(DB_MIN, Math.min(DB_MAX, inputLevelDb));
        float dotOut = Math.max(DB_MIN, Math.min(DB_MAX, outputLevelDb));
        int dx = dbToX(dotIn, pw, pad);
        int dy = dbToY(dotOut, ph, pad);

        // Glow effect
        g2.setColor(new Color(255, 200, 50, 40));
        g2.fillOval(dx - 8, dy - 8, 16, 16);
        g2.setColor(new Color(255, 200, 50, 80));
        g2.fillOval(dx - 5, dy - 5, 10, 10);
        // Core dot
        g2.setColor(new Color(0xFFC832));
        g2.fillOval(dx - 3, dy - 3, 6, 6);

        // Draw crosshair lines to axes for readability
        g2.setColor(new Color(255, 200, 50, 30));
        g2.setStroke(new BasicStroke(0.5f));
        g2.drawLine(dx, dy, dx, pad + ph);
        g2.drawLine(dx, dy, pad, dy);
      }

      g2.dispose();
    }

    private int dbToX(float db, int w, int pad) {
      return pad + (int) ((db - DB_MIN) / (DB_MAX - DB_MIN) * w);
    }

    private int dbToY(float db, int h, int pad) {
      return pad + h - (int) ((db - DB_MIN) / (DB_MAX - DB_MIN) * h);
    }
  }

  // ─── Gain reduction meter ──────────────────────────────────────

  private class GrMeterPanel extends JPanel {
    GrMeterPanel() {
      setBackground(Theme.getInstance().BG_DARKER);
      setBorder(BorderFactory.createMatteBorder(0, 1, 0, 0, Theme.getInstance().BORDER));
    }

    @Override
    protected void paintComponent(Graphics g) {
      super.paintComponent(g);
      Graphics2D g2 = (Graphics2D) g.create();
      int w = getWidth(), h = getHeight();
      int pad = 4;

      // GR bar (top-down, orange)
      float grNorm = Math.min(1, Math.abs(gainReductionDb) / 30.0f);
      int barH = (int) (grNorm * (h - pad * 2));
      if (barH > 0) {
        g2.setColor(new Color(0xCD853F));
        g2.fillRect(pad, pad, w - pad * 2, barH);
      }

      // GR label
      g2.setFont(Theme.getInstance().FONT_UI.deriveFont(Theme.getInstance().scale(7.0f)));
      g2.setColor(new Color(255, 255, 255, 80));
      String grStr = String.format("%.1f", gainReductionDb);
      g2.drawString(grStr, pad, h - pad);
      g2.drawString("GR", pad, pad + 8);

      g2.dispose();
    }
  }

  // ─── Knob panel ────────────────────────────────────────────────

  private class KnobPanel extends JPanel {
    private double value;
    private final String name;
    private final java.util.List<ChangeListener> listeners = new java.util.ArrayList<>();
    private int dragStartY;
    private final JLabel nameLabel;
    private final JLabel valLabel;

    KnobPanel(String name, double initialValue) {
      this.name = name;
      this.value = initialValue;
      Theme theme = Theme.getInstance();
      setBackground(theme.BG_DARK);
      setLayout(new BorderLayout());

      nameLabel = new JLabel(name, SwingConstants.CENTER);
      nameLabel.setForeground(theme.TEXT_DIM);
      nameLabel.setFont(theme.FONT_UI.deriveFont(theme.scale(9.0f)));
      add(nameLabel, BorderLayout.NORTH);

      // Arc knob canvas
      JPanel knobCanvas =
          new JPanel() {
            @Override
            protected void paintComponent(Graphics g) {
              super.paintComponent(g);
              Graphics2D g2 = (Graphics2D) g.create();
              g2.setRenderingHint(
                  RenderingHints.KEY_ANTIALIASING, RenderingHints.VALUE_ANTIALIAS_ON);
              int sz = Math.min(getWidth(), getHeight()) - 4;
              int kx = (getWidth() - sz) / 2;
              int ky = (getHeight() - sz) / 2;

              // Background arc
              g2.setColor(new Color(0x3A3A3A));
              g2.setStroke(new BasicStroke(3.0f, BasicStroke.CAP_ROUND, BasicStroke.JOIN_ROUND));
              g2.drawArc(kx, ky, sz, sz, 225, -270);

              // Value arc
              int arcAngle = (int) (-270 * value);
              g2.setColor(new Color(0xCD853F));
              g2.drawArc(kx, ky, sz, sz, 225, arcAngle);

              // Indicator dot
              g2.setColor(new Color(0xEEEEEE));
              double angle = Math.toRadians(225 - 270 * value);
              int cx = kx + sz / 2 + (int) ((sz / 2 - 2) * Math.cos(angle));
              int cy = ky + sz / 2 - (int) ((sz / 2 - 2) * Math.sin(angle));
              g2.fillOval(cx - 2, cy - 2, 5, 5);

              g2.dispose();
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
              valLabel.setText(formatValue(value));
              knobCanvas.repaint();
              for (ChangeListener l : listeners) {
                l.stateChanged(new ChangeEvent(KnobPanel.this));
              }
            }
          });
      add(knobCanvas, BorderLayout.CENTER);

      // Value display
      valLabel = new JLabel(formatValue(initialValue), SwingConstants.CENTER);
      valLabel.setForeground(theme.TEXT_LIGHT);
      valLabel.setFont(theme.FONT_UI.deriveFont(theme.scale(8.0f)));
      add(valLabel, BorderLayout.SOUTH);
    }

    double getValue() {
      return value;
    }

    void setValue(double v) {
      this.value = v;
      valLabel.setText(formatValue(v));
      repaint();
    }

    void addChangeListener(ChangeListener l) {
      listeners.add(l);
    }

    private String formatValue(double norm) {
      if ("Thresh".equals(name)) return String.format("%.1f dB", norm * 60 - 60);
      if ("Ratio".equals(name)) {
        float r = normToRatioStatic(norm);
        return r > 100 ? "\u221E:1" : String.format("%.1f:1", r);
      }
      if ("Attack".equals(name)) return String.format("%.1f ms", 0.1 * Math.pow(1000, norm));
      if ("Release".equals(name)) return String.format("%.0f ms", 10.0 * Math.pow(100, norm));
      if ("Knee".equals(name)) return String.format("%.1f dB", norm * 30);
      if ("Makeup".equals(name)) return String.format("%.1f dB", norm * 30);
      if ("UpThresh".equals(name)) return String.format("%.1f dB", norm * 72 - 60);
      if ("UpRatio".equals(name)) {
        float r = normToRatioStatic(norm);
        return r > 100 ? "\u221E:1" : String.format("%.1f:1", r);
      }
      return String.format("%.2f", norm);
    }

    private float normToRatioStatic(double norm) {
      if (norm >= 0.999) return 1000;
      return 1.0f / (1.0f - (float) norm);
    }
  }

  // ─── Param mapping (matching C++) ──────────────────────────────

  private static float normToThreshold(double norm) {
    return (float) (norm * 60.0 - 60.0);
  }

  private static float normToRatio(double norm) {
    if (norm >= 0.999) return 1000;
    return 1.0f / (1.0f - (float) norm);
  }

  private static float computeOutputDb(float inputDb, float threshold, float ratio, float kneeDb,
                                        float upThreshold, float upRatio) {
    float halfKnee = kneeDb / 2;
    float gr;
    if (kneeDb <= 0.01f) {
      gr = (inputDb > threshold) ? (threshold - inputDb) * (1 - 1 / ratio) : 0;
    } else {
      float lower = threshold - halfKnee;
      float upper = threshold + halfKnee;
      if (inputDb <= lower) gr = 0;
      else if (inputDb >= upper) gr = (threshold - inputDb) * (1 - 1 / ratio);
      else {
        float x = inputDb - lower;
        gr = -(1 - 1 / ratio) * x * x / (2 * kneeDb);
      }
    }
    // Upward compression
    if (upRatio > 1.001f && inputDb < upThreshold && inputDb > -100.0f) {
      float under = upThreshold - inputDb;
      float target = upThreshold - under / upRatio;
      gr += (target - inputDb);
    }
    return inputDb + gr;
  }

  private static float normToUpThreshold(double norm) {
    return (float) (norm * 72.0 - 60.0);
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
