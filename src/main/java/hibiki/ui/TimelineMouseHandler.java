package hibiki.ui;

import hibiki.BackendManager;
import hibiki.pb.commands.*;
import hibiki.pb.core.EntityRef;
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
    int scaleTimeRuler = Theme.getInstance().scale(TimelineView.TIME_RULER_HEIGHT);
    if (e.getY() < scaleTimeRuler) {
      view.updatePlayhead(e.getX());
      return;
    }

    int trackIdx = view.getTrackIdxAtY(e.getY() - scaleTimeRuler);
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
      if (clip != null && view.isNearRightEdge(clip, e.getX())) {
        view.dragMode = TimelineView.DragMode.RESIZE_CLIP;
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

    // Handle clip resize completion
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
    int scaleTimeRuler = Theme.getInstance().scale(TimelineView.TIME_RULER_HEIGHT);
    int targetTrackIdx = view.getTrackIdxAtY(e.getY() - scaleTimeRuler);
    targetTrackIdx = Math.max(0, Math.min(view.tracks.size() - 1, targetTrackIdx));

    float newStartTime = Math.max(0, e.getX() / view.getPixelsPerSecond());
    if (!e.isShiftDown()) {
      newStartTime = view.snapToBar(newStartTime);
    }

    boolean isCopy = e.isAltDown();

    // Find clip index in source track before modifying
    TimelineView.TrackTimeline sourceTrack = view.tracks.get(view.dragSourceTrack);
    int clipIndex = sourceTrack.clips.indexOf(view.draggingClip);

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
      // TODO: copy needs a separate backend command (addTimelineClip w/ data)
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
      endTime = view.snapToBar(endTime);
    }
    float newDuration = Math.max(60.0f / view.bpm, endTime - view.resizeClip.startTime);
    view.resizeClip.duration = newDuration;
    float durationBeats = newDuration * (view.bpm / 60.0f);

    TimelineView.TrackTimeline track = view.tracks.get(view.resizeTrackIdx);
    int clipIndex = track.clips.indexOf(view.resizeClip);
    if (clipIndex >= 0) {
      BackendManager.getInstance()
          .resizeTimelineClip(view.resizeTrackIdx, clipIndex, durationBeats);
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
      BackendManager.getInstance()
          .addTimelineClip(view.creatingTrackIdx, "", view.creatingStartTime, durationBeats);
    }
    view.contentPanel.repaint();
  }

  // ─── Mouse drag ─────────────────────────────────────────────────────

  private void handleMouseDragged(MouseEvent e) {
    // Delegate automation drags
    if (autoHandler.handleDrag(e)) return;

    int scaleTimeRuler = Theme.getInstance().scale(TimelineView.TIME_RULER_HEIGHT);
    if (e.getY() < scaleTimeRuler
        && view.draggingClip == null
        && view.dragMode != TimelineView.DragMode.CREATE_CLIP
        && view.dragMode != TimelineView.DragMode.RESIZE_CLIP) {
      view.updatePlayhead(e.getX());
    } else if (view.dragMode == TimelineView.DragMode.RESIZE_CLIP && view.resizeClip != null) {
      float mouseTime = Math.max(0, e.getX() / view.getPixelsPerSecond());
      float newDuration = Math.max(60.0f / view.bpm, mouseTime - view.resizeClip.startTime);
      view.resizeClip.duration = newDuration;
      view.contentPanel.repaint();
    } else if (view.dragMode == TimelineView.DragMode.CREATE_CLIP
        && view.creatingClipRect != null) {
      float endTime = Math.max(0, e.getX() / view.getPixelsPerSecond());
      if (!e.isShiftDown()) {
        endTime = view.snapToBar(endTime);
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
          newStartTime = view.snapToBar(newStartTime);
        }
        view.draggingClip.startTime = newStartTime;
        view.dragCurrentY = e.getY();
        view.contentPanel.repaint();
      }
    }
  }

  // ─── Mouse move (cursor changes) ────────────────────────────────────

  private void handleMouseMoved(MouseEvent e) {
    int scaleTimeRuler = Theme.getInstance().scale(TimelineView.TIME_RULER_HEIGHT);
    if (e.getY() < scaleTimeRuler) return;

    int trackIdx = view.getTrackIdxAtY(e.getY() - scaleTimeRuler);
    if (trackIdx < 0 || trackIdx >= view.tracks.size()) return;

    TimelineView.ClipRect clip = view.findClipAtPosition(trackIdx, e.getX());
    if (clip != null && view.isNearRightEdge(clip, e.getX())) {
      view.contentPanel.setCursor(
          java.awt.Cursor.getPredefinedCursor(java.awt.Cursor.E_RESIZE_CURSOR));
    } else {
      view.contentPanel.setCursor(java.awt.Cursor.getDefaultCursor());
    }
  }
}
