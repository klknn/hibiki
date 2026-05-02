package hibiki.ui;

import static org.junit.Assert.*;

import org.junit.Assume;
import org.junit.Before;
import org.junit.Test;

/** Safety-net tests for MarkerDialog — construct and verify field parsing. */
public class MarkerDialogTest {

  @Before
  public void skipIfHeadless() {
    Assume.assumeFalse(java.awt.GraphicsEnvironment.isHeadless());
  }

  @Test
  public void testConstruction() {
    MarkerDialog d = new MarkerDialog(null, "Test", "MyMarker", 5.0f, 140.0f, 3, 4, 120.0f);
    assertFalse(d.isConfirmed());
    assertEquals("MyMarker", d.markerName);
    assertEquals(5.0f, d.positionSec, 0.001f);
    assertEquals(140.0f, d.bpm, 0.001f);
    assertEquals(3, d.beatsPerBar);
    assertEquals(4, d.beatDenominator);
    d.dispose();
  }

  @Test
  public void testConstruction_defaults() {
    MarkerDialog d = new MarkerDialog(null, "New", "", 0.0f, 0, 0, 0, 120.0f);
    assertEquals("", d.markerName);
    assertEquals(0.0f, d.positionSec, 0.001f);
    assertEquals(0, d.bpm, 0.001f);
    d.dispose();
  }

  @Test
  public void testConstruction_noTimeSig() {
    MarkerDialog d = new MarkerDialog(null, "X", "A", 10.0f, 0, 0, 0, 100.0f);
    assertEquals(0, d.beatsPerBar);
    assertEquals(0, d.beatDenominator);
    d.dispose();
  }
}
