package hibiki;

import java.awt.*;
import java.io.File;
import javax.swing.*;
import javax.swing.plaf.basic.BasicLookAndFeel;

public class SimpleLaf extends BasicLookAndFeel {

  public String getDescription() {
    return "This is " + getName();
  }

  public String getID() {
    return "SimpleLaf";
  }

  public String getName() {
    return "Simple Look & Feel";
  }

  public boolean isNativeLookAndFeel() {
    return false;
  }

  public boolean isSupportedLookAndFeel() {
    return true;
  }

  @Override
  public UIDefaults getDefaults() {
    UIDefaults defaults = super.getDefaults();

    // Use system font for better HiDPI compatibility
    Font defaultFont = new Font("SansSerif", Font.PLAIN, 12);

    // File chooser specific defaults to ensure proper rendering
    defaults.put("FileChooser.listFont", defaultFont);
    defaults.put("FileChooser.detailsViewIcon", UIManager.getIcon("FileView.directoryIcon"));
    defaults.put("FileChooser.homeFolderIcon", UIManager.getIcon("FileView.directoryIcon"));
    defaults.put("FileChooser.newFolderIcon", UIManager.getIcon("FileView.directoryIcon"));
    defaults.put("FileChooser.upFolderIcon", UIManager.getIcon("FileView.directoryIcon"));

    // List and Tree fonts (used in file chooser)
    defaults.put("List.font", defaultFont);
    defaults.put("Tree.font", defaultFont);
    defaults.put("Table.font", defaultFont);
    defaults.put("TextField.font", defaultFont);
    defaults.put("ComboBox.font", defaultFont);
    defaults.put("Label.font", defaultFont);
    defaults.put("Button.font", defaultFont);

    // Ensure proper background and foreground for file chooser
    Color bg = new Color(60, 60, 60);
    Color fg = Color.WHITE;
    defaults.put("FileChooser.background", bg);
    defaults.put("FileChooser.foreground", fg);
    defaults.put("List.background", bg);
    defaults.put("List.foreground", fg);
    defaults.put("Table.background", bg);
    defaults.put("Table.foreground", fg);
    defaults.put("TextField.background", bg.brighter());
    defaults.put("TextField.foreground", fg);
    defaults.put("ComboBox.background", bg);
    defaults.put("ComboBox.foreground", fg);
    defaults.put("ScrollPane.background", bg);
    defaults.put("Viewport.background", bg);

    return defaults;
  }

  /**
   * Shows an open file dialog. Uses native FileDialog when SimpleLaf is active for better HiDPI
   * compatibility on Linux.
   *
   * @param parent the parent component
   * @param title the dialog title
   * @param initialDir optional initial directory (can be null)
   * @return the selected file, or null if cancelled
   */
  public static File showOpenDialog(Component parent, String title, String initialDir) {
    if (UIManager.getLookAndFeel() instanceof SimpleLaf) {
      Frame frame = getFrame(parent);
      FileDialog dialog = new FileDialog(frame, title, FileDialog.LOAD);
      if (initialDir != null) dialog.setDirectory(initialDir);
      dialog.setVisible(true);
      String dir = dialog.getDirectory();
      String file = dialog.getFile();
      if (dir != null && file != null) {
        return new File(dir, file);
      }
      return null;
    } else {
      JFileChooser chooser = initialDir != null ? new JFileChooser(initialDir) : new JFileChooser();
      if (chooser.showOpenDialog(parent) == JFileChooser.APPROVE_OPTION) {
        return chooser.getSelectedFile();
      }
      return null;
    }
  }

  /**
   * Shows a save file dialog. Uses native FileDialog when SimpleLaf is active for better HiDPI
   * compatibility on Linux.
   *
   * @param parent the parent component
   * @param title the dialog title
   * @return the selected file, or null if cancelled
   */
  public static File showSaveDialog(Component parent, String title) {
    if (UIManager.getLookAndFeel() instanceof SimpleLaf) {
      Frame frame = getFrame(parent);
      FileDialog dialog = new FileDialog(frame, title, FileDialog.SAVE);
      dialog.setVisible(true);
      String dir = dialog.getDirectory();
      String file = dialog.getFile();
      if (dir != null && file != null) {
        return new File(dir, file);
      }
      return null;
    } else {
      JFileChooser chooser = new JFileChooser();
      if (chooser.showSaveDialog(parent) == JFileChooser.APPROVE_OPTION) {
        return chooser.getSelectedFile();
      }
      return null;
    }
  }

  private static Frame getFrame(Component c) {
    if (c instanceof Frame) return (Frame) c;
    return (Frame) SwingUtilities.getWindowAncestor(c);
  }
}
