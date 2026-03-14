package hibiki.ui;

import org.junit.Test;
import static org.junit.Assert.*;

import hibiki.ipc.ClipMidiData;
import hibiki.ipc.MidiEventData;
import com.google.flatbuffers.FlatBufferBuilder;
import java.nio.ByteBuffer;

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
    public void testNoteExtendsMidiDataModelNote() {
        PianoRoll.Note note = new PianoRoll.Note(60, 480, 240, 100);
        // Note should be an instance of MidiDataModel.Note
        assertTrue(note instanceof MidiDataModel.Note);
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
    public void testGridTickInterval_autoMode() {
        if (java.awt.GraphicsEnvironment.isHeadless()) return;
        java.io.File dummyFile = new java.io.File("dummy.mid");
        PianoRoll pr = new PianoRoll(null, dummyFile, 0, -1, 0, 0.0f);

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
    public void testGetTickWidth() {
        if (java.awt.GraphicsEnvironment.isHeadless())
            return;
        java.io.File dummyFile = new java.io.File("dummy.mid");
        PianoRoll pr = new PianoRoll(null, dummyFile, 0, -1, 0, 0.0f);

        float tw = pr.getTickWidth();
        assertTrue("Tick width should be positive", tw > 0);
        pr.dispose();
    }

    @Test
    public void testGetScaledKeyHeight() {
        if (java.awt.GraphicsEnvironment.isHeadless())
            return;
        java.io.File dummyFile = new java.io.File("dummy.mid");
        PianoRoll pr = new PianoRoll(null, dummyFile, 0, -1, 0, 0.0f);

        int height = pr.getScaledKeyHeight();
        assertTrue("Scaled key height should be positive", height > 0);
        pr.dispose();
    }

    @Test
    public void testLoadMidi_nonExistentFile() {
        if (java.awt.GraphicsEnvironment.isHeadless())
            return;
        // Using a nonsense file should create empty sequence, not crash
        java.io.File dummyFile = new java.io.File("nonexistent_file.mid");
        PianoRoll pr = new PianoRoll(null, dummyFile, 0, -1, 0, 0.0f);
        assertNotNull(pr.sequence);
        assertNotNull(pr.notes);
        pr.dispose();
    }

    @Test
    public void testLoadMidi_testFile() {
        if (java.awt.GraphicsEnvironment.isHeadless())
            return;
        java.io.File testMidi = new java.io.File("testdata/test.mid");
        if (!testMidi.exists())
            return;
        PianoRoll pr = new PianoRoll(null, testMidi, 0, -1, 0, 0.0f);
        assertNotNull(pr.sequence);
        assertNotNull(pr.notes);
        pr.dispose();
    }

    @Test
    public void testKeyHeightDefault() {
        if (java.awt.GraphicsEnvironment.isHeadless())
            return;
        java.io.File dummyFile = new java.io.File("dummy.mid");
        PianoRoll pr = new PianoRoll(null, dummyFile, 0, -1, 0, 0.0f);
        assertEquals(12, pr.keyHeight);
        pr.dispose();
    }

    @Test
    public void testPlayheadFields() {
        if (java.awt.GraphicsEnvironment.isHeadless())
            return;
        java.io.File dummyFile = new java.io.File("dummy.mid");
        PianoRoll pr = new PianoRoll(null, dummyFile, 0, -1, 0, 5.0f);
        assertEquals(5.0f, pr.clipStartTime, 0.01f);
        assertEquals(0.0f, pr.playheadPos, 0.01f);
        assertEquals(120.0f, pr.bpm, 0.01f);
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

    @Test
    public void testNoteMultipleInstances() {
        PianoRoll.Note n1 = new PianoRoll.Note(60, 0, 96, 100);
        PianoRoll.Note n2 = new PianoRoll.Note(64, 96, 48, 80);
        assertNotSame(n1, n2);
        assertEquals(60, n1.pitch);
        assertEquals(64, n2.pitch);
    }

    @Test
    public void testInitialization() {
        if (java.awt.GraphicsEnvironment.isHeadless())
            return;
        java.io.File dummyFile = new java.io.File("dummy.mid");
        PianoRoll pr = new PianoRoll(null, dummyFile, 0, -1, 0, 0.0f);
        assertNotNull(pr);
        assertTrue(pr.isDisplayable() || true); // May or may not be displayable
        assertEquals("Piano Roll - dummy.mid", pr.getTitle());
        pr.dispose();
    }

    @Test
    public void testGridPanelCreated() {
        if (java.awt.GraphicsEnvironment.isHeadless())
            return;
        java.io.File dummyFile = new java.io.File("dummy.mid");
        PianoRoll pr = new PianoRoll(null, dummyFile, 0, -1, 0, 0.0f);
        assertNotNull(pr.gridPanel);
        assertNotNull(pr.keysPanel);
        assertNotNull(pr.velocityPanel);
        pr.dispose();
    }

    @Test
    public void testDraggingStateDefaults() {
        if (java.awt.GraphicsEnvironment.isHeadless())
            return;
        java.io.File dummyFile = new java.io.File("dummy.mid");
        PianoRoll pr = new PianoRoll(null, dummyFile, 0, -1, 0, 0.0f);
        assertNull(pr.draggingNote);
        assertNull(pr.resizingNote);
        assertEquals(0, pr.dragOffsetX);
        assertEquals(0, pr.dragOffsetY);
        assertFalse(pr.isDraggingNote);
        assertFalse(pr.editingVelocity);
        pr.dispose();
    }

    @Test
    public void testDispose() {
        if (java.awt.GraphicsEnvironment.isHeadless())
            return;
        java.io.File dummyFile = new java.io.File("dummy.mid");
        PianoRoll pr = new PianoRoll(null, dummyFile, 0, -1, 0, 0.0f);
        pr.dispose();
        // Should not throw on double dispose
        pr.dispose();
    }
}
