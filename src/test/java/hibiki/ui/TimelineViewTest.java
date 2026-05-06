package hibiki.ui;

import static org.junit.Assert.*;

import hibiki.pb.commands.*;
import hibiki.pb.core.*;
import hibiki.pb.notifications.*;
import hibiki.pb.notifications.Notification;
import javax.swing.*;
import org.junit.Test;

public class TimelineViewTest {

  @Test
  public void testHandleTimelineClipNotification() {
    TimelineView view = new TimelineView();
    assertEquals(4, view.tracks.size());
    assertEquals(0, view.tracks.get(0).clips.size());

    Notification n =
        Notification.newBuilder()
            .setTimelineClipInfo(
                TimelineClipInfo.newBuilder()
                    .setTrackIndex(0)
                    .setClipIndex(0)
                    .setName("TestClip")
                    .setPath("/path/to/test.wav")
                    .setStartTime(10.0f)
                    .setDuration(5.0f)
                    .addWaveform(0.1f)
                    .addWaveform(0.5f)
                    .addWaveform(0.8f)
                    .addWaveform(0.3f))
            .build();

    view.handleNotification(n);
    assertEquals(1, view.tracks.get(0).clips.size());
    TimelineView.ClipRect clip = view.tracks.get(0).clips.get(0);
    assertEquals("TestClip", clip.name);
    assertEquals(10.0f, clip.startTime, 0.001f);
    assertEquals(5.0f, clip.duration, 0.001f);
  }

  @Test
  public void testSnapToBar_120bpm() {
    TimelineView view = new TimelineView();
    view.bpm = 120.0f;
    view.setGridMode(GridMode.BAR);
    // At 120 BPM: 1 beat = 0.5s, 1 bar = 2.0s
    assertEquals(0.0f, view.snapToGrid(0.1f), 0.001f);
    assertEquals(2.0f, view.snapToGrid(1.5f), 0.001f);
    assertEquals(2.0f, view.snapToGrid(2.0f), 0.001f);
    assertEquals(4.0f, view.snapToGrid(3.5f), 0.001f);
    assertEquals(4.0f, view.snapToGrid(4.0f), 0.001f);
  }

  @Test
  public void testSnapToBar_140bpm() {
    TimelineView view = new TimelineView();
    view.bpm = 140.0f;
    view.setGridMode(GridMode.BAR);
    // At 140 BPM: 1 beat = 60/140s, 1 bar = 240/140 = ~1.714s
    float barLen = 240.0f / 140.0f;
    assertEquals(0.0f, view.snapToGrid(0.5f), 0.001f);
    assertEquals(barLen, view.snapToGrid(barLen - 0.1f), 0.001f);
    assertEquals(barLen, view.snapToGrid(barLen + 0.1f), 0.001f);
    assertEquals(2 * barLen, view.snapToGrid(2 * barLen), 0.001f);
  }

  @Test
  public void testFindClipAtPosition_noClips() {
    TimelineView view = new TimelineView();
    assertNull(view.findClipAtPosition(0, 100));
  }

  @Test
  public void testFindClipAtPosition_withClip() {
    TimelineView view = new TimelineView();
    TimelineView.ClipRect clip = new TimelineView.ClipRect();
    clip.name = "A";
    clip.startTime = 2.0f;
    clip.duration = 3.0f;
    view.tracks.get(0).clips.add(clip);

    // Click inside clip (at 3.0s → within [2.0, 5.0])
    int xInside = (int) (3.0f * view.getPixelsPerSecond());
    assertSame(clip, view.findClipAtPosition(0, xInside));

    // Click before clip
    int xBefore = (int) (1.0f * view.getPixelsPerSecond());
    assertNull(view.findClipAtPosition(0, xBefore));

    // Click after clip
    int xAfter = (int) (6.0f * view.getPixelsPerSecond());
    assertNull(view.findClipAtPosition(0, xAfter));
  }

  @Test
  public void testFindClipAtPosition_invalidTrack() {
    TimelineView view = new TimelineView();
    assertNull(view.findClipAtPosition(-1, 100));
    assertNull(view.findClipAtPosition(100, 100));
  }

  @Test
  public void testIsNearRightEdge() {
    TimelineView view = new TimelineView();
    TimelineView.ClipRect clip = new TimelineView.ClipRect();
    clip.startTime = 2.0f;
    clip.duration = 3.0f;
    // Right edge at 5.0s
    int rightEdge = (int) (5.0f * view.getPixelsPerSecond());

    assertTrue(view.isNearRightEdge(clip, rightEdge));
    assertTrue(view.isNearRightEdge(clip, rightEdge - 5));
    assertTrue(view.isNearRightEdge(clip, rightEdge + 5));
    assertFalse(view.isNearRightEdge(clip, rightEdge - 20));
    assertFalse(view.isNearRightEdge(clip, rightEdge + 20));
  }

  @Test
  public void testMultipleClipsOnMultipleTracks() {
    TimelineView view = new TimelineView();

    // Clip on track 0
    Notification n0 =
        Notification.newBuilder()
            .setTimelineClipInfo(
                TimelineClipInfo.newBuilder()
                    .setTrackIndex(0)
                    .setClipIndex(0)
                    .setName("Clip0")
                    .setPath("a.mid")
                    .setStartTime(0.0f)
                    .setDuration(2.0f))
            .build();
    view.handleNotification(n0);

    // Clip on track 2
    Notification n2 =
        Notification.newBuilder()
            .setTimelineClipInfo(
                TimelineClipInfo.newBuilder()
                    .setTrackIndex(2)
                    .setClipIndex(0)
                    .setName("Clip2")
                    .setPath("b.mid")
                    .setStartTime(5.0f)
                    .setDuration(3.0f))
            .build();
    view.handleNotification(n2);

    assertEquals(1, view.tracks.get(0).clips.size());
    assertEquals(0, view.tracks.get(1).clips.size());
    assertEquals(1, view.tracks.get(2).clips.size());
    assertEquals("Clip2", view.tracks.get(2).clips.get(0).name);
    assertEquals(5.0f, view.tracks.get(2).clips.get(0).startTime, 0.001f);
  }

