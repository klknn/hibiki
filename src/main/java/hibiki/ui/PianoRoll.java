package hibiki.ui;

import javax.swing.*;
import java.awt.*;
import java.awt.event.*;
import java.io.File;
import java.util.ArrayList;
import java.util.List;

import javax.sound.midi.*;

import hibiki.BackendManager;
import hibiki.ipc.Notification;
import hibiki.ipc.Response;
import hibiki.ipc.ClipMidiData;
import hibiki.ipc.MidiEventData;
import java.util.function.Consumer;

public class PianoRoll extends JDialog {
    static final int NUM_KEYS = 128;

    // Zoom settings (adjustable)
    int keyHeight = 12; // Default, adjustable via vertical zoom
    private float tickScale = 1.0f; // Multiplier for base tick width
    private float baseTickWidth = 1.0f; // Calculated to fit entire clip at scale=1.0

    private final File midiFile;
    private final int trackIdx;
    private final int slotIdx; // Session clip slot (-1 for timeline clips)
    private final int clipIdx; // Timeline clip index (-1 for session clips)
    Sequence sequence;
    private Track midiTrack;
    List<Note> notes = new ArrayList<>();

    JPanel gridPanel;
    JPanel keysPanel;
    JPanel velocityPanel;
    private JScrollPane gridScroll;
    private JScrollPane keysScroll;

    // Interaction state
    Note draggingNote = null;
    Note resizingNote = null;
    int dragOffsetX = 0;
    int dragOffsetY = 0;

    // Ghost/copy state for drag
    int dragOriginalPitch = -1;
    long dragOriginalTick = 0;
    boolean isDraggingNote = false;

    // Zoom sliders
    private JSlider hZoomSlider;
    private JSlider vZoomSlider;

    // Velocity editing state
    boolean editingVelocity = false;

    // Grid mode for rendering and snapping
    private enum GridMode {
        AUTO("Auto"), // Adaptive based on zoom level
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

    // Playhead and Auto-scroll state
    volatile float playheadPos = 0.0f; // in seconds
    volatile float bpm = 120.0f;
    private volatile boolean isPlaying = false;
    float clipStartTime = 0.0f; // Start time of clip on timeline (seconds)
    private boolean autoScroll = true; // Auto-scroll to follow playhead during playback
    private int playheadScreenOffset = -1; // Screen X position to keep playhead at during auto-scroll
    private javax.swing.Timer repaintTimer;

    // Renderer delegate
    private final PianoRollRenderer renderer = new PianoRollRenderer(this);
    private final PianoRollMouseHandler mouseHandler = new PianoRollMouseHandler(this);

    static class Note {
        int pitch;
        long startTick;
        long durationTicks;
        int velocity;
        MidiEvent onEvent;
        MidiEvent offEvent;

        Note(int pitch, long startTick, long durationTicks, int velocity) {
            this.pitch = pitch;
            this.startTick = startTick;
            this.durationTicks = durationTicks;
            this.velocity = velocity;
        }
    }

    public PianoRoll(Frame owner, File midiFile, int trackIdx, int slotIdx, int clipIdx, float clipStartTime) {
        super(owner, "Piano Roll - " + midiFile.getName(), false);
        this.midiFile = midiFile;
        this.trackIdx = trackIdx;
        this.slotIdx = slotIdx;
        this.clipIdx = clipIdx;
        this.clipStartTime = clipStartTime;

        // First load from file as fallback / initial state
        loadMidi();
        initUI();

        // Register listener for backend MIDI data
        notificationListener = this::handleNotification;
        BackendManager.getInstance().addNotificationListener(notificationListener);

        // Request MIDI data from backend (will override file data if clip exists in
        // memory)
        BackendManager.getInstance().requestClipMidi(trackIdx, slotIdx, clipIdx);

        setSize(Theme.getInstance().scale(800), Theme.getInstance().scale(600));
        setLocationRelativeTo(owner);
    }

    // Convenience constructor for session clips (slotIdx >= 0, clipIdx = -1)
    public PianoRoll(Frame owner, File midiFile, int trackIdx, int slotIdx) {
        this(owner, midiFile, trackIdx, slotIdx, -1, 0.0f);
    }

