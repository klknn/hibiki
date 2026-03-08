package hibiki.ui;

import hibiki.BackendManager;
import hibiki.ipc.Notification;
import hibiki.ipc.TimelineClipInfo;

import javax.swing.*;
import java.awt.*;
import java.awt.event.*;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.HashMap;

public class TimelineView extends JPanel implements Theme.ThemeListener {
    private static final int TRACK_HEIGHT = 80;
    private static final int TIME_RULER_HEIGHT = 30;
    private static final float PIXELS_PER_SECOND = 50.0f;
 
    enum GridUnit { SECONDS, BARS }
    private GridUnit gridUnit = GridUnit.BARS;
    private float bpm = 120.0f;
    private boolean isPlaying = false;

    float playheadPos = 0.0f;
    final List<TrackTimeline> tracks = new ArrayList<>();
    private final JScrollPane scrollPane;
    private final JPanel contentPanel;
    private final Timer repaintTimer;

    public TimelineView() {
        Theme.getInstance().addListener(this);
        setLayout(new BorderLayout());
        setBackground(Theme.getInstance().BG_DARK);

        contentPanel = new JPanel() {
            @Override
            protected void paintComponent(Graphics g) {
                super.paintComponent(g);
                drawTimeline(g);
            }
        };
        contentPanel.setLayout(null);
        contentPanel.setBackground(Theme.getInstance().BG_DARK);

        scrollPane = new JScrollPane(contentPanel);
        scrollPane.setBorder(null);
        scrollPane.getVerticalScrollBar().setUnitIncrement(16);
        add(scrollPane, BorderLayout.CENTER);

        // Initial tracks
        for (int i = 0; i < 8; i++) {
            tracks.add(new TrackTimeline(i));
        }
        updateContentSize();

        repaintTimer = new Timer(33, e -> {
            repaint();
        });
        repaintTimer.start();

        BackendManager.getInstance().addNotificationListener(this::handleNotification);
        
        setupMouseListeners();
        setupDropTarget();
        setupControls();
    }

    private void setupControls() {
        JPanel controls = new JPanel(new FlowLayout(FlowLayout.LEFT));
        controls.setOpaque(false);
        
        JComboBox<GridUnit> unitCombo = new JComboBox<>(GridUnit.values());
        unitCombo.setSelectedItem(gridUnit);
        unitCombo.addActionListener(e -> {
            gridUnit = (GridUnit) unitCombo.getSelectedItem();
            repaint();
        });
        
        JLabel label = new JLabel("Grid:");
        label.setForeground(Theme.getInstance().TEXT_DIM);
        controls.add(label);
        controls.add(unitCombo);
        
        add(controls, BorderLayout.NORTH);
    }

    private void setupDropTarget() {
        new java.awt.dnd.DropTarget(contentPanel, new java.awt.dnd.DropTargetAdapter() {
            @Override
            public void drop(java.awt.dnd.DropTargetDropEvent dtde) {
                try {
                    if (dtde.isDataFlavorSupported(java.awt.datatransfer.DataFlavor.stringFlavor)) {
                        dtde.acceptDrop(java.awt.dnd.DnDConstants.ACTION_COPY);
                        String data = (String) dtde.getTransferable().getTransferData(java.awt.datatransfer.DataFlavor.stringFlavor);
                        dtde.dropComplete(true);

                        String[] parts = data.split(":", 2);
                        if (parts.length == 2) {
                            String type = parts[0];
                            String path = parts[1];
                            
                            Point p = dtde.getLocation();
                            int trackIndex = (p.y - Theme.getInstance().scale(TIME_RULER_HEIGHT)) / Theme.getInstance().scale(TRACK_HEIGHT);
                            double timeSec = p.x / PIXELS_PER_SECOND;

                            // Snap to nearest bar boundary
                            if (gridUnit == GridUnit.BARS) {
                                float secondsPerBar = (60.0f / bpm) * 4.0f;
                                timeSec = Math.round(timeSec / secondsPerBar) * secondsPerBar;
                            }

                            if (trackIndex >= 0 && trackIndex < tracks.size()) {
                                BackendManager.getInstance().addTimelineClip(trackIndex, path, (float)timeSec);
                            }
                        }
                    } else {
                        dtde.rejectDrop();
                    }
                } catch (Exception e) {
                    e.printStackTrace();
                    dtde.rejectDrop();
                }
            }
        });
    }

    private void updateContentSize() {
        int width = (int) (PIXELS_PER_SECOND * 600); // 10 minutes
        int height = tracks.size() * Theme.getInstance().scale(TRACK_HEIGHT) + Theme.getInstance().scale(TIME_RULER_HEIGHT);
        contentPanel.setPreferredSize(new Dimension(width, height));
        contentPanel.revalidate();
    }

