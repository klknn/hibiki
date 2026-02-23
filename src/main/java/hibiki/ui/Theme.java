package hibiki.ui;

import java.awt.Color;
import java.awt.Font;
import java.util.ArrayList;
import java.util.List;

public class Theme {
  public enum Preset {
    ABLETON_DARK, ABLETON_LIGHT, SOLARIZED_DARK, SOLARIZED_LIGHT, WIN95, WINXP
  }

  private static Theme instance = new Theme(Preset.ABLETON_DARK, 1.0f, 11);

    // Dynamic properties
    public Color BG_DARKER;
    public Color BG_DARK;
    public Color BG_MEDIUM;
    public Color PANEL_BG;
    public Color PANEL_BG_LIGHT;
    public Color BORDER;
    public Color TEXT_NORMAL;
    public Color TEXT_BRIGHT;
    public Color TEXT_LIGHT;
    public Color TEXT_DIM;
    public Color ACCENT_ORANGE;
    public Color ACCENT_BLUE;
    public Color ACCENT_GREEN;
    public Color TRACK_HEADER;
    public Color CLIP_MIDI;
    public Color CLIP_AUDIO;
    public Color CLIP_PLAYING;

    public Font FONT_UI;
    public Font FONT_UI_BOLD;
    public Font FONT_DISPLAY;

    private float scaling = 1.0f;
    private int baseFontSize = 11;
    private Preset currentPreset = Preset.ABLETON_DARK;

    public interface ThemeListener {
      void onThemeChanged();
    }

    private final List<ThemeListener> listeners = new ArrayList<>();

    public static Theme getInstance() {
      return instance;
    }

    private Theme(Preset preset, float scaling, int fontSize) {
      update(preset, scaling, fontSize);
    }

    public void update(Preset preset, float scaling, int fontSize) {
      this.currentPreset = preset;
      this.scaling = scaling;
      this.baseFontSize = fontSize;

      applyPreset(preset);
      applyScaling();

      for (ThemeListener l : listeners) {
        l.onThemeChanged();
      }
    }

    public void addListener(ThemeListener l) {
      listeners.add(l);
    }

    public Preset getCurrentPreset() {
      return currentPreset;
    }

    public float getScaling() {
      return scaling;
    }

    public int getBaseFontSize() {
      return baseFontSize;
    }

