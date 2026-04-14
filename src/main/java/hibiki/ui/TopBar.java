package hibiki.ui;

import hibiki.BackendManager;
import hibiki.SimpleLaf;
import hibiki.pb.commands.*;
import hibiki.pb.core.*;
import hibiki.pb.notifications.*;
import java.awt.*;
import javax.swing.*;

public class TopBar extends JPanel {
  private static TopBar instance;

  public static TopBar getInstance() {
    return instance;
  }

  public VirtualKeyboard getVirtualKeyboard() {
    return virtualKeyboard;
  }

  private JTextField bpmField;
  private JLabel timeSigLabel;
  private JLabel positionLabel;
  private JLabel cpuLabel;
  private ViewToggleListener viewToggleListener;
  private ReplToggleListener replToggleListener;
  private JButton replBtn;
  private boolean isLooping = false;
  private JButton loopBtn;
  private boolean isRecording = false;
  private JButton recordButton;
  private final VirtualKeyboard virtualKeyboard = new VirtualKeyboard();
  private JButton pianoBtn;
  private JLabel octaveLabel;

  public interface ViewToggleListener {
    void onViewToggle(boolean isTimeline);
  }

  public interface ReplToggleListener {
    void onReplToggle();
  }

  public void setViewToggleListener(ViewToggleListener listener) {
    this.viewToggleListener = listener;
  }

  public void setReplToggleListener(ReplToggleListener listener) {
    this.replToggleListener = listener;
  }

