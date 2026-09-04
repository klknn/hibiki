package hibiki.android.model;

/**
 * Navigation views for the mobile DAW UI.
 */
public enum ViewMode {
    TRACKER("TRACKER"),
    ARRANGER("ARRANGER"),
    INSTRUMENT("INSTRUMENT"),
    MIXER("MIXER"),
    PROJECT("PROJECT");

    private final String label;

    ViewMode(String label) {
        this.label = label;
    }

    public String getLabel() {
        return label;
    }
}
