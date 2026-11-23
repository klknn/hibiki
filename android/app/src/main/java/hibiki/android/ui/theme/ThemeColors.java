package hibiki.android.ui.theme;

/**
 * Common color palette constants for Hibiki mobile DAW.
 */
public final class ThemeColors {
    private ThemeColors() {}

    public static final int BG_OLED_BLACK = 0xFF090A0E;
    public static final int BG_PANEL_DARK = 0xFF14161F;
    public static final int BG_PANEL_CARD = 0xFF1E212E;
    public static final int BG_CELL_ACTIVE = 0xFF2B2F42;
    public static final int BG_CELL_STEP_EVEN = 0xFF12141C;
    public static final int BG_CELL_STEP_ODD = 0xFF181B26;

    public static final int TEXT_PRIMARY = 0xFFF0F3F8;
    public static final int TEXT_SECONDARY = 0xFF8C95A8;
    public static final int TEXT_MUTED = 0xFF555D70;

    public static final int ACCENT_CYAN = 0xFF00E5FF;
    public static final int ACCENT_LIME = 0xFF00E676;
    public static final int ACCENT_AMBER = 0xFFFFD600;
    public static final int ACCENT_PINK = 0xFFFF4081;
    public static final int ACCENT_PURPLE = 0xFFD500F9;
    public static final int ACCENT_ORANGE = 0xFFFF6D00;
    public static final int ACCENT_RED = 0xFFFF1744;

    public static final int[] CHANNEL_COLORS = new int[] {
        0xFF00E5FF, // Cyan
        0xFFFF4081, // Pink / Magenta
        0xFF00E676, // Lime
        0xFFFFD600, // Amber
        0xFFD500F9, // Purple
        0xFFFF6D00  // Orange
    };
}
