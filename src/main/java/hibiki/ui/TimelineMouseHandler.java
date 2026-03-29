package hibiki.ui;

import hibiki.BackendManager;
import hibiki.pb.commands.*;
import hibiki.pb.core.EntityRef;
import hibiki.pb.core.AutomationPoint;

import javax.swing.*;
import java.awt.*;
import java.awt.event.*;
import java.util.Comparator;
import java.util.List;

/**
 * Handles mouse interactions on the Timeline content panel:
 * - Click to select tracks and seek playhead
 * - Right-click context menus for clips and empty areas
 * - Left-click drag to move/copy clips between tracks
 * - Left-click drag on empty area to create new clips
 * - Drag clip right edge to resize
 * - Hover near right edge shows resize cursor
 * - Click/drag automation points in expanded automation lanes
 */
class TimelineMouseHandler {
    private final TimelineView view;

    // Automation editing state
    private boolean editingAutomation = false;
    private int autoTrackIdx = -1;
    private int autoLaneIdx = -1;
    private int autoDragPointIdx = -1;

    // Tension handle drag state (midpoint between two consecutive points)
    private boolean draggingTensionHandle = false;
    private int tensionHandleSegmentIdx = -1; // index of the FIRST point of the segment

    TimelineMouseHandler(TimelineView view) {
        this.view = view;
    }

    /** Wire up mouse listeners on the content panel. */
    void install() {
        view.contentPanel.addMouseListener(new MouseAdapter() {
            @Override
            public void mousePressed(MouseEvent e) {
                handleMousePressed(e);
            }

            @Override
            public void mouseReleased(MouseEvent e) {
                handleMouseReleased(e);
            }
        });

        view.contentPanel.addMouseMotionListener(new MouseMotionAdapter() {
            @Override
            public void mouseDragged(MouseEvent e) {
                handleMouseDragged(e);
            }

            @Override
            public void mouseMoved(MouseEvent e) {
                handleMouseMoved(e);
            }
        });
    }

    // ─── Automation lane coordinate helpers ─────────────────────────────

    /**
     * Check if a Y position falls within an automation lane sub-row.
     * If so, returns the lane index (0-based). Otherwise returns -1.
     * Also sets autoTrackIdx as a side effect.
     */
    private int findAutomationLaneAtY(int trackIdx, int mouseY) {
        if (trackIdx < 0 || trackIdx >= view.tracks.size())
            return -1;
        TimelineView.TrackTimeline track = view.tracks.get(trackIdx);
        if (!track.automationExpanded || track.automationLanes.isEmpty())
            return -1;

        int scaleTimeRuler = Theme.getInstance().scale(TimelineView.TIME_RULER_HEIGHT);
        int trackTopY = scaleTimeRuler + Theme.getInstance().scale(view.getTrackY(trackIdx));
        int scaleBaseTrack = Theme.getInstance().scale(view.getBaseTrackHeight());
        int scaleAutoLane = Theme.getInstance().scale(view.getAutomationLaneHeight());

        int yInTrack = mouseY - trackTopY;
        if (yInTrack < scaleBaseTrack)
            return -1; // In clip area, not automation

        int yInAutoArea = yInTrack - scaleBaseTrack;
        int laneIdx = yInAutoArea / scaleAutoLane;
        if (laneIdx >= 0 && laneIdx < track.automationLanes.size()) {
            return laneIdx;
        }
        return -1;
    }

    /** Convert screen X to automation time in beats. */
    private float xToAutoBeats(int x) {
        float pps = view.getPixelsPerSecond();
        float timeSec = x / pps;
        float secondsPerBeat = 60.0f / view.bpm;
        return timeSec / secondsPerBeat;
    }

    /** Snap automation beats to grid (unless shift is held). */
    private float snapAutoBeats(float beats, boolean shiftHeld) {
        if (shiftHeld)
            return beats; // shift disables snap
        float secondsPerBeat = 60.0f / view.bpm;
        float gridSeconds = view.getGridSnapSeconds(view.getGridMode(), secondsPerBeat);
        if (gridSeconds <= 0)
            return beats;
        float gridBeats = gridSeconds / secondsPerBeat;
        return Math.round(beats / gridBeats) * gridBeats;
    }

