package hibiki.ui;

import hibiki.BackendManager;
import hibiki.pb.commands.*;
import hibiki.pb.core.EntityRef;
import java.awt.*;
import java.awt.datatransfer.DataFlavor;
import java.awt.dnd.*;
import java.awt.event.*;
import java.awt.geom.*;
import java.io.File;
import java.util.List;
import javax.swing.*;
import javax.swing.filechooser.FileNameExtensionFilter;

/**
 * Ableton Simpler-style single-waveform sampler panel. Top: waveform display with draggable
 * start/end markers. Bottom: root note, gain ADSR, filter section.
 */
public class SamplerDevicePanel extends JPanel {
  private static final int TOTAL_PARAMS = 17;

  // Param IDs (matching C++ BuiltinSampler::ParamId)
  private static final int P_SAMPLE_START = 0, P_SAMPLE_END = 1, P_ROOT_NOTE = 2;
  private static final int P_GAIN_A = 3, P_GAIN_D = 4, P_GAIN_S = 5, P_GAIN_R = 6;
  private static final int P_FILT_TYPE = 7, P_FILT_CUT = 8, P_FILT_RES = 9;
  private static final int P_FILT_A = 10, P_FILT_D = 11, P_FILT_S = 12, P_FILT_R = 13;
  private static final int P_FILT_DEPTH = 14, P_VOLUME = 15, P_ENABLE = 16;

  private static final String[] FILT_NAMES = {"LP", "HP", "BP"};
  private static final double[] FILT_NORMS = {0.0, 0.5, 1.0};
  private static final String[] NOTE_NAMES = {
    "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
  };

  private final int trackIndex;
  private final int pluginIndex;
  private final double[] params = new double[TOTAL_PARAMS];
  private boolean enabled = true;
  private float[] waveform = null;
  private String sampleName = "(no sample)";
  private final WaveformPanel waveformPanel;

  /** Callback invoked when user clicks Mod button; set by PluginPane wrapper. */
  public Runnable modToggleCallback;

  public SamplerDevicePanel(int trackIndex, int pluginIndex) {
    this.trackIndex = trackIndex;
    this.pluginIndex = pluginIndex;

    // Defaults
    params[P_SAMPLE_END] = 1.0;
    params[P_ROOT_NOTE] = 60.0 / 127.0;
    params[P_GAIN_S] = 1.0;
    params[P_GAIN_R] = 0.3;
    params[P_FILT_CUT] = 1.0;
    params[P_FILT_DEPTH] = 0.5;
    params[P_VOLUME] = 0.8;
    params[P_ENABLE] = 1.0;

    setLayout(new BorderLayout());
    Theme theme = Theme.getInstance();
    setPreferredSize(new Dimension(theme.scale(440), theme.scale(330)));
    setMaximumSize(new Dimension(theme.scale(440), Short.MAX_VALUE));
    setBackground(theme.BG_MEDIUM);
    setBorder(BorderFactory.createLineBorder(theme.BORDER));

    // Header
    JPanel header = new JPanel(new BorderLayout());
    header.setBackground(new Color(0x8B5A2B));
    header.setBorder(BorderFactory.createEmptyBorder(2, 5, 2, 5));
    JLabel nameLabel = new JLabel("Sampler");
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

    JButton loadBtn = new JButton("Load");
    loadBtn.setFont(theme.FONT_UI.deriveFont(theme.scale(9.0f)));
    loadBtn.setFocusPainted(false);
    loadBtn.addActionListener(e -> loadSample());
    btnPanel.add(loadBtn);

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
    JPanel content = new JPanel(new BorderLayout());
    content.setBackground(theme.BG_MEDIUM);

    // Waveform display
    waveformPanel = new WaveformPanel();
    waveformPanel.setPreferredSize(new Dimension(0, theme.scale(120)));
    content.add(waveformPanel, BorderLayout.CENTER);

    // Controls panel
    JPanel controls = new JPanel();
    controls.setLayout(new BoxLayout(controls, BoxLayout.Y_AXIS));
    controls.setBackground(theme.BG_MEDIUM);
    controls.setBorder(BorderFactory.createEmptyBorder(3, 6, 6, 6));

    // Root note + Volume row
    JPanel topRow = new JPanel(new FlowLayout(FlowLayout.LEFT, theme.scale(4), 0));
    topRow.setBackground(theme.BG_MEDIUM);

    // Root note label
    JLabel rootLabel = new JLabel("Root: " + noteNameFromNorm(params[P_ROOT_NOTE]));
    rootLabel.setForeground(new Color(0xCCCCCC));
    rootLabel.setFont(theme.FONT_UI.deriveFont(theme.scale(9.0f)));
    topRow.add(createKnobWithLabel("Root", P_ROOT_NOTE, params[P_ROOT_NOTE], theme, rootLabel));
    topRow.add(createKnob("Vol", P_VOLUME, 0.8, theme));
    controls.add(topRow);

    // ADSR + Filter
    JPanel envPanel = new JPanel(new GridLayout(1, 2, theme.scale(4), 0));
    envPanel.setBackground(theme.BG_MEDIUM);
    envPanel.add(createAdsrSection("GAIN ENV", P_GAIN_A, theme));
    envPanel.add(createFilterSection(theme));
    controls.add(envPanel);

    content.add(controls, BorderLayout.SOUTH);
    add(content, BorderLayout.CENTER);
  }

