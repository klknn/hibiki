package hibiki.ui;

import java.awt.*;
import javax.swing.JPanel;
import java.util.List;

/**
 * Handles rendering for PianoRoll panels: the note grid, velocity bars, time ruler,
 * and piano key labels.
 */
class PianoRollRenderer {
    private final PianoRoll pianoRoll;

    PianoRollRenderer(PianoRoll pianoRoll) {
        this.pianoRoll = pianoRoll;
    }

    /**
     * Paint the piano key labels panel (row header).
     */
    void paintKeyLabels(Graphics g, JPanel panel, int numKeys, int scaledKeyHeight) {
        Graphics2D g2 = (Graphics2D) g;
        g2.setRenderingHint(RenderingHints.KEY_ANTIALIASING, RenderingHints.VALUE_ANTIALIAS_ON);

        for (int i = 0; i < numKeys; i++) {
            int pitch = numKeys - 1 - i;
            int y = i * scaledKeyHeight;
            boolean black = pianoRoll.isBlackKey(pitch);

            g2.setColor(black ? new Color(60, 60, 60) : new Color(240, 240, 240));
            g2.fillRect(0, y, panel.getWidth(), scaledKeyHeight);
            g2.setColor(new Color(180, 180, 180));
            g2.drawLine(0, y, panel.getWidth(), y);

            if (pitch % 12 == 0) {
                g2.setColor(Color.DARK_GRAY);
                g2.setFont(new Font("SansSerif", Font.PLAIN, 9));
                g2.drawString("C" + (pitch / 12 - 1), 2, y + scaledKeyHeight - 2);
            }
        }
    }

    /**
     * Paint the note grid with horizontal key lines, vertical grid lines,
     * drag ghost, notes, and playhead.
     */
    void paintGrid(Graphics g, JPanel gridPanel, int numKeys, int scaledKeyHeight, float tickWidth,
                   javax.sound.midi.Sequence sequence, List<PianoRoll.Note> notes,
                   boolean isDraggingNote, PianoRoll.Note draggingNote,
                   int dragOriginalPitch, long dragOriginalTick,
                   float playheadPos, float clipStartTime, float bpm) {
        int kh = scaledKeyHeight;
        float tw = tickWidth;

        // Draw horizontal grid lines (key rows)
        for (int i = 0; i < numKeys; i++) {
            int y = i * kh;
            int pitch = numKeys - 1 - i;
            g.setColor(pianoRoll.isBlackKey(pitch) ? new Color(40, 40, 40) : Theme.getInstance().BG_DARKER);
            g.fillRect(0, y, gridPanel.getWidth(), kh);
            g.setColor(new Color(60, 60, 60));
            g.drawLine(0, y, gridPanel.getWidth(), y);
        }

        // Draw vertical grid lines
        int res = sequence.getResolution();
        int ticksPerBar = res * 4;
        int gridTicks = pianoRoll.getGridTickInterval();
        float gridWidth = gridTicks * tw;
        Graphics2D g2 = (Graphics2D) g;

        // Subdivision lines (finest)
        if (gridWidth >= 2) {
            g2.setColor(new Color(60, 60, 60));
            for (long tick = 0; tick * tw < gridPanel.getWidth(); tick += gridTicks) {
                int x = (int) (tick * tw);
                g2.drawLine(x, 0, x, gridPanel.getHeight());
            }
        }

        // Beat lines (quarter notes)
        float beatWidth = res * tw;
        if (beatWidth >= 4 && gridTicks < res) {
            g2.setColor(new Color(90, 90, 90));
            for (long tick = 0; tick * tw < gridPanel.getWidth(); tick += res) {
                int x = (int) (tick * tw);
                g2.drawLine(x, 0, x, gridPanel.getHeight());
            }
        }

        // Bar lines (brightest)
        float barWidth = ticksPerBar * tw;
        if (barWidth >= 4) {
            g2.setColor(new Color(120, 120, 120));
            g2.setStroke(new BasicStroke(1.5f));
            for (long tick = 0; tick * tw < gridPanel.getWidth(); tick += ticksPerBar) {
                int x = (int) (tick * tw);
                g2.drawLine(x, 0, x, gridPanel.getHeight());
            }
            g2.setStroke(new BasicStroke(1.0f));
        }

        // Draw ghost shadow of dragged note
        if (isDraggingNote && draggingNote != null && dragOriginalPitch >= 0) {
            int ghostX = (int) (dragOriginalTick * tw);
            int ghostY = (numKeys - 1 - dragOriginalPitch) * kh;
            int ghostW = Math.max(1, (int) (draggingNote.durationTicks * tw));

            Composite oldComposite = g2.getComposite();
            g2.setComposite(AlphaComposite.getInstance(AlphaComposite.SRC_OVER, 0.3f));
            g2.setColor(Theme.getInstance().ACCENT_BLUE);
            g2.fillRect(ghostX, ghostY + 1, ghostW, kh - 2);
            g2.setComposite(oldComposite);
            g2.setColor(Theme.getInstance().ACCENT_BLUE.brighter());
            Stroke oldStroke = g2.getStroke();
            g2.setStroke(new BasicStroke(1.0f, BasicStroke.CAP_BUTT, BasicStroke.JOIN_BEVEL, 0,
                    new float[]{2, 2}, 0));
            g2.drawRect(ghostX, ghostY + 1, ghostW, kh - 2);
            g2.setStroke(oldStroke);
        }

        // Draw notes
        for (PianoRoll.Note n : notes) {
            int x = (int) (n.startTick * tw);
            int y = (numKeys - 1 - n.pitch) * kh;
            int w = Math.max(1, (int) (n.durationTicks * tw));
            g.setColor(Theme.getInstance().ACCENT_BLUE);
            g.fillRect(x, y + 1, w, kh - 2);
            g.setColor(Theme.getInstance().ACCENT_BLUE.brighter());
            g.drawRect(x, y + 1, w, kh - 2);
        }

        // Draw playhead
        float relativePos = playheadPos - clipStartTime;
        if (relativePos >= 0) {
            int seqRes = sequence.getResolution();
            float beatsPerSecond = bpm / 60.0f;
            long playheadTick = (long) (relativePos * beatsPerSecond * seqRes);
            int px = (int) (playheadTick * tw);
            g2.setColor(Color.RED);
            g2.setStroke(new BasicStroke(2.0f));
            g2.drawLine(px, 0, px, gridPanel.getHeight());
        }
    }

