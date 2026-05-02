package hibiki.ui;

import static org.junit.Assert.*;

import hibiki.pb.commands.HibikiConfig;
import hibiki.pb.commands.PluginHostMode;
import java.util.List;
import javax.swing.UIManager;
import org.junit.Test;

/**
 * Tests for SettingsDialog logic — config restoration helpers, scale parsing, LAF resolution. All
 * tests are headless-compatible since they only test static helper methods.
 */
public class SettingsDialogTest {

  // ═══════════════════════════════════════════════════════════════════════
  // getAudioDeviceLabel()
  // ═══════════════════════════════════════════════════════════════════════

  @Test
  public void testGetAudioDeviceLabel_notNull() {
    String label = SettingsDialog.getAudioDeviceLabel();
    assertNotNull(label);
    assertTrue(label.startsWith("Audio Engine:"));
  }

  @Test
  public void testGetAudioDeviceLabel_containsPlatformName() {
    String label = SettingsDialog.getAudioDeviceLabel();
    String os = System.getProperty("os.name", "").toLowerCase();
    if (os.contains("win")) {
      assertTrue(label.contains("WASAPI"));
    } else if (os.contains("mac")) {
      assertTrue(label.contains("CoreAudio"));
    } else {
      assertTrue(label.contains("ALSA"));
    }
  }

  // ═══════════════════════════════════════════════════════════════════════
  // getBufferMs()
  // ═══════════════════════════════════════════════════════════════════════

  @Test
  public void testGetBufferMs_nullConfig_returnsDefault() {
    assertEquals(SettingsDialog.DEFAULT_BUFFER_MS, SettingsDialog.getBufferMs(null));
  }

  @Test
  public void testGetBufferMs_emptyConfig_returnsDefault() {
    // Proto3 default for int32 is 0 → treated as "not set"
    HibikiConfig cfg = HibikiConfig.newBuilder().build();
    assertEquals(SettingsDialog.DEFAULT_BUFFER_MS, SettingsDialog.getBufferMs(cfg));
  }

  @Test
  public void testGetBufferMs_configWithValue_returnsValue() {
    HibikiConfig cfg = HibikiConfig.newBuilder().setBufferLatencyMs(50).build();
    assertEquals(50, SettingsDialog.getBufferMs(cfg));
  }

  @Test
  public void testGetBufferMs_configWithLargeValue() {
    HibikiConfig cfg = HibikiConfig.newBuilder().setBufferLatencyMs(2000).build();
    assertEquals(2000, SettingsDialog.getBufferMs(cfg));
  }

  @Test
  public void testGetBufferMs_configWith10ms() {
    HibikiConfig cfg = HibikiConfig.newBuilder().setBufferLatencyMs(10).build();
    assertEquals(10, SettingsDialog.getBufferMs(cfg));
  }

  // ═══════════════════════════════════════════════════════════════════════
  // getUseDoublePrecision()
  // ═══════════════════════════════════════════════════════════════════════

  @Test
  public void testGetUseDoublePrecision_nullConfig() {
    assertFalse(SettingsDialog.getUseDoublePrecision(null));
  }

  @Test
  public void testGetUseDoublePrecision_default() {
    HibikiConfig cfg = HibikiConfig.newBuilder().build();
    assertFalse(SettingsDialog.getUseDoublePrecision(cfg));
  }

  @Test
  public void testGetUseDoublePrecision_true() {
    HibikiConfig cfg = HibikiConfig.newBuilder().setUseDoublePrecision(true).build();
    assertTrue(SettingsDialog.getUseDoublePrecision(cfg));
  }

  @Test
  public void testGetUseDoublePrecision_false() {
    HibikiConfig cfg = HibikiConfig.newBuilder().setUseDoublePrecision(false).build();
    assertFalse(SettingsDialog.getUseDoublePrecision(cfg));
  }

  // ═══════════════════════════════════════════════════════════════════════
  // getHostModeIndex()
  // ═══════════════════════════════════════════════════════════════════════

