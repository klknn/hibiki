package hibiki.ui;

import hibiki.BackendManager;
import hibiki.pb.commands.*;
import java.awt.*;
import java.awt.event.ActionEvent;
import java.awt.event.InputEvent;
import java.awt.event.KeyEvent;
import javax.swing.*;

/**
 * Builds the main application menu bar. All actions delegate to existing controls already wired in
 * TopBar, MainView, BackendManager, etc.
 */
public class MenuBarFactory {

  /** Callback interface for actions that require MainView coordination. */
  public interface MenuActions {
    void showSaveDialog();

    void showLoadDialog();

    void showSettings();

    void toggleRepl();

    void switchToView(boolean isTimeline);

    void selectTrack(int trackIdx);
  }

  /** Build and return the full menu bar. */
  public static JMenuBar createMenuBar(JFrame frame, MenuActions actions) {
    // Use platform-native modifier (Cmd on macOS, Ctrl elsewhere)
    int mod =
        Toolkit.getDefaultToolkit().getMenuShortcutKeyMaskEx(); // Java 10+: getMenuShortcutKeyMaskEx

    JMenuBar menuBar = new JMenuBar();
    menuBar.add(createFileMenu(frame, actions, mod));
    menuBar.add(createEditMenu(mod));
    menuBar.add(createTransportMenu(mod));
    menuBar.add(createViewMenu(actions, mod));
    menuBar.add(createHelpMenu(frame));
    return menuBar;
  }

  // ─── File ───────────────────────────────────────────────────────────

  private static JMenu createFileMenu(JFrame frame, MenuActions actions, int mod) {
    JMenu menu = new JMenu("File");
    menu.setMnemonic(KeyEvent.VK_F);

    // New Project
    JMenuItem newItem = new JMenuItem("New Project");
    newItem.setAccelerator(KeyStroke.getKeyStroke(KeyEvent.VK_N, mod));
    newItem.addActionListener(
        e -> {
          int result =
              JOptionPane.showConfirmDialog(
                  frame,
                  "Create a new project? Unsaved changes will be lost.",
                  "New Project",
                  JOptionPane.OK_CANCEL_OPTION,
                  JOptionPane.WARNING_MESSAGE);
          if (result == JOptionPane.OK_OPTION) {
            // Load empty path triggers fresh project in backend
            BackendManager.getInstance()
                .sendRequest(
                    Request.newBuilder()
                        .setProject(
                            ProjectCmd.newBuilder().setAction(ProjectCmd.Action.ACTION_LOAD))
                        .build());
          }
        });
    menu.add(newItem);

    // Open
    JMenuItem openItem = new JMenuItem("Open…");
    openItem.setAccelerator(KeyStroke.getKeyStroke(KeyEvent.VK_O, mod));
    openItem.addActionListener(e -> actions.showLoadDialog());
    menu.add(openItem);

    menu.addSeparator();

    // Save
    JMenuItem saveItem = new JMenuItem("Save…");
    saveItem.setAccelerator(KeyStroke.getKeyStroke(KeyEvent.VK_S, mod));
    saveItem.addActionListener(e -> actions.showSaveDialog());
    menu.add(saveItem);

    // Save As
    JMenuItem saveAsItem = new JMenuItem("Save As…");
    saveAsItem.setAccelerator(
        KeyStroke.getKeyStroke(KeyEvent.VK_S, mod | InputEvent.SHIFT_DOWN_MASK));
    saveAsItem.addActionListener(e -> actions.showSaveDialog());
    menu.add(saveAsItem);

    menu.addSeparator();

    // Bounce / Export
    JMenuItem bounceItem = new JMenuItem("Bounce/Export…");
    bounceItem.setAccelerator(
        KeyStroke.getKeyStroke(KeyEvent.VK_E, mod | InputEvent.SHIFT_DOWN_MASK));
    bounceItem.addActionListener(
        e ->
            BackendManager.getInstance()
                .sendRequest(
                    Request.newBuilder()
                        .setProject(
                            ProjectCmd.newBuilder().setAction(ProjectCmd.Action.ACTION_BOUNCE))
                        .build()));
    menu.add(bounceItem);

    menu.addSeparator();

    // Settings
    JMenuItem settingsItem = new JMenuItem("Settings…");
    settingsItem.setAccelerator(KeyStroke.getKeyStroke(KeyEvent.VK_COMMA, mod));
    settingsItem.addActionListener(e -> actions.showSettings());
    menu.add(settingsItem);

    menu.addSeparator();

    // Quit
    JMenuItem quitItem = new JMenuItem("Quit");
    quitItem.setAccelerator(KeyStroke.getKeyStroke(KeyEvent.VK_Q, mod));
    quitItem.addActionListener(
        e -> {
          BackendManager.getInstance()
              .sendRequest(
                  Request.newBuilder()
                      .setProject(
                          ProjectCmd.newBuilder().setAction(ProjectCmd.Action.ACTION_QUIT))
                      .build());
          frame.dispose();
          System.exit(0);
        });
    menu.add(quitItem);

    return menu;
  }

