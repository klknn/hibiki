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
    private static final int NUM_KEYS = 128;

    // Zoom settings (adjustable)
    private int keyHeight = 12; // Default, adjustable via vertical zoom
    private float tickScale = 1.0f; // Multiplier for base tick width
    private float baseTickWidth = 1.0f; // Calculated to fit entire clip at scale=1.0

    private final File midiFile;
    private final int trackIdx;
    private final int slotIdx; // Session clip slot (-1 for timeline clips)
    private final int clipIdx; // Timeline clip index (-1 for session clips)
    private Sequence sequence;
    private Track midiTrack;
    private List<Note> notes = new ArrayList<>();

    private JPanel gridPanel;
    private JPanel keysPanel;
    private JPanel velocityPanel;
    private JScrollPane gridScroll;
    private JScrollPane keysScroll;

    // Interaction state
    private Note draggingNote = null;
    private Note resizingNote = null;
    private int dragOffsetX = 0;
    private int dragOffsetY = 0;

    // Ghost/copy state for drag
    private int dragOriginalPitch = -1;
    private long dragOriginalTick = 0;
    private boolean isDraggingNote = false;

    // Zoom sliders
    private JSlider hZoomSlider;
    private JSlider vZoomSlider;

    // Velocity editing state
    private boolean editingVelocity = false;

    // Playhead state
    private volatile float playheadPos = 0.0f; // in seconds
    private volatile float bpm = 120.0f;
    private volatile boolean isPlaying = false;
    private float clipStartTime = 0.0f; // Start time of clip on timeline (seconds)
    private javax.swing.Timer repaintTimer;

    private static class Note {
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

    public PianoRoll(Frame owner, File midiFile, int trackIdx, int slotIdx, int clipIdx) {
        super(owner, "Piano Roll - " + midiFile.getName(), false);
        this.midiFile = midiFile;
        this.trackIdx = trackIdx;
        this.slotIdx = slotIdx;
        this.clipIdx = clipIdx;

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
        this(owner, midiFile, trackIdx, slotIdx, -1);
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
            isPlaying = info.isPlaying();
        }
    }

    private void loadFromBackendData(ClipMidiData data) {
        javax.swing.SwingUtilities.invokeLater(() -> {
            notes.clear();
            int resolution = data.resolution();
            if (resolution > 0 && sequence != null) {
                // Keep resolution sync - but we use the sequence's PPQ for display
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

    private float getTickWidth() {
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
    private void syncToBackend() {
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
                int kh = getScaledKeyHeight();
                for (int i = 0; i < NUM_KEYS; i++) {
                    int pitch = NUM_KEYS - 1 - i;
                    int y = i * kh;

                    boolean isBlack = isBlackKey(pitch);
                    g.setColor(isBlack ? Color.BLACK : Color.WHITE);
                    g.fillRect(0, y, getWidth(), kh);

                    g.setColor(Color.GRAY);
                    g.drawRect(0, y, getWidth(), kh);

                    if (!isBlack && (pitch % 12 == 0)) { // C notes
                        g.setColor(Color.BLACK);
                        g.setFont(new Font("SansSerif", Font.PLAIN, Math.min(kh - 2, Theme.getInstance().scale(9))));
                        g.drawString("C" + (pitch / 12 - 1), 2, y + kh - 2);
                    }
                }
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
                int kh = getScaledKeyHeight();
                float tw = getTickWidth();

                // Draw horizontal grid lines
                for (int i = 0; i < NUM_KEYS; i++) {
                    int y = i * kh;
                    int pitch = NUM_KEYS - 1 - i;
                    g.setColor(isBlackKey(pitch) ? new Color(40, 40, 40) : Theme.getInstance().BG_DARKER);
                    g.fillRect(0, y, getWidth(), kh);
                    g.setColor(new Color(60, 60, 60));
                    g.drawLine(0, y, getWidth(), y);
                }

                // Draw vertical grid lines (beat markers)
                int res = sequence.getResolution();
                g.setColor(new Color(80, 80, 80));
                float beatWidth = res * tw;
                if (beatWidth >= 2) { // Only draw if visible
                    for (float x = 0; x < getWidth(); x += beatWidth) {
                        g.drawLine((int) x, 0, (int) x, getHeight());
                    }
                }

                // Draw ghost shadow of dragged note at original position
                if (isDraggingNote && draggingNote != null && dragOriginalPitch >= 0) {
                    int ghostX = (int) (dragOriginalTick * tw);
                    int ghostY = (NUM_KEYS - 1 - dragOriginalPitch) * kh;
                    int ghostW = Math.max(1, (int) (draggingNote.durationTicks * tw));

                    Graphics2D g2 = (Graphics2D) g;
                    Composite oldComposite = g2.getComposite();
                    g2.setComposite(AlphaComposite.getInstance(AlphaComposite.SRC_OVER, 0.3f));
                    g2.setColor(Theme.getInstance().ACCENT_BLUE);
                    g2.fillRect(ghostX, ghostY + 1, ghostW, kh - 2);
                    g2.setComposite(oldComposite);

                    // Draw dashed border
                    g2.setColor(Theme.getInstance().ACCENT_BLUE.brighter());
                    Stroke oldStroke = g2.getStroke();
                    g2.setStroke(new BasicStroke(1.0f, BasicStroke.CAP_BUTT, BasicStroke.JOIN_BEVEL, 0,
                            new float[] { 2, 2 }, 0));
                    g2.drawRect(ghostX, ghostY + 1, ghostW, kh - 2);
                    g2.setStroke(oldStroke);
                }

                // Draw notes
                for (Note n : notes) {
                    int x = (int) (n.startTick * tw);
                    int y = (NUM_KEYS - 1 - n.pitch) * kh;
                    int w = Math.max(1, (int) (n.durationTicks * tw));

                    // Draw filled rect
                    g.setColor(Theme.getInstance().ACCENT_BLUE);
                    g.fillRect(x, y + 1, w, kh - 2);

                    // Draw border
                    g.setColor(Theme.getInstance().ACCENT_BLUE.brighter());
                    g.drawRect(x, y + 1, w, kh - 2);
                }

                // Draw playhead
                // Convert playhead position (seconds relative to clip start) to ticks
                float relativePos = playheadPos - clipStartTime;
                if (relativePos >= 0) {
                    int seqRes = sequence.getResolution();
                    // Convert seconds to beats, then to ticks
                    float beatsPerSecond = bpm / 60.0f;
                    long playheadTick = (long) (relativePos * beatsPerSecond * seqRes);
                    int px = (int) (playheadTick * tw);

                    Graphics2D g2 = (Graphics2D) g;
                    g2.setColor(Color.RED);
                    g2.setStroke(new BasicStroke(2.0f));
                    g2.drawLine(px, 0, px, getHeight());
                }
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
                Graphics2D g2 = (Graphics2D) g;
                g2.setRenderingHint(RenderingHints.KEY_ANTIALIASING, RenderingHints.VALUE_ANTIALIAS_ON);

                // Background
                g2.setColor(Theme.getInstance().BG_DARKER);
                g2.fillRect(0, 0, getWidth(), getHeight());

                float tw = getTickWidth();
                int res = sequence.getResolution();
                float ticksPerBar = res * 4; // 4/4 time

                // Draw bar markers
                g2.setFont(Theme.getInstance().FONT_UI.deriveFont(Font.PLAIN, Theme.getInstance().scale(10.0f)));

                for (int bar = 0; bar * ticksPerBar * tw < getWidth() + 200; bar++) {
                    int x = (int) (bar * ticksPerBar * tw);

                    // Bar line
                    g2.setColor(new Color(255, 255, 255, 60));
                    g2.drawLine(x, 0, x, getHeight());

                    // Bar number
                    g2.setColor(Theme.getInstance().TEXT_BRIGHT);
                    g2.drawString(String.valueOf(bar + 1), x + 3, 14);

                    // Beat markers within bar
                    for (int beat = 1; beat < 4; beat++) {
                        int bx = (int) ((bar * ticksPerBar + beat * res) * tw);
                        g2.setColor(new Color(255, 255, 255, 30));
                        g2.drawLine(bx, getHeight() - 6, bx, getHeight());
                    }
                }

                // Draw playhead position indicator
                float relativePos = playheadPos - clipStartTime;
                if (relativePos >= 0) {
                    float beatsPerSecond = bpm / 60.0f;
                    long playheadTick = (long) (relativePos * beatsPerSecond * res);
                    int px = (int) (playheadTick * tw);

                    g2.setColor(Color.RED);
                    g2.setStroke(new BasicStroke(2.0f));
                    g2.drawLine(px, 0, px, getHeight());
                    // Draw small triangle indicator at top
                    int[] xPoints = { px - 4, px + 4, px };
                    int[] yPoints = { 0, 0, 6 };
                    g2.fillPolygon(xPoints, yPoints, 3);
                }

                // Bottom border
                g2.setColor(Theme.getInstance().BORDER);
                g2.drawLine(0, getHeight() - 1, getWidth(), getHeight() - 1);
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
                Graphics2D g2 = (Graphics2D) g;
                g2.setRenderingHint(RenderingHints.KEY_ANTIALIASING, RenderingHints.VALUE_ANTIALIAS_ON);

                float tw = getTickWidth();
                int panelHeight = getHeight() - 4; // Leave margin at top and bottom

                // Background
                g2.setColor(Theme.getInstance().BG_DARKER);
                g2.fillRect(0, 0, getWidth(), getHeight());

                // Draw grid lines for velocity levels
                g2.setColor(new Color(60, 60, 60));
                for (int v = 0; v <= 127; v += 32) {
                    int y = getHeight() - 2 - (int) (v / 127.0 * panelHeight);
                    g2.drawLine(0, y, getWidth(), y);
                }

                // Draw velocity label on left
                g2.setColor(Theme.getInstance().TEXT_DIM);
                g2.setFont(new Font("SansSerif", Font.PLAIN, 9));
                g2.drawString("VEL", 2, 12);

                // Draw bars for each note
                for (Note n : notes) {
                    int x = (int) (n.startTick * tw);
                    int fullWidth = Math.max(4, (int) (n.durationTicks * tw));
                    int barHeight = (int) (n.velocity / 127.0 * panelHeight);
                    int y = getHeight() - 2 - barHeight;

                    // Color based on velocity (red for high, blue for low)
                    float hue = 0.6f - (n.velocity / 127.0f) * 0.6f; // Blue to red
                    Color barColor = Color.getHSBColor(hue, 0.8f, 0.9f);

                    // Draw ghost/shadow showing full note duration (semi-transparent)
                    g2.setColor(new Color(barColor.getRed(), barColor.getGreen(), barColor.getBlue(), 30));
                    g2.fillRect(x, y, fullWidth, barHeight);

                    // Draw thin editable bar at start (4px wide) - solid
                    int thinBarWidth = 4;
                    g2.setColor(barColor);
                    g2.fillRect(x, y, thinBarWidth, barHeight);

                    // Highlight thin bar border
                    g2.setColor(new Color(255, 255, 255, 80));
                    g2.drawRect(x, y, thinBarWidth, barHeight);
                }
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

        // Horizontal zoom slider: 100 = 100% = fits entire clip, 500 = 500% = zoomed in
        // 5x
        JLabel hLabel = new JLabel("H:");
        hLabel.setForeground(Theme.getInstance().TEXT_DIM);
        zoomPanel.add(hLabel);
        hZoomSlider = new JSlider(100, 500, 100);
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

    /**
     * Update velocity of notes at the given X position based on Y position
     * Y is inverted: top = 127, bottom = 0
     */
    private void updateVelocityAt(int x, int y) {
        float tw = getTickWidth();
        int panelHeight = velocityPanel.getHeight() - 4;

        // Calculate target velocity from Y (inverted: top = 127)
        int newVelocity = 127 - (int) ((y - 2) * 127.0 / panelHeight);
        newVelocity = Math.max(1, Math.min(127, newVelocity)); // Clamp to 1-127

        // Find notes whose thin bar (4px at start) is at this X position
        int thinBarWidth = 6; // Slightly larger hit area for usability
        for (Note n : notes) {
            int noteX = (int) (n.startTick * tw);

            // Only match if clicking on the thin bar at the start of the note
            if (x >= noteX && x < noteX + thinBarWidth) {
                n.velocity = newVelocity;
            }
        }

        velocityPanel.repaint();
        gridPanel.repaint(); // Force grid repaint too for any highlighting
    }

    private int getScaledKeyHeight() {
        return Theme.getInstance().scale(keyHeight);
    }

    private void setupMouseListeners() {
        MouseAdapter ma = new MouseAdapter() {
            @Override
            public void mousePressed(MouseEvent e) {
                int kh = getScaledKeyHeight();
                float tw = getTickWidth();
                int pitch = NUM_KEYS - 1 - (e.getY() / kh);
                long tick = (long) (e.getX() / tw);

                // Middle-click or Ctrl+click: Seek playhead to this position
                if (SwingUtilities.isMiddleMouseButton(e) ||
                        (SwingUtilities.isLeftMouseButton(e) && e.isControlDown())) {
                    seekToTick(tick);
                    return;
                }

                if (pitch < 0 || pitch >= NUM_KEYS)
                    return;

                Note clickedNote = getNoteAt(e.getX(), e.getY());

                if (SwingUtilities.isRightMouseButton(e)) {
                    if (clickedNote != null) {
                        notes.remove(clickedNote);
                        gridPanel.repaint();
                        gridPanel.revalidate();
                        syncToBackend(); // Auto-sync after delete
                    }
                } else if (SwingUtilities.isLeftMouseButton(e)) {
                    if (clickedNote != null) {
                        int noteEndX = (int) ((clickedNote.startTick + clickedNote.durationTicks) * tw);
                        // If clicking near right edge -> resize
                        if (Math.abs(e.getX() - noteEndX) <= 8) {
                            resizingNote = clickedNote;
                        } else {
                            draggingNote = clickedNote;
                            dragOffsetX = (int) (e.getX() - clickedNote.startTick * tw);
                            dragOffsetY = pitch - clickedNote.pitch;
                            // Store original position for ghost
                            dragOriginalPitch = clickedNote.pitch;
                            dragOriginalTick = clickedNote.startTick;
                            isDraggingNote = false;
                        }
                    } else {
                        // Create new note and let user drag to adjust length
                        long snapTick = (tick / (sequence.getResolution() / 4)) * (sequence.getResolution() / 4);
                        int minDuration = sequence.getResolution() / 8; // 8th note minimum
                        Note n = new Note(pitch, snapTick, minDuration, 100);
                        notes.add(n);
                        resizingNote = n; // Set to resizing mode so user can drag to adjust length
                        gridPanel.repaint();
                        gridPanel.revalidate();
                        // Don't sync yet - wait until mouseReleased
                    }
                }
            }

            @Override
            public void mouseReleased(MouseEvent e) {
                boolean changed = false;
                if (draggingNote != null && isDraggingNote && e.isAltDown()) {
                    // Alt+release: Copy note to new location
                    Note copy = new Note(draggingNote.pitch, draggingNote.startTick,
                            draggingNote.durationTicks, draggingNote.velocity);
                    // Restore original note position
                    draggingNote.pitch = dragOriginalPitch;
                    draggingNote.startTick = dragOriginalTick;
                    // Add the copy
                    notes.add(copy);
                    changed = true;
                } else if (isDraggingNote || resizingNote != null) {
                    changed = true;
                }
                draggingNote = null;
                resizingNote = null;
                isDraggingNote = false;
                dragOriginalPitch = -1;
                gridPanel.repaint();
                gridPanel.revalidate();

                // Auto-sync to backend on any note change
                if (changed) {
                    syncToBackend();
                }
            }

            @Override
            public void mouseDragged(MouseEvent e) {
                float tw = getTickWidth();
                if (draggingNote != null) {
                    int kh = getScaledKeyHeight();
                    int pitch = NUM_KEYS - 1 - (e.getY() / kh);
                    pitch = Math.max(0, Math.min(NUM_KEYS - 1, pitch));

                    long tick = (long) ((e.getX() - dragOffsetX) / tw);
                    long snapTick;
                    if (e.isShiftDown()) {
                        // Shift held: no snap
                        snapTick = tick;
                    } else {
                        // Snap to 16th notes
                        snapTick = Math.round((double) tick / (sequence.getResolution() / 4))
                                * (sequence.getResolution() / 4);
                    }

                    // Check drag threshold
                    if (!isDraggingNote) {
                        if (Math.abs(pitch - dragOriginalPitch) > 0 || Math.abs(snapTick - dragOriginalTick) > 0) {
                            isDraggingNote = true;
                        }
                    }

                    draggingNote.pitch = pitch;
                    draggingNote.startTick = Math.max(0, snapTick);
                    gridPanel.repaint();
                } else if (resizingNote != null) {
                    long newEndTick = (long) (e.getX() / tw);
                    long snapEndTick;
                    if (e.isShiftDown()) {
                        snapEndTick = newEndTick;
                    } else {
                        snapEndTick = Math.round((double) newEndTick / (sequence.getResolution() / 4))
                                * (sequence.getResolution() / 4);
                    }

                    long durTick = snapEndTick - resizingNote.startTick;
                    resizingNote.durationTicks = Math.max(sequence.getResolution() / 8, durTick);
                    gridPanel.repaint();
                }
            }
        };

        gridPanel.addMouseListener(ma);
        gridPanel.addMouseMotionListener(ma);
    }

    private Note getNoteAt(int x, int y) {
        int kh = getScaledKeyHeight();
        float tw = getTickWidth();
        int pitch = NUM_KEYS - 1 - (y / kh);
        long tick = (long) (x / tw);

        for (int i = notes.size() - 1; i >= 0; i--) {
            Note n = notes.get(i);
            if (n.pitch == pitch && tick >= n.startTick && tick <= n.startTick + n.durationTicks) {
                return n;
            }
        }
        return null;
    }

    /**
     * Seek the playhead to the given tick position within the clip
     * Converts tick to absolute time (seconds) accounting for clip's start on
     * timeline
     */
    private void seekToTick(long tick) {
        int seqRes = sequence.getResolution();
        // Convert ticks to seconds relative to clip start
        float beatsPerSecond = bpm / 60.0f;
        float ticksPerSecond = beatsPerSecond * seqRes;
        float relativeSeconds = tick / ticksPerSecond;
        // Add clip start time to get absolute position on timeline
        float absoluteSeconds = clipStartTime + relativeSeconds;
        BackendManager.getInstance().seek(absoluteSeconds);
    }

    private boolean isBlackKey(int pitch) {
        int note = pitch % 12;
        return note == 1 || note == 3 || note == 6 || note == 8 || note == 10;
    }
}
