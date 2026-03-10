package hibiki.ui;

import javax.swing.*;
import java.awt.*;
import java.awt.event.*;
import java.io.File;
import java.util.ArrayList;
import java.util.List;

import javax.sound.midi.*;

import hibiki.BackendManager;

public class PianoRoll extends JDialog {
    private static final int NUM_KEYS = 128;
    
    // Zoom settings (adjustable)
    private int keyHeight = 12; // Default, adjustable via vertical zoom
    private float tickScale = 1.0f; // Multiplier for base tick width
    private float baseTickWidth = 1.0f; // Calculated to fit entire clip at scale=1.0

    private final File midiFile;
    private final int trackIdx;
    private final int slotIdx;
    private Sequence sequence;
    private Track midiTrack;
    private List<Note> notes = new ArrayList<>();
    
    private JPanel gridPanel;
    private JPanel keysPanel;
    private JScrollPane gridScroll;
    private JScrollPane keysScroll;
    
    // Interaction state
    private Note draggingNote = null;
    private Note resizingNote = null;
    private int dragOffsetX = 0;
    private int dragOffsetY = 0;
    
    // Zoom sliders
    private JSlider hZoomSlider;
    private JSlider vZoomSlider;

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

    public PianoRoll(Frame owner, File midiFile, int trackIdx, int slotIdx) {
        super(owner, "Piano Roll - " + midiFile.getName(), false);
        this.midiFile = midiFile;
        this.trackIdx = trackIdx;
        this.slotIdx = slotIdx;
        
        loadMidi();
        initUI();
        
        setSize(Theme.getInstance().scale(800), Theme.getInstance().scale(600));
        setLocationRelativeTo(owner);
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
                    if (midiTrack == null) midiTrack = sequence.getTracks()[0];
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
            JOptionPane.showMessageDialog(this, "Failed to load MIDI: " + e.getMessage(), "Error", JOptionPane.ERROR_MESSAGE);
            // Create empty fallback
            try {
                sequence = new Sequence(Sequence.PPQ, 96);
                midiTrack = sequence.createTrack();
            } catch (Exception ex) {}
        }
    }
    
    private boolean hasNotes(Track t) {
        for (int i = 0; i < t.size(); i++) {
            MidiMessage msg = t.get(i).getMessage();
            if (msg instanceof ShortMessage) {
                ShortMessage sm = (ShortMessage) msg;
                if (sm.getCommand() == ShortMessage.NOTE_ON) return true;
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
        try {
            // Rebuild track
            sequence.deleteTrack(midiTrack);
            midiTrack = sequence.createTrack();
            
            for (Note n : notes) {
                if (n.durationTicks <= 0) n.durationTicks = 1; // Failsafe
                ShortMessage onInfo = new ShortMessage();
                onInfo.setMessage(ShortMessage.NOTE_ON, 0, n.pitch, n.velocity);
                MidiEvent onEvent = new MidiEvent(onInfo, n.startTick);
                midiTrack.add(onEvent);
                
                ShortMessage offInfo = new ShortMessage();
                offInfo.setMessage(ShortMessage.NOTE_OFF, 0, n.pitch, 0);
                MidiEvent offEvent = new MidiEvent(offInfo, n.startTick + n.durationTicks);
                midiTrack.add(offEvent);
            }
            
            MidiSystem.write(sequence, 1, midiFile);
            
            // Tell engine to reload
            ((SessionView)getParentSessionView()).sendLoadClip(trackIdx, slotIdx, midiFile.getAbsolutePath(), false);
            
        } catch (Exception e) {
            e.printStackTrace();
            JOptionPane.showMessageDialog(this, "Failed to save MIDI: " + e.getMessage(), "Error", JOptionPane.ERROR_MESSAGE);
        }
    }
    
    private SessionView getParentSessionView() {
        Component c = getOwner();
        while (c != null) {
            if (c instanceof JFrame) {
                JFrame f = (JFrame)c;
                return findSessionView(f.getContentPane());
            }
            c = c.getParent();
        }
        return null;
    }
    
    private SessionView findSessionView(Container c) {
        if (c instanceof SessionView) return (SessionView)c;
        for (Component child : c.getComponents()) {
            if (child instanceof Container) {
                SessionView sv = findSessionView((Container)child);
                if (sv != null) return sv;
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
        
        JButton btnSave = new JButton("Save & Apply");
        btnSave.setFocusPainted(false);
        btnSave.addActionListener(e -> saveMidi());
        toolbar.add(btnSave);
        
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
            }
        };
        
        setupMouseListeners();
        
        // Sync scrolling
        gridScroll = new JScrollPane(gridPanel);
        gridScroll.getVerticalScrollBar().setUnitIncrement(getScaledKeyHeight());
        gridScroll.getHorizontalScrollBar().setUnitIncrement(Theme.getInstance().scale(20));
        
        keysScroll = new JScrollPane(keysPanel);
        keysScroll.setVerticalScrollBarPolicy(JScrollPane.VERTICAL_SCROLLBAR_NEVER);
        keysScroll.setHorizontalScrollBarPolicy(JScrollPane.HORIZONTAL_SCROLLBAR_NEVER);
        
        gridScroll.getVerticalScrollBar().getModel().addChangeListener(e -> {
            keysScroll.getVerticalScrollBar().setValue(gridScroll.getVerticalScrollBar().getValue());
        });
        
        JSplitPane split = new JSplitPane(JSplitPane.HORIZONTAL_SPLIT, keysScroll, gridScroll);
        split.setDividerLocation(Theme.getInstance().scale(60));
        split.setDividerSize(0);
        
        // Main content panel with zoom sliders at bottom right
        JPanel mainContent = new JPanel(new BorderLayout());
        mainContent.add(split, BorderLayout.CENTER);

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
                
                if (pitch < 0 || pitch >= NUM_KEYS) return;
                
                Note clickedNote = getNoteAt(e.getX(), e.getY());
                
                if (SwingUtilities.isRightMouseButton(e)) {
                    if (clickedNote != null) {
                        notes.remove(clickedNote);
                        gridPanel.repaint();
                        gridPanel.revalidate();
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
                            dragOffsetY = pitch - clickedNote.pitch; // Usually 0
                        }
                    } else {
                        // Create new note
                        long snapTick = (tick / (sequence.getResolution() / 4)) * (sequence.getResolution() / 4); // 16th note snap
                        Note n = new Note(pitch, snapTick, sequence.getResolution() / 4, 100);
                        notes.add(n);
                        draggingNote = n;
                        dragOffsetX = 0;
                        gridPanel.repaint();
                        gridPanel.revalidate();
                    }
                }
            }
            
            @Override
            public void mouseReleased(MouseEvent e) {
                draggingNote = null;
                resizingNote = null;
                gridPanel.revalidate(); // Recompute preferred size if bounds expanded
            }
            
            @Override
            public void mouseDragged(MouseEvent e) {
                float tw = getTickWidth();
                if (draggingNote != null) {
                    int kh = getScaledKeyHeight();
                    int pitch = NUM_KEYS - 1 - (e.getY() / kh);
                    pitch = Math.max(0, Math.min(NUM_KEYS - 1, pitch));
                    
                    long tick = (long) ((e.getX() - dragOffsetX) / tw);
                    // Snap to 16th notes
                    long snapTick = Math.round((double)tick / (sequence.getResolution() / 4)) * (sequence.getResolution() / 4);
                    
                    draggingNote.pitch = pitch;
                    draggingNote.startTick = Math.max(0, snapTick);
                    gridPanel.repaint();
                } else if (resizingNote != null) {
                    long newEndTick = (long) (e.getX() / tw);
                    long snapEndTick = Math.round((double)newEndTick / (sequence.getResolution() / 4)) * (sequence.getResolution() / 4);
                    
                    long durTick = snapEndTick - resizingNote.startTick;
                    resizingNote.durationTicks = Math.max(sequence.getResolution() / 8, durTick); // Min 32nd note
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

    private boolean isBlackKey(int pitch) {
        int note = pitch % 12;
        return note == 1 || note == 3 || note == 6 || note == 8 || note == 10;
    }
}
