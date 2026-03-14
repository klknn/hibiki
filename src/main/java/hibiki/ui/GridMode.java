package hibiki.ui;

/**
 * Grid subdivision modes shared between PianoRoll and TimelineView.
 * Controls both grid line rendering and note/clip snap intervals.
 */
enum GridMode {
    AUTO("Auto"),        // Adaptive based on zoom level
    SECONDS("Seconds"),  // Absolute time in seconds (TimelineView only)
    BAR("1/1"),          // Whole bar
    HALF("1/2"),         // Half bar
    QUARTER("1/4"),      // Quarter note (beat)
    EIGHTH("1/8"),       // Eighth note
    SIXTEENTH("1/16"),   // Sixteenth note
    THIRTY_SECOND("1/32"),       // Thirty-second note
    TRIPLET_QUARTER("1/3"),      // Triplet quarter
    TRIPLET_EIGHTH("1/6"),       // Triplet eighth
    TRIPLET_16TH("1/12"),        // Triplet sixteenth
    TRIPLET_32ND("1/24");        // Triplet thirty-second

    private final String label;

    GridMode(String label) {
        this.label = label;
    }

    @Override
    public String toString() {
        return label;
    }
}
