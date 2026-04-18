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

/** Ableton Live-style Limiter device panel with arc-knobs and GR meter. */
public class LimiterDevicePanel extends JPanel {
  private static final int PARAM_CEILING = 0;
  private static final int PARAM_RELEASE = 1;
  private static final int PARAM_LOOKAHEAD = 2;
  private static final int PARAM_GAIN = 3;
  private static final int PARAM_LINK_STEREO = 4;
  private static final int PARAM_ENABLE = 5;
  private static final int TOTAL_PARAMS = 6;

  private final int trackIndex;
  private final int pluginIndex;
  private final double[] params = new double[TOTAL_PARAMS];
  private boolean enabled = true;
  private final KnobPanel[] knobs = new KnobPanel[4];
  private float gainReductionDb = 0;
  private final GrMeterPanel grMeter;
  private boolean updatingFromBackend = false;

  public Runnable modToggleCallback;

  public LimiterDevicePanel(int trackIndex, int pluginIndex) {
    this.trackIndex = trackIndex;
    this.pluginIndex = pluginIndex;

    params[PARAM_CEILING] = 0.975;
    params[PARAM_RELEASE] = 0.3;
    params[PARAM_LOOKAHEAD] = 0.2;
    params[PARAM_GAIN] = 0.0;
    params[PARAM_LINK_STEREO] = 1.0;
    params[PARAM_ENABLE] = 1.0;

    Theme theme = Theme.getInstance();
    setLayout(new BorderLayout());
    setPreferredSize(new Dimension(theme.scale(340), theme.scale(160)));
    setMaximumSize(new Dimension(theme.scale(340), Short.MAX_VALUE));
    setBackground(theme.BG_MEDIUM);
    setBorder(BorderFactory.createLineBorder(theme.BORDER));

    // Header
    JPanel header = new JPanel(new BorderLayout());
    header.setBackground(new Color(0xCC4400));
    header.setBorder(BorderFactory.createEmptyBorder(2, 5, 2, 5));

    JLabel nameLabel = new JLabel("Limiter");
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

    // Center: knobs + GR meter
    JPanel center = new JPanel(new BorderLayout());
    center.setBackground(theme.BG_DARK);

    JPanel knobRow = new JPanel(new GridLayout(1, 5, 4, 0));
    knobRow.setBackground(theme.BG_DARK);
    knobRow.setBorder(BorderFactory.createEmptyBorder(6, 6, 6, 6));
    knobRow.setPreferredSize(new Dimension(0, theme.scale(80)));

    String[] names = {"Ceiling", "Release", "Look", "Gain"};
    int[] paramIds = {PARAM_CEILING, PARAM_RELEASE, PARAM_LOOKAHEAD, PARAM_GAIN};
    for (int i = 0; i < 4; i++) {
      final int pi = paramIds[i];
      final int idx = i;
      knobs[i] = new KnobPanel(names[i], params[pi], pi);
      knobs[i].addChangeListener(
          e -> {
            if (!updatingFromBackend) {
              sendParam(pi, knobs[idx].getValue());
            }
          });
      knobRow.add(knobs[i]);
    }

    // Stereo link toggle
    JToggleButton linkBtn = new JToggleButton("Link", true);
    linkBtn.setFont(theme.FONT_UI.deriveFont(theme.scale(9.0f)));
    linkBtn.setFocusPainted(false);
    linkBtn.addActionListener(
        e -> {
          sendParam(PARAM_LINK_STEREO, linkBtn.isSelected() ? 1.0 : 0.0);
        });
    knobRow.add(linkBtn);

    center.add(knobRow, BorderLayout.CENTER);

    // GR meter on right side
    grMeter = new GrMeterPanel();
    grMeter.setPreferredSize(new Dimension(theme.scale(20), 0));
    center.add(grMeter, BorderLayout.EAST);

    add(center, BorderLayout.CENTER);
  }

  public void updateParam(int paramId, double value) {
    if (paramId >= 0 && paramId < TOTAL_PARAMS) {
      params[paramId] = value;
      updatingFromBackend = true;
      int[] ids = {PARAM_CEILING, PARAM_RELEASE, PARAM_LOOKAHEAD, PARAM_GAIN};
      for (int i = 0; i < ids.length; i++) {
        if (ids[i] == paramId) {
          knobs[i].setValue(value);
          break;
        }
      }
      updatingFromBackend = false;
    }
  }

  public void updateMetering(float grDb) {
    this.gainReductionDb = grDb;
    grMeter.repaint();
  }

  // ─── GR Meter ──────────────────────────────────────────────────

  private class GrMeterPanel extends JPanel {
    @Override
    protected void paintComponent(Graphics g) {
      super.paintComponent(g);
      Graphics2D g2 = (Graphics2D) g.create();
      Theme theme = Theme.getInstance();
      g2.setColor(theme.BG_DARKER);
      g2.fillRect(0, 0, getWidth(), getHeight());

      // GR bar (grows downward from top)
      float gr = Math.min(0, gainReductionDb);
      float frac = Math.min(1.0f, Math.abs(gr) / 24.0f);
      int barH = (int) (getHeight() * frac);
      g2.setColor(new Color(0xFF4444));
      g2.fillRect(2, 0, getWidth() - 4, barH);

      // Label
      g2.setColor(theme.TEXT_DIM);
      g2.setFont(theme.FONT_UI.deriveFont(theme.scale(7.0f)));
      g2.drawString("GR", 2, getHeight() - 3);
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
      if (paramId == PARAM_CEILING) {
        float db = (float) (value * 12.0 - 12.0);
        return String.format("%.1fdB", db);
      }
      if (paramId == PARAM_RELEASE) {
        float ms = 10.0f * (float) Math.pow(100.0f, value);
        return ms < 100 ? String.format("%.1fms", ms) : String.format("%.0fms", ms);
      }
      if (paramId == PARAM_LOOKAHEAD) {
        float ms = 0.1f * (float) Math.pow(50.0f, value);
        return String.format("%.1fms", ms);
      }
      if (paramId == PARAM_GAIN) {
        float db = (float) (value * 24.0);
        return String.format("%.1fdB", db);
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
    g2.setColor(new Color(0xFF6633)); // orange-red for limiter
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
