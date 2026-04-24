package hibiki.ui;

import hibiki.BackendManager;
import hibiki.pb.commands.*;
import hibiki.pb.core.EntityRef;
import java.awt.*;
import java.awt.event.*;
import java.awt.geom.*;
import javax.swing.*;

/**
 * Phaser device panel with Lissajous scope visualization (+20dB boost), mode selector strip, and
 * clockwise arc-knob controls.
 */
public class PhaserDevicePanel extends JPanel {
  private static final int PARAM_RATE = 0;
  private static final int PARAM_DEPTH = 1;
  private static final int PARAM_FEEDBACK = 2;
  private static final int PARAM_STAGES = 3;
  private static final int PARAM_MIX = 4;
  private static final int PARAM_STEREO = 5;
  private static final int PARAM_MODE = 6;
  private static final int PARAM_LFO_SHAPE = 7;
  private static final int PARAM_CENTER = 8;
  private static final int PARAM_SPREAD = 9;
  private static final int PARAM_SYNC = 10;
  private static final int PARAM_ENABLE = 11;

  private static final String[] MODE_NAMES = {"Phaser", "Chorus", "Flanger", "RingMod", "Disper."};
  private static final String[] LFO_NAMES = {"Sin", "Tri", "Saw", "Sqr"};

  private final int trackIndex, pluginIndex;
  private final KnobPanel rateKnob,
      depthKnob,
      fbKnob,
      mixKnob,
      stereoKnob,
      centerKnob,
      spreadKnob,
      stagesKnob;
  private final JToggleButton syncBtn, enableBtn;
  private int selectedMode = 0, selectedLfo = 0;
  private final LissajousCanvas scopeCanvas;
  private JPanel modeStrip, lfoStrip;
  public Runnable modToggleCallback;

  private float[] scopeL = new float[256];
  private float[] scopeR = new float[256];

