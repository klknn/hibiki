package hibiki.ui;

import static org.junit.Assert.*;

import hibiki.pb.core.*;
import hibiki.pb.notifications.*;
import hibiki.pb.notifications.Notification;
import org.junit.Test;

/**
 * Tests for TimelineNotificationHandler — the IPC notification dispatch layer that routes engine
 * notifications to TimelineView state updates.
 */
public class TimelineNotificationHandlerTest {

  private TimelineView createView() {
    return new TimelineView();
  }

  // ── PlayheadInfo ──────────────────────────────────────────────

  @Test
  public void testHandlePlayhead_updatesPositionAndBpm() {
    TimelineView view = createView();
    Notification n =
        Notification.newBuilder()
            .setPlayheadInfo(
                PlayheadInfo.newBuilder()
                    .setPositionSec(5.5f)
                    .setBpm(140.0f)
                    .setTransportState(TransportState.TRANSPORT_STATE_STOPPED))
            .build();
    view.handleNotification(n);
    assertEquals(5.5f, view.playheadPos, 0.001f);
    assertEquals(140.0f, view.bpm, 0.001f);
    assertFalse(view.isPlaying);
  }

  @Test
  public void testHandlePlayhead_setsIsPlayingOnPlay() {
    TimelineView view = createView();
    Notification n =
        Notification.newBuilder()
            .setPlayheadInfo(
                PlayheadInfo.newBuilder()
                    .setPositionSec(1.0f)
                    .setBpm(120.0f)
                    .setTransportState(TransportState.TRANSPORT_STATE_PLAYING))
            .build();
    view.handleNotification(n);
    assertTrue(view.isPlaying);
  }

  @Test
  public void testHandlePlayhead_setsIsPlayingOnRecording() {
    TimelineView view = createView();
    Notification n =
        Notification.newBuilder()
            .setPlayheadInfo(
                PlayheadInfo.newBuilder()
                    .setPositionSec(0.0f)
                    .setBpm(120.0f)
                    .setTransportState(TransportState.TRANSPORT_STATE_RECORDING))
            .build();
    view.handleNotification(n);
    assertTrue(view.isPlaying);
  }

  @Test
  public void testHandlePlayhead_updatesLoop() {
    TimelineView view = createView();
    Notification n =
        Notification.newBuilder()
            .setPlayheadInfo(
                PlayheadInfo.newBuilder()
                    .setPositionSec(0.0f)
                    .setBpm(120.0f)
                    .setLoopEnabled(true)
                    .setLoopStart(2.0f)
                    .setLoopEnd(6.0f)
                    .setTransportState(TransportState.TRANSPORT_STATE_STOPPED))
            .build();
    view.handleNotification(n);
    assertTrue(view.loopEnabled);
    assertEquals(2.0f, view.loopStartSec, 0.001f);
    assertEquals(6.0f, view.loopEndSec, 0.001f);
  }

  @Test
  public void testHandlePlayhead_doesNotOverwriteLoopDuringDrag() {
    TimelineView view = createView();
    view.loopStartSec = 1.0f;
    view.loopEndSec = 3.0f;
    view.dragMode = TimelineView.DragMode.DRAG_LOOP_REGION;

    Notification n =
        Notification.newBuilder()
            .setPlayheadInfo(
                PlayheadInfo.newBuilder()
                    .setPositionSec(0.0f)
                    .setBpm(120.0f)
                    .setLoopEnabled(true)
                    .setLoopStart(10.0f)
                    .setLoopEnd(20.0f)
                    .setTransportState(TransportState.TRANSPORT_STATE_STOPPED))
            .build();
    view.handleNotification(n);
    // Should NOT have been overwritten
    assertEquals(1.0f, view.loopStartSec, 0.001f);
    assertEquals(3.0f, view.loopEndSec, 0.001f);
  }

  @Test
  public void testHandlePlayhead_doesNotOverwriteLoopDuringMarkerDrag() {
    TimelineView view = createView();
    view.loopStartSec = 1.0f;
    view.loopEndSec = 3.0f;
    view.dragMode = TimelineView.DragMode.DRAG_LOOP_MARKER;

    Notification n =
        Notification.newBuilder()
            .setPlayheadInfo(
                PlayheadInfo.newBuilder()
                    .setPositionSec(0.0f)
                    .setBpm(120.0f)
                    .setLoopEnabled(true)
                    .setLoopStart(10.0f)
                    .setLoopEnd(20.0f)
                    .setTransportState(TransportState.TRANSPORT_STATE_STOPPED))
            .build();
    view.handleNotification(n);
    // Should NOT have been overwritten
    assertEquals(1.0f, view.loopStartSec, 0.001f);
    assertEquals(3.0f, view.loopEndSec, 0.001f);
  }