  @Test
  public void testGetHostModeIndex_nullConfig() {
    assertEquals(0, SettingsDialog.getHostModeIndex(null));
  }

  @Test
  public void testGetHostModeIndex_default_inProcess() {
    HibikiConfig cfg = HibikiConfig.newBuilder().build();
    assertEquals(0, SettingsDialog.getHostModeIndex(cfg));
  }

  @Test
  public void testGetHostModeIndex_inProcess() {
    HibikiConfig cfg =
        HibikiConfig.newBuilder().setPluginHostMode(PluginHostMode.PLUGIN_HOST_IN_PROCESS).build();
    assertEquals(0, SettingsDialog.getHostModeIndex(cfg));
  }

  @Test
  public void testGetHostModeIndex_sandbox() {
    HibikiConfig cfg =
        HibikiConfig.newBuilder()
            .setPluginHostMode(PluginHostMode.PLUGIN_HOST_LOCAL_SANDBOX)
            .build();
    assertEquals(1, SettingsDialog.getHostModeIndex(cfg));
  }

  // ═══════════════════════════════════════════════════════════════════════
  // getRemoteHosts()
  // ═══════════════════════════════════════════════════════════════════════

  @Test
  public void testGetRemoteHosts_nullConfig() {
    List<String> hosts = SettingsDialog.getRemoteHosts(null);
    assertEquals(1, hosts.size());
    assertEquals("localhost:9100", hosts.get(0));
  }

  @Test
  public void testGetRemoteHosts_emptyConfig() {
    HibikiConfig cfg = HibikiConfig.newBuilder().build();
    List<String> hosts = SettingsDialog.getRemoteHosts(cfg);
    assertEquals(1, hosts.size());
    assertEquals("localhost:9100", hosts.get(0));
  }

  @Test
  public void testGetRemoteHosts_singleHost() {
    HibikiConfig cfg = HibikiConfig.newBuilder().addRemoteHosts("192.168.1.42:9100").build();
    List<String> hosts = SettingsDialog.getRemoteHosts(cfg);
    assertEquals(1, hosts.size());
    assertEquals("192.168.1.42:9100", hosts.get(0));
  }

  @Test
  public void testGetRemoteHosts_multipleHosts() {
    HibikiConfig cfg =
        HibikiConfig.newBuilder()
            .addRemoteHosts("host1:9100")
            .addRemoteHosts("host2:9100")
            .addRemoteHosts("host3:9100")
            .build();
    List<String> hosts = SettingsDialog.getRemoteHosts(cfg);
    assertEquals(3, hosts.size());
    assertEquals("host1:9100", hosts.get(0));
    assertEquals("host2:9100", hosts.get(1));
    assertEquals("host3:9100", hosts.get(2));
  }

  // ═══════════════════════════════════════════════════════════════════════
  // parseScaling()
  // ═══════════════════════════════════════════════════════════════════════

  @Test
  public void testParseScaling_100percent() {
    assertEquals(1.0f, SettingsDialog.parseScaling("100%"), 0.001f);
  }

  @Test
  public void testParseScaling_125percent() {
    assertEquals(1.25f, SettingsDialog.parseScaling("125%"), 0.001f);
  }

  @Test
  public void testParseScaling_50percent() {
    assertEquals(0.5f, SettingsDialog.parseScaling("50%"), 0.001f);
  }

  @Test
  public void testParseScaling_200percent() {
    assertEquals(2.0f, SettingsDialog.parseScaling("200%"), 0.001f);
  }

  @Test
  public void testParseScaling_75percent() {
    assertEquals(0.75f, SettingsDialog.parseScaling("75%"), 0.001f);
  }

  @Test
  public void testParseScaling_null_returnsDefault() {
    assertEquals(1.0f, SettingsDialog.parseScaling(null), 0.001f);
  }