  public TopBar() {
    instance = this;
    setLayout(new BorderLayout());
    setBackground(Theme.getInstance().BG_DARK);
    setPreferredSize(new Dimension(Integer.MAX_VALUE, Theme.getInstance().scale(40)));
    setBorder(BorderFactory.createMatteBorder(0, 0, 1, 0, Theme.getInstance().BORDER));

    // Left section: Song Info
    JPanel leftPanel = new JPanel(new FlowLayout(FlowLayout.LEFT, 10, 10));
    leftPanel.setOpaque(false);

    bpmField = createEditableDisplayField("140.00", Theme.getInstance().scale(60));
    bpmField.addActionListener(e -> sendSetBpm(bpmField.getText()));
    timeSigLabel = createDisplayLabel("4 / 4", Theme.getInstance().scale(50));

    leftPanel.add(bpmField);
    leftPanel.add(timeSigLabel);

    leftPanel.add(Theme.getInstance().createButton("Save", e -> showSaveDialog()));
    leftPanel.add(Theme.getInstance().createButton("Load", e -> showLoadDialog()));

    // View Toggles
    leftPanel.add(Box.createHorizontalStrut(Theme.getInstance().scale(20)));
    JButton sessionBtn =
        Theme.getInstance()
            .createButton(
                "Session",
                e -> {
                  if (viewToggleListener != null) viewToggleListener.onViewToggle(false);
                });
    JButton timelineBtn =
        Theme.getInstance()
            .createButton(
                "Timeline",
                e -> {
                  if (viewToggleListener != null) viewToggleListener.onViewToggle(true);
                });
    leftPanel.add(sessionBtn);
    leftPanel.add(timelineBtn);

    add(leftPanel, BorderLayout.WEST);

    // Center section: Playback Controls
    JPanel centerPanel = new JPanel(new FlowLayout(FlowLayout.CENTER, 5, 10));
    centerPanel.setOpaque(false);

    JButton playBtn = Theme.getInstance().createButton("▶", e -> sendPlay());
    playBtn.setForeground(Theme.getInstance().ACCENT_GREEN);
    playBtn.setFont(new Font("SansSerif", Font.PLAIN, Theme.getInstance().scale(14)));

    JButton stopBtn = Theme.getInstance().createButton("■", e -> sendStop());
    stopBtn.setFont(new Font("SansSerif", Font.PLAIN, Theme.getInstance().scale(14)));

    recordButton = Theme.getInstance().createButton("●", e -> sendRecord());
    recordButton.setForeground(new Color(200, 50, 50));
    recordButton.setFont(new Font("SansSerif", Font.PLAIN, Theme.getInstance().scale(14)));
    recordButton.setToolTipText("Record");

    loopBtn = Theme.getInstance().createButton("⟳", e -> toggleLoop());
    loopBtn.setFont(new Font("SansSerif", Font.PLAIN, Theme.getInstance().scale(14)));
    loopBtn.setToolTipText("Loop toggle");

    positionLabel = createDisplayLabel("1. 1. 1", Theme.getInstance().scale(80));

    centerPanel.add(playBtn);
    centerPanel.add(stopBtn);
    centerPanel.add(recordButton);
    centerPanel.add(loopBtn);
    centerPanel.add(Box.createHorizontalStrut(Theme.getInstance().scale(10)));
    centerPanel.add(positionLabel);
    add(centerPanel, BorderLayout.CENTER);

    // Right section: Device Info
    JPanel rightPanel = new JPanel(new FlowLayout(FlowLayout.RIGHT, 10, 10));
    rightPanel.setOpaque(false);

    JLabel rateLabel = new JLabel("44100 Hz");
    rateLabel.setForeground(Color.LIGHT_GRAY);
    rateLabel.setFont(new Font("SansSerif", Font.PLAIN, Theme.getInstance().scale(10)));

    cpuLabel = createDisplayLabel("CPU: 0.0%", Theme.getInstance().scale(90));

    replBtn =
        Theme.getInstance()
            .createButton(
                "λ REPL",
                e -> {
                  if (replToggleListener != null) replToggleListener.onReplToggle();
                });
    replBtn.setFont(new Font("SansSerif", Font.BOLD, Theme.getInstance().scale(11)));
    replBtn.setToolTipText("Toggle REPL panel (Ctrl+R)");

    JButton settingsBtn = Theme.getInstance().createButton("⚙", e -> showSettings());
    settingsBtn.setFont(new Font("SansSerif", Font.PLAIN, Theme.getInstance().scale(14)));

    // Virtual keyboard toggle
    pianoBtn =
        Theme.getInstance()
            .createButton(
                "🎹",
                e -> {
                  boolean nowEnabled = !virtualKeyboard.isEnabled();
                  virtualKeyboard.setEnabled(nowEnabled);
                  pianoBtn.setForeground(
                      nowEnabled ? Theme.getInstance().ACCENT_GREEN : Color.LIGHT_GRAY);
                  octaveLabel.setVisible(nowEnabled);
                  octaveLabel.setText("C" + virtualKeyboard.getOctave());
                });
    pianoBtn.setFont(new Font("SansSerif", Font.PLAIN, Theme.getInstance().scale(14)));
    pianoBtn.setForeground(Color.LIGHT_GRAY);
    pianoBtn.setToolTipText("Virtual MIDI Keyboard (PC keys → notes)");

    octaveLabel = new JLabel("C" + virtualKeyboard.getOctave());
    octaveLabel.setForeground(Theme.getInstance().ACCENT_GREEN);
    octaveLabel.setFont(new Font("SansSerif", Font.PLAIN, Theme.getInstance().scale(9)));
    octaveLabel.setVisible(false);

    rightPanel.add(rateLabel);
    rightPanel.add(cpuLabel);
    rightPanel.add(pianoBtn);
    rightPanel.add(octaveLabel);
    rightPanel.add(replBtn);
    rightPanel.add(settingsBtn);
    add(rightPanel, BorderLayout.EAST);

    // Poll process CPU load via MXBean (~1Hz)
    com.sun.management.OperatingSystemMXBean osBean =
        (com.sun.management.OperatingSystemMXBean)
            java.lang.management.ManagementFactory.getOperatingSystemMXBean();
    new javax.swing.Timer(
            1000,
            e -> {
              double load = osBean.getCpuLoad() * 100.0;
              cpuLabel.setText(String.format("CPU: %.1f%%", load));
            })
        .start();
  }

  public void showSettings() {
    SettingsDialog dialog = new SettingsDialog((Frame) SwingUtilities.getWindowAncestor(this));
    dialog.setVisible(true);
  }

  private JLabel createDisplayLabel(String text, int width) {
    JLabel label = new JLabel(text, SwingConstants.CENTER);
    label.setPreferredSize(new Dimension(width, Theme.getInstance().scale(22)));
    label.setBackground(Theme.getInstance().PANEL_BG_LIGHT);
    label.setForeground(Theme.getInstance().TEXT_BRIGHT);
    label.setOpaque(true);
    label.setFont(Theme.getInstance().FONT_DISPLAY);
    label.setBorder(BorderFactory.createLineBorder(Theme.getInstance().BORDER));
    return label;
  }

