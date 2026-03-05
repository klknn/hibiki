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
    private static final int KEY_HEIGHT = 12;
    private static final int TICK_WIDTH = 2; // Pixels per tick (adjustable)
    private static final int NUM_KEYS = 128;
    
    private final File midiFile;
    private final int trackIdx;
    private final int slotIdx;
    private Sequence sequence;
    private Track midiTrack;
    private List<Note> notes = new ArrayList<>();
    
    private JPanel gridPanel;
    private JScrollPane scrollPane;
    
    // Interaction state
    private Note draggingNote = null;
    private Note resizingNote = null;
    private int dragOffsetX = 0;
    private int dragOffsetY = 0;
    
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
        return null; // Should not happen in normal usage if passed MainView's frame
    }
    
    private SessionView findSessionView(Container c) {
        if (c instanceof SessionView) return (SessionView)c;
        for (Component child : c.getComponents()) {
            if (child instanceof Container) {
                SessionView sv = findSessionView((Container)child);
                if (sv != null) return sv;
            }
        }
        return null; // Should not happen if correctly integrated
    }

    private void initUI() {
        setLayout(new BorderLayout());
        setBackground(Theme.getInstance().BG_DARKER);
        
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
        JPanel keysPanel = new JPanel() {
            @Override
            public Dimension getPreferredSize() {
                return new Dimension(Theme.getInstance().scale(60), NUM_KEYS * Theme.getInstance().scale(KEY_HEIGHT));
            }
            
            @Override
            protected void paintComponent(Graphics g) {
                super.paintComponent(g);
                int kh = Theme.getInstance().scale(KEY_HEIGHT);
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
                        g.setFont(new Font("SansSerif", Font.PLAIN, Theme.getInstance().scale(9)));
                        g.drawString("C" + (pitch / 12 - 1), 2, y + kh - 2);
                    }
                }
            }
        };
        
        // Grid (Right)
        gridPanel = new JPanel() {
            @Override
            public Dimension getPreferredSize() {
                long maxTick = 0;
                for (Note n : notes) {
                    if (n.startTick + n.durationTicks > maxTick) {
                        maxTick = n.startTick + n.durationTicks;
                    }
                }
                maxTick = Math.max(maxTick, sequence.getResolution() * 4 * 4); // At least 4 bars
                return new Dimension((int)(maxTick * TICK_WIDTH) + 200, NUM_KEYS * Theme.getInstance().scale(KEY_HEIGHT));
            }
            
            @Override
            protected void paintComponent(Graphics g) {
                super.paintComponent(g);
                int kh = Theme.getInstance().scale(KEY_HEIGHT);
                
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
                for (int x = 0; x < getWidth(); x += res * TICK_WIDTH) {
                    g.drawLine(x, 0, x, getHeight());
                }
                
                // Draw notes
                for (Note n : notes) {
                    int x = (int)(n.startTick * TICK_WIDTH);
                    int y = (NUM_KEYS - 1 - n.pitch) * kh;
                    int w = (int)(n.durationTicks * TICK_WIDTH);
                    
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
        JScrollPane gridScroll = new JScrollPane(gridPanel);
        gridScroll.getVerticalScrollBar().setUnitIncrement(Theme.getInstance().scale(KEY_HEIGHT));
        gridScroll.getHorizontalScrollBar().setUnitIncrement(Theme.getInstance().scale(20));
        
        JScrollPane keysScroll = new JScrollPane(keysPanel);
        keysScroll.setVerticalScrollBarPolicy(JScrollPane.VERTICAL_SCROLLBAR_NEVER);
        keysScroll.setHorizontalScrollBarPolicy(JScrollPane.HORIZONTAL_SCROLLBAR_NEVER);
        
        gridScroll.getVerticalScrollBar().getModel().addChangeListener(e -> {
            keysScroll.getVerticalScrollBar().setValue(gridScroll.getVerticalScrollBar().getValue());
        });
        
        JSplitPane split = new JSplitPane(JSplitPane.HORIZONTAL_SPLIT, keysScroll, gridScroll);
        split.setDividerLocation(Theme.getInstance().scale(60));
        split.setDividerSize(0);
        
        add(split, BorderLayout.CENTER);
        
        // Scroll to middle C roughly
        SwingUtilities.invokeLater(() -> {
            int middleY = (NUM_KEYS - 1 - 60) * Theme.getInstance().scale(KEY_HEIGHT);
            gridScroll.getVerticalScrollBar().setValue(Math.max(0, middleY - getHeight()/2));
        });
    }
    
    private void setupMouseListeners() {
        MouseAdapter ma = new MouseAdapter() {
            @Override
            public void mousePressed(MouseEvent e) {
                int kh = Theme.getInstance().scale(KEY_HEIGHT);
                int pitch = NUM_KEYS - 1 - (e.getY() / kh);
                long tick = e.getX() / TICK_WIDTH;
                
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
                        int noteEndX = (int)((clickedNote.startTick + clickedNote.durationTicks) * TICK_WIDTH);
                        // If clicking near right edge -> resize
                        if (Math.abs(e.getX() - noteEndX) <= 8) {
                            resizingNote = clickedNote;
                        } else {
                            draggingNote = clickedNote;
                            dragOffsetX = (int)(e.getX() - clickedNote.startTick * TICK_WIDTH);
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
                if (draggingNote != null) {
                    int kh = Theme.getInstance().scale(KEY_HEIGHT);
                    int pitch = NUM_KEYS - 1 - (e.getY() / kh);
                    pitch = Math.max(0, Math.min(NUM_KEYS - 1, pitch));
                    
                    long tick = (e.getX() - dragOffsetX) / TICK_WIDTH;
                    // Snap to 16th notes
                    long snapTick = Math.round((double)tick / (sequence.getResolution() / 4)) * (sequence.getResolution() / 4);
                    
                    draggingNote.pitch = pitch;
                    draggingNote.startTick = Math.max(0, snapTick);
                    gridPanel.repaint();
                } else if (resizingNote != null) {
                    long newEndTick = e.getX() / TICK_WIDTH;
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
        int kh = Theme.getInstance().scale(KEY_HEIGHT);
        int pitch = NUM_KEYS - 1 - (y / kh);
        long tick = x / TICK_WIDTH;
        
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