    private Consumer<Notification> notificationListener;

    private void handleNotification(Notification notification) {
        if (notification.responseType() == Response.ClipMidiData) {
            ClipMidiData data = (ClipMidiData) notification.response(new ClipMidiData());
            // Match either by slotIdx (session clip) or clipIdx (timeline clip)
            System.out.println("[PianoRoll] Got ClipMidiData: track=" + data.trackIndex() +
                    " slot=" + data.slotIndex() + " clip=" + data.clipIndex() +
                    " events=" + data.eventsLength());
            System.out.println("[PianoRoll] My trackIdx=" + trackIdx + " slotIdx=" + slotIdx + " clipIdx=" + clipIdx);
            if (data != null && data.trackIndex() == trackIdx) {
                boolean matches = (slotIdx >= 0 && data.slotIndex() == slotIdx) ||
                        (clipIdx >= 0 && data.clipIndex() == clipIdx);
                System.out.println("[PianoRoll] matches=" + matches);
                if (matches) {
                    // Load notes from backend memory
                    loadFromBackendData(data);
                }
            }
        } else if (notification.responseType() == Response.PlayheadInfo) {
            hibiki.ipc.PlayheadInfo info = (hibiki.ipc.PlayheadInfo) notification
                    .response(new hibiki.ipc.PlayheadInfo());
            playheadPos = info.positionSec();
            bpm = info.bpm();
            boolean wasPlaying = isPlaying;
            isPlaying = info.isPlaying();

            // Capture playhead screen position when playback starts
            if (isPlaying && !wasPlaying && autoScroll) {
                float relativePos = playheadPos - clipStartTime;
                if (relativePos >= 0 && sequence != null) {
                    float beatsPerSecond = bpm / 60.0f;
                    long playheadTick = (long) (relativePos * beatsPerSecond * sequence.getResolution());
                    int playheadX = (int) (playheadTick * getTickWidth());
                    int scrollX = gridScroll.getHorizontalScrollBar().getValue();
                    playheadScreenOffset = playheadX - scrollX;
                }
            }
        }
    }

    private void loadFromBackendData(ClipMidiData data) {
        javax.swing.SwingUtilities.invokeLater(() -> {
            notes.clear();
            int resolution = data.resolution();
            if (resolution > 0 && (sequence == null || sequence.getResolution() != resolution)) {
                // Create new sequence matching backend resolution so grid lines align with note
                // ticks
                try {
                    sequence = new javax.sound.midi.Sequence(javax.sound.midi.Sequence.PPQ, resolution);
                    midiTrack = sequence.createTrack();
                } catch (Exception ex) {
                    ex.printStackTrace();
                }
            }
            for (int i = 0; i < data.eventsLength(); i++) {
                MidiEventData ev = data.events(i);
                Note n = new Note(ev.pitch(), ev.tick(), ev.durationTicks(), ev.velocity());
                notes.add(n);
            }
            if (gridPanel != null) {
                gridPanel.repaint();
                gridPanel.revalidate();
            }
        });
    }

    @Override
    public void dispose() {
        // Unregister listener when closing
        // Note: BackendManager doesn't have removeListener yet, but we track the
        // listener
        super.dispose();
    }

    float getTickWidth() {
        return baseTickWidth * tickScale;
    }

    private void loadMidi() {
        notes.clear();
        try {
            if (midiFile.exists()) {
                sequence = MidiSystem.getSequence(midiFile);
                if (sequence.getTracks().length > 0) {
                    // Find first track with notes
                    for (Track t : sequence.getTracks()) {
                        if (hasNotes(t)) {
                            midiTrack = t;
                            break;
                        }
                    }
                    if (midiTrack == null)
                        midiTrack = sequence.getTracks()[0];
                } else {
                    sequence = new Sequence(Sequence.PPQ, 96);
                    midiTrack = sequence.createTrack();
                }
            } else {
                sequence = new Sequence(Sequence.PPQ, 96);
                midiTrack = sequence.createTrack();
            }

            parseTrack(midiTrack);

        } catch (Exception e) {
            e.printStackTrace();
            JOptionPane.showMessageDialog(this, "Failed to load MIDI: " + e.getMessage(), "Error",
                    JOptionPane.ERROR_MESSAGE);
            // Create empty fallback
            try {
                sequence = new Sequence(Sequence.PPQ, 96);
                midiTrack = sequence.createTrack();
            } catch (Exception ex) {
            }
        }
    }

