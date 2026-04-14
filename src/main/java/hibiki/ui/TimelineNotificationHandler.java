package hibiki.ui;

import hibiki.pb.notifications.Notification;

/**
 * Handles all IPC notification dispatch for TimelineView. Processes PlayheadInfo, TimelineClipInfo,
 * ParamList, ClearProject, TrackInfo, and AutomationLanesData.
 */
class TimelineNotificationHandler {
  private final TimelineView view;

  TimelineNotificationHandler(TimelineView view) {
    this.view = view;
  }

  void handleNotification(Notification n) {
    switch (n.getResponseCase()) {
      case PLAYHEAD_INFO:
        handlePlayhead(n);
        break;
      case TIMELINE_CLIP_INFO:
        handleTimelineClip(n);
        break;
      case PARAM_LIST:
        handleParamList(n);
        break;
      case CLEAR_PROJECT:
        handleClearProject();
        break;
      case TRACK_INFO:
        handleTrackInfo(n);
        break;
      case AUTOMATION_LANES_DATA:
        handleAutomationLanes(n);
        break;
      case RECORDING_FINISHED:
        handleRecordingFinished(n);
        break;
      case AUDIO_INPUT_LIST:
        handleAudioInputList(n);
        break;
      case MIDI_INPUT_LIST:
        handleMidiInputList(n);
        break;
      case TRACK_LEVELS:
        handleTrackLevels(n);
        break;
      default:
        break;
    }
  }

  private void handlePlayhead(Notification n) {
    var info = n.getPlayheadInfo();
    view.playheadPos = info.getPositionSec();
    view.bpm = info.getBpm();
    boolean wasPlaying = view.isPlaying;
    view.isPlaying =
        info.getTransportState() == hibiki.pb.core.TransportState.TRANSPORT_STATE_PLAYING
            || info.getTransportState() == hibiki.pb.core.TransportState.TRANSPORT_STATE_RECORDING;

    if (view.isPlaying && !wasPlaying && view.autoScroll) {
      int playheadX = (int) (view.playheadPos * view.getPixelsPerSecond());
      int scrollX = view.scrollPane.getHorizontalScrollBar().getValue();
      view.playheadScreenOffset = playheadX - scrollX;
    }
  }

  private void handleTimelineClip(Notification n) {
    var info = n.getTimelineClipInfo();
    int tidx = info.getTrackIndex();

    while (view.tracks.size() <= tidx) {
      view.tracks.add(new TimelineView.TrackTimeline(view.tracks.size()));
    }
    view.tracks.get(tidx).addOrUpdateClip(info);
    view.updateContentSize();
  }

  private void handleParamList(Notification n) {
    var paramList = n.getParamList();
    int tidx = paramList.getTrackIndex();
    while (view.tracks.size() <= tidx) {
      view.tracks.add(new TimelineView.TrackTimeline(view.tracks.size()));
    }
    if (!paramList.getPluginName().isEmpty()) {
      view.tracks.get(tidx).pluginName = paramList.getPluginName();
      view.tracks.get(tidx).isInstrument = paramList.getIsInstrument();
    }
  }

  private void handleClearProject() {
    for (TimelineView.TrackTimeline t : view.tracks) {
      t.clips.clear();
      t.clipMap.clear();
      t.pluginName = null;
      t.isInstrument = false;
      t.customName = null;
      t.automationLanes.clear();
    }
  }

  private void handleTrackInfo(Notification n) {
    var info = n.getTrackInfo();
    int tidx = info.getTrackIndex();
    while (view.tracks.size() <= tidx) {
      view.tracks.add(new TimelineView.TrackTimeline(view.tracks.size()));
    }
    String name = info.getName();
    view.tracks.get(tidx).customName = name.isEmpty() ? null : name;
    view.repaint();
    // Sync with SessionView
    if (SessionView.getInstance() != null && SessionView.getInstance().trackHeaders.length > tidx) {
      javax.swing.JLabel header = SessionView.getInstance().trackHeaders[tidx];
      if (header != null) {
        String displayName = view.tracks.get(tidx).getDisplayName();
        header.setText(tidx + " " + displayName);
      }
    }
  }