  @Test
  public void testParseScaling_empty_returnsDefault() {
    assertEquals(1.0f, SettingsDialog.parseScaling(""), 0.001f);
  }

  @Test
  public void testParseScaling_invalidString_returnsDefault() {
    assertEquals(1.0f, SettingsDialog.parseScaling("abc"), 0.001f);
  }

  // ═══════════════════════════════════════════════════════════════════════
  // resolveLafClassName()
  // ═══════════════════════════════════════════════════════════════════════

  @Test
  public void testResolveLafClassName_simpleLaf() {
    assertEquals("hibiki.SimpleLaf", SettingsDialog.resolveLafClassName("SimpleLaf", null));
  }

  @Test
  public void testResolveLafClassName_flatDarkLaf() {
    assertEquals(
        "com.formdev.flatlaf.FlatDarkLaf", SettingsDialog.resolveLafClassName("FlatDarkLaf", null));
  }

  @Test
  public void testResolveLafClassName_installedLaf() {
    UIManager.LookAndFeelInfo[] lafs = {
      new UIManager.LookAndFeelInfo("Metal", "javax.swing.plaf.metal.MetalLookAndFeel"),
      new UIManager.LookAndFeelInfo("Nimbus", "javax.swing.plaf.nimbus.NimbusLookAndFeel"),
    };
    assertEquals(
        "javax.swing.plaf.metal.MetalLookAndFeel",
        SettingsDialog.resolveLafClassName("Metal", lafs));
    assertEquals(
        "javax.swing.plaf.nimbus.NimbusLookAndFeel",
        SettingsDialog.resolveLafClassName("Nimbus", lafs));
  }

  @Test
  public void testResolveLafClassName_notFound_returnsNull() {
    UIManager.LookAndFeelInfo[] lafs = {
      new UIManager.LookAndFeelInfo("Metal", "javax.swing.plaf.metal.MetalLookAndFeel"),
    };
    assertNull(SettingsDialog.resolveLafClassName("NonExistentLaf", lafs));
  }

  @Test
  public void testResolveLafClassName_nullName_returnsNull() {
    assertNull(SettingsDialog.resolveLafClassName(null, null));
  }

  @Test
  public void testResolveLafClassName_emptyInstalledLafs() {
    assertNull(SettingsDialog.resolveLafClassName("SomeLaf", new UIManager.LookAndFeelInfo[0]));
  }

  // ═══════════════════════════════════════════════════════════════════════
  // Full config restoration round-trip
  // ═══════════════════════════════════════════════════════════════════════

  @Test
  public void testFullConfig_allFieldsSet() {
    HibikiConfig cfg =
        HibikiConfig.newBuilder()
            .setBufferLatencyMs(100)
            .setUseDoublePrecision(true)
            .setPluginHostMode(PluginHostMode.PLUGIN_HOST_LOCAL_SANDBOX)
            .addRemoteHosts("server1:9100")
            .addRemoteHosts("server2:9200")
            .build();

    assertEquals(100, SettingsDialog.getBufferMs(cfg));
    assertTrue(SettingsDialog.getUseDoublePrecision(cfg));
    assertEquals(1, SettingsDialog.getHostModeIndex(cfg));
    assertEquals(2, SettingsDialog.getRemoteHosts(cfg).size());
    assertEquals("server1:9100", SettingsDialog.getRemoteHosts(cfg).get(0));
    assertEquals("server2:9200", SettingsDialog.getRemoteHosts(cfg).get(1));
  }

  @Test
  public void testFullConfig_defaultConfig() {
    // Simulates opening SettingsDialog when config has never been received
    assertEquals(SettingsDialog.DEFAULT_BUFFER_MS, SettingsDialog.getBufferMs(null));
    assertFalse(SettingsDialog.getUseDoublePrecision(null));
    assertEquals(0, SettingsDialog.getHostModeIndex(null));
    assertEquals(1, SettingsDialog.getRemoteHosts(null).size());
  }
}
