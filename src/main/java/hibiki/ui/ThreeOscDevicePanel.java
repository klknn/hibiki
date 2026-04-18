package hibiki.ui;

import hibiki.BackendManager;
import hibiki.pb.commands.*;
import hibiki.pb.core.EntityRef;
import java.awt.*;
import java.awt.event.*;
import javax.swing.*;

/**
 * FL Studio 3xOSC-style synthesizer panel. 3 oscillators (sin/saw/square/tri) with coarse/fine
 * tune, volume, pan. Global gain ADSR + filter (LP/HP/BP) with ADSR modulation.
 */
public class ThreeOscDevicePanel extends JPanel {
  private static final int NUM_OSC = 3;
  private static final int PARAMS_PER_OSC = 5; // waveform, coarse, fine, vol, pan
  private static final int OSC_PARAMS = NUM_OSC * PARAMS_PER_OSC; // 15
  private static final int TOTAL_PARAMS = OSC_PARAMS + 14; // 29

  // Global param offsets (matching C++ Builtin3xOsc::ParamId)
  private static final int P_GAIN_A = 15, P_GAIN_D = 16, P_GAIN_S = 17, P_GAIN_R = 18;
  private static final int P_FILT_TYPE = 19, P_FILT_CUT = 20, P_FILT_RES = 21;
  private static final int P_FILT_A = 22, P_FILT_D = 23, P_FILT_S = 24, P_FILT_R = 25;
  private static final int P_FILT_DEPTH = 26, P_VOLUME = 27, P_ENABLE = 28;

  private static final String[] WAVE_NAMES = {"Sin", "Saw", "Sq", "Tri"};
  private static final double[] WAVE_NORMS = {0.0, 0.33, 0.67, 1.0};
  private static final String[] FILT_NAMES = {"LP", "HP", "BP"};
  private static final double[] FILT_NORMS = {0.0, 0.5, 1.0};

  private final int trackIndex;
  private final int pluginIndex;
  private final double[] params = new double[TOTAL_PARAMS];
  private boolean enabled = true;

  /** Callback invoked when user clicks Mod button; set by PluginPane wrapper. */
  public Runnable modToggleCallback;