    /** Convert automation beats to screen X. */
    private float autoBeatsToX(float beats) {
        float secondsPerBeat = 60.0f / view.bpm;
        return beats * secondsPerBeat * view.getPixelsPerSecond();
    }

    /** Convert screen Y to automation value (0..1) within a lane. */
    private float yToAutoValue(int mouseY, int laneTopY, int laneHeight) {
        int pad = 4;
        int drawH = laneHeight - 2 * pad;
        float val = 1.0f - (float) (mouseY - laneTopY - pad) / drawH;
        return Math.max(0, Math.min(1, val));
    }

    /** Get the screen Y of the top of a specific automation lane. */
    private int getAutoLaneTopY(int trackIdx, int laneIdx) {
        int scaleTimeRuler = Theme.getInstance().scale(TimelineView.TIME_RULER_HEIGHT);
        int trackTopY = scaleTimeRuler + Theme.getInstance().scale(view.getTrackY(trackIdx));
        int scaleBaseTrack = Theme.getInstance().scale(view.getBaseTrackHeight());
        int scaleAutoLane = Theme.getInstance().scale(view.getAutomationLaneHeight());
        return trackTopY + scaleBaseTrack + laneIdx * scaleAutoLane;
    }

    /** Find the nearest automation point to the given screen coordinates. */
    private int findAutoPointAt(List<AutomationEditor.AutoPoint> points, int mx, int my,
            int laneTopY, int laneHeight, int threshold) {
        int pad = 4;
        int drawH = laneHeight - 2 * pad;
        for (int i = 0; i < points.size(); i++) {
            AutomationEditor.AutoPoint p = points.get(i);
            float px = autoBeatsToX(p.timeBeats);
            float py = laneTopY + pad + drawH - (p.value * drawH);
            if (Math.abs(mx - px) < threshold && Math.abs(my - py) < threshold) {
                return i;
            }
        }
        return -1;
    }

    /**
     * Find a tension handle (midpoint circle between two consecutive points).
     * Returns the index of the FIRST point of the segment, or -1.
     */
    private int findTensionHandleAt(List<AutomationEditor.AutoPoint> points, int mx, int my,
            int laneTopY, int laneHeight, int threshold) {
        if (points.size() < 2)
            return -1;
        int pad = 4;
        int drawH = laneHeight - 2 * pad;
        for (int i = 0; i < points.size() - 1; i++) {
            AutomationEditor.AutoPoint p0 = points.get(i);
            AutomationEditor.AutoPoint p1 = points.get(i + 1);
            float mx0 = autoBeatsToX(p0.timeBeats);
            float mx1 = autoBeatsToX(p1.timeBeats);
            float midX = (mx0 + mx1) / 2f;
            // Interpolate value at midpoint using current tension
            float t = 0.5f;
            float exponent = (float) Math.pow(2.0, p0.tension);
            float curvedT = (float) Math.pow(t, exponent);
            float midVal = p0.value + (p1.value - p0.value) * curvedT;
            float midY = laneTopY + pad + drawH - midVal * drawH;
            if (Math.abs(mx - midX) < threshold && Math.abs(my - midY) < threshold) {
                return i;
            }
        }
        return -1;
    }

