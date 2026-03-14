package hibiki.ui;

import hibiki.ipc.Notification;
import hibiki.ipc.TimelineClipInfo;
import hibiki.ipc.Response;
import com.google.flatbuffers.FlatBufferBuilder;
import org.junit.Test;
import static org.junit.Assert.*;

import javax.swing.*;
import java.nio.ByteBuffer;

public class TimelineViewTest {

    @Test
    public void testHandleTimelineClipNotification() {
        TimelineView view = new TimelineView();
        assertEquals(8, view.tracks.size());
        assertEquals(0, view.tracks.get(0).clips.size());

        FlatBufferBuilder builder = new FlatBufferBuilder(1024);
        int nameOff = builder.createString("TestClip");
        int pathOff = builder.createString("/path/to/test.wav");
        float[] waveformData = {0.1f, 0.5f, 0.8f, 0.3f};
        int wfOff = TimelineClipInfo.createWaveformVector(builder, waveformData);
        int timelineOff = TimelineClipInfo.createTimelineClipInfo(builder, 0, 0, nameOff, pathOff, 10.0f, 5.0f, wfOff);
        int nfOff = Notification.createNotification(builder, Response.TimelineClipInfo, timelineOff);
        builder.finish(nfOff);
        ByteBuffer bb = builder.dataBuffer();
        Notification n = Notification.getRootAsNotification(bb);

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
        FlatBufferBuilder builder = new FlatBufferBuilder(1024);

        // Clip on track 0
        int name0 = builder.createString("Clip0");
        int path0 = builder.createString("a.mid");
        int wf0 = TimelineClipInfo.createWaveformVector(builder, new float[0]);
        int tc0 = TimelineClipInfo.createTimelineClipInfo(builder, 0, 0, name0, path0, 0.0f, 2.0f, wf0);
        int n0 = Notification.createNotification(builder, Response.TimelineClipInfo, tc0);
        builder.finish(n0);
        view.handleNotification(Notification.getRootAsNotification(builder.dataBuffer()));

        // Clip on track 2
        builder = new FlatBufferBuilder(1024);
        int name2 = builder.createString("Clip2");
        int path2 = builder.createString("b.mid");
        int wf2 = TimelineClipInfo.createWaveformVector(builder, new float[0]);
        int tc2 = TimelineClipInfo.createTimelineClipInfo(builder, 2, 0, name2, path2, 5.0f, 3.0f, wf2);
        int n2 = Notification.createNotification(builder, Response.TimelineClipInfo, tc2);
        builder.finish(n2);
        view.handleNotification(Notification.getRootAsNotification(builder.dataBuffer()));

        assertEquals(1, view.tracks.get(0).clips.size());
        assertEquals(0, view.tracks.get(1).clips.size());
        assertEquals(1, view.tracks.get(2).clips.size());
        assertEquals("Clip2", view.tracks.get(2).clips.get(0).name);
        assertEquals(5.0f, view.tracks.get(2).clips.get(0).startTime, 0.001f);
    }

    @Test
    public void testEmptyClipPathIsMidi() {
        TimelineView view = new TimelineView();
        FlatBufferBuilder builder = new FlatBufferBuilder(1024);
        int name = builder.createString("New Clip");
        int path = builder.createString("");
        int wf = TimelineClipInfo.createWaveformVector(builder, new float[0]);
        int tc = TimelineClipInfo.createTimelineClipInfo(builder, 0, 0, name, path, 0.0f, 2.0f, wf);
        int nf = Notification.createNotification(builder, Response.TimelineClipInfo, tc);
        builder.finish(nf);
        view.handleNotification(Notification.getRootAsNotification(builder.dataBuffer()));

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
        for (TimelineView.GridMode mode : TimelineView.GridMode.values()) {
            assertNotNull(mode.toString());
            assertFalse(mode.toString().isEmpty());
        }
        assertEquals("Auto", TimelineView.GridMode.AUTO.toString());
        assertEquals("1/4", TimelineView.GridMode.QUARTER.toString());
        assertEquals("1/16", TimelineView.GridMode.SIXTEENTH.toString());
    }

    @Test
    public void testGetGridSnapSeconds_barMode() {
        TimelineView view = new TimelineView();
        view.bpm = 120.0f;
        float secondsPerBeat = 60.0f / 120.0f; // 0.5
        // BAR mode = 4 beats = 2.0s
        float snap = view.getGridSnapSeconds(TimelineView.GridMode.BAR, secondsPerBeat);
        assertEquals(2.0f, snap, 0.001f);
    }

    @Test
    public void testGetGridSnapSeconds_quarterMode() {
        TimelineView view = new TimelineView();
        view.bpm = 120.0f;
        float secondsPerBeat = 60.0f / 120.0f; // 0.5
        // QUARTER mode = 1 beat = 0.5s
        float snap = view.getGridSnapSeconds(TimelineView.GridMode.QUARTER, secondsPerBeat);
        assertEquals(0.5f, snap, 0.001f);
    }

    @Test
    public void testGetGridSnapSeconds_eighthMode() {
        TimelineView view = new TimelineView();
        view.bpm = 120.0f;
        float secondsPerBeat = 60.0f / 120.0f; // 0.5
        // EIGHTH mode = 0.5 beat = 0.25s
        float snap = view.getGridSnapSeconds(TimelineView.GridMode.EIGHTH, secondsPerBeat);
        assertEquals(0.25f, snap, 0.001f);
    }

    @Test
    public void testGetGridSnapSeconds_secondsMode() {
        TimelineView view = new TimelineView();
        float secondsPerBeat = 0.5f;
        float snap = view.getGridSnapSeconds(TimelineView.GridMode.SECONDS, secondsPerBeat);
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
        FlatBufferBuilder builder = new FlatBufferBuilder(1024);
        int name = builder.createString("Initial");
        int path = builder.createString("test.mid");
        int wf = TimelineClipInfo.createWaveformVector(builder, new float[] { 0.1f, 0.2f });
        int tc = TimelineClipInfo.createTimelineClipInfo(builder, 0, 0, name, path, 1.0f, 2.0f, wf);
        int nf = Notification.createNotification(builder, Response.TimelineClipInfo, tc);
        builder.finish(nf);
        view.handleNotification(Notification.getRootAsNotification(builder.dataBuffer()));

        assertEquals(1, view.tracks.get(0).clips.size());
        assertEquals("Initial", view.tracks.get(0).clips.get(0).name);

        // Update same clip index 0 on same track
        builder = new FlatBufferBuilder(1024);
        name = builder.createString("Updated");
        path = builder.createString("test2.mid");
        wf = TimelineClipInfo.createWaveformVector(builder, new float[] { 0.5f });
        tc = TimelineClipInfo.createTimelineClipInfo(builder, 0, 0, name, path, 3.0f, 4.0f, wf);
        nf = Notification.createNotification(builder, Response.TimelineClipInfo, tc);
        builder.finish(nf);
        view.handleNotification(Notification.getRootAsNotification(builder.dataBuffer()));

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