  // ── TimelineClipInfo ──────────────────────────────────────────

  @Test
  public void testHandleTimelineClip_addsClipToTrack() {
    TimelineView view = createView();
    Notification n =
        Notification.newBuilder()
            .setTimelineClipInfo(
                TimelineClipInfo.newBuilder()
                    .setTrackIndex(0)
                    .setClipIndex(0)
                    .setName("beat.wav")
                    .setPath("/audio/beat.wav")
                    .setStartTime(2.0f)
                    .setDuration(4.0f))
            .build();
    view.handleNotification(n);

    assertEquals(1, view.tracks.get(0).clips.size());
    TimelineView.ClipRect clip = view.tracks.get(0).clips.get(0);
    assertEquals("beat.wav", clip.name);
    assertEquals("/audio/beat.wav", clip.path);
    assertEquals(2.0f, clip.startTime, 0.001f);
    assertEquals(4.0f, clip.duration, 0.001f);
  }

  @Test
  public void testHandleTimelineClip_expandsTrackListIfNeeded() {
    TimelineView view = createView();
    int initialSize = view.tracks.size();
    // Send clip for a track beyond the current list
    Notification n =
        Notification.newBuilder()
            .setTimelineClipInfo(
                TimelineClipInfo.newBuilder()
                    .setTrackIndex(initialSize + 3)
                    .setClipIndex(0)
                    .setName("clip")
                    .setStartTime(0)
                    .setDuration(1))
            .build();
    view.handleNotification(n);
    assertTrue(view.tracks.size() > initialSize);
    assertEquals(1, view.tracks.get(initialSize + 3).clips.size());
  }

  @Test
  public void testHandleTimelineClip_updatesExistingClip() {
    TimelineView view = createView();
    // Add initial clip
    Notification n1 =
        Notification.newBuilder()
            .setTimelineClipInfo(
                TimelineClipInfo.newBuilder()
                    .setTrackIndex(0)
                    .setClipIndex(0)
                    .setName("original")
                    .setStartTime(1.0f)
                    .setDuration(2.0f))
            .build();
    view.handleNotification(n1);

    // Update the same clip
    Notification n2 =
        Notification.newBuilder()
            .setTimelineClipInfo(
                TimelineClipInfo.newBuilder()
                    .setTrackIndex(0)
                    .setClipIndex(0)
                    .setName("updated")
                    .setStartTime(3.0f)
                    .setDuration(5.0f))
            .build();
    view.handleNotification(n2);

    // Should still be 1 clip, but updated
    assertEquals(1, view.tracks.get(0).clips.size());
    assertEquals("updated", view.tracks.get(0).clips.get(0).name);
    assertEquals(3.0f, view.tracks.get(0).clips.get(0).startTime, 0.001f);
  }

  @Test
  public void testHandleTimelineClip_loopedClip() {
    TimelineView view = createView();
    Notification n =
        Notification.newBuilder()
            .setTimelineClipInfo(
                TimelineClipInfo.newBuilder()
                    .setTrackIndex(0)
                    .setClipIndex(0)
                    .setName("loop")
                    .setStartTime(0)
                    .setDuration(8.0f)
                    .setIsLooped(true)
                    .setLoopInterval(2.0f))
            .build();
    view.handleNotification(n);
    TimelineView.ClipRect clip = view.tracks.get(0).clips.get(0);
    assertTrue(clip.isLooped);
    assertEquals(2.0f, clip.loopInterval, 0.001f);
  }

  @Test
  public void testHandleTimelineClip_aliasClip() {
    TimelineView view = createView();
    Notification n =
        Notification.newBuilder()
            .setTimelineClipInfo(
                TimelineClipInfo.newBuilder()
                    .setTrackIndex(0)
                    .setClipIndex(1)
                    .setName("alias")
                    .setStartTime(4.0f)
                    .setDuration(2.0f)
                    .setAliasSource(0))
            .build();
    view.handleNotification(n);
    TimelineView.ClipRect clip = view.tracks.get(0).clips.get(0);
    assertTrue(clip.isAlias);
    assertEquals(0, clip.aliasSourceIndex);
  }

