package hibiki.ui;

import hibiki.BackendManager;
import hibiki.pb.commands.*;
import hibiki.pb.core.EntityRef;
import java.awt.*;
import java.awt.event.*;
import javax.swing.*;
import javax.swing.event.ChangeEvent;
import javax.swing.event.ChangeListener;

/**
 * OTT-style three-band multiband compressor device panel ("Hott"). Shows per-band horizontal GR
 * meters, crossover frequency controls, per-band output knobs, and global Amount/Time/Output knobs.
 */
public class HottDevicePanel extends JPanel {
  // Parameter IDs matching C++ BuiltinHott::ParamId
  private static final int PARAM_LOW_XOVER = 0;
  private static final int PARAM_HIGH_XOVER = 1;
  private static final int PARAM_AMOUNT = 2;
  private static final int PARAM_TIME = 3;
  private static final int PARAM_OUTPUT = 4;
  private static final int PARAM_LOW_OUT = 5;
  private static final int PARAM_MID_OUT = 6;
  private static final int PARAM_HIGH_OUT = 7;
  private static final int PARAM_ENABLE = 8;
  private static final int TOTAL_PARAMS = 9;

  // OTT default band settings (read-only display)
  private static final String[] BAND_NAMES = {"High", "Mid", "Low"};
  private static final float[] BAND_THRESH = {-29.9f, -16.4f, -29.9f};
  private static final float[] BAND_ATTACK = {13.5f, 22.4f, 47.8f};
  private static final float[] BAND_RELEASE = {132f, 282f, 282f};

  private final int trackIndex;
  private final int pluginIndex;
  private final double[] params = new double[TOTAL_PARAMS];
  private boolean enabled = true;
  private final float[] bandGrDb = {0, 0, 0}; // per-band gain reduction
  private float inputDb = -200, outputDb = -200;
  private final KnobPanel knobLowXover, knobHighXover;
  private final KnobPanel knobAmount, knobTime, knobOutput;
  private final KnobPanel knobLowOut, knobMidOut, knobHighOut;
  private final BandMeterPanel meterPanel;
  private boolean updatingFromBackend = false;

  /** Callback invoked when user clicks Mod button; set by PluginPane wrapper. */
  public Runnable modToggleCallback;

