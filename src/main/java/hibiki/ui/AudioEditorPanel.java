package hibiki.ui;

import hibiki.BackendManager;
import hibiki.pb.commands.*;
import hibiki.pb.notifications.*;
import java.awt.*;
import java.awt.datatransfer.StringSelection;
import java.awt.dnd.*;
import java.awt.event.*;
import java.awt.image.BufferedImage;
import java.io.File;
import java.util.List;
import java.util.function.Consumer;
import javax.swing.*;
import javax.swing.filechooser.FileNameExtensionFilter;

/**
 * Edison-style audio clip editor panel. Provides waveform/spectrogram display, selection, zoom,
 * transport controls, destructive edits (normalize, reverse, fade, trim, gain), convolution reverb,
 * blur tool, playback preview, and drag-and-drop export.
 */
public class AudioEditorPanel extends JPanel {
  // Waveform / spectrogram data
  private float[] waveform = null;
  private float[] spectrogram = null;
  private int spectrogramWidth = 0, spectrogramHeight = 0;
  private BufferedImage spectrogramImage = null;
  private boolean showSpectrogram = false;

  // Audio metadata
  private float durationSec = 0;
  private int sampleRate = 0;
  private int numChannels = 0;
  private String fileName = "(no file loaded)";

  // View state
  private float viewStart = 0.0f; // 0.0–1.0 range of audio visible
  private float viewEnd = 1.0f;

  // Selection state
  private float selStart = 0.0f; // Normalized 0–1
  private float selEnd = 0.0f;
  private boolean hasSelection = false;
  private boolean isDraggingSelection = false;

  // Clip context (set when opened from "Edit Clip" context menu)
  private int sourceTrackIdx = -1;
  private int sourceClipIdx = -1;
  private String sourceClipPath = null;

  // UI components
  private final WaveformDisplay waveformDisplay;
  private final JLabel infoLabel;
  private final JButton applyToClipBtn;
  private final Consumer<Notification> notificationListener;

  public AudioEditorPanel() {
    this(null, -1, -1);
  }