  private void loadSample() {
    JFileChooser fc = new JFileChooser(".");
    fc.setFileFilter(new FileNameExtensionFilter("WAV Audio", "wav"));
    if (fc.showOpenDialog(this) == JFileChooser.APPROVE_OPTION) {
      sendLoadSample(fc.getSelectedFile().getAbsolutePath());
    }
  }

  /** Send ACTION_LOAD_SAMPLE to backend. Used by both file chooser and drop handler. */
  void sendLoadSample(String path) {
    BackendManager.getInstance()
        .sendRequest(
            Request.newBuilder()
                .setPlugin(
                    PluginCmd.newBuilder()
                        .setAction(PluginCmd.Action.ACTION_LOAD_SAMPLE)
                        .setTarget(
                            EntityRef.newBuilder()
                                .setTrackIndex(trackIndex)
                                .setPluginIndex(pluginIndex))
                        .setSamplePath(path))
                .build());
  }

  /** Called by PluginPane when PluginSampleData notification arrives */
  public void updateWaveform(List<Float> wf, String name) {
    float[] arr = new float[wf.size()];
    for (int i = 0; i < wf.size(); i++) arr[i] = wf.get(i);
    this.waveform = arr;
    this.sampleName = name;
    waveformPanel.repaint();
  }

  public void handleParamChange(int paramId, double value) {
    if (paramId >= 0 && paramId < TOTAL_PARAMS) {
      params[paramId] = value;
      repaint();
    }
  }

  // ── Waveform display with start/end markers ──────────────
  private class WaveformPanel extends JPanel {
    private int dragging = -1; // 0=start, 1=end
    private static final int HANDLE_W = 6;
    private final javax.swing.border.Border normalBorder =
        BorderFactory.createLineBorder(new Color(0x333355));
    private final javax.swing.border.Border dropBorder =
        BorderFactory.createLineBorder(new Color(0x55AAFF), 2);

