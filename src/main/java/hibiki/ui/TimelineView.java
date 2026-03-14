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
    static final int TIME_RULER_HEIGHT = 30;
    private static final int TRACK_LABEL_WIDTH = 100;
    private static final float BASE_PIXELS_PER_SECOND = 50.0f;

    // Zoom scales (adjustable via sliders)
    private float hZoomScale = 1.0f; // Horizontal zoom multiplier
    private float vZoomScale = 1.0f; // Vertical zoom multiplier

    // Convenience getters for zoom-scaled values
    int getTrackHeight() {
        return (int) (BASE_TRACK_HEIGHT * vZoomScale);
    }

    float getPixelsPerSecond() {
        return BASE_PIXELS_PER_SECOND * hZoomScale;
    }

    // GridMode is shared - see GridMode.java

    private GridMode gridMode = GridMode.AUTO;
    volatile float bpm = 120.0f;
    volatile boolean isPlaying = false;

    volatile float playheadPos = 0.0f; // volatile for thread-safe updates from notification thread
    final List<TrackTimeline> tracks = new ArrayList<>();
    private int selectedTrack = 0; // Currently selected track for plugin/clip operations
    private static TimelineView instance; // Static reference for global access
    final JScrollPane scrollPane;
    final JPanel contentPanel;
    private JPanel rowHeader; // Track labels panel (needs update on vZoom)
    private final Timer repaintTimer;
    boolean autoScroll = true; // Auto-scroll to follow playhead during playback
    int playheadScreenOffset = -1; // Screen X position to keep playhead at during auto-scroll

    // Renderer delegate
    private final TimelineRenderer renderer = new TimelineRenderer(this);

    // Clip drag-and-drop state
    ClipRect draggingClip = null;
    int dragSourceTrack = -1;
    int dragStartX = 0;
    int dragStartY = 0;
    int dragCurrentY = 0; // Current Y position for rendering during cross-track drag
    float dragOriginalStartTime = 0;
    boolean isDragging = false;

    // Clip creation state (like Piano Roll note creation)
    boolean creatingClip = false;
    int creatingTrackIdx = -1;
    float creatingStartTime = 0;
    ClipRect creatingClipRect = null;

    // Clip resize state
    boolean resizingClip = false;
    ClipRect resizeClip = null;
    int resizeTrackIdx = -1;
    float resizeOriginalDuration = 0;

    // Mouse handler delegate
    private final TimelineMouseHandler mouseHandler = new TimelineMouseHandler(this);
    private final TimelineNotificationHandler notificationHandler = new TimelineNotificationHandler(this);

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
        ZoomControlPanel controlPanel = new ZoomControlPanel(
                GridMode.values(), gridMode,
                mode -> {
                    gridMode = mode;
                    repaint();
                },
                scale -> {
                    hZoomScale = scale;
                    updateContentSize();
                    contentPanel.repaint();
                },
                5, 400, 100,
                val -> {
                    vZoomScale = val / 100.0f;
                    updateContentSize();
                    if (rowHeader != null) {
                        rowHeader.revalidate();
                        rowHeader.repaint();
                    }
                    contentPanel.repaint();
                },
                5, 200, 100,
                auto -> autoScroll = auto, autoScroll);
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
                                BackendManager.getInstance().addTimelineClip(trackIndex, path, (float) timeSec, 0);
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

    void updateContentSize() {
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
        mouseHandler.install();
    }


    /** Snap time to nearest bar boundary based on current BPM */
    float snapToBar(float time) {
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
    ClipRect findClipAtPosition(int trackIdx, int x) {
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

    /** Check if x position is near the right edge of a clip (within 8px) */
    boolean isNearRightEdge(ClipRect clip, int x) {
        int rightEdgeX = (int) ((clip.startTime + clip.duration) * getPixelsPerSecond());
        return Math.abs(x - rightEdgeX) <= 8;
    }

    /** Show context menu for a timeline clip */
    void showClipContextMenu(int trackIdx, ClipRect clip, int x, int y) {
        JPopupMenu menu = new JPopupMenu();

        // Edit Clip (MIDI only)
        JMenuItem editItem = new JMenuItem("Edit Clip...");
        editItem.addActionListener(e -> {
            boolean isMidi = clip.path == null || clip.path.isEmpty()
                    || clip.path.endsWith(".mid"); // Empty clips are treated as MIDI
            if (isMidi) {
                File file = (clip.path != null && !clip.path.isEmpty()) ? new File(clip.path)
                        : new File("New Clip.mid");
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
    void showEmptyAreaContextMenu(int trackIdx, float clickTime, int x, int y) {
        JPopupMenu menu = new JPopupMenu();

        // Create New Clip
        JMenuItem createItem = new JMenuItem("Create New Clip");
        createItem.addActionListener(e -> {
            // Create a new clip at the clicked position
            float snapTime = snapToBar(clickTime);

            // Send to backend - it will create the clip and notify us via TimelineClipInfo
            BackendManager.getInstance().addTimelineClip(trackIdx, "", snapTime, 0);
        });
        menu.add(createItem);

        menu.show(contentPanel, x, y);
    }

    void updatePlayhead(int x) {
        // Content panel starts at x=0, no label offset needed
        float timelineX = Math.max(0, x);
        playheadPos = timelineX / getPixelsPerSecond();
        BackendManager.getInstance().seek(playheadPos);
        repaint();
    }

    public void handleNotification(Notification n) {
        notificationHandler.handleNotification(n);
    }

    private void drawTrackLabels(Graphics g) {
        renderer.drawTrackLabels(g, tracks, selectedTrack, getTrackHeight(),
                TIME_RULER_HEIGHT, TRACK_LABEL_WIDTH);
    }

    private void drawTimeline(Graphics g) {
        renderer.drawTimeline(g, contentPanel, tracks, selectedTrack, bpm, gridMode,
                playheadPos, isDragging, draggingClip, dragSourceTrack, dragOriginalStartTime,
                dragCurrentY, creatingClip, creatingTrackIdx, creatingClipRect,
                getTrackHeight(), TIME_RULER_HEIGHT);
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
    float getGridSnapSeconds(GridMode mode, float secondsPerBeat) {
        if (mode == GridMode.AUTO) {
            return GridMode.autoSecondsInterval(secondsPerBeat, getPixelsPerSecond(), 15);
        }
        return mode.getSecondsInterval(secondsPerBeat);
    }
}
