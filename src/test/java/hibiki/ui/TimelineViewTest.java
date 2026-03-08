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
        // Create TimelineView (headless environment usually okay for basic Swing components)
        TimelineView view = new TimelineView();
        
        // Initial state
        assertEquals(8, view.tracks.size());
        assertEquals(0, view.tracks.get(0).clips.size());

        // Construct a FlatBuffer notification for TimelineClipInfo
        FlatBufferBuilder builder = new FlatBufferBuilder(1024);
        int nameOff = builder.createString("TestClip");
        int pathOff = builder.createString("/path/to/test.wav");
        // Create sample waveform data
        float[] waveformData = {0.1f, 0.5f, 0.8f, 0.3f};
        int wfOff = TimelineClipInfo.createWaveformVector(builder, waveformData);
        
        int timelineOff = TimelineClipInfo.createTimelineClipInfo(builder, 0, 0, nameOff, pathOff, 10.0f, 5.0f, wfOff);
        int nfOff = Notification.createNotification(builder, Response.TimelineClipInfo, timelineOff);
        builder.finish(nfOff);
        
        ByteBuffer bb = builder.dataBuffer();
        Notification n = Notification.getRootAsNotification(bb);

        // Call handleNotification (it's private, but we can call it if we make it package-private or use reflection)
        // Let's make it package-private in the original file.
        view.handleNotification(n);

        // Verify state update
        assertEquals(1, view.tracks.get(0).clips.size());
        TimelineView.ClipRect clip = view.tracks.get(0).clips.get(0);
        assertEquals("TestClip", clip.name);
        assertEquals(10.0f, clip.startTime, 0.001f);
        assertEquals(5.0f, clip.duration, 0.001f);
    }
}
