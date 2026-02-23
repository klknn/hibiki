package hibiki.ui;

import javax.swing.*;
import java.awt.*;
import hibiki.BackendManager;
import com.google.flatbuffers.FlatBufferBuilder;
import hibiki.ipc.Request;
import hibiki.ipc.Command;
import hibiki.ipc.PlayClip;
import hibiki.ipc.StopTrack;
import hibiki.ipc.LoadClip;
import hibiki.ipc.SetClipLoop;
import hibiki.ipc.PlayScene;
import hibiki.ipc.DeleteClip;
import java.awt.event.MouseAdapter;
import java.awt.event.MouseEvent;
import java.io.File;

import hibiki.ipc.Response;
import hibiki.ipc.ClipInfo;
import hibiki.ipc.TrackLevels;
import hibiki.ipc.TrackLevel;
import hibiki.ipc.ClipWaveform;

public class SessionView extends JPanel {
    private JButton[][] slotButtons = new JButton[5][5]; // 4 tracks + master, 5 slots
    private LevelMeter[] trackMeters = new LevelMeter[5]; // 1-4 for tracks

    public SessionView() {
        setLayout(new BorderLayout());
        setBackground(Theme.getInstance().BG_DARK);

        JPanel master = createMasterStrip();

        JPanel trackPanel = new JPanel() {
            @Override
            public Dimension getPreferredSize() {
                Dimension d = super.getPreferredSize();
                if (getParent() instanceof JViewport) {
                    d.height = Math.max(d.height, getParent().getHeight());
                }
                return d;
            }
        };
        trackPanel.setLayout(new BoxLayout(trackPanel, BoxLayout.X_AXIS));
        trackPanel.setBackground(Theme.getInstance().BG_DARK);

        for (int i = 1; i <= 4; i++) {
            trackPanel.add(createTrackStrip("Track " + i, i));
        }

        JScrollPane scrollPane = new JScrollPane(trackPanel);
        scrollPane.setHorizontalScrollBarPolicy(JScrollPane.HORIZONTAL_SCROLLBAR_AS_NEEDED);
        scrollPane.setVerticalScrollBarPolicy(JScrollPane.VERTICAL_SCROLLBAR_NEVER);
        scrollPane.setBorder(null);
        scrollPane.setBackground(Theme.getInstance().BG_DARK);
        scrollPane.getHorizontalScrollBar().setUnitIncrement(Theme.getInstance().scale(16));

        add(scrollPane, BorderLayout.CENTER);
        add(master, BorderLayout.EAST);

        // Listen for clip load notifications
        BackendManager.getInstance().addNotificationListener(notification -> {
            if (notification.responseType() == Response.ClipInfo) {
                ClipInfo info = (ClipInfo) notification.response(new ClipInfo());
                updateSlotLabel(info.trackIndex(), info.slotIndex(), info.name());
            } else if (notification.responseType() == Response.ClearProject) {
                clearAllSlots();
            } else if (notification.responseType() == Response.TrackLevels) {
                TrackLevels tl = (TrackLevels) notification.response(new TrackLevels());
                for (int i = 0; i < tl.levelsLength(); i++) {
                    TrackLevel l = tl.levels(i);
                    updateLevel(l.trackIndex(), l.peakL(), l.peakR());
                }
            }
        });
    }

    private void clearAllSlots() {
        SwingUtilities.invokeLater(() -> {
            for (int t = 1; t <= 4; t++) {
                for (int s = 0; s < 5; s++) {
                    JButton btn = slotButtons[t][s];
                    if (btn != null) {
                        btn.setText("");
                        btn.setBackground(Theme.getInstance().PANEL_BG_LIGHT);
                        btn.setForeground(Theme.getInstance().TEXT_NORMAL);
                    }
                }
            }
        });
    }