  public HottDevicePanel(int trackIndex, int pluginIndex) {
    this.trackIndex = trackIndex;
    this.pluginIndex = pluginIndex;

    // Defaults matching C++
    params[PARAM_LOW_XOVER] = 0.25;
    params[PARAM_HIGH_XOVER] = 0.65;
    params[PARAM_AMOUNT] = 1.0;
    params[PARAM_TIME] = 1.0;
    params[PARAM_OUTPUT] = 0.5;
    params[PARAM_LOW_OUT] = 0.62;
    params[PARAM_MID_OUT] = 0.71;
    params[PARAM_HIGH_OUT] = 0.71;
    params[PARAM_ENABLE] = 1.0;

    Theme theme = Theme.getInstance();
    setLayout(new BorderLayout());
    setPreferredSize(new Dimension(theme.scale(480), theme.scale(240)));
    setMaximumSize(new Dimension(theme.scale(480), Short.MAX_VALUE));
    setBackground(theme.BG_MEDIUM);
    setBorder(BorderFactory.createLineBorder(theme.BORDER));

    // ── Header ──
    JPanel header = new JPanel(new BorderLayout());
    header.setBackground(new Color(0x1A6B8A));
    header.setBorder(BorderFactory.createEmptyBorder(2, 5, 2, 5));

    JLabel nameLabel = new JLabel("⚡ Hott");
    nameLabel.setForeground(new Color(0x00D4FF));
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
    JToggleButton enableBtn = new JToggleButton("On", enabled);
    enableBtn.setFont(theme.FONT_UI.deriveFont(theme.scale(9.0f)));
    enableBtn.setFocusPainted(false);
    enableBtn.addActionListener(
        e -> {
          enabled = enableBtn.isSelected();
          sendParam(PARAM_ENABLE, enabled ? 1.0 : 0.0);
          repaint();
        });
    btnPanel.add(enableBtn);
    JButton delBtn = new JButton("\u274C");
    delBtn.addActionListener(e -> sendRemove());
    btnPanel.add(delBtn);
    header.add(btnPanel, BorderLayout.EAST);
    add(header, BorderLayout.NORTH);

    // ── Main body: [Left: split freq] [Center: meters] [Right: knobs] ──
    JPanel body = new JPanel(new BorderLayout(theme.scale(4), 0));
    body.setBackground(theme.BG_MEDIUM);
    body.setBorder(
        BorderFactory.createEmptyBorder(
            theme.scale(4), theme.scale(4), theme.scale(4), theme.scale(4)));

    // Left column: split freq knobs + labels
    JPanel leftCol = new JPanel();
    leftCol.setLayout(new BoxLayout(leftCol, BoxLayout.Y_AXIS));
    leftCol.setBackground(theme.BG_MEDIUM);
    leftCol.setPreferredSize(new Dimension(theme.scale(70), 0));

    JLabel splitLabel = new JLabel("Split Freq", SwingConstants.CENTER);
    splitLabel.setForeground(theme.TEXT_DIM);
    splitLabel.setFont(theme.FONT_UI.deriveFont(theme.scale(9.0f)));
    splitLabel.setAlignmentX(Component.CENTER_ALIGNMENT);
    leftCol.add(splitLabel);
    leftCol.add(Box.createVerticalStrut(theme.scale(4)));

    // High crossover
    JLabel hiLabel = new JLabel("High", SwingConstants.CENTER);
    hiLabel.setForeground(new Color(0x00D4FF));
    hiLabel.setFont(theme.FONT_UI_BOLD.deriveFont(theme.scale(9.0f)));
    hiLabel.setAlignmentX(Component.CENTER_ALIGNMENT);
    leftCol.add(hiLabel);

    knobHighXover =
        new KnobPanel(
            "HiFreq",
            params[PARAM_HIGH_XOVER],
            v -> String.format("%.1f kHz", normToFreq(v, 500, 20000) / 1000));
    knobHighXover.addChangeListener(e -> onKnobChanged(PARAM_HIGH_XOVER, knobHighXover));
    leftCol.add(knobHighXover);

    leftCol.add(Box.createVerticalGlue());

    // Low crossover
    JLabel loLabel = new JLabel("Low", SwingConstants.CENTER);
    loLabel.setForeground(new Color(0x00D4FF));
    loLabel.setFont(theme.FONT_UI_BOLD.deriveFont(theme.scale(9.0f)));
    loLabel.setAlignmentX(Component.CENTER_ALIGNMENT);
    leftCol.add(loLabel);

    knobLowXover =
        new KnobPanel(
            "LoFreq",
            params[PARAM_LOW_XOVER],
            v -> String.format("%.1f Hz", normToFreq(v, 20, 500)));
    knobLowXover.addChangeListener(e -> onKnobChanged(PARAM_LOW_XOVER, knobLowXover));
    leftCol.add(knobLowXover);

    body.add(leftCol, BorderLayout.WEST);

    // Center: band meters
    meterPanel = new BandMeterPanel();
    body.add(meterPanel, BorderLayout.CENTER);

    // Right column: per-band output + global output/time/amount
    JPanel rightCol = new JPanel();
    rightCol.setLayout(new BoxLayout(rightCol, BoxLayout.Y_AXIS));
    rightCol.setBackground(theme.BG_MEDIUM);
    rightCol.setPreferredSize(new Dimension(theme.scale(160), 0));

    // Per-band output knobs in a row
    JPanel bandOutRow = new JPanel(new GridLayout(1, 3, theme.scale(2), 0));
    bandOutRow.setOpaque(false);
    bandOutRow.setMaximumSize(new Dimension(Short.MAX_VALUE, theme.scale(72)));

    knobHighOut =
        new KnobPanel("Hi Out", params[PARAM_HIGH_OUT], v -> String.format("%.1f dB", v * 48 - 24));
    knobHighOut.addChangeListener(e -> onKnobChanged(PARAM_HIGH_OUT, knobHighOut));
    bandOutRow.add(knobHighOut);

    knobMidOut =
        new KnobPanel("Mid Out", params[PARAM_MID_OUT], v -> String.format("%.1f dB", v * 48 - 24));
    knobMidOut.addChangeListener(e -> onKnobChanged(PARAM_MID_OUT, knobMidOut));
    bandOutRow.add(knobMidOut);

    knobLowOut =
        new KnobPanel("Lo Out", params[PARAM_LOW_OUT], v -> String.format("%.1f dB", v * 48 - 24));
    knobLowOut.addChangeListener(e -> onKnobChanged(PARAM_LOW_OUT, knobLowOut));
    bandOutRow.add(knobLowOut);

    rightCol.add(bandOutRow);

    // Separator
    rightCol.add(Box.createVerticalStrut(theme.scale(4)));
    JSeparator sep = new JSeparator();
    sep.setMaximumSize(new Dimension(Short.MAX_VALUE, 1));
    sep.setForeground(theme.BORDER);
    rightCol.add(sep);
    rightCol.add(Box.createVerticalStrut(theme.scale(4)));

    // Global knobs row
    JPanel globalRow = new JPanel(new GridLayout(1, 3, theme.scale(2), 0));
    globalRow.setOpaque(false);
    globalRow.setMaximumSize(new Dimension(Short.MAX_VALUE, theme.scale(72)));

    knobOutput =
        new KnobPanel("Output", params[PARAM_OUTPUT], v -> String.format("%.1f dB", v * 48 - 24));
    knobOutput.addChangeListener(e -> onKnobChanged(PARAM_OUTPUT, knobOutput));
    globalRow.add(knobOutput);

    knobTime = new KnobPanel("Time", params[PARAM_TIME], v -> String.format("%.0f %%", v * 200));
    knobTime.addChangeListener(e -> onKnobChanged(PARAM_TIME, knobTime));
    globalRow.add(knobTime);

    knobAmount =
        new KnobPanel("Amount", params[PARAM_AMOUNT], v -> String.format("%.0f %%", v * 200));
    knobAmount.addChangeListener(e -> onKnobChanged(PARAM_AMOUNT, knobAmount));
    globalRow.add(knobAmount);

    rightCol.add(globalRow);
    rightCol.add(Box.createVerticalGlue());

    body.add(rightCol, BorderLayout.EAST);
    add(body, BorderLayout.CENTER);
  }

