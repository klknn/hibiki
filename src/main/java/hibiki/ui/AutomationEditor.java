package hibiki.ui;

import javax.swing.*;
import java.awt.*;
import java.awt.event.*;
import java.awt.geom.*;
import java.util.*;
import java.util.List;

import hibiki.BackendManager;
import hibiki.pb.commands.*;
import hibiki.pb.core.EntityRef;
import hibiki.pb.core.Clip;
import hibiki.pb.notifications.*;
import hibiki.pb.core.*;

/**
 * AutomationEditor — renders and edits automation curves for a single lane.
 * Used as collapsible sub-rows below tracks in the TimelineView.
 *
 * FL Studio-inspired:
 *  - Click on empty area to add a control point
 *  - Drag points to move them (time + value)
 *  - Right-click a point to delete it or adjust tension
 *  - Curve is rendered with tension-based interpolation
 */
public class AutomationEditor extends JPanel {

    // Data model
    private int trackIndex;
    private int laneIndex;
    private int pluginIndex;
    private long paramId;
    private String paramName = "Parameter";
    private final List<AutoPoint> points = new ArrayList<>();

    // View state
    private float pixelsPerBeat = 40.0f;
    private float scrollOffsetBeats = 0.0f;
    private int headerWidth = 120;  // Track label area
    private int dragIdx = -1;
    private boolean isDragging = false;

    // Colors
    private static final Color BG_COLOR = new Color(28, 28, 32);
    private static final Color GRID_COLOR = new Color(50, 50, 55);
    private static final Color CURVE_COLOR = new Color(0, 180, 255);
    private static final Color CURVE_FILL_COLOR = new Color(0, 180, 255, 30);
    private static final Color POINT_COLOR = new Color(255, 200, 60);
    private static final Color POINT_HOVER_COLOR = new Color(255, 255, 120);
    private static final Color HEADER_BG = new Color(38, 38, 45);
    private static final Color LABEL_COLOR = new Color(180, 180, 190);

    static class AutoPoint {
        float timeBeats;
        float value;      // 0.0 – 1.0
        float tension;    // -1..1
        AutoPoint(float t, float v, float tension) {
            this.timeBeats = t; this.value = v; this.tension = tension;
        }
    }

    public AutomationEditor(int trackIndex, int laneIndex, int pluginIndex, long paramId, String paramName) {
        this.trackIndex = trackIndex;
        this.laneIndex = laneIndex;
        this.pluginIndex = pluginIndex;
        this.paramId = paramId;
        this.paramName = paramName;
        setPreferredSize(new Dimension(800, 60));
        setMinimumSize(new Dimension(100, 40));

        MouseAdapter mouse = new MouseAdapter() {
            @Override
            public void mousePressed(MouseEvent e) { handleMousePress(e); }
            @Override
            public void mouseReleased(MouseEvent e) { handleMouseRelease(e); }
            @Override
            public void mouseDragged(MouseEvent e) { handleMouseDrag(e); }
        };
        addMouseListener(mouse);
        addMouseMotionListener(mouse);
    }

    public void setLaneData(int laneIndex, int pluginIndex, long paramId, String paramName, List<AutoPoint> pts) {
        this.laneIndex = laneIndex;
        this.pluginIndex = pluginIndex;
        this.paramId = paramId;
        this.paramName = paramName;
        this.points.clear();
        this.points.addAll(pts);
        repaint();
    }

    public void setViewParams(float pixelsPerBeat, float scrollOffsetBeats, int headerWidth) {
        this.pixelsPerBeat = pixelsPerBeat;
        this.scrollOffsetBeats = scrollOffsetBeats;
        this.headerWidth = headerWidth;
        repaint();
    }

    // --- Coordinate conversion ---
    private float beatToX(float beat) {
        return headerWidth + (beat - scrollOffsetBeats) * pixelsPerBeat;
    }
    private float xToBeat(float x) {
        return (x - headerWidth) / pixelsPerBeat + scrollOffsetBeats;
    }
    private float valueToY(float value) {
        int h = getHeight();
        int pad = 4;
        return pad + (1.0f - value) * (h - 2 * pad);
    }
    private float yToValue(float y) {
        int h = getHeight();
        int pad = 4;
        return Math.max(0, Math.min(1, 1.0f - (y - pad) / (h - 2 * pad)));
    }

