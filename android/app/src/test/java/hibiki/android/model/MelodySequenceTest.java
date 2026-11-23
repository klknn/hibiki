package hibiki.android.model;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertTrue;

import org.junit.Test;

public class MelodySequenceTest {

    @Test
    public void testMelodyStepPitchAndFrequency() {
        MelodyStep stepA4 = MelodyStep.of(69, 1.0f);
        assertEquals(69, stepA4.getPitch());
        assertEquals("A4", stepA4.getNoteName());
        assertEquals(440.0, stepA4.getFrequencyHz(), 0.001);

        MelodyStep stepC4 = MelodyStep.of(60, 0.8f);
        assertEquals(60, stepC4.getPitch());
        assertEquals("C4", stepC4.getNoteName());
        assertEquals(261.625, stepC4.getFrequencyHz(), 0.01);

        MelodyStep stepFSharp = MelodyStep.of(66, 0.5f);
        assertEquals("F#4", stepFSharp.getNoteName());
    }

    @Test
    public void testMelodyStepImmutability() {
        MelodyStep original = MelodyStep.of(60, 0.5f);
        MelodyStep modified = original.withPitch(64).withVelocity(0.9f).withActive(false);

        assertEquals(60, original.getPitch());
        assertTrue(original.isActive());
        assertEquals(0.5f, original.getVelocity(), 0.001f);

        assertEquals(64, modified.getPitch());
        assertFalse(modified.isActive());
        assertEquals(0.9f, modified.getVelocity(), 0.001f);
    }

    @Test
    public void testSequenceDefaultLengthAndToggle() {
        MelodySequence seq = new MelodySequence(16);
        assertEquals(16, seq.getLengthSteps());

        for (int i = 0; i < 16; i++) {
            assertFalse(seq.getStep(i).isActive());
        }

        // Toggle step 3 on with pitch E4 (64)
        MelodySequence toggledOn = seq.toggleStep(3, 64);
        assertTrue(toggledOn.getStep(3).isActive());
        assertEquals(64, toggledOn.getStep(3).getPitch());
        assertEquals("E4", toggledOn.getStep(3).getNoteName());

        // Toggle step 3 off
        MelodySequence toggledOff = toggledOn.toggleStep(3, 64);
        assertFalse(toggledOff.getStep(3).isActive());
    }

    @Test
    public void testDemoMelodyCreation() {
        MelodySequence demo = MelodySequence.createDemoMelody();
        assertEquals(16, demo.getLengthSteps());

        assertTrue(demo.getStep(0).isActive());
        assertEquals("C4", demo.getStep(0).getNoteName());

        assertFalse(demo.getStep(1).isActive());

        assertTrue(demo.getStep(2).isActive());
        assertEquals("D4", demo.getStep(2).getNoteName());

        assertTrue(demo.getStep(4).isActive());
        assertEquals("E4", demo.getStep(4).getNoteName());

        assertTrue(demo.getStep(6).isActive());
        assertEquals("G4", demo.getStep(6).getNoteName());
    }

    @Test
    public void testSequenceBoundaryCounts() {
        // Zero steps clamped to minimum 1
        MelodySequence seqZero = new MelodySequence(0);
        assertEquals(1, seqZero.getLengthSteps());

        // Negative steps clamped to minimum 1
        MelodySequence seqNeg = new MelodySequence(-8);
        assertEquals(1, seqNeg.getLengthSteps());

        // Single step sequence
        MelodySequence seqSingle = new MelodySequence(1);
        assertEquals(1, seqSingle.getLengthSteps());

        // 64-step sequence
        MelodySequence seq64 = new MelodySequence(64);
        assertEquals(64, seq64.getLengthSteps());
    }

    @Test
    public void testNullOrEmptyListConstructorGracefulFallback() {
        MelodySequence seqNull = new MelodySequence((java.util.List<MelodyStep>) null);
        assertEquals(MelodySequence.DEFAULT_STEP_COUNT, seqNull.getLengthSteps());

        MelodySequence seqEmpty = new MelodySequence(java.util.Collections.emptyList());
        assertEquals(MelodySequence.DEFAULT_STEP_COUNT, seqEmpty.getLengthSteps());
    }

    @Test
    public void testGetStepOutOfBoundsReturnsEmptyStepSafely() {
        MelodySequence seq = new MelodySequence(16);

        MelodyStep under = seq.getStep(-1);
        assertNotNull(under);
        assertFalse(under.isActive());

        MelodyStep over = seq.getStep(16);
        assertNotNull(over);
        assertFalse(over.isActive());

        MelodyStep farOver = seq.getStep(999);
        assertNotNull(farOver);
        assertFalse(farOver.isActive());
    }

    @Test
    public void testWithStepOutOfBoundsReturnsOriginalSequence() {
        MelodySequence seq = new MelodySequence(16);
        MelodyStep validStep = MelodyStep.of(60, 0.9f);

        MelodySequence resUnder = seq.withStep(-1, validStep);
        assertEquals(seq, resUnder);

        MelodySequence resOver = seq.withStep(16, validStep);
        assertEquals(seq, resOver);
    }

    @Test
    public void testWithStepNullReplacesWithEmptyStep() {
        MelodySequence seq = new MelodySequence(16);
        seq = seq.withStep(0, MelodyStep.of(60, 1.0f));
        assertTrue(seq.getStep(0).isActive());

        // Pass null: replaces step 0 with empty step
        seq = seq.withStep(0, null);
        assertNotNull(seq.getStep(0));
        assertFalse(seq.getStep(0).isActive());
    }

    @Test
    public void testToggleStepOutOfBoundsSafelyNoOps() {
        MelodySequence seq = new MelodySequence(16);
        MelodySequence res = seq.toggleStep(-5, 60);
        assertEquals(seq, res);

        MelodySequence resOver = seq.toggleStep(20, 60);
        assertEquals(seq, resOver);
    }

    @Test
    public void testClearResetsAllStepsToInactive() {
        MelodySequence seq = MelodySequence.createDemoMelody();
        assertTrue(seq.getStep(0).isActive());
        assertTrue(seq.getStep(2).isActive());

        MelodySequence cleared = seq.clear();
        assertEquals(seq.getLengthSteps(), cleared.getLengthSteps());
        for (int i = 0; i < cleared.getLengthSteps(); i++) {
            assertFalse("All steps must be inactive after clear()", cleared.getStep(i).isActive());
        }
    }
}
