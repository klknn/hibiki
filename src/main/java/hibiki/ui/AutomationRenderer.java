package hibiki.ui;

import java.awt.*;
import java.awt.geom.*;
import java.util.List;

/**
 * Renders automation curves within timeline automation lanes.
 * Extracted from TimelineRenderer to reduce file size.
 */
class AutomationRenderer {

    /** Draw an automation curve inside its lane sub-row based on individual clips. */
    static void drawAutomationCurve(Graphics2D g2, TimelineView.AutomationLaneData lane,
            int y, int h, int xOff, float pps, float secPerBeat) {
        int pad = TimelineConstants.AUTOMATION_PAD;
        int headerH = TimelineConstants.CLIP_HEADER_HEIGHT;
        int drawH = h - pad * 2 - headerH;
        int drawY = y + pad + headerH;

        // Draw 0.0 and 1.0 reference lines (below header)
        g2.setColor(new Color(255, 255, 255, 20));
        g2.drawLine(0, drawY, (int) (600 * pps), drawY); // 1.0
        g2.drawLine(0, drawY + drawH, (int) (600 * pps), drawY + drawH); // 0.0
        g2.setColor(new Color(255, 255, 255, 12));
        g2.drawLine(0, drawY + drawH / 2, (int) (600 * pps), drawY + drawH / 2); // 0.5

        if (lane.clips == null || lane.clips.isEmpty())
            return;

        Color accentOrange = Theme.getInstance().ACCENT_ORANGE;
        Color topColor = TimelineConstants.withAlpha(accentOrange, 100);
        Color bottomColor = TimelineConstants.darkened(accentOrange, 100);

        for (TimelineView.ClipRect cr : lane.clips) {
            int startPx = xOff + (int) (cr.startTime * pps);
            float durationSec = cr.duration * secPerBeat;
            int widthPx = (int) (durationSec * pps);
            int endPx = startPx + widthPx;

            int clipTopY = drawY - headerH;

            // Draw clip background area (faint orange, transparent content)
            g2.setColor(TimelineConstants.withAlpha(accentOrange, 20));
            g2.fillRect(startPx, clipTopY, widthPx, drawH + headerH);
            g2.setColor(TimelineConstants.withAlpha(accentOrange, 60));
            g2.drawRect(startPx, clipTopY, widthPx, drawH + headerH);

            // Draw clip header
            g2.setColor(TimelineConstants.withAlpha(accentOrange, 120));
            g2.fillRect(startPx, clipTopY, widthPx, headerH);

            // Draw clip name
            g2.setColor(Color.WHITE);
            g2.setFont(new Font("SansSerif", Font.PLAIN, 10));
            String displayName = (cr.name != null && !cr.name.isEmpty()) ? cr.name : "Automation";
            if (widthPx > 20) {
                Shape oldClip = g2.getClip();
                g2.clipRect(startPx, clipTopY, widthPx, headerH);
                g2.drawString(displayName, startPx + 4, clipTopY + 11);
                g2.setClip(oldClip);
            }

            List<AutomationEditor.AutoPoint> points = cr.automationPoints;
            if (points == null || points.isEmpty()) continue;

            GeneralPath curvePath = new GeneralPath();
            GeneralPath fillPath = new GeneralPath();
            int bottomY = drawY + drawH;

            // First point position (relative to clip start)
            AutomationEditor.AutoPoint firstPt = points.get(0);
            float firstXsec = cr.startTime + (firstPt.timeBeats * secPerBeat);
            int firstPx = xOff + (int) (firstXsec * pps);
            int firstPy = drawY + drawH - (int) (firstPt.value * drawH);

            // Last point position
            AutomationEditor.AutoPoint lastPt = points.get(points.size() - 1);
            float lastXsec = cr.startTime + (lastPt.timeBeats * secPerBeat);
            int lastPx = xOff + (int) (lastXsec * pps);
            int lastPy = drawY + drawH - (int) (lastPt.value * drawH);

            fillPath.moveTo(startPx, bottomY);
            fillPath.lineTo(startPx, firstPy);
            if (firstPx > startPx) {
                fillPath.lineTo(firstPx, firstPy);
            }

            curvePath.moveTo(firstPx, firstPy);

            for (int i = 0; i < points.size() - 1; i++) {
                AutomationEditor.AutoPoint p0 = points.get(i);
                AutomationEditor.AutoPoint p1 = points.get(i + 1);
                float x0sec = cr.startTime + (p0.timeBeats * secPerBeat);
                float x1sec = cr.startTime + (p1.timeBeats * secPerBeat);
                int px0 = xOff + (int) (x0sec * pps);
                int px1 = xOff + (int) (x1sec * pps);
                int steps = Math.max(4, Math.abs(px1 - px0) / 2);
                for (int s = 1; s <= steps; s++) {
                    float t = (float) s / steps;
                    float ct = (float) Math.pow(t, Math.pow(2, p0.tension));
                    float val = p0.value + (p1.value - p0.value) * ct;
                    int cx = px0 + (int) ((px1 - px0) * t);
                    int cy = drawY + drawH - (int) (val * drawH);
                    curvePath.lineTo(cx, cy);
                    fillPath.lineTo(cx, cy);
                }
            }

            if (lastPx < endPx) {
                fillPath.lineTo(endPx, lastPy);
            }
            fillPath.lineTo(endPx, bottomY);
            fillPath.closePath();

            g2.setPaint(new GradientPaint(0, drawY, topColor, 0, bottomY, bottomColor));
            g2.fill(fillPath);

            g2.setColor(accentOrange);
            g2.setStroke(new BasicStroke(1.5f));
            g2.draw(curvePath);

            // Draw points
            g2.setStroke(new BasicStroke(1.0f));
            for (AutomationEditor.AutoPoint p : points) {
                float pxSec = cr.startTime + (p.timeBeats * secPerBeat);
                int ptX = xOff + (int) (pxSec * pps);
                int ptY = drawY + drawH - (int) (p.value * drawH);
                g2.setColor(Color.WHITE);
                g2.fillOval(ptX - 3, ptY - 3, 6, 6);
                g2.setColor(accentOrange);
                g2.drawOval(ptX - 3, ptY - 3, 6, 6);
            }
        }
    }
}
