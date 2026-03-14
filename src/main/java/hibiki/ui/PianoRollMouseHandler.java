package hibiki.ui;

import hibiki.BackendManager;

import javax.swing.*;
import java.awt.event.*;

/**
 * Handles mouse interactions on the PianoRoll grid panel:
 * - Left-click to create notes (with snap-to-grid)
 * - Right-click to delete notes
 * - Left-drag to move notes
 * - Left-drag on note right edge to resize
 * - Middle-click / Ctrl+click to seek playhead
 * - Alt+drag to copy notes
 */
class PianoRollMouseHandler {
    private final PianoRoll pr;

    PianoRollMouseHandler(PianoRoll pr) {
        this.pr = pr;
    }

    /** Wire up mouse listeners on the grid panel. */
    void install() {
        MouseAdapter ma = new MouseAdapter() {
            @Override
            public void mousePressed(MouseEvent e) {
                handleMousePressed(e);
            }

            @Override
            public void mouseReleased(MouseEvent e) {
                handleMouseReleased(e);
            }

            @Override
            public void mouseDragged(MouseEvent e) {
                handleMouseDragged(e);
            }
        };

        pr.gridPanel.addMouseListener(ma);
        pr.gridPanel.addMouseMotionListener(ma);
    }

    private void handleMousePressed(MouseEvent e) {
        int kh = getScaledKeyHeight();
        float tw = pr.getTickWidth();
        int pitch = PianoRoll.NUM_KEYS - 1 - (e.getY() / kh);
        long tick = (long) (e.getX() / tw);

        // Middle-click or Ctrl+click: Seek playhead
        if (SwingUtilities.isMiddleMouseButton(e) ||
                (SwingUtilities.isLeftMouseButton(e) && e.isControlDown())) {
            seekToTick(tick);
            return;
        }

        if (pitch < 0 || pitch >= PianoRoll.NUM_KEYS)
            return;

        PianoRoll.Note clickedNote = getNoteAt(e.getX(), e.getY());

        if (SwingUtilities.isRightMouseButton(e)) {
            if (clickedNote != null) {
                pr.notes.remove(clickedNote);
                pr.gridPanel.repaint();
                pr.gridPanel.revalidate();
                pr.syncToBackend();
            }
        } else if (SwingUtilities.isLeftMouseButton(e)) {
            if (clickedNote != null) {
                int noteEndX = (int) ((clickedNote.startTick + clickedNote.durationTicks) * tw);
                if (Math.abs(e.getX() - noteEndX) <= 8) {
                    pr.resizingNote = clickedNote;
                } else {
                    pr.draggingNote = clickedNote;
                    pr.dragOffsetX = (int) (e.getX() - clickedNote.startTick * tw);
                    pr.dragOffsetY = pitch - clickedNote.pitch;
                    pr.dragOriginalPitch = clickedNote.pitch;
                    pr.dragOriginalTick = clickedNote.startTick;
                    pr.isDraggingNote = false;
                }
            } else {
                // Create new note and let user drag to adjust length
                int snapInterval = pr.getSnapTickInterval();
                long snapTick = (tick / snapInterval) * snapInterval;
                int minDuration = Math.max(snapInterval, pr.sequence.getResolution() / 8);
                PianoRoll.Note n = new PianoRoll.Note(pitch, snapTick, minDuration, 100);
                pr.notes.add(n);
                pr.resizingNote = n;
                pr.gridPanel.repaint();
                pr.gridPanel.revalidate();
            }
        }
    }

    private void handleMouseReleased(MouseEvent e) {
        boolean changed = false;
        if (pr.draggingNote != null && pr.isDraggingNote && e.isAltDown()) {
            // Alt+release: Copy note to new location
            PianoRoll.Note copy = new PianoRoll.Note(pr.draggingNote.pitch, pr.draggingNote.startTick,
                    pr.draggingNote.durationTicks, pr.draggingNote.velocity);
            pr.draggingNote.pitch = pr.dragOriginalPitch;
            pr.draggingNote.startTick = pr.dragOriginalTick;
            pr.notes.add(copy);
            changed = true;
        } else if (pr.isDraggingNote || pr.resizingNote != null) {
            changed = true;
        }
        pr.draggingNote = null;
        pr.resizingNote = null;
        pr.isDraggingNote = false;
        pr.dragOriginalPitch = -1;
        pr.gridPanel.repaint();
        pr.gridPanel.revalidate();

        if (changed) {
            pr.syncToBackend();
        }
    }