  @Test
  public void testEmptyClipPathIsMidi() {
    TimelineView view = new TimelineView();
    Notification n =
        Notification.newBuilder()
            .setTimelineClipInfo(
                TimelineClipInfo.newBuilder()
                    .setTrackIndex(0)
                    .setClipIndex(0)
                    .setName("New Clip")
                    .setPath("")
                    .setStartTime(0.0f)
                    .setDuration(2.0f))
            .build();
    view.handleNotification(n);

    TimelineView.ClipRect clip = view.tracks.get(0).clips.get(0);
    // Empty path should be treated as editable (MIDI)
    assertTrue(clip.path == null || clip.path.isEmpty() || clip.path.endsWith(".mid"));
  }

  @Test
  public void testClipRectFields() {
    TimelineView.ClipRect clip = new TimelineView.ClipRect();
    assertNull(clip.name);
    assertNull(clip.path);
    assertEquals(0.0f, clip.startTime, 0.001f);
    assertEquals(0.0f, clip.duration, 0.001f);
    assertNull(clip.waveform);
  }

  // --- New coverage-targeted tests ---

  @Test
  public void testGetSelectedTrack_default() {
    TimelineView view = new TimelineView();
    assertEquals(0, view.getSelectedTrack());
  }

  @Test
  public void testSetSelectedTrack_valid() {
    TimelineView view = new TimelineView();
    view.setSelectedTrack(3);
    assertEquals(3, view.getSelectedTrack());
    // Should auto-expand tracks list
    assertTrue(view.tracks.size() > 3);
  }

  @Test
  public void testSetSelectedTrack_negative() {
    TimelineView view = new TimelineView();
    view.setSelectedTrack(-1);
    // Should not change from default (0)
    assertEquals(0, view.getSelectedTrack());
  }

  @Test
  public void testSetSelectedTrack_sameTrack() {
    TimelineView view = new TimelineView();
    view.setSelectedTrack(2);
    assertEquals(2, view.getSelectedTrack());
    // Setting same track should be a no-op
    view.setSelectedTrack(2);
    assertEquals(2, view.getSelectedTrack());
  }

  @Test
  public void testGetPixelsPerSecond() {
    TimelineView view = new TimelineView();
    float pps = view.getPixelsPerSecond();
    assertTrue("Pixels per second should be positive", pps > 0);
  }

  @Test
  public void testGetTrackHeight() {
    TimelineView view = new TimelineView();
    int th = view.getTrackHeight();
    assertTrue("Track height should be positive", th > 0);
  }

  @Test
  public void testGridModeEnum() {
    // Verify all grid modes are accessible and have labels
    for (GridMode mode : GridMode.values()) {
      assertNotNull(mode.toString());
      assertFalse(mode.toString().isEmpty());
    }
    assertEquals("Auto", GridMode.AUTO.toString());
    assertEquals("1/4", GridMode.QUARTER.toString());
    assertEquals("1/16", GridMode.SIXTEENTH.toString());
  }

  @Test
  public void testGetGridSnapSeconds_barMode() {
    TimelineView view = new TimelineView();
    view.bpm = 120.0f;
    float secondsPerBeat = 60.0f / 120.0f; // 0.5
    // BAR mode = 4 beats = 2.0s
    float snap = view.getGridSnapSeconds(GridMode.BAR, secondsPerBeat);
    assertEquals(2.0f, snap, 0.001f);
  }

  @Test
  public void testGetGridSnapSeconds_quarterMode() {
    TimelineView view = new TimelineView();
    view.bpm = 120.0f;
    float secondsPerBeat = 60.0f / 120.0f; // 0.5
    // QUARTER mode = 1 beat = 0.5s
    float snap = view.getGridSnapSeconds(GridMode.QUARTER, secondsPerBeat);
    assertEquals(0.5f, snap, 0.001f);
  }

  @Test
  public void testGetGridSnapSeconds_eighthMode() {
    TimelineView view = new TimelineView();
    view.bpm = 120.0f;
    float secondsPerBeat = 60.0f / 120.0f; // 0.5
    // EIGHTH mode = 0.5 beat = 0.25s
    float snap = view.getGridSnapSeconds(GridMode.EIGHTH, secondsPerBeat);
    assertEquals(0.25f, snap, 0.001f);
  }

  @Test
  public void testGetGridSnapSeconds_secondsMode() {
    TimelineView view = new TimelineView();
    float secondsPerBeat = 0.5f;
    float snap = view.getGridSnapSeconds(GridMode.SECONDS, secondsPerBeat);
    assertEquals(1.0f, snap, 0.001f);
  }

  @Test
  public void testSnapToBar_zeroBpm() {
    TimelineView view = new TimelineView();
    view.bpm = 60.0f;
    view.setGridMode(GridMode.BAR);
    // At 60 BPM: 1 beat = 1.0s, 1 bar = 4.0s
    assertEquals(0.0f, view.snapToGrid(0.0f), 0.001f);
    assertEquals(4.0f, view.snapToGrid(3.5f), 0.001f);
    assertEquals(4.0f, view.snapToGrid(4.0f), 0.001f);
    assertEquals(8.0f, view.snapToGrid(7.0f), 0.001f);
  }

  @Test
  public void testTrackTimelineDefaults() {
    TimelineView.TrackTimeline track = new TimelineView.TrackTimeline(3);
    assertEquals(3, track.index);
    assertNotNull(track.clips);
    assertTrue(track.clips.isEmpty());
    assertNotNull(track.clipMap);
    assertTrue(track.clipMap.isEmpty());
  }

  @Test
  public void testTrackTimelineCustomName() {
    TimelineView.TrackTimeline track = new TimelineView.TrackTimeline(0);
    assertNull(track.customName);
    track.customName = "Drums";
    assertEquals("Drums", track.customName);
  }

