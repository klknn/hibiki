package hibiki.ui;

import javax.swing.*;
import java.awt.*;
import hibiki.BackendManager;
import hibiki.SimpleLaf;
import com.google.flatbuffers.FlatBufferBuilder;
import hibiki.ipc.Request;
import hibiki.ipc.Command;
import hibiki.ipc.Play;
import hibiki.ipc.Stop;
import hibiki.ipc.SaveProject;
import hibiki.ipc.LoadProject;
import hibiki.ipc.SetBpm;
import java.io.File;

public class TopBar extends JPanel {
    private JTextField bpmField;
    private JLabel timeSigLabel;
    private JLabel positionLabel;
    private JLabel cpuLabel;
    private ViewToggleListener viewToggleListener;
    private boolean isLooping = false;
    private JButton loopBtn;

    public interface ViewToggleListener {
        void onViewToggle(boolean isTimeline);
    }

    public void setViewToggleListener(ViewToggleListener listener) {
        this.viewToggleListener = listener;
    }

    public TopBar() {
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
        JButton sessionBtn = Theme.getInstance().createButton("Session", e -> {
            if (viewToggleListener != null) viewToggleListener.onViewToggle(false);
        });
        JButton timelineBtn = Theme.getInstance().createButton("Timeline", e -> {
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

        JButton recordBtn = Theme.getInstance().createButton("●", e -> {
            /* Record placeholder for future */});
        recordBtn.setForeground(new Color(200, 50, 50)); // Red for record
        recordBtn.setFont(new Font("SansSerif", Font.PLAIN, Theme.getInstance().scale(14)));
        recordBtn.setToolTipText("Record (coming soon)");

        loopBtn = Theme.getInstance().createButton("⟳", e -> toggleLoop());
        loopBtn.setFont(new Font("SansSerif", Font.PLAIN, Theme.getInstance().scale(14)));
        loopBtn.setToolTipText("Loop toggle");

        positionLabel = createDisplayLabel("1. 1. 1", Theme.getInstance().scale(80));

        centerPanel.add(playBtn);
        centerPanel.add(stopBtn);
        centerPanel.add(recordBtn);
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

        cpuLabel = createDisplayLabel("CPU: 0%", Theme.getInstance().scale(70));

        JButton settingsBtn = Theme.getInstance().createButton("⚙", e -> showSettings());
        settingsBtn.setFont(new Font("SansSerif", Font.PLAIN, Theme.getInstance().scale(14)));

        rightPanel.add(rateLabel);
        rightPanel.add(cpuLabel);
        rightPanel.add(settingsBtn);
        add(rightPanel, BorderLayout.EAST);
    }

    private void showSettings() {
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

    private void showSaveDialog() {
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

    private void showLoadDialog() {
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
        FlatBufferBuilder builder = new FlatBufferBuilder(512);
        int pathOff = builder.createString(path);
        int saveOff = SaveProject.createSaveProject(builder, pathOff);
        int requestOffset = Request.createRequest(builder, Command.SaveProject, saveOff);
        builder.finish(requestOffset);
        BackendManager.getInstance().sendRequest(builder);
    }

    private void sendLoadProject(String path) {
        FlatBufferBuilder builder = new FlatBufferBuilder(512);
        int pathOff = builder.createString(path);
        int loadOff = LoadProject.createLoadProject(builder, pathOff);
        int requestOffset = Request.createRequest(builder, Command.LoadProject, loadOff);
        builder.finish(requestOffset);
        BackendManager.getInstance().sendRequest(builder);
    }

    private void sendSetBpm(String bpmStr) {
        try {
            float bpm = Float.parseFloat(bpmStr);
            FlatBufferBuilder builder = new FlatBufferBuilder(128);
            int setBpmOff = SetBpm.createSetBpm(builder, bpm);
            int requestOffset = Request.createRequest(builder, Command.SetBpm, setBpmOff);
            builder.finish(requestOffset);
            BackendManager.getInstance().sendRequest(builder);
        } catch (NumberFormatException ex) {
            // Revert or ignore
        }
    }
}
