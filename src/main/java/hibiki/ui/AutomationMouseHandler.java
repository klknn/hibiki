package hibiki.ui;

import hibiki.BackendManager;
import hibiki.pb.commands.*;
import hibiki.pb.core.AutomationPoint;
import hibiki.pb.core.EntityRef;
import java.awt.event.*;
import java.util.Comparator;
import java.util.List;
import javax.swing.*;

/**
 * Handles all mouse interactions within automation lanes: - Single click to drag points or tension
 * handles - Double click to add/remove points or rename clips - Right-click context menus for
 * points and clips - Clip move/resize/create within automation lanes
 *
 * <p>Extracted from TimelineMouseHandler to improve readability.
 */
class AutomationMouseHandler {
  private final TimelineView view;

  // Automation editing state
  private boolean editingAutomation = false;
  int autoTrackIdx = -1;
  int autoLaneIdx = -1;
  int autoClipIdx = -1;
  private int autoDragPointIdx = -1;

  // Tension handle drag state
  private boolean draggingTensionHandle = false;
  private int tensionHandleSegmentIdx = -1;

  AutomationMouseHandler(TimelineView view) {
    this.view = view;
  }

  /** Whether an automation drag is currently active. */
  boolean isEditing() {
    return editingAutomation;
  }

  // ─── Coordinate helpers ─────────────────────────────────────────────

  /**
   * Check if a Y position falls within an automation lane sub-row. Returns the lane index
   * (0-based), or -1 if not in a lane.
   */
  int findAutomationLaneAtY(int trackIdx, int mouseY) {
    if (trackIdx < 0 || trackIdx >= view.tracks.size()) return -1;
    TimelineView.TrackTimeline track = view.tracks.get(trackIdx);
    if (!track.automationExpanded || track.automationLanes.isEmpty()) return -1;

    int scaleTimeRuler = Theme.getInstance().scale(TimelineView.TIME_RULER_HEIGHT);
    int trackTopY = scaleTimeRuler + Theme.getInstance().scale(view.getTrackY(trackIdx));
    int scaleBaseTrack = Theme.getInstance().scale(view.getBaseTrackHeight());
    int scaleAutoLane = Theme.getInstance().scale(view.getAutomationLaneHeight());

    int yInTrack = mouseY - trackTopY;
    if (yInTrack < scaleBaseTrack) return -1;

    int yInAutoArea = yInTrack - scaleBaseTrack;
    int laneIdx = yInAutoArea / scaleAutoLane;
    if (laneIdx >= 0 && laneIdx < track.automationLanes.size()) {
      return laneIdx;
    }
    return -1;
  }

  private float xToAutoBeats(int x) {
    float pps = view.getPixelsPerSecond();
    float timeSec = x / pps;
    float secondsPerBeat = 60.0f / view.bpm;
    return timeSec / secondsPerBeat;
  }

  private float snapAutoBeats(float beats, boolean shiftHeld) {
    if (shiftHeld) return beats;
    float secondsPerBeat = 60.0f / view.bpm;
    float gridSeconds = view.getGridSnapSeconds(view.getGridMode(), secondsPerBeat);
    if (gridSeconds <= 0) return beats;
    float gridBeats = gridSeconds / secondsPerBeat;
    return Math.round(beats / gridBeats) * gridBeats;
  }

  private float autoBeatsToX(float beats) {
    float secondsPerBeat = 60.0f / view.bpm;
    return beats * secondsPerBeat * view.getPixelsPerSecond();
  }

  private float yToAutoValue(int mouseY, int laneTopY, int laneHeight) {
    int pad = TimelineConstants.AUTOMATION_PAD;
    int drawH = laneHeight - 2 * pad;
    float val = 1.0f - (float) (mouseY - laneTopY - pad) / drawH;
    return Math.max(0, Math.min(1, val));
  }

  private int getAutoLaneTopY(int trackIdx, int laneIdx) {
    int scaleTimeRuler = Theme.getInstance().scale(TimelineView.TIME_RULER_HEIGHT);
    int trackTopY = scaleTimeRuler + Theme.getInstance().scale(view.getTrackY(trackIdx));
    int scaleBaseTrack = Theme.getInstance().scale(view.getBaseTrackHeight());
    int scaleAutoLane = Theme.getInstance().scale(view.getAutomationLaneHeight());
    return trackTopY + scaleBaseTrack + laneIdx * scaleAutoLane;
  }

