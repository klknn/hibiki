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

/** Aux return device panel with Gain and Pan arc-knobs. */
public class AuxDevicePanel extends JPanel {
  private static final int PARAM_GAIN = 0; // 0.0–1.0, mapped to -inf..+6 dB
  private static final int PARAM_PAN = 1; // 0.0–1.0, center = 0.5
  private static final int TOTAL_PARAMS = 2;

  private final int trackIndex;
  private final int pluginIndex;
  private final double[] params = new double[TOTAL_PARAMS];
  private final KnobPanel[] knobs = new KnobPanel[2];
  private boolean updatingFromBackend = false;

  public Runnable modToggleCallback;

  public AuxDevicePanel(int trackIndex, int pluginIndex) {
    this.trackIndex = trackIndex;
    this.pluginIndex = pluginIndex;

    params[PARAM_GAIN] = 0.7; // ~0 dB default
    params[PARAM_PAN] = 0.5; // center

    Theme theme = Theme.getInstance();
    setLayout(new BorderLayout());
    setPreferredSize(new Dimension(theme.scale(200), theme.scale(170)));
    setMaximumSize(new Dimension(theme.scale(200), Short.MAX_VALUE));
    setBackground(theme.BG_MEDIUM);
    setBorder(BorderFactory.createLineBorder(theme.BORDER));

    // Header bar
    JPanel header = new JPanel(new BorderLayout());
    header.setBackground(new Color(0x3388AA)); // teal for aux
    header.setBorder(BorderFactory.createEmptyBorder(2, 5, 2, 5));

    JLabel nameLabel = new JLabel("Aux");
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

    JButton delBtn = new JButton("\u274C");
    delBtn.addActionListener(e -> sendRemove());
    btnPanel.add(delBtn);
    header.add(btnPanel, BorderLayout.EAST);
    add(header, BorderLayout.NORTH);

    // Center: knobs
    JPanel knobRow = new JPanel(new GridLayout(1, 2, 8, 0));
    knobRow.setBackground(theme.BG_DARK);
    knobRow.setBorder(BorderFactory.createEmptyBorder(8, 16, 8, 16));
    knobRow.setPreferredSize(new Dimension(0, theme.scale(80)));

    knobs[0] = new KnobPanel("Gain", params[PARAM_GAIN], PARAM_GAIN);
    knobs[0].addChangeListener(
        e -> {
          if (!updatingFromBackend) sendParam(PARAM_GAIN, knobs[0].getValue());
        });
    knobRow.add(knobs[0]);

    knobs[1] = new KnobPanel("Pan", params[PARAM_PAN], PARAM_PAN);
    knobs[1].addChangeListener(
        e -> {
          if (!updatingFromBackend) sendParam(PARAM_PAN, knobs[1].getValue());
        });
    knobRow.add(knobs[1]);

    add(knobRow, BorderLayout.CENTER);

    // Bottom: send destination dropdown + send level
    JPanel sendRow = new JPanel(new BorderLayout(4, 0));
    sendRow.setBackground(theme.BG_DARK);
    sendRow.setBorder(BorderFactory.createEmptyBorder(4, 8, 6, 8));

    JLabel sendLabel = new JLabel("Send →");
    sendLabel.setForeground(theme.TEXT_DIM);
    sendLabel.setFont(theme.FONT_UI.deriveFont(theme.scale(9.0f)));
    sendRow.add(sendLabel, BorderLayout.WEST);

    JComboBox<String> destCombo = new JComboBox<>();
    destCombo.addItem("(none)");
    // Populate with available tracks (query SessionView for count)
    int trackCount = 16; // reasonable upper bound
    if (SessionView.getInstance() != null) {
      trackCount = SessionView.getInstance().getTrackCount();
    }
    for (int i = 0; i < trackCount; i++) {
      if (i == trackIndex) continue;
      destCombo.addItem("Track " + i);
    }
    destCombo.setFont(theme.FONT_UI.deriveFont(theme.scale(9.0f)));
    destCombo.setPreferredSize(new Dimension(theme.scale(80), theme.scale(20)));
    destCombo.addActionListener(
        e -> {
          int sel = destCombo.getSelectedIndex();
          if (sel <= 0) return; // "(none)" selected
          // Map combo index to track index (skip self)
          int destTrack = -1;
          int count = 0;
          int tc =
              SessionView.getInstance() != null ? SessionView.getInstance().getTrackCount() : 16;
          for (int i = 0; i < tc; i++) {
            if (i == trackIndex) continue;
            count++;
            if (count == sel) {
              destTrack = i;
              break;
            }
          }
          if (destTrack >= 0) {
            BackendManager.getInstance().setAuxSend(trackIndex, destTrack, 1.0f, false);
          }
        });
    sendRow.add(destCombo, BorderLayout.CENTER);

    add(sendRow, BorderLayout.SOUTH);
  }

  public void updateParam(int paramId, double value) {
    if (paramId >= 0 && paramId < TOTAL_PARAMS) {
      params[paramId] = value;
      updatingFromBackend = true;
      if (paramId < knobs.length) {
        knobs[paramId].setValue(value);
      }
      updatingFromBackend = false;
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
      knobCanvas.setPreferredSize(new Dimension(theme.scale(36), theme.scale(36)));
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
      if (paramId == PARAM_GAIN) {
        // 0.0 = -inf, 0.7 ≈ 0 dB, 1.0 = +6 dB (cubic curve)
        if (value <= 0.001) return "-∞ dB";
        double gain = value * value * value; // cubic
        float db = (float) (20.0 * Math.log10(gain));
        return String.format("%+.1f dB", db);
      }
      if (paramId == PARAM_PAN) {
        double pan = (value - 0.5) * 2.0; // -1..+1
        if (Math.abs(pan) < 0.02) return "C";
        if (pan < 0) return String.format("%.0fL", -pan * 50);
        return String.format("%.0fR", pan * 50);
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
    g2.setColor(new Color(0x33AACC)); // teal arc for aux
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