  /**
   * Create editor panel with optional clip context for "Edit Clip" integration.
   *
   * @param clipPath path to pre-load, or null
   * @param trackIdx source track index, or -1
   * @param clipIdx source clip/slot index, or -1
   */
  public AudioEditorPanel(String clipPath, int trackIdx, int clipIdx) {
    this.sourceClipPath = clipPath;
    this.sourceTrackIdx = trackIdx;
    this.sourceClipIdx = clipIdx;

    setLayout(new BorderLayout());
    Theme theme = Theme.getInstance();
    setBackground(theme.BG_DARK);

    // ── Header ──
    JPanel header = new JPanel(new BorderLayout());
    header.setBackground(new Color(0x2D5A8A));
    header.setBorder(BorderFactory.createEmptyBorder(4, 8, 4, 8));
    JLabel title = new JLabel("Audio Editor");
    title.setForeground(Color.WHITE);
    title.setFont(theme.FONT_UI_BOLD.deriveFont(theme.scale(12.0f)));
    header.add(title, BorderLayout.WEST);

    // Header buttons: Load + Transport
    JPanel headerBtns = new JPanel(new FlowLayout(FlowLayout.RIGHT, 4, 0));
    headerBtns.setOpaque(false);

    JButton loadBtn = new JButton("Load");
    loadBtn.setFont(theme.FONT_UI.deriveFont(theme.scale(10.0f)));
    loadBtn.addActionListener(e -> loadFile());
    headerBtns.add(loadBtn);

    // Transport buttons
    JButton playBtn = new JButton("\u25B6"); // ▶
    playBtn.setFont(theme.FONT_UI.deriveFont(theme.scale(12.0f)));
    playBtn.setToolTipText("Preview Play");
    playBtn.addActionListener(e -> sendPreviewPlay());
    headerBtns.add(playBtn);

    JButton stopBtn = new JButton("\u25A0"); // ■
    stopBtn.setFont(theme.FONT_UI.deriveFont(theme.scale(12.0f)));
    stopBtn.setToolTipText("Preview Stop");
    stopBtn.addActionListener(e -> sendPreviewStop());
    headerBtns.add(stopBtn);

    header.add(headerBtns, BorderLayout.EAST);
    add(header, BorderLayout.NORTH);

    // ── Waveform display ──
    waveformDisplay = new WaveformDisplay();
    add(waveformDisplay, BorderLayout.CENTER);

    // ── Bottom panel: toolbar + info ──
    JPanel bottom = new JPanel(new BorderLayout());
    bottom.setBackground(theme.BG_MEDIUM);
    bottom.setBorder(BorderFactory.createEmptyBorder(4, 6, 4, 6));

    // Edit toolbar
    JPanel toolbar = new JPanel(new FlowLayout(FlowLayout.LEFT, 4, 2));
    toolbar.setOpaque(false);
    toolbar.add(
        makeButton("Normalize", e -> sendAction(AudioEditorCmd.Action.AE_ACTION_NORMALIZE)));
    toolbar.add(makeButton("Reverse", e -> sendAction(AudioEditorCmd.Action.AE_ACTION_REVERSE)));
    toolbar.add(makeButton("Fade In", e -> sendAction(AudioEditorCmd.Action.AE_ACTION_FADE_IN)));
    toolbar.add(makeButton("Fade Out", e -> sendAction(AudioEditorCmd.Action.AE_ACTION_FADE_OUT)));
    toolbar.add(makeButton("Trim", e -> sendAction(AudioEditorCmd.Action.AE_ACTION_TRIM)));
    toolbar.add(makeButton("Gain +3dB", e -> sendGain(3.0f)));
    toolbar.add(makeButton("Gain -3dB", e -> sendGain(-3.0f)));
    toolbar.add(makeButton("Save", e -> saveFile()));

    // Spectrogram toggle
    JToggleButton specBtn = new JToggleButton("Spectrogram");
    specBtn.setFont(Theme.getInstance().FONT_UI.deriveFont(Theme.getInstance().scale(9.0f)));
    specBtn.addActionListener(
        e -> {
          showSpectrogram = specBtn.isSelected();
          waveformDisplay.repaint();
        });
    toolbar.add(specBtn);

    // Convolution button
    toolbar.add(makeButton("Convolve...", e -> showConvolutionDialog()));

    // Blur button
    toolbar.add(makeButton("Blur...", e -> showBlurDialog()));

    // Apply to Clip button (visible when opened from context menu)
    applyToClipBtn = makeButton("Apply to Clip", e -> applyToClip());
    applyToClipBtn.setToolTipText("Replace the source clip with the edited audio");
    applyToClipBtn.setVisible(sourceTrackIdx >= 0 && sourceClipIdx >= 0);
    toolbar.add(applyToClipBtn);

    bottom.add(toolbar, BorderLayout.NORTH);

    // Info bar
    infoLabel = new JLabel(" ");
    infoLabel.setForeground(new Color(0xAABBCC));
    infoLabel.setFont(theme.FONT_UI.deriveFont(theme.scale(9.0f)));
    bottom.add(infoLabel, BorderLayout.SOUTH);
    add(bottom, BorderLayout.SOUTH);

    // ── Drag-and-drop source ──
    setupDragSource();

    // Notification listener
    notificationListener = this::handleNotification;
    BackendManager.getInstance().addNotificationListener(notificationListener);

    // Auto-load if opened from clip context menu
    if (clipPath != null && !clipPath.isEmpty()) {
      SwingUtilities.invokeLater(() -> loadFromPath(clipPath));
    }
  }

  /** Clean up notification listener. */
  public void dispose() {
    BackendManager.getInstance().removeNotificationListener(notificationListener);
  }