  // ─── Hit testing ────────────────────────────────────────────────────

  private int[] findAutoPointAt(
      List<TimelineView.ClipRect> clips,
      int mx,
      int my,
      int laneTopY,
      int laneHeight,
      int threshold) {
    int pad = TimelineConstants.AUTOMATION_PAD;
    int drawH = laneHeight - 2 * pad;
    float secondsPerBeat = 60.0f / view.bpm;
    for (int c = 0; c < clips.size(); c++) {
      TimelineView.ClipRect clip = clips.get(c);
      for (int i = 0; i < clip.automationPoints.size(); i++) {
        AutomationEditor.AutoPoint p = clip.automationPoints.get(i);
        float px = autoBeatsToX(clip.startTime / secondsPerBeat + p.timeBeats);
        float py = laneTopY + pad + drawH - (p.value * drawH);
        if (Math.abs(mx - px) < threshold && Math.abs(my - py) < threshold) {
          return new int[] {c, i};
        }
      }
    }
    return null;
  }

  private int[] findTensionHandleAt(
      List<TimelineView.ClipRect> clips,
      int mx,
      int my,
      int laneTopY,
      int laneHeight,
      int threshold) {
    int pad = TimelineConstants.AUTOMATION_PAD;
    int drawH = laneHeight - 2 * pad;
    float secondsPerBeat = 60.0f / view.bpm;
    for (int c = 0; c < clips.size(); c++) {
      TimelineView.ClipRect clip = clips.get(c);
      if (clip.automationPoints.size() < 2) continue;
      for (int i = 0; i < clip.automationPoints.size() - 1; i++) {
        AutomationEditor.AutoPoint p0 = clip.automationPoints.get(i);
        AutomationEditor.AutoPoint p1 = clip.automationPoints.get(i + 1);
        float mx0 = autoBeatsToX(clip.startTime / secondsPerBeat + p0.timeBeats);
        float mx1 = autoBeatsToX(clip.startTime / secondsPerBeat + p1.timeBeats);
        float midX = (mx0 + mx1) / 2f;
        float t = 0.5f;
        float exponent = (float) Math.pow(2.0, p0.tension);
        float curvedT = (float) Math.pow(t, exponent);
        float midVal = p0.value + (p1.value - p0.value) * curvedT;
        float midY = laneTopY + pad + drawH - midVal * drawH;
        if (Math.abs(mx - midX) < threshold && Math.abs(my - midY) < threshold) {
          return new int[] {c, i};
        }
      }
    }
    return null;
  }

  // ─── IPC ────────────────────────────────────────────────────────────

  private void sendAutoUpdate(
      int trackIdx, int laneIdx, int clipIdx, List<AutomationEditor.AutoPoint> points) {
    TimelineView.TrackTimeline track = view.tracks.get(trackIdx);
    TimelineView.AutomationLaneData lane = track.automationLanes.get(laneIdx);
    AutomationCmd.Builder cmdBuilder =
        AutomationCmd.newBuilder()
            .setAction(AutomationCmd.Action.ACTION_UPDATE_POINTS)
            .setClipIndex(clipIdx)
            .setTarget(EntityRef.newBuilder().setTrackIndex(trackIdx).setLaneIndex(laneIdx));
    for (AutomationEditor.AutoPoint p : points) {
      cmdBuilder.addPoints(
          AutomationPoint.newBuilder()
              .setTimeBeats(p.timeBeats)
              .setValue(p.value)
              .setTension(p.tension));
    }
    BackendManager.getInstance()
        .sendRequest(Request.newBuilder().setAutomation(cmdBuilder).build());
  }

  // ─── Mouse press ────────────────────────────────────────────────────