    private void handleMouseDragged(MouseEvent e) {
        float tw = pr.getTickWidth();
        if (pr.draggingNote != null) {
            int kh = getScaledKeyHeight();
            int pitch = PianoRoll.NUM_KEYS - 1 - (e.getY() / kh);
            pitch = Math.max(0, Math.min(PianoRoll.NUM_KEYS - 1, pitch));

            long tick = (long) ((e.getX() - pr.dragOffsetX) / tw);
            long snapTick;
            if (e.isShiftDown()) {
                snapTick = tick;
            } else {
                int snapInterval = pr.getSnapTickInterval();
                snapTick = Math.round((double) tick / snapInterval) * snapInterval;
            }

            if (!pr.isDraggingNote) {
                if (Math.abs(pitch - pr.dragOriginalPitch) > 0
                        || Math.abs(snapTick - pr.dragOriginalTick) > 0) {
                    pr.isDraggingNote = true;
                }
            }

            pr.draggingNote.pitch = pitch;
            pr.draggingNote.startTick = Math.max(0, snapTick);
            pr.gridPanel.repaint();
        } else if (pr.resizingNote != null) {
            long newEndTick = (long) (e.getX() / tw);
            long snapEndTick;
            if (e.isShiftDown()) {
                snapEndTick = newEndTick;
            } else {
                snapEndTick = Math.round((double) newEndTick / (pr.sequence.getResolution() / 4))
                        * (pr.sequence.getResolution() / 4);
            }

            long durTick = snapEndTick - pr.resizingNote.startTick;
            pr.resizingNote.durationTicks = Math.max(pr.sequence.getResolution() / 8, durTick);
            pr.gridPanel.repaint();
        }
    }

    // --- Utility methods extracted from PianoRoll ---

    /** Update velocity of notes at given X/Y position. Y is inverted: top=127, bottom=0. */
    void updateVelocityAt(int x, int y) {
        float tw = pr.getTickWidth();
        int panelHeight = pr.velocityPanel.getHeight() - 4;

        int newVelocity = 127 - (int) ((y - 2) * 127.0 / panelHeight);
        newVelocity = Math.max(1, Math.min(127, newVelocity));

        int thinBarWidth = 6;
        for (PianoRoll.Note n : pr.notes) {
            int noteX = (int) (n.startTick * tw);
            if (x >= noteX && x < noteX + thinBarWidth) {
                n.velocity = newVelocity;
            }
        }

        pr.velocityPanel.repaint();
        pr.gridPanel.repaint();
    }

    int getScaledKeyHeight() {
        return Theme.getInstance().scale(pr.keyHeight);
    }

    PianoRoll.Note getNoteAt(int x, int y) {
        int kh = getScaledKeyHeight();
        float tw = pr.getTickWidth();
        int pitch = PianoRoll.NUM_KEYS - 1 - (y / kh);
        long tick = (long) (x / tw);

        for (int i = pr.notes.size() - 1; i >= 0; i--) {
            PianoRoll.Note n = pr.notes.get(i);
            if (n.pitch == pitch && tick >= n.startTick && tick <= n.startTick + n.durationTicks) {
                return n;
            }
        }
        return null;
    }

    /**
     * Seek playhead to given tick, converting to absolute seconds
     * accounting for clip start time.
     */
    void seekToTick(long tick) {
        int seqRes = pr.sequence.getResolution();
        float beatsPerSecond = pr.bpm / 60.0f;
        float ticksPerSecond = beatsPerSecond * seqRes;
        float relativeSeconds = tick / ticksPerSecond;
        float absoluteSeconds = pr.clipStartTime + relativeSeconds;
        BackendManager.getInstance().seek(absoluteSeconds);
    }
}
