package hibiki.ui;

import hibiki.BackendManager;
import hibiki.ipc.Notification;
import hibiki.ipc.TimelineClipInfo;
import hibiki.ipc.ParamList;
import hibiki.ipc.Response;

import javax.swing.*;
import java.awt.*;
import java.awt.event.*;
import java.io.File;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.HashMap;

public class TimelineView extends JPanel implements Theme.ThemeListener {
    private static final int BASE_TRACK_HEIGHT = 80;
    private static final int TIME_RULER_HEIGHT = 30;
    private static final int TRACK_LABEL_WIDTH = 100;
    private static final float BASE_PIXELS_PER_SECOND = 50.0f;

    // Zoom scales (adjustable via sliders)
    private float hZoomScale = 1.0f; // Horizontal zoom multiplier
    private float vZoomScale = 1.0f; // Vertical zoom multiplier

    // Convenience getters for zoom-scaled values
    private int getTrackHeight() {
        return (int) (BASE_TRACK_HEIGHT * vZoomScale);
    }

    private float getPixelsPerSecond() {
        return BASE_PIXELS_PER_SECOND * hZoomScale;
    }

    // Grid mode for rendering - matches PianoRoll
    enum GridMode {
        AUTO("Auto"), // Adaptive based on zoom level
        SECONDS("Seconds"), // Absolute time in seconds
        BAR("1/1"), // Whole bar
        HALF("1/2"), // Half bar
        QUARTER("1/4"), // Quarter note (beat)
        EIGHTH("1/8"), // Eighth note
        SIXTEENTH("1/16"), // Sixteenth note
        THIRTY_SECOND("1/32"), // Thirty-second note
        TRIPLET_QUARTER("1/3"), // Triplet quarter
        TRIPLET_EIGHTH("1/6"), // Triplet eighth
        TRIPLET_16TH("1/12"), // Triplet sixteenth
        TRIPLET_32ND("1/24"); // Triplet thirty-second

        private final String label;

        GridMode(String label) {
            this.label = label;
        }

        @Override
        public String toString() {
            return label;
        }
    }

    private GridMode gridMode = GridMode.AUTO;
    private volatile float bpm = 120.0f;
    private volatile boolean isPlaying = false;

    volatile float playheadPos = 0.0f; // volatile for thread-safe updates from notification thread
    final List<TrackTimeline> tracks = new ArrayList<>();
    private int selectedTrack = 0; // Currently selected track for plugin/clip operations
    private static TimelineView instance; // Static reference for global access
    private final JScrollPane scrollPane;
    private final JPanel contentPanel;
    private JPanel rowHeader; // Track labels panel (needs update on vZoom)
    private final Timer repaintTimer;
    private boolean autoScroll = true; // Auto-scroll to follow playhead during playback
    private int playheadScreenOffset = -1; // Screen X position to keep playhead at during auto-scroll

    // Clip drag-and-drop state
    private ClipRect draggingClip = null;
    private int dragSourceTrack = -1;
    private int dragStartX = 0;
    private int dragStartY = 0;
    private int dragCurrentY = 0; // Current Y position for rendering during cross-track drag
    private float dragOriginalStartTime = 0;
    private boolean isDragging = false;

    // Clip creation state (like Piano Roll note creation)
    private boolean creatingClip = false;
    private int creatingTrackIdx = -1;
    private float creatingStartTime = 0;
    private ClipRect creatingClipRect = null;

    /** Get the singleton TimelineView instance */
    public static TimelineView getInstance() {
        return instance;
    }

    public TimelineView() {
        instance = this; // Set the static reference
        Theme.getInstance().addListener(this);
        setLayout(new BorderLayout());
        setBackground(Theme.getInstance().BG_DARK);

        contentPanel = new JPanel() {
            @Override
            protected void paintComponent(Graphics g) {
                super.paintComponent(g);
                drawTimeline(g);
            }
        };
        contentPanel.setLayout(null);
        contentPanel.setBackground(Theme.getInstance().BG_DARK);

        // Create fixed row header for track labels
        rowHeader = new JPanel() {
            @Override
            protected void paintComponent(Graphics g) {
                super.paintComponent(g);
                drawTrackLabels(g);
            }

            @Override
            public Dimension getPreferredSize() {
                int scaleLabelWidth = Theme.getInstance().scale(TRACK_LABEL_WIDTH);
                int scaleTimeRuler = Theme.getInstance().scale(TIME_RULER_HEIGHT);
                int scaleTrackHeight = Theme.getInstance().scale(getTrackHeight());
                return new Dimension(scaleLabelWidth, scaleTimeRuler + tracks.size() * scaleTrackHeight);
            }
        };
        rowHeader.setBackground(Theme.getInstance().BG_DARK);

        // Add mouse listener to rowHeader for track selection and rename
        rowHeader.addMouseListener(new MouseAdapter() {
            @Override
            public void mousePressed(MouseEvent e) {
                int scaleTimeRuler = Theme.getInstance().scale(TIME_RULER_HEIGHT);
                int scaleTrackHeight = Theme.getInstance().scale(getTrackHeight());
                if (e.getY() >= scaleTimeRuler) {
                    int trackIdx = (e.getY() - scaleTimeRuler) / scaleTrackHeight;
                    if (trackIdx >= 0 && trackIdx < tracks.size()) {
                        setSelectedTrack(trackIdx);
                        rowHeader.repaint();
                        contentPanel.repaint();
                    }
                }
            }

            @Override
            public void mouseClicked(MouseEvent e) {
                if (e.getClickCount() == 2) {
                    int scaleTimeRuler = Theme.getInstance().scale(TIME_RULER_HEIGHT);
                    int scaleTrackHeight = Theme.getInstance().scale(getTrackHeight());
                    if (e.getY() >= scaleTimeRuler) {
                        int trackIdx = (e.getY() - scaleTimeRuler) / scaleTrackHeight;
                        if (trackIdx >= 0 && trackIdx < tracks.size()) {
                            renameTrack(trackIdx);
                        }
                    }
                }
            }
        });

        scrollPane = new JScrollPane(contentPanel);
        scrollPane.setBorder(null);
        scrollPane.setRowHeaderView(rowHeader);
        scrollPane.getVerticalScrollBar().setUnitIncrement(16);
        add(scrollPane, BorderLayout.CENTER);

        // Initial tracks
        for (int i = 0; i < 8; i++) {
            tracks.add(new TrackTimeline(i));
        }
        updateContentSize();

        repaintTimer = new Timer(33, e -> {
            // Auto-scroll to follow playhead during playback
            if (isPlaying && autoScroll) {
                // No label offset - content panel starts at x=0
                int playheadX = (int) (playheadPos * getPixelsPerSecond());

                // Keep playhead at the same screen position where playback started
                int targetScrollX = playheadX - playheadScreenOffset;
                targetScrollX = Math.max(0, targetScrollX);
                scrollPane.getHorizontalScrollBar().setValue(targetScrollX);
            }
            contentPanel.repaint();
        });
        repaintTimer.start();

        BackendManager.getInstance().addNotificationListener(this::handleNotification);
        
        setupMouseListeners();
        setupDropTarget();
        setupControls();
    }