    /**
     * Paint the time ruler (bar numbers, beat markers, playhead indicator).
     */
    void paintTimeRuler(Graphics g, JPanel panel, javax.sound.midi.Sequence sequence,
                        float tickWidth, float playheadPos, float clipStartTime, float bpm) {
        Graphics2D g2 = (Graphics2D) g;
        g2.setRenderingHint(RenderingHints.KEY_ANTIALIASING, RenderingHints.VALUE_ANTIALIAS_ON);

        g2.setColor(Theme.getInstance().BG_DARKER);
        g2.fillRect(0, 0, panel.getWidth(), panel.getHeight());

        float tw = tickWidth;
        int res = sequence.getResolution();
        float ticksPerBar = res * 4;

        g2.setFont(Theme.getInstance().FONT_UI.deriveFont(Font.PLAIN, Theme.getInstance().scale(10.0f)));

        for (int bar = 0; bar * ticksPerBar * tw < panel.getWidth() + 200; bar++) {
            int x = (int) (bar * ticksPerBar * tw);
            g2.setColor(new Color(255, 255, 255, 60));
            g2.drawLine(x, 0, x, panel.getHeight());
            g2.setColor(Theme.getInstance().TEXT_BRIGHT);
            g2.drawString(String.valueOf(bar + 1), x + 3, 14);
            for (int beat = 1; beat < 4; beat++) {
                int bx = (int) ((bar * ticksPerBar + beat * res) * tw);
                g2.setColor(new Color(255, 255, 255, 30));
                g2.drawLine(bx, panel.getHeight() - 6, bx, panel.getHeight());
            }
        }

        // Playhead indicator
        float relativePos = playheadPos - clipStartTime;
        if (relativePos >= 0) {
            float beatsPerSecond = bpm / 60.0f;
            long playheadTick = (long) (relativePos * beatsPerSecond * res);
            int px = (int) (playheadTick * tw);
            g2.setColor(Color.RED);
            g2.setStroke(new BasicStroke(2.0f));
            g2.drawLine(px, 0, px, panel.getHeight());
            int[] xPoints = {px - 4, px + 4, px};
            int[] yPoints = {0, 0, 6};
            g2.fillPolygon(xPoints, yPoints, 3);
        }

        g2.setColor(Theme.getInstance().BORDER);
        g2.drawLine(0, panel.getHeight() - 1, panel.getWidth(), panel.getHeight() - 1);
    }

    /**
     * Paint velocity bars for each note.
     */
    void paintVelocity(Graphics g, JPanel panel, float tickWidth, List<PianoRoll.Note> notes) {
        Graphics2D g2 = (Graphics2D) g;
        g2.setRenderingHint(RenderingHints.KEY_ANTIALIASING, RenderingHints.VALUE_ANTIALIAS_ON);

        float tw = tickWidth;
        int panelHeight = panel.getHeight() - 4;

        g2.setColor(Theme.getInstance().BG_DARKER);
        g2.fillRect(0, 0, panel.getWidth(), panel.getHeight());

        // Grid lines for velocity levels
        g2.setColor(new Color(60, 60, 60));
        for (int v = 0; v <= 127; v += 32) {
            int y = panel.getHeight() - 2 - (int) (v / 127.0 * panelHeight);
            g2.drawLine(0, y, panel.getWidth(), y);
        }

        g2.setColor(Theme.getInstance().TEXT_DIM);
        g2.setFont(new Font("SansSerif", Font.PLAIN, 9));
        g2.drawString("VEL", 2, 12);

        for (PianoRoll.Note n : notes) {
            int x = (int) (n.startTick * tw);
            int fullWidth = Math.max(4, (int) (n.durationTicks * tw));
            int barHeight = (int) (n.velocity / 127.0 * panelHeight);
            int y = panel.getHeight() - 2 - barHeight;

            float hue = 0.6f - (n.velocity / 127.0f) * 0.6f;
            Color barColor = Color.getHSBColor(hue, 0.8f, 0.9f);

            g2.setColor(new Color(barColor.getRed(), barColor.getGreen(), barColor.getBlue(), 30));
            g2.fillRect(x, y, fullWidth, barHeight);
            int thinBarWidth = 4;
            g2.setColor(barColor);
            g2.fillRect(x, y, thinBarWidth, barHeight);
            g2.setColor(new Color(255, 255, 255, 80));
            g2.drawRect(x, y, thinBarWidth, barHeight);
        }
    }
}