  public ThreeOscDevicePanel(int trackIndex, int pluginIndex) {
    this.trackIndex = trackIndex;
    this.pluginIndex = pluginIndex;

    // Set defaults
    params[3] = 1.0; // osc1 vol
    params[4] = 0.5; // osc1 pan center
    params[1] = 0.5; // osc1 coarse center
    params[2] = 0.5; // osc1 fine center
    params[6] = 0.5;
    params[7] = 0.5; // osc2 coarse/fine
    params[9] = 0.5; // osc2 pan
    params[11] = 0.5;
    params[12] = 0.5; // osc3 coarse/fine
    params[14] = 0.5; // osc3 pan
    params[P_GAIN_S] = 0.7;
    params[P_GAIN_R] = 0.3;
    params[P_FILT_CUT] = 1.0;
    params[P_FILT_DEPTH] = 0.5;
    params[P_VOLUME] = 0.8;
    params[P_ENABLE] = 1.0;

    setLayout(new BorderLayout());
    Theme theme = Theme.getInstance();
    setPreferredSize(new Dimension(theme.scale(440), theme.scale(340)));
    setMaximumSize(new Dimension(theme.scale(440), Short.MAX_VALUE));
    setBackground(theme.BG_MEDIUM);
    setBorder(BorderFactory.createLineBorder(theme.BORDER));

    // Header
    JPanel header = new JPanel(new BorderLayout());
    header.setBackground(new Color(0x2D8A4E));
    header.setBorder(BorderFactory.createEmptyBorder(2, 5, 2, 5));
    JLabel nameLabel = new JLabel("3xOsc");
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
          sendParam(P_ENABLE, enabled ? 1.0 : 0.0);
        });
    btnPanel.add(enableBtn);
    header.add(btnPanel, BorderLayout.EAST);
    add(header, BorderLayout.NORTH);

    // Main content
    JPanel content = new JPanel();
    content.setLayout(new BoxLayout(content, BoxLayout.Y_AXIS));
    content.setBackground(theme.BG_MEDIUM);

    // Oscillator rows
    Color[] oscColors = {new Color(0x4080E0), new Color(0x40B040), new Color(0xE07020)};
    for (int osc = 0; osc < NUM_OSC; osc++) {
      content.add(createOscRow(osc, "OSC " + (osc + 1), oscColors[osc], theme));
    }

    // Separator
    content.add(Box.createVerticalStrut(theme.scale(4)));

    // ADSR + Filter section
    JPanel envPanel = new JPanel(new GridLayout(1, 2, theme.scale(4), 0));
    envPanel.setBackground(theme.BG_MEDIUM);
    envPanel.setBorder(BorderFactory.createEmptyBorder(4, 6, 6, 6));
    envPanel.add(createAdsrSection("GAIN ENV", P_GAIN_A, theme));
    envPanel.add(createFilterSection(theme));
    content.add(envPanel);

    // Volume
    JPanel volPanel = new JPanel(new FlowLayout(FlowLayout.LEFT, theme.scale(4), 0));
    volPanel.setBackground(theme.BG_MEDIUM);
    volPanel.add(createKnob("Vol", P_VOLUME, 0.8, theme));
    content.add(volPanel);

    add(content, BorderLayout.CENTER);
  }

  private JPanel createOscRow(int osc, String label, Color color, Theme theme) {
    int base = osc * PARAMS_PER_OSC;
    JPanel row = new JPanel(new FlowLayout(FlowLayout.LEFT, theme.scale(4), theme.scale(2)));
    row.setBackground(theme.BG_DARK);
    row.setBorder(
        BorderFactory.createCompoundBorder(
            BorderFactory.createMatteBorder(0, theme.scale(3), 0, 0, color),
            BorderFactory.createEmptyBorder(2, 6, 2, 6)));

    // Label
    JLabel l = new JLabel(label);
    l.setForeground(color);
    l.setFont(theme.FONT_UI_BOLD.deriveFont(theme.scale(9.0f)));
    l.setPreferredSize(new Dimension(theme.scale(40), theme.scale(16)));
    row.add(l);

    // Waveform selector buttons
    JPanel wavePanel = new JPanel(new GridLayout(1, 4, 1, 0));
    wavePanel.setOpaque(false);
    ButtonGroup bg = new ButtonGroup();
    for (int w = 0; w < WAVE_NAMES.length; w++) {
      JToggleButton btn = new JToggleButton(WAVE_NAMES[w]);
      btn.setFont(theme.FONT_UI.deriveFont(theme.scale(8.0f)));
      btn.setFocusPainted(false);
      btn.setMargin(new Insets(0, 1, 0, 1));
      btn.setPreferredSize(new Dimension(theme.scale(30), theme.scale(20)));
      if (w == 0 && osc == 0) btn.setSelected(true);
      final int waveIdx = w;
      final int paramBase = base;
      btn.addActionListener(
          e -> {
            params[paramBase] = WAVE_NORMS[waveIdx];
            sendParam(paramBase, WAVE_NORMS[waveIdx]);
          });
      bg.add(btn);
      wavePanel.add(btn);
    }
    row.add(wavePanel);

    // Coarse, Fine, Vol, Pan knobs
    row.add(createKnob("C", base + 1, 0.5, theme));
    row.add(createKnob("F", base + 2, 0.5, theme));
    row.add(createKnob("V", base + 3, osc == 0 ? 1.0 : 0.0, theme));
    row.add(createKnob("P", base + 4, 0.5, theme));

    return row;
  }

  private JPanel createAdsrSection(String title, int aParam, Theme theme) {
    JPanel p = new JPanel(new BorderLayout());
    p.setBackground(theme.BG_DARK);
    p.setBorder(
        BorderFactory.createCompoundBorder(
            BorderFactory.createLineBorder(theme.BORDER),
            BorderFactory.createEmptyBorder(3, 6, 3, 6)));

    JLabel lbl = new JLabel(title);
    lbl.setForeground(new Color(0xBBBBBB));
    lbl.setFont(theme.FONT_UI.deriveFont(theme.scale(9.0f)));
    p.add(lbl, BorderLayout.NORTH);

    JPanel knobs = new JPanel(new FlowLayout(FlowLayout.LEFT, theme.scale(4), 0));
    knobs.setOpaque(false);
    knobs.add(createKnob("A", aParam, 0.0, theme));
    knobs.add(createKnob("D", aParam + 1, 0.2, theme));
    knobs.add(createKnob("S", aParam + 2, 0.7, theme));
    knobs.add(createKnob("R", aParam + 3, 0.3, theme));
    p.add(knobs, BorderLayout.CENTER);

    return p;
  }

  private JPanel createFilterSection(Theme theme) {
    JPanel p = new JPanel(new BorderLayout());
    p.setBackground(theme.BG_DARK);
    p.setBorder(
        BorderFactory.createCompoundBorder(
            BorderFactory.createLineBorder(theme.BORDER),
            BorderFactory.createEmptyBorder(3, 6, 3, 6)));

    JLabel lbl = new JLabel("FILTER");
    lbl.setForeground(new Color(0xBBBBBB));
    lbl.setFont(theme.FONT_UI.deriveFont(theme.scale(9.0f)));
    p.add(lbl, BorderLayout.NORTH);

    // Row 1: Type, Cut, Res, Depth
    JPanel row1 = new JPanel(new FlowLayout(FlowLayout.LEFT, theme.scale(4), 0));
    row1.setOpaque(false);

    JComboBox<String> typeCombo = new JComboBox<>(FILT_NAMES);
    typeCombo.setFont(theme.FONT_UI.deriveFont(theme.scale(9.0f)));
    typeCombo.setPreferredSize(new Dimension(theme.scale(42), theme.scale(20)));
    typeCombo.addActionListener(
        e -> {
          int sel = typeCombo.getSelectedIndex();
          if (sel >= 0 && sel < FILT_NORMS.length) {
            params[P_FILT_TYPE] = FILT_NORMS[sel];
            sendParam(P_FILT_TYPE, FILT_NORMS[sel]);
          }
        });
    row1.add(typeCombo);
    row1.add(createKnob("Cut", P_FILT_CUT, 1.0, theme));
    row1.add(createKnob("Res", P_FILT_RES, 0.0, theme));
    row1.add(createKnob("Dep", P_FILT_DEPTH, 0.5, theme));

    // Row 2: ADSR
    JPanel row2 = new JPanel(new FlowLayout(FlowLayout.LEFT, theme.scale(4), 0));
    row2.setOpaque(false);
    row2.add(createKnob("A", P_FILT_A, 0.0, theme));
    row2.add(createKnob("D", P_FILT_D, 0.2, theme));
    row2.add(createKnob("S", P_FILT_S, 0.7, theme));
    row2.add(createKnob("R", P_FILT_R, 0.3, theme));

    JPanel rows = new JPanel();
    rows.setLayout(new BoxLayout(rows, BoxLayout.Y_AXIS));
    rows.setOpaque(false);
    rows.add(row1);
    rows.add(row2);
    p.add(rows, BorderLayout.CENTER);

    return p;
  }

  private JPanel createKnob(String label, int paramId, double defaultVal, Theme theme) {
    JPanel p = new JPanel(new BorderLayout());
    p.setOpaque(false);
    p.setPreferredSize(new Dimension(theme.scale(40), theme.scale(52)));

    KnobPanel knob = new KnobPanel(paramId, defaultVal);
    p.add(knob, BorderLayout.CENTER);

    JLabel l = new JLabel(label, SwingConstants.CENTER);
    l.setForeground(new Color(0x999999));
    l.setFont(theme.FONT_UI.deriveFont(theme.scale(8.0f)));
    p.add(l, BorderLayout.SOUTH);

    return p;
  }

  /** Called when plugin parameters change from the backend */
  public void handleParamChange(int paramId, double value) {
    if (paramId >= 0 && paramId < TOTAL_PARAMS) {
      params[paramId] = value;
      repaint();
    }
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

  // ── Mini arc-knob ─────────────────────────────────────────
  private class KnobPanel extends JPanel {
    private double value;
    private final int paramId;
    private int dragStartY;

    KnobPanel(int paramId, double defaultVal) {
      this.paramId = paramId;
      this.value = defaultVal;
      setOpaque(false);
      setPreferredSize(new Dimension(30, 30));
      setCursor(Cursor.getPredefinedCursor(Cursor.N_RESIZE_CURSOR));

      addMouseListener(
          new MouseAdapter() {
            @Override
            public void mousePressed(MouseEvent e) {
              dragStartY = e.getY();
            }
          });
      addMouseMotionListener(
          new MouseMotionAdapter() {
            @Override
            public void mouseDragged(MouseEvent e) {
              int dy = dragStartY - e.getY();
              dragStartY = e.getY();
              value = Math.max(0.0, Math.min(1.0, value + dy * 0.005));
              params[paramId] = value;
              sendParam(paramId, value);
              repaint();
            }
          });
    }

    @Override
    protected void paintComponent(Graphics g) {
      super.paintComponent(g);
      Graphics2D g2 = (Graphics2D) g.create();
      g2.setRenderingHint(RenderingHints.KEY_ANTIALIASING, RenderingHints.VALUE_ANTIALIAS_ON);
      int size = Math.min(getWidth(), getHeight()) - 2;
      int x = (getWidth() - size) / 2;
      int y = (getHeight() - size) / 2;

      // Background arc
      g2.setColor(new Color(0x333333));
      g2.setStroke(new BasicStroke(3.0f, BasicStroke.CAP_ROUND, BasicStroke.JOIN_ROUND));
      g2.drawArc(x, y, size, size, 225, -270);

      // Value arc
      int arcAngle = (int) (-270 * value);
      g2.setColor(new Color(0x4CAF50));
      g2.drawArc(x, y, size, size, 225, arcAngle);

      // Center dot
      g2.setColor(new Color(0xDDDDDD));
      double angle = Math.toRadians(225 - 270 * value);
      int cx = x + size / 2 + (int) ((size / 2 - 2) * Math.cos(angle));
      int cy = y + size / 2 - (int) ((size / 2 - 2) * Math.sin(angle));
      g2.fillOval(cx - 2, cy - 2, 4, 4);

      g2.dispose();
    }
  }
}
