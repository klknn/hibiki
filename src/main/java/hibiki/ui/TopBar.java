package hibiki.ui;

import javax.swing.*;
import java.awt.*;
import hibiki.BackendManager;
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

    public TopBar() {
        setLayout(new BorderLayout());
        setBackground(Theme.BG_DARK);
        setPreferredSize(new Dimension(Integer.MAX_VALUE, 40));
        setBorder(BorderFactory.createMatteBorder(0, 0, 1, 0, Theme.BORDER));

        // Left section: Song Info
        JPanel leftPanel = new JPanel(new FlowLayout(FlowLayout.LEFT, 10, 10));
        leftPanel.setOpaque(false);

        bpmField = createEditableDisplayField("140.00", 60);
        bpmField.addActionListener(e -> sendSetBpm(bpmField.getText()));
        timeSigLabel = createDisplayLabel("4 / 4", 50);

        leftPanel.add(bpmField);
        leftPanel.add(timeSigLabel);

        leftPanel.add(createFlatButton("Save", e -> showSaveDialog()));
        leftPanel.add(createFlatButton("Load", e -> showLoadDialog()));

        add(leftPanel, BorderLayout.WEST);

        // Center section: Playback Controls
        JPanel centerPanel = new JPanel(new FlowLayout(FlowLayout.CENTER, 5, 10));
        centerPanel.setOpaque(false);

        JButton playBtn = createFlatButton("▶", e -> sendPlay());
        playBtn.setForeground(Theme.ACCENT_GREEN);
        playBtn.setFont(new Font("SansSerif", Font.PLAIN, 14));

        JButton stopBtn = createFlatButton("■", e -> sendStop());
        stopBtn.setFont(new Font("SansSerif", Font.PLAIN, 14));

        positionLabel = createDisplayLabel("1. 1. 1", 80);

        centerPanel.add(playBtn);
        centerPanel.add(stopBtn);
        centerPanel.add(Box.createHorizontalStrut(10));
        centerPanel.add(positionLabel);
        add(centerPanel, BorderLayout.CENTER);

        // Right section: Device Info
        JPanel rightPanel = new JPanel(new FlowLayout(FlowLayout.RIGHT, 10, 10));
        rightPanel.setOpaque(false);

        JLabel rateLabel = new JLabel("44100 Hz");
        rateLabel.setForeground(Color.LIGHT_GRAY);
        rateLabel.setFont(new Font("SansSerif", Font.PLAIN, 10));

        cpuLabel = createDisplayLabel("CPU: 0%", 70);

        rightPanel.add(rateLabel);
        rightPanel.add(cpuLabel);
        add(rightPanel, BorderLayout.EAST);
    }

    private JLabel createDisplayLabel(String text, int width) {
        JLabel label = new JLabel(text, SwingConstants.CENTER);
        label.setPreferredSize(new Dimension(width, 22));
        label.setBackground(Theme.PANEL_BG_LIGHT);
        label.setForeground(Theme.TEXT_BRIGHT);
        label.setOpaque(true);
        label.setFont(Theme.FONT_DISPLAY);
        label.setBorder(BorderFactory.createLineBorder(Theme.BORDER));
        return label;
    }

    private JTextField createEditableDisplayField(String text, int width) {
        JTextField field = new JTextField(text);
        field.setPreferredSize(new Dimension(width, 22));
        field.setBackground(Theme.PANEL_BG_LIGHT);
        field.setForeground(Theme.TEXT_BRIGHT);
        field.setCaretColor(Theme.TEXT_BRIGHT);
        field.setFont(Theme.FONT_DISPLAY);
        field.setBorder(BorderFactory.createLineBorder(Theme.BORDER));
        field.setHorizontalAlignment(JTextField.CENTER);
        return field;
    }

    private JButton createFlatButton(String text, java.awt.event.ActionListener listener) {
        JButton btn = new JButton(text);
        btn.setFont(Theme.FONT_UI);
        btn.setBackground(Theme.PANEL_BG);
        btn.setForeground(Theme.TEXT_NORMAL);
        btn.setFocusPainted(false);
        btn.setBorder(BorderFactory.createLineBorder(Theme.BORDER));
        btn.setMargin(new Insets(2, 8, 2, 8));
        btn.addActionListener(listener);

        btn.addMouseListener(new java.awt.event.MouseAdapter() {
            public void mouseEntered(java.awt.event.MouseEvent e) {
                btn.setBackground(Theme.PANEL_BG_LIGHT);
            }

            public void mouseExited(java.awt.event.MouseEvent e) {
                btn.setBackground(Theme.PANEL_BG);
            }
        });

        return btn;
    }

    private void sendPlay() {
        FlatBufferBuilder builder = new FlatBufferBuilder(128);
        Play.startPlay(builder);
        int playOffset = Play.endPlay(builder);
        int requestOffset = Request.createRequest(builder, Command.Play, playOffset);
        builder.finish(requestOffset);
        BackendManager.getInstance().sendRequest(builder);
    }

    private void sendStop() {
        FlatBufferBuilder builder = new FlatBufferBuilder(128);
        Stop.startStop(builder);
        int stopOffset = Stop.endStop(builder);
        int requestOffset = Request.createRequest(builder, Command.Stop, stopOffset);
        builder.finish(requestOffset);
        BackendManager.getInstance().sendRequest(builder);
    }

    private void showSaveDialog() {
        JFileChooser chooser = new JFileChooser();
        if (chooser.showSaveDialog(this) == JFileChooser.APPROVE_OPTION) {
            sendSaveProject(chooser.getSelectedFile().getAbsolutePath());
        }
    }

    private void showLoadDialog() {
        JFileChooser chooser = new JFileChooser();
        if (chooser.showOpenDialog(this) == JFileChooser.APPROVE_OPTION) {
            sendLoadProject(chooser.getSelectedFile().getAbsolutePath());
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