  void handlePress(MouseEvent e, int trackIdx, int laneIdx) {
    TimelineView.TrackTimeline track = view.tracks.get(trackIdx);
    TimelineView.AutomationLaneData lane = track.automationLanes.get(laneIdx);
    int scaleAutoLane = Theme.getInstance().scale(view.getAutomationLaneHeight());
    int laneTopY = getAutoLaneTopY(trackIdx, laneIdx);
    float secondsPerBeat = 60.0f / view.bpm;

    if (SwingUtilities.isRightMouseButton(e)) {
      int[] ptInfo = findAutoPointAt(lane.clips, e.getX(), e.getY(), laneTopY, scaleAutoLane, 8);
      if (ptInfo != null) {
        showAutoPointContextMenu(e, trackIdx, laneIdx, ptInfo[0], ptInfo[1]);
      } else {
        JPopupMenu menu = new JPopupMenu();

        float clickTimeSec = e.getX() / view.getPixelsPerSecond();
        int clickedClipIdx = -1;
        for (int i = 0; i < lane.clips.size(); i++) {
          TimelineView.ClipRect c = lane.clips.get(i);
          float endSec = c.startTime + c.duration * secondsPerBeat;
          if (clickTimeSec >= c.startTime && clickTimeSec <= endSec) {
            clickedClipIdx = i;
            break;
          }
        }

        if (clickedClipIdx >= 0) {
          TimelineView.ClipRect clickedClip = lane.clips.get(clickedClipIdx);
          JMenuItem editorItem = new JMenuItem("Open Automation Editor");
          editorItem.addActionListener(
              ev -> {
                java.awt.Frame frame = (java.awt.Frame) SwingUtilities.getWindowAncestor(view);
                AutomationEditorDialog dlg =
                    new AutomationEditorDialog(
                        frame,
                        trackIdx,
                        laneIdx,
                        lane.paramName,
                        clickedClip.automationPoints,
                        view.bpm);
                dlg.setVisible(true);
              });
          menu.add(editorItem);
          menu.addSeparator();
        }

        JMenuItem addItem = new JMenuItem("Add Automation Clip");
        addItem.addActionListener(
            ev -> {
              float startTime = xToAutoBeats(e.getX()) * (60.0f / view.bpm);
              float durationBeats = 4.0f;
              BackendManager.getInstance()
                  .sendRequest(
                      Request.newBuilder()
                          .setAutomation(
                              AutomationCmd.newBuilder()
                                  .setAction(AutomationCmd.Action.ACTION_ADD_CLIP)
                                  .setTarget(
                                      EntityRef.newBuilder()
                                          .setTrackIndex(trackIdx)
                                          .setLaneIndex(laneIdx))
                                  .setStartTimeSec(startTime)
                                  .setDurationBeats(durationBeats))
                          .build());
            });
        menu.add(addItem);
        menu.show(view.contentPanel, e.getX(), e.getY());
      }
      return;
    }

    if (e.getClickCount() == 2) {
      int[] ptInfo = findAutoPointAt(lane.clips, e.getX(), e.getY(), laneTopY, scaleAutoLane, 8);
      if (ptInfo != null) {
        TimelineView.ClipRect clip = lane.clips.get(ptInfo[0]);
        clip.automationPoints.remove(ptInfo[1]);
        sendAutoUpdate(trackIdx, laneIdx, ptInfo[0], clip.automationPoints);
        view.contentPanel.repaint();
        return;
      }

      float clickTimeSec = e.getX() / view.getPixelsPerSecond();
      for (int i = 0; i < lane.clips.size(); i++) {
        TimelineView.ClipRect c = lane.clips.get(i);
        float endSec = c.startTime + c.duration * secondsPerBeat;
        if (clickTimeSec >= c.startTime && clickTimeSec <= endSec) {
          int pad = TimelineConstants.AUTOMATION_PAD;
          int drawY = laneTopY + pad;
          if (e.getY() >= drawY && e.getY() <= drawY + TimelineConstants.CLIP_HEADER_HEIGHT) {
            String newName =
                JOptionPane.showInputDialog(
                    view.contentPanel,
                    "Enter automation clip name:",
                    c.name != null ? c.name : "Automation");
            if (newName != null) {
              c.name = newName;
              BackendManager.getInstance().renameAutomationClip(trackIdx, laneIdx, i, newName);
              view.contentPanel.repaint();
            }
            return;
          }

          float localBeat = xToAutoBeats(e.getX()) - (c.startTime / secondsPerBeat);
          localBeat = Math.max(0, snapAutoBeats(localBeat, e.isShiftDown()));
          float val = yToAutoValue(e.getY(), laneTopY, scaleAutoLane);
          AutomationEditor.AutoPoint np = new AutomationEditor.AutoPoint(localBeat, val, 0.0f);
          c.automationPoints.add(np);
          c.automationPoints.sort(Comparator.comparingDouble(a -> a.timeBeats));
          sendAutoUpdate(trackIdx, laneIdx, i, c.automationPoints);
          view.contentPanel.repaint();
          return;
        }
      }
      return;
    }

    // Single Click: tension handle, point, or clip interactions
    int[] handleInfo =
        findTensionHandleAt(lane.clips, e.getX(), e.getY(), laneTopY, scaleAutoLane, 8);
    if (handleInfo != null) {
      editingAutomation = true;
      draggingTensionHandle = true;
      autoTrackIdx = trackIdx;
      autoLaneIdx = laneIdx;
      autoClipIdx = handleInfo[0];
      tensionHandleSegmentIdx = handleInfo[1];
      autoDragPointIdx = -1;
      return;
    }

    int[] ptInfo = findAutoPointAt(lane.clips, e.getX(), e.getY(), laneTopY, scaleAutoLane, 8);
    if (ptInfo != null) {
      editingAutomation = true;
      autoTrackIdx = trackIdx;
      autoLaneIdx = laneIdx;
      autoClipIdx = ptInfo[0];
      autoDragPointIdx = ptInfo[1];
      return;
    }

    // Clip edge resize or body move
    float clickTimeSec = e.getX() / view.getPixelsPerSecond();
    float edgeThresholdSec = 5.0f / view.getPixelsPerSecond();
    for (int i = 0; i < lane.clips.size(); i++) {
      TimelineView.ClipRect c = lane.clips.get(i);
      float endSec = c.startTime + c.duration * secondsPerBeat;
      if (clickTimeSec >= c.startTime - edgeThresholdSec
          && clickTimeSec <= endSec + edgeThresholdSec) {
        if (Math.abs(clickTimeSec - endSec) < edgeThresholdSec) {
          view.dragMode = TimelineView.DragMode.RESIZE_CLIP;
          view.resizeClip = c;
          view.resizeTrackIdx = trackIdx;
          view.resizeOriginalDuration = c.duration;
          autoTrackIdx = trackIdx;
          autoLaneIdx = laneIdx;
          autoClipIdx = i;
          return;
        }

        if (clickTimeSec >= c.startTime && clickTimeSec <= endSec) {
          view.dragMode = TimelineView.DragMode.MOVE_CLIP;
          view.draggingClip = c;
          view.dragSourceTrack = trackIdx;
          view.dragStartX = e.getX();
          view.dragStartY = e.getY();
          view.dragCurrentY = e.getY();
          view.dragOriginalStartTime = c.startTime;
          autoTrackIdx = trackIdx;
          autoLaneIdx = laneIdx;
          autoClipIdx = i;
          return;
        }
      }
    }

    // Empty lane → create clip
    view.dragMode = TimelineView.DragMode.CREATE_CLIP;
    view.creatingTrackIdx = trackIdx;
    view.creatingAutoLaneIdx = laneIdx;
    float snapTime = Math.max(0, e.getX() / view.getPixelsPerSecond());
    if (!e.isShiftDown())
      snapTime = view.snapToGrid(snapTime);
    view.creatingStartTime = snapTime;
    view.creatingClipRect = new TimelineView.ClipRect();
    view.creatingClipRect.startTime = snapTime;
    view.creatingClipRect.duration = 0;
    view.creatingClipRect.isAutomation = true;
  }

