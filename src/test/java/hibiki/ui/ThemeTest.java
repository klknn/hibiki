package hibiki.ui;

import static org.junit.Assert.*;

import java.awt.Color;
import org.junit.Test;

public class ThemeTest {
  @Test
  public void testThemeColors() {
    assertNotNull("BG_DARK should not be null", Theme.getInstance().BG_DARK);
    assertNotNull("ACCENT_ORANGE should not be null", Theme.getInstance().ACCENT_ORANGE);
    assertNotNull("ACCENT_BLUE should not be null", Theme.getInstance().ACCENT_BLUE);
    assertNotNull("ACCENT_GREEN should not be null", Theme.getInstance().ACCENT_GREEN);

    // Verify some specific values if needed, but primarily checking existence
    assertEquals(new Color(34, 34, 34), Theme.getInstance().BG_DARK);
  }

  @Test
  public void testThemeFonts() {
    assertNotNull("FONT_UI should not be null", Theme.getInstance().FONT_UI);
    assertNotNull("FONT_DISPLAY should not be null", Theme.getInstance().FONT_DISPLAY);
  }

  @Test
  public void testGetCurrentPreset() {
    assertNotNull(Theme.getInstance().getCurrentPreset());
  }

  @Test
  public void testGetScaling() {
    float scaling = Theme.getInstance().getScaling();
    assertTrue("Scaling should be positive", scaling > 0);
  }

  @Test
  public void testGetBaseFontSize() {
    int fontSize = Theme.getInstance().getBaseFontSize();
    assertTrue("Font size should be > 0", fontSize > 0);
  }

  @Test
  public void testGetFontFamily() {
    String family = Theme.getInstance().getFontFamily();
    assertNotNull("Font family should not be null", family);
  }

  @Test
  public void testScaleInt() {
    int base = 100;
    int scaled = Theme.getInstance().scale(base);
    assertTrue("Scaled value should be positive", scaled > 0);
  }

  @Test
  public void testScaleFloat() {
    float base = 10.0f;
    float scaled = Theme.getInstance().scale(base);
    assertTrue("Scaled value should be positive", scaled > 0);
  }

  @Test
  public void testThemeListener() {
    final boolean[] called = {false};
    Theme.ThemeListener listener = () -> called[0] = true;
    Theme.getInstance().addListener(listener);

    // Trigger an update
    Theme.Preset current = Theme.getInstance().getCurrentPreset();
    float scaling = Theme.getInstance().getScaling();
    int fontSize = Theme.getInstance().getBaseFontSize();
    Theme.getInstance().update(current, scaling, fontSize);

    assertTrue("Listener should have been called", called[0]);
  }

  @Test
  public void testUpdateWithPreset_abletonDark() {
    Theme.getInstance().update(Theme.Preset.ABLETON_DARK, 1.0f, 11);
    assertNotNull(Theme.getInstance().BG_DARK);
    assertNotNull(Theme.getInstance().ACCENT_ORANGE);
  }

  @Test
  public void testUpdateWithPreset_abletonLight() {
    Theme.getInstance().update(Theme.Preset.ABLETON_LIGHT, 1.0f, 11);
    assertNotNull(Theme.getInstance().BG_DARK);
  }

  @Test
  public void testUpdateWithPreset_solarizedDark() {
    Theme.getInstance().update(Theme.Preset.SOLARIZED_DARK, 1.0f, 11);
    assertNotNull(Theme.getInstance().BG_DARK);
  }

  @Test
  public void testUpdateWithPreset_solarizedLight() {
    Theme.getInstance().update(Theme.Preset.SOLARIZED_LIGHT, 1.0f, 11);
    assertNotNull(Theme.getInstance().BG_DARK);
  }

  @Test
  public void testUpdateWithPreset_win95() {
    Theme.getInstance().update(Theme.Preset.WIN95, 1.0f, 11);
    assertNotNull(Theme.getInstance().BG_DARK);
  }

  @Test
  public void testUpdateWithFontFamily() {
    Theme.getInstance().update(Theme.Preset.ABLETON_DARK, 1.0f, 12, "Monospaced");
    assertEquals("Monospaced", Theme.getInstance().getFontFamily());
    // Reset
    Theme.getInstance().update(Theme.Preset.ABLETON_DARK, 1.0f, 11);
  }

  @Test
  public void testUpdateWithScaling() {
    Theme.getInstance().update(Theme.Preset.ABLETON_DARK, 1.5f, 11);
    assertEquals(1.5f, Theme.getInstance().getScaling(), 0.01f);
    assertEquals(150, Theme.getInstance().scale(100));
    // Reset
    Theme.getInstance().update(Theme.Preset.ABLETON_DARK, 1.0f, 11);
  }

  @Test
  public void testPresetEnumValues() {
    Theme.Preset[] presets = Theme.Preset.values();
    assertTrue("Should have multiple presets", presets.length >= 2);
  }

  @Test
  public void testAllColors() {
    Theme t = Theme.getInstance();
    assertNotNull(t.BG_DARKER);
    assertNotNull(t.BG_DARK);
    assertNotNull(t.BG_MEDIUM);
    assertNotNull(t.PANEL_BG);
    assertNotNull(t.PANEL_BG_LIGHT);
    assertNotNull(t.BORDER);
    assertNotNull(t.TEXT_BRIGHT);
    assertNotNull(t.TEXT_LIGHT);
    assertNotNull(t.TEXT_DIM);
    assertNotNull(t.TRACK_HEADER);
    assertNotNull(t.CLIP_MIDI);
    assertNotNull(t.CLIP_AUDIO);
    assertNotNull(t.CLIP_PLAYING);
  }

  @Test
  public void testCreateButton() {
    javax.swing.JButton btn = Theme.getInstance().createButton("Test", e -> {});
    assertNotNull(btn);
    assertEquals("Test", btn.getText());
  }

  @Test
  public void testCreateFlatButton() {
    javax.swing.JButton btn = Theme.getInstance().createFlatButton("Flat", e -> {});
    assertNotNull(btn);
    assertEquals("Flat", btn.getText());
  }

  @Test
  public void testFontUIBold() {
    assertNotNull(Theme.getInstance().FONT_UI_BOLD);
  }
}