  @Test
  public void testHandleTimelineClip_withWaveform() {
    TimelineView view = createView();
    Notification n =
        Notification.newBuilder()
            .setTimelineClipInfo(
                TimelineClipInfo.newBuilder()
                    .setTrackIndex(0)
                    .setClipIndex(0)
                    .setName("audio")
                    .setStartTime(0)
                    .setDuration(1.0f)
                    .addWaveform(0.1f)
                    .addWaveform(0.5f)
                    .addWaveform(0.9f))
            .build();
    view.handleNotification(n);
    TimelineView.ClipRect clip = view.tracks.get(0).clips.get(0);
    assertNotNull(clip.waveform);
    assertEquals(3, clip.waveform.length);
    assertEquals(0.5f, clip.waveform[1], 0.001f);
  }

  // ── ParamList ─────────────────────────────────────────────────

  @Test
  public void testHandleParamList_setsPluginName() {
    TimelineView view = createView();
    Notification n =
        Notification.newBuilder()
            .setParamList(
                ParamList.newBuilder()
                    .setTrackIndex(0)
                    .setPluginIndex(0)
                    .setPluginName("Dexed")
                    .setIsInstrument(true))
            .build();
    view.handleNotification(n);
    assertEquals("Dexed", view.tracks.get(0).pluginName);
    assertTrue(view.tracks.get(0).isInstrument);
  }

  @Test
  public void testHandleParamList_clearsPluginOnEmptyName() {
    TimelineView view = createView();
    // First, set a plugin
    view.tracks.get(0).pluginName = "Dexed";
    view.tracks.get(0).isInstrument = true;

    // Then send empty name at plugin index 0 → clear
    Notification n =
        Notification.newBuilder()
            .setParamList(
                ParamList.newBuilder().setTrackIndex(0).setPluginIndex(0).setPluginName(""))
            .build();
    view.handleNotification(n);
    assertNull(view.tracks.get(0).pluginName);
    assertFalse(view.tracks.get(0).isInstrument);
  }

  @Test
  public void testHandleParamList_expandsTrackList() {
    TimelineView view = createView();
    int initialSize = view.tracks.size();
    Notification n =
        Notification.newBuilder()
            .setParamList(
                ParamList.newBuilder()
                    .setTrackIndex(initialSize + 2)
                    .setPluginIndex(0)
                    .setPluginName("Synth"))
            .build();
    view.handleNotification(n);
    assertTrue(view.tracks.size() > initialSize);
    assertEquals("Synth", view.tracks.get(initialSize + 2).pluginName);
  }

  // ── ClearProject ──────────────────────────────────────────────

  @Test
  public void testHandleClearProject_clearsAllTracks() {
    TimelineView view = createView();
    // Add some data
    view.tracks.get(0).pluginName = "Dexed";
    view.tracks.get(0).customName = "Lead";
    view.tracks.get(0).isInstrument = true;
    view.tracks.get(0).clips.add(new TimelineView.ClipRect());

    Notification n = Notification.newBuilder().setClearProject(ClearProject.newBuilder()).build();
    view.handleNotification(n);

    assertNull(view.tracks.get(0).pluginName);
    assertNull(view.tracks.get(0).customName);
    assertFalse(view.tracks.get(0).isInstrument);
    assertTrue(view.tracks.get(0).clips.isEmpty());
    assertTrue(view.tracks.get(0).clipMap.isEmpty());
    assertTrue(view.tracks.get(0).automationLanes.isEmpty());
  }

  // ── TrackInfo ─────────────────────────────────────────────────

  @Test
  public void testHandleTrackInfo_setsCustomName() {
    TimelineView view = createView();
    Notification n =
        Notification.newBuilder()
            .setTrackInfo(TrackInfo.newBuilder().setTrackIndex(0).setName("Bass"))
            .build();
    view.handleNotification(n);
    assertEquals("Bass", view.tracks.get(0).customName);
  }

  @Test
  public void testHandleTrackInfo_emptyNameClearsCustomName() {
    TimelineView view = createView();
    view.tracks.get(0).customName = "OldName";
    Notification n =
        Notification.newBuilder()
            .setTrackInfo(TrackInfo.newBuilder().setTrackIndex(0).setName(""))
            .build();
    view.handleNotification(n);
    assertNull(view.tracks.get(0).customName);
  }

