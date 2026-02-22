package hibiki.ui;

import javax.swing.*;
import java.awt.*;
import hibiki.BackendManager;
import com.google.flatbuffers.FlatBufferBuilder;
import hibiki.ipc.Request;
import hibiki.ipc.Command;
import hibiki.ipc.PlayClip;
import hibiki.ipc.StopTrack;

import hibiki.ipc.Response;
import hibiki.ipc.ClipInfo;

public class SessionView extends JPanel {
    private JButton[][] slotButtons = new JButton[5][5]; // 4 tracks + master, 5 slots

    public SessionView() {
        setLayout(new BorderLayout());
        setBackground(new Color(128, 128, 128));

        JPanel master = createMasterStrip();

        // Build a chain of split panes for tracks with proportional resizing
        Component lastComponent = master;
        for (int i = 4; i >= 1; i--) {
            JPanel track = createTrackStrip("Track " + i, i);
            JSplitPane split = new JSplitPane(JSplitPane.HORIZONTAL_SPLIT, track, lastComponent);
            split.setContinuousLayout(true);
            split.setBorder(null);
            split.setDividerLocation(110);

            // To maintain track proportions (aspect ratio) when resizing the whole window,
            // we prefer the right component (which eventually contains Master) to grow.
            // However, the user said "fit to parent", so we share space.
            // Using 0.0 for the track preserves its width unless manually dragged.
            split.setResizeWeight(0.0);

            lastComponent = split;
        }

        add(lastComponent, BorderLayout.CENTER);

        // Listen for clip load notifications
        BackendManager.getInstance().addNotificationListener(notification -> {
            if (notification.responseType() == Response.ClipInfo) {
                ClipInfo info = (ClipInfo) notification.response(new ClipInfo());
                updateSlotLabel(info.trackIndex(), info.slotIndex(), info.name());
            }
        });
    }

    private void updateSlotLabel(int trackIdx, int slotIdx, String name) {
        SwingUtilities.invokeLater(() -> {
            if (trackIdx >= 1 && trackIdx <= 4 && slotIdx >= 0 && slotIdx < 5) {
                JButton btn = slotButtons[trackIdx][slotIdx];
                if (btn != null) {
                    btn.setText("<html><center>" + name + "<br>►</center></html>");
                    btn.setBackground(new Color(100, 200, 100));
                }
            }
        });
    }

    private JPanel createTrackStrip(String name, int trackIdx) {
        JPanel strip = new JPanel();
        strip.setLayout(new BoxLayout(strip, BoxLayout.Y_AXIS));
        strip.setBackground(new Color(144, 144, 144));
        strip.setPreferredSize(new Dimension(100, 400));
        strip.setBorder(BorderFactory.createLineBorder(Color.BLACK));

        // Header
        JLabel header = new JLabel(name, SwingConstants.CENTER);
        header.setAlignmentX(Component.CENTER_ALIGNMENT);
        header.setMaximumSize(new Dimension(100, 30));
        header.setBackground(new Color(80, 80, 80));
        header.setForeground(Color.WHITE);
        header.setOpaque(true);
        strip.add(header);

        // Clips
        for (int i = 0; i < 5; i++) {
            JButton clipBtn = new JButton("");
            clipBtn.setAlignmentX(Component.CENTER_ALIGNMENT);
            clipBtn.setMaximumSize(new Dimension(90, 35)); // Slightly larger for text
            clipBtn.setFont(new Font("SansSerif", Font.PLAIN, 10));
            int slotIdx = i;
            clipBtn.addActionListener(e -> sendPlayClip(trackIdx, slotIdx));
            slotButtons[trackIdx][slotIdx] = clipBtn;
            strip.add(Box.createVerticalStrut(2));
            strip.add(clipBtn);
        }

        strip.add(Box.createVerticalGlue());

        // Pan
        JSlider panSlider = new JSlider(-50, 50, 0);
        panSlider.setMaximumSize(new Dimension(90, 20));
        strip.add(new JLabel("Pan", SwingConstants.CENTER));
        strip.add(panSlider);

        // Vol
        JSlider volSlider = new JSlider(JSlider.VERTICAL, -70, 6, 0);
        volSlider.setMaximumSize(new Dimension(90, 100));
        strip.add(new JLabel("Vol", SwingConstants.CENTER));
        strip.add(volSlider);

        // Activator
        JButton activeBtn = new JButton("" + trackIdx);
        activeBtn.setAlignmentX(Component.CENTER_ALIGNMENT);
        activeBtn.setBackground(new Color(224, 176, 80));
        activeBtn.addActionListener(e -> sendStopTrack(trackIdx));
        strip.add(Box.createVerticalStrut(5));
        strip.add(activeBtn);

        return strip;
    }

    private JPanel createMasterStrip() {
        JPanel strip = new JPanel();
        strip.setLayout(new BoxLayout(strip, BoxLayout.Y_AXIS));
        strip.setBackground(new Color(128, 128, 128));
        strip.setPreferredSize(new Dimension(100, 400));
        strip.setBorder(BorderFactory.createLineBorder(Color.BLACK));

        JLabel header = new JLabel("Master", SwingConstants.CENTER);
        header.setAlignmentX(Component.CENTER_ALIGNMENT);
        header.setMaximumSize(new Dimension(100, 30));
        header.setBackground(new Color(64, 64, 64));
        header.setForeground(Color.WHITE);
        header.setOpaque(true);
        strip.add(header);

        for (int i = 0; i < 5; i++) {
            JButton sceneBtn = new JButton((i + 1) + "  ►");
            sceneBtn.setAlignmentX(Component.CENTER_ALIGNMENT);
            sceneBtn.setMaximumSize(new Dimension(90, 25));
            strip.add(Box.createVerticalStrut(2));
            strip.add(sceneBtn);
        }

        strip.add(Box.createVerticalGlue());

        JSlider masterVol = new JSlider(JSlider.VERTICAL, -70, 6, 0);
        masterVol.setMaximumSize(new Dimension(90, 100));
        strip.add(new JLabel("Master Vol", SwingConstants.CENTER));
        strip.add(masterVol);

        return strip;
    }

    private void sendPlayClip(int trackIdx, int slotIdx) {
        FlatBufferBuilder builder = new FlatBufferBuilder(128);
        int playClipOffset = PlayClip.createPlayClip(builder, trackIdx, slotIdx);
        int requestOffset = Request.createRequest(builder, Command.PlayClip, playClipOffset);
        builder.finish(requestOffset);
        BackendManager.getInstance().sendRequest(builder);
    }

    private void sendStopTrack(int trackIdx) {
        FlatBufferBuilder builder = new FlatBufferBuilder(128);
        int stopTrackOffset = StopTrack.createStopTrack(builder, trackIdx);
        int requestOffset = Request.createRequest(builder, Command.StopTrack, stopTrackOffset);
        builder.finish(requestOffset);
        BackendManager.getInstance().sendRequest(builder);
    }
}