  private void handleAutomationLanes(Notification n) {
    var data = n.getAutomationLanesData();
    int tidx = data.getTrackIndex();
    while (view.tracks.size() <= tidx) {
      view.tracks.add(new TimelineView.TrackTimeline(view.tracks.size()));
    }
    TimelineView.TrackTimeline track = view.tracks.get(tidx);
    track.automationLanes.clear();
    for (int i = 0; i < data.getLanesCount(); i++) {
      var laneInfo = data.getLanes(i);
      TimelineView.AutomationLaneData laneData = new TimelineView.AutomationLaneData();
      laneData.laneIndex = laneInfo.getLaneIndex();
      laneData.pluginIndex = laneInfo.getPluginIndex();
      laneData.paramId = laneInfo.getParamId();
      laneData.paramName = laneInfo.getParamName();
      for (int j = 0; j < laneInfo.getClipsCount(); j++) {
        var clipInfo = laneInfo.getClips(j);
        TimelineView.ClipRect cr = new TimelineView.ClipRect();
        cr.startTime = (float) clipInfo.getStartTimeSec();
        cr.duration = (float) clipInfo.getClip().getDurationBeats();
        cr.name = clipInfo.getClip().getName();
        cr.isAutomation = true;
        for (int k = 0; k < clipInfo.getClip().getAutomationPointsCount(); k++) {
          var pt = clipInfo.getClip().getAutomationPoints(k);
          cr.automationPoints.add(
              new AutomationEditor.AutoPoint(pt.getTimeBeats(), pt.getValue(), pt.getTension()));
        }
        laneData.clips.add(cr);
      }
      track.automationLanes.add(laneData);
    }
    view.updateContentSize();
    view.repaint();
  }

  private void handleRecordingFinished(Notification n) {
    var info = n.getRecordingFinished();
    int tidx = info.getTrackIndex();
    while (view.tracks.size() <= tidx) {
      view.tracks.add(new TimelineView.TrackTimeline(view.tracks.size()));
    }
    // The clip is already added via TimelineClipInfo notification from the engine,
    // but update display to show the recording is complete
    view.updateContentSize();
    view.repaint();
  }

  /** Cached audio input device list for dialogs */
  static java.util.List<hibiki.pb.notifications.AudioInputDevice> cachedInputDevices =
      new java.util.ArrayList<>();

  private void handleAudioInputList(Notification n) {
    var list = n.getAudioInputList();
    cachedInputDevices.clear();
    for (int i = 0; i < list.getDevicesCount(); i++) {
      cachedInputDevices.add(list.getDevices(i));
    }
  }

  /** Cached MIDI input device list for dropdowns */
  static java.util.List<hibiki.pb.notifications.MidiInputDevice> cachedMidiDevices =
      new java.util.ArrayList<>();

  private void handleMidiInputList(Notification n) {
    var list = n.getMidiInputList();
    cachedMidiDevices.clear();
    for (int i = 0; i < list.getDevicesCount(); i++) {
      cachedMidiDevices.add(list.getDevices(i));
    }
  }

  private long lastLevelRepaintMs = 0;

  private void handleTrackLevels(Notification n) {
    var tl = n.getTrackLevels();
    for (int i = 0; i < tl.getLevelsCount(); i++) {
      var l = tl.getLevels(i);
      int tidx = l.getTrackIndex();
      if (tidx >= 0 && tidx < view.tracks.size()) {
        view.tracks.get(tidx).peakL = l.getPeakL();
        view.tracks.get(tidx).peakR = l.getPeakR();
      }
    }
    long now = System.currentTimeMillis();
    if (now - lastLevelRepaintMs > 33) { // throttle to ~30fps
      lastLevelRepaintMs = now;
      javax.swing.SwingUtilities.invokeLater(() -> view.repaintRowHeader());
    }
  }
}