  private void onKnobChanged(int paramId, KnobPanel knob) {
    if (updatingFromBackend) return;
    params[paramId] = knob.getValue();
    sendParam(paramId, params[paramId]);
    meterPanel.repaint();
  }

  /** Update a parameter from backend notification. */
  public void updateParam(int paramId, float value) {
    if (paramId < 0 || paramId >= TOTAL_PARAMS) return;
    updatingFromBackend = true;
    params[paramId] = value;
    if (paramId == PARAM_ENABLE) {
      enabled = value >= 0.5;
    } else {
      getKnobForParam(paramId).setValue(value);
    }
    updatingFromBackend = false;
    meterPanel.repaint();
  }

  /** Update metering data from backend. */
  public void setGainReduction(float db) {
    bandGrDb[0] = db; // for now, low band from generic metering
    meterPanel.repaint();
  }

  /** Update real-time input/output levels. */
  public void setInputOutputLevel(float inDb, float outDb) {
    this.inputDb = inDb;
    this.outputDb = outDb;
    meterPanel.repaint();
  }

  private KnobPanel getKnobForParam(int paramId) {
    switch (paramId) {
      case PARAM_LOW_XOVER:
        return knobLowXover;
      case PARAM_HIGH_XOVER:
        return knobHighXover;
      case PARAM_AMOUNT:
        return knobAmount;
      case PARAM_TIME:
        return knobTime;
      case PARAM_OUTPUT:
        return knobOutput;
      case PARAM_LOW_OUT:
        return knobLowOut;
      case PARAM_MID_OUT:
        return knobMidOut;
      case PARAM_HIGH_OUT:
        return knobHighOut;
      default:
        return knobAmount; // fallback
    }
  }

  // ─── Band Meter Panel (OTT-style horizontal GR meters) ──────────

  private class BandMeterPanel extends JPanel {
    BandMeterPanel() {
      setBackground(Theme.getInstance().BG_DARKER);
    }