  // ─── Mouse drag ─────────────────────────────────────────────────────

  /** Returns true if this handler consumed the drag event. */
  boolean handleDrag(MouseEvent e) {
    if (editingAutomation && draggingTensionHandle && tensionHandleSegmentIdx >= 0) {
      TimelineView.TrackTimeline track = view.tracks.get(autoTrackIdx);
      TimelineView.AutomationLaneData lane = track.automationLanes.get(autoLaneIdx);
      TimelineView.ClipRect clip = lane.clips.get(autoClipIdx);
      if (tensionHandleSegmentIdx < clip.automationPoints.size() - 1) {
        int scaleAutoLane = Theme.getInstance().scale(view.getAutomationLaneHeight());
        int laneTopY = getAutoLaneTopY(autoTrackIdx, autoLaneIdx);
        AutomationEditor.AutoPoint p0 = clip.automationPoints.get(tensionHandleSegmentIdx);
        AutomationEditor.AutoPoint p1 = clip.automationPoints.get(tensionHandleSegmentIdx + 1);
        float midVal = (p0.value + p1.value) / 2f;
        float dragVal = yToAutoValue(e.getY(), laneTopY, scaleAutoLane);
        float diff = midVal - dragVal;
        float newTension = Math.max(-1f, Math.min(1f, diff * 4f));
        p0.tension = newTension;
        view.contentPanel.repaint();
      }
      return true;
    }

    if (editingAutomation && autoDragPointIdx >= 0) {
      TimelineView.TrackTimeline track = view.tracks.get(autoTrackIdx);
      TimelineView.AutomationLaneData lane = track.automationLanes.get(autoLaneIdx);
      TimelineView.ClipRect clip = lane.clips.get(autoClipIdx);
      if (autoDragPointIdx < clip.automationPoints.size()) {
        int scaleAutoLane = Theme.getInstance().scale(view.getAutomationLaneHeight());
        int laneTopY = getAutoLaneTopY(autoTrackIdx, autoLaneIdx);
        AutomationEditor.AutoPoint p = clip.automationPoints.get(autoDragPointIdx);
        float secondsPerBeat = 60.0f / view.bpm;
        float localBeat = xToAutoBeats(e.getX()) - (clip.startTime / secondsPerBeat);
        p.timeBeats = Math.max(0, snapAutoBeats(localBeat, e.isShiftDown()));
        p.value = yToAutoValue(e.getY(), laneTopY, scaleAutoLane);
        view.contentPanel.repaint();
      }
      return true;
    }

    return false;
  }