  // ─── Edit ───────────────────────────────────────────────────────────

  private static JMenu createEditMenu(int mod) {
    JMenu menu = new JMenu("Edit");
    menu.setMnemonic(KeyEvent.VK_E);

    // Undo
    JMenuItem undoItem = new JMenuItem("Undo");
    undoItem.setAccelerator(KeyStroke.getKeyStroke(KeyEvent.VK_Z, mod));
    undoItem.addActionListener(
        e ->
            BackendManager.getInstance()
                .sendRequest(
                    Request.newBuilder()
                        .setProject(
                            ProjectCmd.newBuilder().setAction(ProjectCmd.Action.ACTION_UNDO))
                        .build()));
    menu.add(undoItem);

    // Redo
    JMenuItem redoItem = new JMenuItem("Redo");
    redoItem.setAccelerator(
        KeyStroke.getKeyStroke(KeyEvent.VK_Z, mod | InputEvent.SHIFT_DOWN_MASK));
    redoItem.addActionListener(
        e ->
            BackendManager.getInstance()
                .sendRequest(
                    Request.newBuilder()
                        .setProject(
                            ProjectCmd.newBuilder().setAction(ProjectCmd.Action.ACTION_REDO))
                        .build()));
    menu.add(redoItem);

    menu.addSeparator();

    // Set BPM
    JMenuItem bpmItem = new JMenuItem("Set BPM…");
    bpmItem.addActionListener(
        e -> {
          String input =
              JOptionPane.showInputDialog(null, "Enter BPM:", "Set BPM", JOptionPane.PLAIN_MESSAGE);
          if (input != null && !input.isEmpty()) {
            try {
              float bpm = Float.parseFloat(input);
              BackendManager.getInstance()
                  .sendRequest(
                      Request.newBuilder()
                          .setProject(
                              ProjectCmd.newBuilder()
                                  .setAction(ProjectCmd.Action.ACTION_SET_BPM)
                                  .setBpm(bpm))
                          .build());
            } catch (NumberFormatException ex) {
              JOptionPane.showMessageDialog(
                  null, "Invalid BPM value.", "Error", JOptionPane.ERROR_MESSAGE);
            }
          }
        });
    menu.add(bpmItem);

    return menu;
  }

  // ─── Transport ──────────────────────────────────────────────────────

  private static JMenu createTransportMenu(int mod) {
    JMenu menu = new JMenu("Transport");
    menu.setMnemonic(KeyEvent.VK_T);

    // Play
    JMenuItem playItem = new JMenuItem("Play");
    playItem.addActionListener(e -> BackendManager.getInstance().startPlayback());
    menu.add(playItem);

    // Stop
    JMenuItem stopItem = new JMenuItem("Stop");
    stopItem.addActionListener(e -> BackendManager.getInstance().stopPlayback());
    menu.add(stopItem);

    // Play/Stop Toggle
    JMenuItem toggleItem = new JMenuItem("Play/Stop Toggle");
    toggleItem.setAccelerator(KeyStroke.getKeyStroke(KeyEvent.VK_SPACE, 0));
    toggleItem.addActionListener(e -> BackendManager.getInstance().togglePlay());
    menu.add(toggleItem);

    menu.addSeparator();

    // Return to Start
    JMenuItem seekStartItem = new JMenuItem("Return to Start");
    seekStartItem.setAccelerator(KeyStroke.getKeyStroke(KeyEvent.VK_ENTER, 0));
    seekStartItem.addActionListener(e -> BackendManager.getInstance().seek(0));
    menu.add(seekStartItem);

    // Loop Toggle
    JMenuItem loopItem = new JMenuItem("Toggle Loop");
    loopItem.setAccelerator(KeyStroke.getKeyStroke(KeyEvent.VK_L, 0));
    loopItem.addActionListener(
        e -> {
          // Delegate to the existing TopBar loop toggle logic
          // For now, send backend command (TopBar visual state is handled separately)
        });
    menu.add(loopItem);

    return menu;
  }