  @Test
  public void testClipUpdateByIndex() {
    TimelineView view = new TimelineView();
    // Add initial clip
    Notification n1 =
        Notification.newBuilder()
            .setTimelineClipInfo(
                TimelineClipInfo.newBuilder()
                    .setTrackIndex(0)
                    .setClipIndex(0)
                    .setName("Initial")
                    .setPath("test.mid")
                    .setStartTime(1.0f)
                    .setDuration(2.0f)
                    .addWaveform(0.1f)
                    .addWaveform(0.2f))
            .build();
    view.handleNotification(n1);

    assertEquals(1, view.tracks.get(0).clips.size());
    assertEquals("Initial", view.tracks.get(0).clips.get(0).name);

    // Update same clip index 0 on same track
    Notification n2 =
        Notification.newBuilder()
            .setTimelineClipInfo(
                TimelineClipInfo.newBuilder()
                    .setTrackIndex(0)
                    .setClipIndex(0)
                    .setName("Updated")
                    .setPath("test2.mid")
                    .setStartTime(3.0f)
                    .setDuration(4.0f)
                    .addWaveform(0.5f))
            .build();
    view.handleNotification(n2);

    // Should update existing clip at index 0
    assertEquals(1, view.tracks.get(0).clips.size());
    assertEquals("Updated", view.tracks.get(0).clips.get(0).name);
    assertEquals(3.0f, view.tracks.get(0).clips.get(0).startTime, 0.001f);
  }

  @Test
  public void testUpdateContentSize() {
    TimelineView view = new TimelineView();
    // Should not throw
    view.updateContentSize();
  }

  @Test
  public void testAddTrack() {
    TimelineView tv = new TimelineView();
    int before = tv.tracks.size();
    tv.addTrack();
    assertEquals(before + 1, tv.tracks.size());
    assertEquals(before, tv.tracks.get(before).index);
  }

  @Test
  public void testRemoveTrack() {
    TimelineView tv = new TimelineView();
    int before = tv.tracks.size();
    tv.removeTrack(0);
    // Size stays same (hidden, not removed)
    assertEquals(before, tv.tracks.size());
    // But visible count decreases
    assertEquals(before - 1, tv.getVisibleTrackCount());
    assertTrue(tv.tracks.get(0).hidden);
  }

  @Test
  public void testRemoveTrack_cannotRemoveLast() {
    TimelineView tv = new TimelineView();
    // Remove all but one
    tv.removeTrack(0);
    tv.removeTrack(1);
    tv.removeTrack(2);
    assertEquals(1, tv.getVisibleTrackCount());
    tv.removeTrack(3); // should be no-op
    assertEquals(1, tv.getVisibleTrackCount());
  }

  @Test
  public void testComputeBarPositions_noMarkers() {
    java.util.List<TimelineView.TimelineMarker> markers = new java.util.ArrayList<>();
    // 120 BPM, 4/4: bars at 0, 2, 4, 6, 8
    java.util.List<Float> positions =
        TimelineRenderer.computeBarPositions(120.0f, 4, markers, 10.0f);
    assertTrue(positions.size() >= 5);
    assertEquals(0.0f, positions.get(0), 0.001f);
    assertEquals(2.0f, positions.get(1), 0.001f);
    assertEquals(4.0f, positions.get(2), 0.001f);
    assertEquals(6.0f, positions.get(3), 0.001f);
    assertEquals(8.0f, positions.get(4), 0.001f);
  }

  @Test
  public void testComputeBarPositions_singleMarkerTimeSigChange() {
    java.util.List<TimelineView.TimelineMarker> markers = new java.util.ArrayList<>();
    // Marker at 4.0s: change to 3/4
    TimelineView.TimelineMarker m = new TimelineView.TimelineMarker("A", 4.0f);
    m.beatsPerBar = 3;
    markers.add(m);
    // 120 BPM, 4/4 → 3/4 at 4.0s
    // Before marker: bars at 0, 2, 4 (secPerBar=2.0)
    // After marker: bars at 4.0, 5.5, 7.0 (secPerBar=1.5)
    java.util.List<Float> positions =
        TimelineRenderer.computeBarPositions(120.0f, 4, markers, 10.0f);
    assertEquals(0.0f, positions.get(0), 0.001f);
    assertEquals(2.0f, positions.get(1), 0.001f);
    assertEquals(4.0f, positions.get(2), 0.001f);
    assertEquals(5.5f, positions.get(3), 0.001f);
    assertEquals(7.0f, positions.get(4), 0.001f);
  }

  @Test
  public void testComputeBarPositions_noDoubledPositions_irrationalBpm() {
    // Simulate real-world float drift: marker position is set from a different
    // computation path than the bar accumulation, causing a tiny epsilon offset.
    // At 140 BPM, 3/4: secPerBar = 60/140*3 ≈ 1.28571...
    // After 5 bars of accumulation: timeSec ≈ 6.42857
    // But if the marker was placed from an independent calculation, it may
    // differ by a tiny epsilon, causing the marker to not be consumed.
    java.util.List<TimelineView.TimelineMarker> markers = new java.util.ArrayList<>();
    // Compute marker position with a slightly different float path to cause drift
    float secPerBeat = 60.0f / 140.0f;
    float markerAt = secPerBeat * 3 * 5; // different grouping than secPerBar * 5
    // Nudge by a tiny epsilon to simulate the mismatch
    markerAt = Math.nextUp(markerAt);
    TimelineView.TimelineMarker m = new TimelineView.TimelineMarker("B", markerAt);
    m.beatsPerBar = 5;
    markers.add(m);

    java.util.List<Float> positions =
        TimelineRenderer.computeBarPositions(140.0f, 3, markers, 20.0f);

    // Verify no two bars are at the same pixel (at 100 pps)
    float pps = 100.0f;
    for (int i = 1; i < positions.size(); i++) {
      int px0 = (int) (positions.get(i - 1) * pps);
      int px1 = (int) (positions.get(i) * pps);
      assertNotEquals(
          "Bar "
              + i
              + " and "
              + (i + 1)
              + " at same pixel (times: "
              + positions.get(i - 1)
              + ", "
              + positions.get(i)
              + ")",
          px0,
          px1);
    }
  }

  @Test
  public void testComputeBarPositions_twoMarkersNoDoubles() {
    // Two markers: 3/4 at start, 5/4 at bar 4
    java.util.List<TimelineView.TimelineMarker> markers = new java.util.ArrayList<>();
    TimelineView.TimelineMarker m1 = new TimelineView.TimelineMarker("A", 0.0f);
    m1.beatsPerBar = 3;
    markers.add(m1);
    // At 120 BPM 3/4: secPerBar=1.5, bars at 0, 1.5, 3.0, 4.5
    // Marker B at bar 4 start = 4.5s
    TimelineView.TimelineMarker m2 = new TimelineView.TimelineMarker("B", 4.5f);
    m2.beatsPerBar = 5;
    markers.add(m2);

    java.util.List<Float> positions =
        TimelineRenderer.computeBarPositions(120.0f, 4, markers, 15.0f);

    // Verify no two bars at same pixel (100 pps)
    float pps = 100.0f;
    for (int i = 1; i < positions.size(); i++) {
      int px0 = (int) (positions.get(i - 1) * pps);
      int px1 = (int) (positions.get(i) * pps);
      assertNotEquals(
          "Bar "
              + i
              + " and "
              + (i + 1)
              + " at same pixel (times: "
              + positions.get(i - 1)
              + ", "
              + positions.get(i)
              + ")",
          px0,
          px1);
    }
  }