    private void setupControls() {
        // Control panel at bottom right, matching PianoRoll layout
        JPanel controlPanel = new JPanel(new FlowLayout(FlowLayout.RIGHT, 5, 2));
        controlPanel.setBackground(Theme.getInstance().PANEL_BG);
        controlPanel.setBorder(BorderFactory.createMatteBorder(1, 0, 0, 0, Theme.getInstance().BORDER));

        // Grid mode selector
        JLabel gridLabel = new JLabel("Grid:");
        gridLabel.setForeground(Theme.getInstance().TEXT_DIM);
        controlPanel.add(gridLabel);
        JComboBox<GridMode> gridCombo = new JComboBox<>(GridMode.values());
        gridCombo.setSelectedItem(gridMode);
        gridCombo.setPreferredSize(new Dimension(70, 20));
        gridCombo.addActionListener(e -> {
            gridMode = (GridMode) gridCombo.getSelectedItem();
            repaint();
        });
        controlPanel.add(gridCombo);

        // Horizontal zoom slider
        JLabel hLabel = new JLabel("H:");
        hLabel.setForeground(Theme.getInstance().TEXT_DIM);
        controlPanel.add(hLabel);
        JSlider hZoomSlider = new JSlider(5, 400, 100); // 5% to 400%
        hZoomSlider.setPreferredSize(new Dimension(80, 20));
        hZoomSlider.addChangeListener(e -> {
            hZoomScale = hZoomSlider.getValue() / 100.0f;
            updateContentSize();
            contentPanel.repaint();
        });
        controlPanel.add(hZoomSlider);

        // Vertical zoom slider
        JLabel vLabel = new JLabel("V:");
        vLabel.setForeground(Theme.getInstance().TEXT_DIM);
        controlPanel.add(vLabel);
        JSlider vZoomSlider = new JSlider(5, 200, 100); // 5% to 200%
        vZoomSlider.setPreferredSize(new Dimension(80, 20));
        vZoomSlider.addChangeListener(e -> {
            vZoomScale = vZoomSlider.getValue() / 100.0f;
            updateContentSize();
            // Also update track label header heights
            if (rowHeader != null) {
                rowHeader.revalidate();
                rowHeader.repaint();
            }
            contentPanel.repaint();
        });
        controlPanel.add(vZoomSlider);

        // Auto-scroll checkbox
        JCheckBox autoScrollCheck = new JCheckBox("Auto-scroll", autoScroll);
        autoScrollCheck.setForeground(Theme.getInstance().TEXT_DIM);
        autoScrollCheck.setOpaque(false);
        autoScrollCheck.addActionListener(e -> autoScroll = autoScrollCheck.isSelected());
        controlPanel.add(autoScrollCheck);

        add(controlPanel, BorderLayout.SOUTH);
    }

    private void setupDropTarget() {
        if (java.awt.GraphicsEnvironment.isHeadless())
            return;
        new java.awt.dnd.DropTarget(contentPanel, new java.awt.dnd.DropTargetAdapter() {
            @Override
            public void drop(java.awt.dnd.DropTargetDropEvent dtde) {
                try {
                    if (dtde.isDataFlavorSupported(java.awt.datatransfer.DataFlavor.stringFlavor)) {
                        dtde.acceptDrop(java.awt.dnd.DnDConstants.ACTION_COPY);
                        String data = (String) dtde.getTransferable().getTransferData(java.awt.datatransfer.DataFlavor.stringFlavor);
                        dtde.dropComplete(true);

                        String[] parts = data.split(":", 2);
                        if (parts.length == 2) {
                            String type = parts[0];
                            String path = parts[1];
                            
                            Point p = dtde.getLocation();
                            int trackIndex = (p.y - Theme.getInstance().scale(TIME_RULER_HEIGHT))
                                    / Theme.getInstance().scale(getTrackHeight());
                            double timeSec = p.x / getPixelsPerSecond();

                            // Snap to nearest grid boundary
                            float secondsPerBeat = 60.0f / bpm;
                            float snapSeconds = getGridSnapSeconds(gridMode, secondsPerBeat);
                            if (snapSeconds > 0) {
                                timeSec = Math.round(timeSec / snapSeconds) * snapSeconds;
                            }

                            if (trackIndex >= 0 && trackIndex < tracks.size()) {
                                BackendManager.getInstance().addTimelineClip(trackIndex, path, (float)timeSec);
                            }
                        }
                    } else {
                        dtde.rejectDrop();
                    }
                } catch (Exception e) {
                    e.printStackTrace();
                    dtde.rejectDrop();
                }
            }
        });
    }