    private void applyPreset(Preset preset) {
      switch (preset) {
        case ABLETON_DARK:
          BG_DARKER = new Color(25, 25, 25);
          BG_DARK = new Color(34, 34, 34);
          BG_MEDIUM = new Color(45, 45, 45);
          PANEL_BG = new Color(45, 45, 45);
          PANEL_BG_LIGHT = new Color(55, 55, 55);
          BORDER = new Color(20, 20, 20);
          TEXT_NORMAL = new Color(190, 190, 190);
          TEXT_BRIGHT = new Color(240, 240, 240);
          TEXT_LIGHT = new Color(220, 220, 220);
          TEXT_DIM = new Color(120, 120, 120);
          ACCENT_ORANGE = new Color(255, 153, 0);
          ACCENT_BLUE = new Color(0, 170, 255);
          ACCENT_GREEN = new Color(0, 255, 127);
          TRACK_HEADER = new Color(60, 60, 60);
          CLIP_MIDI = new Color(255, 120, 120);
          CLIP_AUDIO = new Color(120, 200, 255);
          CLIP_PLAYING = new Color(120, 255, 120);
          break;
        case ABLETON_LIGHT:
          BG_DARKER = new Color(220, 220, 220);
          BG_DARK = new Color(200, 200, 200);
          BG_MEDIUM = new Color(180, 180, 180);
          PANEL_BG = new Color(210, 210, 210);
          PANEL_BG_LIGHT = new Color(230, 230, 230);
          BORDER = new Color(160, 160, 160);
          TEXT_NORMAL = new Color(40, 40, 40);
          TEXT_BRIGHT = new Color(0, 0, 0);
          TEXT_LIGHT = new Color(60, 60, 60);
          TEXT_DIM = new Color(100, 100, 100);
          ACCENT_ORANGE = new Color(255, 153, 0);
          ACCENT_BLUE = new Color(0, 120, 255);
          ACCENT_GREEN = new Color(0, 200, 100);
          TRACK_HEADER = new Color(170, 170, 170);
          CLIP_MIDI = new Color(255, 120, 120);
          CLIP_AUDIO = new Color(120, 200, 255);
          CLIP_PLAYING = new Color(120, 255, 120);
          break;
        case SOLARIZED_DARK:
          BG_DARKER = new Color(0, 25, 30);
          BG_DARK = new Color(7, 54, 66);
          BG_MEDIUM = new Color(88, 110, 117);
          PANEL_BG = new Color(7, 54, 66);
          PANEL_BG_LIGHT = new Color(63, 85, 93);
          BORDER = new Color(0, 43, 54);
          TEXT_NORMAL = new Color(147, 161, 161);
          TEXT_BRIGHT = new Color(253, 246, 227);
          TEXT_LIGHT = new Color(131, 148, 150);
          TEXT_DIM = new Color(88, 110, 117);
          ACCENT_ORANGE = new Color(203, 75, 22);
          ACCENT_BLUE = new Color(38, 139, 210);
          ACCENT_GREEN = new Color(133, 153, 0);
          TRACK_HEADER = new Color(0, 43, 54);
          CLIP_MIDI = new Color(211, 54, 130);
          CLIP_AUDIO = new Color(38, 139, 210);
          CLIP_PLAYING = new Color(133, 153, 0);
          break;
        case SOLARIZED_LIGHT:
          BG_DARKER = new Color(240, 235, 215);
          BG_DARK = new Color(253, 246, 227);
          BG_MEDIUM = new Color(147, 161, 161);
          PANEL_BG = new Color(253, 246, 227);
          PANEL_BG_LIGHT = new Color(238, 232, 213);
          BORDER = new Color(211, 201, 171);
          TEXT_NORMAL = new Color(101, 123, 131);
          TEXT_BRIGHT = new Color(7, 54, 66);
          TEXT_LIGHT = new Color(88, 110, 117);
          TEXT_DIM = new Color(147, 161, 161);
          ACCENT_ORANGE = new Color(203, 75, 22);
          ACCENT_BLUE = new Color(38, 139, 210);
          ACCENT_GREEN = new Color(133, 153, 0);
          TRACK_HEADER = new Color(238, 232, 213);
          CLIP_MIDI = new Color(211, 54, 130);
          CLIP_AUDIO = new Color(38, 139, 210);
          CLIP_PLAYING = new Color(133, 153, 0);
          break;
        case WIN95:
          BG_DARKER = new Color(128, 128, 128);
          BG_DARK = new Color(192, 192, 192);
          BG_MEDIUM = new Color(223, 223, 223);
          PANEL_BG = new Color(192, 192, 192);
          PANEL_BG_LIGHT = new Color(255, 255, 255);
          BORDER = new Color(0, 0, 0);
          TEXT_NORMAL = new Color(0, 0, 0);
          TEXT_BRIGHT = new Color(0, 0, 0);
          TEXT_LIGHT = new Color(64, 64, 64);
          TEXT_DIM = new Color(128, 128, 128);
          ACCENT_ORANGE = new Color(0, 0, 128); // Title bar blue
          ACCENT_BLUE = new Color(0, 0, 255);
          ACCENT_GREEN = new Color(0, 128, 0);
          TRACK_HEADER = new Color(128, 128, 128);
          CLIP_MIDI = new Color(255, 0, 255);
          CLIP_AUDIO = new Color(0, 255, 255);
          CLIP_PLAYING = new Color(0, 255, 0);
          break;
        case WINXP:
          BG_DARKER = new Color(212, 208, 200);
          BG_DARK = new Color(236, 233, 216);
          BG_MEDIUM = new Color(255, 255, 255);
          PANEL_BG = new Color(236, 233, 216);
          PANEL_BG_LIGHT = new Color(255, 255, 255);
          BORDER = new Color(172, 168, 153);
          TEXT_NORMAL = new Color(0, 0, 0);
          TEXT_BRIGHT = new Color(0, 0, 0);
          TEXT_LIGHT = new Color(80, 80, 80);
          TEXT_DIM = new Color(150, 150, 150);
          ACCENT_ORANGE = new Color(0, 85, 225); // XP Blue
          ACCENT_BLUE = new Color(49, 106, 197);
          ACCENT_GREEN = new Color(58, 110, 165);
          TRACK_HEADER = new Color(192, 192, 192);
          CLIP_MIDI = new Color(255, 100, 100);
          CLIP_AUDIO = new Color(100, 100, 255);
          CLIP_PLAYING = new Color(100, 255, 100);
          break;
      }
    }

    private void applyScaling() {
      int scaledSize = (int) (baseFontSize * scaling);
      FONT_UI = new Font("SansSerif", Font.PLAIN, scaledSize);
      FONT_UI_BOLD = new Font("SansSerif", Font.BOLD, scaledSize);
      FONT_DISPLAY = new Font("Monospaced", Font.BOLD, (int) (14 * scaling));
    }

    // Helper for scaling dimensions
    public int scale(int val) {
      return (int) (val * scaling);
    }

    public float scale(float val) {
      return val * scaling;
    }
}
