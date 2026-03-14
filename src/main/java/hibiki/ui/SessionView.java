package hibiki.ui;

import javax.swing.*;
import java.awt.*;
import hibiki.BackendManager;
import hibiki.SimpleLaf;
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
import java.awt.datatransfer.DataFlavor;
import java.awt.datatransfer.Transferable;
import java.awt.dnd.*;
import java.util.List;

import hibiki.ipc.Response;
import hibiki.ipc.ClipInfo;
import hibiki.ipc.TrackLevels;
import hibiki.ipc.TrackLevel;
import hibiki.ipc.ClipWaveform;

public class SessionView extends JPanel {
    private static SessionView instance;
    private final SessionViewIpc ipc = new SessionViewIpc(this);

    private JButton[][] slotButtons = new JButton[5][5]; // 4 tracks + master, 5 slots
    String[][] slotPaths = new String[5][5]; // paths to loaded clips
    private LevelMeter[] trackMeters = new LevelMeter[4]; // 0-3 for tracks
    private JPanel[] trackStrips = new JPanel[4]; // Track strip panels for selection highlighting
    JLabel[] trackHeaders = new JLabel[4]; // Track header labels (package-visible for TimelineView sync)
    private int selectedTrack = 0; // Currently selected track (0-based, 0-3)

    public static SessionView getInstance() {
        return instance;
    }

    /** Select track by index (1-based for tracks 1-4) */
    public void selectTrackByIdx(int trackIdx) {
        selectTrack(trackIdx);
    }

    /** Get currently selected track index */
    public int getSelectedTrack() {
        return selectedTrack;
    }

