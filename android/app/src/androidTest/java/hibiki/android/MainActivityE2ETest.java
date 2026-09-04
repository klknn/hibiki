package hibiki.android;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertTrue;

import android.view.View;
import androidx.test.core.app.ActivityScenario;
import androidx.test.ext.junit.runners.AndroidJUnit4;
import androidx.test.filters.LargeTest;
import hibiki.android.engine.HibikiEngine;
import hibiki.android.model.ChannelState;
import hibiki.android.model.ViewMode;
import hibiki.android.ui.components.HeaderBarView;
import hibiki.android.ui.views.ArrangerView;
import hibiki.android.ui.views.InstrumentView;
import hibiki.android.ui.views.MixerView;
import hibiki.android.ui.views.ProjectView;
import hibiki.android.ui.views.TrackerView;
import org.junit.Test;
import org.junit.runner.RunWith;

/**
 * End-to-end (E2E) instrumented testing suite running on Android Simulator / Device.
 * Verifies full application lifecycle, UI interactions, tab routing, state synchronization,
 * and native C++ JNI audio engine integration.
 */
@RunWith(AndroidJUnit4.class)
@LargeTest
public class MainActivityE2ETest {

    @Test
    public void testActivityLaunchAndInitialUIState() {
        try (ActivityScenario<MainActivity> scenario = ActivityScenario.launch(MainActivity.class)) {
            scenario.onActivity(activity -> {
                assertNotNull("MainActivity should be initialized", activity);

                HeaderBarView header = activity.getHeaderBar();
                assertNotNull("HeaderBarView must be present", header);
                assertEquals("HIBIKI", header.getLogoText().getText().toString());
                assertEquals("128 BPM", header.getBpmDisplay().getText().toString());

                // Default view is TRACKER
                assertEquals(ViewMode.TRACKER, activity.getCurrentView());
                assertEquals(View.VISIBLE, activity.getTrackerView().getVisibility());
                assertEquals(View.GONE, activity.getArrangerView().getVisibility());
                assertEquals(View.GONE, activity.getInstrumentView().getVisibility());
                assertEquals(View.GONE, activity.getMixerView().getVisibility());
                assertEquals(View.GONE, activity.getProjectView().getVisibility());

                // Default 4 groovebox channels
                assertEquals(4, activity.getChannels().size());
                assertFalse("Engine should not be playing on launch", activity.isPlaying());
            });
        }
    }

    @Test
    public void testTransportPlayStopLifecycle() {
        try (ActivityScenario<MainActivity> scenario = ActivityScenario.launch(MainActivity.class)) {
            // 1. Trigger PLAY
            scenario.onActivity(activity -> {
                HeaderBarView header = activity.getHeaderBar();
                header.getBtnPlayPause().performClick();
                assertTrue("App state should be playing after click", activity.isPlaying());
                assertEquals("PAUSE", header.getBtnPlayPause().getText().toString());
            });

            // Allow playback clock to advance
            try {
                Thread.sleep(200);
            } catch (InterruptedException ignored) {}

            // 2. Trigger STOP
            scenario.onActivity(activity -> {
                HeaderBarView header = activity.getHeaderBar();
                header.getBtnStop().performClick();
                assertFalse("App state should stop playing after STOP click", activity.isPlaying());
                assertEquals("PLAY", header.getBtnPlayPause().getText().toString());
                assertEquals(0.0, activity.getPlayheadSec(), 0.001);
                assertEquals(0, activity.getCurrentStepIndex());
            });
        }
    }

    @Test
    public void testBpmControlAndAdjustment() {
        try (ActivityScenario<MainActivity> scenario = ActivityScenario.launch(MainActivity.class)) {
            scenario.onActivity(activity -> {
                HeaderBarView header = activity.getHeaderBar();
                assertEquals(128.0, activity.getBpm(), 0.01);

                // Simulate BPM + button tap via listener
                activity.getHeaderBar().updateState(
                        activity.isPlaying(),
                        activity.isRecording(),
                        activity.isLooping(),
                        132.0,
                        activity.getPlayheadSec(),
                        activity.getCurrentView()
                );
                assertEquals("132 BPM", header.getBpmDisplay().getText().toString());
            });
        }
    }

