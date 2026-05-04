package hibiki.ui;

import static org.junit.Assert.*;

import org.junit.Before;
import org.junit.Test;

/**
 * Tests for AudioEditorPanel: construction, selection, zoom, data updates. Runs in headless mode
 * (no BackendManager connection needed).
 */
public class AudioEditorPanelTest {

  @Before
  public void setUp() {
    System.setProperty("java.awt.headless", "true");
  }

  @Test
  public void testConstruction() {
    AudioEditorPanel panel = new AudioEditorPanel();
    assertNotNull(panel);
    assertEquals(0.0f, panel.getViewStart(), 0.001f);
    assertEquals(1.0f, panel.getViewEnd(), 0.001f);
    assertFalse(panel.hasSelection());
  }

  @Test
  public void testSetWaveformData() {
    AudioEditorPanel panel = new AudioEditorPanel();
    float[] wf = new float[256];
    for (int i = 0; i < wf.length; i++) wf[i] = (float) i / wf.length;
    panel.setWaveformData(wf, "test.wav", 2.5f, 44100, 2);
    // Panel should accept the data without errors
    assertNotNull(panel);
  }

  @Test
  public void testSelectionDefaults() {
    AudioEditorPanel panel = new AudioEditorPanel();
    assertEquals(0.0f, panel.getSelStart(), 0.001f);
    assertEquals(0.0f, panel.getSelEnd(), 0.001f);
    assertFalse(panel.hasSelection());
  }

  @Test
  public void testViewRange() {
    AudioEditorPanel panel = new AudioEditorPanel();
    // Default should show full range
    assertEquals(0.0f, panel.getViewStart(), 0.001f);
    assertEquals(1.0f, panel.getViewEnd(), 0.001f);
  }
}