    private void updateSlotLabel(int trackIdx, int slotIdx, String name) {
        SwingUtilities.invokeLater(() -> {
            if (trackIdx >= 1 && trackIdx <= 4 && slotIdx >= 0 && slotIdx < 5) {
                JButton btn = slotButtons[trackIdx][slotIdx];
                if (btn != null) {
                    if (name.isEmpty()) {
                        btn.setText("");
                        btn.setBackground(Theme.getInstance().PANEL_BG_LIGHT);
                        btn.setForeground(Theme.getInstance().TEXT_NORMAL);
                    } else {
                        btn.setText("<html><center>" + name + "<br>▶</center></html>");
                        btn.setBackground(Theme.getInstance().CLIP_PLAYING);
                        btn.setForeground(Color.BLACK);
                    }
                }
            }
        });
    }

    private void updateLevel(int trackIdx, float peakL, float peakR) {
        SwingUtilities.invokeLater(() -> {
            if (trackIdx >= 1 && trackIdx <= 4) {
                if (trackMeters[trackIdx] != null) {
                    trackMeters[trackIdx].setLevels(peakL, peakR);
                }
            }
        });
    }

    private JPanel createTrackStrip(String name, int trackIdx) {
        JPanel strip = new JPanel();
        strip.setLayout(new BoxLayout(strip, BoxLayout.Y_AXIS));
        strip.setBackground(Theme.getInstance().PANEL_BG);
        strip.setPreferredSize(new Dimension(Theme.getInstance().scale(110), Theme.getInstance().scale(400)));
        strip.setMaximumSize(new Dimension(Theme.getInstance().scale(110), 32767));
        strip.setBorder(BorderFactory.createMatteBorder(0, 0, 0, 1, Theme.getInstance().BORDER));

        // Header
        JLabel header = new JLabel(trackIdx + " " + name, SwingConstants.CENTER);
        header.setAlignmentX(Component.CENTER_ALIGNMENT);
        header.setMinimumSize(new Dimension(Theme.getInstance().scale(110), Theme.getInstance().scale(30)));
        header.setMaximumSize(new Dimension(Theme.getInstance().scale(110), Theme.getInstance().scale(30)));
        header.setPreferredSize(new Dimension(Theme.getInstance().scale(110), Theme.getInstance().scale(30)));
        header.setBackground(Theme.getInstance().TRACK_HEADER);
        header.setForeground(Theme.getInstance().TEXT_BRIGHT);
        header.setFont(Theme.getInstance().FONT_UI_BOLD);
        header.setOpaque(true);
        header.setBorder(BorderFactory.createMatteBorder(0, 0, 1, 0, Theme.getInstance().BORDER));
        strip.add(header);

        // Clips
        for (int i = 0; i < 5; i++) {
            JButton clipBtn = new JButton("");
            clipBtn.setAlignmentX(Component.CENTER_ALIGNMENT);
            clipBtn.setMinimumSize(new Dimension(Theme.getInstance().scale(100), Theme.getInstance().scale(30)));
            clipBtn.setMaximumSize(new Dimension(Theme.getInstance().scale(100), Theme.getInstance().scale(30)));
            clipBtn.setPreferredSize(new Dimension(Theme.getInstance().scale(100), Theme.getInstance().scale(30)));
            clipBtn.setFont(Theme.getInstance().FONT_UI);
            clipBtn.setBackground(Theme.getInstance().PANEL_BG_LIGHT);
            clipBtn.setForeground(Theme.getInstance().TEXT_NORMAL);
            clipBtn.setBorder(BorderFactory.createLineBorder(Theme.getInstance().BORDER));
            clipBtn.setFocusPainted(false);

            int slotIdx = i;
            clipBtn.addActionListener(e -> sendPlayClip(trackIdx, slotIdx));

            clipBtn.addMouseListener(new MouseAdapter() {
                public void mousePressed(MouseEvent e) {
                    if (SwingUtilities.isRightMouseButton(e)) {
                        showClipContextMenu(clipBtn, trackIdx, slotIdx, e.getX(), e.getY());
                    }
                }
            });

            slotButtons[trackIdx][slotIdx] = clipBtn;
            strip.add(Box.createVerticalStrut(Theme.getInstance().scale(2)));
            strip.add(clipBtn);
        }

        strip.add(Box.createVerticalGlue());

        // Level Meter and Controls
        JPanel controls = new JPanel();
        controls.setLayout(new BoxLayout(controls, BoxLayout.X_AXIS));
        controls.setOpaque(false);
        controls.setMaximumSize(new Dimension(Theme.getInstance().scale(110), Theme.getInstance().scale(150)));

        LevelMeter meter = new LevelMeter();
        meter.setPreferredSize(new Dimension(Theme.getInstance().scale(12), Theme.getInstance().scale(100)));
        trackMeters[trackIdx] = meter;

        JPanel volPanel = new JPanel();
        volPanel.setLayout(new BoxLayout(volPanel, BoxLayout.Y_AXIS));
        volPanel.setOpaque(false);

        // Vol
        JSlider volSlider = new JSlider(JSlider.VERTICAL, -70, 6, 0);
        volSlider.setMaximumSize(new Dimension(Theme.getInstance().scale(30), Theme.getInstance().scale(100)));
        volSlider.setBackground(Theme.getInstance().PANEL_BG);
        volPanel.add(volSlider);

        controls.add(Box.createHorizontalStrut(Theme.getInstance().scale(10)));
        controls.add(meter);
        controls.add(Box.createHorizontalStrut(Theme.getInstance().scale(5)));
        controls.add(volPanel);
        controls.add(Box.createHorizontalStrut(Theme.getInstance().scale(10)));

        strip.add(controls);
        strip.add(Box.createVerticalStrut(Theme.getInstance().scale(5)));

        // Pan
        JSlider panSlider = new JSlider(-50, 50, 0);
        panSlider.setMaximumSize(new Dimension(Theme.getInstance().scale(90), Theme.getInstance().scale(20)));
        panSlider.setBackground(Theme.getInstance().PANEL_BG);
        strip.add(createControlLabel("Pan"));
        strip.add(panSlider);

        // Activator
        JButton activeBtn = createFlatButton("" + trackIdx, e -> sendStopTrack(trackIdx));
        activeBtn.setBackground(new Color(200, 160, 50));
        activeBtn.setForeground(Color.BLACK);
        strip.add(Box.createVerticalStrut(Theme.getInstance().scale(5)));
        strip.add(activeBtn);
        strip.add(Box.createVerticalStrut(Theme.getInstance().scale(5)));

        return strip;
    }

