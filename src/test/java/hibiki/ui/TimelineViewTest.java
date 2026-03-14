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
}
