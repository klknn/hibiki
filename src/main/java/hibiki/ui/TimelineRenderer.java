package hibiki.ui;

import java.awt.*;
import javax.swing.JPanel;
import java.util.List;

/**
 * Handles all rendering for the TimelineView: track backgrounds, grid lines,
 * clip rectangles (with MIDI/audio waveform previews), drag ghosts, and the playhead.
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
        int scaleTrackHeight = Theme.getInstance().scale(trackHeight);
        int scaleLabelWidth = Theme.getInstance().scale(labelWidth);

        g2.setColor(Theme.getInstance().BG_DARKER);
        g2.fillRect(0, 0, scaleLabelWidth, scaleTimeRuler);

        for (int i = 0; i < tracks.size(); i++) {
            int y = scaleTimeRuler + i * scaleTrackHeight;

            if (i == selectedTrack) {
                g2.setColor(Theme.getInstance().ACCENT_BLUE.darker());
            } else {
                g2.setColor(Theme.getInstance().TRACK_HEADER);
            }
            g2.fillRect(0, y, scaleLabelWidth, scaleTrackHeight - 1);

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

            g2.setColor(Theme.getInstance().BORDER);
            g2.drawLine(0, y + scaleTrackHeight - 1, scaleLabelWidth, y + scaleTrackHeight - 1);
        }
    }

    void drawTimeline(Graphics g, JPanel contentPanel, List<TimelineView.TrackTimeline> tracks,
                      int selectedTrack, float bpm, GridMode gridMode,
                      float playheadPos, boolean isDragging, TimelineView.ClipRect draggingClip,
                      int dragSourceTrack, float dragOriginalStartTime, int dragCurrentY,
                      boolean creatingClip, int creatingTrackIdx, TimelineView.ClipRect creatingClipRect,
                      int trackHeight, int timeRulerHeight) {
        Graphics2D g2 = (Graphics2D) g;
        g2.setRenderingHint(RenderingHints.KEY_ANTIALIASING, RenderingHints.VALUE_ANTIALIAS_ON);

        int scaleTimeRuler = Theme.getInstance().scale(timeRulerHeight);
        int scaleTrackHeight = Theme.getInstance().scale(trackHeight);
        int scaleLabelWidth = 0;
        float pps = view.getPixelsPerSecond();

        // Draw tracks background
        for (int i = 0; i < tracks.size(); i++) {
            int y = scaleTimeRuler + i * scaleTrackHeight;
            if (i == selectedTrack) {
                g2.setColor(Theme.getInstance().ACCENT_BLUE.darker().darker());
            } else {
                g2.setColor(i % 2 == 0 ? Theme.getInstance().BG_DARK : Theme.getInstance().BG_DARKER);
            }
            g2.fillRect(0, y, contentPanel.getWidth(), scaleTrackHeight);
            g2.setColor(Theme.getInstance().PANEL_BG_LIGHT.darker());
            g2.drawLine(0, y + scaleTrackHeight - 1, contentPanel.getWidth(), y + scaleTrackHeight - 1);
        }

        // Draw grid lines
        int trackAreaBottom = scaleTimeRuler + tracks.size() * scaleTrackHeight;
        float secondsPerBeat = 60.0f / bpm;
        float secondsPerBar = secondsPerBeat * 4;
        float gridSeconds = view.getGridSnapSeconds(gridMode, secondsPerBeat);

        drawGridLines(g2, contentPanel, scaleTimeRuler, trackAreaBottom, scaleLabelWidth,
                pps, gridSeconds, secondsPerBeat, secondsPerBar);

        // Draw ghost shadow of dragged clip
        if (isDragging && draggingClip != null && dragSourceTrack >= 0) {
            drawDragGhost(g2, scaleTimeRuler, scaleTrackHeight, scaleLabelWidth, pps,
                    dragSourceTrack, dragOriginalStartTime, draggingClip.duration);
        }

        // Draw clips
        drawClips(g2, tracks, scaleTimeRuler, scaleTrackHeight, scaleLabelWidth, pps,
                isDragging, draggingClip);

        // Draw dragging clip at cursor position
        if (isDragging && draggingClip != null) {
            int targetTrackIdx = (dragCurrentY - scaleTimeRuler) / scaleTrackHeight;
            targetTrackIdx = Math.max(0, Math.min(tracks.size() - 1, targetTrackIdx));
            drawClipAt(g2, draggingClip, scaleTimeRuler + targetTrackIdx * scaleTrackHeight + 5,
                    scaleLabelWidth, pps, scaleTrackHeight, 0.8f, Theme.getInstance().ACCENT_BLUE.brighter());
        }

        // Draw clip being created
        if (creatingClip && creatingClipRect != null && creatingClipRect.duration > 0) {
            int y = scaleTimeRuler + creatingTrackIdx * scaleTrackHeight + 5;
            int x = scaleLabelWidth + (int) (creatingClipRect.startTime * pps);
            int w = (int) (creatingClipRect.duration * pps);
            int h = scaleTrackHeight - 10;
            g2.setComposite(AlphaComposite.getInstance(AlphaComposite.SRC_OVER, 0.6f));
            g2.setColor(new Color(100, 200, 100));
            g2.fillRoundRect(x, y, w, h, 8, 8);
            g2.setColor(new Color(150, 255, 150));
            g2.drawRoundRect(x, y, w, h, 8, 8);
            g2.setColor(Color.WHITE);
            g2.setFont(Theme.getInstance().FONT_UI.deriveFont(Font.BOLD, Theme.getInstance().scale(10.0f)));
            g2.drawString("New Clip", x + 5, y + 15);
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

    private void drawDragGhost(Graphics2D g2, int scaleTimeRuler, int scaleTrackHeight,
                               int scaleLabelWidth, float pps, int sourceTrack,
                               float originalStartTime, float duration) {
        int ghostY = scaleTimeRuler + sourceTrack * scaleTrackHeight + 5;
        int ghostX = scaleLabelWidth + (int) (originalStartTime * pps);
        int ghostW = (int) (duration * pps);
        int ghostH = scaleTrackHeight - 10;

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

    private void drawClips(Graphics2D g2, List<TimelineView.TrackTimeline> tracks,
                           int scaleTimeRuler, int scaleTrackHeight, int scaleLabelWidth,
                           float pps, boolean isDragging, TimelineView.ClipRect draggingClip) {
        for (int i = 0; i < tracks.size(); i++) {
            int y = scaleTimeRuler + i * scaleTrackHeight + 5;
            for (TimelineView.ClipRect clip : tracks.get(i).clips) {
                if (isDragging && clip == draggingClip) continue;
                int x = scaleLabelWidth + (int) (clip.startTime * pps);
                int w = (int) (clip.duration * pps);
                int h = scaleTrackHeight - 10;

                g2.setColor(Theme.getInstance().ACCENT_BLUE.darker());
                g2.fillRoundRect(x, y, w, h, 8, 8);

                boolean isMidi = clip.path == null || clip.path.isEmpty()
                        || clip.path.toLowerCase().endsWith(".mid")
                        || clip.path.toLowerCase().endsWith(".midi");

                if (isMidi) {
                    drawMidiPreview(g2, clip, x, y, w, h);
                } else {
                    drawAudioWaveform(g2, clip, x, y, w, h);
                }

                g2.setColor(Theme.getInstance().ACCENT_BLUE);
                g2.drawRoundRect(x, y, w, h, 8, 8);
                g2.setColor(Color.WHITE);
                g2.setFont(Theme.getInstance().FONT_UI.deriveFont(Font.BOLD, Theme.getInstance().scale(10.0f)));
                g2.drawString(clip.name, x + 5, y + 15);
            }
        }
    }

    private void drawMidiPreview(Graphics2D g2, TimelineView.ClipRect clip, int x, int y, int w, int h) {
        if (clip.waveform == null || clip.waveform.length == 0) return;
        g2.setColor(new Color(255, 255, 255, 200));
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
    }

    private void drawAudioWaveform(Graphics2D g2, TimelineView.ClipRect clip, int x, int y, int w, int h) {
        if (clip.waveform == null || clip.waveform.length == 0) return;
        g2.setColor(new Color(255, 255, 255, 120));
        int midY = y + h / 2;
        int halfH = h / 2 - 4;
        for (int px = 0; px < w && px < clip.waveform.length; px++) {
            int wfIdx = (int) ((float) px / w * clip.waveform.length);
            if (wfIdx >= clip.waveform.length) wfIdx = clip.waveform.length - 1;
            float amp = clip.waveform[wfIdx];
            int barH = (int) (amp * halfH);
            g2.drawLine(x + px, midY - barH, x + px, midY + barH);
        }
    }

    private void drawClipAt(Graphics2D g2, TimelineView.ClipRect clip, int y, int scaleLabelWidth,
                            float pps, int scaleTrackHeight, float alpha, Color borderColor) {
        int x = scaleLabelWidth + (int) (clip.startTime * pps);
        int w = (int) (clip.duration * pps);
        int h = scaleTrackHeight - 10;
        g2.setComposite(AlphaComposite.getInstance(AlphaComposite.SRC_OVER, alpha));
        g2.setColor(Theme.getInstance().ACCENT_BLUE.darker());
        g2.fillRoundRect(x, y, w, h, 8, 8);
        g2.setColor(borderColor);
        g2.drawRoundRect(x, y, w, h, 8, 8);
        g2.setColor(Color.WHITE);
        g2.setFont(Theme.getInstance().FONT_UI.deriveFont(Font.BOLD, Theme.getInstance().scale(10.0f)));
        g2.drawString(clip.name, x + 5, y + 15);
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