    private JPanel createMasterStrip() {
        JPanel strip = new JPanel();
        strip.setLayout(new BoxLayout(strip, BoxLayout.Y_AXIS));
        strip.setBackground(Theme.getInstance().PANEL_BG);
        strip.setPreferredSize(new Dimension(Theme.getInstance().scale(110), Theme.getInstance().scale(400)));
        strip.setMaximumSize(new Dimension(Theme.getInstance().scale(110), 32767));
        strip.setBorder(BorderFactory.createMatteBorder(0, 0, 0, 1, Theme.getInstance().BORDER));

        JLabel header = new JLabel("Master", SwingConstants.CENTER);
        header.setAlignmentX(Component.CENTER_ALIGNMENT);
        header.setMinimumSize(new Dimension(Theme.getInstance().scale(110), Theme.getInstance().scale(30)));
        header.setMaximumSize(new Dimension(Theme.getInstance().scale(110), Theme.getInstance().scale(30)));
        header.setPreferredSize(new Dimension(Theme.getInstance().scale(110), Theme.getInstance().scale(30)));
        header.setBackground(Theme.getInstance().TRACK_HEADER);
        header.setForeground(Theme.getInstance().TEXT_BRIGHT);
        header.setFont(Theme.getInstance().FONT_UI_BOLD);
        header.setOpaque(true);
        header.setBorder(BorderFactory.createMatteBorder(0, 0, 1, 0, Theme.getInstance().BORDER));
        strip.add(header);

        for (int i = 0; i < 5; i++) {
            int sceneIdx = i;
            JButton sceneBtn = createFlatButton((i + 1) + " ►", e -> sendPlayScene(sceneIdx));
            sceneBtn.setMinimumSize(new Dimension(Theme.getInstance().scale(100), Theme.getInstance().scale(30)));
            sceneBtn.setMaximumSize(new Dimension(Theme.getInstance().scale(100), Theme.getInstance().scale(30)));
            sceneBtn.setPreferredSize(new Dimension(Theme.getInstance().scale(100), Theme.getInstance().scale(30)));
            strip.add(Box.createVerticalStrut(Theme.getInstance().scale(2)));
            strip.add(sceneBtn);
        }

        strip.add(Box.createVerticalGlue());

        JSlider masterVol = new JSlider(JSlider.VERTICAL, -70, 6, 0);
        masterVol.setMaximumSize(new Dimension(Theme.getInstance().scale(30), Theme.getInstance().scale(100)));
        masterVol.setBackground(Theme.getInstance().PANEL_BG);
        strip.add(createControlLabel("Master"));
        strip.add(masterVol);

        return strip;
    }