  @Test
  public void testHandleTrackInfo_expandsTrackList() {
    TimelineView view = createView();
    int initialSize = view.tracks.size();
    Notification n =
        Notification.newBuilder()
            .setTrackInfo(TrackInfo.newBuilder().setTrackIndex(initialSize + 1).setName("NewTrack"))
            .build();
    view.handleNotification(n);
    assertTrue(view.tracks.size() > initialSize);
    assertEquals("NewTrack", view.tracks.get(initialSize + 1).customName);
  }

  // ── AutomationLanesData ───────────────────────────────────────

  @Test
  public void testHandleAutomationLanes_addsLanes() {
    TimelineView view = createView();
    Notification n =
        Notification.newBuilder()
            .setAutomationLanesData(
                AutomationLanesData.newBuilder()
                    .setTrackIndex(0)
                    .addLanes(
                        AutomationLaneInfo.newBuilder()
                            .setLaneIndex(0)
                            .setPluginIndex(0)
                            .setParamId(42)
                            .setParamName("Cutoff")))
            .build();
    view.handleNotification(n);
    assertEquals(1, view.tracks.get(0).automationLanes.size());
    TimelineView.AutomationLaneData lane = view.tracks.get(0).automationLanes.get(0);
    assertEquals(0, lane.laneIndex);
    assertEquals(0, lane.pluginIndex);
    assertEquals(42, lane.paramId);
    assertEquals("Cutoff", lane.paramName);
  }

  @Test
  public void testHandleAutomationLanes_replacesExistingLanes() {
    TimelineView view = createView();
    // Add initial lane
    TimelineView.AutomationLaneData oldLane = new TimelineView.AutomationLaneData();
    oldLane.paramName = "Old";
    view.tracks.get(0).automationLanes.add(oldLane);

    // Send new lanes — should replace
    Notification n =
        Notification.newBuilder()
            .setAutomationLanesData(
                AutomationLanesData.newBuilder()
                    .setTrackIndex(0)
                    .addLanes(
                        AutomationLaneInfo.newBuilder()
                            .setLaneIndex(0)
                            .setPluginIndex(0)
                            .setParamId(1)
                            .setParamName("New1"))
                    .addLanes(
                        AutomationLaneInfo.newBuilder()
                            .setLaneIndex(1)
                            .setPluginIndex(0)
                            .setParamId(2)
                            .setParamName("New2")))
            .build();
    view.handleNotification(n);
    assertEquals(2, view.tracks.get(0).automationLanes.size());
    assertEquals("New1", view.tracks.get(0).automationLanes.get(0).paramName);
    assertEquals("New2", view.tracks.get(0).automationLanes.get(1).paramName);
  }

  // ── RecordingFinished ─────────────────────────────────────────

  @Test
  public void testHandleRecordingFinished_expandsTracksIfNeeded() {
    TimelineView view = createView();
    int initialSize = view.tracks.size();
    Notification n =
        Notification.newBuilder()
            .setRecordingFinished(
                RecordingFinished.newBuilder()
                    .setTrackIndex(initialSize + 2)
                    .setPath("/recorded.wav")
                    .setClipIndex(0))
            .build();
    view.handleNotification(n);
    assertTrue(view.tracks.size() > initialSize + 2);
  }

  // ── AudioInputList ────────────────────────────────────────────

  @Test
  public void testHandleAudioInputList_cacheDevices() {
    TimelineView view = createView();
    Notification n =
        Notification.newBuilder()
            .setAudioInputList(
                AudioInputList.newBuilder()
                    .addDevices(
                        AudioInputDevice.newBuilder()
                            .setId("hw:0")
                            .setName("Built-in Mic")
                            .setChannelCount(2))
                    .addDevices(
                        AudioInputDevice.newBuilder()
                            .setId("hw:1")
                            .setName("USB Interface")
                            .setChannelCount(8)))
            .build();
    view.handleNotification(n);
    assertEquals(2, TimelineNotificationHandler.cachedInputDevices.size());
    assertEquals("Built-in Mic", TimelineNotificationHandler.cachedInputDevices.get(0).getName());
    assertEquals("USB Interface", TimelineNotificationHandler.cachedInputDevices.get(1).getName());
  }