    private void updateContentSize() {
        // Calculate content duration from longest track content
        float maxEndTime = 60.0f; // Minimum 60 seconds default
        for (TrackTimeline track : tracks) {
            for (ClipRect clip : track.clips) {
                float endTime = clip.startTime + clip.duration;
                if (endTime > maxEndTime) {
                    maxEndTime = endTime;
                }
            }
        }
        // Add some padding (20% more)
        int width = (int) (getPixelsPerSecond() * maxEndTime * 1.2f);
        int height = tracks.size() * Theme.getInstance().scale(getTrackHeight())
                + Theme.getInstance().scale(TIME_RULER_HEIGHT);
        contentPanel.setPreferredSize(new Dimension(width, height));
        contentPanel.revalidate();
    }

    private void setupMouseListeners() {
        contentPanel.addMouseListener(new MouseAdapter() {
            @Override
            public void mousePressed(MouseEvent e) {
                int scaleTimeRuler = Theme.getInstance().scale(TIME_RULER_HEIGHT);
                if (e.getY() < scaleTimeRuler) {
                    updatePlayhead(e.getX());
                } else {
                    // Track selection
                    int scaleTrackHeight = Theme.getInstance().scale(getTrackHeight());
                    int trackIdx = (e.getY() - scaleTimeRuler) / scaleTrackHeight;
                    if (trackIdx >= 0 && trackIdx < tracks.size()) {
                        setSelectedTrack(trackIdx);

                        // Right-click: show clip context menu if clicked on a clip, or empty area menu
                        if (SwingUtilities.isRightMouseButton(e)) {
                            ClipRect clip = findClipAtPosition(trackIdx, e.getX());
                            if (clip != null) {
                                showClipContextMenu(trackIdx, clip, e.getX(), e.getY());
                            } else {
                                // Show empty area context menu with "Create New Clip"
                                float clickTime = e.getX() / getPixelsPerSecond();
                                showEmptyAreaContextMenu(trackIdx, clickTime, e.getX(), e.getY());
                            }
                        } else if (SwingUtilities.isLeftMouseButton(e)) {
                            // Left-click: start dragging if on a clip, or start creating a new clip
                            ClipRect clip = findClipAtPosition(trackIdx, e.getX());
                            if (clip != null) {
                                draggingClip = clip;
                                dragSourceTrack = trackIdx;
                                dragStartX = e.getX();
                                dragStartY = e.getY();
                                dragOriginalStartTime = clip.startTime;
                                isDragging = false; // Will become true after threshold
                            } else {
                                // Start creating a new clip in empty area
                                creatingClip = true;
                                creatingTrackIdx = trackIdx;
                                float startTime = e.getX() / getPixelsPerSecond();
                                // Snap to grid unless shift is held
                                if (!e.isShiftDown()) {
                                    startTime = snapToBar(startTime);
                                }
                                creatingStartTime = startTime;
                                // Create temporary clip rectangle for visual feedback
                                creatingClipRect = new ClipRect();
                                creatingClipRect.name = "New Clip";
                                creatingClipRect.startTime = startTime;
                                creatingClipRect.duration = 0;
                            }
                        }
                    }
                }
            }

            @Override
            public void mouseReleased(MouseEvent e) {
                if (draggingClip != null && isDragging) {
                    int scaleTimeRuler = Theme.getInstance().scale(TIME_RULER_HEIGHT);
                    int scaleTrackHeight = Theme.getInstance().scale(getTrackHeight());
                    int targetTrackIdx = (e.getY() - scaleTimeRuler) / scaleTrackHeight;
                    targetTrackIdx = Math.max(0, Math.min(tracks.size() - 1, targetTrackIdx));

                    float newStartTime = Math.max(0, e.getX() / getPixelsPerSecond());
                    // Snap to bar unless shift is held
                    if (!e.isShiftDown()) {
                        newStartTime = snapToBar(newStartTime);
                    }

                    boolean isCopy = e.isAltDown();

                    if (isCopy) {
                        // Alt+drag: Copy clip to new location
                        ClipRect newClip = new ClipRect();
                        newClip.name = draggingClip.name;
                        newClip.path = draggingClip.path;
                        newClip.startTime = newStartTime;
                        newClip.duration = draggingClip.duration;
                        newClip.waveform = draggingClip.waveform;

                        // Add to target track
                        TrackTimeline targetTrack = tracks.get(targetTrackIdx);
                        targetTrack.clips.add(newClip);
                        targetTrack.clipMap.put(targetTrack.clips.size() - 1, newClip);

                        // Restore original clip position
                        draggingClip.startTime = dragOriginalStartTime;
                    } else {
                        // Normal drag: Move clip to new location
                        if (targetTrackIdx != dragSourceTrack) {
                            // Moving to different track
                            TrackTimeline sourceTrack = tracks.get(dragSourceTrack);
                            TrackTimeline targetTrack = tracks.get(targetTrackIdx);

                            // Remove from source
                            int clipIdx = sourceTrack.clips.indexOf(draggingClip);
                            if (clipIdx >= 0) {
                                sourceTrack.clips.remove(clipIdx);
                                sourceTrack.clipMap.clear();
                                for (int i = 0; i < sourceTrack.clips.size(); i++) {
                                    sourceTrack.clipMap.put(i, sourceTrack.clips.get(i));
                                }
                            }

                            // Add to target
                            draggingClip.startTime = newStartTime;
                            targetTrack.clips.add(draggingClip);
                            targetTrack.clipMap.put(targetTrack.clips.size() - 1, draggingClip);
                        } else {
                            // Same track, just update time
                            draggingClip.startTime = newStartTime;
                        }
                    }

                    updateContentSize();
                    repaint();
                }

                // Reset drag state
                draggingClip = null;
                dragSourceTrack = -1;
                isDragging = false;

                // Handle clip creation completion
                if (creatingClip && creatingClipRect != null) {
                    float endTime = Math.max(0, e.getX() / getPixelsPerSecond());
                    if (!e.isShiftDown()) {
                        endTime = snapToBar(endTime);
                    }
                    float duration = endTime - creatingStartTime;

                    // Only create if duration is positive and meaningful
                    if (duration > 0.1f) {
                        creatingClipRect.duration = duration;

                        // Add the new clip to the track
                        TrackTimeline track = tracks.get(creatingTrackIdx);
                        track.clips.add(creatingClipRect);
                        track.clipMap.put(track.clips.size() - 1, creatingClipRect);
                        updateContentSize();
                    }
                    repaint();
                }

                // Reset creation state
                creatingClip = false;
                creatingTrackIdx = -1;
                creatingClipRect = null;
            }
        });

        contentPanel.addMouseMotionListener(new MouseMotionAdapter() {
            @Override
            public void mouseDragged(MouseEvent e) {
                int scaleTimeRuler = Theme.getInstance().scale(TIME_RULER_HEIGHT);
                if (e.getY() < scaleTimeRuler && draggingClip == null && !creatingClip) {
                    updatePlayhead(e.getX());
                } else if (creatingClip && creatingClipRect != null) {
                    // Update clip duration during creation
                    float endTime = Math.max(0, e.getX() / getPixelsPerSecond());
                    if (!e.isShiftDown()) {
                        endTime = snapToBar(endTime);
                    }
                    creatingClipRect.duration = Math.max(0, endTime - creatingStartTime);
                    repaint();
                } else if (draggingClip != null) {
                    // Check drag threshold (5 pixels)
                    if (!isDragging) {
                        int dx = e.getX() - dragStartX;
                        int dy = e.getY() - dragStartY;
                        if (Math.abs(dx) > 5 || Math.abs(dy) > 5) {
                            isDragging = true;
                        }
                    }

                    if (isDragging) {
                        // Update clip position visually during drag
                        float newStartTime = Math.max(0, e.getX() / getPixelsPerSecond());
                        // Snap to bar unless shift is held
                        if (!e.isShiftDown()) {
                            newStartTime = snapToBar(newStartTime);
                        }
                        draggingClip.startTime = newStartTime;
                        dragCurrentY = e.getY(); // Track Y for cross-track rendering
                        repaint();
                    }
                }
            }
        });
    }

