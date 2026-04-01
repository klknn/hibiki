package hibiki;

import com.formdev.flatlaf.FlatDarkLaf;
import hibiki.ui.MainView;
import java.awt.*;
import java.net.URL;
import javax.imageio.ImageIO;
import javax.swing.*;

public class GuiMain {
  public static void main(String[] args) {
    // Linux HiDPI scaling - must be set before any AWT/Swing initialization
    String os = System.getProperty("os.name").toLowerCase();
    if (os.contains("linux")) {
      // Enable GTK-based file dialogs which respect system scaling
      System.setProperty("sun.java2d.uiScale.enabled", "true");
      // Try to auto-detect scale from GDK_SCALE env var
      String gdkScale = System.getenv("GDK_SCALE");
      if (gdkScale != null && !gdkScale.isEmpty()) {
        System.setProperty("sun.java2d.uiScale", gdkScale);
      } else {
        // Fallback: query GNOME scaling via gsettings
        String scale = getGnomeScaleFactor();
        if (scale != null && !scale.isEmpty()) {
          System.setProperty("sun.java2d.uiScale", scale);
        }
      }
    }

    // macOS specific settings
    if (os.contains("mac")) {
      System.setProperty("apple.laf.useScreenMenuBar", "true");
      System.setProperty("apple.awt.application.name", "Hibiki");
      System.setProperty("apple.awt.application.appearance", "system");
    }

    // Use SimpleLaf for a modern look
    try {
      UIManager.setLookAndFeel(new FlatDarkLaf());
    } catch (Exception ex) {
      System.err.println("Failed to initialize LaF");
    }

    // Start backend connection
    BackendManager.getInstance().start();

    SwingUtilities.invokeLater(
        () -> {
          JFrame frame = new JFrame("Hibiki DAW");
          frame.setDefaultCloseOperation(JFrame.EXIT_ON_CLOSE);
          frame.setSize(1200, 800);

          // Set app icon
          try {
            URL iconUrl = GuiMain.class.getResource("/hibiki/icon.png");
            if (iconUrl != null) {
              Image image = ImageIO.read(iconUrl);
              frame.setIconImage(image);

              // For macOS Dock icon
              try {
                if (Taskbar.isTaskbarSupported()) {
                  Taskbar taskbar = Taskbar.getTaskbar();
                  if (taskbar.isSupported(Taskbar.Feature.ICON_IMAGE)) {
                    taskbar.setIconImage(image);
                  }
                }
              } catch (Exception te) {
                // Taskbar not supported on all platforms/versions
              }
            }
          } catch (Exception e) {
            System.err.println("Failed to load app icon: " + e.getMessage());
          }

          MainView mainView = new MainView();
          frame.add(mainView);

          frame.setLocationRelativeTo(null);
          frame.setVisible(true);
        });
  }

  /**
   * Query GNOME desktop scaling factor via gsettings. Returns the scale factor as a string (e.g.
   * "2"), or null if not available.
   */
  private static String getGnomeScaleFactor() {
    try {
      ProcessBuilder pb =
          new ProcessBuilder("gsettings", "get", "org.gnome.desktop.interface", "scaling-factor");
      pb.redirectErrorStream(true);
      Process process = pb.start();

      java.io.BufferedReader reader =
          new java.io.BufferedReader(new java.io.InputStreamReader(process.getInputStream()));
      String line = reader.readLine();
      process.waitFor();

      if (line != null && line.startsWith("uint32 ")) {
        // Output format is "uint32 2", extract the number
        String value = line.substring(7).trim();
        int scale = Integer.parseInt(value);
        if (scale > 0) {
          return String.valueOf(scale);
        }
      }
    } catch (Exception e) {
      // gsettings not available or failed - ignore silently
    }
    return null;
  }
}