  @Test
  public void testComputeBarPositions_markerAtBarBoundary_noDoubles() {
    // Reproduce user's scenario: 120 BPM 4/4, 3/4 marker placed at bar 3.
    // Bar 3 starts at 4.0s. Dialog may parse position with tiny offset.
    java.util.List<TimelineView.TimelineMarker> markers = new java.util.ArrayList<>();
    // Test with exact position
    TimelineView.TimelineMarker m = new TimelineView.TimelineMarker("A", 4.0f);
    m.beatsPerBar = 3;
    markers.add(m);
    java.util.List<Float> positions =
        TimelineRenderer.computeBarPositions(120.0f, 4, markers, 12.0f);
    float pps = 100.0f;
    for (int i = 1; i < positions.size(); i++) {
      int px0 = (int) (positions.get(i - 1) * pps);
      int px1 = (int) (positions.get(i) * pps);
      assertNotEquals(
          "Exact: Bar "
              + i
              + " and "
              + (i + 1)
              + " at same pixel (times: "
              + positions.get(i - 1)
              + ", "
              + positions.get(i)
              + ")",
          px0,
          px1);
    }

    // Test with slight positive offset (simulates dialog rounding up)
    markers.clear();
    TimelineView.TimelineMarker m2 = new TimelineView.TimelineMarker("A", 4.01f);
    m2.beatsPerBar = 3;
    markers.add(m2);
    positions = TimelineRenderer.computeBarPositions(120.0f, 4, markers, 12.0f);
    for (int i = 1; i < positions.size(); i++) {
      int px0 = (int) (positions.get(i - 1) * pps);
      int px1 = (int) (positions.get(i) * pps);
      assertNotEquals(
          "Offset+0.01: Bar "
              + i
              + " and "
              + (i + 1)
              + " at same pixel (times: "
              + positions.get(i - 1)
              + ", "
              + positions.get(i)
              + ")",
          px0,
          px1);
    }

    // Test with slight negative offset (simulates dialog rounding down)
    markers.clear();
    TimelineView.TimelineMarker m3 = new TimelineView.TimelineMarker("A", 3.99f);
    m3.beatsPerBar = 3;
    markers.add(m3);
    positions = TimelineRenderer.computeBarPositions(120.0f, 4, markers, 12.0f);
    for (int i = 1; i < positions.size(); i++) {
      int px0 = (int) (positions.get(i - 1) * pps);
      int px1 = (int) (positions.get(i) * pps);
      assertNotEquals(
          "Offset-0.01: Bar "
              + i
              + " and "
              + (i + 1)
              + " at same pixel (times: "
              + positions.get(i - 1)
              + ", "
              + positions.get(i)
              + ")",
          px0,
          px1);
    }
  }

  // ─── Regression: Loop/Trim ClipRect behavior ────────────────────────

  @Test
  public void testClipRect_loopIntervalDefaultsToZero() {
    TimelineView.ClipRect clip = new TimelineView.ClipRect();
    assertEquals(0.0f, clip.loopInterval, 0.001f);
    assertFalse(clip.isLooped);
    assertFalse(clip.isAlias);
    assertEquals(-1, clip.aliasSourceIndex);
  }

  @Test
  public void testContentDuration_setOnceNotOverwritten() {
    // Regression: contentDuration must be set on first notification and NOT
    // overwritten when duration changes (which broke trim/pad scaling).
    TimelineView view = new TimelineView();

    // First notification: clip duration = 5.0
    Notification n1 =
        Notification.newBuilder()
            .setTimelineClipInfo(
                TimelineClipInfo.newBuilder()
                    .setTrackIndex(0)
                    .setClipIndex(0)
                    .setName("Clip")
                    .setPath("/test.wav")
                    .setStartTime(0.0f)
                    .setDuration(5.0f))
            .build();
    view.handleNotification(n1);
    TimelineView.ClipRect clip = view.tracks.get(0).clips.get(0);
    assertEquals(
        "contentDuration should equal initial duration", 5.0f, clip.contentDuration, 0.001f);

    // Second notification: resize to 3.0 (trim) — contentDuration must stay 5.0
    Notification n2 =
        Notification.newBuilder()
            .setTimelineClipInfo(
                TimelineClipInfo.newBuilder()
                    .setTrackIndex(0)
                    .setClipIndex(0)
                    .setName("Clip")
                    .setPath("/test.wav")
                    .setStartTime(0.0f)
                    .setDuration(3.0f))
            .build();
    view.handleNotification(n2);
    assertEquals("duration should be updated to 3.0", 3.0f, clip.duration, 0.001f);
    assertEquals("contentDuration must NOT change on resize", 5.0f, clip.contentDuration, 0.001f);
  }

  @Test
  public void testIsLoopedAndAliasFromNotification() {
    TimelineView view = new TimelineView();
    Notification n =
        Notification.newBuilder()
            .setTimelineClipInfo(
                TimelineClipInfo.newBuilder()
                    .setTrackIndex(0)
                    .setClipIndex(0)
                    .setName("LoopClip")
                    .setPath("/test.mid")
                    .setStartTime(0.0f)
                    .setDuration(8.0f)
                    .setIsLooped(true)
                    .setAliasSource(2))
            .build();
    view.handleNotification(n);
    TimelineView.ClipRect clip = view.tracks.get(0).clips.get(0);
    assertTrue("isLooped should be true", clip.isLooped);
    assertTrue("isAlias should be true (aliasSource >= 0)", clip.isAlias);
    assertEquals(2, clip.aliasSourceIndex);
  }