    private void setupMouseListeners() {
        contentPanel.addMouseListener(new MouseAdapter() {
            @Override
            public void mousePressed(MouseEvent e) {
                if (e.getY() < Theme.getInstance().scale(TIME_RULER_HEIGHT)) {
                    updatePlayhead(e.getX());
                }
            }
        });

        contentPanel.addMouseMotionListener(new MouseMotionAdapter() {
            @Override
            public void mouseDragged(MouseEvent e) {
                if (e.getY() < Theme.getInstance().scale(TIME_RULER_HEIGHT)) {
                    updatePlayhead(e.getX());
                }
            }
        });
    }

    private void updatePlayhead(int x) {
        playheadPos = Math.max(0, x / PIXELS_PER_SECOND);
        BackendManager.getInstance().seek(playheadPos);
        repaint();
    }

    void handleNotification(Notification n) {
        if (n.responseType() == hibiki.ipc.Response.TimelineClipInfo) {
            TimelineClipInfo info = (TimelineClipInfo) n.response(new TimelineClipInfo());
            int tidx = info.trackIndex();
            if (tidx >= 0 && tidx < tracks.size()) {
                tracks.get(tidx).addOrUpdateClip(info);
                repaint();
            }
        } else if (n.responseType() == hibiki.ipc.Response.PlayheadInfo) {
            hibiki.ipc.PlayheadInfo info = (hibiki.ipc.PlayheadInfo) n.response(new hibiki.ipc.PlayheadInfo());
            float pos = info.positionSec();
            float newBpm = info.bpm();
            boolean playing = info.isPlaying();
            SwingUtilities.invokeLater(() -> {
                this.playheadPos = pos;
                this.bpm = newBpm;
                this.isPlaying = playing;
                repaint();
            });
        }
    }

