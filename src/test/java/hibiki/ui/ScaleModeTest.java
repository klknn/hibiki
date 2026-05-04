package hibiki.ui;

import static org.junit.Assert.*;

import org.junit.Test;

/** Tests for ScaleMode: containsPitch, snapPitch, and enumeration integrity. */
public class ScaleModeTest {

  // ── containsPitch ─────────────────────────────────────────────────

  @Test
  public void testChromaticContainsAllPitches() {
    for (int p = 0; p < 128; p++) {
      assertTrue("Chromatic should contain pitch " + p, ScaleMode.CHROMATIC.containsPitch(0, p));
    }
  }

  @Test
  public void testMajorScaleContainsPitch() {
    // C major: C D E F G A B
    int[] expected = {0, 2, 4, 5, 7, 9, 11};
    for (int interval : expected) {
      assertTrue(
          "C Major should contain " + interval, ScaleMode.MAJOR.containsPitch(0, 60 + interval));
    }
    // C# and D# are not in C major
    assertFalse(ScaleMode.MAJOR.containsPitch(0, 61));
    assertFalse(ScaleMode.MAJOR.containsPitch(0, 63));
  }

  @Test
  public void testMajorScaleWithDifferentRoot() {
    // D major (root=2): D E F# G A B C#
    assertTrue(ScaleMode.MAJOR.containsPitch(2, 62)); // D
    assertTrue(ScaleMode.MAJOR.containsPitch(2, 64)); // E
    assertTrue(ScaleMode.MAJOR.containsPitch(2, 66)); // F#
    assertFalse(ScaleMode.MAJOR.containsPitch(2, 65)); // F natural - not in D major
  }

  @Test
  public void testMinorNaturalContainsPitch() {
    // A minor (root=9): A B C D E F G
    assertTrue(ScaleMode.NATURAL_MINOR.containsPitch(9, 69)); // A
    assertTrue(ScaleMode.NATURAL_MINOR.containsPitch(9, 71)); // B
    assertTrue(ScaleMode.NATURAL_MINOR.containsPitch(9, 72)); // C
    assertFalse(ScaleMode.NATURAL_MINOR.containsPitch(9, 70)); // Bb
  }

  @Test
  public void testBluesScaleContainsPitch() {
    // C blues: C Eb F F# G Bb
    int[] expected = {0, 3, 5, 6, 7, 10};
    for (int interval : expected) {
      assertTrue(
          "C Blues should contain " + interval, ScaleMode.BLUES.containsPitch(0, 60 + interval));
    }
    assertFalse(ScaleMode.BLUES.containsPitch(0, 62)); // D not in blues
  }

  @Test
  public void testPentatonicMajorContainsPitch() {
    // C pentatonic major: C D E G A
    int[] expected = {0, 2, 4, 7, 9};
    for (int interval : expected) {
      assertTrue(ScaleMode.PENTATONIC_MAJOR.containsPitch(0, 60 + interval));
    }
    assertFalse(ScaleMode.PENTATONIC_MAJOR.containsPitch(0, 65)); // F not in pentatonic
  }

  @Test
  public void testContainsPitch_allOctaves() {
    // A pitch that's in the scale should be in the scale at all octaves
    for (int octave = 0; octave < 10; octave++) {
      int pitch = octave * 12; // C at each octave
      if (pitch < 128) {
        assertTrue(ScaleMode.MAJOR.containsPitch(0, pitch));
      }
    }
  }

  // ── snapPitch ─────────────────────────────────────────────────────

  @Test
  public void testSnapPitch_chromaticIsIdentity() {
    for (int p = 0; p < 128; p++) {
      assertEquals(p, ScaleMode.CHROMATIC.snapPitch(0, p));
    }
  }

  @Test
  public void testSnapPitch_majorSnapsToNearestDegree() {
    // C major snap: C# should snap to C or D
    int snapped = ScaleMode.MAJOR.snapPitch(0, 61); // C#
    assertTrue("C# should snap to C(60) or D(62)", snapped == 60 || snapped == 62);
  }

  @Test
  public void testSnapPitch_inScalePitchUnchanged() {
    // E (64) is in C major -> should snap to itself
    assertEquals(64, ScaleMode.MAJOR.snapPitch(0, 64));
  }

  @Test
  public void testSnapPitch_boundsLow() {
    int snapped = ScaleMode.MAJOR.snapPitch(0, 0); // C0
    assertTrue(snapped >= 0);
    assertTrue(ScaleMode.MAJOR.containsPitch(0, snapped));
  }

  @Test
  public void testSnapPitch_boundsHigh() {
    int snapped = ScaleMode.MAJOR.snapPitch(0, 127); // G9
    assertTrue(snapped >= 0 && snapped <= 127);
    assertTrue(ScaleMode.MAJOR.containsPitch(0, snapped));
  }

  @Test
  public void testSnapPitch_minorNatural() {
    // F# (66) is not in A minor → should snap to F (65, lower neighbor checked first)
    int snapped = ScaleMode.NATURAL_MINOR.snapPitch(9, 66);
    assertEquals("F# should snap to F(65)", 65, snapped);
  }

  // ── Enumeration ───────────────────────────────────────────────────

  @Test
  public void testAllScalesHaveIntervals() {
    for (ScaleMode mode : ScaleMode.values()) {
      assertNotNull(mode.displayName);
      assertFalse(mode.displayName.isEmpty());
      assertNotNull(mode.intervals);
      assertTrue("Scale " + mode + " should have at least 1 interval", mode.intervals.length > 0);
    }
  }

  @Test
  public void testNoteNamesArray() {
    assertEquals(12, ScaleMode.NOTE_NAMES.length);
    assertEquals("C", ScaleMode.NOTE_NAMES[0]);
    assertEquals("B", ScaleMode.NOTE_NAMES[11]);
  }

  @Test
  public void testToStringUsesDisplayName() {
    assertEquals("Major", ScaleMode.MAJOR.toString());
    assertEquals("Chromatic", ScaleMode.CHROMATIC.toString());
    assertEquals("Blues", ScaleMode.BLUES.toString());
  }

  @Test
  public void testScaleCount() {
    assertEquals(17, ScaleMode.values().length);
  }
}
