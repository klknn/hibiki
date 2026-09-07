package hibiki.android.engine;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

import hibiki.android.model.MelodySequence;
import hibiki.android.model.MelodyStep;
import org.junit.Before;
import org.junit.Test;

public class SineMelodySynthesizerTest {
    private SineMelodySynthesizer synth;

    @Before
    public void setUp() {
        synth = new SineMelodySynthesizer(44100.0, 120.0);
    }

    @Test
    public void testInitialStateOutputsSilence() {
        float[] left = new float[512];
        float[] right = new float[512];
        synth.render(left, right, 512);

        for (int i = 0; i < 512; i++) {
            assertEquals(0.0f, left[i], 0.0001f);
            assertEquals(0.0f, right[i], 0.0001f);
        }
    }

    @Test
    public void testLiveAuditionProducesSineSound() {
        float[] left = new float[512];
        float[] right = new float[512];

        // Trigger Middle C (60)
        synth.triggerAudition(60, 0.9f);
        synth.render(left, right, 512);

        boolean hasAudio = false;
        for (int i = 0; i < 512; i++) {
            if (Math.abs(left[i]) > 0.01f) {
                hasAudio = true;
                break;
            }
        }
        assertTrue("Live audition must render non-zero audio samples", hasAudio);

        // Release audition and render more frames to verify smooth decay
        synth.releaseAudition();
        float[] releaseBufL = new float[2048];
        float[] releaseBufR = new float[2048];
        synth.render(releaseBufL, releaseBufR, 2048);

        // Tail of release should settle back to 0.0
        assertEquals(0.0f, releaseBufL[2047], 0.001f);
    }

    @Test
    public void testPlaybackSequenceRendersWithoutClippingOrDiscontinuity() {
        MelodySequence seq = new MelodySequence(16);
        // Put note on step 0
        seq = seq.withStep(0, MelodyStep.of(60, 1.0f));
        synth.setSequence(seq);
        synth.setPlaying(true);

        // At 120BPM, 1 beat = 0.5s = 22050 samples. 16th note = 5512.5 samples.
        int frames = 6000;
        float[] left = new float[frames];
        float[] right = new float[frames];
        synth.render(left, right, frames);

        // Check bounds [-1.0, 1.0] and smooth continuity (no sudden clicks)
        float maxVal = 0.0f;
        for (int i = 1; i < frames; i++) {
            assertTrue("Sample must not exceed +1.0 limit", left[i] <= 1.0f);
            assertTrue("Sample must not exceed -1.0 limit", left[i] >= -1.0f);
            float diff = Math.abs(left[i] - left[i - 1]);
            assertTrue("Adjacent sample delta must be smooth to avoid clicks (was " + diff + ")", diff < 0.25f);
            if (Math.abs(left[i]) > maxVal) {
                maxVal = Math.abs(left[i]);
            }
        }
        assertTrue("Note on step 0 must have sounded", maxVal > 0.2f);
    }

    @Test
    public void testStepIndexAdvancesDeterministically() {
        synth.setPlaying(true);
        // At 120BPM and 44100Hz, 1 step (16th note) is 44100 * (60/120) / 4 = 5512.5 samples.
        // Rendering 5513 samples crosses the step boundary to step 1.
        int samplesToStep1 = (int) Math.ceil((44100.0 * (60.0 / 120.0)) / 4.0);

        float[] bufL = new float[samplesToStep1];
        float[] bufR = new float[samplesToStep1];

        assertEquals(0, synth.getCurrentStepIndex());

        // Render 1 full step into step 1
        synth.render(bufL, bufR, samplesToStep1);

        // Should now be on step 1
        assertEquals(1, synth.getCurrentStepIndex());
    }

    @Test
    public void testStoppingPlaybackReleasesNotesSmoothlyWithoutDiscontinuity() {
        MelodySequence seq = new MelodySequence(16).withStep(0, MelodyStep.of(60, 1.0f));
        synth.setSequence(seq);
        synth.setPlaying(true);

        // Render into middle of note (at 44.1kHz, 500 samples is well into the note)
        float[] bufL = new float[500];
        float[] bufR = new float[500];
        synth.render(bufL, bufR, 500);

        float lastPlayingSample = bufL[499];
        assertTrue("Note must be active and non-zero before stop", Math.abs(lastPlayingSample) > 0.1f);

        // Stop playback
        synth.setPlaying(false);

        // Render next block: the very next sample must NOT abruptly jump to 0.0
        float[] stopBufL = new float[1024];
        float[] stopBufR = new float[1024];
        synth.render(stopBufL, stopBufR, 1024);

        float delta = Math.abs(stopBufL[0] - lastPlayingSample);
        assertTrue("Sample immediately after stop must transition smoothly without pop (was " + delta + ")",
                delta < 0.25f);

        // And tail of release must settle back to silence
        assertEquals(0.0f, stopBufL[1023], 0.001f);
    }