    private boolean hasNotes(Track t) {
        for (int i = 0; i < t.size(); i++) {
            MidiMessage msg = t.get(i).getMessage();
            if (msg instanceof ShortMessage) {
                ShortMessage sm = (ShortMessage) msg;
                if (sm.getCommand() == ShortMessage.NOTE_ON)
                    return true;
            }
        }
        return false;
    }

    private void parseTrack(Track track) {
        Note[] pendingNotes = new Note[128]; // Max 128 keys

        for (int i = 0; i < track.size(); i++) {
            MidiEvent event = track.get(i);
            MidiMessage msg = event.getMessage();

            if (msg instanceof ShortMessage) {
                ShortMessage sm = (ShortMessage) msg;
                int cmd = sm.getCommand();
                int pitch = sm.getData1();
                int vel = sm.getData2();

                if (cmd == ShortMessage.NOTE_ON && vel > 0) {
                    if (pendingNotes[pitch] == null) {
                        Note n = new Note(pitch, event.getTick(), 0, vel);
                        n.onEvent = event;
                        pendingNotes[pitch] = n;
                    }
                } else if (cmd == ShortMessage.NOTE_OFF || (cmd == ShortMessage.NOTE_ON && vel == 0)) {
                    if (pendingNotes[pitch] != null) {
                        Note n = pendingNotes[pitch];
                        n.durationTicks = event.getTick() - n.startTick;
                        n.offEvent = event;
                        notes.add(n);
                        pendingNotes[pitch] = null;
                    }
                }
            }
        }
    }

    private void saveMidi() {
        // Sync to backend via IPC (immediate in-memory update)
        syncToBackend();
    }

    /**
     * Sync notes to backend via IPC - changes apply immediately without file save
     */
    void syncToBackend() {
        int resolution = (sequence != null) ? sequence.getResolution() : 480;
        long[] ticks = new long[notes.size()];
        int[] pitches = new int[notes.size()];
        long[] durations = new long[notes.size()];
        int[] velocities = new int[notes.size()];

        for (int i = 0; i < notes.size(); i++) {
            Note n = notes.get(i);
            ticks[i] = n.startTick;
            pitches[i] = n.pitch;
            durations[i] = Math.max(1, n.durationTicks);
            velocities[i] = n.velocity;
        }

        BackendManager.getInstance().updateClipMidi(trackIdx, slotIdx, clipIdx, resolution, ticks, pitches, durations,
                velocities);
    }

    private SessionView getParentSessionView() {
        Component c = getOwner();
        while (c != null) {
            if (c instanceof JFrame) {
                JFrame f = (JFrame) c;
                return findSessionView(f.getContentPane());
            }
            c = c.getParent();
        }
        return null;
    }

    private SessionView findSessionView(Container c) {
        if (c instanceof SessionView)
            return (SessionView) c;
        for (Component child : c.getComponents()) {
            if (child instanceof Container) {
                SessionView sv = findSessionView((Container) child);
                if (sv != null)
                    return sv;
            }
        }
        return null;
    }

