package hibiki.ui;

import org.junit.Test;
import static org.junit.Assert.*;

import java.awt.event.*;
import java.util.*;

/**
 * Tests for AutomationMouseHandler: coordinate helpers, lane hit detection,
 * state management, and handler delegation returns.
 */
public class AutomationMouseHandlerTest {

    // --- Helper to build a view with automation lanes ---

    private TimelineView createViewWithAutomation() {
        TimelineView view = new TimelineView();
        view.bpm = 120.0f;
        // Enable automation on track 0 with one lane
        TimelineView.TrackTimeline track = view.tracks.get(0);
        track.automationExpanded = true;
        TimelineView.AutomationLaneData lane = new TimelineView.AutomationLaneData();
        lane.paramName = "Volume";
        lane.clips = new ArrayList<>();
        track.automationLanes.add(lane);
        return view;
    }

    private TimelineView createViewWithAutomationClip() {
        TimelineView view = createViewWithAutomation();
        TimelineView.TrackTimeline track = view.tracks.get(0);
        TimelineView.AutomationLaneData lane = track.automationLanes.get(0);
        TimelineView.ClipRect clip = new TimelineView.ClipRect();
        clip.startTime = 0.0f;
        clip.duration = 4.0f; // 4 beats at 120bpm = 2 seconds
        clip.isAutomation = true;
        clip.automationPoints = new ArrayList<>();
        clip.automationPoints.add(new AutomationEditor.AutoPoint(0.0f, 0.5f, 0.0f));
        clip.automationPoints.add(new AutomationEditor.AutoPoint(2.0f, 0.8f, 0.0f));
        lane.clips.add(clip);
        return view;
    }

    // --- isEditing ---

    @Test
    public void testIsEditing_initiallyFalse() {
        TimelineView view = new TimelineView();
        AutomationMouseHandler handler = new AutomationMouseHandler(view);
        assertFalse(handler.isEditing());
    }

    // --- findAutomationLaneAtY ---

    @Test
    public void testFindAutomationLaneAtY_noAutomation() {
        TimelineView view = new TimelineView();
        AutomationMouseHandler handler = new AutomationMouseHandler(view);
        // Track 0 has no automation expanded
        assertEquals(-1, handler.findAutomationLaneAtY(0, 200));
    }

    @Test
    public void testFindAutomationLaneAtY_invalidTrack() {
        TimelineView view = new TimelineView();
        AutomationMouseHandler handler = new AutomationMouseHandler(view);
        assertEquals(-1, handler.findAutomationLaneAtY(-1, 200));
        assertEquals(-1, handler.findAutomationLaneAtY(100, 200));
    }

    @Test
    public void testFindAutomationLaneAtY_inClipArea() {
        TimelineView view = createViewWithAutomation();
        AutomationMouseHandler handler = new AutomationMouseHandler(view);
        // Y in the base track area (before automation lanes) should return -1
        int scaleTimeRuler = Theme.getInstance().scale(TimelineView.TIME_RULER_HEIGHT);
        int trackTopY = scaleTimeRuler + Theme.getInstance().scale(view.getTrackY(0));
        int yInClipArea = trackTopY + 10; // Well within base track area
        assertEquals(-1, handler.findAutomationLaneAtY(0, yInClipArea));
    }

    @Test
    public void testFindAutomationLaneAtY_inFirstLane() {
        TimelineView view = createViewWithAutomation();
        AutomationMouseHandler handler = new AutomationMouseHandler(view);
        int scaleTimeRuler = Theme.getInstance().scale(TimelineView.TIME_RULER_HEIGHT);
        int trackTopY = scaleTimeRuler + Theme.getInstance().scale(view.getTrackY(0));
        int scaleBaseTrack = Theme.getInstance().scale(view.getBaseTrackHeight());
        // Y just inside the first automation lane
        int yInLane0 = trackTopY + scaleBaseTrack + 5;
        assertEquals(0, handler.findAutomationLaneAtY(0, yInLane0));
    }

    @Test
    public void testFindAutomationLaneAtY_twoLanes() {
        TimelineView view = createViewWithAutomation();
        // Add a second lane
        TimelineView.TrackTimeline track = view.tracks.get(0);
        TimelineView.AutomationLaneData lane2 = new TimelineView.AutomationLaneData();
        lane2.paramName = "Pan";
        lane2.clips = new ArrayList<>();
        track.automationLanes.add(lane2);

        AutomationMouseHandler handler = new AutomationMouseHandler(view);
        int scaleTimeRuler = Theme.getInstance().scale(TimelineView.TIME_RULER_HEIGHT);
        int trackTopY = scaleTimeRuler + Theme.getInstance().scale(view.getTrackY(0));
        int scaleBaseTrack = Theme.getInstance().scale(view.getBaseTrackHeight());
        int scaleAutoLane = Theme.getInstance().scale(view.getAutomationLaneHeight());

        int yInLane0 = trackTopY + scaleBaseTrack + 5;
        int yInLane1 = trackTopY + scaleBaseTrack + scaleAutoLane + 5;
        assertEquals(0, handler.findAutomationLaneAtY(0, yInLane0));
        assertEquals(1, handler.findAutomationLaneAtY(0, yInLane1));
    }

