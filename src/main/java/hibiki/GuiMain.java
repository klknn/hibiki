package hibiki;

import com.formdev.flatlaf.FlatDarkLaf;
import javax.swing.*;
import java.awt.*;
import hibiki.ui.MainView;
import javax.imageio.ImageIO;
import java.net.URL;

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
            UIManager.setLookAndFeel(new SimpleLaf());
        } catch (Exception ex) {
            System.err.println("Failed to initialize LaF");
        }

        // Start backend connection
        BackendManager.getInstance().start();

        SwingUtilities.invokeLater(() -> {
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
}