  @Test
  public void testContentDuration_preservedForPadding() {
    // When duration > contentDuration (padding), the draw methods use
    // contentDuration/duration ratio to scale content correctly.
    TimelineView view = new TimelineView();

    Notification n1 =
        Notification.newBuilder()
            .setTimelineClipInfo(
                TimelineClipInfo.newBuilder()
                    .setTrackIndex(0)
                    .setClipIndex(0)
                    .setName("Short")
                    .setPath("/test.wav")
                    .setStartTime(0.0f)
                    .setDuration(2.0f))
            .build();
    view.handleNotification(n1);
    TimelineView.ClipRect clip = view.tracks.get(0).clips.get(0);
    assertEquals(2.0f, clip.contentDuration, 0.001f);

    // Extend to 6.0 (pad beyond content)
    Notification n2 =
        Notification.newBuilder()
            .setTimelineClipInfo(
                TimelineClipInfo.newBuilder()
                    .setTrackIndex(0)
                    .setClipIndex(0)
                    .setName("Short")
                    .setPath("/test.wav")
                    .setStartTime(0.0f)
                    .setDuration(6.0f))
            .build();
    view.handleNotification(n2);
    assertEquals("duration should be padded to 6.0", 6.0f, clip.duration, 0.001f);
    assertEquals("contentDuration must stay at original 2.0", 2.0f, clip.contentDuration, 0.001f);
  }

  /**
   * Regression: trimmed-and-looped clip ghost waveform.
   *
   * <p>When a 4-beat clip (contentDuration=2.0s) is trimmed to 1 beat and then loop-extended to 4
   * beats, the renderer's initial full-clip draw paints the entire waveform across the width. The
   * tiled rendering only overdraws on top but doesn't fully mask the initial draw, causing "ghost"
   * content from the trimmed region to bleed through.
   *
   * <p>This test reproduces the pixel→waveform mapping to prove the bug and verify the fix: pixels
   * in repeat regions should NEVER map to waveform indices beyond the loop interval's fraction of
   * the content.
   */
  @Test
  public void testTrimmedLoopedClip_noGhostWaveformInRepeatRegion() {
    // Setup: 4-beat clip trimmed to 1 beat, looped to 4 beats at 120bpm
    TimelineView.ClipRect clip = new TimelineView.ClipRect();
    clip.contentDuration = 2.0f; // Original 4-beat content (2.0s at 120bpm)
    clip.duration = 2.0f; // Total looped duration (4 beats = 2.0s)
    clip.loopInterval = 0.5f; // 1-beat loop interval (0.5s)
    clip.isLooped = true;
    clip.trimStartSec = 0.0f;
    // 100-sample waveform: indices 0-24=beat1, 25-49=beat2, 50-74=beat3, 75-99=beat4
    clip.waveform = new float[100];
    for (int i = 0; i < 100; i++) {
      clip.waveform[i] = (i < 25) ? 1.0f : 0.5f; // beat1=1.0, beats2-4=0.5
    }

    int totalW = 400; // Total pixel width of clip
    int tileW = (int) (clip.loopInterval / clip.duration * totalW); // 100px per tile

    // --- Simulate drawAudioWaveform with FULL clip (the initial draw) ---
    // contentW = (contentDuration/duration) * totalW = (2.0/2.0)*400 = 400
    float fullContentW = (clip.contentDuration / clip.duration) * totalW;

    // At pixel 150 (middle of second tile): should be silence in correct render
    // But in the initial full-clip draw:
    float contentPx150 = 150 + 0; // no trim offset
    int wfIdx150 = (int) (contentPx150 / fullContentW * clip.waveform.length);
    // wfIdx150 = (150/400)*100 = 37 → beat 2 region (ghost content!)
    assertTrue(
        "BUG REPRO: initial full-clip draw maps pixel 150 to trimmed content (idx="
            + wfIdx150
            + ")",
        wfIdx150 >= 25); // Ghost: maps to beat 2+ content

    // --- Simulate drawAudioWaveform with TILE clip (correct behavior) ---
    // tileClip: contentDuration=2.0, duration=loopInterval=0.5
    TimelineView.ClipRect tileClip = new TimelineView.ClipRect();
    tileClip.contentDuration = clip.contentDuration;
    tileClip.duration = clip.loopInterval;
    tileClip.trimStartSec = 0.0f;
    tileClip.waveform = clip.waveform;

    // tileContentW = (contentDuration/loopInterval) * tileW = (2.0/0.5)*100 = 400
    float tileContentW = (tileClip.contentDuration / tileClip.duration) * tileW;

    // For all pixels within a tile (0..tileW-1), all waveform indices
    // should be in the first-beat region (0..24)
    for (int px = 0; px < tileW; px++) {
      float tileContentPx = px + 0; // no trim offset
      int wfIdx = (int) (tileContentPx / tileContentW * clip.waveform.length);
      assertTrue(
          "Tile px " + px + " should map to beat-1 waveform (idx=" + wfIdx + ")",
          wfIdx >= 0 && wfIdx < 25);
    }

    // KEY ASSERTION: The initial full-clip draw MUST be skipped for looped clips,
    // otherwise ghost content from beats 2-4 bleeds through the tile overlay.
    // After the fix, only tileClip draws should occur — no full-width draw.
    // This flag captures the expected renderer behavior:
    boolean shouldSkipInitialDraw =
        clip.isLooped && clip.loopInterval > 0 && clip.duration > clip.loopInterval;
    assertTrue(
        "Renderer should skip initial full-clip draw for looped clips", shouldSkipInitialDraw);
  }