    WaveformPanel() {
      setBackground(new Color(0x1A1A2E));
      setBorder(normalBorder);

      addMouseListener(
          new MouseAdapter() {
            @Override
            public void mousePressed(MouseEvent e) {
              int w = getWidth();
              int xStart = (int) (params[P_SAMPLE_START] * w);
              int xEnd = (int) (params[P_SAMPLE_END] * w);
              if (Math.abs(e.getX() - xStart) < HANDLE_W + 2) dragging = 0;
              else if (Math.abs(e.getX() - xEnd) < HANDLE_W + 2) dragging = 1;
            }

            @Override
            public void mouseReleased(MouseEvent e) {
              dragging = -1;
            }
          });
      addMouseMotionListener(
          new MouseMotionAdapter() {
            @Override
            public void mouseDragged(MouseEvent e) {
              if (dragging < 0) return;
              double norm = Math.max(0, Math.min(1.0, (double) e.getX() / getWidth()));
              if (dragging == 0) {
                params[P_SAMPLE_START] = Math.min(norm, params[P_SAMPLE_END] - 0.01);
                sendParam(P_SAMPLE_START, params[P_SAMPLE_START]);
              } else {
                params[P_SAMPLE_END] = Math.max(norm, params[P_SAMPLE_START] + 0.01);
                sendParam(P_SAMPLE_END, params[P_SAMPLE_END]);
              }
              repaint();
            }
          });

      // Drop target for audio files from browser, timeline, and OS file manager
      if (!java.awt.GraphicsEnvironment.isHeadless()) {
        new DropTarget(
            this,
            new DropTargetAdapter() {
              @Override
              public void dragEnter(DropTargetDragEvent dtde) {
                if (dtde.isDataFlavorSupported(DataFlavor.stringFlavor)
                    || dtde.isDataFlavorSupported(DataFlavor.javaFileListFlavor)) {
                  dtde.acceptDrag(DnDConstants.ACTION_COPY);
                  setBorder(dropBorder);
                } else {
                  dtde.rejectDrag();
                }
              }

              @Override
              public void dragExit(DropTargetEvent dte) {
                setBorder(normalBorder);
              }

              @Override
              public void drop(DropTargetDropEvent dtde) {
                setBorder(normalBorder);
                try {
                  // String flavor: browser drag ("audio:/path") or timeline ("audio:/path")
                  if (dtde.isDataFlavorSupported(DataFlavor.stringFlavor)) {
                    dtde.acceptDrop(DnDConstants.ACTION_COPY);
                    String data =
                        (String) dtde.getTransferable().getTransferData(DataFlavor.stringFlavor);
                    dtde.dropComplete(true);
                    String[] parts = data.split(":", 2);
                    if (parts.length == 2 && "audio".equals(parts[0])) {
                      sendLoadSample(parts[1]);
                    }
                    return;
                  }
                  // File list flavor: OS file manager drag
                  if (dtde.isDataFlavorSupported(DataFlavor.javaFileListFlavor)) {
                    dtde.acceptDrop(DnDConstants.ACTION_COPY);
                    @SuppressWarnings("unchecked")
                    java.util.List<File> files =
                        (java.util.List<File>)
                            dtde.getTransferable().getTransferData(DataFlavor.javaFileListFlavor);
                    dtde.dropComplete(true);
                    for (File f : files) {
                      String name = f.getName().toLowerCase();
                      if (name.endsWith(".wav")
                          || name.endsWith(".aiff")
                          || name.endsWith(".flac")) {
                        sendLoadSample(f.getAbsolutePath());
                        break;
                      }
                    }
                    return;
                  }
                  dtde.rejectDrop();
                } catch (Exception ex) {
                  java.util.logging.Logger.getLogger(SamplerDevicePanel.class.getName())
                      .log(java.util.logging.Level.WARNING, "Drop failed", ex);
                  dtde.rejectDrop();
                }
              }
            });
      }
    }

    @Override
    protected void paintComponent(Graphics g) {
      super.paintComponent(g);
      Graphics2D g2 = (Graphics2D) g.create();
      g2.setRenderingHint(RenderingHints.KEY_ANTIALIASING, RenderingHints.VALUE_ANTIALIAS_ON);
      int w = getWidth(), h = getHeight();

      if (waveform == null || waveform.length == 0) {
        g2.setColor(new Color(0x555577));
        g2.setFont(getFont().deriveFont(12.0f));
        String msg =
            sampleName.equals("(no sample)") ? "Click 'Load' to load a WAV sample" : sampleName;
        FontMetrics fm = g2.getFontMetrics();
        g2.drawString(msg, (w - fm.stringWidth(msg)) / 2, h / 2);
        g2.dispose();
        return;
      }

      // Draw waveform
      int startX = (int) (params[P_SAMPLE_START] * w);
      int endX = (int) (params[P_SAMPLE_END] * w);

      // Inactive regions (darker)
      g2.setColor(new Color(0x0D0D1A));
      g2.fillRect(0, 0, startX, h);
      g2.fillRect(endX, 0, w - endX, h);

      // Waveform bars
      for (int i = 0; i < waveform.length; i++) {
        int x = i * w / waveform.length;
        int barH = (int) (waveform[i] * h * 0.9f);
        boolean active = x >= startX && x <= endX;
        g2.setColor(active ? new Color(0x5599DD) : new Color(0x334455));
        g2.fillRect(x, (h - barH) / 2, Math.max(1, w / waveform.length - 1), barH);
      }

      // Start/End handles
      g2.setColor(new Color(0xFF8844));
      g2.setStroke(new BasicStroke(2));
      g2.drawLine(startX, 0, startX, h);
      g2.fillRect(startX - HANDLE_W / 2, 0, HANDLE_W, h / 4);

      g2.setColor(new Color(0xFF4444));
      g2.drawLine(endX, 0, endX, h);
      g2.fillRect(endX - HANDLE_W / 2, 0, HANDLE_W, h / 4);

      // Sample name
      g2.setColor(new Color(0xAABBCC));
      g2.setFont(getFont().deriveFont(10.0f));
      g2.drawString(sampleName, 4, h - 4);

      g2.dispose();
    }
  }