  private void handleNotification(Notification n) {
    if (n.hasAudioEditorData()) {
      AudioEditorData data = n.getAudioEditorData();
      SwingUtilities.invokeLater(
          () -> {
            List<Float> wfList = data.getWaveformList();
            waveform = new float[wfList.size()];
            for (int i = 0; i < wfList.size(); i++) waveform[i] = wfList.get(i);

            List<Float> specList = data.getSpectrogramList();
            if (!specList.isEmpty()) {
              spectrogram = new float[specList.size()];
              for (int i = 0; i < specList.size(); i++) spectrogram[i] = specList.get(i);
              spectrogramWidth = data.getSpectrogramWidth();
              spectrogramHeight = data.getSpectrogramHeight();
              buildSpectrogramImage();
            }

            durationSec = data.getDurationSec();
            sampleRate = data.getSampleRate();
            numChannels = data.getNumChannels();
            fileName = data.getFileName();
            updateInfo();
            waveformDisplay.repaint();
          });
    }
  }

  private void buildSpectrogramImage() {
    if (spectrogram == null || spectrogramWidth <= 0 || spectrogramHeight <= 0) return;
    spectrogramImage =
        new BufferedImage(spectrogramWidth, spectrogramHeight, BufferedImage.TYPE_INT_RGB);
    for (int x = 0; x < spectrogramWidth; x++) {
      for (int y = 0; y < spectrogramHeight; y++) {
        float db = spectrogram[x * spectrogramHeight + y];
        // Map dB to color (viridis-like: dark blue → green → yellow)
        float norm = Math.max(0, Math.min(1, (db + 100) / 100.0f));
        int r = (int) (norm * norm * 255);
        int g = (int) (norm * 200 + 30);
        int b = (int) ((1 - norm) * 180 + 40);
        r = Math.max(0, Math.min(255, r));
        g = Math.max(0, Math.min(255, g));
        b = Math.max(0, Math.min(255, b));
        // Flip Y: low frequencies at bottom
        spectrogramImage.setRGB(x, spectrogramHeight - 1 - y, (r << 16) | (g << 8) | b);
      }
    }
  }

  private void updateInfo() {
    String sel =
        hasSelection
            ? String.format("  Sel: %.3fs–%.3fs", selStart * durationSec, selEnd * durationSec)
            : "";
    infoLabel.setText(
        String.format(
            "%s  |  %.2fs  |  %d Hz  |  %d ch%s",
            fileName, durationSec, sampleRate, numChannels, sel));
  }

  // ── Drag-and-drop source ──

  private void setupDragSource() {
    if (GraphicsEnvironment.isHeadless()) return;
    DragSource ds = DragSource.getDefaultDragSource();
    ds.createDefaultDragGestureRecognizer(
        waveformDisplay,
        DnDConstants.ACTION_COPY,
        dge -> {
          if (waveform == null || waveform.length == 0) return;
          // Save to temp file first, then provide the path for drop
          String tempPath =
              System.getProperty("java.io.tmpdir") + File.separator + "hibiki_editor_drag.wav";
          // Ask engine to save
          BackendManager.getInstance()
              .sendRequest(
                  Request.newBuilder()
                      .setAudioEditor(
                          AudioEditorCmd.newBuilder()
                              .setAction(AudioEditorCmd.Action.AE_ACTION_SAVE)
                              .setPath(tempPath))
                      .build());
          // Start drag with the path (timeline expects "audio:<path>" format)
          dge.startDrag(DragSource.DefaultCopyDrop, new StringSelection("audio:" + tempPath));
        });
  }

  // ── Actions ──

  private void loadFile() {
    JFileChooser fc = new JFileChooser(".");
    fc.setFileFilter(new FileNameExtensionFilter("Audio Files", "wav", "aiff", "flac"));
    if (fc.showOpenDialog(this) == JFileChooser.APPROVE_OPTION) {
      loadFromPath(fc.getSelectedFile().getAbsolutePath());
    }
  }

  private void loadFromPath(String path) {
    BackendManager.getInstance()
        .sendRequest(
            Request.newBuilder()
                .setAudioEditor(
                    AudioEditorCmd.newBuilder()
                        .setAction(AudioEditorCmd.Action.AE_ACTION_LOAD)
                        .setPath(path))
                .build());
    // Reset view
    viewStart = 0;
    viewEnd = 1;
    hasSelection = false;
  }