  public PhaserDevicePanel(int trackIndex, int pluginIndex) {
    this.trackIndex = trackIndex;
    this.pluginIndex = pluginIndex;
    Theme theme = Theme.getInstance();

    setLayout(new BorderLayout());
    setPreferredSize(new Dimension(theme.scale(500), theme.scale(260)));
    setMaximumSize(new Dimension(theme.scale(500), Short.MAX_VALUE));
    setBackground(theme.BG_MEDIUM);
    setBorder(BorderFactory.createLineBorder(theme.BORDER));

    // Header
    JPanel header = new JPanel(new BorderLayout());
    header.setBackground(new Color(0x2E5984));
    header.setBorder(BorderFactory.createEmptyBorder(2, 5, 2, 5));
    JLabel nameLabel = new JLabel("Phaser");
    nameLabel.setForeground(Color.WHITE);
    nameLabel.setFont(theme.FONT_UI_BOLD);
    header.add(nameLabel, BorderLayout.CENTER);

    JPanel btnPanel = new JPanel(new FlowLayout(FlowLayout.RIGHT, 2, 0));
    btnPanel.setOpaque(false);
    enableBtn = new JToggleButton("On", true);
    enableBtn.setFont(theme.FONT_UI.deriveFont(theme.scale(9.0f)));
    enableBtn.addActionListener(e -> sendParam(PARAM_ENABLE, enableBtn.isSelected() ? 1.0 : 0.0));
    btnPanel.add(enableBtn);
    JButton modBtn = new JButton("Mod");
    modBtn.addActionListener(
        e -> {
          if (modToggleCallback != null) modToggleCallback.run();
        });
    btnPanel.add(modBtn);
    JButton scBtn = new JButton("SC");
    scBtn.addActionListener(e -> PluginPane.showSidechainPopup(scBtn, trackIndex, pluginIndex));
    btnPanel.add(scBtn);
    JButton delBtn = new JButton("\u274C");
    delBtn.addActionListener(e -> sendRemove());
    btnPanel.add(delBtn);
    header.add(btnPanel, BorderLayout.EAST);
    add(header, BorderLayout.NORTH);

    // Center: scope + controls
    JPanel centerPanel = new JPanel(new BorderLayout());
    centerPanel.setBackground(theme.BG_MEDIUM);

    scopeCanvas = new LissajousCanvas();
    scopeCanvas.setPreferredSize(new Dimension(theme.scale(160), 0));
    centerPanel.add(scopeCanvas, BorderLayout.WEST);

    // Right panel: mode + lfo strips + sync
    JPanel rightPanel = new JPanel(new BorderLayout());
    rightPanel.setBackground(theme.BG_MEDIUM);

    modeStrip = createButtonStrip(MODE_NAMES, idx -> {});
    lfoStrip = createButtonStrip(LFO_NAMES, idx -> {});
    // Wire click handlers after field assignment
    for (int i = 0; i < modeStrip.getComponentCount(); i++) {
      final int mi = i;
      ((JButton) modeStrip.getComponent(i))
          .addActionListener(
              e -> {
                selectedMode = mi;
                sendParam(PARAM_MODE, mi / 4.0);
                updateStripColors(modeStrip, mi);
              });
    }
    for (int i = 0; i < lfoStrip.getComponentCount(); i++) {
      final int li = i;
      ((JButton) lfoStrip.getComponent(i))
          .addActionListener(
              e -> {
                selectedLfo = li;
                sendParam(PARAM_LFO_SHAPE, li / 3.0);
                updateStripColors(lfoStrip, li);
              });
    }
    JPanel strips = new JPanel(new GridLayout(2, 1, 0, 2));
    strips.setBackground(theme.BG_MEDIUM);
    strips.add(modeStrip);
    strips.add(lfoStrip);
    rightPanel.add(strips, BorderLayout.NORTH);

    syncBtn = new JToggleButton("Sync");
    syncBtn.setFont(theme.FONT_UI.deriveFont(theme.scale(9.0f)));
    syncBtn.addActionListener(e -> sendParam(PARAM_SYNC, syncBtn.isSelected() ? 1.0 : 0.0));
    JPanel syncPanel = new JPanel(new FlowLayout(FlowLayout.CENTER));
    syncPanel.setBackground(theme.BG_MEDIUM);
    syncPanel.add(syncBtn);
    rightPanel.add(syncPanel, BorderLayout.CENTER);

    centerPanel.add(rightPanel, BorderLayout.CENTER);
    add(centerPanel, BorderLayout.CENTER);

    // Bottom: knobs
    JPanel knobRow = new JPanel(new GridLayout(1, 8, 4, 0));
    knobRow.setBackground(theme.BG_DARK);
    knobRow.setBorder(BorderFactory.createEmptyBorder(4, 4, 4, 4));
    knobRow.setPreferredSize(new Dimension(0, theme.scale(75)));

    rateKnob = new KnobPanel("Rate", PARAM_RATE, 0.3);
    depthKnob = new KnobPanel("Depth", PARAM_DEPTH, 0.7);
    fbKnob = new KnobPanel("Feedbk", PARAM_FEEDBACK, 0.4);
    stagesKnob = new KnobPanel("Stages", PARAM_STAGES, 0.33);
    mixKnob = new KnobPanel("Mix", PARAM_MIX, 0.5);
    stereoKnob = new KnobPanel("Stereo", PARAM_STEREO, 0.5);
    centerKnob = new KnobPanel("Center", PARAM_CENTER, 0.5);
    spreadKnob = new KnobPanel("Spread", PARAM_SPREAD, 0.5);

    knobRow.add(rateKnob);
    knobRow.add(depthKnob);
    knobRow.add(fbKnob);
    knobRow.add(stagesKnob);
    knobRow.add(mixKnob);
    knobRow.add(stereoKnob);
    knobRow.add(centerKnob);
    knobRow.add(spreadKnob);
    add(knobRow, BorderLayout.SOUTH);

    updateStripColors(modeStrip, 0);
    updateStripColors(lfoStrip, 0);
  }

  private JPanel createButtonStrip(String[] names, java.util.function.IntConsumer onClick) {
    Theme theme = Theme.getInstance();
    JPanel strip = new JPanel(new GridLayout(1, names.length, 2, 0));
    strip.setBackground(theme.BG_DARKER);
    strip.setBorder(BorderFactory.createEmptyBorder(2, 2, 2, 2));
    for (int i = 0; i < names.length; i++) {
      final int idx = i;
      JButton btn = new JButton(names[i]);
      btn.setFont(theme.FONT_UI.deriveFont(theme.scale(8.5f)));
      btn.setFocusPainted(false);
      btn.addActionListener(e -> onClick.accept(idx));
      strip.add(btn);
    }
    return strip;
  }