  private JTextField createEditableDisplayField(String text, int width) {
    JTextField field = new JTextField(text);
    field.setPreferredSize(new Dimension(width, Theme.getInstance().scale(22)));
    field.setBackground(Theme.getInstance().PANEL_BG_LIGHT);
    field.setForeground(Theme.getInstance().TEXT_BRIGHT);
    field.setCaretColor(Theme.getInstance().TEXT_BRIGHT);
    field.setFont(Theme.getInstance().FONT_DISPLAY);
    field.setBorder(BorderFactory.createLineBorder(Theme.getInstance().BORDER));
    field.setHorizontalAlignment(JTextField.CENTER);
    return field;
  }

  private void sendPlay() {
    BackendManager.getInstance().startPlayback();
  }

  private void sendStop() {
    BackendManager.getInstance().stopPlayback();
    isRecording = false;
    recordButton.setForeground(new Color(200, 50, 50));
  }

  private void sendRecord() {
    if (isRecording) {
      sendStop();
    } else {
      BackendManager.getInstance().startRecording();
      isRecording = true;
      recordButton.setForeground(new Color(255, 50, 50));
      recordButton.setBackground(new Color(80, 20, 20));
    }
  }

  private void toggleLoop() {
    isLooping = !isLooping;
    if (isLooping) {
      loopBtn.setForeground(Theme.getInstance().ACCENT_ORANGE);
    } else {
      loopBtn.setForeground(Theme.getInstance().TEXT_NORMAL);
    }
    // TODO: Send loop state to backend when implemented
  }

  public void showSaveDialog() {
    if (UIManager.getLookAndFeel() instanceof SimpleLaf) {
      Frame frame = (Frame) SwingUtilities.getWindowAncestor(this);
      FileDialog dialog = new FileDialog(frame, "Save Project", FileDialog.SAVE);
      dialog.setVisible(true);
      String dir = dialog.getDirectory();
      String file = dialog.getFile();
      if (dir != null && file != null) {
        sendSaveProject(dir + file);
      }
    } else {
      JFileChooser chooser = new JFileChooser();
      if (chooser.showSaveDialog(this) == JFileChooser.APPROVE_OPTION) {
        sendSaveProject(chooser.getSelectedFile().getAbsolutePath());
      }
    }
  }

  public void showLoadDialog() {
    if (UIManager.getLookAndFeel() instanceof SimpleLaf) {
      Frame frame = (Frame) SwingUtilities.getWindowAncestor(this);
      FileDialog dialog = new FileDialog(frame, "Load Project", FileDialog.LOAD);
      dialog.setVisible(true);
      String dir = dialog.getDirectory();
      String file = dialog.getFile();
      if (dir != null && file != null) {
        sendLoadProject(dir + file);
      }
    } else {
      JFileChooser chooser = new JFileChooser();
      if (chooser.showOpenDialog(this) == JFileChooser.APPROVE_OPTION) {
        sendLoadProject(chooser.getSelectedFile().getAbsolutePath());
      }
    }
  }

  private void sendSaveProject(String path) {
    BackendManager.getInstance()
        .sendRequest(
            Request.newBuilder()
                .setProject(
                    ProjectCmd.newBuilder().setAction(ProjectCmd.Action.ACTION_SAVE).setPath(path))
                .build());
  }

  private void sendLoadProject(String path) {
    BackendManager.getInstance()
        .sendRequest(
            Request.newBuilder()
                .setProject(
                    ProjectCmd.newBuilder().setAction(ProjectCmd.Action.ACTION_LOAD).setPath(path))
                .build());
  }

  private void sendSetBpm(String bpmStr) {
    try {
      float bpm = Float.parseFloat(bpmStr);
      BackendManager.getInstance()
          .sendRequest(
              Request.newBuilder()
                  .setProject(
                      ProjectCmd.newBuilder()
                          .setAction(ProjectCmd.Action.ACTION_SET_BPM)
                          .setBpm(bpm))
                  .build());
    } catch (NumberFormatException ex) {
      // Revert or ignore
    }
  }
}
