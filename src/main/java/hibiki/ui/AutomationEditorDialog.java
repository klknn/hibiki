package hibiki.ui;

import hibiki.BackendManager;
import hibiki.pb.commands.*;
import hibiki.pb.core.EntityRef;
import hibiki.pb.core.AutomationPoint;

import javax.swing.*;
import java.awt.*;
import java.awt.event.*;
import java.util.ArrayList;
import java.util.Comparator;
import java.util.List;

/**
 * A dedicated automation editor dialog, similar to the Piano Roll for MIDI.
 * Opens via right-click on an automation lane → "Edit Automation..."
 * Provides a full-size view with zooming, beat grid, value rulers,
 * and fine-grained point editing.
 */
public class AutomationEditorDialog extends JDialog {
    private final int trackIdx;
    private final int laneIdx;
    private final String paramName;
    private final List<AutomationEditor.AutoPoint> points;
    private final EditorPanel editorPanel;

    private float bpm;
    private float pixelsPerBeat = 40.0f;
    private int dragIdx = -1;
    private int tensionDragIdx = -1; // segment whose tension is being dragged

    private static final int VALUE_RULER_WIDTH = 50;
    private static final int BEAT_RULER_HEIGHT = 30;
    private static final int PAD = 8;

    public AutomationEditorDialog(Frame owner, int trackIdx, int laneIdx, String paramName,
                                   List<AutomationEditor.AutoPoint> points, float bpm) {
        super(owner, "Automation: " + paramName, false);
        this.trackIdx = trackIdx;
        this.laneIdx = laneIdx;
        this.paramName = paramName;
        this.bpm = bpm;
        // Deep copy points
        this.points = new ArrayList<>();
        for (AutomationEditor.AutoPoint p : points) {
            this.points.add(new AutomationEditor.AutoPoint(p.timeBeats, p.value, p.tension));
        }

        setSize(800, 400);
        setLocationRelativeTo(owner);
        getContentPane().setBackground(Theme.getInstance().BG_DARK);

        editorPanel = new EditorPanel();
        JScrollPane scroll = new JScrollPane(editorPanel);
        scroll.setBorder(null);
        scroll.getHorizontalScrollBar().setUnitIncrement(20);
        scroll.getVerticalScrollBar().setUnitIncrement(10);
        add(scroll, BorderLayout.CENTER);

        // Zoom controls
        JPanel bottomBar = new JPanel(new FlowLayout(FlowLayout.LEFT));
        bottomBar.setBackground(Theme.getInstance().BG_DARKER);
        JButton zoomIn = new JButton("Zoom +");
        zoomIn.addActionListener(e -> { pixelsPerBeat *= 1.5f; editorPanel.updateSize(); editorPanel.repaint(); });
        JButton zoomOut = new JButton("Zoom -");
        zoomOut.addActionListener(e -> { pixelsPerBeat = Math.max(10, pixelsPerBeat / 1.5f); editorPanel.updateSize(); editorPanel.repaint(); });
        bottomBar.add(zoomIn);
        bottomBar.add(zoomOut);
        add(bottomBar, BorderLayout.SOUTH);
    }

    private void sendUpdate() {
        this.points.sort(Comparator.comparingDouble(a -> a.timeBeats));
        AutomationCmd.Builder cmdBuilder = AutomationCmd.newBuilder()
                .setAction(AutomationCmd.Action.ACTION_UPDATE_POINTS)
                .setTarget(EntityRef.newBuilder()
                        .setTrackIndex(trackIdx)
                        .setLaneIndex(laneIdx));
        for (AutomationEditor.AutoPoint p : points) {
            cmdBuilder.addPoints(AutomationPoint.newBuilder()
                    .setTimeBeats(p.timeBeats)
                    .setValue(p.value)
                    .setTension(p.tension));
        }
        BackendManager.getInstance().sendRequest(Request.newBuilder()
                .setAutomation(cmdBuilder)
                .build());
    }

    private class EditorPanel extends JPanel {
        EditorPanel() {
            setBackground(new Color(25, 25, 40));
            updateSize();

            addMouseListener(new MouseAdapter() {
                @Override
                public void mousePressed(MouseEvent e) {
                    handlePress(e);
                }
                @Override
                public void mouseReleased(MouseEvent e) {
                    if (dragIdx >= 0 || tensionDragIdx >= 0) {
                        sendUpdate();
                    }
                    dragIdx = -1;
                    tensionDragIdx = -1;
                }
            });
            addMouseMotionListener(new MouseMotionAdapter() {
                @Override
                public void mouseDragged(MouseEvent e) {
                    handleDrag(e);
                }
            });
        }