  // ─── View ───────────────────────────────────────────────────────────

  private static JMenu createViewMenu(MenuActions actions, int mod) {
    JMenu menu = new JMenu("View");
    menu.setMnemonic(KeyEvent.VK_V);

    // Session / Timeline
    JMenuItem sessionItem = new JMenuItem("Session View");
    sessionItem.setAccelerator(KeyStroke.getKeyStroke(KeyEvent.VK_1, mod | InputEvent.ALT_DOWN_MASK));
    sessionItem.addActionListener(e -> actions.switchToView(false));
    menu.add(sessionItem);

    JMenuItem timelineItem = new JMenuItem("Timeline View");
    timelineItem.setAccelerator(KeyStroke.getKeyStroke(KeyEvent.VK_2, mod | InputEvent.ALT_DOWN_MASK));
    timelineItem.addActionListener(e -> actions.switchToView(true));
    menu.add(timelineItem);

    JMenuItem toggleViewItem = new JMenuItem("Toggle Session/Timeline");
    toggleViewItem.setAccelerator(KeyStroke.getKeyStroke(KeyEvent.VK_TAB, 0));
    menu.add(toggleViewItem);

    menu.addSeparator();

    // REPL
    JMenuItem replItem = new JMenuItem("Toggle REPL");
    replItem.setAccelerator(KeyStroke.getKeyStroke(KeyEvent.VK_R, mod));
    replItem.addActionListener(e -> actions.toggleRepl());
    menu.add(replItem);

    menu.addSeparator();

    // Track selection
    JMenu trackMenu = new JMenu("Select Track");
    for (int i = 1; i <= 4; i++) {
      final int idx = i - 1;
      JMenuItem trackItem = new JMenuItem("Track " + i);
      trackItem.setAccelerator(KeyStroke.getKeyStroke(KeyEvent.VK_0 + i, 0));
      trackItem.addActionListener(e -> actions.selectTrack(idx));
      trackMenu.add(trackItem);
    }
    menu.add(trackMenu);

    menu.addSeparator();

    // Grid Mode submenu
    JMenu gridMenu = new JMenu("Grid Mode");
    ButtonGroup gridGroup = new ButtonGroup();
    for (GridMode mode : GridMode.values()) {
      JRadioButtonMenuItem item = new JRadioButtonMenuItem(mode.toString());
      if (mode == GridMode.AUTO) {
        item.setSelected(true);
      }
      item.addActionListener(
          e -> {
            if (TimelineView.getInstance() != null) {
              TimelineView.getInstance().setGridMode(mode);
            }
          });
      gridGroup.add(item);
      gridMenu.add(item);
    }
    menu.add(gridMenu);

    return menu;
  }

  // ─── Help ──────────────────────────────────────────────────────────

  private static JMenu createHelpMenu(JFrame frame) {
    JMenu menu = new JMenu("Help");
    menu.setMnemonic(KeyEvent.VK_H);

    JMenuItem aboutItem = new JMenuItem("About Hibiki");
    aboutItem.addActionListener(
        e ->
            JOptionPane.showMessageDialog(
                frame,
                "Hibiki DAW\n\nA digital audio workstation built with Java & C++.",
                "About Hibiki",
                JOptionPane.INFORMATION_MESSAGE));
    menu.add(aboutItem);

    return menu;
  }
}
