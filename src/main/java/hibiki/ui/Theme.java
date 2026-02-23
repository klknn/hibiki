package hibiki.ui;

import java.awt.Color;
import java.awt.Font;

public class Theme {
  // Ableton Live 11/12 inspired palette
  public static final Color BG_DARKER = new Color(25, 25, 25);
  public static final Color BG_DARK = new Color(34, 34, 34); // Main background
  public static final Color BG_MEDIUM = new Color(45, 45, 45);
  public static final Color PANEL_BG = new Color(45, 45, 45); // Secondary panel backgrounds
  public static final Color PANEL_BG_LIGHT = new Color(55, 55, 55);
  public static final Color BORDER = new Color(20, 20, 20); // Dark borders

  public static final Color TEXT_NORMAL = new Color(190, 190, 190);
  public static final Color TEXT_BRIGHT = new Color(240, 240, 240);
  public static final Color TEXT_LIGHT = new Color(220, 220, 220);
  public static final Color TEXT_DIM = new Color(120, 120, 120);

  public static final Color ACCENT_ORANGE = new Color(255, 153, 0); // Play button / Focus
  public static final Color ACCENT_BLUE = new Color(0, 170, 255); // Selection
  public static final Color ACCENT_GREEN = new Color(0, 255, 127); // Audio / MIDI items

  public static final Color TRACK_HEADER = new Color(60, 60, 60);
  public static final Color CLIP_MIDI = new Color(255, 120, 120);
  public static final Color CLIP_AUDIO = new Color(120, 200, 255);
  public static final Color CLIP_PLAYING = new Color(120, 255, 120);

  public static final Font FONT_UI = new Font("SansSerif", Font.PLAIN, 11);
  public static final Font FONT_UI_BOLD = new Font("SansSerif", Font.BOLD, 11);
  public static final Font FONT_DISPLAY = new Font("Monospaced", Font.BOLD, 14);

  private Theme() {
  } // Prevent instantiation
}