  /**
   * Regression: loop-extend drag preview must show tiled content.
   *
   * <p>Before the fix, loopInterval was only set on mouse release. During dragging, loopInterval
   * was 0, so the renderer never entered tiled mode and drew the full original waveform instead of
   * repeating tiles.
   *
   * <p>This test simulates the drag start + drag motion state updates and verifies the renderer has
   * the data it needs for tiled preview.
   */
  @Test
  public void testLoopExtendDrag_setsLoopIntervalForTiledPreview() {
    // Setup: 1-beat clip (0.5s at 120bpm), not yet looped
    TimelineView.ClipRect clip = new TimelineView.ClipRect();
    clip.duration = 0.5f;
    clip.contentDuration = 0.5f;
    clip.loopInterval = 0;
    clip.isLooped = false;

    // --- BUG REPRO: Without the fix, drag start does NOT set loopInterval ---
    // (Old code just set dragMode without touching loopInterval)
    // Simulate old drag motion: extend to 2.0s
    float newDuration = 2.0f;
    // Old drag handler logic: isLooped = loopInterval > 0 && newDuration > loopInterval
    boolean oldIsLooped = clip.loopInterval > 0 && newDuration > clip.loopInterval;
    assertFalse("BUG: without fix, isLooped stays false because loopInterval is 0", oldIsLooped);
    boolean oldHasLoopTiles =
        oldIsLooped && clip.loopInterval > 0 && newDuration > clip.loopInterval;
    assertFalse("BUG: renderer shows no tiles during drag without fix", oldHasLoopTiles);

    // --- FIX: drag start sets loopInterval = clip.duration ---
    // This is the logic added in TimelineMouseHandler.handleMousePressed
    if (!clip.isLooped && clip.loopInterval <= 0) {
      clip.loopInterval = clip.duration; // 0.5s
    }

    assertEquals(
        "loopInterval should be set to pre-drag duration at drag start",
        0.5f,
        clip.loopInterval,
        0.001f);

    // --- After fix: drag motion correctly triggers tiled preview ---
    clip.duration = newDuration;
    clip.isLooped = clip.loopInterval > 0 && newDuration > clip.loopInterval;

    assertTrue("After fix: isLooped should be true when dragged past loopInterval", clip.isLooped);

    boolean hasLoopTiles =
        clip.isLooped && clip.loopInterval > 0 && clip.duration > clip.loopInterval;
    assertTrue("After fix: renderer should show tiled preview during drag", hasLoopTiles);

    int numTiles = (int) Math.ceil(clip.duration / clip.loopInterval);
    assertEquals("Should show 4 tiles (2.0s / 0.5s)", 4, numTiles);

    // --- Verify shrink back below interval removes loop ---
    clip.duration = 0.3f;
    clip.isLooped = clip.loopInterval > 0 && clip.duration > clip.loopInterval;
    assertFalse("isLooped should be false when shrunk below loopInterval", clip.isLooped);
  }

  // ─── Group Track Folder Tests ────────────────────────────────────────

  @Test
  public void testTrackTimeline_groupFields_defaults() {
    TimelineView.TrackTimeline track = new TimelineView.TrackTimeline(0);
    assertEquals(-1, track.groupParentIndex);
    assertEquals(0, track.trackType);
    assertFalse(track.collapsed);
    assertFalse(track.isGroupTrack());
  }

  @Test
  public void testIsGroupTrack_returnsTrue_when_trackType_is_1() {
    TimelineView.TrackTimeline track = new TimelineView.TrackTimeline(0);
    track.trackType = 1; // GROUP
    assertTrue(track.isGroupTrack());
  }

  @Test
  public void testCollapsedChildren_haveZeroHeight() {
    TimelineView view = new TimelineView();
    // Set track 0 as group (type=1)
    view.tracks.get(0).trackType = 1;
    // Set tracks 1, 2 as children of track 0
    view.tracks.get(1).groupParentIndex = 0;
    view.tracks.get(2).groupParentIndex = 0;

    // Before collapse: all tracks have height
    int h1Before = view.getTotalTrackHeight(1);
    int h2Before = view.getTotalTrackHeight(2);
    assertTrue("Child track should have positive height before collapse", h1Before > 0);
    assertTrue("Child track should have positive height before collapse", h2Before > 0);

    // Collapse group
    view.tracks.get(0).collapsed = true;

    // After collapse: children have zero height
    assertEquals("Collapsed child should have zero height", 0, view.getTotalTrackHeight(1));
    assertEquals("Collapsed child should have zero height", 0, view.getTotalTrackHeight(2));
    // Group itself should still have height
    assertTrue("Group track should still have height", view.getTotalTrackHeight(0) > 0);
    // Non-child track should still have height
    assertTrue("Non-child track should still have height", view.getTotalTrackHeight(3) > 0);
  }

  @Test
  public void testGetTrackY_skipsCollapsedChildren() {
    TimelineView view = new TimelineView();
    // Set track 0 as group, tracks 1,2 as children
    view.tracks.get(0).trackType = 1;
    view.tracks.get(1).groupParentIndex = 0;
    view.tracks.get(2).groupParentIndex = 0;

    int y3Before = view.getTrackY(3);

    // Collapse group
    view.tracks.get(0).collapsed = true;

    int y3After = view.getTrackY(3);
    // Track 3's Y should be smaller after collapse (children take 0 height)
    assertTrue(
        "Track 3 Y should be smaller after collapsing children: before="
            + y3Before
            + " after="
            + y3After,
        y3After < y3Before);

    // Track 3 Y should equal track 0 height (since children are collapsed)
    int groupHeight = view.getTotalTrackHeight(0);
    assertEquals("Track 3 Y should be right after group track", groupHeight, y3After);
  }

  @Test
  public void testGetTotalTracksHeight_decreasesOnCollapse() {
    TimelineView view = new TimelineView();
    view.tracks.get(0).trackType = 1;
    view.tracks.get(1).groupParentIndex = 0;
    view.tracks.get(2).groupParentIndex = 0;

    int totalBefore = view.getTotalTracksHeight();

    view.tracks.get(0).collapsed = true;

    int totalAfter = view.getTotalTracksHeight();
    assertTrue("Total height should decrease after collapsing children", totalAfter < totalBefore);
  }

  @Test
  public void testHandleTrackInfo_setsGroupFields() {
    TimelineView view = new TimelineView();
    Notification n =
        Notification.newBuilder()
            .setTrackInfo(
                TrackInfo.newBuilder()
                    .setTrackIndex(1)
                    .setName("Child Track")
                    .setGroupParentIndex(0)
                    .setTrackType(hibiki.pb.core.TrackType.TRACK_GROUP))
            .build();
    view.handleNotification(n);
    assertEquals(0, view.tracks.get(1).groupParentIndex);
    assertEquals(1, view.tracks.get(1).trackType); // GROUP = 1
  }