    @Test
    public void testFindAutomationLaneAtY_beyondLanes() {
        TimelineView view = createViewWithAutomation();
        AutomationMouseHandler handler = new AutomationMouseHandler(view);
        int scaleTimeRuler = Theme.getInstance().scale(TimelineView.TIME_RULER_HEIGHT);
        int trackTopY = scaleTimeRuler + Theme.getInstance().scale(view.getTrackY(0));
        int scaleBaseTrack = Theme.getInstance().scale(view.getBaseTrackHeight());
        int scaleAutoLane = Theme.getInstance().scale(view.getAutomationLaneHeight());
        // Y beyond the single lane
        int yBeyond = trackTopY + scaleBaseTrack + scaleAutoLane + 5;
        assertEquals(-1, handler.findAutomationLaneAtY(0, yBeyond));
    }

    // --- handleDrag / handleRelease when not editing ---

    @Test
    public void testHandleDrag_notEditing() {
        TimelineView view = new TimelineView();
        AutomationMouseHandler handler = new AutomationMouseHandler(view);
        MouseEvent e = new MouseEvent(view.contentPanel, MouseEvent.MOUSE_DRAGGED,
                System.currentTimeMillis(), 0, 100, 100, 1, false);
        assertFalse(handler.handleDrag(e));
    }

    @Test
    public void testHandleRelease_notEditing() {
        TimelineView view = new TimelineView();
        AutomationMouseHandler handler = new AutomationMouseHandler(view);
        MouseEvent e = new MouseEvent(view.contentPanel, MouseEvent.MOUSE_RELEASED,
                System.currentTimeMillis(), 0, 100, 100, 1, false);
        assertFalse(handler.handleRelease(e));
    }

    // --- DragMode interaction from automation press ---

    @Test
    public void testHandlePress_emptyLane_createsDragMode() {
        TimelineView view = createViewWithAutomation();
        AutomationMouseHandler handler = new AutomationMouseHandler(view);

        int scaleTimeRuler = Theme.getInstance().scale(TimelineView.TIME_RULER_HEIGHT);
        int trackTopY = scaleTimeRuler + Theme.getInstance().scale(view.getTrackY(0));
        int scaleBaseTrack = Theme.getInstance().scale(view.getBaseTrackHeight());
        int y = trackTopY + scaleBaseTrack + 10;

        // Left click in empty automation lane should start clip creation
        MouseEvent e = new MouseEvent(view.contentPanel, MouseEvent.MOUSE_PRESSED,
                System.currentTimeMillis(), 0, 200, y, 1, false, MouseEvent.BUTTON1);
        handler.handlePress(e, 0, 0);

        assertEquals(TimelineView.DragMode.CREATE_CLIP, view.dragMode);
        assertEquals(0, view.creatingTrackIdx);
        assertEquals(0, view.creatingAutoLaneIdx);
        assertNotNull(view.creatingClipRect);
        assertTrue(view.creatingClipRect.isAutomation);
    }

    @Test
    public void testHandlePress_onClipBody_setsMoveDragMode() {
        TimelineView view = createViewWithAutomationClip();
        AutomationMouseHandler handler = new AutomationMouseHandler(view);

        // Compute Y inside the automation lane
        int scaleTimeRuler = Theme.getInstance().scale(TimelineView.TIME_RULER_HEIGHT);
        int trackTopY = scaleTimeRuler + Theme.getInstance().scale(view.getTrackY(0));
        int scaleBaseTrack = Theme.getInstance().scale(view.getBaseTrackHeight());
        int scaleAutoLane = Theme.getInstance().scale(view.getAutomationLaneHeight());
        int y = trackTopY + scaleBaseTrack + scaleAutoLane / 2;

        // Click at 1.0 seconds (within clip that spans 0-2s)
        int x = (int) (1.0f * view.getPixelsPerSecond());
        MouseEvent e = new MouseEvent(view.contentPanel, MouseEvent.MOUSE_PRESSED,
                System.currentTimeMillis(), 0, x, y, 1, false, MouseEvent.BUTTON1);
        handler.handlePress(e, 0, 0);

        assertEquals(TimelineView.DragMode.MOVE_CLIP, view.dragMode);
        assertNotNull(view.draggingClip);
    }

    // --- State reset ---

    @Test
    public void testAutoTrackIdx_initialValue() {
        TimelineView view = new TimelineView();
        AutomationMouseHandler handler = new AutomationMouseHandler(view);
        assertEquals(-1, handler.autoTrackIdx);
        assertEquals(-1, handler.autoLaneIdx);
        assertEquals(-1, handler.autoClipIdx);
    }
}
