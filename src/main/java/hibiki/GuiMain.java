package hibiki;

import com.formdev.flatlaf.FlatDarkLaf;
import javax.swing.*;
import java.awt.*;
import hibiki.ui.MainView;

public class GuiMain {
    public static void main(String[] args) {
        // Use FlatLaf for a modern look
        try {
            UIManager.setLookAndFeel(new FlatDarkLaf());
        } catch (Exception ex) {
            System.err.println("Failed to initialize LaF");
        }

        SwingUtilities.invokeLater(() -> {
            JFrame frame = new JFrame("Hibiki DAW");
            frame.setDefaultCloseOperation(JFrame.EXIT_ON_CLOSE);
            frame.setSize(1200, 800);
            
            MainView mainView = new MainView();
            frame.add(mainView);
            
            frame.setLocationRelativeTo(null);
            frame.setVisible(true);
            
            // Start backend connection
            BackendManager.getInstance().start();
        });
    }
}