    private void initUI() {
        setLayout(new BorderLayout());
        getContentPane().setBackground(Theme.getInstance().BG_DARKER);

        // Calculate note range for auto-fit
        int minPitch = 127, maxPitch = 0;
        long minTick = Long.MAX_VALUE, maxTick = 0;
        for (Note n : notes) {
            minPitch = Math.min(minPitch, n.pitch);
            maxPitch = Math.max(maxPitch, n.pitch);
            minTick = Math.min(minTick, n.startTick);
            maxTick = Math.max(maxTick, n.startTick + n.durationTicks);
        }
        if (notes.isEmpty()) {
            minPitch = 48;
            maxPitch = 72; // Default C3-C5 range
            minTick = 0;
            maxTick = sequence.getResolution() * 4 * 4;
        }

        // Calculate baseTickWidth so that scale=1.0 fits entire clip
        int viewWidth = Theme.getInstance().scale(700);
        long totalTicks = maxTick - minTick;
        if (totalTicks > 0) {
            baseTickWidth = (float) viewWidth / totalTicks;
        } else {
            baseTickWidth = 2.0f;
        }
        tickScale = 1.0f; // Start at 100% (fits entire clip)

        // Calculate vertical zoom to fit entire note range with margin
        int viewHeight = Theme.getInstance().scale(400);
        int noteRange = maxPitch - minPitch + 1;
        int keysToShow = noteRange + 6; // Add margin above and below
        keyHeight = Math.max(2, viewHeight / keysToShow);

        final int centerPitch = (minPitch + maxPitch) / 2;
        final long centerTick = (minTick + maxTick) / 2;

        // Toolbar
        JPanel toolbar = new JPanel(new FlowLayout(FlowLayout.LEFT));
        toolbar.setBackground(Theme.getInstance().PANEL_BG);
        toolbar.setBorder(BorderFactory.createMatteBorder(0, 0, 1, 0, Theme.getInstance().BORDER));

        // Note: Edits sync automatically via IPC - no save button needed
        JLabel autoSyncLabel = new JLabel("Auto-sync enabled");
        autoSyncLabel.setForeground(Theme.getInstance().TEXT_DIM);
        toolbar.add(autoSyncLabel);

        add(toolbar, BorderLayout.NORTH);

        // Piano Keys (Left)
        keysPanel = new JPanel() {
            @Override
            public Dimension getPreferredSize() {
                return new Dimension(Theme.getInstance().scale(60), NUM_KEYS * getScaledKeyHeight());
            }

            @Override
            protected void paintComponent(Graphics g) {
                super.paintComponent(g);
                renderer.paintKeyLabels(g, this, NUM_KEYS, getScaledKeyHeight());
            }
        };

        // Grid (Right)
        gridPanel = new JPanel() {
            @Override
            public Dimension getPreferredSize() {
                long maxT = 0;
                for (Note n : notes) {
                    if (n.startTick + n.durationTicks > maxT) {
                        maxT = n.startTick + n.durationTicks;
                    }
                }
                maxT = Math.max(maxT, sequence.getResolution() * 4 * 4); // At least 4 bars
                return new Dimension((int) (maxT * getTickWidth()) + 200, NUM_KEYS * getScaledKeyHeight());
            }

            @Override
            protected void paintComponent(Graphics g) {
                super.paintComponent(g);
                renderer.paintGrid(g, this, NUM_KEYS, getScaledKeyHeight(), getTickWidth(),
                        sequence, notes, isDraggingNote, draggingNote,
                        dragOriginalPitch, dragOriginalTick,
                        playheadPos, clipStartTime, bpm);
            }
        };

        setupMouseListeners();

        // Sync scrolling
        gridScroll = new JScrollPane(gridPanel);
        gridScroll.getVerticalScrollBar().setUnitIncrement(getScaledKeyHeight());
        gridScroll.getHorizontalScrollBar().setUnitIncrement(Theme.getInstance().scale(20));

        // Time ruler panel (column header) - shows bars/beats and allows seeking
        final int TIME_RULER_HEIGHT = Theme.getInstance().scale(24);
        JPanel timeRulerPanel = new JPanel() {
            @Override
            public Dimension getPreferredSize() {
                return new Dimension(gridPanel.getPreferredSize().width, TIME_RULER_HEIGHT);
            }

            @Override
            protected void paintComponent(Graphics g) {
                super.paintComponent(g);
                renderer.paintTimeRuler(g, this, sequence, getTickWidth(), playheadPos, clipStartTime, bpm);
            }
        };
        timeRulerPanel.setBackground(Theme.getInstance().BG_DARKER);

        // Time ruler mouse handling for seeking
        MouseAdapter timeRulerMouse = new MouseAdapter() {
            @Override
            public void mousePressed(MouseEvent e) {
                if (SwingUtilities.isLeftMouseButton(e)) {
                    seekFromRuler(e.getX());
                }
            }

            @Override
            public void mouseDragged(MouseEvent e) {
                if (SwingUtilities.isLeftMouseButton(e)) {
                    seekFromRuler(e.getX());
                }
            }

            private void seekFromRuler(int x) {
                float tw = getTickWidth();
                long tick = (long) (x / tw);
                seekToTick(tick);
                timeRulerPanel.repaint();
            }
        };
        timeRulerPanel.addMouseListener(timeRulerMouse);
        timeRulerPanel.addMouseMotionListener(timeRulerMouse);

        // Set time ruler as column header
        gridScroll.setColumnHeaderView(timeRulerPanel);

        keysScroll = new JScrollPane(keysPanel);
        keysScroll.setVerticalScrollBarPolicy(JScrollPane.VERTICAL_SCROLLBAR_NEVER);
        keysScroll.setHorizontalScrollBarPolicy(JScrollPane.HORIZONTAL_SCROLLBAR_NEVER);

        gridScroll.getVerticalScrollBar().getModel().addChangeListener(e -> {
            keysScroll.getVerticalScrollBar().setValue(gridScroll.getVerticalScrollBar().getValue());
        });

        // Corner panel for keys column header (empty spacer)
        JPanel cornerPanel = new JPanel();
        cornerPanel.setPreferredSize(new Dimension(Theme.getInstance().scale(60), TIME_RULER_HEIGHT));
        cornerPanel.setBackground(Theme.getInstance().BG_DARKER);

        // Wrap keysScroll with a panel that has corner spacer at top
        JPanel keysWithCorner = new JPanel(new BorderLayout());
        keysWithCorner.add(cornerPanel, BorderLayout.NORTH);
        keysWithCorner.add(keysScroll, BorderLayout.CENTER);

        JSplitPane split = new JSplitPane(JSplitPane.HORIZONTAL_SPLIT, keysWithCorner, gridScroll);
        split.setDividerLocation(Theme.getInstance().scale(60));
        split.setDividerSize(0);

        // Velocity panel height
        final int VELOCITY_HEIGHT = Theme.getInstance().scale(80);

        // Velocity Panel - shows velocity bars at the bottom
        velocityPanel = new JPanel() {
            @Override
            public Dimension getPreferredSize() {
                return new Dimension(gridPanel.getPreferredSize().width, VELOCITY_HEIGHT);
            }

            @Override
            protected void paintComponent(Graphics g) {
                super.paintComponent(g);
                renderer.paintVelocity(g, this, getTickWidth(), notes);
            }
        };
        velocityPanel.setBackground(Theme.getInstance().BG_DARKER);
        velocityPanel.setBorder(BorderFactory.createMatteBorder(1, 0, 0, 0, Theme.getInstance().BORDER));

        // Add mouse handlers for velocity editing
        MouseAdapter velocityMouse = new MouseAdapter() {
            @Override
            public void mousePressed(MouseEvent e) {
                editingVelocity = true;
                updateVelocityAt(e.getX(), e.getY());
            }

            @Override
            public void mouseReleased(MouseEvent e) {
                if (editingVelocity) {
                    editingVelocity = false;
                    syncToBackend();
                }
            }

            @Override
            public void mouseDragged(MouseEvent e) {
                if (editingVelocity) {
                    updateVelocityAt(e.getX(), e.getY());
                }
            }
        };
        velocityPanel.addMouseListener(velocityMouse);
        velocityPanel.addMouseMotionListener(velocityMouse);

        // Create scroll pane for velocity panel (synced with grid)
        JScrollPane velocityScroll = new JScrollPane(velocityPanel,
                JScrollPane.VERTICAL_SCROLLBAR_NEVER, JScrollPane.HORIZONTAL_SCROLLBAR_NEVER);
        velocityScroll.setPreferredSize(new Dimension(100, VELOCITY_HEIGHT));
        velocityScroll.setBorder(null);

        // Sync velocity panel scroll with grid scroll
        gridScroll.getHorizontalScrollBar().addAdjustmentListener(e -> {
            velocityScroll.getHorizontalScrollBar().setValue(e.getValue());
            velocityPanel.repaint();
        });

        // Spacer panel for velocity row (aligns with keys column)
        JPanel velocityLabelPanel = new JPanel();
        velocityLabelPanel.setPreferredSize(new Dimension(Theme.getInstance().scale(60), VELOCITY_HEIGHT));
        velocityLabelPanel.setBackground(Theme.getInstance().PANEL_BG);
        velocityLabelPanel.setBorder(BorderFactory.createMatteBorder(1, 0, 0, 0, Theme.getInstance().BORDER));

        // Row for velocity area
        JPanel velocityRow = new JPanel(new BorderLayout());
        velocityRow.add(velocityLabelPanel, BorderLayout.WEST);
        velocityRow.add(velocityScroll, BorderLayout.CENTER);

        // Upper panel with piano roll
        JPanel upperPanel = new JPanel(new BorderLayout());
        upperPanel.add(split, BorderLayout.CENTER);

        // Combine piano roll and velocity panel
        JPanel pianoAndVelocity = new JPanel(new BorderLayout());
        pianoAndVelocity.add(upperPanel, BorderLayout.CENTER);
        pianoAndVelocity.add(velocityRow, BorderLayout.SOUTH);

        // Main content panel with zoom sliders at bottom right
        JPanel mainContent = new JPanel(new BorderLayout());
        mainContent.add(pianoAndVelocity, BorderLayout.CENTER);

        // Zoom slider panel (bottom right corner)
        JPanel zoomPanel = new JPanel(new FlowLayout(FlowLayout.RIGHT, 5, 2));
        zoomPanel.setBackground(Theme.getInstance().PANEL_BG);
        zoomPanel.setBorder(BorderFactory.createMatteBorder(1, 0, 0, 0, Theme.getInstance().BORDER));

        // Grid mode selector
        JLabel gridLabel = new JLabel("Grid:");
        gridLabel.setForeground(Theme.getInstance().TEXT_DIM);
        zoomPanel.add(gridLabel);
        JComboBox<GridMode> gridCombo = new JComboBox<>(GridMode.values());
        gridCombo.setSelectedItem(gridMode);
        gridCombo.setPreferredSize(new Dimension(60, 20));
        gridCombo.addActionListener(e -> {
            gridMode = (GridMode) gridCombo.getSelectedItem();
            gridPanel.repaint();
            velocityPanel.repaint();
        });
        zoomPanel.add(gridCombo);

        // Calculate max zoom that limits view to ~1 bar
        int ticksPerBar = sequence.getResolution() * 4;
        int zoomViewWidth = Theme.getInstance().scale(700);
        // Max scale = when 1 bar fills the view
        int maxZoomPercent = Math.max(500, (int) (zoomViewWidth / (ticksPerBar * baseTickWidth) * 100));

        // Horizontal zoom slider
        JLabel hLabel = new JLabel("H:");
        hLabel.setForeground(Theme.getInstance().TEXT_DIM);
        zoomPanel.add(hLabel);
        hZoomSlider = new JSlider(100, maxZoomPercent, 100);
        hZoomSlider.setPreferredSize(new Dimension(80, 20));
        hZoomSlider.addChangeListener(e -> {
            tickScale = hZoomSlider.getValue() / 100.0f;
            gridPanel.revalidate();
            gridPanel.repaint();
            velocityPanel.revalidate();
            velocityPanel.repaint();
        });
        zoomPanel.add(hZoomSlider);

        // Vertical zoom slider
        JLabel vLabel = new JLabel("V:");
        vLabel.setForeground(Theme.getInstance().TEXT_DIM);
        zoomPanel.add(vLabel);
        vZoomSlider = new JSlider(2, 30, Math.min(30, Math.max(2, keyHeight)));
        vZoomSlider.setPreferredSize(new Dimension(80, 20));
        vZoomSlider.addChangeListener(e -> {
            keyHeight = vZoomSlider.getValue();
            keysPanel.revalidate();
            keysPanel.repaint();
            gridPanel.revalidate();
            gridPanel.repaint();
        });
        zoomPanel.add(vZoomSlider);

        // Auto-scroll checkbox
        JCheckBox autoScrollCheck = new JCheckBox("Auto-scroll", autoScroll);
        autoScrollCheck.setForeground(Theme.getInstance().TEXT_DIM);
        autoScrollCheck.setOpaque(false);
        autoScrollCheck.addActionListener(e -> autoScroll = autoScrollCheck.isSelected());
        zoomPanel.add(autoScrollCheck);

        mainContent.add(zoomPanel, BorderLayout.SOUTH);
        add(mainContent, BorderLayout.CENTER);

        // Scroll to center on note content
        SwingUtilities.invokeLater(() -> {
            int centerY = (NUM_KEYS - 1 - centerPitch) * getScaledKeyHeight();
            int centerX = (int) (centerTick * getTickWidth());
            gridScroll.getVerticalScrollBar().setValue(Math.max(0, centerY - gridScroll.getViewport().getHeight() / 2));
            gridScroll.getHorizontalScrollBar()
                    .setValue(Math.max(0, centerX - gridScroll.getViewport().getWidth() / 2));
        });

        // Repaint timer for playhead animation (30 fps)
        repaintTimer = new javax.swing.Timer(33, e -> {
            if (isPlaying && autoScroll && sequence != null) {
                float relativePos = playheadPos - clipStartTime;
                if (relativePos >= 0) {
                    float beatsPerSecond = bpm / 60.0f;
                    long playheadTick = (long) (relativePos * beatsPerSecond * sequence.getResolution());
                    int playheadX = (int) (playheadTick * getTickWidth());

                    int targetScrollX = playheadX - playheadScreenOffset;
                    targetScrollX = Math.max(0, targetScrollX);
                    gridScroll.getHorizontalScrollBar().setValue(targetScrollX);
                }
            }
            gridPanel.repaint();
            velocityPanel.repaint();
        });
        repaintTimer.start();

        // Stop timer and remove listener when dialog closes
        addWindowListener(new WindowAdapter() {
            @Override
            public void windowClosing(WindowEvent e) {
                if (repaintTimer != null) {
                    repaintTimer.stop();
                }
                BackendManager.getInstance().removeNotificationListener(notificationListener);
            }
        });
    }

