package hibiki.android.model;

/**
 * Musical scales for the mobile touch keyboard.
 */
public enum ScaleType {
    CHROMATIC("Chromatic", new int[] {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11}),
    MAJOR("Major", new int[] {0, 2, 4, 5, 7, 9, 11}),
    MINOR("Minor (Natural)", new int[] {0, 2, 3, 5, 7, 8, 10}),
    PENTATONIC_MAJOR("Pentatonic Major", new int[] {0, 2, 4, 7, 9}),
    PENTATONIC_MINOR("Pentatonic Minor", new int[] {0, 3, 5, 7, 10}),
    BLUES("Blues", new int[] {0, 3, 5, 6, 7, 10}),
    DORIAN("Dorian", new int[] {0, 2, 3, 5, 7, 9, 10});

    private final String displayName;
    private final int[] intervals;

    ScaleType(String displayName, int[] intervals) {
        this.displayName = displayName;
        this.intervals = intervals;
    }

    public String getDisplayName() {
        return displayName;
    }

    public int[] getIntervals() {
        return intervals;
    }
}