        void updateSize() {
            float maxBeat = 32;
            for (AutomationEditor.AutoPoint p : points) {
                maxBeat = Math.max(maxBeat, p.timeBeats + 8);
            }
            int width = VALUE_RULER_WIDTH + (int)(maxBeat * pixelsPerBeat) + 50;
            int height = 400;
            setPreferredSize(new Dimension(width, height));
            revalidate();
        }

        private int beatToX(float beats) {
            return VALUE_RULER_WIDTH + (int)(beats * pixelsPerBeat);
        }
        private float xToBeat(int x) {
            return Math.max(0, (x - VALUE_RULER_WIDTH) / pixelsPerBeat);
        }
        private int valueToY(float val) {
            int drawH = getHeight() - BEAT_RULER_HEIGHT - 2 * PAD;
            return BEAT_RULER_HEIGHT + PAD + drawH - (int)(val * drawH);
        }
        private float yToValue(int y) {
            int drawH = getHeight() - BEAT_RULER_HEIGHT - 2 * PAD;
            float val = 1.0f - (float)(y - BEAT_RULER_HEIGHT - PAD) / drawH;
            return Math.max(0, Math.min(1, val));
        }
        private float snapBeat(float beat, boolean shiftHeld) {
            if (shiftHeld) return beat;
            float snap = 0.25f; // default: 16th note
            return Math.round(beat / snap) * snap;
        }

        private void handlePress(MouseEvent e) {
            if (SwingUtilities.isRightMouseButton(e)) {
                // Delete nearest point
                int idx = findPointAt(e.getX(), e.getY(), 10);
                if (idx >= 0) {
                    points.remove(idx);
                    sendUpdate();
                    repaint();
                }
                return;
            }

            // Check tension handle first
            int thIdx = findTensionHandleAt(e.getX(), e.getY(), 8);
            if (thIdx >= 0) {
                tensionDragIdx = thIdx;
                dragIdx = -1;
                return;
            }

            // Check existing point
            int idx = findPointAt(e.getX(), e.getY(), 8);
            if (idx >= 0) {
                dragIdx = idx;
                tensionDragIdx = -1;
            } else {
                // Add new point
                float beat = snapBeat(xToBeat(e.getX()), e.isShiftDown());
                float val = yToValue(e.getY());
                points.add(new AutomationEditor.AutoPoint(beat, val, 0.0f));
                points.sort(Comparator.comparingDouble(a -> a.timeBeats));
                for (int i = 0; i < points.size(); i++) {
                    if (Math.abs(points.get(i).timeBeats - beat) < 0.001f
                            && Math.abs(points.get(i).value - val) < 0.001f) {
                        dragIdx = i;
                        break;
                    }
                }
                repaint();
            }
        }

        private void handleDrag(MouseEvent e) {
            if (tensionDragIdx >= 0 && tensionDragIdx < points.size() - 1) {
                AutomationEditor.AutoPoint p0 = points.get(tensionDragIdx);
                AutomationEditor.AutoPoint p1 = points.get(tensionDragIdx + 1);
                float midVal = (p0.value + p1.value) / 2f;
                float dragVal = yToValue(e.getY());
                float diff = midVal - dragVal;
                p0.tension = Math.max(-1f, Math.min(1f, diff * 4f));
                repaint();
            } else if (dragIdx >= 0 && dragIdx < points.size()) {
                AutomationEditor.AutoPoint p = points.get(dragIdx);
                p.timeBeats = Math.max(0, snapBeat(xToBeat(e.getX()), e.isShiftDown()));
                p.value = yToValue(e.getY());
                repaint();
            }
        }

        private int findPointAt(int mx, int my, int threshold) {
            for (int i = 0; i < points.size(); i++) {
                AutomationEditor.AutoPoint p = points.get(i);
                if (Math.abs(mx - beatToX(p.timeBeats)) < threshold
                        && Math.abs(my - valueToY(p.value)) < threshold) {
                    return i;
                }
            }
            return -1;
        }

        private int findTensionHandleAt(int mx, int my, int threshold) {
            for (int i = 0; i < points.size() - 1; i++) {
                AutomationEditor.AutoPoint p0 = points.get(i);
                AutomationEditor.AutoPoint p1 = points.get(i + 1);
                float midX = (beatToX(p0.timeBeats) + beatToX(p1.timeBeats)) / 2f;
                float t = 0.5f;
                float exp = (float) Math.pow(2.0, p0.tension);
                float ct = (float) Math.pow(t, exp);
                float midVal = p0.value + (p1.value - p0.value) * ct;
                float midY = valueToY(midVal);
                if (Math.abs(mx - midX) < threshold && Math.abs(my - midY) < threshold) {
                    return i;
                }
            }
            return -1;
        }