    /** Snap time to nearest bar boundary based on current BPM */
    private float snapToBar(float time) {
        float secondsPerBeat = 60.0f / bpm;
        float secondsPerBar = secondsPerBeat * 4; // 4/4 time signature
        return Math.round(time / secondsPerBar) * secondsPerBar;
    }

    /**
     * Get the currently selected track index for plugin/clip operations (0-based)
     */
    public int getSelectedTrack() {
        return selectedTrack; // 0-based to match internal track array and backend notifications
    }

    /** Set the selected track (for sync with SessionView) */
    public void setSelectedTrack(int trackIdx) {
        if (trackIdx >= 0 && trackIdx != selectedTrack) {
            selectedTrack = trackIdx;
            // Ensure track exists
            while (tracks.size() <= selectedTrack) {
                tracks.add(new TrackTimeline(tracks.size()));
            }
            // Sync with SessionView (0-based)
            if (SessionView.getInstance() != null) {
                SessionView.getInstance().selectTrackByIdx(trackIdx);
            }
            // Notify PluginPane about track selection change
            if (PluginPane.getInstance() != null) {
                PluginPane.getInstance().setSelectedTrack(trackIdx);
            }
            repaint();
        }
    }

    /** Show dialog to rename a track */
    private void renameTrack(int trackIdx) {
        if (trackIdx < 0 || trackIdx >= tracks.size())
            return;
        TrackTimeline track = tracks.get(trackIdx);
        String currentName = track.customName != null ? track.customName : "Track " + trackIdx;
        String newName = JOptionPane.showInputDialog(this, "Enter track name:", currentName);
        if (newName != null) {
            track.customName = newName.isEmpty() ? null : newName;
            repaint();
        }
    }

    /** Find clip at the given x position in the specified track */
    private ClipRect findClipAtPosition(int trackIdx, int x) {
        if (trackIdx < 0 || trackIdx >= tracks.size())
            return null;
        TrackTimeline track = tracks.get(trackIdx);
        float clickTime = x / getPixelsPerSecond();
        for (ClipRect clip : track.clips) {
            if (clickTime >= clip.startTime && clickTime <= clip.startTime + clip.duration) {
                return clip;
            }
        }
        return null;
    }

    /** Show context menu for a timeline clip */
    private void showClipContextMenu(int trackIdx, ClipRect clip, int x, int y) {
        JPopupMenu menu = new JPopupMenu();

        // Edit Clip (MIDI only)
        JMenuItem editItem = new JMenuItem("Edit Clip...");
        editItem.addActionListener(e -> {
            boolean isMidi = (clip.path != null && clip.path.endsWith(".mid"))
                    || clip.path == null; // Empty clips are treated as MIDI
            if (isMidi) {
                File file = clip.path != null ? new File(clip.path) : new File("New Clip.mid");
                JFrame ownerFrame = (JFrame) SwingUtilities.getWindowAncestor(this);
                // Find clip index in track's timeline clips
                TrackTimeline trackTimeline = tracks.get(trackIdx);
                int clipIndex = -1;
                for (int i = 0; i < trackTimeline.clips.size(); i++) {
                    if (trackTimeline.clips.get(i) == clip) {
                        clipIndex = i;
                        break;
                    }
                }
                // Use 6-arg constructor: slotIdx=-1 for timeline clips, clipIdx=actual index, clipStartTime=clip.startTime
                PianoRoll pr = new PianoRoll(ownerFrame, file, trackIdx, -1, clipIndex, clip.startTime);
                pr.setVisible(true);
            } else {
                JOptionPane.showMessageDialog(this, "Can only edit MIDI (.mid) clips.", "Error",
                        JOptionPane.ERROR_MESSAGE);
            }
        });
        menu.add(editItem);

        // Delete Clip
        menu.addSeparator();
        JMenuItem deleteItem = new JMenuItem("Delete Clip");
        deleteItem.addActionListener(e -> {
            // Find clip index and remove from GUI
            // Note: Backend delete not implemented yet, just removes from display
            TrackTimeline track = tracks.get(trackIdx);
            int clipIdx = track.clips.indexOf(clip);
            if (clipIdx >= 0) {
                track.clips.remove(clipIdx);
                track.clipMap.clear();
                for (int i = 0; i < track.clips.size(); i++) {
                    track.clipMap.put(i, track.clips.get(i));
                }
                updateContentSize();
                repaint();
            }
        });
        menu.add(deleteItem);

        menu.show(contentPanel, x, y);
    }

