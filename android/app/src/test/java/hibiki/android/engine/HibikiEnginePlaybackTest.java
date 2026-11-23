package hibiki.android.engine;

import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

import org.junit.After;
import org.junit.Before;
import org.junit.Test;

/**
 * Regression tests reproducing playback freeze and silence bugs in HibikiEngine.
 * Specifically reproduces:
 * 1. setPlayback(true) does not set isPlaying to true in fallback mode.
 * 2. getPlaybackPosition() returns 0.0s continuously without advancing.
 * 3. sendMidiNote fails in fallback mode causing complete silence.
 */
public class HibikiEnginePlaybackTest {

    @Before
    public void setUp() {
        HibikiEngine.initEngine(44100, 50);
    }

    @After
    public void tearDown() {
        HibikiEngine.destroyEngine();
    }

    @Test
    public void testPlaybackStateAndPositionAdvancement() throws InterruptedException {
        // Reproduce: PLAY button pressed, but engine reports not playing
        HibikiEngine.setPlayback(true);
        assertTrue("FAIL: Engine must report isPlaying() == true when playback is started",
                HibikiEngine.isPlaying());

        // Wait for clock to advance
        Thread.sleep(60);

        // Reproduce: playhead position stays at 0.0s, so UI playhead never moves
        double pos = HibikiEngine.getPlaybackPosition();
        assertTrue("FAIL: Playback position must advance beyond 0.0s (was " + pos + "s)",
                pos > 0.001);

        // Stop playback
        HibikiEngine.setPlayback(false);
        assertFalse("FAIL: Engine must report isPlaying() == false when playback is stopped",
                HibikiEngine.isPlaying());
    }

    @Test
    public void testSendMidiNoteSucceedsInSimulatedMode() {
        // Reproduce: Triggering notes in simulated/fallback mode fails and produces no sound
        boolean result = HibikiEngine.sendMidiNote(0, 36, 120, true);
        assertTrue("FAIL: sendMidiNote must succeed (return true) even in simulated mode so sound is produced",
                result);
    }

    @Test
    public void testPlaybackAdvancesPastFirstBarWithoutPrematureLoop() throws InterruptedException {
        // At 120 BPM: 1 beat = 0.5s, 1 bar (4 beats) = 2.0s, 2 bars = 4.0s
        HibikiEngine.setBpm(120.0);
        HibikiEngine.setPlayback(true);

        // Jump to 3.0s (middle of Bar 2)
        HibikiEngine.setPlaybackPosition(3.0);
        Thread.sleep(10);

        double pos = HibikiEngine.getPlaybackPosition();
        // Reproduce: Hardcoded 4-beat (1 bar) loop forced pos % 2.0 = 1.0s (trapped in Bar 1)
        assertTrue("FAIL: Playback position must advance past Bar 1 (2.0s) and stay in Bar 2, but was wrapped to " + pos + "s",
                pos >= 2.5);
    }

    @Test
    public void testLoopingDisabledDoesNotWrap() throws InterruptedException {
        HibikiEngine.setBpm(120.0);
        HibikiEngine.setLooping(false);
        HibikiEngine.setPlayback(true);

        // Jump to 5.0s (Bar 3)
        HibikiEngine.setPlaybackPosition(5.0);
        Thread.sleep(10);

        double pos = HibikiEngine.getPlaybackPosition();
        assertTrue("FAIL: When looping is disabled, playhead must not wrap (expected >= 5.0s, was " + pos + "s)",
                pos >= 5.0);
    }
}
