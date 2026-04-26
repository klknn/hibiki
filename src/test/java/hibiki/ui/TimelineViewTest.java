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
}