  private void updateStripColors(JPanel strip, int selected) {
    for (int i = 0; i < strip.getComponentCount(); i++) {
      JButton btn = (JButton) strip.getComponent(i);
      btn.setBackground(i == selected ? new Color(0x4A9BD9) : null);
      btn.setForeground(i == selected ? Color.WHITE : Theme.getInstance().TEXT_LIGHT);
    }
  }

  public void updateParam(int paramId, double value) {
    if (paramId == PARAM_RATE) rateKnob.setValue(value);
    else if (paramId == PARAM_DEPTH) depthKnob.setValue(value);
    else if (paramId == PARAM_FEEDBACK) fbKnob.setValue(value);
    else if (paramId == PARAM_STAGES) stagesKnob.setValue(value);
    else if (paramId == PARAM_MIX) mixKnob.setValue(value);
    else if (paramId == PARAM_STEREO) stereoKnob.setValue(value);
    else if (paramId == PARAM_CENTER) centerKnob.setValue(value);
    else if (paramId == PARAM_SPREAD) spreadKnob.setValue(value);
    else if (paramId == PARAM_MODE) {
      selectedMode = Math.min(4, Math.max(0, (int) (value * 4 + 0.5)));
      updateStripColors(modeStrip, selectedMode);
    } else if (paramId == PARAM_LFO_SHAPE) {
      selectedLfo = Math.min(3, Math.max(0, (int) (value * 3 + 0.5)));
      updateStripColors(lfoStrip, selectedLfo);
    } else if (paramId == PARAM_SYNC) syncBtn.setSelected(value >= 0.5);
    else if (paramId == PARAM_ENABLE) enableBtn.setSelected(value >= 0.5);
  }

  public void setInputOutputLevel(float inDb, float outDb) {}

  public void setScopeData(java.util.List<Float> left, java.util.List<Float> right) {
    int size = Math.min(left.size(), 256);
    for (int i = 0; i < size; i++) {
      scopeL[i] = left.get(i);
      scopeR[i] = right.get(i);
    }
    scopeCanvas.repaint();
  }

  // --- Lissajous Canvas with +20dB boost ---

  private class LissajousCanvas extends JPanel {
    LissajousCanvas() {
      setOpaque(true);
      setBackground(new Color(0x121212));
    }

    @Override
    protected void paintComponent(Graphics g) {
      super.paintComponent(g);
      Graphics2D g2 = (Graphics2D) g.create();
      g2.setRenderingHint(RenderingHints.KEY_ANTIALIASING, RenderingHints.VALUE_ANTIALIAS_ON);
      int w = getWidth(), h = getHeight();
      int cx = w / 2, cy = h / 2;
      int radius = Math.min(w, h) / 2 - 8;

      // Grid
      g2.setColor(new Color(0x2A2A2A));
      g2.drawLine(cx, 4, cx, h - 4);
      g2.drawLine(4, cy, w - 4, cy);
      g2.drawLine(cx - radius, cy - radius, cx + radius, cy + radius);
      g2.drawLine(cx - radius, cy + radius, cx + radius, cy - radius);
      g2.setColor(new Color(0x333333));
      g2.drawOval(cx - radius, cy - radius, radius * 2, radius * 2);

      // Lissajous trail with +20dB amplification (10x)
      float gain = 10.0f; // +20dB
      g2.setStroke(new BasicStroke(1.5f));
      for (int i = 1; i < 256; i++) {
        float alpha = (float) i / 256.0f;
        g2.setColor(new Color(0.29f, 0.61f, 0.85f, alpha * 0.8f));
        float xl = (scopeL[i - 1] + scopeR[i - 1]) * 0.5f * gain;
        float yl = (scopeL[i - 1] - scopeR[i - 1]) * 0.5f * gain;
        float xr = (scopeL[i] + scopeR[i]) * 0.5f * gain;
        float yr = (scopeL[i] - scopeR[i]) * 0.5f * gain;
        // Clamp to circle
        xl = Math.max(-1, Math.min(1, xl));
        yl = Math.max(-1, Math.min(1, yl));
        xr = Math.max(-1, Math.min(1, xr));
        yr = Math.max(-1, Math.min(1, yr));
        g2.drawLine(
            cx + (int) (xl * radius), cy - (int) (yl * radius),
            cx + (int) (xr * radius), cy - (int) (yr * radius));
      }
      g2.dispose();
    }
  }