  @Test
  public void testHandleAudioInputList_replacesOldCache() {
    TimelineView view = createView();
    // First list
    Notification n1 =
        Notification.newBuilder()
            .setAudioInputList(
                AudioInputList.newBuilder()
                    .addDevices(AudioInputDevice.newBuilder().setId("old").setName("Old")))
            .build();
    view.handleNotification(n1);
    assertEquals(1, TimelineNotificationHandler.cachedInputDevices.size());

    // Second list replaces
    Notification n2 =
        Notification.newBuilder()
            .setAudioInputList(
                AudioInputList.newBuilder()
                    .addDevices(AudioInputDevice.newBuilder().setId("new1").setName("New1"))
                    .addDevices(AudioInputDevice.newBuilder().setId("new2").setName("New2")))
            .build();
    view.handleNotification(n2);
    assertEquals(2, TimelineNotificationHandler.cachedInputDevices.size());
    assertEquals("New1", TimelineNotificationHandler.cachedInputDevices.get(0).getName());
  }

  // ── MidiInputList ─────────────────────────────────────────────

  @Test
  public void testHandleMidiInputList_cacheDevices() {
    TimelineView view = createView();
    Notification n =
        Notification.newBuilder()
            .setMidiInputList(
                MidiInputList.newBuilder()
                    .addDevices(
                        MidiInputDevice.newBuilder()
                            .setId("midi:0")
                            .setName("MIDI Keyboard")
                            .setPortCount(1)))
            .build();
    view.handleNotification(n);
    assertEquals(1, TimelineNotificationHandler.cachedMidiDevices.size());
    assertEquals("MIDI Keyboard", TimelineNotificationHandler.cachedMidiDevices.get(0).getName());
  }

  // ── TrackLevels ───────────────────────────────────────────────

  @Test
  public void testHandleTrackLevels_updatesPeaks() {
    TimelineView view = createView();
    Notification n =
        Notification.newBuilder()
            .setTrackLevels(
                TrackLevels.newBuilder()
                    .addLevels(
                        TrackLevel.newBuilder().setTrackIndex(0).setPeakL(0.75f).setPeakR(0.5f))
                    .addLevels(
                        TrackLevel.newBuilder().setTrackIndex(1).setPeakL(0.3f).setPeakR(0.2f)))
            .build();
    view.handleNotification(n);
    assertEquals(0.75f, view.tracks.get(0).peakL, 0.001f);
    assertEquals(0.5f, view.tracks.get(0).peakR, 0.001f);
    assertEquals(0.3f, view.tracks.get(1).peakL, 0.001f);
    assertEquals(0.2f, view.tracks.get(1).peakR, 0.001f);
  }

  @Test
  public void testHandleTrackLevels_ignoresOutOfBoundsIndex() {
    TimelineView view = createView();
    int trackCount = view.tracks.size();
    Notification n =
        Notification.newBuilder()
            .setTrackLevels(
                TrackLevels.newBuilder()
                    .addLevels(
                        TrackLevel.newBuilder()
                            .setTrackIndex(trackCount + 10)
                            .setPeakL(1.0f)
                            .setPeakR(1.0f)))
            .build();
    // Should not throw
    view.handleNotification(n);
  }

  // ── Default/unhandled ─────────────────────────────────────────

  @Test
  public void testHandleNotification_unknownType_doesNotThrow() {
    TimelineView view = createView();
    // Build a notification with a type that TimelineNotificationHandler doesn't handle
    Notification n =
        Notification.newBuilder()
            .setLog(hibiki.pb.notifications.Log.newBuilder().setMessage("test log"))
            .build();
    // Should not throw
    view.handleNotification(n);
  }

  @Test
  public void testHandleNotification_multipleSequential() {
    TimelineView view = createView();
    // Play, clip, track info — all in sequence
    view.handleNotification(
        Notification.newBuilder()
            .setPlayheadInfo(
                PlayheadInfo.newBuilder()
                    .setPositionSec(1.0f)
                    .setBpm(128.0f)
                    .setTransportState(TransportState.TRANSPORT_STATE_PLAYING))
            .build());
    view.handleNotification(
        Notification.newBuilder()
            .setTimelineClipInfo(
                TimelineClipInfo.newBuilder()
                    .setTrackIndex(0)
                    .setClipIndex(0)
                    .setName("clip1")
                    .setStartTime(0)
                    .setDuration(4))
            .build());
    view.handleNotification(
        Notification.newBuilder()
            .setTrackInfo(TrackInfo.newBuilder().setTrackIndex(0).setName("Drums"))
            .build());

    assertTrue(view.isPlaying);
    assertEquals(128.0f, view.bpm, 0.001f);
    assertEquals(1, view.tracks.get(0).clips.size());
    assertEquals("Drums", view.tracks.get(0).customName);
  }
}
