package hibiki.android.model;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotEquals;
import static org.junit.Assert.assertTrue;

import org.junit.Test;

public class MelodyStepTest {

    @Test
    public void testPitchClampingBoundaries() {
        // Negative pitch clamped to 0
        MelodyStep stepNeg = new MelodyStep(true, -10, 0.8f, 0.8f);
        assertEquals(0, stepNeg.getPitch());

        // Zero pitch
        MelodyStep stepZero = new MelodyStep(true, 0, 0.8f, 0.8f);
        assertEquals(0, stepZero.getPitch());

        // 127 pitch (max MIDI)
        MelodyStep step127 = new MelodyStep(true, 127, 0.8f, 0.8f);
        assertEquals(127, step127.getPitch());

        // Oversized pitch clamped to 127
        MelodyStep stepOver = new MelodyStep(true, 250, 0.8f, 0.8f);
        assertEquals(127, stepOver.getPitch());
    }

    @Test
    public void testVelocityClampingAndNaNSanitization() {
        // Negative velocity clamped to 0.0f
        MelodyStep stepNeg = new MelodyStep(true, 60, -0.5f, 0.8f);
        assertEquals(0.0f, stepNeg.getVelocity(), 0.0001f);

        // Zero velocity
        MelodyStep stepZero = new MelodyStep(true, 60, 0.0f, 0.8f);
        assertEquals(0.0f, stepZero.getVelocity(), 0.0001f);

        // 1.0f velocity
        MelodyStep stepMax = new MelodyStep(true, 60, 1.0f, 0.8f);
        assertEquals(1.0f, stepMax.getVelocity(), 0.0001f);

        // Excessive velocity clamped to 1.0f
        MelodyStep stepOver = new MelodyStep(true, 60, 2.5f, 0.8f);
        assertEquals(1.0f, stepOver.getVelocity(), 0.0001f);

        // Float.NaN sanitized to default
        MelodyStep stepNaN = new MelodyStep(true, 60, Float.NaN, 0.8f);
        assertFalse(Float.isNaN(stepNaN.getVelocity()));
        assertEquals(0.8f, stepNaN.getVelocity(), 0.0001f);
    }

    @Test
    public void testGateClampingAndNaNSanitization() {
        // Gate below minimum (0.05f) clamped
        MelodyStep stepMin = new MelodyStep(true, 60, 0.8f, -0.1f);
        assertEquals(0.05f, stepMin.getGate(), 0.0001f);

        // Gate above maximum (1.0f) clamped
        MelodyStep stepOver = new MelodyStep(true, 60, 0.8f, 1.8f);
        assertEquals(1.0f, stepOver.getGate(), 0.0001f);

        // Float.NaN sanitized
        MelodyStep stepNaN = new MelodyStep(true, 60, 0.8f, Float.NaN);
        assertFalse(Float.isNaN(stepNaN.getGate()));
        assertEquals(0.8f, stepNaN.getGate(), 0.0001f);
    }

    @Test
    public void testPitchToNoteNameMapping() {
        assertEquals("C-1", MelodyStep.pitchToNoteName(0));
        assertEquals("C#0", MelodyStep.pitchToNoteName(13));
        assertEquals("C4", MelodyStep.pitchToNoteName(60)); // Middle C
        assertEquals("A4", MelodyStep.pitchToNoteName(69)); // Concert pitch
        assertEquals("B4", MelodyStep.pitchToNoteName(71));
        assertEquals("G9", MelodyStep.pitchToNoteName(127));
    }

    @Test
    public void testOctaveAndOffsetToPitchStandardMidi() {
        // C4 (Middle C) in standard MIDI must be note 60 (261.63 Hz)
        assertEquals(60, MelodyStep.octaveAndOffsetToPitch(4, 0));
        assertEquals("C4", MelodyStep.pitchToNoteName(MelodyStep.octaveAndOffsetToPitch(4, 0)));

        // A4 (Concert pitch 440 Hz) must be note 69
        assertEquals(69, MelodyStep.octaveAndOffsetToPitch(4, 9));
        assertEquals("A4", MelodyStep.pitchToNoteName(MelodyStep.octaveAndOffsetToPitch(4, 9)));

        // C2 must be note 36
        assertEquals(36, MelodyStep.octaveAndOffsetToPitch(2, 0));
        assertEquals("C2", MelodyStep.pitchToNoteName(MelodyStep.octaveAndOffsetToPitch(2, 0)));

        // C5 must be note 72
        assertEquals(72, MelodyStep.octaveAndOffsetToPitch(5, 0));
        assertEquals("C5", MelodyStep.pitchToNoteName(MelodyStep.octaveAndOffsetToPitch(5, 0)));
    }

    @Test
    public void testPitchToFrequencyAccurateHz() {
        // A4 = 440 Hz
        assertEquals(440.0, MelodyStep.pitchToFrequencyHz(69), 0.001);

        // A5 = 880 Hz
        assertEquals(880.0, MelodyStep.pitchToFrequencyHz(81), 0.001);

        // A3 = 220 Hz
        assertEquals(220.0, MelodyStep.pitchToFrequencyHz(57), 0.001);

        // Middle C (C4 = 60) ≈ 261.625565 Hz
        assertEquals(261.6256, MelodyStep.pitchToFrequencyHz(60), 0.001);

        // MIDI 0 (C-1) ≈ 8.1757989 Hz
        assertEquals(8.1758, MelodyStep.pitchToFrequencyHz(0), 0.001);
    }

    @Test
    public void testImmutabilityOfWithMethods() {
        MelodyStep original = new MelodyStep(true, 60, 0.8f, 0.7f);

        MelodyStep pitchMod = original.withPitch(65);
        assertEquals(60, original.getPitch());
        assertEquals(65, pitchMod.getPitch());

        MelodyStep velMod = original.withVelocity(0.3f);
        assertEquals(0.8f, original.getVelocity(), 0.001f);
        assertEquals(0.3f, velMod.getVelocity(), 0.001f);

        MelodyStep gateMod = original.withGate(0.95f);
        assertEquals(0.7f, original.getGate(), 0.001f);
        assertEquals(0.95f, gateMod.getGate(), 0.001f);

        MelodyStep actMod = original.withActive(false);
        assertTrue(original.isActive());
        assertFalse(actMod.isActive());
    }

    @Test
    public void testEqualsAndHashCodeContract() {
        MelodyStep step1 = new MelodyStep(true, 64, 0.8f, 0.75f);
        MelodyStep step2 = new MelodyStep(true, 64, 0.8f, 0.75f);
        MelodyStep step3 = new MelodyStep(false, 64, 0.8f, 0.75f);

        assertEquals(step1, step2);
        assertEquals(step1.hashCode(), step2.hashCode());
        assertNotEquals(step1, step3);
        assertNotEquals(step1, null);
        assertNotEquals(step1, "different type");
    }
}