  // --- KnobPanel (clockwise 0→100%, matching existing panels) ---

  private class KnobPanel extends JPanel {
    private double value;
    private final int paramId;
    private int dragStartY;
    private final JLabel valLabel;

    KnobPanel(String name, int paramId, double defaultVal) {
      this.paramId = paramId;
      this.value = defaultVal;
      Theme theme = Theme.getInstance();
      setBackground(theme.BG_DARK);
      setLayout(new BorderLayout());

      JLabel nameLabel = new JLabel(name, SwingConstants.CENTER);
      nameLabel.setForeground(theme.TEXT_DIM);
      nameLabel.setFont(theme.FONT_UI.deriveFont(theme.scale(8.5f)));
      add(nameLabel, BorderLayout.NORTH);

      JPanel knobArea =
          new JPanel() {
            @Override
            protected void paintComponent(Graphics g) {
              super.paintComponent(g);
              paintArcKnob((Graphics2D) g.create(), getWidth(), getHeight(), value);
            }
          };
      knobArea.setOpaque(false);
      knobArea.setCursor(Cursor.getPredefinedCursor(Cursor.N_RESIZE_CURSOR));
      knobArea.addMouseListener(
          new MouseAdapter() {
            public void mousePressed(MouseEvent e) {
              dragStartY = e.getY();
            }
          });
      knobArea.addMouseMotionListener(
          new MouseMotionAdapter() {
            public void mouseDragged(MouseEvent e) {
              int dy = dragStartY - e.getY();
              dragStartY = e.getY();
              value = Math.max(0.0, Math.min(1.0, value + dy * 0.005));
              sendParam(paramId, value);
              valLabel.setText(formatValue());
              repaint();
            }
          });
      add(knobArea, BorderLayout.CENTER);

      valLabel = new JLabel(formatValue(), SwingConstants.CENTER);
      valLabel.setForeground(theme.TEXT_LIGHT);
      valLabel.setFont(theme.FONT_UI.deriveFont(theme.scale(7.5f)));
      add(valLabel, BorderLayout.SOUTH);
    }

    void setValue(double v) {
      this.value = v;
      valLabel.setText(formatValue());
      repaint();
    }

    private String formatValue() {
      if (paramId == PARAM_STAGES) {
        int s = Math.max(0, Math.min(5, (int) (value * 5 + 0.5)));
        return String.valueOf(s * 2 + 2);
      }
      return String.format("%.0f%%", value * 100);
    }
  }

  /** Arc knob matching existing panel convention: clockwise 0→100%. */
  private static void paintArcKnob(Graphics2D g2, int w, int h, double value) {
    g2.setRenderingHint(RenderingHints.KEY_ANTIALIASING, RenderingHints.VALUE_ANTIALIAS_ON);
    int sz = Math.min(w, h) - 4;
    int kx = (w - sz) / 2, ky = (h - sz) / 2;
    g2.setColor(new Color(0x3A3A3A));
    g2.setStroke(new BasicStroke(3.0f, BasicStroke.CAP_ROUND, BasicStroke.JOIN_ROUND));
    g2.drawArc(kx, ky, sz, sz, 225, -270);
    g2.setColor(new Color(0x4A9BD9));
    g2.drawArc(kx, ky, sz, sz, 225, (int) (-270 * value));
    g2.setColor(new Color(0xEEEEEE));
    double angle = Math.toRadians(225 - 270 * value);
    int cx = kx + sz / 2 + (int) ((sz / 2 - 2) * Math.cos(angle));
    int cy = ky + sz / 2 - (int) ((sz / 2 - 2) * Math.sin(angle));
    g2.fillOval(cx - 2, cy - 2, 5, 5);
    g2.dispose();
  }

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