        @Override
        protected void paintComponent(Graphics g) {
            super.paintComponent(g);
            Graphics2D g2 = (Graphics2D) g;
            g2.setRenderingHint(RenderingHints.KEY_ANTIALIASING, RenderingHints.VALUE_ANTIALIAS_ON);
            int w = getWidth();
            int h = getHeight();
            int drawH = h - BEAT_RULER_HEIGHT - 2 * PAD;

            // Beat ruler
            g2.setColor(Theme.getInstance().BG_DARKER);
            g2.fillRect(0, 0, w, BEAT_RULER_HEIGHT);
            g2.setColor(Theme.getInstance().TEXT_DIM);
            g2.setFont(Theme.getInstance().FONT_UI.deriveFont(10.0f));
            for (int beat = 0; beatToX(beat) < w; beat++) {
                int x = beatToX(beat);
                boolean isBar = (beat % 4 == 0);
                g2.setColor(isBar ? Theme.getInstance().TEXT_BRIGHT : Theme.getInstance().TEXT_DIM);
                g2.drawLine(x, BEAT_RULER_HEIGHT - (isBar ? 12 : 6), x, BEAT_RULER_HEIGHT);
                if (isBar) {
                    g2.drawString(String.valueOf(beat / 4 + 1), x + 2, BEAT_RULER_HEIGHT - 14);
                }
                // Grid line
                g2.setColor(new Color(255, 255, 255, isBar ? 30 : 12));
                g2.drawLine(x, BEAT_RULER_HEIGHT, x, h);
            }

            // Value ruler
            g2.setColor(Theme.getInstance().BG_DARKER);
            g2.fillRect(0, BEAT_RULER_HEIGHT, VALUE_RULER_WIDTH, drawH + 2 * PAD);
            g2.setFont(Theme.getInstance().FONT_UI.deriveFont(9.0f));
            for (int pct = 0; pct <= 100; pct += 25) {
                float val = pct / 100f;
                int y = valueToY(val);
                g2.setColor(Theme.getInstance().TEXT_DIM);
                g2.drawString(pct + "%", 5, y + 4);
                g2.setColor(new Color(255, 255, 255, 20));
                g2.drawLine(VALUE_RULER_WIDTH, y, w, y);
            }

            if (points.isEmpty()) return;

            // Draw curve segments
            g2.setColor(Theme.getInstance().ACCENT_ORANGE);
            g2.setStroke(new BasicStroke(2.0f));
            for (int i = 0; i < points.size() - 1; i++) {
                AutomationEditor.AutoPoint p0 = points.get(i);
                AutomationEditor.AutoPoint p1 = points.get(i + 1);
                int px0 = beatToX(p0.timeBeats);
                int px1 = beatToX(p1.timeBeats);
                int py0 = valueToY(p0.value);
                int py1 = valueToY(p1.value);
                int steps = Math.max(4, Math.abs(px1 - px0) / 2);
                int prevX = px0, prevY2 = py0;
                for (int s = 1; s <= steps; s++) {
                    float t = (float) s / steps;
                    float ct = (float) Math.pow(t, Math.pow(2, p0.tension));
                    float val = p0.value + (p1.value - p0.value) * ct;
                    int cx = px0 + (int)((px1 - px0) * t);
                    int cy = valueToY(val);
                    g2.drawLine(prevX, prevY2, cx, cy);
                    prevX = cx;
                    prevY2 = cy;
                }
            }

            // Draw points
            g2.setStroke(new BasicStroke(1.0f));
            for (int i = 0; i < points.size(); i++) {
                AutomationEditor.AutoPoint p = points.get(i);
                int px = beatToX(p.timeBeats);
                int py = valueToY(p.value);
                boolean isDragged = (i == dragIdx);
                g2.setColor(isDragged ? Theme.getInstance().ACCENT_BLUE : Color.WHITE);
                g2.fillOval(px - 5, py - 5, 10, 10);
                g2.setColor(Theme.getInstance().ACCENT_ORANGE);
                g2.drawOval(px - 5, py - 5, 10, 10);
            }

            // Draw tension handles
            for (int i = 0; i < points.size() - 1; i++) {
                AutomationEditor.AutoPoint p0 = points.get(i);
                AutomationEditor.AutoPoint p1 = points.get(i + 1);
                float midBeat = (p0.timeBeats + p1.timeBeats) / 2f;
                float t = 0.5f;
                float exp = (float) Math.pow(2.0, p0.tension);
                float ct = (float) Math.pow(t, exp);
                float midVal = p0.value + (p1.value - p0.value) * ct;
                int mx = beatToX(midBeat);
                int my = valueToY(midVal);
                boolean isDragged = (i == tensionDragIdx);
                g2.setColor(isDragged ? new Color(0, 255, 255) : new Color(0, 200, 220, 200));
                g2.fillOval(mx - 4, my - 4, 8, 8);
                g2.setColor(new Color(0, 220, 240));
                g2.drawOval(mx - 4, my - 4, 8, 8);
            }
        }
    }
}
