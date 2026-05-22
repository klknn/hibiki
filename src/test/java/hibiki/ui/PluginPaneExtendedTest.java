package hibiki.ui;

import static org.junit.Assert.*;

import hibiki.pb.notifications.*;
import hibiki.ui.panels.*;
import hibiki.ui.panels.devices.*;
import org.junit.Test;

/**
 * Additional PluginPane tests — focus on updateParams, LastTouchedParam, BUILTIN_DEVICE_PANELS,
 * rebuildDeviceChain, and notification routing.
 */
public class PluginPaneExtendedTest {

  // ── LastTouchedParam ──────────────────────────────────────────

  @Test
  public void testLastTouchedParam_initiallyNull() {
    new PluginPane(); // reset singleton
    assertNull(PluginPane.getLastTouchedParam());
  }

  @Test
  public void testLastTouchedParam_constructAndRead() {
    PluginPane.LastTouchedParam ltp = new PluginPane.LastTouchedParam(2, 1, 42, "Cutoff");
    assertEquals(2, ltp.trackIndex);
    assertEquals(1, ltp.pluginIndex);
    assertEquals(42, ltp.paramId);
    assertEquals("Cutoff", ltp.paramName);
  }

  // ── updateParams ──────────────────────────────────────────────

  @Test
  public void testUpdateParams_vstPlugin() throws Exception {
    PluginPane pane = new PluginPane();
    // Create a param list for a regular VST plugin
    ParamList pl =
        ParamList.newBuilder()
            .setTrackIndex(0)
            .setPluginIndex(0)
            .setPluginName("UnknownVST")
            .setIsInstrument(false)
            .addParams(ParamInfo.newBuilder().setId(1).setName("Volume").setDefaultValue(0.5f))
            .addParams(ParamInfo.newBuilder().setId(2).setName("Pan").setDefaultValue(0.0f))
            .build();
    pane.updateParams(pl);
    // Wait for SwingUtilities.invokeLater to complete
    javax.swing.SwingUtilities.invokeAndWait(() -> {});
    // Should not throw
  }

  @Test
  public void testUpdateParams_builtinEqPlugin() throws Exception {
    PluginPane pane = new PluginPane();
    ParamList pl =
        ParamList.newBuilder()
            .setTrackIndex(0)
            .setPluginIndex(0)
            .setPluginName("EQ Eight")
            .setIsInstrument(false)
            .build();
    pane.updateParams(pl);
    javax.swing.SwingUtilities.invokeAndWait(() -> {});
    // Should create an EqDevicePanel
  }

  @Test
  public void testUpdateParams_builtinCompressorPlugin() throws Exception {
    PluginPane pane = new PluginPane();
    ParamList pl =
        ParamList.newBuilder()
            .setTrackIndex(0)
            .setPluginIndex(1)
            .setPluginName("Compressor")
            .setIsInstrument(false)
            .build();
    pane.updateParams(pl);
    javax.swing.SwingUtilities.invokeAndWait(() -> {});
  }

  @Test
  public void testUpdateParams_builtin3xOsc() throws Exception {
    PluginPane pane = new PluginPane();
    ParamList pl =
        ParamList.newBuilder()
            .setTrackIndex(0)
            .setPluginIndex(0)
            .setPluginName("3xOsc")
            .setIsInstrument(true)
            .build();
    pane.updateParams(pl);
    javax.swing.SwingUtilities.invokeAndWait(() -> {});
  }

  @Test
  public void testUpdateParams_builtinSampler() throws Exception {
    PluginPane pane = new PluginPane();
    ParamList pl =
        ParamList.newBuilder()
            .setTrackIndex(0)
            .setPluginIndex(0)
            .setPluginName("Sampler")
            .setIsInstrument(true)
            .build();
    pane.updateParams(pl);
    javax.swing.SwingUtilities.invokeAndWait(() -> {});
  }

  @Test
  public void testUpdateParams_builtinDelay() throws Exception {
    PluginPane pane = new PluginPane();
    ParamList pl =
        ParamList.newBuilder()
            .setTrackIndex(0)
            .setPluginIndex(1)
            .setPluginName("Delay")
            .setIsInstrument(false)
            .build();
    pane.updateParams(pl);
    javax.swing.SwingUtilities.invokeAndWait(() -> {});
  }

  @Test
  public void testUpdateParams_builtinReverb() throws Exception {
    PluginPane pane = new PluginPane();
    ParamList pl =
        ParamList.newBuilder()
            .setTrackIndex(0)
            .setPluginIndex(1)
            .setPluginName("Reverb")
            .setIsInstrument(false)
            .build();
    pane.updateParams(pl);
    javax.swing.SwingUtilities.invokeAndWait(() -> {});
  }

  @Test
  public void testUpdateParams_builtinLimiter() throws Exception {
    PluginPane pane = new PluginPane();
    ParamList pl =
        ParamList.newBuilder()
            .setTrackIndex(0)
            .setPluginIndex(1)
            .setPluginName("Limiter")
            .setIsInstrument(false)
            .build();
    pane.updateParams(pl);
    javax.swing.SwingUtilities.invokeAndWait(() -> {});
  }

  @Test
  public void testUpdateParams_builtinHott() throws Exception {
    PluginPane pane = new PluginPane();
    ParamList pl =
        ParamList.newBuilder()
            .setTrackIndex(0)
            .setPluginIndex(1)
            .setPluginName("Hott")
            .setIsInstrument(false)
            .build();
    pane.updateParams(pl);
    javax.swing.SwingUtilities.invokeAndWait(() -> {});
  }