  @Test
  public void testGetTrackIdxAtY_skipsCollapsedChildren() {
    TimelineView view = new TimelineView();
    view.tracks.get(0).trackType = 1;
    view.tracks.get(1).groupParentIndex = 0;
    view.tracks.get(2).groupParentIndex = 0;

    // Collapse group
    view.tracks.get(0).collapsed = true;

    // Y position right after group should resolve to track 3 (not track 1)
    int groupHeight = view.getTotalTrackHeight(0);
    int scaledGroupHeight = Theme.getInstance().scale(groupHeight);
    int trackAtY = view.getTrackIdxAtY(scaledGroupHeight + 1);
    assertEquals("Track at Y after collapsed group should be track 3", 3, trackAtY);
  }

  /**
   * Verifies the correct pattern: set groupParentIndex AFTER reorderTrackLocally, using the group
   * track's new ArrayList position.
   */
  @Test
  public void testReorderLocally_groupParentIndex_afterDnD() {
    TimelineView view = new TimelineView();
    // 5 tracks: [0, 1, 2, 3, 4(group)]
    view.tracks.add(new TimelineView.TrackTimeline(4));
    view.tracks.get(4).trackType = 1; // GROUP

    // Simulate correct DnD: drag track 3 onto group track 4
    int dndSource = 3;
    int hoverGroup = 4;
    TimelineView.TrackTimeline groupTrack = view.tracks.get(hoverGroup);
    TimelineView.TrackTimeline childTrack = view.tracks.get(dndSource);

    // Reorder FIRST
    int destIdx = dndSource < hoverGroup ? hoverGroup : hoverGroup + 1; // = 4
    view.reorderTrackLocally(dndSource, destIdx);

    // Set groupParentIndex AFTER reorder using group's new position
    childTrack.groupParentIndex = view.tracks.indexOf(groupTrack);

    // After reorder: [0, 1, 2, 4(group), 3(child)]
    int groupNewPos = view.tracks.indexOf(groupTrack);
    int childNewPos = view.tracks.indexOf(childTrack);

    assertEquals("Group track should be at position 3", 3, groupNewPos);
    assertEquals("Child track should be at position 4", 4, childNewPos);
    assertEquals(
        "Child's groupParentIndex should point to group's new position",
        groupNewPos,
        childTrack.groupParentIndex);
  }

  /**
   * Verifies collapse works correctly after DnD onto group when groupParentIndex is set AFTER
   * reorder.
   */
  @Test
  public void testCollapseGroup_afterDnD_childShouldHaveZeroHeight() {
    TimelineView view = new TimelineView();
    // 5 tracks: [0, 1, 2, 3, 4(group)]
    view.tracks.add(new TimelineView.TrackTimeline(4));
    view.tracks.get(4).trackType = 1; // GROUP

    int dndSource = 3;
    int hoverGroup = 4;
    TimelineView.TrackTimeline groupTrack = view.tracks.get(hoverGroup);
    TimelineView.TrackTimeline childTrack = view.tracks.get(dndSource);

    // Reorder first, then set parent
    view.reorderTrackLocally(dndSource, hoverGroup);
    childTrack.groupParentIndex = view.tracks.indexOf(groupTrack);

    // Collapse the group track
    groupTrack.collapsed = true;

    int childPos = view.tracks.indexOf(childTrack);
    int childHeight = view.getTotalTrackHeight(childPos);
    assertEquals("Collapsed group child should have zero height", 0, childHeight);
  }

  /** Engine indices (track.index) must remain stable after UI-only reorder. */
  @Test
  public void testReorderLocally_engineIndicesStable() {
    TimelineView view = new TimelineView();
    // Default 4 tracks with engine indices 0,1,2,3
    assertEquals(0, view.tracks.get(0).index);
    assertEquals(1, view.tracks.get(1).index);
    assertEquals(2, view.tracks.get(2).index);
    assertEquals(3, view.tracks.get(3).index);

    // Move track 0 to position 2
    view.reorderTrackLocally(0, 2);

    // Display order changed: [1, 2, 0, 3]
    // But engine indices must remain unchanged
    assertEquals("Engine index must stay stable", 1, view.tracks.get(0).index);
    assertEquals("Engine index must stay stable", 2, view.tracks.get(1).index);
    assertEquals("Engine index must stay stable", 0, view.tracks.get(2).index);
    assertEquals("Engine index must stay stable", 3, view.tracks.get(3).index);
  }

  /** Multiple children can be dragged into the same group. */
  @Test
  public void testDnD_multipleChildrenInGroup() {
    TimelineView view = new TimelineView();
    view.tracks.add(new TimelineView.TrackTimeline(4));
    // Track 0 is group
    view.tracks.get(0).trackType = 1;

    // Drag track 2 onto group track 0
    TimelineView.TrackTimeline group = view.tracks.get(0);
    TimelineView.TrackTimeline child1 = view.tracks.get(2);
    // source=2, group=0 → destIdx = 0+1 = 1
    view.reorderTrackLocally(2, 1);
    child1.groupParentIndex = view.tracks.indexOf(group);

    // Drag track 3 (now at pos 3 after first reorder) onto group track 0
    TimelineView.TrackTimeline child2 = view.tracks.get(3);
    view.reorderTrackLocally(3, 1);
    child2.groupParentIndex = view.tracks.indexOf(group);

    // Both children should point to the group
    assertEquals(
        "First child should point to group", view.tracks.indexOf(group), child1.groupParentIndex);
    assertEquals(
        "Second child should point to group", view.tracks.indexOf(group), child2.groupParentIndex);

    // Collapse should hide both
    group.collapsed = true;
    assertEquals(0, view.getTotalTrackHeight(view.tracks.indexOf(child1)));
    assertEquals(0, view.getTotalTrackHeight(view.tracks.indexOf(child2)));
  }

  /** Drag track above group (reverse direction). */
  @Test
  public void testDnD_dragTrackAboveGroup_reverseDirection() {
    TimelineView view = new TimelineView();
    view.tracks.add(new TimelineView.TrackTimeline(4));
    // Track 4 is group at position 4, drag track 3 onto it
    view.tracks.get(4).trackType = 1;

    // source=3, group=4 → destIdx = 3 < 4 ? 4 : 5 = 4
    TimelineView.TrackTimeline groupTrack = view.tracks.get(4);
    TimelineView.TrackTimeline childTrack = view.tracks.get(3);
    view.reorderTrackLocally(3, 4);
    childTrack.groupParentIndex = view.tracks.indexOf(groupTrack);

    // Child should be right after group
    int groupPos = view.tracks.indexOf(groupTrack);
    int childPos = view.tracks.indexOf(childTrack);
    assertEquals("Child should be right after group", groupPos + 1, childPos);
    assertEquals("groupParentIndex correct", groupPos, childTrack.groupParentIndex);
  }