    /** Show context menu for empty track area */
    private void showEmptyAreaContextMenu(int trackIdx, float clickTime, int x, int y) {
        JPopupMenu menu = new JPopupMenu();

        // Create New Clip
        JMenuItem createItem = new JMenuItem("Create New Clip");
        createItem.addActionListener(e -> {
            // Create a new clip at the clicked position
            float snapTime = snapToBar(clickTime);
            float secondsPerBar = (60.0f / bpm) * 4; // 1 bar duration

            ClipRect newClip = new ClipRect();
            newClip.name = "New Clip";
            newClip.startTime = snapTime;
            newClip.duration = secondsPerBar; // Default to 1 bar

            TrackTimeline track = tracks.get(trackIdx);
            track.clips.add(newClip);
            track.clipMap.put(track.clips.size() - 1, newClip);

            updateContentSize();
            repaint();
        });
        menu.add(createItem);

        menu.show(contentPanel, x, y);
    }

    private void updatePlayhead(int x) {
        // Content panel starts at x=0, no label offset needed
        float timelineX = Math.max(0, x);
        playheadPos = timelineX / getPixelsPerSecond();
        BackendManager.getInstance().seek(playheadPos);
        repaint();
    }

    public void handleNotification(Notification n) {
        if (n.responseType() == hibiki.ipc.Response.PlayheadInfo) {
            hibiki.ipc.PlayheadInfo info = (hibiki.ipc.PlayheadInfo) n.response(new hibiki.ipc.PlayheadInfo());
            playheadPos = info.positionSec();
            bpm = info.bpm();
            boolean wasPlaying = isPlaying;
            isPlaying = info.isPlaying();

            // Capture playhead screen position when playback starts
            if (isPlaying && !wasPlaying && autoScroll) {
                // No label offset - content panel starts at x=0
                int playheadX = (int) (playheadPos * getPixelsPerSecond());
                int scrollX = scrollPane.getHorizontalScrollBar().getValue();
                playheadScreenOffset = playheadX - scrollX;
            }
        } else if (n.responseType() == hibiki.ipc.Response.TimelineClipInfo) {
            TimelineClipInfo info = (TimelineClipInfo) n.response(new TimelineClipInfo());
            int tidx = info.trackIndex();

            while (tracks.size() <= tidx) {
                tracks.add(new TrackTimeline(tracks.size()));
            }
            tracks.get(tidx).addOrUpdateClip(info);
            updateContentSize();
        } else if (n.responseType() == Response.ParamList) {
            // Track plugin names to display in track labels
            ParamList paramList = (ParamList) n.response(new ParamList());
            int tidx = paramList.trackIndex();
            while (tracks.size() <= tidx) {
                tracks.add(new TrackTimeline(tracks.size()));
            }
            if (paramList.pluginName() != null && !paramList.pluginName().isEmpty()) {
                tracks.get(tidx).pluginName = paramList.pluginName();
                tracks.get(tidx).isInstrument = paramList.isInstrument();
            }
        } else if (n.responseType() == hibiki.ipc.Response.ClearProject) {
            for (TrackTimeline t : tracks) {
                t.clips.clear();
                t.clipMap.clear();
                t.pluginName = null;
                t.isInstrument = false;
                t.customName = null; // Clear track names on project clear
            }
        } else if (n.responseType() == hibiki.ipc.Response.TrackInfo) {
            // Receive track name from project load
            hibiki.ipc.TrackInfo info = (hibiki.ipc.TrackInfo) n.response(new hibiki.ipc.TrackInfo());
            int tidx = info.trackIndex();
            while (tracks.size() <= tidx) {
                tracks.add(new TrackTimeline(tracks.size()));
            }
            String name = info.name();
            tracks.get(tidx).customName = (name == null || name.isEmpty()) ? null : name;
            repaint();
            // Sync with SessionView
            if (SessionView.getInstance() != null && SessionView.getInstance().trackHeaders.length > tidx) {
                JLabel header = SessionView.getInstance().trackHeaders[tidx];
                if (header != null) {
                    String displayName = tracks.get(tidx).getDisplayName();
                    header.setText(tidx + " " + displayName);
                }
            }
        }
    }