  private void sendAction(AudioEditorCmd.Action action) {
    AudioEditorCmd.Builder cmd = AudioEditorCmd.newBuilder().setAction(action);
    if (hasSelection) {
      cmd.setSelectionStart(selStart).setSelectionEnd(selEnd);
    } else {
      cmd.setSelectionStart(0).setSelectionEnd(1);
    }
    BackendManager.getInstance().sendRequest(Request.newBuilder().setAudioEditor(cmd).build());
  }

  private void sendGain(float db) {
    AudioEditorCmd.Builder cmd =
        AudioEditorCmd.newBuilder().setAction(AudioEditorCmd.Action.AE_ACTION_GAIN).setValue(db);
    if (hasSelection) {
      cmd.setSelectionStart(selStart).setSelectionEnd(selEnd);
    } else {
      cmd.setSelectionStart(0).setSelectionEnd(1);
    }
    BackendManager.getInstance().sendRequest(Request.newBuilder().setAudioEditor(cmd).build());
  }

  private void sendPreviewPlay() {
    BackendManager.getInstance()
        .sendRequest(
            Request.newBuilder()
                .setAudioEditor(
                    AudioEditorCmd.newBuilder()
                        .setAction(AudioEditorCmd.Action.AE_ACTION_PREVIEW_PLAY))
                .build());
  }

  private void sendPreviewStop() {
    BackendManager.getInstance()
        .sendRequest(
            Request.newBuilder()
                .setAudioEditor(
                    AudioEditorCmd.newBuilder()
                        .setAction(AudioEditorCmd.Action.AE_ACTION_PREVIEW_STOP))
                .build());
  }

  private void applyToClip() {
    if (sourceTrackIdx < 0 || sourceClipIdx < 0) return;
    BackendManager.getInstance()
        .sendRequest(
            Request.newBuilder()
                .setAudioEditor(
                    AudioEditorCmd.newBuilder()
                        .setAction(AudioEditorCmd.Action.AE_ACTION_APPLY_TO_CLIP)
                        .setTrackIndex(sourceTrackIdx)
                        .setClipIndex(sourceClipIdx))
                .build());
  }

  private void saveFile() {
    JFileChooser fc = new JFileChooser(".");
    fc.setFileFilter(new FileNameExtensionFilter("WAV Audio", "wav"));
    if (fc.showSaveDialog(this) == JFileChooser.APPROVE_OPTION) {
      String path = fc.getSelectedFile().getAbsolutePath();
      if (!path.endsWith(".wav")) path += ".wav";
      BackendManager.getInstance()
          .sendRequest(
              Request.newBuilder()
                  .setAudioEditor(
                      AudioEditorCmd.newBuilder()
                          .setAction(AudioEditorCmd.Action.AE_ACTION_SAVE)
                          .setPath(path))
                  .build());
    }
  }

  private void showConvolutionDialog() {
    JFileChooser fc = new JFileChooser(".");
    fc.setDialogTitle("Select Impulse Response (any WAV — drums, textures, etc.)");
    fc.setFileFilter(new FileNameExtensionFilter("WAV Audio", "wav"));
    if (fc.showOpenDialog(this) != JFileChooser.APPROVE_OPTION) return;
    String irPath = fc.getSelectedFile().getAbsolutePath();

    // Dry/Wet dialog
    JPanel panel = new JPanel(new GridLayout(4, 2, 4, 4));
    panel.add(new JLabel("IR file:"));
    panel.add(new JLabel(new File(irPath).getName()));
    JSpinner drySpinner = new JSpinner(new SpinnerNumberModel(0.3, 0.0, 1.0, 0.05));
    JSpinner wetSpinner = new JSpinner(new SpinnerNumberModel(0.7, 0.0, 1.0, 0.05));
    JCheckBox tailBox = new JCheckBox("Add tail", true);
    panel.add(new JLabel("Dry:"));
    panel.add(drySpinner);
    panel.add(new JLabel("Wet:"));
    panel.add(wetSpinner);
    panel.add(tailBox);
    panel.add(new JLabel(""));

    int result =
        JOptionPane.showConfirmDialog(
            this, panel, "Convolution Reverb", JOptionPane.OK_CANCEL_OPTION);
    if (result != JOptionPane.OK_OPTION) return;

    float dry = ((Number) drySpinner.getValue()).floatValue();
    float wet = ((Number) wetSpinner.getValue()).floatValue();
    boolean addTail = tailBox.isSelected();

    AudioEditorCmd.Builder cmd =
        AudioEditorCmd.newBuilder()
            .setAction(AudioEditorCmd.Action.AE_ACTION_CONVOLVE)
            .setImpulsePath(irPath)
            .setConvDry(dry)
            .setConvWet(wet)
            .setConvAddTail(addTail);
    if (hasSelection) {
      cmd.setSelectionStart(selStart).setSelectionEnd(selEnd);
    }
    BackendManager.getInstance().sendRequest(Request.newBuilder().setAudioEditor(cmd).build());
  }

