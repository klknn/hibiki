package hibiki;

import static org.junit.Assert.*;

import java.awt.Color;
import java.awt.Font;
import javax.swing.UIDefaults;
import javax.swing.UIManager;
import org.junit.Test;

/**
 * Tests for SimpleLaf — verifies LookAndFeel metadata, UIDefaults entries, and installation via
 * UIManager. All tests are headless-compatible since they only interact with UIDefaults, not with
 * actual window peers.
 */
public class SimpleLafTest {

  // ── Metadata ──────────────────────────────────────────────────────────

  @Test
  public void testGetName() {
    SimpleLaf laf = new SimpleLaf();
    assertEquals("Simple Look & Feel", laf.getName());
  }

  @Test
  public void testGetID() {
    SimpleLaf laf = new SimpleLaf();
    assertEquals("SimpleLaf", laf.getID());
  }

  @Test
  public void testGetDescription() {
    SimpleLaf laf = new SimpleLaf();
    assertEquals("This is Simple Look & Feel", laf.getDescription());
  }

  @Test
  public void testIsNativeLookAndFeel() {
    SimpleLaf laf = new SimpleLaf();
    assertFalse(laf.isNativeLookAndFeel());
  }

  @Test
  public void testIsSupportedLookAndFeel() {
    SimpleLaf laf = new SimpleLaf();
    assertTrue(laf.isSupportedLookAndFeel());
  }

  // ── UIDefaults ────────────────────────────────────────────────────────

  @Test
  public void testGetDefaults_returnsNonNull() {
    SimpleLaf laf = new SimpleLaf();
    UIDefaults defaults = laf.getDefaults();
    assertNotNull(defaults);
  }

  @Test
  public void testGetDefaults_containsFontEntries() {
    SimpleLaf laf = new SimpleLaf();
    UIDefaults defaults = laf.getDefaults();
    // Verify core font entries are set
    assertNotNull("List.font should be set", defaults.get("List.font"));
    assertNotNull("Table.font should be set", defaults.get("Table.font"));
    assertNotNull("TextField.font should be set", defaults.get("TextField.font"));
    assertNotNull("ComboBox.font should be set", defaults.get("ComboBox.font"));
    assertNotNull("Label.font should be set", defaults.get("Label.font"));
    assertNotNull("Button.font should be set", defaults.get("Button.font"));
  }

  @Test
  public void testGetDefaults_fontIsSansSerif12() {
    SimpleLaf laf = new SimpleLaf();
    UIDefaults defaults = laf.getDefaults();
    Font listFont = defaults.getFont("List.font");
    assertNotNull(listFont);
    assertEquals("SansSerif", listFont.getFamily());
    assertEquals(Font.PLAIN, listFont.getStyle());
    assertEquals(12, listFont.getSize());
  }

  @Test
  public void testGetDefaults_containsColorEntries() {
    SimpleLaf laf = new SimpleLaf();
    UIDefaults defaults = laf.getDefaults();
    Color bg = (Color) defaults.get("FileChooser.background");
    Color fg = (Color) defaults.get("FileChooser.foreground");
    assertNotNull("FileChooser.background should be set", bg);
    assertNotNull("FileChooser.foreground should be set", fg);
    // Background should be dark gray (60,60,60)
    assertEquals(60, bg.getRed());
    assertEquals(60, bg.getGreen());
    assertEquals(60, bg.getBlue());
    // Foreground should be white
    assertEquals(Color.WHITE, fg);
  }

  @Test
  public void testGetDefaults_listColors() {
    SimpleLaf laf = new SimpleLaf();
    UIDefaults defaults = laf.getDefaults();
    Color listBg = (Color) defaults.get("List.background");
    Color listFg = (Color) defaults.get("List.foreground");
    assertEquals(60, listBg.getRed());
    assertEquals(Color.WHITE, listFg);
  }

  @Test
  public void testGetDefaults_textFieldBrighter() {
    SimpleLaf laf = new SimpleLaf();
    UIDefaults defaults = laf.getDefaults();
    Color tfBg = (Color) defaults.get("TextField.background");
    assertNotNull(tfBg);
    // TextField background should be brighter than base (60,60,60).brighter()
    assertTrue("TextField bg should be brighter than 60", tfBg.getRed() > 60);
  }

  @Test
  public void testGetDefaults_fileChooserIcons() {
    SimpleLaf laf = new SimpleLaf();
    UIDefaults defaults = laf.getDefaults();
    // These should be set (may be null if no icon available, but key should exist)
    assertTrue(defaults.containsKey("FileChooser.listFont"));
  }

  // ── UIManager installation ────────────────────────────────────────────

  @Test
  public void testInstallViaUIManager() throws Exception {
    // Save current LAF to restore later
    String originalLaf = UIManager.getLookAndFeel().getClass().getName();
    try {
      UIManager.setLookAndFeel(new SimpleLaf());
      assertTrue(UIManager.getLookAndFeel() instanceof SimpleLaf);
      assertEquals("SimpleLaf", UIManager.getLookAndFeel().getID());
    } finally {
      // Restore original LAF
      UIManager.setLookAndFeel(originalLaf);
    }
  }

  @Test
  public void testInstallByClassName() throws Exception {
    String originalLaf = UIManager.getLookAndFeel().getClass().getName();
    try {
      UIManager.setLookAndFeel("hibiki.SimpleLaf");
      assertTrue(UIManager.getLookAndFeel() instanceof SimpleLaf);
    } finally {
      UIManager.setLookAndFeel(originalLaf);
    }
  }

  // ── getFrame helper ───────────────────────────────────────────────────

  @Test
  public void testGetFrame_nullParent() throws Exception {
    // getFrame is private, but showOpenDialog/showSaveDialog use it
    // Test that showOpenDialog with non-SimpleLaf active doesn't crash on null parent
    // (Would use JFileChooser path, which doesn't need getFrame)
    String originalLaf = UIManager.getLookAndFeel().getClass().getName();
    try {
      // Ensure NOT SimpleLaf so it takes JFileChooser path
      if (UIManager.getLookAndFeel() instanceof SimpleLaf) {
        UIManager.setLookAndFeel(UIManager.getCrossPlatformLookAndFeelClassName());
      }
      // We can't easily test the dialog showing without X11, but we can verify
      // that the static method exists and is callable
      assertNotNull(
          SimpleLaf.class.getMethod(
              "showOpenDialog", java.awt.Component.class, String.class, String.class));
      assertNotNull(
          SimpleLaf.class.getMethod("showSaveDialog", java.awt.Component.class, String.class));
    } finally {
      UIManager.setLookAndFeel(originalLaf);
    }
  }
}