  // ── Shared helpers ────────────────────────────────────────
  private String noteNameFromNorm(double norm) {
    int midi = Math.max(0, Math.min(127, (int) (norm * 127)));
    return NOTE_NAMES[midi % 12] + (midi / 12 - 1);
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
    knobs.add(createKnob("S", aParam + 2, 1.0, theme));
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
    JPanel knobs = new JPanel(new FlowLayout(FlowLayout.LEFT, theme.scale(2), 0));
    knobs.setOpaque(false);
    JComboBox<String> typeCombo = new JComboBox<>(FILT_NAMES);
    typeCombo.setFont(theme.FONT_UI.deriveFont(theme.scale(8.0f)));
    typeCombo.setPreferredSize(new Dimension(theme.scale(36), theme.scale(18)));
    typeCombo.addActionListener(
        e -> {
          int sel = typeCombo.getSelectedIndex();
          if (sel >= 0 && sel < FILT_NORMS.length) {
            params[P_FILT_TYPE] = FILT_NORMS[sel];
            sendParam(P_FILT_TYPE, FILT_NORMS[sel]);
          }
        });
    knobs.add(typeCombo);
    knobs.add(createKnob("Cut", P_FILT_CUT, 1.0, theme));
    knobs.add(createKnob("Res", P_FILT_RES, 0.0, theme));
    knobs.add(createKnob("Dep", P_FILT_DEPTH, 0.5, theme));

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
    rows.add(knobs);
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

  private JPanel createKnobWithLabel(
      String label, int paramId, double defaultVal, Theme theme, JLabel displayLabel) {
    JPanel p = new JPanel(new BorderLayout());
    p.setOpaque(false);
    p.setPreferredSize(new Dimension(theme.scale(40), theme.scale(52)));
    KnobPanel knob =
        new KnobPanel(paramId, defaultVal) {
          @Override
          protected void onValueChanged() {
            displayLabel.setText("Root: " + noteNameFromNorm(value));
          }
        };
    p.add(knob, BorderLayout.CENTER);
    JLabel l = new JLabel(label, SwingConstants.CENTER);
    l.setForeground(new Color(0x999999));
    l.setFont(theme.FONT_UI.deriveFont(theme.scale(8.0f)));
    p.add(l, BorderLayout.SOUTH);
    return p;
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
    protected double value;
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
              onValueChanged();
              repaint();
            }
          });
    }

    protected void onValueChanged() {}

    @Override
    protected void paintComponent(Graphics g) {
      super.paintComponent(g);
      Graphics2D g2 = (Graphics2D) g.create();
      g2.setRenderingHint(RenderingHints.KEY_ANTIALIASING, RenderingHints.VALUE_ANTIALIAS_ON);
      int size = Math.min(getWidth(), getHeight()) - 2;
      int x = (getWidth() - size) / 2;
      int y = (getHeight() - size) / 2;
      g2.setColor(new Color(0x333333));
      g2.setStroke(new BasicStroke(3.0f, BasicStroke.CAP_ROUND, BasicStroke.JOIN_ROUND));
      g2.drawArc(x, y, size, size, 225, -270);
      int arcAngle = (int) (-270 * value);
      g2.setColor(new Color(0xE09040));
      g2.drawArc(x, y, size, size, 225, arcAngle);
      g2.setColor(new Color(0xDDDDDD));
      double angle = Math.toRadians(225 - 270 * value);
      int cx = x + size / 2 + (int) ((size / 2 - 2) * Math.cos(angle));
      int cy = y + size / 2 - (int) ((size / 2 - 2) * Math.sin(angle));
      g2.fillOval(cx - 2, cy - 2, 4, 4);
      g2.dispose();
    }
  }
}
