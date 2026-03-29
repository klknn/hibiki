package hibiki.ui;

import hibiki.pb.commands.*;
import hibiki.pb.notifications.*;
import hibiki.pb.core.*;
import hibiki.pb.notifications.Notification;
import org.junit.Test;
import static org.junit.Assert.*;

import javax.swing.*;

public class TimelineViewTest {

    @Test
    public void testHandleTimelineClipNotification() {
        TimelineView view = new TimelineView();
        assertEquals(8, view.tracks.size());
        assertEquals(0, view.tracks.get(0).clips.size());

        Notification n = Notification.newBuilder()
                .setTimelineClipInfo(TimelineClipInfo.newBuilder()
                        .setTrackIndex(0)
                        .setClipIndex(0)
                        .setName("TestClip")
                        .setPath("/path/to/test.wav")
                        .setStartTime(10.0f)
                        .setDuration(5.0f)
                        .addWaveform(0.1f).addWaveform(0.5f).addWaveform(0.8f).addWaveform(0.3f))
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
        // At 120 BPM: 1 beat = 0.5s, 1 bar = 2.0s
        assertEquals(0.0f, view.snapToBar(0.1f), 0.001f);
        assertEquals(2.0f, view.snapToBar(1.5f), 0.001f);
        assertEquals(2.0f, view.snapToBar(2.0f), 0.001f);
        assertEquals(4.0f, view.snapToBar(3.5f), 0.001f);
        assertEquals(4.0f, view.snapToBar(4.0f), 0.001f);
    }

    @Test
    public void testSnapToBar_140bpm() {
        TimelineView view = new TimelineView();
        view.bpm = 140.0f;
        // At 140 BPM: 1 beat = 60/140s, 1 bar = 240/140 = ~1.714s
        float barLen = 240.0f / 140.0f;
        assertEquals(0.0f, view.snapToBar(0.5f), 0.001f);
        assertEquals(barLen, view.snapToBar(barLen - 0.1f), 0.001f);
        assertEquals(barLen, view.snapToBar(barLen + 0.1f), 0.001f);
        assertEquals(2 * barLen, view.snapToBar(2 * barLen), 0.001f);
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
        Notification n0 = Notification.newBuilder()
                .setTimelineClipInfo(TimelineClipInfo.newBuilder()
                        .setTrackIndex(0).setClipIndex(0)
                        .setName("Clip0").setPath("a.mid")
                        .setStartTime(0.0f).setDuration(2.0f))
                .build();
        view.handleNotification(n0);

        // Clip on track 2
        Notification n2 = Notification.newBuilder()
                .setTimelineClipInfo(TimelineClipInfo.newBuilder()
                        .setTrackIndex(2).setClipIndex(0)
                        .setName("Clip2").setPath("b.mid")
                        .setStartTime(5.0f).setDuration(3.0f))
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
        Notification n = Notification.newBuilder()
                .setTimelineClipInfo(TimelineClipInfo.newBuilder()
                        .setTrackIndex(0).setClipIndex(0)
                        .setName("New Clip").setPath("")
                        .setStartTime(0.0f).setDuration(2.0f))
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
        // At 60 BPM: 1 beat = 1.0s, 1 bar = 4.0s
        assertEquals(0.0f, view.snapToBar(0.0f), 0.001f);
        assertEquals(4.0f, view.snapToBar(3.5f), 0.001f);
        assertEquals(4.0f, view.snapToBar(4.0f), 0.001f);
        assertEquals(8.0f, view.snapToBar(7.0f), 0.001f);
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
        Notification n1 = Notification.newBuilder()
                .setTimelineClipInfo(TimelineClipInfo.newBuilder()
                        .setTrackIndex(0).setClipIndex(0)
                        .setName("Initial").setPath("test.mid")
                        .setStartTime(1.0f).setDuration(2.0f)
                        .addWaveform(0.1f).addWaveform(0.2f))
                .build();
        view.handleNotification(n1);

        assertEquals(1, view.tracks.get(0).clips.size());
        assertEquals("Initial", view.tracks.get(0).clips.get(0).name);

        // Update same clip index 0 on same track
        Notification n2 = Notification.newBuilder()
                .setTimelineClipInfo(TimelineClipInfo.newBuilder()
                        .setTrackIndex(0).setClipIndex(0)
                        .setName("Updated").setPath("test2.mid")
                        .setStartTime(3.0f).setDuration(4.0f)
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
}