  private void showBlurDialog() {
    JPanel panel = new JPanel(new GridLayout(3, 2, 4, 4));

    panel.add(new JLabel("Amount:"));
    JSpinner amountSpinner = new JSpinner(new SpinnerNumberModel(0.01, 0.001, 1.0, 0.005));
    panel.add(amountSpinner);

    panel.add(new JLabel("Envelope:"));
    JComboBox<String> envCombo = new JComboBox<>(new String[] {"Flat", "Triangle", "Parabolic"});
    panel.add(envCombo);

    panel.add(new JLabel(""));
    panel.add(new JLabel("Higher amount = more blur"));

    int result =
        JOptionPane.showConfirmDialog(this, panel, "Blur Tool", JOptionPane.OK_CANCEL_OPTION);
    if (result != JOptionPane.OK_OPTION) return;

    float amount = ((Number) amountSpinner.getValue()).floatValue();
    int envelope = envCombo.getSelectedIndex();

    BackendManager.getInstance()
        .sendRequest(
            Request.newBuilder()
                .setAudioEditor(
                    AudioEditorCmd.newBuilder()
                        .setAction(AudioEditorCmd.Action.AE_ACTION_BLUR)
                        .setBlurAmount(amount)
                        .setBlurEnvelope(envelope))
                .build());
  }

  private JButton makeButton(String label, ActionListener action) {
    Theme theme = Theme.getInstance();
    JButton btn = new JButton(label);
    btn.setFont(theme.FONT_UI.deriveFont(theme.scale(9.0f)));
    btn.setFocusPainted(false);
    btn.addActionListener(action);
    return btn;
  }

  // ── Waveform display ──

  private class WaveformDisplay extends JPanel {
    WaveformDisplay() {
      setBackground(new Color(0x0D0D1A));
      setBorder(BorderFactory.createLineBorder(new Color(0x333355)));

      addMouseListener(
          new MouseAdapter() {
            @Override
            public void mousePressed(MouseEvent e) {
              if (waveform == null) return;
              float norm = viewStart + (viewEnd - viewStart) * e.getX() / (float) getWidth();
              selStart = Math.max(0, Math.min(1, norm));
              selEnd = selStart;
              hasSelection = false;
              isDraggingSelection = true;
              repaint();
            }

            @Override
            public void mouseReleased(MouseEvent e) {
              isDraggingSelection = false;
              if (Math.abs(selEnd - selStart) > 0.001f) {
                hasSelection = true;
                if (selStart > selEnd) {
                  float tmp = selStart;
                  selStart = selEnd;
                  selEnd = tmp;
                }
              } else {
                hasSelection = false;
              }
              updateInfo();
              repaint();
            }
          });

      addMouseMotionListener(
          new MouseMotionAdapter() {
            @Override
            public void mouseDragged(MouseEvent e) {
              if (!isDraggingSelection) return;
              float norm = viewStart + (viewEnd - viewStart) * e.getX() / (float) getWidth();
              selEnd = Math.max(0, Math.min(1, norm));
              hasSelection = Math.abs(selEnd - selStart) > 0.001f;
              updateInfo();
              repaint();
            }
          });

      // Mouse wheel zoom
      addMouseWheelListener(
          e -> {
            if (waveform == null) return;
            float center = viewStart + (viewEnd - viewStart) * e.getX() / (float) getWidth();
            float range = viewEnd - viewStart;
            float newRange =
                (e.getWheelRotation() > 0)
                    ? Math.min(1.0f, range * 1.2f) // zoom out
                    : Math.max(0.01f, range * 0.8f); // zoom in
            viewStart = Math.max(0, center - newRange / 2);
            viewEnd = Math.min(1, viewStart + newRange);
            if (viewStart < 0) {
              viewStart = 0;
              viewEnd = Math.min(1, newRange);
            }
            repaint();
          });
    }