    @Test
    public void testTabNavigationAcrossAllViews() {
        try (ActivityScenario<MainActivity> scenario = ActivityScenario.launch(MainActivity.class)) {
            // Switch to ARRANGER
            scenario.onActivity(activity -> {
                activity.performSwitchView(ViewMode.ARRANGER);
                assertEquals(ViewMode.ARRANGER, activity.getCurrentView());
                assertEquals(View.VISIBLE, activity.getArrangerView().getVisibility());
                assertEquals(View.GONE, activity.getTrackerView().getVisibility());
            });

            // Switch to INSTRUMENT
            scenario.onActivity(activity -> {
                activity.performSwitchView(ViewMode.INSTRUMENT);
                assertEquals(ViewMode.INSTRUMENT, activity.getCurrentView());
                assertEquals(View.VISIBLE, activity.getInstrumentView().getVisibility());
                assertEquals(View.GONE, activity.getArrangerView().getVisibility());
            });

            // Switch to MIXER
            scenario.onActivity(activity -> {
                activity.performSwitchView(ViewMode.MIXER);
                assertEquals(ViewMode.MIXER, activity.getCurrentView());
                assertEquals(View.VISIBLE, activity.getMixerView().getVisibility());
                assertEquals(View.GONE, activity.getInstrumentView().getVisibility());
            });

            // Switch to PROJECT
            scenario.onActivity(activity -> {
                activity.performSwitchView(ViewMode.PROJECT);
                assertEquals(ViewMode.PROJECT, activity.getCurrentView());
                assertEquals(View.VISIBLE, activity.getProjectView().getVisibility());
                assertEquals(View.GONE, activity.getMixerView().getVisibility());
            });

            // Switch back to TRACKER
            scenario.onActivity(activity -> {
                activity.performSwitchView(ViewMode.TRACKER);
                assertEquals(ViewMode.TRACKER, activity.getCurrentView());
                assertEquals(View.VISIBLE, activity.getTrackerView().getVisibility());
                assertEquals(View.GONE, activity.getProjectView().getVisibility());
            });
        }
    }

    @Test
    public void testInstrumentViewTabsAndMacros() {
        try (ActivityScenario<MainActivity> scenario = ActivityScenario.launch(MainActivity.class)) {
            scenario.onActivity(activity -> {
                activity.performSwitchView(ViewMode.INSTRUMENT);
                InstrumentView instrument = activity.getInstrumentView();
                assertNotNull("InstrumentView should be present", instrument);

                // Default active tab is 0 (Pads)
                assertEquals(0, instrument.getActiveTab());
                assertNotNull("DrumPadsView must be present", instrument.getDrumPadsView());
                assertEquals(View.VISIBLE, instrument.getDrumPadsView().getVisibility());

                // Switch to Scale Keyboard tab
                instrument.switchTabDirect(1);
                assertEquals(1, instrument.getActiveTab());
                assertEquals(View.GONE, instrument.getDrumPadsView().getVisibility());
                assertNotNull("ScaleKeyboardView must be present", instrument.getScaleKeyboardView());

                // Verify 4 macros exist
                assertEquals(4, instrument.getMacros().size());
                assertEquals("CUTOFF", instrument.getMacros().get(0).getName());
            });
        }
    }

    @Test
    public void testMixerViewChannelStripsAndMuteSolo() {
        try (ActivityScenario<MainActivity> scenario = ActivityScenario.launch(MainActivity.class)) {
            scenario.onActivity(activity -> {
                activity.performSwitchView(ViewMode.MIXER);
                MixerView mixer = activity.getMixerView();
                assertNotNull("MixerView should be present", mixer);

                assertEquals(4, mixer.getChannels().size());
                assertEquals(4, mixer.getContainer().getChildCount());

                ChannelState ch0 = mixer.getChannels().get(0);
                assertFalse("Channel 0 should not be muted initially", ch0.isMuted());

                // Toggle mute on channel 0
                ChannelState updated = ch0.withMuted(true);
                activity.getChannels().set(0, updated);
                mixer.setChannels(activity.getChannels());

                assertTrue("Channel 0 should be muted after toggle", mixer.getChannels().get(0).isMuted());
            });
        }
    }

    @Test
    public void testNativeEngineJniHealthCheck() {
        // Direct verification that JNI bindings operate cleanly within the simulator process
        double currentBpm = HibikiEngine.getBpm();
        assertTrue("BPM should be positive", currentBpm > 0);

        boolean playing = HibikiEngine.isPlaying();
        assertFalse("Engine should report stopped initially", playing);

        double pos = HibikiEngine.getPlaybackPosition();
        assertTrue("Playback position should be non-negative", pos >= 0.0);

        // Test pollNotification does not crash
        byte[] notif = HibikiEngine.pollNotification();
        assertNotNull("pollNotification should return a byte array (possibly empty)", notif);
    }
}
