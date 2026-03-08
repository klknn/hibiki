package hibiki;

import com.formdev.flatlaf.FlatDarkLaf;
import javax.swing.*;
import java.awt.*;
import hibiki.ui.MainView;
import javax.imageio.ImageIO;
import java.net.URL;

public class GuiMain {
    public static void main(String[] args) {
        // macOS specific settings
        if (System.getProperty("os.name").toLowerCase().contains("mac")) {
            System.setProperty("apple.laf.useScreenMenuBar", "true");
            System.setProperty("apple.awt.application.name", "Hibiki");
            System.setProperty("apple.awt.application.appearance", "system");
        }

        // Use FlatLaf for a modern look
        try {
            UIManager.setLookAndFeel(new SimpleLaf()); // FlatDarkLaf());
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