    @Override
    protected void paintComponent(Graphics g) {
      super.paintComponent(g);
      Graphics2D g2 = (Graphics2D) g.create();
      g2.setRenderingHint(RenderingHints.KEY_ANTIALIASING, RenderingHints.VALUE_ANTIALIAS_ON);
      Theme theme = Theme.getInstance();
      int w = getWidth(), h = getHeight();
      int pad = theme.scale(6);
      int bandH = (h - pad * 4) / 3;
      int meterW = w - pad * 2;

      // dB scale at bottom
      g2.setFont(theme.FONT_UI.deriveFont(theme.scale(7.0f)));
      g2.setColor(new Color(255, 255, 255, 60));
      int scaleY = h - pad + theme.scale(2);
      for (int db : new int[] {-80, -70, -60, -50, -40, -30, -20, -10, 0}) {
        float norm = (db + 80) / 80.0f;
        int x = pad + (int) (norm * meterW);
        g2.drawLine(x, pad, x, h - pad);
        g2.drawString(String.valueOf(Math.abs(db)), x - theme.scale(4), scaleY);
      }

      Color accentCyan = new Color(0x00D4FF);
      Color meterBg = new Color(0x2A3540);
      Color grColor = new Color(0xFF8C00); // orange GR indicator

      for (int b = 0; b < 3; ++b) {
        int y = pad + b * (bandH + pad);

        // Band background
        g2.setColor(meterBg);
        g2.fillRect(pad, y, meterW, bandH);

        // Gridlines inside meter
        g2.setColor(new Color(255, 255, 255, 15));
        for (int db : new int[] {-60, -40, -20}) {
          float norm = (db + 80) / 80.0f;
          int x = pad + (int) (norm * meterW);
          g2.drawLine(x, y, x, y + bandH);
        }

        // GR bar (horizontal, cyan)
        float grNorm = Math.min(1.0f, Math.abs(bandGrDb[b]) / 80.0f);
        int barW = (int) (grNorm * meterW);
        if (barW > 0) {
          g2.setColor(accentCyan);
          g2.fillRect(pad, y + theme.scale(2), barW, bandH - theme.scale(4));
        }

        // GR value text
        g2.setColor(accentCyan);
        g2.setFont(theme.FONT_UI_BOLD.deriveFont(theme.scale(10.0f)));
        String grText = String.format("%.1f", bandGrDb[b]);
        int textX = pad + barW + theme.scale(4);
        if (textX > w - theme.scale(40)) textX = pad + theme.scale(4);
        g2.drawString(grText, textX, y + bandH / 2 + theme.scale(4));

        // Band label (right side)
        g2.setFont(theme.FONT_UI.deriveFont(theme.scale(8.0f)));
        g2.setColor(new Color(255, 255, 255, 100));
        String bandInfo =
            String.format(
                "%s  Att %.1fms  Rel %.0fms", BAND_NAMES[b], BAND_ATTACK[b], BAND_RELEASE[b]);
        // Position at right edge
        FontMetrics fm = g2.getFontMetrics();
        g2.drawString(
            bandInfo,
            pad + meterW - fm.stringWidth(bandInfo) - theme.scale(2),
            y + theme.scale(10));

        // GR indicator line (vertical orange bar in the meter)
        if (grNorm > 0.01f) {
          g2.setColor(grColor);
          int indicatorX = pad + barW;
          g2.fillRect(indicatorX - 1, y + 1, 3, bandH - 2);
        }
      }

      g2.dispose();
    }
  }

  // ─── Knob Panel ─────────────────────────────────────────────────

  @FunctionalInterface
  interface ValueFormatter {
    String format(double value);
  }

  private class KnobPanel extends JPanel {
    private double value;
    private final String name;
    private final java.util.List<ChangeListener> listeners = new java.util.ArrayList<>();
    private int dragStartY;
    private final JLabel valLabel;
    private final ValueFormatter formatter;

    KnobPanel(String name, double initialValue, ValueFormatter formatter) {
      this.name = name;
      this.value = initialValue;
      this.formatter = formatter;
      Theme theme = Theme.getInstance();
      setBackground(theme.BG_DARK);
      setLayout(new BorderLayout());

      JLabel nameLabel = new JLabel(name, SwingConstants.CENTER);
      nameLabel.setForeground(theme.TEXT_DIM);
      nameLabel.setFont(theme.FONT_UI.deriveFont(theme.scale(8.0f)));
      add(nameLabel, BorderLayout.NORTH);

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

              // Value arc (cyan)
              int arcAngle = (int) (-270 * value);
              g2.setColor(new Color(0x00D4FF));
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
      knobCanvas.setPreferredSize(new Dimension(theme.scale(28), theme.scale(28)));
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
              valLabel.setText(formatter.format(value));
              knobCanvas.repaint();
              for (ChangeListener l : listeners) l.stateChanged(new ChangeEvent(KnobPanel.this));
            }
          });
      add(knobCanvas, BorderLayout.CENTER);

      valLabel = new JLabel(formatter.format(initialValue), SwingConstants.CENTER);
      valLabel.setForeground(theme.TEXT_LIGHT);
      valLabel.setFont(theme.FONT_UI.deriveFont(theme.scale(7.0f)));
      add(valLabel, BorderLayout.SOUTH);
    }

    double getValue() {
      return value;
    }

    void setValue(double v) {
      this.value = v;
      valLabel.setText(formatter.format(v));
      repaint();
    }

    void addChangeListener(ChangeListener l) {
      listeners.add(l);
    }
  }

  // ─── Utility ────────────────────────────────────────────────────

  private static float normToFreq(double norm, float minHz, float maxHz) {
    return (float) (minHz * Math.pow(maxHz / minHz, norm));
  }

  // ─── Backend communication ──────────────────────────────────────

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