    @Test
    public void testRenderZeroNegativeOrNullBuffersSafely() {
        float[] bufL = new float[64];
        float[] bufR = new float[64];

        // 0 frames
        synth.render(bufL, bufR, 0);

        // Negative frames
        synth.render(bufL, bufR, -10);

        // Null buffers
        synth.render(null, bufR, 32);
        synth.render(bufL, null, 32);

        // Buffer smaller than requested numFrames (clamps to available without exception)
        float[] smallL = new float[16];
        float[] smallR = new float[16];
        synth.render(smallL, smallR, 64);
    }

    @Test
    public void testExtremeBpmBoundariesAndDynamicChanges() {
        // Minimum BPM boundary (20.0)
        synth.setBpm(20.0);
        assertEquals(20.0, synth.getBpm(), 0.001);

        // Maximum BPM boundary (300.0)
        synth.setBpm(300.0);
        assertEquals(300.0, synth.getBpm(), 0.001);

        // Out-of-range BPMs ignored
        synth.setBpm(120.0);
        synth.setBpm(5.0); // Too low
        assertEquals(120.0, synth.getBpm(), 0.001);
        synth.setBpm(500.0); // Too high
        assertEquals(120.0, synth.getBpm(), 0.001);
        synth.setBpm(Double.NaN);
        assertEquals(120.0, synth.getBpm(), 0.001);

        // Dynamic BPM change during active playback mid-sequence
        synth.setSequence(MelodySequence.createDemoMelody());
        synth.setPlaying(true);
        float[] bufL = new float[1024];
        float[] bufR = new float[1024];
        synth.render(bufL, bufR, 1024);

        // Change BPM to 240 while playing
        synth.setBpm(240.0);
        synth.render(bufL, bufR, 1024);

        // Change BPM to 60 while playing
        synth.setBpm(60.0);
        synth.render(bufL, bufR, 1024);

        for (int i = 0; i < 1024; i++) {
            assertFalse(Float.isNaN(bufL[i]));
            assertFalse(Float.isInfinite(bufL[i]));
            assertTrue(bufL[i] <= 1.0f && bufL[i] >= -1.0f);
        }
    }

    @Test
    public void testMultipleSampleRates() {
        double[] sampleRates = {8000.0, 22050.0, 44100.0, 48000.0, 96000.0, 192000.0};
        for (double sr : sampleRates) {
            SineMelodySynthesizer customSynth = new SineMelodySynthesizer(sr, 120.0);
            customSynth.setSequence(MelodySequence.createDemoMelody());
            customSynth.setPlaying(true);

            float[] left = new float[512];
            float[] right = new float[512];
            customSynth.render(left, right, 512);

            for (int i = 0; i < 512; i++) {
                assertFalse("Sample rate " + sr + " must not produce NaN", Float.isNaN(left[i]));
                assertTrue("Sample rate " + sr + " must stay within [-1.0, 1.0]", left[i] <= 1.0f && left[i] >= -1.0f);
            }
        }
    }

    @Test
    public void testAllStepsActiveLegatoPlayback() {
        MelodySequence seq = new MelodySequence(16);
        for (int i = 0; i < 16; i++) {
            // Consecutive ascending notes with 100% gate (legato)
            seq = seq.withStep(i, new MelodyStep(true, 60 + i, 0.9f, 1.0f));
        }
        synth.setSequence(seq);
        synth.setPlaying(true);

        int totalFrames = 22050; // 0.5s of audio (multiple steps)
        float[] left = new float[totalFrames];
        float[] right = new float[totalFrames];
        synth.render(left, right, totalFrames);

        for (int i = 1; i < totalFrames; i++) {
            assertFalse(Float.isNaN(left[i]));
            float diff = Math.abs(left[i] - left[i - 1]);
            assertTrue("Legato playback must not click across step boundaries (delta=" + diff + ")", diff < 0.25f);
        }
    }

