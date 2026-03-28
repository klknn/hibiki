package hibiki.ui;

import hibiki.BackendManager;

import javax.swing.*;
import java.awt.*;
import java.awt.event.*;

/**
 * Handles mouse interactions on the Timeline content panel:
 * - Click to select tracks and seek playhead
 * - Right-click context menus for clips and empty areas
 * - Left-click drag to move/copy clips between tracks
 * - Left-click drag on empty area to create new clips
 * - Drag clip right edge to resize
 * - Hover near right edge shows resize cursor
 */
class TimelineMouseHandler {
    private final TimelineView view;

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

    private void handleMousePressed(MouseEvent e) {
        int scaleTimeRuler = Theme.getInstance().scale(TimelineView.TIME_RULER_HEIGHT);
        if (e.getY() < scaleTimeRuler) {
            view.updatePlayhead(e.getX());
        } else {
            int trackIdx = view.getTrackIdxAtY(e.getY() - scaleTimeRuler);
            if (trackIdx >= 0 && trackIdx < view.tracks.size()) {
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
        }
    }

    private void handleMouseReleased(MouseEvent e) {
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

    private void handleMouseDragged(MouseEvent e) {
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

    private void handleMouseMoved(MouseEvent e) {
        int scaleTimeRuler = Theme.getInstance().scale(TimelineView.TIME_RULER_HEIGHT);
        int trackIdx = view.getTrackIdxAtY(e.getY() - scaleTimeRuler);
        if (trackIdx >= 0 && trackIdx < view.tracks.size()) {
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