    private void drawTrackLabels(Graphics g) {
        Graphics2D g2 = (Graphics2D) g;
        g2.setRenderingHint(RenderingHints.KEY_ANTIALIASING, RenderingHints.VALUE_ANTIALIAS_ON);

        int scaleTimeRuler = Theme.getInstance().scale(TIME_RULER_HEIGHT);
        int scaleTrackHeight = Theme.getInstance().scale(getTrackHeight());
        int scaleLabelWidth = Theme.getInstance().scale(TRACK_LABEL_WIDTH);

        // Draw time ruler corner
        g2.setColor(Theme.getInstance().BG_DARKER);
        g2.fillRect(0, 0, scaleLabelWidth, scaleTimeRuler);

        // Draw track labels
        for (int i = 0; i < tracks.size(); i++) {
            int y = scaleTimeRuler + i * scaleTrackHeight;

            // Draw label background
            if (i == selectedTrack) {
                g2.setColor(Theme.getInstance().ACCENT_BLUE.darker());
            } else {
                g2.setColor(Theme.getInstance().TRACK_HEADER);
            }
            g2.fillRect(0, y, scaleLabelWidth, scaleTrackHeight - 1);

            // Draw track number
            g2.setColor(Theme.getInstance().TEXT_BRIGHT);
            g2.setFont(Theme.getInstance().FONT_UI_BOLD);
            g2.drawString(tracks.get(i).getDisplayName(), 5, y + 16);

            // Draw plugin name if available
            TrackTimeline track = tracks.get(i);
            if (track.pluginName != null) {
                g2.setFont(Theme.getInstance().FONT_UI);
                g2.setColor(track.isInstrument ? Theme.getInstance().ACCENT_ORANGE : Theme.getInstance().TEXT_DIM);
                String pname = track.pluginName;
                if (pname.length() > 12)
                    pname = pname.substring(0, 11) + "…";
                g2.drawString(pname, 5, y + 32);
            } else {
                g2.setFont(Theme.getInstance().FONT_UI);
                g2.setColor(Theme.getInstance().TEXT_DIM);
                g2.drawString("(no plugin)", 5, y + 32);
            }

            // Draw separator line
            g2.setColor(Theme.getInstance().BORDER);
            g2.drawLine(0, y + scaleTrackHeight - 1, scaleLabelWidth, y + scaleTrackHeight - 1);
        }
    }

