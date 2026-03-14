package hibiki.ui;

import org.junit.Test;
import static org.junit.Assert.*;

public class PianoRollTest {

    @Test
    public void testNoteConstructor() {
        PianoRoll.Note note = new PianoRoll.Note(60, 480, 240, 100);
        assertEquals(60, note.pitch);
        assertEquals(480, note.startTick);
        assertEquals(240, note.durationTicks);
        assertEquals(100, note.velocity);
    }

    @Test
    public void testNoteDefaultFields() {
        PianoRoll.Note note = new PianoRoll.Note(36, 0, 96, 64);
        assertNull(note.onEvent);
        assertNull(note.offEvent);
    }

    @Test
    public void testNoteMutation() {
        PianoRoll.Note note = new PianoRoll.Note(60, 0, 96, 100);
        note.pitch = 72;
        note.startTick = 480;
        note.durationTicks = 192;
        note.velocity = 50;
        assertEquals(72, note.pitch);
        assertEquals(480, note.startTick);
        assertEquals(192, note.durationTicks);
        assertEquals(50, note.velocity);
    }

    @Test
    public void testNoteVelocityBounds() {
        // Velocity should be stored as-is (no built-in clamping)
        PianoRoll.Note note = new PianoRoll.Note(60, 0, 96, 0);
        assertEquals(0, note.velocity);
        note.velocity = 127;
        assertEquals(127, note.velocity);
    }

    @Test
    public void testIsBlackKey() {
        if (java.awt.GraphicsEnvironment.isHeadless()) return;
        java.io.File dummyFile = new java.io.File("dummy.mid");
        PianoRoll pr = new PianoRoll(null, dummyFile, 0, -1, 0, 0.0f);

        // C=0 is white
        assertFalse(pr.isBlackKey(0));
        assertFalse(pr.isBlackKey(12));
        assertFalse(pr.isBlackKey(60)); // Middle C
        // C#=1 is black
        assertTrue(pr.isBlackKey(1));
        assertTrue(pr.isBlackKey(13));
        assertTrue(pr.isBlackKey(61));
        // D=2 is white
        assertFalse(pr.isBlackKey(2));
        // D#=3 is black
        assertTrue(pr.isBlackKey(3));
        // E=4 is white
        assertFalse(pr.isBlackKey(4));
        // F=5 is white
        assertFalse(pr.isBlackKey(5));
        // F#=6 is black
        assertTrue(pr.isBlackKey(6));
        // G=7 is white
        assertFalse(pr.isBlackKey(7));
        // G#=8 is black
        assertTrue(pr.isBlackKey(8));
        // A=9 is white
        assertFalse(pr.isBlackKey(9));
        // A#=10 is black
        assertTrue(pr.isBlackKey(10));
        // B=11 is white
        assertFalse(pr.isBlackKey(11));

        pr.dispose();
    }

    @Test
    public void testGridTickInterval_quarterMode() {
        if (java.awt.GraphicsEnvironment.isHeadless()) return;
        java.io.File dummyFile = new java.io.File("dummy.mid");
        PianoRoll pr = new PianoRoll(null, dummyFile, 0, -1, 0, 0.0f);

        // Access private gridMode field via package access
        // Default gridMode is AUTO, but we can test the method
        int interval = pr.getGridTickInterval();
        assertTrue("Grid tick interval should be positive", interval > 0);

        pr.dispose();
    }

    @Test
    public void testSnapTickInterval() {
        if (java.awt.GraphicsEnvironment.isHeadless()) return;
        java.io.File dummyFile = new java.io.File("dummy.mid");
        PianoRoll pr = new PianoRoll(null, dummyFile, 0, -1, 0, 0.0f);

        int snapInterval = pr.getSnapTickInterval();
        int gridInterval = pr.getGridTickInterval();
        assertEquals("Snap interval should equal grid interval", gridInterval, snapInterval);

        pr.dispose();
    }

    @Test
    public void testNoteHighPitch() {
        PianoRoll.Note note = new PianoRoll.Note(127, 0, 48, 127);
        assertEquals(127, note.pitch);
        assertEquals(127, note.velocity);
    }

    @Test
    public void testNoteLowPitch() {
        PianoRoll.Note note = new PianoRoll.Note(0, 0, 48, 1);
        assertEquals(0, note.pitch);
        assertEquals(1, note.velocity);
    }

    @Test
    public void testNumKeysConstant() {
        assertEquals("Standard MIDI has 128 keys", 128, PianoRoll.NUM_KEYS);
    }
}
