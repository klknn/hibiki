package hibiki.ui;

import hibiki.BackendManager;
import hibiki.pb.commands.*;
import hibiki.pb.core.EntityRef;
import java.awt.*;
import java.awt.event.*;
import javax.swing.*;

/**
 * Handles mouse interactions on the Timeline content panel: - Click to select tracks and seek
 * playhead - Right-click context menus for clips and empty areas - Left-click drag to move/copy
 * clips between tracks - Left-click drag on empty area to create new clips - Drag clip right edge
 * to resize - Hover near right edge shows resize cursor
 *
 * <p>Automation lane interactions are delegated to AutomationMouseHandler.
 */
class TimelineMouseHandler {
  private final TimelineView view;
  private final AutomationMouseHandler autoHandler;

  TimelineMouseHandler(TimelineView view) {
    this.view = view;
    this.autoHandler = new AutomationMouseHandler(view);
  }

  /** Wire up mouse listeners on the content panel. */
  void install() {
    view.contentPanel.addMouseListener(
        new MouseAdapter() {
          @Override
          public void mousePressed(MouseEvent e) {
            handleMousePressed(e);
          }

          @Override
          public void mouseReleased(MouseEvent e) {
            handleMouseReleased(e);
          }
        });

    view.contentPanel.addMouseMotionListener(
        new MouseMotionAdapter() {
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

  // ─── Mouse press ────────────────────────────────────────────────────

  private void handleMousePressed(MouseEvent e) {
    int scaleMarkerLane = Theme.getInstance().scale(TimelineView.MARKER_LANE_HEIGHT);
    int scaleSeekBottom =
        scaleMarkerLane + Theme.getInstance().scale(TimelineView.TIME_RULER_HEIGHT);
    int scaleTotalRuler = Theme.getInstance().scale(TimelineView.TOTAL_RULER_HEIGHT);

    if (e.getY() < scaleTotalRuler) {
      if (e.getY() < scaleMarkerLane) {
        // Marker lane
        handleMarkerPress(e, scaleMarkerLane);
      } else if (e.getY() < scaleSeekBottom) {
        // Seek lane: seek playhead
        view.updatePlayhead(e.getX());
      } else {
        // Loop lane
        if (e.getClickCount() == 2 && SwingUtilities.isLeftMouseButton(e)) {
          if (view.loopEndSec > view.loopStartSec) {
            view.loopEnabled = !view.loopEnabled;
            BackendManager.getInstance()
                .sendSetLoop(view.loopEnabled, view.loopStartSec, view.loopEndSec);
            view.contentPanel.repaint();
          }
        } else if (SwingUtilities.isLeftMouseButton(e)) {
          float pps = view.getPixelsPerSecond();
          int hitThreshold = Theme.getInstance().scale(6);
          if (view.loopEndSec > view.loopStartSec) {
            int startPx = (int) (view.loopStartSec * pps);
            int endPx = (int) (view.loopEndSec * pps);
            if (Math.abs(e.getX() - startPx) <= hitThreshold) {
              view.draggingLoopEnd = false;
              view.dragMode = TimelineView.DragMode.DRAG_LOOP_MARKER;
              return;
            } else if (Math.abs(e.getX() - endPx) <= hitThreshold) {
              view.draggingLoopEnd = true;
              view.dragMode = TimelineView.DragMode.DRAG_LOOP_MARKER;
              return;
            }
          }
          float clickTime = Math.max(0, e.getX() / pps);
          if (!e.isShiftDown()) clickTime = view.snapToGrid(clickTime);
          view.loopDragStartSec = clickTime;
          view.dragMode = TimelineView.DragMode.DRAG_LOOP_REGION;
        }
      }
      return;
    }

    int trackIdx = view.getTrackIdxAtY(e.getY() - scaleTotalRuler);
    if (trackIdx < 0 || trackIdx >= view.tracks.size()) return;

    // Check if click is in an automation lane
    int laneIdx = autoHandler.findAutomationLaneAtY(trackIdx, e.getY());
    if (laneIdx >= 0) {
      autoHandler.handlePress(e, trackIdx, laneIdx);
      return;
    }

    // Normal clip/track handling
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
      // Compute clip Y position for top/bottom half detection
      int clipY = scaleTotalRuler + Theme.getInstance().scale(view.getTrackY(trackIdx)) + 5;
      int clipH = Theme.getInstance().scale(view.getBaseTrackHeight(trackIdx)) - 10;
      if (clip != null && view.isNearLeftEdge(clip, e.getX())) {
        view.dragMode = TimelineView.DragMode.TRIM_LEFT;
        view.resizeClip = clip;
        view.resizeTrackIdx = trackIdx;
        view.resizeOriginalDuration = clip.duration;
      } else if (clip != null && view.isNearRightEdge(clip, e.getX())) {
        // Right edge hit — determine top half (loop extend) vs bottom half (resize)
        boolean topHalf = (e.getY() - clipY) <= clipH / 2;
        if (topHalf) {
          view.dragMode = TimelineView.DragMode.LOOP_EXTEND;
          // Set loopInterval at drag start so renderer shows tiled preview
          if (!clip.isLooped && clip.loopInterval <= 0) {
            clip.loopInterval = clip.duration;
          }
        } else {
          view.dragMode = TimelineView.DragMode.RESIZE_CLIP;
        }
        view.resizeClip = clip;
        view.resizeTrackIdx = trackIdx;
        view.resizeOriginalDuration = clip.duration;
      } else if (clip != null) {
        view.draggingClip = clip;
        view.dragSourceTrack = trackIdx;
        view.dragStartX = e.getX();
        view.dragStartY = e.getY();
        view.dragOriginalStartTime = clip.startTime;
        view.dragMode = TimelineView.DragMode.NONE;
      } else {
        // Start creating a new clip in empty area
        view.dragMode = TimelineView.DragMode.CREATE_CLIP;
        view.creatingTrackIdx = trackIdx;
        float startTime = e.getX() / view.getPixelsPerSecond();
        if (!e.isShiftDown()) {
          startTime = view.snapToGrid(startTime);
        }
        view.creatingStartTime = startTime;
        view.creatingClipRect = new TimelineView.ClipRect();
        view.creatingClipRect.name = "New Clip";
        view.creatingClipRect.startTime = startTime;
        view.creatingClipRect.duration = 0;
      }
    }
  }

  // ─── Mouse release ──────────────────────────────────────────────────

  private void handleMouseReleased(MouseEvent e) {
    // Delegate automation point/tension release
    if (autoHandler.handleRelease(e)) return;

    // Handle clip move completion
    if (view.draggingClip != null && view.dragMode == TimelineView.DragMode.MOVE_CLIP) {
      if (view.draggingClip.isAutomation) {
        BackendManager.getInstance()
            .moveAutomationClip(
                autoHandler.autoTrackIdx, autoHandler.autoLaneIdx,
                autoHandler.autoClipIdx, view.draggingClip.startTime);
      } else {
        completeDrag(e);
      }
    }

    // Handle clip resize completion (right edge)
    if (view.dragMode == TimelineView.DragMode.RESIZE_CLIP && view.resizeClip != null) {
      if (view.resizeClip.isAutomation) {
        float durationBeats = view.resizeClip.duration * (view.bpm / 60.0f);
        BackendManager.getInstance()
            .resizeAutomationClip(
                autoHandler.autoTrackIdx,
                autoHandler.autoLaneIdx,
                autoHandler.autoClipIdx,
                durationBeats);
      } else {
        completeResize(e);
      }
    }

    // Handle loop extend completion (top-right corner)
    if (view.dragMode == TimelineView.DragMode.LOOP_EXTEND && view.resizeClip != null) {
      completeLoopExtend(e);
    }

    // Handle clip head-trim completion (left edge)
    if (view.dragMode == TimelineView.DragMode.TRIM_LEFT && view.resizeClip != null) {
      completeTrimLeft(e);
    }

    // Handle clip creation completion
    if (view.dragMode == TimelineView.DragMode.CREATE_CLIP && view.creatingClipRect != null) {
      if (view.creatingAutoLaneIdx >= 0) {
        float duration = view.creatingClipRect.duration;
        if (duration > 0.1f) {
          float durationBeats = duration * (view.bpm / 60.0f);
          BackendManager.getInstance()
              .sendRequest(
                  Request.newBuilder()
                      .setAutomation(
                          AutomationCmd.newBuilder()
                              .setAction(AutomationCmd.Action.ACTION_ADD_CLIP)
                              .setTarget(
                                  EntityRef.newBuilder()
                                      .setTrackIndex(view.creatingTrackIdx)
                                      .setLaneIndex(view.creatingAutoLaneIdx))
                              .setStartTimeSec(view.creatingClipRect.startTime)
                              .setDurationBeats(durationBeats))
                      .build());
        }
      } else {
        completeCreation(e);
      }
    }

    // Handle loop marker drag completion
    if (view.dragMode == TimelineView.DragMode.DRAG_LOOP_MARKER) {
      if (view.loopEndSec > view.loopStartSec) {
        BackendManager.getInstance()
            .sendSetLoop(view.loopEnabled, view.loopStartSec, view.loopEndSec);
      }
    }

    // Handle loop region drag completion
    if (view.dragMode == TimelineView.DragMode.DRAG_LOOP_REGION) {
      float dragStart = view.loopDragStartSec;
      float mouseTime = Math.max(0, e.getX() / view.getPixelsPerSecond());
      if (!e.isShiftDown()) mouseTime = view.snapToGrid(mouseTime);
      float lo = Math.min(dragStart, mouseTime);
      float hi = Math.max(dragStart, mouseTime);
      if (hi - lo > 0.05f) {
        view.loopStartSec = lo;
        view.loopEndSec = hi;
        view.loopEnabled = true;
        BackendManager.getInstance().sendSetLoop(true, lo, hi);
      } else {
        view.updatePlayhead(e.getX());
      }
    }

    // Handle marker drag completion
    if (view.dragMode == TimelineView.DragMode.DRAG_MARKER) {
      if (view.draggingMarkerIdx >= 0 && view.draggingMarkerIdx < view.markers.size()) {
        java.util.Collections.sort(view.markers);
      }
      view.draggingMarkerIdx = -1;
    }

    // Reset all drag state
    view.dragMode = TimelineView.DragMode.NONE;
    view.draggingClip = null;
    view.dragSourceTrack = -1;
    view.resizeClip = null;
    view.resizeTrackIdx = -1;
    view.creatingTrackIdx = -1;
    view.creatingAutoLaneIdx = -1;
    view.creatingClipRect = null;
  }

  private void completeDrag(MouseEvent e) {
    int scaleTimeRuler = Theme.getInstance().scale(TimelineView.TOTAL_RULER_HEIGHT);
    int targetTrackIdx = view.getTrackIdxAtY(e.getY() - scaleTimeRuler);
    targetTrackIdx = Math.max(0, Math.min(view.tracks.size() - 1, targetTrackIdx));

    float newStartTime = Math.max(0, e.getX() / view.getPixelsPerSecond());
    if (!e.isShiftDown()) {
      newStartTime = view.snapToGrid(newStartTime);
    }

    boolean isCopy = e.isAltDown();
    boolean isAlias = e.isControlDown();

    // Find clip index in source track before modifying
    TimelineView.TrackTimeline sourceTrack = view.tracks.get(view.dragSourceTrack);
    int clipIndex = sourceTrack.clips.indexOf(view.draggingClip);

    if (isCopy || isAlias) {
      // Restore original clip position
      view.draggingClip.startTime = view.dragOriginalStartTime;
      // Send to engine — it will deep-copy clip data and notify GUI with correct flags
      if (clipIndex >= 0) {
        BackendManager.getInstance()
            .copyTimelineClip(
                view.dragSourceTrack, clipIndex, newStartTime, targetTrackIdx, isAlias);
      }
    } else {
      if (targetTrackIdx != view.dragSourceTrack) {
        TimelineView.TrackTimeline targetTrack = view.tracks.get(targetTrackIdx);

        if (clipIndex >= 0) {
          sourceTrack.clips.remove(clipIndex);
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

      // Sync to engine
      if (clipIndex >= 0) {
        BackendManager.getInstance()
            .moveTimelineClip(view.dragSourceTrack, clipIndex, newStartTime, targetTrackIdx);
      }
    }

    view.updateContentSize();
    view.contentPanel.repaint();
  }

  private void completeResize(MouseEvent e) {
    float endTime = view.resizeClip.startTime + view.resizeClip.duration;
    if (!e.isShiftDown()) {
      endTime = view.snapToGrid(endTime);
    }
    float newDuration = Math.max(60.0f / view.bpm, endTime - view.resizeClip.startTime);
    view.resizeClip.duration = newDuration;
    float durationBeats = newDuration * (view.bpm / 60.0f);
    float trimStartBeats = view.resizeClip.trimStartSec * (view.bpm / 60.0f);

    TimelineView.TrackTimeline track = view.tracks.get(view.resizeTrackIdx);
    int clipIndex = track.clips.indexOf(view.resizeClip);
    if (clipIndex >= 0) {
      BackendManager.getInstance()
          .resizeTimelineClip(view.resizeTrackIdx, clipIndex, durationBeats, trimStartBeats);
    }
    view.contentPanel.repaint();
  }

  private void completeTrimLeft(MouseEvent e) {
    // Snap the left edge
    float leftEdge = view.resizeClip.startTime;
    if (!e.isShiftDown()) {
      leftEdge = view.snapToGrid(leftEdge);
    }
    float rightEdge = view.resizeClip.startTime + view.resizeClip.duration;
    float newDuration = Math.max(60.0f / view.bpm, rightEdge - leftEdge);
    view.resizeClip.startTime = rightEdge - newDuration;
    view.resizeClip.duration = newDuration;

    float durationBeats = newDuration * (view.bpm / 60.0f);
    float trimStartBeats = view.resizeClip.trimStartSec * (view.bpm / 60.0f);

    TimelineView.TrackTimeline track = view.tracks.get(view.resizeTrackIdx);
    int clipIndex = track.clips.indexOf(view.resizeClip);
    if (clipIndex >= 0) {
      // Send move FIRST so engine updates start_time before resize notification
      BackendManager.getInstance()
          .moveTimelineClip(
              view.resizeTrackIdx, clipIndex, view.resizeClip.startTime, view.resizeTrackIdx);
      BackendManager.getInstance()
          .resizeTimelineClip(view.resizeTrackIdx, clipIndex, durationBeats, trimStartBeats);
    }
    view.contentPanel.repaint();
  }

  private void completeCreation(MouseEvent e) {
    float endTime = Math.max(0, e.getX() / view.getPixelsPerSecond());
    if (!e.isShiftDown()) {
      endTime = view.snapToGrid(endTime);
    }
    float duration = endTime - view.creatingStartTime;

    if (duration > 0.1f) {
      float durationBeats = duration * (view.bpm / 60.0f);
      BackendManager.getInstance()
          .addTimelineClip(view.creatingTrackIdx, "", view.creatingStartTime, durationBeats);
    }
    view.contentPanel.repaint();
  }

  private void completeLoopExtend(MouseEvent e) {
    float endTime = view.resizeClip.startTime + view.resizeClip.duration;
    if (!e.isShiftDown()) {
      endTime = view.snapToGrid(endTime);
    }
    // Set loop interval from pre-drag duration, only if clip wasn't already looped
    if (!view.resizeClip.isLooped) {
      view.resizeClip.loopInterval = view.resizeOriginalDuration;
    }
    float minBeat = 60.0f / view.bpm;
    float newDuration = Math.max(minBeat, endTime - view.resizeClip.startTime);
    view.resizeClip.duration = newDuration;
    boolean shouldLoop =
        view.resizeClip.loopInterval > 0 && newDuration > view.resizeClip.loopInterval;
    view.resizeClip.isLooped = shouldLoop;
    float durationBeats = newDuration * (view.bpm / 60.0f);
    float trimStartBeats = view.resizeClip.trimStartSec * (view.bpm / 60.0f);

    TimelineView.TrackTimeline track = view.tracks.get(view.resizeTrackIdx);
    int clipIndex = track.clips.indexOf(view.resizeClip);
    if (clipIndex >= 0) {
      // Compute loop interval in beats from the pre-drag duration (in seconds)
      float loopIntervalBeats = view.resizeClip.loopInterval * (view.bpm / 60.0f);
      // Set loop FIRST so the resize notification has correct loop state
      BackendManager.getInstance()
          .setTimelineClipLoop(view.resizeTrackIdx, clipIndex, shouldLoop, loopIntervalBeats);
      BackendManager.getInstance()
          .resizeTimelineClip(view.resizeTrackIdx, clipIndex, durationBeats, trimStartBeats);
    }
    view.contentPanel.repaint();
  }

  // ─── Mouse drag ─────────────────────────────────────────────────────

  private void handleMouseDragged(MouseEvent e) {
    // Delegate automation drags
    if (autoHandler.handleDrag(e)) return;

    int scaleTimeRuler = Theme.getInstance().scale(TimelineView.TOTAL_RULER_HEIGHT);

    // Handle marker drag
    if (view.dragMode == TimelineView.DragMode.DRAG_MARKER
        && view.draggingMarkerIdx >= 0
        && view.draggingMarkerIdx < view.markers.size()) {
      float mouseTime = Math.max(0, e.getX() / view.getPixelsPerSecond());
      if (!e.isShiftDown()) mouseTime = view.snapToGrid(mouseTime);
      view.markers.get(view.draggingMarkerIdx).positionSec = mouseTime;
      view.contentPanel.repaint();
      return;
    }

    // Handle loop marker drag
    if (view.dragMode == TimelineView.DragMode.DRAG_LOOP_MARKER) {
      float mouseTime = Math.max(0, e.getX() / view.getPixelsPerSecond());
      if (!e.isShiftDown()) mouseTime = view.snapToGrid(mouseTime);
      if (view.draggingLoopEnd) {
        view.loopEndSec = Math.max(view.loopStartSec + 0.05f, mouseTime);
      } else {
        view.loopStartSec = Math.min(view.loopEndSec - 0.05f, Math.max(0, mouseTime));
      }
      view.contentPanel.repaint();
      return;
    }

    // Handle loop region drag on ruler
    if (view.dragMode == TimelineView.DragMode.DRAG_LOOP_REGION) {
      float mouseTime = Math.max(0, e.getX() / view.getPixelsPerSecond());
      if (!e.isShiftDown()) mouseTime = view.snapToGrid(mouseTime);
      view.loopStartSec = Math.min(view.loopDragStartSec, mouseTime);
      view.loopEndSec = Math.max(view.loopDragStartSec, mouseTime);
      view.contentPanel.repaint();
      return;
    }

    if (e.getY() < scaleTimeRuler
        && view.draggingClip == null
        && view.dragMode != TimelineView.DragMode.CREATE_CLIP
        && view.dragMode != TimelineView.DragMode.RESIZE_CLIP
        && view.dragMode != TimelineView.DragMode.LOOP_EXTEND
        && view.dragMode != TimelineView.DragMode.TRIM_LEFT) {
      view.updatePlayhead(e.getX());
    } else if (view.dragMode == TimelineView.DragMode.TRIM_LEFT && view.resizeClip != null) {
      // Head-trim: move left edge, shrink duration, increase trim offset
      float mouseTime = Math.max(0, e.getX() / view.getPixelsPerSecond());
      if (!e.isShiftDown()) {
        mouseTime = view.snapToGrid(mouseTime);
      }
      float rightEdge = view.resizeClip.startTime + view.resizeClip.duration;
      float minDuration = 60.0f / view.bpm; // 1 beat minimum
      mouseTime = Math.min(mouseTime, rightEdge - minDuration);
      float delta = mouseTime - view.resizeClip.startTime;
      view.resizeClip.startTime = mouseTime;
      view.resizeClip.duration = rightEdge - mouseTime;
      view.resizeClip.trimStartSec += delta;
      view.contentPanel.repaint();
    } else if (view.dragMode == TimelineView.DragMode.RESIZE_CLIP && view.resizeClip != null) {
      float mouseTime = Math.max(0, e.getX() / view.getPixelsPerSecond());
      float newDuration = Math.max(60.0f / view.bpm, mouseTime - view.resizeClip.startTime);
      view.resizeClip.duration = newDuration;
      view.contentPanel.repaint();
    } else if (view.dragMode == TimelineView.DragMode.LOOP_EXTEND && view.resizeClip != null) {
      float mouseTime = Math.max(0, e.getX() / view.getPixelsPerSecond());
      float minBeat = 60.0f / view.bpm;
      float newDuration = Math.max(minBeat, mouseTime - view.resizeClip.startTime);
      view.resizeClip.duration = newDuration;
      // Show/hide loop state live during drag
      view.resizeClip.isLooped =
          view.resizeClip.loopInterval > 0 && newDuration > view.resizeClip.loopInterval;
      view.contentPanel.repaint();
    } else if (view.dragMode == TimelineView.DragMode.CREATE_CLIP
        && view.creatingClipRect != null) {
      float endTime = Math.max(0, e.getX() / view.getPixelsPerSecond());
      if (!e.isShiftDown()) {
        endTime = view.snapToGrid(endTime);
      }
      view.creatingClipRect.duration = Math.max(0, endTime - view.creatingStartTime);
      view.contentPanel.repaint();
    } else if (view.draggingClip != null) {
      if (view.dragMode != TimelineView.DragMode.MOVE_CLIP) {
        int dx = e.getX() - view.dragStartX;
        int dy = e.getY() - view.dragStartY;
        if (Math.abs(dx) > 5 || Math.abs(dy) > 5) {
          view.dragMode = TimelineView.DragMode.MOVE_CLIP;
        }
      }

      if (view.dragMode == TimelineView.DragMode.MOVE_CLIP) {
        float newStartTime = Math.max(0, e.getX() / view.getPixelsPerSecond());
        if (!e.isShiftDown()) {
          newStartTime = view.snapToGrid(newStartTime);
        }
        view.draggingClip.startTime = newStartTime;
        view.dragCurrentY = e.getY();
        view.contentPanel.repaint();
      }
    }
  }

  // ─── Mouse move (cursor changes) ────────────────────────────────────

  private void handleMouseMoved(MouseEvent e) {
    int scaleMarkerLane = Theme.getInstance().scale(TimelineView.MARKER_LANE_HEIGHT);
    int scaleSeekBottom =
        scaleMarkerLane + Theme.getInstance().scale(TimelineView.TIME_RULER_HEIGHT);
    int scaleTotalRuler = Theme.getInstance().scale(TimelineView.TOTAL_RULER_HEIGHT);
    if (e.getY() < scaleTotalRuler) {
      if (e.getY() < scaleMarkerLane) {
        // In marker lane: check for marker hover
        int hitIdx = findMarkerAtX(e.getX());
        if (hitIdx >= 0) {
          view.contentPanel.setCursor(
              java.awt.Cursor.getPredefinedCursor(java.awt.Cursor.HAND_CURSOR));
          return;
        }
      } else if (e.getY() >= scaleSeekBottom) {
        // In loop lane: check for loop marker hover
        if (view.loopEndSec > view.loopStartSec) {
          float pps = view.getPixelsPerSecond();
          int hitThreshold = Theme.getInstance().scale(6);
          int startPx = (int) (view.loopStartSec * pps);
          int endPx = (int) (view.loopEndSec * pps);
          if (Math.abs(e.getX() - startPx) <= hitThreshold
              || Math.abs(e.getX() - endPx) <= hitThreshold) {
            view.contentPanel.setCursor(
                java.awt.Cursor.getPredefinedCursor(java.awt.Cursor.E_RESIZE_CURSOR));
            return;
          }
        }
      }
      view.contentPanel.setCursor(java.awt.Cursor.getDefaultCursor());
      return;
    }

    int trackIdx = view.getTrackIdxAtY(e.getY() - scaleTotalRuler);
    if (trackIdx < 0 || trackIdx >= view.tracks.size()) return;

    TimelineView.ClipRect clip = view.findClipAtPosition(trackIdx, e.getX());
    int clipY = scaleTotalRuler + Theme.getInstance().scale(view.getTrackY(trackIdx)) + 5;
    int clipH = Theme.getInstance().scale(view.getBaseTrackHeight(trackIdx)) - 10;
    if (clip != null && view.isNearLeftEdge(clip, e.getX())) {
      view.contentPanel.setCursor(
          java.awt.Cursor.getPredefinedCursor(java.awt.Cursor.W_RESIZE_CURSOR));
    } else if (clip != null && view.isNearRightEdge(clip, e.getX())) {
      // Right edge — top half = loop cursor, bottom half = resize cursor
      boolean topHalf = (e.getY() - clipY) <= clipH / 2;
      if (topHalf) {
        view.contentPanel.setCursor(
            java.awt.Cursor.getPredefinedCursor(java.awt.Cursor.N_RESIZE_CURSOR));
      } else {
        view.contentPanel.setCursor(
            java.awt.Cursor.getPredefinedCursor(java.awt.Cursor.E_RESIZE_CURSOR));
      }
    } else {
      view.contentPanel.setCursor(java.awt.Cursor.getDefaultCursor());
    }
  }

  // ─── Marker interaction helpers ──────────────────────────────────────

  private void handleMarkerPress(MouseEvent e, int scaleMarkerLane) {
    float pps = view.getPixelsPerSecond();
    int hitIdx = findMarkerAtX(e.getX());

    if (SwingUtilities.isRightMouseButton(e) && hitIdx >= 0) {
      showMarkerContextMenu(hitIdx, e);
      return;
    }

    if (e.getClickCount() == 2 && SwingUtilities.isLeftMouseButton(e)) {
      if (hitIdx >= 0) {
        editMarker(hitIdx);
      } else {
        // Create new marker at click position
        float clickTime = Math.max(0, e.getX() / pps);
        if (!e.isShiftDown()) clickTime = view.snapToGrid(clickTime);
        TimelineView.TimelineMarker m = new TimelineView.TimelineMarker("Marker", clickTime);
        MarkerDialog dlg =
            new MarkerDialog(
                (Frame) SwingUtilities.getWindowAncestor(view),
                "New Marker",
                m.name,
                m.positionSec,
                0,
                0,
                0,
                view.bpm);
        dlg.setVisible(true);
        if (dlg.isConfirmed()) {
          m.name = dlg.markerName;
          m.positionSec = dlg.positionSec;
          m.bpm = dlg.bpm;
          m.beatsPerBar = dlg.beatsPerBar;
          m.beatDenominator = dlg.beatDenominator;
          view.markers.add(m);
          java.util.Collections.sort(view.markers);
          view.contentPanel.repaint();
        }
      }
      return;
    }

    if (SwingUtilities.isLeftMouseButton(e) && hitIdx >= 0) {
      view.draggingMarkerIdx = hitIdx;
      view.dragMode = TimelineView.DragMode.DRAG_MARKER;
    }
  }

  private int findMarkerAtX(int mouseX) {
    float pps = view.getPixelsPerSecond();
    int hitThreshold = Theme.getInstance().scale(8);
    for (int i = 0; i < view.markers.size(); i++) {
      int mx = (int) (view.markers.get(i).positionSec * pps);
      if (Math.abs(mouseX - mx) <= hitThreshold) return i;
    }
    return -1;
  }

  private void editMarker(int idx) {
    TimelineView.TimelineMarker m = view.markers.get(idx);
    MarkerDialog dlg =
        new MarkerDialog(
            (Frame) SwingUtilities.getWindowAncestor(view),
            "Edit Marker",
            m.name,
            m.positionSec,
            m.bpm,
            m.beatsPerBar,
            m.beatDenominator,
            view.bpm);
    dlg.setVisible(true);
    if (dlg.isConfirmed()) {
      m.name = dlg.markerName;
      m.positionSec = dlg.positionSec;
      m.bpm = dlg.bpm;
      m.beatsPerBar = dlg.beatsPerBar;
      m.beatDenominator = dlg.beatDenominator;
      java.util.Collections.sort(view.markers);
      view.contentPanel.repaint();
    }
  }

  private void showMarkerContextMenu(int idx, MouseEvent e) {
    JPopupMenu menu = new JPopupMenu();
    JMenuItem editItem = new JMenuItem("Edit Marker");
    editItem.addActionListener(ev -> editMarker(idx));
    menu.add(editItem);
    JMenuItem deleteItem = new JMenuItem("Delete Marker");
    deleteItem.addActionListener(
        ev -> {
          view.markers.remove(idx);
          view.contentPanel.repaint();
        });
    menu.add(deleteItem);
    menu.show(view.contentPanel, e.getX(), e.getY());
  }
}