    private void drawTimeline(Graphics g) {
        Graphics2D g2 = (Graphics2D) g;
        g2.setRenderingHint(RenderingHints.KEY_ANTIALIASING, RenderingHints.VALUE_ANTIALIAS_ON);

        int scaleTimeRuler = Theme.getInstance().scale(TIME_RULER_HEIGHT);
        int scaleTrackHeight = Theme.getInstance().scale(getTrackHeight());
        int scaleLabelWidth = 0; // Labels are in the row header now, content starts at x=0

        // Draw tracks background (no labels - they're in the row header)
        for (int i = 0; i < tracks.size(); i++) {
            int y = scaleTimeRuler + i * scaleTrackHeight;
            // Highlight selected track
            if (i == selectedTrack) {
                g2.setColor(Theme.getInstance().ACCENT_BLUE.darker().darker());
            } else {
                g2.setColor(i % 2 == 0 ? Theme.getInstance().BG_DARK : Theme.getInstance().BG_DARKER);
            }
            g2.fillRect(0, y, contentPanel.getWidth(), scaleTrackHeight);
            g2.setColor(Theme.getInstance().PANEL_BG_LIGHT.darker());
            g2.drawLine(0, y + scaleTrackHeight - 1, contentPanel.getWidth(), y + scaleTrackHeight - 1);
        }
        // Draw vertical grid lines through track area
        int trackAreaBottom = scaleTimeRuler + tracks.size() * scaleTrackHeight;
        float secondsPerBeat = 60.0f / bpm;
        float secondsPerBar = secondsPerBeat * 4;
        float gridSeconds = getGridSnapSeconds(gridMode, secondsPerBeat);

        // Draw subdivision grid lines
        if (gridSeconds > 0) {
            float gridWidth = gridSeconds * getPixelsPerSecond();
            if (gridWidth >= 2) {
                g2.setColor(new Color(255, 255, 255, 15));
                for (float t = 0; t * getPixelsPerSecond() < contentPanel.getWidth(); t += gridSeconds) {
                    int x = scaleLabelWidth + (int) (t * getPixelsPerSecond());
                    g2.drawLine(x, scaleTimeRuler, x, trackAreaBottom);
                }
            }
        }

        // Draw beat lines (quarter notes)
        float beatWidth = secondsPerBeat * getPixelsPerSecond();
        if (beatWidth >= 4 && gridSeconds < secondsPerBeat) {
            g2.setColor(new Color(255, 255, 255, 25));
            for (float t = 0; t * getPixelsPerSecond() < contentPanel.getWidth(); t += secondsPerBeat) {
                int x = scaleLabelWidth + (int) (t * getPixelsPerSecond());
                g2.drawLine(x, scaleTimeRuler, x, trackAreaBottom);
            }
        }

        // Draw bar lines - brightest
        float barWidth = secondsPerBar * getPixelsPerSecond();
        if (barWidth >= 4) {
            g2.setColor(new Color(255, 255, 255, 40));
            for (float t = 0; t * getPixelsPerSecond() < contentPanel.getWidth(); t += secondsPerBar) {
                int x = scaleLabelWidth + (int) (t * getPixelsPerSecond());
                g2.drawLine(x, scaleTimeRuler, x, trackAreaBottom);
            }
        }

        // Draw ghost shadow of dragged clip at original position
        if (isDragging && draggingClip != null && dragSourceTrack >= 0) {
            int ghostY = scaleTimeRuler + dragSourceTrack * scaleTrackHeight + 5;
            int ghostX = scaleLabelWidth + (int) (dragOriginalStartTime * getPixelsPerSecond());
            int ghostW = (int) (draggingClip.duration * getPixelsPerSecond());
            int ghostH = scaleTrackHeight - 10;

            // Draw semi-transparent ghost outline
            Graphics2D g2d = (Graphics2D) g2;
            Composite oldComposite = g2d.getComposite();
            g2d.setComposite(AlphaComposite.getInstance(AlphaComposite.SRC_OVER, 0.3f));
            g2d.setColor(Theme.getInstance().ACCENT_BLUE.darker());
            g2d.fillRoundRect(ghostX, ghostY, ghostW, ghostH, 8, 8);
            g2d.setComposite(oldComposite);

            // Draw dashed border
            g2d.setColor(Theme.getInstance().ACCENT_BLUE);
            Stroke oldStroke = g2d.getStroke();
            g2d.setStroke(
                    new BasicStroke(1.5f, BasicStroke.CAP_BUTT, BasicStroke.JOIN_BEVEL, 0, new float[] { 4, 4 }, 0));
            g2d.drawRoundRect(ghostX, ghostY, ghostW, ghostH, 8, 8);
            g2d.setStroke(oldStroke);
        }

        // Draw clips
        for (int i = 0; i < tracks.size(); i++) {
            int y = scaleTimeRuler + i * scaleTrackHeight + 5;
            for (ClipRect clip : tracks.get(i).clips) {
                // Skip dragging clip - will be drawn separately at cursor position
                if (isDragging && clip == draggingClip) {
                    continue;
                }
                int x = scaleLabelWidth + (int) (clip.startTime * getPixelsPerSecond());
                int w = (int) (clip.duration * getPixelsPerSecond());
                int h = scaleTrackHeight - 10;

                g2.setColor(Theme.getInstance().ACCENT_BLUE.darker());
                g2.fillRoundRect(x, y, w, h, 8, 8);

                boolean isMidi = clip.path.toLowerCase().endsWith(".mid") || clip.path.toLowerCase().endsWith(".midi");

                if (isMidi) {
                    if (clip.waveform != null && clip.waveform.length > 0) {
                        g2.setColor(new Color(255, 255, 255, 200));
                        for (int nIdx = 0; nIdx + 2 < clip.waveform.length; nIdx += 3) {
                            float startRatio = clip.waveform[nIdx]; // Now 0-1 ratio
                            float pitch = clip.waveform[nIdx+1];
                            float durationRatio = clip.waveform[nIdx + 2]; // Now 0-1 ratio

                            int nx = x + (int) (startRatio * w);
                            int nw = (int) (durationRatio * w);
                            if (nw < 2) nw = 2; // Minimum visible width
                            
                            int minPitch = 21; // A0
                            int maxPitch = 108; // C8
                            float normalizedPitch = (pitch - minPitch) / (float)(maxPitch - minPitch);
                            if (normalizedPitch < 0) normalizedPitch = 0;
                            if (normalizedPitch > 1) normalizedPitch = 1;
                            
                            int nh = Math.max(2, h / 40);
                            int ny = y + h - (int)(normalizedPitch * (h - nh)) - nh;

                            // Clip to box
                            if (nx < x + w && nx + nw >= x) {
                                int drawX = Math.max(x, nx);
                                int drawW = Math.min(x + w - drawX, nx + nw - drawX);
                                g2.fillRect(drawX, ny, drawW, nh);
                            }
                        }
                    }
                } else {
                    // Draw waveform inside audio clip
                    if (clip.waveform != null && clip.waveform.length > 0) {
                        g2.setColor(new Color(255, 255, 255, 120));
                        int midY = y + h / 2;
                        int halfH = h / 2 - 4;
                        for (int px = 0; px < w && px < clip.waveform.length; px++) {
                            int wfIdx = (int)((float)px / w * clip.waveform.length);
                            if (wfIdx >= clip.waveform.length) wfIdx = clip.waveform.length - 1;
                            float amp = clip.waveform[wfIdx];
                            int barH = (int)(amp * halfH);
                            g2.drawLine(x + px, midY - barH, x + px, midY + barH);
                        }
                    }
                }

                g2.setColor(Theme.getInstance().ACCENT_BLUE);
                g2.drawRoundRect(x, y, w, h, 8, 8);

                g2.setColor(Color.WHITE);
                g2.setFont(Theme.getInstance().FONT_UI.deriveFont(Font.BOLD, Theme.getInstance().scale(10.0f)));
                g2.drawString(clip.name, x + 5, y + 15);
            }
        }

        // Draw dragging clip at cursor position (for cross-track visualization)
        if (isDragging && draggingClip != null) {
            int targetTrackIdx = (dragCurrentY - scaleTimeRuler) / scaleTrackHeight;
            targetTrackIdx = Math.max(0, Math.min(tracks.size() - 1, targetTrackIdx));
            int y = scaleTimeRuler + targetTrackIdx * scaleTrackHeight + 5;
            int x = scaleLabelWidth + (int) (draggingClip.startTime * getPixelsPerSecond());
            int w = (int) (draggingClip.duration * getPixelsPerSecond());
            int h = scaleTrackHeight - 10;

            // Draw filled clip at drag position with slight transparency
            g2.setComposite(AlphaComposite.getInstance(AlphaComposite.SRC_OVER, 0.8f));
            g2.setColor(Theme.getInstance().ACCENT_BLUE.darker());
            g2.fillRoundRect(x, y, w, h, 8, 8);
            g2.setColor(Theme.getInstance().ACCENT_BLUE.brighter());
            g2.drawRoundRect(x, y, w, h, 8, 8);
            g2.setColor(Color.WHITE);
            g2.setFont(Theme.getInstance().FONT_UI.deriveFont(Font.BOLD, Theme.getInstance().scale(10.0f)));
            g2.drawString(draggingClip.name, x + 5, y + 15);
            g2.setComposite(AlphaComposite.SrcOver);
        }

        // Draw clip being created (visual feedback during drag creation)
        if (creatingClip && creatingClipRect != null && creatingClipRect.duration > 0) {
            int y = scaleTimeRuler + creatingTrackIdx * scaleTrackHeight + 5;
            int x = scaleLabelWidth + (int) (creatingClipRect.startTime * getPixelsPerSecond());
            int w = (int) (creatingClipRect.duration * getPixelsPerSecond());
            int h = scaleTrackHeight - 10;

            // Draw with green tint to indicate new clip creation
            g2.setComposite(AlphaComposite.getInstance(AlphaComposite.SRC_OVER, 0.6f));
            g2.setColor(new Color(100, 200, 100)); // Green tint for creation
            g2.fillRoundRect(x, y, w, h, 8, 8);
            g2.setColor(new Color(150, 255, 150));
            g2.drawRoundRect(x, y, w, h, 8, 8);
            g2.setColor(Color.WHITE);
            g2.setFont(Theme.getInstance().FONT_UI.deriveFont(Font.BOLD, Theme.getInstance().scale(10.0f)));
            g2.drawString("New Clip", x + 5, y + 15);
            g2.setComposite(AlphaComposite.SrcOver);
        }

        // Draw time ruler
        g2.setColor(Theme.getInstance().BG_DARKER);
        g2.fillRect(scaleLabelWidth, 0, contentPanel.getWidth() - scaleLabelWidth, scaleTimeRuler);
        g2.setColor(Theme.getInstance().TEXT_DIM);
        // Time ruler header - show seconds or bars based on mode
        if (gridMode == GridMode.SECONDS) {
            // Show absolute seconds
            for (int s = 0; s < 600; s += 5) {
                int x = scaleLabelWidth + (int) (s * getPixelsPerSecond());
                if (x > contentPanel.getWidth())
                    break;
                g2.drawLine(x, scaleTimeRuler - 10, x, scaleTimeRuler);
                g2.drawString(s + "s", x + 2, scaleTimeRuler - 12);
            }
        } else {
            // Show bar numbers
            float rulerSecondsPerBeat = 60.0f / bpm;
            float rulerSecondsPerBar = rulerSecondsPerBeat * 4;
            for (int bar = 0; bar < 200; bar++) {
                int x = scaleLabelWidth + (int) (bar * rulerSecondsPerBar * getPixelsPerSecond());
                if (x > contentPanel.getWidth()) break;
                g2.drawLine(x, scaleTimeRuler - 10, x, scaleTimeRuler);
                g2.drawString(String.valueOf(bar + 1), x + 3, scaleTimeRuler - 12);
            }
        }

        // Draw playhead
        int px = scaleLabelWidth + (int) (playheadPos * getPixelsPerSecond());
        g2.setColor(Color.RED);
        g2.setStroke(new BasicStroke(2.0f));
        g2.drawLine(px, 0, px, contentPanel.getHeight());
    }

