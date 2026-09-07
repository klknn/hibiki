package hibiki.android.engine;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertTrue;

import hibiki.android.model.MelodySequence;
import hibiki.android.model.MelodyStep;
import org.junit.After;
import org.junit.Before;
import org.junit.Test;

public class MelodySequencerIntegrationTest {

    @Before
    public void setUp() {
        HibikiEngine.initEngine(44100, 50);
    }

    @After
    public void tearDown() {
        HibikiEngine.destroyEngine();
    }

    @Test
    public void testEngineMelodySequenceIntegration() {
        MelodySequence seq = MelodySequence.createDemoMelody();
        HibikiEngine.setMelodySequence(seq);
        assertNotNull(HibikiEngine.getMelodySequence());
        assertEquals(16, HibikiEngine.getMelodySequence().getLengthSteps());

        // Verify tempo and playback synchronization
        HibikiEngine.setMelodyBpm(130.0);
        assertEquals(130.0, HibikiEngine.getBpm(), 0.01);
        assertEquals(130.0, HibikiEngine.getMelodySynthesizer().getBpm(), 0.01);

        HibikiEngine.setMelodyPlaying(true);
        assertTrue(HibikiEngine.isMelodyPlaying());
        assertTrue(HibikiEngine.isPlaying());

        HibikiEngine.setMelodyPlaying(false);
        assertFalse(HibikiEngine.isMelodyPlaying());
        assertFalse(HibikiEngine.isPlaying());
    }

    @Test
    public void testAuditionToneIntegration() {
        // Trigger Middle C audition
        HibikiEngine.triggerAudition(60, 0.9f);

        SineMelodySynthesizer synth = HibikiEngine.getMelodySynthesizer();
        float[] left = new float[256];
        float[] right = new float[256];
        synth.render(left, right, 256);

        boolean hasAudio = false;
        for (int i = 0; i < 256; i++) {
            if (Math.abs(left[i]) > 0.05f) {
                hasAudio = true;
                break;
            }
        }
        assertTrue("Audition tone must produce sine wave output", hasAudio);

        // Release audition
        HibikiEngine.releaseAudition();
        synth.render(left, right, 256);
    }

    @Test
    public void testSequenceStepParamModification() {
        MelodySequence seq = new MelodySequence(16);
        // Step 4 set to G4 (67) with 0.75 velocity
        seq = seq.withStep(4, MelodyStep.of(67, 0.75f));
        HibikiEngine.setMelodySequence(seq);

        MelodyStep retrieved = HibikiEngine.getMelodySequence().getStep(4);
        assertTrue(retrieved.isActive());
        assertEquals(67, retrieved.getPitch());
        assertEquals(0.75f, retrieved.getVelocity(), 0.001f);
        assertEquals("G4", retrieved.getNoteName());
    }

    @Test
    public void testRapidPlaybackToggleAndPositionReset() {
        for (int i = 0; i < 20; i++) {
            HibikiEngine.setMelodyPlaying(true);
            assertTrue(HibikiEngine.isMelodyPlaying());

            HibikiEngine.setMelodyPlaying(false);
            assertFalse(HibikiEngine.isMelodyPlaying());

            HibikiEngine.resetPlaybackPosition();
            assertEquals(0.0, HibikiEngine.getPlaybackPosition(), 0.0001);
            assertEquals(0, HibikiEngine.getMelodyCurrentStep());
        }
    }

    @Test
    public void testExtremeMidiNoteInputs() {
        // Boundary notes (0 and 127)
        assertTrue(HibikiEngine.sendMidiNote(0, 0, 127, true));
        assertTrue(HibikiEngine.sendMidiNote(0, 0, 0, false));

        assertTrue(HibikiEngine.sendMidiNote(0, 127, 127, true));
        assertTrue(HibikiEngine.sendMidiNote(0, 127, 0, false));

        // Note off without note on
        assertTrue(HibikiEngine.sendMidiNote(0, 60, 0, false));

        // Velocity 0 note on (treated as note off or silent)
        assertTrue(HibikiEngine.sendMidiNote(0, 60, 0, true));
    }

    @Test
    public void testConcurrentEngineAccessFromMultipleThreads() throws InterruptedException {
        int numThreads = 4;
        java.util.concurrent.ExecutorService executor = java.util.concurrent.Executors.newFixedThreadPool(numThreads);
        java.util.concurrent.atomic.AtomicBoolean hasError = new java.util.concurrent.atomic.AtomicBoolean(false);

        HibikiEngine.setMelodyPlaying(true);

        for (int t = 0; t < numThreads; t++) {
            final int threadId = t;
            executor.submit(() -> {
                try {
                    for (int i = 0; i < 100; i++) {
                        if (threadId == 0) {
                            HibikiEngine.setMelodyBpm(100.0 + (i % 60));
                        } else if (threadId == 1) {
                            MelodySequence seq = new MelodySequence(16).withStep(i % 16, MelodyStep.of(60 + (i % 12), 0.8f));
                            HibikiEngine.setMelodySequence(seq);
                        } else if (threadId == 2) {
                            HibikiEngine.triggerAudition(60 + (i % 12), 0.7f);
                            HibikiEngine.releaseAudition();
                        } else {
                            float[] l = new float[128];
                            float[] r = new float[128];
                            HibikiEngine.getMelodySynthesizer().render(l, r, 128);
                        }
                    }
                } catch (Throwable e) {
                    hasError.set(true);
                }
            });
        }

        executor.shutdown();
        boolean finished = executor.awaitTermination(5, java.util.concurrent.TimeUnit.SECONDS);
        assertTrue("Concurrent tasks must complete in time", finished);
        assertFalse("Concurrent operations must not throw exceptions or crash", hasError.get());

        HibikiEngine.setMelodyPlaying(false);
    }
}