    private JLabel createControlLabel(String text) {
        JLabel l = new JLabel(text, SwingConstants.CENTER);
        l.setAlignmentX(Component.CENTER_ALIGNMENT);
        l.setFont(new Font("SansSerif", Font.PLAIN, Theme.getInstance().scale(9)));
        l.setForeground(Theme.getInstance().TEXT_DIM);
        return l;
    }

    private JButton createFlatButton(String text, java.awt.event.ActionListener listener) {
        JButton btn = new JButton(text);
        btn.setAlignmentX(Component.CENTER_ALIGNMENT);
        btn.setFont(Theme.getInstance().FONT_UI_BOLD);
        btn.setFocusPainted(false);
        btn.setBorder(BorderFactory.createLineBorder(Theme.getInstance().BORDER));
        if (listener != null)
            btn.addActionListener(listener);
        return btn;
    }

    private void showClipContextMenu(JButton btn, int trackIdx, int slotIdx, int x, int y) {
        JPopupMenu menu = new JPopupMenu();

        JMenuItem loadItem = new JMenuItem("Load Clip...");
        loadItem.addActionListener(e -> {
            JFileChooser chooser = new JFileChooser("testdata");
            if (chooser.showOpenDialog(this) == JFileChooser.APPROVE_OPTION) {
                File file = chooser.getSelectedFile();
                sendLoadClip(trackIdx, slotIdx, file.getAbsolutePath(), false);
            }
        });
        menu.add(loadItem);

        JCheckBoxMenuItem loopItem = new JCheckBoxMenuItem("Loop");
        loopItem.addActionListener(e -> {
            sendSetClipLoop(trackIdx, slotIdx, loopItem.isSelected());
        });
        menu.add(loopItem);

        menu.addSeparator();
        JMenuItem deleteItem = new JMenuItem("Delete Clip");
        deleteItem.addActionListener(e -> sendDeleteClip(trackIdx, slotIdx));
        menu.add(deleteItem);

        menu.show(btn, x, y);
    }

    private void sendLoadClip(int trackIdx, int slotIdx, String path, boolean isLoop) {
        FlatBufferBuilder builder = new FlatBufferBuilder(512);
        int pathOff = builder.createString(path);
        LoadClip.startLoadClip(builder);
        LoadClip.addTrackIndex(builder, trackIdx);
        LoadClip.addSlotIndex(builder, slotIdx);
        LoadClip.addPath(builder, pathOff);
        LoadClip.addIsLoop(builder, isLoop);
        int loadOff = LoadClip.endLoadClip(builder);
        int requestOffset = Request.createRequest(builder, Command.LoadClip, loadOff);
        builder.finish(requestOffset);
        BackendManager.getInstance().sendRequest(builder);
    }

