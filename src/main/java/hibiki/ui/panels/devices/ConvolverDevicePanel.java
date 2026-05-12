package hibiki.ui.panels.devices;

import hibiki.BackendManager;
import hibiki.pb.commands.*;
import hibiki.pb.core.EntityRef;
import hibiki.ui.PluginPane;
import hibiki.ui.Theme;
import hibiki.ui.panels.KnobPanel;
import java.awt.*;
import java.awt.event.*;
import javax.swing.*;
import javax.swing.event.ChangeEvent;
import javax.swing.event.ChangeListener;

/**
 * Convolver device panel with IR file chooser and Dry/Wet/Pre-Delay knobs. Real-time FFT
 * partitioned convolution reverb / effect.
 */
public class ConvolverDevicePanel extends AbstractDevicePanel {
  private static final int PARAM_DRY = 0;
  private static final int PARAM_WET = 1;
  private static final int PARAM_PRE_DELAY = 2;
  private static final int PARAM_ENABLE = 3;
  private static final int TOTAL_PARAMS = 4;
  private boolean enabled = true;
  private final KnobPanel[] knobs = new KnobPanel[3];
  private String irName = "(no IR loaded)";
  private final JLabel irLabel;

  public Runnable modToggleCallback;

  public ConvolverDevicePanel(int trackIndex, int pluginIndex) {
    super(trackIndex, pluginIndex, TOTAL_PARAMS);

    params[PARAM_DRY] = 0.0;
    params[PARAM_WET] = 1.0;
    params[PARAM_PRE_DELAY] = 0.0;
    params[PARAM_ENABLE] = 1.0;

    Theme theme = Theme.getInstance();
    setLayout(new BorderLayout());
    setPreferredSize(new Dimension(theme.scale(340), theme.scale(180)));
    setMaximumSize(new Dimension(theme.scale(340), Short.MAX_VALUE));
    setBackground(theme.BG_MEDIUM);
    setBorder(BorderFactory.createLineBorder(theme.BORDER));

    // ── Header ──
    JPanel header = new JPanel(new BorderLayout());
    header.setBackground(new Color(0x2D6B4F));
    header.setBorder(BorderFactory.createEmptyBorder(2, 5, 2, 5));

    JLabel nameLabel = new JLabel("Convolver");
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

    // ── IR loader area ──
    JPanel irPanel = new JPanel(new BorderLayout());
    irPanel.setBackground(new Color(0x1E1E1E));
    irPanel.setBorder(BorderFactory.createEmptyBorder(8, 10, 8, 10));

    irLabel = new JLabel(irName, SwingConstants.CENTER);
    irLabel.setForeground(theme.TEXT_DIM);
    irLabel.setFont(theme.FONT_UI.deriveFont(Font.ITALIC, theme.scale(10.0f)));

    JButton loadIrBtn = new JButton("Load IR...");
    loadIrBtn.setFont(theme.FONT_UI.deriveFont(theme.scale(10.0f)));
    loadIrBtn.addActionListener(
        e -> {
          JFileChooser fc = new JFileChooser();
          fc.setFileFilter(new javax.swing.filechooser.FileNameExtensionFilter("WAV files", "wav"));
          if (fc.showOpenDialog(this) == JFileChooser.APPROVE_OPTION) {
            String path = fc.getSelectedFile().getAbsolutePath();
            irName = fc.getSelectedFile().getName();
            irLabel.setText(irName);
            // Reload plugin with ?ir= query to pass IR path to engine
            sendLoadWithIR(path);
          }
        });

    irPanel.add(irLabel, BorderLayout.CENTER);
    irPanel.add(loadIrBtn, BorderLayout.EAST);
    add(irPanel, BorderLayout.CENTER);

    // ── Knob row ──
    JPanel knobRow = new JPanel(new GridLayout(1, 3, 8, 0));
    knobRow.setBackground(theme.BG_DARK);
    knobRow.setBorder(BorderFactory.createEmptyBorder(6, 20, 6, 20));
    knobRow.setPreferredSize(new Dimension(0, theme.scale(80)));

    String[] names = {"Dry", "Wet", "Pre-Dly"};
    int[] paramIds = {PARAM_DRY, PARAM_WET, PARAM_PRE_DELAY};
    for (int i = 0; i < 3; i++) {
      final int pi = paramIds[i];
      final int idx = i;
      knobs[i] = new KnobPanel(names[i], params[pi], pi);
      knobs[i].addChangeListener(
          unused -> {
            if (!updatingFromBackend) {
              sendParam(pi, knobs[idx].getValue());
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
      int[] ids = {PARAM_DRY, PARAM_WET, PARAM_PRE_DELAY};
      for (int i = 0; i < ids.length; i++) {
        if (ids[i] == paramId) {
          knobs[i].setValue(value);
          break;
        }
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
              paintArcKnob(
                  (Graphics2D) g.create(), getWidth(), getHeight(), value, new Color(0x66BB6A));
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
      return String.format("%.0f%%", value * 100);
    }
  }

  // ─── Backend communication ─────────────────────────────────────

  private void sendLoadWithIR(String irPath) {
    // Reload the convolver plugin with the IR path encoded in the load path
    BackendManager.getInstance()
        .sendRequest(
            Request.newBuilder()
                .setPlugin(
                    PluginCmd.newBuilder()
                        .setAction(PluginCmd.Action.ACTION_LOAD)
                        .setTarget(
                            EntityRef.newBuilder()
                                .setTrackIndex(trackIndex)
                                .setPluginIndex(pluginIndex))
                        .setPath("builtin://convolver?ir=" + irPath))
                .build());
  }
}
