package hibiki.ui;

/**
 * Musical scale definitions for piano roll visualization and pitch snapping.
 *
 * <p>Each scale is defined by its set of semitone intervals relative to the root note. Used for:
 *
 * <ul>
 *   <li>Highlighting in-scale rows on the piano roll grid
 *   <li>Snapping note pitches to scale degrees during creation/editing
 * </ul>
 */
enum ScaleMode {
  CHROMATIC("Chromatic", new int[] {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11}),
  MAJOR("Major", new int[] {0, 2, 4, 5, 7, 9, 11}),
  NATURAL_MINOR("Natural Minor", new int[] {0, 2, 3, 5, 7, 8, 10}),
  HARMONIC_MINOR("Harmonic Minor", new int[] {0, 2, 3, 5, 7, 8, 11}),
  MELODIC_MINOR("Melodic Minor", new int[] {0, 2, 3, 5, 7, 9, 11}),
  DORIAN("Dorian", new int[] {0, 2, 3, 5, 7, 9, 10}),
  MIXOLYDIAN("Mixolydian", new int[] {0, 2, 4, 5, 7, 9, 10}),
  PHRYGIAN("Phrygian", new int[] {0, 1, 3, 5, 7, 8, 10}),
  LYDIAN("Lydian", new int[] {0, 2, 4, 6, 7, 9, 11}),
  LOCRIAN("Locrian", new int[] {0, 1, 3, 5, 6, 8, 10}),
  BLUES("Blues", new int[] {0, 3, 5, 6, 7, 10}),
  PENTATONIC_MAJOR("Pentatonic Major", new int[] {0, 2, 4, 7, 9}),
  PENTATONIC_MINOR("Pentatonic Minor", new int[] {0, 3, 5, 7, 10}),
  WHOLE_TONE("Whole Tone", new int[] {0, 2, 4, 6, 8, 10}),
  DIMINISHED("Diminished", new int[] {0, 2, 3, 5, 6, 8, 9, 11}),
  BEBOP_DOMINANT("Bebop Dominant", new int[] {0, 2, 4, 5, 7, 9, 10, 11}),
  BEBOP_MAJOR("Bebop Major", new int[] {0, 2, 4, 5, 7, 8, 9, 11});

  static final String[] NOTE_NAMES = {
    "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
  };

  final String displayName;
  final int[] intervals;

  ScaleMode(String displayName, int[] intervals) {
    this.displayName = displayName;
    this.intervals = intervals;
  }

  /** Check whether the given MIDI pitch belongs to this scale with the given root note. */
  boolean containsPitch(int rootNote, int pitch) {
    int degree = ((pitch - rootNote) % 12 + 12) % 12;
    for (int interval : intervals) {
      if (interval == degree) return true;
    }
    return false;
  }

  /** Snap pitch to the nearest scale degree. */
  int snapPitch(int rootNote, int pitch) {
    if (this == CHROMATIC) return pitch;
    if (containsPitch(rootNote, pitch)) return pitch;
    // Search outward from the pitch for the nearest in-scale note
    for (int offset = 1; offset <= 6; offset++) {
      if (pitch - offset >= 0 && containsPitch(rootNote, pitch - offset)) return pitch - offset;
      if (pitch + offset < 128 && containsPitch(rootNote, pitch + offset)) return pitch + offset;
    }
    return pitch; // fallback
  }

  @Override
  public String toString() {
    return displayName;
  }
}