    // --- Find nearest point ---
    private int findPointAt(int mx, int my, int threshold) {
        for (int i = 0; i < points.size(); i++) {
            AutoPoint p = points.get(i);
            float px = beatToX(p.timeBeats);
            float py = valueToY(p.value);
            if (Math.abs(mx - px) < threshold && Math.abs(my - py) < threshold) {
                return i;
            }
        }
        return -1;
    }

    // --- Mouse handlers ---
    private void handleMousePress(MouseEvent e) {
        if (e.getX() < headerWidth) return;

        if (SwingUtilities.isRightMouseButton(e)) {
            int idx = findPointAt(e.getX(), e.getY(), 8);
            if (idx >= 0) {
                showPointContextMenu(e, idx);
            }
            return;
        }

        int idx = findPointAt(e.getX(), e.getY(), 8);
        if (idx >= 0) {
            dragIdx = idx;
            isDragging = true;
        } else {
            // Add new point
            float beat = xToBeat(e.getX());
            float val = yToValue(e.getY());
            if (beat >= 0) {
                AutoPoint np = new AutoPoint(beat, val, 0.0f);
                points.add(np);
                points.sort(Comparator.comparingDouble(a -> a.timeBeats));
                dragIdx = points.indexOf(np);
                isDragging = true;
                repaint();
            }
        }
    }

    private void handleMouseDrag(MouseEvent e) {
        if (isDragging && dragIdx >= 0 && dragIdx < points.size()) {
            AutoPoint p = points.get(dragIdx);
            p.timeBeats = Math.max(0, xToBeat(e.getX()));
            p.value = yToValue(e.getY());
            repaint();
        }
    }

    private void handleMouseRelease(MouseEvent e) {
        if (isDragging) {
            isDragging = false;
            dragIdx = -1;
            // Sort and send to backend
            points.sort(Comparator.comparingDouble(a -> a.timeBeats));
            sendUpdateToBackend();
            repaint();
        }
    }

    private void showPointContextMenu(MouseEvent e, int idx) {
        JPopupMenu menu = new JPopupMenu();
        JMenuItem deleteItem = new JMenuItem("Delete Point");
        deleteItem.addActionListener(ev -> {
            points.remove(idx);
            sendUpdateToBackend();
            repaint();
        });
        menu.add(deleteItem);

        // Tension submenu
        JMenu tensionMenu = new JMenu("Tension");
        float[] tensions = {-0.8f, -0.5f, -0.2f, 0.0f, 0.2f, 0.5f, 0.8f};
        String[] labels = {"Ease Out (strong)", "Ease Out", "Ease Out (light)", "Linear", "Ease In (light)", "Ease In", "Ease In (strong)"};
        for (int i = 0; i < tensions.length; i++) {
            float t = tensions[i];
            JMenuItem item = new JMenuItem(labels[i] + " (" + t + ")");
            item.addActionListener(ev -> {
                if (idx < points.size()) {
                    points.get(idx).tension = t;
                    sendUpdateToBackend();
                    repaint();
                }
            });
            tensionMenu.add(item);
        }
        menu.add(tensionMenu);
        menu.show(this, e.getX(), e.getY());
    }

    // --- Send data to backend ---
    private void sendUpdateToBackend() {
        AutomationCmd.Builder cmdBuilder = AutomationCmd.newBuilder().setAction(AutomationCmd.Action.ACTION_UPDATE_POINTS).setTarget(EntityRef.newBuilder().setTrackIndex(trackIndex).setLaneIndex(laneIndex));
        for (AutoPoint p : points) {
            cmdBuilder.addPoints(AutomationPoint.newBuilder()
                    .setTimeBeats(p.timeBeats)
                    .setValue(p.value)
                    .setTension(p.tension));
        }
        BackendManager.getInstance().sendRequest(Request.newBuilder()
                .setAutomation(cmdBuilder)
                .build());
    }

    // --- Tension interpolation (must match C++) ---
    private static float interpolate(float v0, float v1, float t, float tension) {
        if (t <= 0) return v0;
        if (t >= 1) return v1;
        float exponent = (float) Math.pow(2.0, tension);
        float curved_t = (float) Math.pow(t, exponent);
        return v0 + (v1 - v0) * curved_t;
    }