    public SessionView() {
        instance = this;
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

        for (int i = 0; i < 4; i++) {
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

        BackendManager.getInstance().addNotificationListener(notification -> {
            if (notification.responseType() == Response.ClipInfo) {
                ClipInfo info = (ClipInfo) notification.response(new ClipInfo());
                updateSlotLabel(info.trackIndex(), info.slotIndex(), info.name());
                if (info.path() != null && !info.path().isEmpty()) {
                    slotPaths[info.trackIndex()][info.slotIndex()] = info.path();
                } else {
                    slotPaths[info.trackIndex()][info.slotIndex()] = null;
                }
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

    void clearAllSlots() {
        SwingUtilities.invokeLater(() -> {
            for (int t = 1; t <= 4; t++) {
                for (int s = 0; s < 5; s++) {
                    slotPaths[t][s] = null;
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

    void updateSlotLabel(int trackIdx, int slotIdx, String name) {
        SwingUtilities.invokeLater(() -> {
            if (trackIdx >= 0 && trackIdx < 4 && slotIdx >= 0 && slotIdx < 5) {
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

    void updateLevel(int trackIdx, float peakL, float peakR) {
        SwingUtilities.invokeLater(() -> {
            if (trackIdx >= 0 && trackIdx < 4) {
                if (trackMeters[trackIdx] != null) {
                    trackMeters[trackIdx].setLevels(peakL, peakR);
                }
            }
        });
    }

    private void selectTrack(int trackIdx) {
        if (trackIdx == selectedTrack)
            return; // Prevent infinite recursion
        selectedTrack = trackIdx;
        // Sync with TimelineView (both now use 0-based)
        if (TimelineView.getInstance() != null) {
            TimelineView.getInstance().setSelectedTrack(trackIdx);
        }
        // Update visual highlighting - entire track panel, not just header
        for (int i = 0; i < 4; i++) {
            if (trackStrips[i] != null) {
                if (i == selectedTrack) {
                    trackStrips[i].setBackground(Theme.getInstance().ACCENT_BLUE.darker().darker());
                    if (trackHeaders[i] != null) {
                        trackHeaders[i].setBackground(Theme.getInstance().ACCENT_BLUE.darker());
                    }
                } else {
                    trackStrips[i].setBackground(Theme.getInstance().PANEL_BG);
                    if (trackHeaders[i] != null) {
                        trackHeaders[i].setBackground(Theme.getInstance().TRACK_HEADER);
                    }
                }
            }
        }
    }

    /** Show dialog to rename a track (syncs with TimelineView) */
    private void renameTrack(int trackIdx) {
        if (trackIdx < 0 || trackIdx >= 4)
            return;
        // Get current name from TimelineView if available
        String currentName = "Track " + trackIdx;
        if (TimelineView.getInstance() != null && trackIdx < TimelineView.getInstance().tracks.size()) {
            TimelineView.TrackTimeline t = TimelineView.getInstance().tracks.get(trackIdx);
            currentName = t.getDisplayName();
        }
        String newName = JOptionPane.showInputDialog(this, "Enter track name:", currentName);
        if (newName != null) {
            // Update TimelineView (which stores the names)
            if (TimelineView.getInstance() != null && trackIdx < TimelineView.getInstance().tracks.size()) {
                TimelineView.getInstance().tracks.get(trackIdx).customName = newName.isEmpty() ? null : newName;
                TimelineView.getInstance().repaint();
            }
            // Update SessionView header label
            if (trackHeaders[trackIdx] != null) {
                String displayName = (newName == null || newName.isEmpty()) ? "Track " + trackIdx : newName;
                trackHeaders[trackIdx].setText(trackIdx + " " + displayName);
            }
        }
    }

    private JPanel createTrackStrip(String name, int trackIdx) {
        JPanel strip = new JPanel();
        strip.setLayout(new BoxLayout(strip, BoxLayout.Y_AXIS));
        strip.setBackground(Theme.getInstance().PANEL_BG);
        strip.setPreferredSize(new Dimension(Theme.getInstance().scale(110), Theme.getInstance().scale(400)));
        strip.setMaximumSize(new Dimension(Theme.getInstance().scale(110), 32767));
        strip.setBorder(BorderFactory.createMatteBorder(0, 0, 0, 1, Theme.getInstance().BORDER));
        trackStrips[trackIdx] = strip;

        // Header (clickable for track selection)
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
        header.setCursor(Cursor.getPredefinedCursor(Cursor.HAND_CURSOR));
        trackHeaders[trackIdx] = header;

        // Click on header to select track, double-click to rename
        int finalTrackIdx = trackIdx; // Final for lambda
        header.addMouseListener(new MouseAdapter() {
            @Override
            public void mousePressed(MouseEvent e) {
                selectTrack(finalTrackIdx);
            }

            @Override
            public void mouseClicked(MouseEvent e) {
                if (e.getClickCount() == 2) {
                    renameTrack(finalTrackIdx);
                }
            }
        });
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

            // Setup drop target for drag-drop clips (skip in headless mode for tests)
            if (!java.awt.GraphicsEnvironment.isHeadless()) {
                new DropTarget(clipBtn, new DropTargetAdapter() {
                    @Override
                    public void drop(DropTargetDropEvent dtde) {
                        try {
                            dtde.acceptDrop(DnDConstants.ACTION_COPY);
                            Transferable t = dtde.getTransferable();
                            if (t.isDataFlavorSupported(DataFlavor.javaFileListFlavor)) {
                                @SuppressWarnings("unchecked")
                                List<File> files = (List<File>) t.getTransferData(DataFlavor.javaFileListFlavor);
                                if (!files.isEmpty()) {
                                    File file = files.get(0);
                                    String path = file.getAbsolutePath();
                                    String lower = path.toLowerCase();
                                    if (lower.endsWith(".mid") || lower.endsWith(".midi") ||
                                            lower.endsWith(".wav") || lower.endsWith(".mp3") ||
                                            lower.endsWith(".ogg") || lower.endsWith(".flac")) {
                                        // Load clip to this slot
                                        sendLoadClip(trackIdx, slotIdx, path, false);
                                    }
                                }
                            }
                            dtde.dropComplete(true);
                        } catch (Exception ex) {
                            dtde.dropComplete(false);
                        }
                    }

                    @Override
                    public void dragEnter(DropTargetDragEvent dtde) {
                        clipBtn.setBackground(Theme.getInstance().ACCENT_BLUE);
                    }

                    @Override
                    public void dragExit(DropTargetEvent dte) {
                        clipBtn.setBackground(Theme.getInstance().PANEL_BG_LIGHT);
                    }
                });
            }

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
        loadItem.addActionListener(ev -> {
            if (UIManager.getLookAndFeel() instanceof SimpleLaf) {
                Frame frame = (Frame) SwingUtilities.getWindowAncestor(this);
                FileDialog dialog = new FileDialog(frame, "Load Clip", FileDialog.LOAD);
                dialog.setDirectory("testdata");
                dialog.setVisible(true);
                String dir = dialog.getDirectory();
                String file = dialog.getFile();
                if (dir != null && file != null) {
                    sendLoadClip(trackIdx, slotIdx, dir + file, false);
                }
            } else {
                JFileChooser chooser = new JFileChooser("testdata");
                if (chooser.showOpenDialog(this) == JFileChooser.APPROVE_OPTION) {
                    File file = chooser.getSelectedFile();
                    sendLoadClip(trackIdx, slotIdx, file.getAbsolutePath(), false);
                }
            }
        });
        menu.add(loadItem);

        JMenuItem editItem = new JMenuItem("Edit Clip...");
        editItem.addActionListener(e -> {
            String path = slotPaths[trackIdx][slotIdx];
            if (path != null && path.endsWith(".mid")) {
                File file = new File(path);
                JFrame ownerFrame = (JFrame) SwingUtilities.getWindowAncestor(this);
                PianoRoll pr = new PianoRoll(ownerFrame, file, trackIdx, slotIdx);
                pr.setVisible(true);
            } else {
                JOptionPane.showMessageDialog(this, "Can only edit MIDI (.mid) clips.", "Error",
                        JOptionPane.ERROR_MESSAGE);
            }
        });
        menu.add(editItem);

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

    void sendLoadClip(int trackIdx, int slotIdx, String path, boolean isLoop) {
        ipc.sendLoadClip(trackIdx, slotIdx, path, isLoop);
    }

    private void sendSetClipLoop(int trackIdx, int slotIdx, boolean isLoop) {
        ipc.sendSetClipLoop(trackIdx, slotIdx, isLoop);
    }

    private void sendPlayClip(int trackIdx, int slotIdx) {
        ipc.sendPlayClip(trackIdx, slotIdx);
    }

    private void sendStopTrack(int trackIdx) {
        ipc.sendStopTrack(trackIdx);
    }

    private void sendPlayScene(int slotIdx) {
        ipc.sendPlayScene(slotIdx);
    }

    private void sendDeleteClip(int trackIdx, int slotIdx) {
        ipc.sendDeleteClip(trackIdx, slotIdx);
    }

}
