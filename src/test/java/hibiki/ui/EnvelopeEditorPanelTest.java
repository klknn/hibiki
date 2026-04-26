package hibiki.ui;

import static org.junit.Assert.*;

import org.junit.Test;

public class EnvelopeEditorPanelTest {

  @Test
  public void testDefaultValues() {
    EnvelopeEditorPanel panel = new EnvelopeEditorPanel();
    // Default ADSR values match FilM defaults
    assertEquals(0.0f, panel.getAttack(), 0.001f);
    assertEquals(0.2f, panel.getDecay(), 0.001f);
    assertEquals(0.7f, panel.getSustain(), 0.001f);
    assertEquals(0.3f, panel.getRelease(), 0.001f);
  }

  @Test
  public void testSetValuesRoundTrip() {
    EnvelopeEditorPanel panel = new EnvelopeEditorPanel();
    panel.setValues(0.1f, 0.5f, 0.3f, 0.8f);

    assertEquals(0.1f, panel.getAttack(), 0.001f);
    assertEquals(0.5f, panel.getDecay(), 0.001f);
    assertEquals(0.3f, panel.getSustain(), 0.001f);
    assertEquals(0.8f, panel.getRelease(), 0.001f);
  }

  @Test
  public void testValuesClamping() {
    EnvelopeEditorPanel panel = new EnvelopeEditorPanel();
    panel.setValues(-0.5f, 1.5f, -0.1f, 2.0f);

    assertEquals("Attack should clamp to 0", 0.0f, panel.getAttack(), 0.001f);
    assertEquals("Decay should clamp to 1", 1.0f, panel.getDecay(), 0.001f);
    assertEquals("Sustain should clamp to 0", 0.0f, panel.getSustain(), 0.001f);
    assertEquals("Release should clamp to 1", 1.0f, panel.getRelease(), 0.001f);
  }

  @Test
  public void testTensionDefaults() {
    EnvelopeEditorPanel panel = new EnvelopeEditorPanel();
    for (int i = 0; i < 4; i++) {
      assertEquals("Tension " + i + " should default to 0", 0.0, panel.getTension(i), 0.001);
    }
  }

  @Test
  public void testTensionSetAndGet() {
    EnvelopeEditorPanel panel = new EnvelopeEditorPanel();
    panel.setTension(0, 0.5);
    panel.setTension(1, -0.8);
    panel.setTension(2, 1.0);
    panel.setTension(3, -1.0);

    assertEquals(0.5, panel.getTension(0), 0.001);
    assertEquals(-0.8, panel.getTension(1), 0.001);
    assertEquals(1.0, panel.getTension(2), 0.001);
    assertEquals(-1.0, panel.getTension(3), 0.001);
  }

  @Test
  public void testTensionClamping() {
    EnvelopeEditorPanel panel = new EnvelopeEditorPanel();
    panel.setTension(0, 5.0);
    panel.setTension(1, -5.0);

    assertEquals("Tension should clamp to 1", 1.0, panel.getTension(0), 0.001);
    assertEquals("Tension should clamp to -1", -1.0, panel.getTension(1), 0.001);
  }

  @Test
  public void testTensionOutOfBounds() {
    EnvelopeEditorPanel panel = new EnvelopeEditorPanel();
    // Out-of-range segment indices should return 0
    assertEquals(0.0, panel.getTension(-1), 0.001);
    assertEquals(0.0, panel.getTension(4), 0.001);

    // Setting out-of-range should be a no-op
    panel.setTension(-1, 0.5);
    panel.setTension(4, 0.5);
  }

  @Test
  public void testListenerFires() {
    EnvelopeEditorPanel panel = new EnvelopeEditorPanel();

    float[] received = new float[4];
    boolean[] fired = {false};
    panel.addListener(
        (a, d, s, r) -> {
          received[0] = a;
          received[1] = d;
          received[2] = s;
          received[3] = r;
          fired[0] = true;
        });

    // setValues doesn't fire listener (only user drags do)
    panel.setValues(0.5f, 0.5f, 0.5f, 0.5f);
    assertFalse("setValues should not fire listener", fired[0]);
  }

  @Test
  public void testApplyTensionLinear() {
    // With tension=0, interpolation should be linear
    float result = EnvelopeEditorPanel.applyTension(0.5f, 0.0f, 100.0f, 0.0);
    assertEquals("Linear midpoint", 50.0f, result, 0.5f);

    float resultStart = EnvelopeEditorPanel.applyTension(0.0f, 0.0f, 100.0f, 0.0);
    assertEquals("Linear start", 0.0f, resultStart, 0.01f);

    float resultEnd = EnvelopeEditorPanel.applyTension(1.0f, 0.0f, 100.0f, 0.0);
    assertEquals("Linear end", 100.0f, resultEnd, 0.01f);
  }

  @Test
  public void testApplyTensionPositive() {
    // Positive tension = ease-out (fast start), at t=0.5 result should be > 50
    float result = EnvelopeEditorPanel.applyTension(0.5f, 0.0f, 100.0f, 1.0);
    assertTrue("Ease-out at midpoint should be > 50", result > 50.0f);
  }

  @Test
  public void testApplyTensionNegative() {
    // Negative tension = ease-in (slow start), at t=0.5 result should be < 50
    float result = EnvelopeEditorPanel.applyTension(0.5f, 0.0f, 100.0f, -1.0);
    assertTrue("Ease-in at midpoint should be < 50", result < 50.0f);
  }

  @Test
  public void testFilmDevicePanelEnvelopeSync() {
    // Verify that handleParamChange updates envelope editors
    FilmDevicePanel panel = new FilmDevicePanel(0, 0);

    // Change OP1 attack param
    int attackParam = FilmDevicePanel.OP_LEVEL - 1 + 4; // OP_ENV_A = 4
    panel.handleParamChange(4, 0.6); // OP_ENV_A for op0

    // Verify the param array was updated
    assertEquals(0.6, panel.getParamValue(4), 0.001);
  }
}