    // --- Paint ---
    @Override
    protected void paintComponent(Graphics g) {
        super.paintComponent(g);
        Graphics2D g2 = (Graphics2D) g.create();
        g2.setRenderingHint(RenderingHints.KEY_ANTIALIASING, RenderingHints.VALUE_ANTIALIAS_ON);
        int w = getWidth(), h = getHeight();

        // Background
        g2.setColor(BG_COLOR);
        g2.fillRect(0, 0, w, h);

        // Header
        g2.setColor(HEADER_BG);
        g2.fillRect(0, 0, headerWidth, h);
        g2.setColor(LABEL_COLOR);
        g2.setFont(new Font("SansSerif", Font.PLAIN, 10));
        String label = paramName;
        if (label.length() > 16) label = label.substring(0, 14) + "…";
        g2.drawString(label, 6, h / 2 + 4);

        // Beat grid lines
        g2.setColor(GRID_COLOR);
        float startBeat = Math.max(0, scrollOffsetBeats);
        float endBeat = xToBeat(w);
        for (float b = (float) Math.ceil(startBeat); b <= endBeat; b += 1.0f) {
            float x = beatToX(b);
            if (x > headerWidth && x < w) {
                g2.drawLine((int) x, 0, (int) x, h);
            }
        }

        // 0.5 value line (center)
        g2.setColor(new Color(60, 60, 65));
        float midY = valueToY(0.5f);
        g2.drawLine(headerWidth, (int) midY, w, (int) midY);

        // Draw curve
        if (points.size() >= 2) {
            // Fill area under curve
            GeneralPath fill = new GeneralPath();
            float firstX = Math.max(headerWidth, beatToX(points.get(0).timeBeats));
            fill.moveTo(firstX, h);

            // Curve path
            GeneralPath curve = new GeneralPath();
            float prevX = beatToX(points.get(0).timeBeats);
            float prevY = valueToY(points.get(0).value);
            curve.moveTo(prevX, prevY);
            fill.lineTo(prevX, prevY);

            for (int i = 1; i < points.size(); i++) {
                AutoPoint p0 = points.get(i - 1);
                AutoPoint p1 = points.get(i);
                float x0 = beatToX(p0.timeBeats);
                float x1 = beatToX(p1.timeBeats);
                int steps = Math.max(2, (int)((x1 - x0) / 3));
                for (int s = 1; s <= steps; s++) {
                    float t = (float) s / steps;
                    float val = interpolate(p0.value, p1.value, t, p0.tension);
                    float x = x0 + t * (x1 - x0);
                    float y = valueToY(val);
                    curve.lineTo(x, y);
                    fill.lineTo(x, y);
                }
            }
            float lastX = beatToX(points.get(points.size() - 1).timeBeats);
            fill.lineTo(lastX, h);
            fill.closePath();

            g2.setClip(headerWidth, 0, w - headerWidth, h);
            g2.setColor(CURVE_FILL_COLOR);
            g2.fill(fill);
            g2.setColor(CURVE_COLOR);
            g2.setStroke(new BasicStroke(1.5f));
            g2.draw(curve);
            g2.setClip(null);

            // Extend flat lines before first and after last point
            g2.setStroke(new BasicStroke(1.0f, BasicStroke.CAP_BUTT, BasicStroke.JOIN_BEVEL, 0, new float[]{4, 4}, 0));
            if (prevX > headerWidth) {
                g2.drawLine(headerWidth, (int) prevY, (int) prevX, (int) prevY);
            }
            float lastY = valueToY(points.get(points.size()-1).value);
            if (lastX < w) {
                g2.drawLine((int) lastX, (int) lastY, w, (int) lastY);
            }
        } else if (points.size() == 1) {
            float y = valueToY(points.get(0).value);
            g2.setColor(CURVE_COLOR);
            g2.setStroke(new BasicStroke(1.0f, BasicStroke.CAP_BUTT, BasicStroke.JOIN_BEVEL, 0, new float[]{4,4}, 0));
            g2.drawLine(headerWidth, (int) y, w, (int) y);
        }

        // Draw control points
        g2.setStroke(new BasicStroke(1.0f));
        for (int i = 0; i < points.size(); i++) {
            AutoPoint p = points.get(i);
            float px = beatToX(p.timeBeats);
            float py = valueToY(p.value);
            if (px < headerWidth || px > w) continue;
            g2.setColor(i == dragIdx ? POINT_HOVER_COLOR : POINT_COLOR);
            g2.fillOval((int)(px - 4), (int)(py - 4), 8, 8);
            g2.setColor(Color.BLACK);
            g2.drawOval((int)(px - 4), (int)(py - 4), 8, 8);
        }

        // Border
        g2.setColor(new Color(60, 60, 70));
        g2.drawLine(headerWidth, h - 1, w, h - 1);

        g2.dispose();
    }
}
