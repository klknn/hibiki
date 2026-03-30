package hibiki.ui;

import java.awt.*;
import java.awt.geom.*;
import javax.swing.JPanel;
import java.util.List;

/**
 * Handles all rendering for the TimelineView: track backgrounds, grid lines,
 * clip rectangles (with MIDI/audio waveform previews), automation lanes,
 * drag ghosts, and the playhead.
 */
class TimelineRenderer {
    private final TimelineView view;

    TimelineRenderer(TimelineView view) {
        this.view = view;
    }

    void drawTrackLabels(Graphics g, List<TimelineView.TrackTimeline> tracks, int selectedTrack,
                         int trackHeight, int timeRulerHeight, int labelWidth) {
        Graphics2D g2 = (Graphics2D) g;
        g2.setRenderingHint(RenderingHints.KEY_ANTIALIASING, RenderingHints.VALUE_ANTIALIAS_ON);

        int scaleTimeRuler = Theme.getInstance().scale(timeRulerHeight);
        int scaleBaseTrack = Theme.getInstance().scale(view.getBaseTrackHeight());
        int scaleAutoLane = Theme.getInstance().scale(view.getAutomationLaneHeight());
        int scaleLabelWidth = Theme.getInstance().scale(labelWidth);

        g2.setColor(Theme.getInstance().BG_DARKER);
        g2.fillRect(0, 0, scaleLabelWidth, scaleTimeRuler);

        for (int i = 0; i < tracks.size(); i++) {
            int y = scaleTimeRuler + Theme.getInstance().scale(view.getTrackY(i));

            // Main track label
            if (i == selectedTrack) {
                g2.setColor(Theme.getInstance().ACCENT_BLUE.darker());
            } else {
                g2.setColor(Theme.getInstance().TRACK_HEADER);
            }
            g2.fillRect(0, y, scaleLabelWidth, scaleBaseTrack - 1);

            g2.setColor(Theme.getInstance().TEXT_BRIGHT);
            g2.setFont(Theme.getInstance().FONT_UI_BOLD);
            g2.drawString(tracks.get(i).getDisplayName(), 5, y + 16);

            TimelineView.TrackTimeline track = tracks.get(i);
            if (track.pluginName != null) {
                g2.setFont(Theme.getInstance().FONT_UI);
                g2.setColor(track.isInstrument ? Theme.getInstance().ACCENT_ORANGE : Theme.getInstance().TEXT_DIM);
                String pname = track.pluginName;
                if (pname.length() > 12) pname = pname.substring(0, 11) + "…";
                g2.drawString(pname, 5, y + 32);
            } else {
                g2.setFont(Theme.getInstance().FONT_UI);
                g2.setColor(Theme.getInstance().TEXT_DIM);
                g2.drawString("(no plugin)", 5, y + 32);
            }

            // Automation expand/collapse indicator
            if (!track.automationLanes.isEmpty()) {
                String toggleSymbol = track.automationExpanded ? "▼" : "▶";
                g2.setColor(Theme.getInstance().ACCENT_BLUE);
                g2.setFont(Theme.getInstance().FONT_UI.deriveFont(Theme.getInstance().scale(9.0f)));
                g2.drawString(toggleSymbol + " Auto (" + track.automationLanes.size() + ")",
                        5, y + scaleBaseTrack - 6);
            }

            g2.setColor(Theme.getInstance().BORDER);
            g2.drawLine(0, y + scaleBaseTrack - 1, scaleLabelWidth, y + scaleBaseTrack - 1);

            // Draw automation lane labels when expanded
            if (track.automationExpanded) {
                for (int j = 0; j < track.automationLanes.size(); j++) {
                    int autoY = y + scaleBaseTrack + j * scaleAutoLane;
                    g2.setColor(Theme.getInstance().BG_DARKER.brighter());
                    g2.fillRect(0, autoY, scaleLabelWidth, scaleAutoLane - 1);

                    TimelineView.AutomationLaneData lane = track.automationLanes.get(j);
                    g2.setColor(Theme.getInstance().ACCENT_ORANGE);
                    g2.setFont(Theme.getInstance().FONT_UI.deriveFont(Theme.getInstance().scale(9.0f)));
                    String lName = lane.paramName;
                    if (lName.length() > 14)
                        lName = lName.substring(0, 13) + "…";
                    g2.drawString(lName, 5, autoY + scaleAutoLane / 2 + 4);

                    g2.setColor(Theme.getInstance().BORDER);
                    g2.drawLine(0, autoY + scaleAutoLane - 1, scaleLabelWidth, autoY + scaleAutoLane - 1);
                }
            }
        }
    }

