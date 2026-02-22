package hibiki.ui;

import javax.swing.*;
import java.awt.*;
import hibiki.BackendManager;
import com.google.flatbuffers.FlatBufferBuilder;
import hibiki.ipc.Request;
import hibiki.ipc.Command;
import hibiki.ipc.Play;
import hibiki.ipc.Stop;

public class TopBar extends JPanel {
    private JLabel bpmLabel;
    private JLabel timeSigLabel;
    private JLabel positionLabel;
    private JLabel cpuLabel;

    public TopBar() {
        setLayout(new BorderLayout());
        setBackground(new Color(45, 45, 45));
        setPreferredSize(new Dimension(Integer.MAX_VALUE, 50));
        setBorder(BorderFactory.createMatteBorder(0, 0, 1, 0, Color.BLACK));

        // Left section: Song Info
        JPanel leftPanel = new JPanel(new FlowLayout(FlowLayout.LEFT, 10, 10));
        leftPanel.setOpaque(false);

        bpmLabel = createDisplayLabel("120.00", 60);
        timeSigLabel = createDisplayLabel("4 / 4", 50);

        leftPanel.add(bpmLabel);
        leftPanel.add(timeSigLabel);
        add(leftPanel, BorderLayout.WEST);

        // Center section: Playback Controls
        JPanel centerPanel = new JPanel(new FlowLayout(FlowLayout.CENTER, 5, 10));
        centerPanel.setOpaque(false);

        JButton playBtn = new JButton("▶");
        playBtn.addActionListener(e -> sendPlay());

        JButton stopBtn = new JButton("■");
        stopBtn.addActionListener(e -> sendStop());

        positionLabel = createDisplayLabel("1. 1. 1", 80);

        centerPanel.add(playBtn);
        centerPanel.add(stopBtn);
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
        label.setPreferredSize(new Dimension(width, 25));
        label.setBackground(new Color(208, 208, 208));
        label.setForeground(Color.BLACK);
        label.setOpaque(true);
        label.setFont(new Font("Monospaced", Font.BOLD, 12));
        label.setBorder(BorderFactory.createLineBorder(Color.GRAY));
        return label;
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
}
