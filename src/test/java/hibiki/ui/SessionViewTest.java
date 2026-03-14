package hibiki.ui;

import org.junit.Test;
import static org.junit.Assert.*;

public class SessionViewTest {

    @Test
    public void testInitialization() {
        SessionView sv = new SessionView();
        assertNotNull(sv);
        assertEquals(Theme.getInstance().BG_DARK, sv.getBackground());
    }

    @Test
    public void testSingleton() {
        SessionView sv = new SessionView();
        assertSame(sv, SessionView.getInstance());
    }

    @Test
    public void testDefaultSelectedTrack() {
        SessionView sv = new SessionView();
        // Default selected track should be 0
        assertEquals(0, sv.getSelectedTrack());
    }

    @Test
    public void testSelectTrack() {
        SessionView sv = new SessionView();
        sv.selectTrackByIdx(2);
        assertEquals(2, sv.getSelectedTrack());
    }

    @Test
    public void testSelectTrack_sequential() {
        SessionView sv = new SessionView();
        sv.selectTrackByIdx(0);
        assertEquals(0, sv.getSelectedTrack());
        sv.selectTrackByIdx(5);
        assertEquals(5, sv.getSelectedTrack());
        sv.selectTrackByIdx(3);
        assertEquals(3, sv.getSelectedTrack());
    }

    @Test
    public void testClearAllSlots() {
        SessionView sv = new SessionView();
        // Should not throw
        sv.clearAllSlots();
    }

    @Test
    public void testUpdateSlotLabel() {
        SessionView sv = new SessionView();
        // Track 0, Slot 0
        sv.updateSlotLabel(0, 0, "TestClip.mid");
        // Should not throw - slot button text updated
    }

    @Test
    public void testUpdateSlotLabel_clearLabel() {
        SessionView sv = new SessionView();
        // Set then clear
        sv.updateSlotLabel(0, 0, "Clip.mid");
        sv.updateSlotLabel(0, 0, "");
        // Should not throw
    }

    @Test
    public void testUpdateSlotLabel_outOfBounds() {
        SessionView sv = new SessionView();
        // Should not throw for OOB indices
        sv.updateSlotLabel(10, 10, "test");
    }

    @Test
    public void testUpdateLevel() {
        SessionView sv = new SessionView();
        // Should not throw
        sv.updateLevel(0, 0.5f, 0.3f);
    }

    @Test
    public void testUpdateLevel_extremeValues() {
        SessionView sv = new SessionView();
        // Silent level
        sv.updateLevel(0, 0.0f, 0.0f);
        // Max level
        sv.updateLevel(0, 1.0f, 1.0f);
    }

    @Test
    public void testUpdateLevel_outOfBounds() {
        SessionView sv = new SessionView();
        // Should not throw for OOB index
        sv.updateLevel(10, 0.5f, 0.3f);
    }

    @Test
    public void testGetPreferredSize() {
        SessionView sv = new SessionView();
        assertNotNull(sv.getPreferredSize());
        assertTrue(sv.getPreferredSize().width > 0);
        assertTrue(sv.getPreferredSize().height > 0);
    }

    @Test
    public void testMultipleSlotUpdates() {
        SessionView sv = new SessionView();
        // Update multiple slots on different tracks
        sv.updateSlotLabel(0, 0, "drums.wav");
        sv.updateSlotLabel(1, 0, "bass.mid");
        sv.updateSlotLabel(2, 1, "keys.wav");
        // Should not throw
    }

    @Test
    public void testSelectTrack_afterClear() {
        SessionView sv = new SessionView();
        sv.selectTrackByIdx(4);
        sv.clearAllSlots();
        // Selection should survive a clear
        assertEquals(4, sv.getSelectedTrack());
    }

    @Test
    public void testUpdateLevel_multipleTracksRapid() {
        SessionView sv = new SessionView();
        // Simulate rapid meter updates like from audio callback
        for (int i = 0; i < 8; i++) {
            sv.updateLevel(i, (float) Math.random(), (float) Math.random());
        }
    }
}