    void drawTimeline(Graphics g, JPanel contentPanel, List<TimelineView.TrackTimeline> tracks,
                      int selectedTrack, float bpm, GridMode gridMode,
                      float playheadPos, boolean isDragging, TimelineView.ClipRect draggingClip,
                      int dragSourceTrack, float dragOriginalStartTime, int dragCurrentY,
                      boolean creatingClip, int creatingTrackIdx, TimelineView.ClipRect creatingClipRect,
                      int creatingAutoLaneIdx, int trackHeight, int timeRulerHeight) {
        Graphics2D g2 = (Graphics2D) g;
        g2.setRenderingHint(RenderingHints.KEY_ANTIALIASING, RenderingHints.VALUE_ANTIALIAS_ON);

        int scaleTimeRuler = Theme.getInstance().scale(timeRulerHeight);
        int scaleBaseTrack = Theme.getInstance().scale(view.getBaseTrackHeight());
        int scaleAutoLane = Theme.getInstance().scale(view.getAutomationLaneHeight());
        int scaleLabelWidth = 0;
        float pps = view.getPixelsPerSecond();
        float secondsPerBeat = 60.0f / bpm;

        // Draw tracks background
        for (int i = 0; i < tracks.size(); i++) {
            int y = scaleTimeRuler + Theme.getInstance().scale(view.getTrackY(i));
            // Main clip area
            if (i == selectedTrack) {
                g2.setColor(Theme.getInstance().ACCENT_BLUE.darker().darker());
            } else {
                g2.setColor(i % 2 == 0 ? Theme.getInstance().BG_DARK : Theme.getInstance().BG_DARKER);
            }
            g2.fillRect(0, y, contentPanel.getWidth(), scaleBaseTrack);
            g2.setColor(Theme.getInstance().PANEL_BG_LIGHT.darker());
            g2.drawLine(0, y + scaleBaseTrack - 1, contentPanel.getWidth(), y + scaleBaseTrack - 1);

            // Automation lane backgrounds (when expanded)
            TimelineView.TrackTimeline track = tracks.get(i);
            if (track.automationExpanded) {
                for (int j = 0; j < track.automationLanes.size(); j++) {
                    int autoY = y + scaleBaseTrack + j * scaleAutoLane;
                    g2.setColor(new Color(30, 30, 45));
                    g2.fillRect(0, autoY, contentPanel.getWidth(), scaleAutoLane);
                    g2.setColor(Theme.getInstance().BORDER);
                    g2.drawLine(0, autoY + scaleAutoLane - 1, contentPanel.getWidth(), autoY + scaleAutoLane - 1);
                }
            }
        }

        // Draw grid lines
        int trackAreaBottom = scaleTimeRuler + Theme.getInstance().scale(view.getTotalTracksHeight());
        float secondsPerBar = secondsPerBeat * 4;
        float gridSeconds = view.getGridSnapSeconds(gridMode, secondsPerBeat);

        drawGridLines(g2, contentPanel, scaleTimeRuler, trackAreaBottom, scaleLabelWidth,
                pps, gridSeconds, secondsPerBeat, secondsPerBar);

        // Draw ghost shadow of dragged clip
        if (isDragging && draggingClip != null && dragSourceTrack >= 0) {
            int ghostY = scaleTimeRuler + Theme.getInstance().scale(view.getTrackY(dragSourceTrack));
            drawDragGhost(g2, ghostY, scaleBaseTrack, scaleLabelWidth, pps,
                    dragOriginalStartTime, draggingClip.duration);
        }

        // Draw clips
        drawClips(g2, tracks, scaleTimeRuler, scaleBaseTrack, scaleLabelWidth, pps,
                isDragging, draggingClip);

        // Draw automation curves (when expanded)
        for (int i = 0; i < tracks.size(); i++) {
            TimelineView.TrackTimeline track = tracks.get(i);
            if (!track.automationExpanded || track.automationLanes.isEmpty())
                continue;
            int trackY = scaleTimeRuler + Theme.getInstance().scale(view.getTrackY(i));
            for (int j = 0; j < track.automationLanes.size(); j++) {
                int autoY = trackY + scaleBaseTrack + j * scaleAutoLane;
                drawAutomationCurve(g2, track.automationLanes.get(j), autoY, scaleAutoLane,
                        scaleLabelWidth, pps, secondsPerBeat);
            }
        }

        // Draw dragging clip at cursor position
        if (isDragging && draggingClip != null) {
            int targetTrackIdx = getTrackAtY(dragCurrentY - scaleTimeRuler, tracks);
            int targetY = scaleTimeRuler + Theme.getInstance().scale(view.getTrackY(targetTrackIdx));
            drawClipAt(g2, draggingClip, targetY + 5,
                    scaleLabelWidth, pps, scaleBaseTrack, 0.8f, Theme.getInstance().ACCENT_BLUE.brighter());
        }

        // Draw clip being created
        if (creatingClip && creatingClipRect != null && creatingClipRect.duration > 0) {
            int x = scaleLabelWidth + (int) (creatingClipRect.startTime * pps);
            int w = (int) (creatingClipRect.duration * pps);
            if (creatingAutoLaneIdx >= 0) {
                int y = scaleTimeRuler + Theme.getInstance().scale(view.getTrackY(creatingTrackIdx)) 
                        + scaleBaseTrack + creatingAutoLaneIdx * scaleAutoLane + 4;
                int h = scaleAutoLane - 8;
                g2.setComposite(AlphaComposite.getInstance(AlphaComposite.SRC_OVER, 0.6f));
                g2.setColor(Theme.getInstance().ACCENT_ORANGE.brighter());
                g2.fillRect(x, y, w, h);
                g2.setColor(Color.WHITE);
                g2.drawRect(x, y, w, h);
            } else {
                int y = scaleTimeRuler + Theme.getInstance().scale(view.getTrackY(creatingTrackIdx)) + 5;
                int h = scaleBaseTrack - 10;
                g2.setComposite(AlphaComposite.getInstance(AlphaComposite.SRC_OVER, 0.6f));
                g2.setColor(new Color(100, 200, 100));
                g2.fillRoundRect(x, y, w, h, 8, 8);
                g2.setColor(new Color(150, 255, 150));
                g2.drawRoundRect(x, y, w, h, 8, 8);
                g2.setColor(Color.WHITE);
                g2.setFont(Theme.getInstance().FONT_UI.deriveFont(Font.BOLD, Theme.getInstance().scale(10.0f)));
                g2.drawString("New Clip", x + 5, y + 15);
            }
            g2.setComposite(AlphaComposite.SrcOver);
        }

        // Draw time ruler
        drawTimeRuler(g2, contentPanel, scaleTimeRuler, scaleLabelWidth, pps, bpm, gridMode);

        // Draw playhead
        int px = scaleLabelWidth + (int) (playheadPos * pps);
        g2.setColor(Color.RED);
        g2.setStroke(new BasicStroke(2.0f));
        g2.drawLine(px, 0, px, contentPanel.getHeight());
    }