    private void drawTimeline(Graphics g) {
        Graphics2D g2 = (Graphics2D) g;
        g2.setRenderingHint(RenderingHints.KEY_ANTIALIASING, RenderingHints.VALUE_ANTIALIAS_ON);

        int scaleTimeRuler = Theme.getInstance().scale(TIME_RULER_HEIGHT);
        int scaleTrackHeight = Theme.getInstance().scale(TRACK_HEIGHT);

        // Draw tracks background
        for (int i = 0; i < tracks.size(); i++) {
            int y = scaleTimeRuler + i * scaleTrackHeight;
            g2.setColor(i % 2 == 0 ? Theme.getInstance().BG_DARK : Theme.getInstance().BG_DARKER);
            g2.fillRect(0, y, contentPanel.getWidth(), scaleTrackHeight);
            g2.setColor(Theme.getInstance().PANEL_BG_LIGHT.darker());
            g2.drawLine(0, y + scaleTrackHeight - 1, contentPanel.getWidth(), y + scaleTrackHeight - 1);
        }

        // Draw vertical grid lines through track area
        int trackAreaBottom = scaleTimeRuler + tracks.size() * scaleTrackHeight;
        g2.setColor(new Color(255, 255, 255, 20));
        if (gridUnit == GridUnit.BARS) {
            float secondsPerBeat = 60.0f / bpm;
            float secondsPerBar = secondsPerBeat * 4;
            for (int b = 0; b < 200; b++) {
                int x = (int) (b * secondsPerBar * PIXELS_PER_SECOND);
                if (x > contentPanel.getWidth()) break;
                g2.setColor(new Color(255, 255, 255, b % 4 == 0 ? 40 : 15));
                g2.drawLine(x, scaleTimeRuler, x, trackAreaBottom);
            }
        } else {
            for (int s = 0; s < 600; s += 5) {
                int x = (int) (s * PIXELS_PER_SECOND);
                g2.drawLine(x, scaleTimeRuler, x, trackAreaBottom);
            }
        }

        // Draw clips
        for (int i = 0; i < tracks.size(); i++) {
            int y = scaleTimeRuler + i * scaleTrackHeight + 5;
            for (ClipRect clip : tracks.get(i).clips) {
                int x = (int) (clip.startTime * PIXELS_PER_SECOND);
                int w = (int) (clip.duration * PIXELS_PER_SECOND);
                int h = scaleTrackHeight - 10;

                g2.setColor(Theme.getInstance().ACCENT_BLUE.darker());
                g2.fillRoundRect(x, y, w, h, 8, 8);

                boolean isMidi = clip.path.toLowerCase().endsWith(".mid") || clip.path.toLowerCase().endsWith(".midi");

                if (isMidi) {
                    if (clip.waveform != null && clip.waveform.length > 0) {
                        g2.setColor(new Color(255, 255, 255, 200));
                        for (int nIdx = 0; nIdx + 2 < clip.waveform.length; nIdx += 3) {
                            float startSec = clip.waveform[nIdx];
                            float pitch = clip.waveform[nIdx+1];
                            float durationSec = clip.waveform[nIdx+2];

                            int nx = x + (int)((startSec / clip.duration) * w);
                            int nw = (int)((durationSec / clip.duration) * w);
                            if (nw < 2) nw = 2; // Minimum visible width
                            
                            int minPitch = 21; // A0
                            int maxPitch = 108; // C8
                            float normalizedPitch = (pitch - minPitch) / (float)(maxPitch - minPitch);
                            if (normalizedPitch < 0) normalizedPitch = 0;
                            if (normalizedPitch > 1) normalizedPitch = 1;
                            
                            int nh = Math.max(2, h / 40);
                            int ny = y + h - (int)(normalizedPitch * (h - nh)) - nh;

                            // Clip to box
                            if (nx < x + w && nx + nw >= x) {
                                int drawX = Math.max(x, nx);
                                int drawW = Math.min(x + w - drawX, nx + nw - drawX);
                                g2.fillRect(drawX, ny, drawW, nh);
                            }
                        }
                    }
                } else {
                    // Draw waveform inside audio clip
                    if (clip.waveform != null && clip.waveform.length > 0) {
                        g2.setColor(new Color(255, 255, 255, 120));
                        int midY = y + h / 2;
                        int halfH = h / 2 - 4;
                        for (int px = 0; px < w && px < clip.waveform.length; px++) {
                            int wfIdx = (int)((float)px / w * clip.waveform.length);
                            if (wfIdx >= clip.waveform.length) wfIdx = clip.waveform.length - 1;
                            float amp = clip.waveform[wfIdx];
                            int barH = (int)(amp * halfH);
                            g2.drawLine(x + px, midY - barH, x + px, midY + barH);
                        }
                    }
                }

                g2.setColor(Theme.getInstance().ACCENT_BLUE);
                g2.drawRoundRect(x, y, w, h, 8, 8);

                g2.setColor(Color.WHITE);
                g2.setFont(Theme.getInstance().FONT_UI.deriveFont(Font.BOLD, Theme.getInstance().scale(10.0f)));
                g2.drawString(clip.name, x + 5, y + 15);
            }
        }

        // Draw time ruler
        g2.setColor(Theme.getInstance().BG_DARKER);
        g2.fillRect(0, 0, contentPanel.getWidth(), scaleTimeRuler);
        g2.setColor(Theme.getInstance().TEXT_DIM);

        if (gridUnit == GridUnit.SECONDS) {
            for (int s = 0; s < 600; s += 5) {
                int x = (int) (s * PIXELS_PER_SECOND);
                g2.drawLine(x, scaleTimeRuler - 10, x, scaleTimeRuler);
                g2.drawString(s + "s", x + 2, scaleTimeRuler - 12);
            }
        } else {
            float secondsPerBeat = 60.0f / bpm;
            float secondsPerBar = secondsPerBeat * 4;
            for (int b = 0; b < 200; b++) {
                float time = b * secondsPerBar;
                int x = (int) (time * PIXELS_PER_SECOND);
                if (x > contentPanel.getWidth()) break;
                g2.drawLine(x, scaleTimeRuler - 15, x, scaleTimeRuler);
                g2.drawString((b + 1) + ".1", x + 2, scaleTimeRuler - 15);
            }
        }

        // Draw playhead
        int px = (int) (playheadPos * PIXELS_PER_SECOND);
        g2.setColor(Color.RED);
        g2.setStroke(new BasicStroke(2.0f));
        g2.drawLine(px, 0, px, contentPanel.getHeight());
    }

    @Override
    public void onThemeChanged() {
        SwingUtilities.invokeLater(() -> {
            setBackground(Theme.getInstance().BG_DARK);
            contentPanel.setBackground(Theme.getInstance().BG_DARK);
            updateContentSize();
            repaint();
        });
    }

    static class TrackTimeline {
        int index;
        List<ClipRect> clips = new ArrayList<>();
        Map<Integer, ClipRect> clipMap = new HashMap<>();

        TrackTimeline(int index) {
            this.index = index;
        }

        void addOrUpdateClip(TimelineClipInfo info) {
            int cidx = info.clipIndex();
            ClipRect cr = clipMap.get(cidx);
            if (cr == null) {
                cr = new ClipRect();
                clips.add(cr);
                clipMap.put(cidx, cr);
            }
            cr.name = info.name();
            cr.path = info.path();
            cr.startTime = info.startTime();
            cr.duration = info.duration();
            // Extract waveform data
            int wfLen = info.waveformLength();
            if (wfLen > 0) {
                cr.waveform = new float[wfLen];
                for (int i = 0; i < wfLen; i++) {
                    cr.waveform[i] = info.waveform(i);
                }
            }
        }
    }

    static class ClipRect {
        String name;
        String path;
        float startTime;
        float duration;
        float[] waveform;
    }
}