    /** Send updated automation points to the backend. */
    private void sendAutoUpdate(int trackIdx, int laneIdx, List<AutomationEditor.AutoPoint> points) {
        TimelineView.TrackTimeline track = view.tracks.get(trackIdx);
        TimelineView.AutomationLaneData lane = track.automationLanes.get(laneIdx);
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

    // ─── Mouse press ────────────────────────────────────────────────────

    private void handleMousePressed(MouseEvent e) {
        int scaleTimeRuler = Theme.getInstance().scale(TimelineView.TIME_RULER_HEIGHT);
        if (e.getY() < scaleTimeRuler) {
            view.updatePlayhead(e.getX());
            return;
        }

        int trackIdx = view.getTrackIdxAtY(e.getY() - scaleTimeRuler);
        if (trackIdx < 0 || trackIdx >= view.tracks.size())
            return;

        // --- Check if click is in an automation lane ---
        int laneIdx = findAutomationLaneAtY(trackIdx, e.getY());
        if (laneIdx >= 0) {
            handleAutomationPress(e, trackIdx, laneIdx);
            return;
        }

        // --- Normal clip/track handling ---
        view.setSelectedTrack(trackIdx);

        if (SwingUtilities.isRightMouseButton(e)) {
            TimelineView.ClipRect clip = view.findClipAtPosition(trackIdx, e.getX());
            if (clip != null) {
                view.showClipContextMenu(trackIdx, clip, e.getX(), e.getY());
            } else {
                float clickTime = e.getX() / view.getPixelsPerSecond();
                view.showEmptyAreaContextMenu(trackIdx, clickTime, e.getX(), e.getY());
            }
        } else if (SwingUtilities.isLeftMouseButton(e)) {
            TimelineView.ClipRect clip = view.findClipAtPosition(trackIdx, e.getX());
            if (clip != null && view.isNearRightEdge(clip, e.getX())) {
                view.resizingClip = true;
                view.resizeClip = clip;
                view.resizeTrackIdx = trackIdx;
                view.resizeOriginalDuration = clip.duration;
            } else if (clip != null) {
                view.draggingClip = clip;
                view.dragSourceTrack = trackIdx;
                view.dragStartX = e.getX();
                view.dragStartY = e.getY();
                view.dragOriginalStartTime = clip.startTime;
                view.isDragging = false;
            } else {
                // Start creating a new clip in empty area
                view.creatingClip = true;
                view.creatingTrackIdx = trackIdx;
                float startTime = e.getX() / view.getPixelsPerSecond();
                if (!e.isShiftDown()) {
                    startTime = view.snapToBar(startTime);
                }
                view.creatingStartTime = startTime;
                view.creatingClipRect = new TimelineView.ClipRect();
                view.creatingClipRect.name = "New Clip";
                view.creatingClipRect.startTime = startTime;
                view.creatingClipRect.duration = 0;
            }
        }
    }

    private void handleAutomationPress(MouseEvent e, int trackIdx, int laneIdx) {
        TimelineView.TrackTimeline track = view.tracks.get(trackIdx);
        TimelineView.AutomationLaneData lane = track.automationLanes.get(laneIdx);
        int scaleAutoLane = Theme.getInstance().scale(view.getAutomationLaneHeight());
        int laneTopY = getAutoLaneTopY(trackIdx, laneIdx);

        if (SwingUtilities.isRightMouseButton(e)) {
            // Right-click: delete point, adjust tension, or open editor
            int idx = findAutoPointAt(lane.points, e.getX(), e.getY(), laneTopY, scaleAutoLane, 8);
            if (idx >= 0) {
                showAutoPointContextMenu(e, trackIdx, laneIdx, idx);
            } else {
                // Show lane context menu with "Edit Automation..."
                JPopupMenu menu = new JPopupMenu();
                JMenuItem editItem = new JMenuItem("Edit Automation...");
                editItem.addActionListener(ev -> {
                    Frame frame = (Frame) SwingUtilities.getWindowAncestor(view.contentPanel);
                    AutomationEditorDialog dialog = new AutomationEditorDialog(
                            frame, trackIdx, laneIdx, lane.paramName, lane.points, view.bpm);
                    dialog.setVisible(true);
                });
                menu.add(editItem);
                menu.show(view.contentPanel, e.getX(), e.getY());
            }
            return;
        }

        // Left-click: drag tension handle, existing point, or add new point
        int handleIdx = findTensionHandleAt(lane.points, e.getX(), e.getY(), laneTopY, scaleAutoLane, 8);
        if (handleIdx >= 0) {
            // Start dragging a tension handle
            editingAutomation = true;
            draggingTensionHandle = true;
            tensionHandleSegmentIdx = handleIdx;
            autoTrackIdx = trackIdx;
            autoLaneIdx = laneIdx;
            autoDragPointIdx = -1;
            return;
        }

        int idx = findAutoPointAt(lane.points, e.getX(), e.getY(), laneTopY, scaleAutoLane, 8);
        if (idx >= 0) {
            // Start dragging existing point
            editingAutomation = true;
            autoTrackIdx = trackIdx;
            autoLaneIdx = laneIdx;
            autoDragPointIdx = idx;
        } else {
            // Add new point (snapped to grid)
            float beat = Math.max(0, snapAutoBeats(xToAutoBeats(e.getX()), e.isShiftDown()));
            float val = yToAutoValue(e.getY(), laneTopY, scaleAutoLane);
            AutomationEditor.AutoPoint np = new AutomationEditor.AutoPoint(beat, val, 0.0f);
            lane.points.add(np);
            lane.points.sort(Comparator.comparingDouble(a -> a.timeBeats));
            editingAutomation = true;
            autoTrackIdx = trackIdx;
            autoLaneIdx = laneIdx;
            autoDragPointIdx = lane.points.indexOf(np);
            view.contentPanel.repaint();
        }
    }

    private void showAutoPointContextMenu(MouseEvent e, int trackIdx, int laneIdx, int pointIdx) {
        TimelineView.TrackTimeline track = view.tracks.get(trackIdx);
        TimelineView.AutomationLaneData lane = track.automationLanes.get(laneIdx);

        JPopupMenu menu = new JPopupMenu();

        JMenuItem deleteItem = new JMenuItem("Delete Point");
        deleteItem.addActionListener(ev -> {
            lane.points.remove(pointIdx);
            sendAutoUpdate(trackIdx, laneIdx, lane.points);
            view.contentPanel.repaint();
        });
        menu.add(deleteItem);

        // Tension submenu
        JMenu tensionMenu = new JMenu("Tension");
        float[] tensions = { -0.8f, -0.5f, -0.2f, 0.0f, 0.2f, 0.5f, 0.8f };
        String[] labels = { "Ease Out (strong)", "Ease Out", "Ease Out (light)",
                "Linear", "Ease In (light)", "Ease In", "Ease In (strong)" };
        for (int i = 0; i < tensions.length; i++) {
            float t = tensions[i];
            JMenuItem item = new JMenuItem(labels[i] + " (" + t + ")");
            item.addActionListener(ev -> {
                if (pointIdx < lane.points.size()) {
                    lane.points.get(pointIdx).tension = t;
                    sendAutoUpdate(trackIdx, laneIdx, lane.points);
                    view.contentPanel.repaint();
                }
            });
            tensionMenu.add(item);
        }
        menu.add(tensionMenu);

        menu.show(view.contentPanel, e.getX(), e.getY());
    }

    // ─── Mouse release ──────────────────────────────────────────────────

    private void handleMouseReleased(MouseEvent e) {
        // Automation editing release
        if (editingAutomation) {
            if (autoTrackIdx >= 0 && autoLaneIdx >= 0) {
                TimelineView.TrackTimeline track = view.tracks.get(autoTrackIdx);
                TimelineView.AutomationLaneData lane = track.automationLanes.get(autoLaneIdx);
                lane.points.sort(Comparator.comparingDouble(a -> a.timeBeats));
                sendAutoUpdate(autoTrackIdx, autoLaneIdx, lane.points);
                view.contentPanel.repaint();
            }
            editingAutomation = false;
            draggingTensionHandle = false;
            tensionHandleSegmentIdx = -1;
            autoTrackIdx = -1;
            autoLaneIdx = -1;
            autoDragPointIdx = -1;
            return;
        }

        if (view.draggingClip != null && view.isDragging) {
            completeDrag(e);
        }

        // Reset drag state
        view.draggingClip = null;
        view.dragSourceTrack = -1;
        view.isDragging = false;

        // Handle clip resize completion
        if (view.resizingClip && view.resizeClip != null) {
            completeResize(e);
        }

        // Reset resize state
        view.resizingClip = false;
        view.resizeClip = null;
        view.resizeTrackIdx = -1;

        // Handle clip creation completion
        if (view.creatingClip && view.creatingClipRect != null) {
            completeCreation(e);
        }

        // Reset creation state
        view.creatingClip = false;
        view.creatingTrackIdx = -1;
        view.creatingClipRect = null;
    }

    private void completeDrag(MouseEvent e) {
        int scaleTimeRuler = Theme.getInstance().scale(TimelineView.TIME_RULER_HEIGHT);
        int targetTrackIdx = view.getTrackIdxAtY(e.getY() - scaleTimeRuler);
        targetTrackIdx = Math.max(0, Math.min(view.tracks.size() - 1, targetTrackIdx));

        float newStartTime = Math.max(0, e.getX() / view.getPixelsPerSecond());
        if (!e.isShiftDown()) {
            newStartTime = view.snapToBar(newStartTime);
        }

        boolean isCopy = e.isAltDown();

        if (isCopy) {
            TimelineView.ClipRect newClip = new TimelineView.ClipRect();
            newClip.name = view.draggingClip.name;
            newClip.path = view.draggingClip.path;
            newClip.startTime = newStartTime;
            newClip.duration = view.draggingClip.duration;
            newClip.waveform = view.draggingClip.waveform;

            TimelineView.TrackTimeline targetTrack = view.tracks.get(targetTrackIdx);
            targetTrack.clips.add(newClip);
            targetTrack.clipMap.put(targetTrack.clips.size() - 1, newClip);

            // Restore original clip position
            view.draggingClip.startTime = view.dragOriginalStartTime;
        } else {
            if (targetTrackIdx != view.dragSourceTrack) {
                TimelineView.TrackTimeline sourceTrack = view.tracks.get(view.dragSourceTrack);
                TimelineView.TrackTimeline targetTrack = view.tracks.get(targetTrackIdx);

                int clipIdx = sourceTrack.clips.indexOf(view.draggingClip);
                if (clipIdx >= 0) {
                    sourceTrack.clips.remove(clipIdx);
                    sourceTrack.clipMap.clear();
                    for (int i = 0; i < sourceTrack.clips.size(); i++) {
                        sourceTrack.clipMap.put(i, sourceTrack.clips.get(i));
                    }
                }

                view.draggingClip.startTime = newStartTime;
                targetTrack.clips.add(view.draggingClip);
                targetTrack.clipMap.put(targetTrack.clips.size() - 1, view.draggingClip);
            } else {
                view.draggingClip.startTime = newStartTime;
            }
        }

        view.updateContentSize();
        view.contentPanel.repaint();
    }

    private void completeResize(MouseEvent e) {
        float endTime = view.resizeClip.startTime + view.resizeClip.duration;
        if (!e.isShiftDown()) {
            endTime = view.snapToBar(endTime);
        }
        float newDuration = Math.max(60.0f / view.bpm, endTime - view.resizeClip.startTime);
        view.resizeClip.duration = newDuration;
        float durationBeats = newDuration * (view.bpm / 60.0f);

        TimelineView.TrackTimeline track = view.tracks.get(view.resizeTrackIdx);
        int clipIndex = track.clips.indexOf(view.resizeClip);
        if (clipIndex >= 0) {
            BackendManager.getInstance().resizeTimelineClip(view.resizeTrackIdx, clipIndex, durationBeats);
        }
        view.contentPanel.repaint();
    }

    private void completeCreation(MouseEvent e) {
        float endTime = Math.max(0, e.getX() / view.getPixelsPerSecond());
        if (!e.isShiftDown()) {
            endTime = view.snapToBar(endTime);
        }
        float duration = endTime - view.creatingStartTime;

        if (duration > 0.1f) {
            float durationBeats = duration * (view.bpm / 60.0f);
            BackendManager.getInstance().addTimelineClip(view.creatingTrackIdx, "",
                    view.creatingStartTime, durationBeats);
        }
        view.contentPanel.repaint();
    }

    // ─── Mouse drag ─────────────────────────────────────────────────────

    private void handleMouseDragged(MouseEvent e) {
        // Automation tension handle drag
        if (editingAutomation && draggingTensionHandle && tensionHandleSegmentIdx >= 0) {
            TimelineView.TrackTimeline track = view.tracks.get(autoTrackIdx);
            TimelineView.AutomationLaneData lane = track.automationLanes.get(autoLaneIdx);
            if (tensionHandleSegmentIdx < lane.points.size() - 1) {
                int scaleAutoLane = Theme.getInstance().scale(view.getAutomationLaneHeight());
                int laneTopY = getAutoLaneTopY(autoTrackIdx, autoLaneIdx);
                AutomationEditor.AutoPoint p0 = lane.points.get(tensionHandleSegmentIdx);
                AutomationEditor.AutoPoint p1 = lane.points.get(tensionHandleSegmentIdx + 1);
                // Map vertical drag to tension: pull up = negative tension (ease-out), pull
                // down = positive (ease-in)
                float midVal = (p0.value + p1.value) / 2f;
                float dragVal = yToAutoValue(e.getY(), laneTopY, scaleAutoLane);
                float diff = midVal - dragVal; // positive = dragged below midpoint
                float newTension = Math.max(-1f, Math.min(1f, diff * 4f));
                p0.tension = newTension;
                view.contentPanel.repaint();
            }
            return;
        }

        // Automation point drag
        if (editingAutomation && autoDragPointIdx >= 0) {
            TimelineView.TrackTimeline track = view.tracks.get(autoTrackIdx);
            TimelineView.AutomationLaneData lane = track.automationLanes.get(autoLaneIdx);
            if (autoDragPointIdx < lane.points.size()) {
                int scaleAutoLane = Theme.getInstance().scale(view.getAutomationLaneHeight());
                int laneTopY = getAutoLaneTopY(autoTrackIdx, autoLaneIdx);
                AutomationEditor.AutoPoint p = lane.points.get(autoDragPointIdx);
                p.timeBeats = Math.max(0, snapAutoBeats(xToAutoBeats(e.getX()), e.isShiftDown()));
                p.value = yToAutoValue(e.getY(), laneTopY, scaleAutoLane);
                view.contentPanel.repaint();
            }
            return;
        }

        int scaleTimeRuler = Theme.getInstance().scale(TimelineView.TIME_RULER_HEIGHT);
        if (e.getY() < scaleTimeRuler && view.draggingClip == null
                && !view.creatingClip && !view.resizingClip) {
            view.updatePlayhead(e.getX());
        } else if (view.resizingClip && view.resizeClip != null) {
            float mouseTime = Math.max(0, e.getX() / view.getPixelsPerSecond());
            float newDuration = Math.max(60.0f / view.bpm, mouseTime - view.resizeClip.startTime);
            view.resizeClip.duration = newDuration;
            view.contentPanel.repaint();
        } else if (view.creatingClip && view.creatingClipRect != null) {
            float endTime = Math.max(0, e.getX() / view.getPixelsPerSecond());
            if (!e.isShiftDown()) {
                endTime = view.snapToBar(endTime);
            }
            view.creatingClipRect.duration = Math.max(0, endTime - view.creatingStartTime);
            view.contentPanel.repaint();
        } else if (view.draggingClip != null) {
            if (!view.isDragging) {
                int dx = e.getX() - view.dragStartX;
                int dy = e.getY() - view.dragStartY;
                if (Math.abs(dx) > 5 || Math.abs(dy) > 5) {
                    view.isDragging = true;
                }
            }

            if (view.isDragging) {
                float newStartTime = Math.max(0, e.getX() / view.getPixelsPerSecond());
                if (!e.isShiftDown()) {
                    newStartTime = view.snapToBar(newStartTime);
                }
                view.draggingClip.startTime = newStartTime;
                view.dragCurrentY = e.getY();
                view.contentPanel.repaint();
            }
        }
    }

    // ─── Mouse move (cursor) ────────────────────────────────────────────

    private void handleMouseMoved(MouseEvent e) {
        int scaleTimeRuler = Theme.getInstance().scale(TimelineView.TIME_RULER_HEIGHT);
        int trackIdx = view.getTrackIdxAtY(e.getY() - scaleTimeRuler);
        if (trackIdx >= 0 && trackIdx < view.tracks.size()) {
            // Check if hovering over automation area
            int laneIdx = findAutomationLaneAtY(trackIdx, e.getY());
            if (laneIdx >= 0) {
                view.contentPanel.setCursor(Cursor.getPredefinedCursor(Cursor.CROSSHAIR_CURSOR));
                return;
            }

            TimelineView.ClipRect clip = view.findClipAtPosition(trackIdx, e.getX());
            if (clip != null && view.isNearRightEdge(clip, e.getX())) {
                view.contentPanel.setCursor(Cursor.getPredefinedCursor(Cursor.E_RESIZE_CURSOR));
            } else {
                view.contentPanel.setCursor(Cursor.getDefaultCursor());
            }
        } else {
            view.contentPanel.setCursor(Cursor.getDefaultCursor());
        }
    }
}