  // ─── Mouse release ──────────────────────────────────────────────────

  /** Returns true if this handler consumed the release event. */
  boolean handleRelease(MouseEvent e) {
    if (!editingAutomation) return false;

    if (autoTrackIdx >= 0 && autoLaneIdx >= 0 && autoClipIdx >= 0) {
      TimelineView.TrackTimeline track = view.tracks.get(autoTrackIdx);
      TimelineView.AutomationLaneData lane = track.automationLanes.get(autoLaneIdx);
      if (autoClipIdx < lane.clips.size()) {
        TimelineView.ClipRect clip = lane.clips.get(autoClipIdx);
        clip.automationPoints.sort(Comparator.comparingDouble(a -> a.timeBeats));
        sendAutoUpdate(autoTrackIdx, autoLaneIdx, autoClipIdx, clip.automationPoints);
      }
      view.contentPanel.repaint();
    }
    resetState();
    return true;
  }

  private void resetState() {
    editingAutomation = false;
    draggingTensionHandle = false;
    tensionHandleSegmentIdx = -1;
    autoTrackIdx = -1;
    autoLaneIdx = -1;
    autoClipIdx = -1;
    autoDragPointIdx = -1;
  }

  // ─── Context menu ───────────────────────────────────────────────────

  private void showAutoPointContextMenu(
      MouseEvent e, int trackIdx, int laneIdx, int clipIdx, int pointIdx) {
    TimelineView.TrackTimeline track = view.tracks.get(trackIdx);
    TimelineView.AutomationLaneData lane = track.automationLanes.get(laneIdx);
    TimelineView.ClipRect clip = lane.clips.get(clipIdx);

    JPopupMenu menu = new JPopupMenu();

    JMenuItem deleteItem = new JMenuItem("Delete Point");
    deleteItem.addActionListener(
        ev -> {
          clip.automationPoints.remove(pointIdx);
          sendAutoUpdate(trackIdx, laneIdx, clipIdx, clip.automationPoints);
          view.contentPanel.repaint();
        });
    menu.add(deleteItem);

    JMenu tensionMenu = new JMenu("Tension");
    float[] tensions = {-0.8f, -0.5f, -0.2f, 0.0f, 0.2f, 0.5f, 0.8f};
    String[] labels = {
      "Ease Out (strong)",
      "Ease Out",
      "Ease Out (light)",
      "Linear",
      "Ease In (light)",
      "Ease In",
      "Ease In (strong)"
    };
    for (int i = 0; i < tensions.length; i++) {
      float t = tensions[i];
      JMenuItem item = new JMenuItem(labels[i] + " (" + t + ")");
      item.addActionListener(
          ev -> {
            if (pointIdx < clip.automationPoints.size()) {
              clip.automationPoints.get(pointIdx).tension = t;
              sendAutoUpdate(trackIdx, laneIdx, clipIdx, clip.automationPoints);
              view.contentPanel.repaint();
            }
          });
      tensionMenu.add(item);
    }
    menu.add(tensionMenu);

    menu.show(view.contentPanel, e.getX(), e.getY());
  }
}