    void updateVelocityAt(int x, int y) {
        mouseHandler.updateVelocityAt(x, y);
    }

    int getScaledKeyHeight() {
        return mouseHandler.getScaledKeyHeight();
    }

    private void setupMouseListeners() {
        mouseHandler.install();
    }

    Note getNoteAt(int x, int y) {
        return mouseHandler.getNoteAt(x, y);
    }

    void seekToTick(long tick) {
        mouseHandler.seekToTick(tick);
    }

    boolean isBlackKey(int pitch) {
        int note = pitch % 12;
        return note == 1 || note == 3 || note == 6 || note == 8 || note == 10;
    }

    /**
     * Get the tick interval for grid lines based on current mode and zoom.
     * For AUTO mode, adapts based on pixel threshold.
     * Returns tick interval for main grid lines.
     */
    int getGridTickInterval() {
        int res = sequence.getResolution(); // ticks per beat
        int ticksPerBar = res * 4; // 4/4 time

        if (gridMode != GridMode.AUTO) {
            // Fixed modes - return exact division
            switch (gridMode) {
                case BAR:
                    return ticksPerBar;
                case HALF:
                    return ticksPerBar / 2;
                case QUARTER:
                    return res;
                case EIGHTH:
                    return res / 2;
                case SIXTEENTH:
                    return res / 4;
                case THIRTY_SECOND:
                    return res / 8;
                case TRIPLET_QUARTER:
                    return ticksPerBar / 3;
                case TRIPLET_EIGHTH:
                    return ticksPerBar / 6;
                case TRIPLET_16TH:
                    return ticksPerBar / 12;
                case TRIPLET_32ND:
                    return ticksPerBar / 24;
                default:
                    return res;
            }
        }

        // AUTO mode: find finest grid that maintains minimum pixel spacing
        float tw = getTickWidth();
        int minPixels = 15; // Minimum pixels between grid lines

        // Try divisions from finest to coarsest, return first that fits
        int[] divisions = {
                res / 8, // 1/32
                res / 4, // 1/16
                res / 2, // 1/8
                res, // 1/4 (beat)
                ticksPerBar / 2, // 1/2
                ticksPerBar // 1/1 (bar)
        };

        for (int div : divisions) {
            if (div * tw >= minPixels) {
                return div;
            }
        }
        return ticksPerBar; // Fallback to bars
    }

    /**
     * Get snap interval for note creation/editing based on grid mode.
     */
    int getSnapTickInterval() {
        return getGridTickInterval();
    }
}