    private void sendSetClipLoop(int trackIdx, int slotIdx, boolean isLoop) {
        FlatBufferBuilder builder = new FlatBufferBuilder(128);
        SetClipLoop.startSetClipLoop(builder);
        SetClipLoop.addTrackIndex(builder, trackIdx);
        SetClipLoop.addSlotIndex(builder, slotIdx);
        SetClipLoop.addIsLoop(builder, isLoop);
        int setOff = SetClipLoop.endSetClipLoop(builder);
        int requestOffset = Request.createRequest(builder, Command.SetClipLoop, setOff);
        builder.finish(requestOffset);
        BackendManager.getInstance().sendRequest(builder);
    }

    private void sendPlayClip(int trackIdx, int slotIdx) {
        FlatBufferBuilder builder = new FlatBufferBuilder(128);
        PlayClip.startPlayClip(builder);
        PlayClip.addTrackIndex(builder, trackIdx);
        PlayClip.addSlotIndex(builder, slotIdx);
        int playClipOffset = PlayClip.endPlayClip(builder);
        int requestOffset = Request.createRequest(builder, Command.PlayClip, playClipOffset);
        builder.finish(requestOffset);
        BackendManager.getInstance().sendRequest(builder);
    }

    private void sendStopTrack(int trackIdx) {
        FlatBufferBuilder builder = new FlatBufferBuilder(128);
        StopTrack.startStopTrack(builder);
        StopTrack.addTrackIndex(builder, trackIdx);
        int stopTrackOffset = StopTrack.endStopTrack(builder);
        int requestOffset = Request.createRequest(builder, Command.StopTrack, stopTrackOffset);
        builder.finish(requestOffset);
        BackendManager.getInstance().sendRequest(builder);
    }

    private void sendPlayScene(int slotIdx) {
        FlatBufferBuilder builder = new FlatBufferBuilder(128);
        PlayScene.startPlayScene(builder);
        PlayScene.addSlotIndex(builder, slotIdx);
        int playSceneOff = PlayScene.endPlayScene(builder);
        int requestOffset = Request.createRequest(builder, Command.PlayScene, playSceneOff);
        builder.finish(requestOffset);
        BackendManager.getInstance().sendRequest(builder);
    }

    private void sendDeleteClip(int trackIdx, int slotIdx) {
        FlatBufferBuilder builder = new FlatBufferBuilder(128);
        DeleteClip.startDeleteClip(builder);
        DeleteClip.addTrackIndex(builder, trackIdx);
        DeleteClip.addSlotIndex(builder, slotIdx);
        int deleteOff = DeleteClip.endDeleteClip(builder);
        int requestOffset = Request.createRequest(builder, Command.DeleteClip, deleteOff);
        builder.finish(requestOffset);
        BackendManager.getInstance().sendRequest(builder);

        // Optimistically clear the UI
        updateSlotLabel(trackIdx, slotIdx, "");
    }

    private static class LevelMeter extends JPanel {
        private float levelL = 0;
        private float levelR = 0;

        LevelMeter() {
            setPreferredSize(new Dimension(Theme.getInstance().scale(12), Theme.getInstance().scale(100)));
            setBackground(Color.BLACK);
        }

        void setLevels(float l, float r) {
            this.levelL = l;
            this.levelR = r;
            repaint();
        }

        @Override
        protected void paintComponent(Graphics g) {
            super.paintComponent(g);
            int h = getHeight();
            int w = getWidth();

            // Draw level meters
            g.setColor(Theme.getInstance().ACCENT_GREEN.darker().darker());
            g.fillRect(1, 0, w / 2 - 2, h);
            g.fillRect(w / 2 + 1, 0, w / 2 - 2, h);

            g.setColor(Theme.getInstance().ACCENT_GREEN);
            int hL = (int) (levelL * h);
            int hR = (int) (levelR * h);

            g.fillRect(1, h - hL, w / 2 - 2, hL);
            g.fillRect(w / 2 + 1, h - hR, w / 2 - 2, hR);

            // Draw scale lines
            g.setColor(new Color(255, 255, 255, 50));
            for (int i = 1; i < 4; i++) {
                int y = i * h / 4;
                g.drawLine(1, y, w - 1, y);
            }
        }
    }
}