    /** Draw an automation curve inside its lane sub-row based on individual clips. */
    private void drawAutomationCurve(Graphics2D g2, TimelineView.AutomationLaneData lane,
            int y, int h, int xOff, float pps, float secPerBeat) {
        int pad = 4;
        int headerH = 15;
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
        Color topColor = new Color(accentOrange.getRed(), accentOrange.getGreen(), accentOrange.getBlue(), 100);
        Color bottomColor = new Color(accentOrange.getRed() / 3, accentOrange.getGreen() / 3, accentOrange.getBlue() / 3, 100);

        for (TimelineView.ClipRect cr : lane.clips) {
            int startPx = xOff + (int) (cr.startTime * pps);
            float durationSec = cr.duration * secPerBeat;
            int widthPx = (int) (durationSec * pps);
            int endPx = startPx + widthPx;

            int clipTopY = drawY - headerH;

            // Draw clip background area (faint orange, transparent content)
            g2.setColor(new Color(accentOrange.getRed(), accentOrange.getGreen(), accentOrange.getBlue(), 20));
            g2.fillRect(startPx, clipTopY, widthPx, drawH + headerH);
            g2.setColor(new Color(accentOrange.getRed(), accentOrange.getGreen(), accentOrange.getBlue(), 60));
            g2.drawRect(startPx, clipTopY, widthPx, drawH + headerH);

            // Draw clip header
            g2.setColor(new Color(accentOrange.getRed(), accentOrange.getGreen(), accentOrange.getBlue(), 120));
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

    /** Find which track index corresponds to a given Y offset from time ruler. */
    private int getTrackAtY(int yFromRuler, List<TimelineView.TrackTimeline> tracks) {
        int cumY = 0;
        for (int i = 0; i < tracks.size(); i++) {
            int th = Theme.getInstance().scale(view.getTotalTrackHeight(i));
            if (yFromRuler < cumY + th)
                return i;
            cumY += th;
        }
        return Math.max(0, tracks.size() - 1);
    }

    private void drawGridLines(Graphics2D g2, JPanel contentPanel, int scaleTimeRuler,
                               int trackAreaBottom, int scaleLabelWidth, float pps,
                               float gridSeconds, float secondsPerBeat, float secondsPerBar) {
        if (gridSeconds > 0) {
            float gridWidth = gridSeconds * pps;
            if (gridWidth >= 2) {
                g2.setColor(new Color(255, 255, 255, 15));
                for (float t = 0; t * pps < contentPanel.getWidth(); t += gridSeconds) {
                    int x = scaleLabelWidth + (int) (t * pps);
                    g2.drawLine(x, scaleTimeRuler, x, trackAreaBottom);
                }
            }
        }
        float beatWidth = secondsPerBeat * pps;
        if (beatWidth >= 4 && gridSeconds < secondsPerBeat) {
            g2.setColor(new Color(255, 255, 255, 25));
            for (float t = 0; t * pps < contentPanel.getWidth(); t += secondsPerBeat) {
                int x = scaleLabelWidth + (int) (t * pps);
                g2.drawLine(x, scaleTimeRuler, x, trackAreaBottom);
            }
        }
        float barWidth = secondsPerBar * pps;
        if (barWidth >= 4) {
            g2.setColor(new Color(255, 255, 255, 40));
            for (float t = 0; t * pps < contentPanel.getWidth(); t += secondsPerBar) {
                int x = scaleLabelWidth + (int) (t * pps);
                g2.drawLine(x, scaleTimeRuler, x, trackAreaBottom);
            }
        }
    }

    private void drawDragGhost(Graphics2D g2, int ghostTrackY, int scaleBaseTrack,
            int scaleLabelWidth, float pps,
                               float originalStartTime, float duration) {
        int ghostY = ghostTrackY + 5;
        int ghostX = scaleLabelWidth + (int) (originalStartTime * pps);
        int ghostW = (int) (duration * pps);
        int ghostH = scaleBaseTrack - 10;

        Composite oldComposite = g2.getComposite();
        g2.setComposite(AlphaComposite.getInstance(AlphaComposite.SRC_OVER, 0.3f));
        g2.setColor(Theme.getInstance().ACCENT_BLUE.darker());
        g2.fillRoundRect(ghostX, ghostY, ghostW, ghostH, 8, 8);
        g2.setComposite(oldComposite);

        g2.setColor(Theme.getInstance().ACCENT_BLUE);
        Stroke oldStroke = g2.getStroke();
        g2.setStroke(new BasicStroke(1.5f, BasicStroke.CAP_BUTT, BasicStroke.JOIN_BEVEL, 0, new float[]{4, 4}, 0));
        g2.drawRoundRect(ghostX, ghostY, ghostW, ghostH, 8, 8);
        g2.setStroke(oldStroke);
    }

    private static final int CLIP_HEADER_H = 15;

    private void drawClips(Graphics2D g2, List<TimelineView.TrackTimeline> tracks,
            int scaleTimeRuler, int scaleBaseTrack, int scaleLabelWidth,
                           float pps, boolean isDragging, TimelineView.ClipRect draggingClip) {
        Color accentBlue = Theme.getInstance().ACCENT_BLUE;
        for (int i = 0; i < tracks.size(); i++) {
            int y = scaleTimeRuler + Theme.getInstance().scale(view.getTrackY(i)) + 5;
            for (TimelineView.ClipRect clip : tracks.get(i).clips) {
                if (isDragging && clip == draggingClip) continue;
                int x = scaleLabelWidth + (int) (clip.startTime * pps);
                int w = (int) (clip.duration * pps);
                int h = scaleBaseTrack - 10;

                // Transparent content background
                g2.setColor(new Color(accentBlue.getRed(), accentBlue.getGreen(), accentBlue.getBlue(), 30));
                g2.fillRoundRect(x, y, w, h, 8, 8);

                // Clip header
                Shape oldClip = g2.getClip();
                g2.clipRect(x, y, w, h);
                g2.setColor(new Color(accentBlue.getRed(), accentBlue.getGreen(), accentBlue.getBlue(), 140));
                g2.fillRect(x, y, w, CLIP_HEADER_H);
                g2.setClip(oldClip);

                // Clip name in header
                if (w > 20) {
                    oldClip = g2.getClip();
                    g2.clipRect(x, y, w, CLIP_HEADER_H);
                    g2.setColor(Color.WHITE);
                    g2.setFont(Theme.getInstance().FONT_UI.deriveFont(Font.BOLD, Theme.getInstance().scale(10.0f)));
                    g2.drawString(clip.name != null ? clip.name : "", x + 5, y + 11);
                    g2.setClip(oldClip);
                }

                // Content area (below header)
                int contentY = y + CLIP_HEADER_H;
                int contentH = h - CLIP_HEADER_H;

                boolean isMidi = clip.path == null || clip.path.isEmpty()
                        || clip.path.toLowerCase().endsWith(".mid")
                        || clip.path.toLowerCase().endsWith(".midi");

                if (isMidi) {
                    drawMidiPreview(g2, clip, x, contentY, w, contentH);
                } else {
                    drawAudioWaveform(g2, clip, x, contentY, w, contentH);
                }

                // Border
                g2.setColor(accentBlue);
                g2.drawRoundRect(x, y, w, h, 8, 8);
            }
        }
    }

    private void drawMidiPreview(Graphics2D g2, TimelineView.ClipRect clip, int x, int y, int w, int h) {
        if (clip.waveform == null || clip.waveform.length == 0) return;
        g2.setColor(new Color(255, 255, 255, 200));
        Shape oldClip = g2.getClip();
        g2.clipRect(x, y, w, h);
        for (int nIdx = 0; nIdx + 2 < clip.waveform.length; nIdx += 3) {
            float startRatio = clip.waveform[nIdx];
            float pitch = clip.waveform[nIdx + 1];
            float durationRatio = clip.waveform[nIdx + 2];
            int nx = x + (int) (startRatio * w);
            int nw = (int) (durationRatio * w);
            if (nw < 2) nw = 2;
            int minPitch = 21, maxPitch = 108;
            float normalizedPitch = (pitch - minPitch) / (float) (maxPitch - minPitch);
            normalizedPitch = Math.max(0, Math.min(1, normalizedPitch));
            int nh = Math.max(2, h / 40);
            int ny = y + h - (int) (normalizedPitch * (h - nh)) - nh;
            if (nx < x + w && nx + nw >= x) {
                int drawX = Math.max(x, nx);
                int drawW = Math.min(x + w - drawX, nx + nw - drawX);
                g2.fillRect(drawX, ny, drawW, nh);
            }
        }
        g2.setClip(oldClip);
    }

    private void drawAudioWaveform(Graphics2D g2, TimelineView.ClipRect clip, int x, int y, int w, int h) {
        if (clip.waveform == null || clip.waveform.length == 0) return;
        g2.setColor(new Color(255, 255, 255, 120));
        Shape oldClip = g2.getClip();
        g2.clipRect(x, y, w, h);
        int midY = y + h / 2;
        int halfH = h / 2 - 4;
        for (int px = 0; px < w; px++) {
            int wfIdx = (int) ((float) px / w * clip.waveform.length);
            if (wfIdx >= clip.waveform.length) wfIdx = clip.waveform.length - 1;
            float amp = clip.waveform[wfIdx];
            int barH = (int) (amp * halfH);
            g2.drawLine(x + px, midY - barH, x + px, midY + barH);
        }
        g2.setClip(oldClip);
    }

    private void drawClipAt(Graphics2D g2, TimelineView.ClipRect clip, int y, int scaleLabelWidth,
                            float pps, int scaleTrackHeight, float alpha, Color borderColor) {
        int x = scaleLabelWidth + (int) (clip.startTime * pps);
        int w = (int) (clip.duration * pps);
        int h = scaleTrackHeight - 10;
        Color accentBlue = Theme.getInstance().ACCENT_BLUE;
        g2.setComposite(AlphaComposite.getInstance(AlphaComposite.SRC_OVER, alpha));

        // Transparent body
        g2.setColor(new Color(accentBlue.getRed(), accentBlue.getGreen(), accentBlue.getBlue(), 30));
        g2.fillRoundRect(x, y, w, h, 8, 8);

        // Header
        Shape oldClip = g2.getClip();
        g2.clipRect(x, y, w, h);
        g2.setColor(new Color(accentBlue.getRed(), accentBlue.getGreen(), accentBlue.getBlue(), 140));
        g2.fillRect(x, y, w, CLIP_HEADER_H);
        g2.setClip(oldClip);

        // Name
        if (w > 20) {
            oldClip = g2.getClip();
            g2.clipRect(x, y, w, CLIP_HEADER_H);
            g2.setColor(Color.WHITE);
            g2.setFont(Theme.getInstance().FONT_UI.deriveFont(Font.BOLD, Theme.getInstance().scale(10.0f)));
            g2.drawString(clip.name != null ? clip.name : "", x + 5, y + 11);
            g2.setClip(oldClip);
        }

        // Border
        g2.setColor(borderColor);
        g2.drawRoundRect(x, y, w, h, 8, 8);
        g2.setComposite(AlphaComposite.SrcOver);
    }

    private void drawTimeRuler(Graphics2D g2, JPanel contentPanel, int scaleTimeRuler,
                               int scaleLabelWidth, float pps, float bpm, GridMode gridMode) {
        g2.setColor(Theme.getInstance().BG_DARKER);
        g2.fillRect(scaleLabelWidth, 0, contentPanel.getWidth() - scaleLabelWidth, scaleTimeRuler);
        g2.setColor(Theme.getInstance().TEXT_DIM);

        if (gridMode == GridMode.SECONDS) {
            for (int s = 0; s < 600; s += 5) {
                int x = scaleLabelWidth + (int) (s * pps);
                if (x > contentPanel.getWidth()) break;
                g2.drawLine(x, scaleTimeRuler - 10, x, scaleTimeRuler);
                g2.drawString(s + "s", x + 2, scaleTimeRuler - 12);
            }
        } else {
            float rulerSecondsPerBar = (60.0f / bpm) * 4;
            for (int bar = 0; bar < 200; bar++) {
                int x = scaleLabelWidth + (int) (bar * rulerSecondsPerBar * pps);
                if (x > contentPanel.getWidth()) break;
                g2.drawLine(x, scaleTimeRuler - 10, x, scaleTimeRuler);
                g2.drawString(String.valueOf(bar + 1), x + 3, scaleTimeRuler - 12);
            }
        }
    }
}