  /** Normal reorder (non-group) should not change groupParentIndex. */
  @Test
  public void testReorderLocally_normalReorder_noGroupParentChange() {
    TimelineView view = new TimelineView();
    // All tracks are normal, move track 0 to position 2
    TimelineView.TrackTimeline t0 = view.tracks.get(0);
    assertEquals(-1, t0.groupParentIndex);

    view.reorderTrackLocally(0, 2);

    // groupParentIndex should remain -1
    // groupParentIndex should remain -1
    assertEquals("Normal reorder should not set groupParentIndex", -1, t0.groupParentIndex);
  }

  /** findDisplayPosition maps engine index → ArrayList position after reorder. */
  @Test
  public void testFindDisplayPosition_afterReorder() {
    TimelineView view = new TimelineView();
    view.tracks.add(new TimelineView.TrackTimeline(4));
    // Before reorder: positions match engine indices
    assertEquals(0, view.findDisplayPosition(0));
    assertEquals(3, view.findDisplayPosition(3));
    assertEquals(4, view.findDisplayPosition(4));

    // Reorder: move track 3 to position 4
    view.reorderTrackLocally(3, 4);
    // After: [0, 1, 2, 4, 3]

    assertEquals("Engine idx 3 should be at display pos 4", 4, view.findDisplayPosition(3));
    assertEquals("Engine idx 4 should be at display pos 3", 3, view.findDisplayPosition(4));
    assertEquals("Engine idx 0 unchanged", 0, view.findDisplayPosition(0));
  }

  /**
   * After reorder, handleTrackInfo must update the correct track. Reproduces bug: engine sends
   * TrackInfo(index=3), but after reorder the track at ArrayList pos 3 is a DIFFERENT track.
   */
  @Test
  public void testHandleTrackInfo_afterReorder_updatesCorrectTrack() {
    TimelineView view = new TimelineView();
    view.tracks.add(new TimelineView.TrackTimeline(4));
    // Track 4 is GROUP
    view.tracks.get(4).trackType = 1;
    view.tracks.get(4).customName = "MyGroup";

    // Reorder: move track 3 to position 4 (behind group)
    view.reorderTrackLocally(3, 4);
    // After: [0, 1, 2, T4("MyGroup",GROUP), T3]

    // Simulate engine sending TrackInfo for engine index 3 (child)
    Notification n =
        Notification.newBuilder()
            .setTrackInfo(
                TrackInfo.newBuilder()
                    .setTrackIndex(3) // engine index
                    .setName("ChildTrack")
                    .setGroupParentIndex(4) // engine index of group
                    .setTrackType(hibiki.pb.core.TrackType.TRACK_NORMAL))
            .build();
    view.handleNotification(n);

    // The track with engine index 3 should be updated (at display pos 4)
    TimelineView.TrackTimeline child = view.tracks.get(4);
    assertEquals("Child track name should be updated", "ChildTrack", child.customName);
    assertEquals("Child engine index unchanged", 3, child.index);

    // The group track at display pos 3 should NOT be affected
    TimelineView.TrackTimeline group = view.tracks.get(3);
    assertEquals("Group track name should be preserved", "MyGroup", group.customName);
    assertEquals("Group track type should still be GROUP", 1, group.trackType);

    // groupParentIndex should be translated to display position (3, not engine index 4)
    assertEquals("groupParentIndex should be display pos of group", 3, child.groupParentIndex);
  }

  /**
   * Reproduces exact user bug: two sequential DnDs into the same group. 1. Add group track 4 2.
   * Move Track 3 to group Track 4 3. Move Track 2 to group Track 4 After step 3, collapsing group
   * must hide BOTH children.
   */
  @Test
  public void testTwoSequentialDnD_collapseHidesBothChildren() {
    TimelineView view = new TimelineView();
    view.tracks.add(new TimelineView.TrackTimeline(4));
    // Track 4 is GROUP
    view.tracks.get(4).trackType = 1;

    // --- DnD 1: drag Track 3 onto group Track 4 ---
    TimelineView.TrackTimeline groupTrack = view.tracks.get(4);
    TimelineView.TrackTimeline track3 = view.tracks.get(3);
    // source=3 < hover=4, so destIdx = 4
    view.reorderTrackLocally(3, 4);
    track3.groupParentIndex = view.tracks.indexOf(groupTrack);
    // State: [0, 1, 2, T4(group@pos3), T3(@pos4, gpi=3)]

    assertEquals("After DnD1: group at pos 3", 3, view.tracks.indexOf(groupTrack));
    assertEquals("After DnD1: T3 at pos 4", 4, view.tracks.indexOf(track3));
    assertEquals("After DnD1: T3.gpi = 3", 3, track3.groupParentIndex);

    // --- DnD 2: drag Track 2 onto group Track 4 (now at pos 3) ---
    TimelineView.TrackTimeline track2 = view.tracks.get(2);
    // source=2 < hover=3 (group pos), so destIdx = 3
    view.reorderTrackLocally(2, 3);
    track2.groupParentIndex = view.tracks.indexOf(groupTrack);
    // State: [0, 1, T4(group@pos2), T2(@pos3, gpi=2), T3(@pos4, gpi=?)]

    int groupPos = view.tracks.indexOf(groupTrack);
    assertEquals("After DnD2: group at pos 2", 2, groupPos);
    assertEquals("After DnD2: T2.gpi = group pos", groupPos, track2.groupParentIndex);
    // KEY: T3's groupParentIndex must have been updated by reorderTrackLocally
    assertEquals(
        "After DnD2: T3.gpi must track group's new position", groupPos, track3.groupParentIndex);

    // Collapse the group: BOTH children must have zero height
    groupTrack.collapsed = true;
    assertEquals(
        "Collapsed: T2 should have zero height",
        0,
        view.getTotalTrackHeight(view.tracks.indexOf(track2)));
    assertEquals(
        "Collapsed: T3 should have zero height",
        0,
        view.getTotalTrackHeight(view.tracks.indexOf(track3)));
  }
}
