package hibiki.ui;

/**
 * Grid subdivision modes shared between PianoRoll and TimelineView. Controls both grid line
 * rendering and note/clip snap intervals. Also provides shared interval computation and music
 * utility methods.
 */
enum GridMode {
  AUTO("Auto"), // Adaptive based on zoom level
  SECONDS("Seconds"), // Absolute time in seconds (TimelineView only)
  BAR("1/1"), // Whole bar
  HALF("1/2"), // Half bar
  QUARTER("1/4"), // Quarter note (beat)
  EIGHTH("1/8"), // Eighth note
  SIXTEENTH("1/16"), // Sixteenth note
  THIRTY_SECOND("1/32"), // Thirty-second note
  TRIPLET_QUARTER("1/3"), // Triplet quarter
  TRIPLET_EIGHTH("1/6"), // Triplet eighth
  TRIPLET_16TH("1/12"), // Triplet sixteenth
  TRIPLET_32ND("1/24"); // Triplet thirty-second

  private final String label;

  GridMode(String label) {
    this.label = label;
  }

  @Override
  public String toString() {
    return label;
  }

  /**
   * Get the tick interval for this grid mode given a MIDI resolution (ticks per beat). Returns -1
   * for AUTO/SECONDS modes (use autoTickInterval instead).
   */
  int getTickInterval(int resolution) {
    int ticksPerBar = resolution * 4; // 4/4 time
    switch (this) {
      case BAR:
        return ticksPerBar;
      case HALF:
        return ticksPerBar / 2;
      case QUARTER:
        return resolution;
      case EIGHTH:
        return resolution / 2;
      case SIXTEENTH:
        return resolution / 4;
      case THIRTY_SECOND:
        return resolution / 8;
      case TRIPLET_QUARTER:
        return ticksPerBar / 3;
      case TRIPLET_EIGHTH:
        return ticksPerBar / 6;
      case TRIPLET_16TH:
        return ticksPerBar / 12;
      case TRIPLET_32ND:
        return ticksPerBar / 24;
      default:
        return resolution; // fallback to quarter
    }
  }

  /**
   * Get the auto-computed tick interval based on zoom level. Finds the finest grid that maintains
   * minimum pixel spacing.
   */
  static int autoTickInterval(int resolution, float tickWidth, int minPixels) {
    int ticksPerBar = resolution * 4;
    int[] divisions = {
      resolution / 8, // 1/32
      resolution / 4, // 1/16
      resolution / 2, // 1/8
      resolution, // 1/4 (beat)
      ticksPerBar / 2, // 1/2
      ticksPerBar // 1/1 (bar)
    };
    for (int div : divisions) {
      if (div * tickWidth >= minPixels) {
        return div;
      }
    }
    return ticksPerBar;
  }

  /**
   * Get the seconds interval for this grid mode given secondsPerBeat. Returns -1 for AUTO mode (use
   * autoSecondsInterval instead).
   */
  float getSecondsInterval(float secondsPerBeat) {
    float secondsPerBar = secondsPerBeat * 4;
    switch (this) {
      case SECONDS:
        return 1.0f;
      case BAR:
        return secondsPerBar;
      case HALF:
        return secondsPerBar / 2;
      case QUARTER:
        return secondsPerBeat;
      case EIGHTH:
        return secondsPerBeat / 2;
      case SIXTEENTH:
        return secondsPerBeat / 4;
      case THIRTY_SECOND:
        return secondsPerBeat / 8;
      case TRIPLET_QUARTER:
        return secondsPerBar / 3;
      case TRIPLET_EIGHTH:
        return secondsPerBar / 6;
      case TRIPLET_16TH:
        return secondsPerBar / 12;
      case TRIPLET_32ND:
        return secondsPerBar / 24;
      default:
        return secondsPerBeat;
    }
  }

  /**
   * Get the auto-computed seconds interval based on zoom level. Finds the finest grid that
   * maintains minimum pixel spacing.
   */
  static float autoSecondsInterval(float secondsPerBeat, float pixelsPerSecond, int minPixels) {
    float secondsPerBar = secondsPerBeat * 4;
    float[] divisions = {
      secondsPerBeat / 8, // 1/32
      secondsPerBeat / 4, // 1/16
      secondsPerBeat / 2, // 1/8
      secondsPerBeat, // 1/4 (beat)
      secondsPerBar / 2, // 1/2
      secondsPerBar // 1/1 (bar)
    };
    for (float div : divisions) {
      if (div * pixelsPerSecond >= minPixels) {
        return div;
      }
    }
    return secondsPerBar;
  }

  /** Check if a MIDI pitch is a black key on a piano keyboard. */
  static boolean isBlackKey(int pitch) {
    int note = pitch % 12;
    return note == 1 || note == 3 || note == 6 || note == 8 || note == 10;
  }
}