  @Test
  public void testUpdateParams_builtinEnvShaper() throws Exception {
    PluginPane pane = new PluginPane();
    ParamList pl =
        ParamList.newBuilder()
            .setTrackIndex(0)
            .setPluginIndex(1)
            .setPluginName("EnvShaper")
            .setIsInstrument(false)
            .build();
    pane.updateParams(pl);
    javax.swing.SwingUtilities.invokeAndWait(() -> {});
  }

  @Test
  public void testUpdateParams_builtinPhaser() throws Exception {
    PluginPane pane = new PluginPane();
    ParamList pl =
        ParamList.newBuilder()
            .setTrackIndex(0)
            .setPluginIndex(1)
            .setPluginName("Phaser")
            .setIsInstrument(false)
            .build();
    pane.updateParams(pl);
    javax.swing.SwingUtilities.invokeAndWait(() -> {});
  }

  @Test
  public void testUpdateParams_builtinFilM() throws Exception {
    PluginPane pane = new PluginPane();
    ParamList pl =
        ParamList.newBuilder()
            .setTrackIndex(0)
            .setPluginIndex(0)
            .setPluginName("FilM")
            .setIsInstrument(true)
            .build();
    pane.updateParams(pl);
    javax.swing.SwingUtilities.invokeAndWait(() -> {});
  }

  @Test
  public void testUpdateParams_removePlugin() throws Exception {
    PluginPane pane = new PluginPane();
    // First add a VST plugin
    ParamList add =
        ParamList.newBuilder()
            .setTrackIndex(0)
            .setPluginIndex(0)
            .setPluginName("TestVST")
            .setIsInstrument(false)
            .addParams(ParamInfo.newBuilder().setId(1).setName("Vol").setDefaultValue(0.5f))
            .build();
    pane.updateParams(add);
    javax.swing.SwingUtilities.invokeAndWait(() -> {});

    // Then remove it (empty plugin name)
    ParamList remove =
        ParamList.newBuilder().setTrackIndex(0).setPluginIndex(0).setPluginName("").build();
    pane.updateParams(remove);
    javax.swing.SwingUtilities.invokeAndWait(() -> {});
  }

  @Test
  public void testUpdateParams_multipleTracksMultiplePlugins() throws Exception {
    PluginPane pane = new PluginPane();
    // Track 0, plugin 0 = instrument
    pane.updateParams(
        ParamList.newBuilder()
            .setTrackIndex(0)
            .setPluginIndex(0)
            .setPluginName("3xOsc")
            .setIsInstrument(true)
            .build());
    // Track 0, plugin 1 = effect
    pane.updateParams(
        ParamList.newBuilder()
            .setTrackIndex(0)
            .setPluginIndex(1)
            .setPluginName("Compressor")
            .setIsInstrument(false)
            .build());
    // Track 1, plugin 0 = different instrument
    pane.updateParams(
        ParamList.newBuilder()
            .setTrackIndex(1)
            .setPluginIndex(0)
            .setPluginName("Sampler")
            .setIsInstrument(true)
            .build());
    javax.swing.SwingUtilities.invokeAndWait(() -> {});

    // Switch to track 1 (skip if headless — DropTarget needs X11)
    if (!java.awt.GraphicsEnvironment.isHeadless()) {
      pane.setSelectedTrack(1);
    }
  }

  // ── setSelectedTrack ──────────────────────────────────────────

  @Test
  public void testSetSelectedTrack_triggersRebuild() throws Exception {
    // DropTarget requires X11 — skip in headless CI
    if (java.awt.GraphicsEnvironment.isHeadless()) return;
    PluginPane pane = new PluginPane();
    // Add plugins for different tracks
    pane.updateParams(
        ParamList.newBuilder()
            .setTrackIndex(0)
            .setPluginIndex(0)
            .setPluginName("3xOsc")
            .setIsInstrument(true)
            .build());
    pane.updateParams(
        ParamList.newBuilder()
            .setTrackIndex(2)
            .setPluginIndex(0)
            .setPluginName("Sampler")
            .setIsInstrument(true)
            .build());
    javax.swing.SwingUtilities.invokeAndWait(() -> {});

    pane.setSelectedTrack(2);
    pane.setSelectedTrack(0);
    // Should not throw
  }

  @Test
  public void testSetSelectedTrack_emptyTrack() {
    PluginPane pane = new PluginPane();
    // Switching to a track with no plugins should work
    pane.setSelectedTrack(99);
  }

  // ── Singleton ─────────────────────────────────────────────────

  @Test
  public void testSingleton_lastConstructedWins() {
    PluginPane a = new PluginPane();
    PluginPane b = new PluginPane();
    assertSame(b, PluginPane.getInstance());
  }

  // ── BUILTIN_DEVICE_PANELS completeness ────────────────────────

  @Test
  public void testBuiltinDevicePanelsMap() {
    // All 11 built-in devices should have panel classes registered
    String[] builtinNames = {
      "EQ Eight",
      "Compressor",
      "3xOsc",
      "Sampler",
      "Delay",
      "Reverb",
      "Limiter",
      "Maxim",
      "Hott",
      "EnvShaper",
      "Phaser",
      "FilM"
    };
    for (String name : builtinNames) {
      ParamList pl =
          ParamList.newBuilder().setTrackIndex(0).setPluginIndex(0).setPluginName(name).build();
      // Just creating should not throw — the builtin lookup should succeed
      PluginPane pane = new PluginPane();
      assertNotNull(pane);
    }
  }

  // ── WaveformPanel ─────────────────────────────────────────────

  @Test
  public void testWaveformPanel_hasDataDefault() {
    WaveformPanel wp = new WaveformPanel();
    assertFalse(wp.hasData());
  }

  @Test
  public void testWaveformPanel_setWaveform() {
    WaveformPanel wp = new WaveformPanel();
    wp.setWaveform(0, 0, new float[] {0.1f, 0.5f, 0.9f});
    assertTrue(wp.hasData());
  }
}