    @Override
    public void onThemeChanged() {
        SwingUtilities.invokeLater(() -> {
            setBackground(Theme.getInstance().BG_DARK);
            contentPanel.setBackground(Theme.getInstance().BG_DARK);
            updateContentSize();
            repaint();
        });
    }

    static class TrackTimeline {
        int index;
        List<ClipRect> clips = new ArrayList<>();
        Map<Integer, ClipRect> clipMap = new HashMap<>();
        String pluginName = null;
        boolean isInstrument = false;
        String customName = null; // User-defined track name

        TrackTimeline(int index) {
            this.index = index;
        }

        String getDisplayName() {
            if (customName != null && !customName.isEmpty()) {
                return customName;
            }
            return "Track " + index;
        }

        void addOrUpdateClip(TimelineClipInfo info) {
            int cidx = info.clipIndex();
            ClipRect cr = clipMap.get(cidx);
            if (cr == null) {
                cr = new ClipRect();
                clips.add(cr);
                clipMap.put(cidx, cr);
            }
            cr.name = info.name();
            cr.path = info.path();
            cr.startTime = info.startTime();
            cr.duration = info.duration();
            // Extract waveform data
            int wfLen = info.waveformLength();
            if (wfLen > 0) {
                cr.waveform = new float[wfLen];
                for (int i = 0; i < wfLen; i++) {
                    cr.waveform[i] = info.waveform(i);
                }
            }
        }
    }

    static class ClipRect {
        String name;
        String path;
        float startTime;
        float duration;
        float[] waveform;
    }

    /**
     * Get the snap interval in seconds for the given grid mode.
     * For AUTO mode, adapts based on pixel threshold.
     */
    private float getGridSnapSeconds(GridMode mode, float secondsPerBeat) {
        float secondsPerBar = secondsPerBeat * 4;

        // Handle SECONDS mode separately - 1 second grid interval
        if (mode == GridMode.SECONDS) {
            return 1.0f;
        }

        if (mode != GridMode.AUTO) {
            switch (mode) {
                case BAR:
                    return secondsPerBar;
                case HALF:
                    return secondsPerBar / 2;
                case QUARTER:
                    return secondsPerBeat;
                case EIGHTH:
                    return secondsPerBeat / 2;
                case SIXTEENTH:
                    return secondsPerBeat / 4;
                case THIRTY_SECOND:
                    return secondsPerBeat / 8;
                case TRIPLET_QUARTER:
                    return secondsPerBar / 3;
                case TRIPLET_EIGHTH:
                    return secondsPerBar / 6;
                case TRIPLET_16TH:
                    return secondsPerBar / 12;
                case TRIPLET_32ND:
                    return secondsPerBar / 24;
                default:
                    return secondsPerBeat;
            }
        }

        // AUTO mode: find finest grid that maintains minimum pixel spacing
        int minPixels = 15;
        float[] divisions = {
                secondsPerBeat / 8, // 1/32
                secondsPerBeat / 4, // 1/16
                secondsPerBeat / 2, // 1/8
                secondsPerBeat, // 1/4 (beat)
                secondsPerBar / 2, // 1/2
                secondsPerBar // 1/1 (bar)
        };

        for (float div : divisions) {
            if (div * getPixelsPerSecond() >= minPixels) {
                return div;
            }
        }
        return secondsPerBar;
    }
}