    @Override
    protected void paintComponent(Graphics g) {
      super.paintComponent(g);
      Graphics2D g2 = (Graphics2D) g.create();
      g2.setRenderingHint(RenderingHints.KEY_ANTIALIASING, RenderingHints.VALUE_ANTIALIAS_ON);
      int w = getWidth(), h = getHeight();

      if (waveform == null || waveform.length == 0) {
        g2.setColor(new Color(0x556688));
        g2.setFont(getFont().deriveFont(14.0f));
        String msg =
            "Click 'Load' to open an audio file, or right-click a clip → 'Edit in Audio Editor'";
        FontMetrics fm = g2.getFontMetrics();
        g2.drawString(msg, (w - fm.stringWidth(msg)) / 2, h / 2);
        g2.dispose();
        return;
      }

      // Draw spectrogram background
      if (showSpectrogram && spectrogramImage != null) {
        int imgX = 0;
        int imgW = w;
        int srcX1 = (int) (viewStart * spectrogramImage.getWidth());
        int srcX2 = (int) (viewEnd * spectrogramImage.getWidth());
        g2.drawImage(
            spectrogramImage,
            imgX,
            0,
            imgX + imgW,
            h,
            srcX1,
            0,
            srcX2,
            spectrogramImage.getHeight(),
            null);
        // Semi-transparent overlay for waveform readability
        g2.setColor(new Color(0, 0, 0, 80));
        g2.fillRect(0, 0, w, h);
      }

      // Selection highlight
      if (hasSelection) {
        float viewRange = viewEnd - viewStart;
        int selX1 = (int) ((Math.min(selStart, selEnd) - viewStart) / viewRange * w);
        int selX2 = (int) ((Math.max(selStart, selEnd) - viewStart) / viewRange * w);
        g2.setColor(new Color(0x55, 0x99, 0xFF, 50));
        g2.fillRect(selX1, 0, selX2 - selX1, h);
        g2.setColor(new Color(0x55, 0x99, 0xFF, 150));
        g2.drawLine(selX1, 0, selX1, h);
        g2.drawLine(selX2, 0, selX2, h);
      }

      // Draw waveform
      float viewRange = viewEnd - viewStart;
      int totalPoints = waveform.length;
      g2.setColor(new Color(0x44, 0xAA, 0xFF, 200));
      g2.setStroke(new BasicStroke(1.0f));

      int mid = h / 2;
      for (int px = 0; px < w; px++) {
        float norm = viewStart + viewRange * px / (float) w;
        int idx = Math.max(0, Math.min(totalPoints - 1, (int) (norm * totalPoints)));
        float val = waveform[idx];
        int barH = (int) (val * h * 0.45f);
        g2.fillRect(px, mid - barH, 1, barH * 2);
      }

      // Center line
      g2.setColor(new Color(0x444466));
      g2.drawLine(0, mid, w, mid);

      // File name
      g2.setColor(new Color(0xAABBCC));
      g2.setFont(getFont().deriveFont(10.0f));
      g2.drawString(fileName, 4, h - 4);

      g2.dispose();
    }
  }

  // ── Accessors for testing ──
  float getViewStart() {
    return viewStart;
  }

  float getViewEnd() {
    return viewEnd;
  }

  float getSelStart() {
    return selStart;
  }

  float getSelEnd() {
    return selEnd;
  }

  boolean hasSelection() {
    return hasSelection;
  }

  /** Set waveform data directly (for testing without IPC). */
  void setWaveformData(float[] wf, String name, float duration, int sr, int ch) {
    this.waveform = wf;
    this.fileName = name;
    this.durationSec = duration;
    this.sampleRate = sr;
    this.numChannels = ch;
    updateInfo();
    waveformDisplay.repaint();
  }
}