    @Test
    public void testAllStepsRestPlaybackOutputsSilence() {
        MelodySequence emptySeq = new MelodySequence(16); // All steps inactive
        synth.setSequence(emptySeq);
        synth.setPlaying(true);

        float[] left = new float[2048];
        float[] right = new float[2048];
        synth.render(left, right, 2048);

        for (int i = 0; i < 2048; i++) {
            assertEquals(0.0f, left[i], 0.00001f);
            assertEquals(0.0f, right[i], 0.00001f);
        }
    }

    @Test
    public void testDynamicSequenceLengthChangeDuringPlayback() {
        MelodySequence seq16 = MelodySequence.createDemoMelody();
        synth.setSequence(seq16);
        synth.setPlaying(true);

        float[] bufL = new float[2048];
        float[] bufR = new float[2048];
        synth.render(bufL, bufR, 2048);

        // Dynamically shrink sequence to 4 steps while playing
        MelodySequence seq4 = new MelodySequence(4).withStep(0, MelodyStep.of(60, 0.8f));
        synth.setSequence(seq4);
        synth.render(bufL, bufR, 2048);

        int currentStep = synth.getCurrentStepIndex();
        assertTrue("Step index must be within new 4-step sequence range (was " + currentStep + ")",
                currentStep >= 0 && currentStep < 4);
    }

    @Test
    public void testAuditionPitchAndVelocityEdgeCases() {
        // Pitch negative: clamped to 0
        synth.triggerAudition(-20, 0.8f);
        float[] bufL = new float[256];
        float[] bufR = new float[256];
        synth.render(bufL, bufR, 256);
        assertFalse(Float.isNaN(bufL[0]));

        // Pitch excessive: clamped to 127
        synth.triggerAudition(200, 0.8f);
        synth.render(bufL, bufR, 256);
        assertFalse(Float.isNaN(bufL[0]));

        // Velocity NaN: sanitized
        synth.triggerAudition(60, Float.NaN);
        synth.render(bufL, bufR, 256);
        assertFalse(Float.isNaN(bufL[0]));

        // Rapid pitch gliding (glissando) while holding audition
        for (int pitch = 60; pitch <= 72; pitch++) {
            synth.triggerAudition(pitch, 0.9f);
            synth.render(bufL, bufR, 64);
            assertFalse(Float.isNaN(bufL[0]));
        }
        synth.releaseAudition();
    }

    @Test
    public void testSimultaneousSequenceAndAuditionSaturation() {
        // Both sequence voice and audition voice active at maximum velocity
        synth.setSequence(new MelodySequence(16).withStep(0, MelodyStep.of(60, 1.0f)));
        synth.setPlaying(true);
        synth.triggerAudition(60, 1.0f);

        float[] bufL = new float[2048];
        float[] bufR = new float[2048];
        synth.render(bufL, bufR, 2048);

        for (int i = 0; i < 2048; i++) {
            assertFalse(Float.isNaN(bufL[i]));
            assertTrue("Mixed audio must be clamped within [-1.0, 1.0] (was " + bufL[i] + ")",
                    bufL[i] <= 1.0f && bufL[i] >= -1.0f);
        }
    }

    @Test
    public void testLongRunTenCycleLoopingAccurateSampleCount() {
        // At 120BPM, 44100Hz: 1 beat = 22050 samples, 16 steps (4 beats) = 88200 samples.
        // 10 full loops = 882,000 samples.
        synth.setSequence(MelodySequence.createDemoMelody());
        synth.setPlaying(true);

        int blockSize = 4096;
        float[] bufL = new float[blockSize];
        float[] bufR = new float[blockSize];
        int iterations = 882000 / blockSize; // 215 blocks
        long expectedSamples = (long) iterations * blockSize;

        for (int i = 0; i < iterations; i++) {
            synth.render(bufL, bufR, blockSize);
        }

        assertEquals(expectedSamples, synth.getTotalSamplesPlayed());
        int currentStep = synth.getCurrentStepIndex();
        assertTrue("Step index must be valid [0, 15] after long run (was " + currentStep + ")",
                currentStep >= 0 && currentStep < 16);
    }
}
