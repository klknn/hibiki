package hibiki.android.model;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertTrue;

import java.util.ArrayList;
import java.util.List;
import org.junit.Test;

/**
 * Fast host JVM unit tests for Hibiki Android model classes.
 * Runs instantly without requiring an Android emulator or device.
 */
public class ModelTest {

    @Test
    public void testTrackerCellDefaultState() {
        TrackerCell cell = new TrackerCell();
        assertFalse("Default TrackerCell should not be active", cell.isActive());
        assertEquals("---", cell.getNote());
        assertEquals(4, cell.getOctave());
        assertEquals("···", cell.getDisplayNote());
        assertEquals("··", cell.getDisplayVel());
        assertEquals("··", cell.getDisplayInst());
        assertEquals("··", cell.getDisplayFx());
    }

    @Test
    public void testTrackerCellActiveFormatting() {
        TrackerCell cell = new TrackerCell("C-", 4, 127, 2, "0F", true);
        assertTrue("Cell should be active", cell.isActive());
        assertEquals("C-4", cell.getDisplayNote());
        assertEquals("7F", cell.getDisplayVel());
        assertEquals("02", cell.getDisplayInst());
        assertEquals("0F", cell.getDisplayFx());
    }

    @Test
    public void testTrackerCellNoteOffDisplay() {
        TrackerCell offCell = new TrackerCell("OFF", 4, 0, 0, "00", true);
        assertEquals("===", offCell.getDisplayNote());

        TrackerCell eqCell = new TrackerCell("===", 4, 0, 0, "00", true);
        assertEquals("===", eqCell.getDisplayNote());
    }

    @Test
    public void testChannelStateImmutability() {
        ChannelState initial = new ChannelState(0, "DRUMS", 0xFFFF0055);
        assertEquals(0, initial.getIndex());
        assertEquals("DRUMS", initial.getName());
        assertEquals(0.8f, initial.getVolume(), 0.001f);
        assertEquals(0.0f, initial.getPan(), 0.001f);
        assertFalse(initial.isMuted());
        assertFalse(initial.isSoloed());
        assertEquals(16, initial.getSteps().size());

        // Volume update
        ChannelState volUpdated = initial.withVolume(0.5f);
        assertEquals(0.5f, volUpdated.getVolume(), 0.001f);
        assertEquals(0.8f, initial.getVolume(), 0.001f);

        // Pan update
        ChannelState panUpdated = initial.withPan(-0.5f);
        assertEquals(-0.5f, panUpdated.getPan(), 0.001f);
        assertEquals(0.0f, initial.getPan(), 0.001f);

        // Mute / Solo update
        ChannelState muteUpdated = initial.withMuted(true);
        assertTrue(muteUpdated.isMuted());
        assertFalse(initial.isMuted());

        ChannelState soloUpdated = initial.withSoloed(true);
        assertTrue(soloUpdated.isSoloed());
        assertFalse(initial.isSoloed());

        // Step update
        TrackerCell newCell = new TrackerCell("D-", 3, 100, 1, "00", true);
        ChannelState stepUpdated = initial.withStep(3, newCell);
        assertTrue(stepUpdated.getSteps().get(3).isActive());
        assertEquals("D-3", stepUpdated.getSteps().get(3).getDisplayNote());
        assertFalse(initial.getSteps().get(3).isActive());
    }

    @Test
    public void testScaleTypeIntervals() {
        assertEquals("Chromatic", ScaleType.CHROMATIC.getDisplayName());
        assertEquals(12, ScaleType.CHROMATIC.getIntervals().length);

        assertEquals("Major", ScaleType.MAJOR.getDisplayName());
        int[] major = ScaleType.MAJOR.getIntervals();
        assertEquals(7, major.length);
        assertEquals(0, major[0]);
        assertEquals(2, major[1]);
        assertEquals(4, major[2]);
        assertEquals(11, major[6]);

        int[] pentMinor = ScaleType.PENTATONIC_MINOR.getIntervals();
        assertEquals(5, pentMinor.length);
        assertEquals(0, pentMinor[0]);
        assertEquals(3, pentMinor[1]);
        assertEquals(10, pentMinor[4]);
    }

    @Test
    public void testDrumPadItemAndSynthMacro() {
        DrumPadItem pad = new DrumPadItem(0, "KICK", 36, 0xFFFF0055);
        assertEquals(0, pad.getIndex());
        assertEquals("KICK", pad.getName());
        assertEquals(36, pad.getMidiNote());
        assertEquals(0xFFFF0055, pad.getColor());

        SynthMacro macro = new SynthMacro("m1", "CUTOFF", 0.75f);
        assertEquals("m1", macro.getId());
        assertEquals("CUTOFF", macro.getName());
        assertEquals(0.75f, macro.getValue(), 0.001f);

        SynthMacro updated = macro.withValue(0.9f);
        assertEquals(0.9f, updated.getValue(), 0.001f);
        assertEquals(0.75f, macro.getValue(), 0.001f);
    }
}
