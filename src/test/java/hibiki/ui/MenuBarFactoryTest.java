package hibiki.ui;

import static org.junit.Assert.*;

import java.awt.event.InputEvent;
import java.awt.event.KeyEvent;
import javax.swing.*;
import org.junit.Assume;
import org.junit.Before;
import org.junit.Test;

/** Tests for MenuBarFactory — verifies menu structure, items, and keyboard accelerators. */
public class MenuBarFactoryTest {

  private JFrame frame;
  private MenuBarFactory.MenuActions stubActions;
  private JMenuBar menuBar;

  @Before
  public void setUp() {
    Assume.assumeFalse(java.awt.GraphicsEnvironment.isHeadless());
    frame = new JFrame();
    stubActions =
        new MenuBarFactory.MenuActions() {
          public void showSaveDialog() {}

          public void showLoadDialog() {}

          public void showSettings() {}

          public void toggleRepl() {}

          public void switchToView(boolean isTimeline) {}

          public void selectTrack(int trackIdx) {}
        };
    menuBar = MenuBarFactory.createMenuBar(frame, stubActions);
  }

  @Test
  public void testMenuBarHasFiveMenus() {
    assertEquals(
        "Should have 5 menus (File, Edit, Transport, View, Help)", 5, menuBar.getMenuCount());
  }

  @Test
  public void testMenuNames() {
    assertEquals("File", menuBar.getMenu(0).getText());
    assertEquals("Edit", menuBar.getMenu(1).getText());
    assertEquals("Transport", menuBar.getMenu(2).getText());
    assertEquals("View", menuBar.getMenu(3).getText());
    assertEquals("Help", menuBar.getMenu(4).getText());
  }

  @Test
  public void testEditMenuHasUndoAndRedo() {
    JMenu editMenu = menuBar.getMenu(1);
    assertNotNull(editMenu);

    JMenuItem undoItem = null, redoItem = null;
    for (int i = 0; i < editMenu.getItemCount(); i++) {
      JMenuItem item = editMenu.getItem(i);
      if (item == null) continue; // separator
      if (item.getText().equals("Undo")) undoItem = item;
      if (item.getText().equals("Redo")) redoItem = item;
    }
    assertNotNull("Edit menu should have Undo item", undoItem);
    assertNotNull("Edit menu should have Redo item", redoItem);
  }

  @Test
  public void testUndoRedoAccelerators() {
    JMenu editMenu = menuBar.getMenu(1);
    JMenuItem undoItem = null, redoItem = null;
    for (int i = 0; i < editMenu.getItemCount(); i++) {
      JMenuItem item = editMenu.getItem(i);
      if (item == null) continue;
      if (item.getText().equals("Undo")) undoItem = item;
      if (item.getText().equals("Redo")) redoItem = item;
    }
    assertNotNull(undoItem);
    assertNotNull(redoItem);

    // Undo should be platform-modifier + Z
    KeyStroke undoAccel = undoItem.getAccelerator();
    assertNotNull("Undo should have an accelerator", undoAccel);
    assertEquals(KeyEvent.VK_Z, undoAccel.getKeyCode());

    // Redo should be platform-modifier + Shift + Z
    KeyStroke redoAccel = redoItem.getAccelerator();
    assertNotNull("Redo should have an accelerator", redoAccel);
    assertEquals(KeyEvent.VK_Z, redoAccel.getKeyCode());
    assertTrue(
        "Redo accelerator should include Shift",
        (redoAccel.getModifiers() & InputEvent.SHIFT_DOWN_MASK) != 0);
  }

  @Test
  public void testFileMenuItems() {
    JMenu fileMenu = menuBar.getMenu(0);
    assertNotNull(fileMenu);

    // Should have at least: New, Open, separator, Save, Save As, separator,
    // Bounce, separator, Settings, separator, Restart Backend, Restart Workers,
    // Restart All, separator, Quit
    int itemCount = 0;
    boolean hasNew = false, hasOpen = false, hasSave = false, hasQuit = false;
    for (int i = 0; i < fileMenu.getItemCount(); i++) {
      JMenuItem item = fileMenu.getItem(i);
      if (item == null) continue; // separator
      itemCount++;
      if (item.getText().equals("New Project")) hasNew = true;
      if (item.getText().startsWith("Open")) hasOpen = true;
      if (item.getText().startsWith("Save") && !item.getText().contains("As")) hasSave = true;
      if (item.getText().equals("Quit")) hasQuit = true;
    }
    assertTrue("File menu should have at least 8 items", itemCount >= 8);
    assertTrue("File menu should have 'New Project'", hasNew);
    assertTrue("File menu should have 'Open'", hasOpen);
    assertTrue("File menu should have 'Save'", hasSave);
    assertTrue("File menu should have 'Quit'", hasQuit);
  }

  @Test
  public void testEditMenuHasSetBpm() {
    JMenu editMenu = menuBar.getMenu(1);
    boolean hasBpm = false;
    for (int i = 0; i < editMenu.getItemCount(); i++) {
      JMenuItem item = editMenu.getItem(i);
      if (item != null && item.getText().contains("BPM")) hasBpm = true;
    }
    assertTrue("Edit menu should have 'Set BPM' item", hasBpm);
  }

  @Test
  public void testViewMenuHasGridModeSubmenu() {
    JMenu viewMenu = menuBar.getMenu(3);
    boolean hasGridMenu = false;
    for (int i = 0; i < viewMenu.getItemCount(); i++) {
      if (viewMenu.getMenuComponent(i) instanceof JMenu) {
        JMenu sub = (JMenu) viewMenu.getMenuComponent(i);
        if (sub.getText().equals("Grid Mode")) {
          hasGridMenu = true;
          // Should have one item per GridMode enum value
          assertEquals(
              "Grid Mode submenu should have all GridMode values",
              GridMode.values().length,
              sub.getItemCount());
        }
      }
    }
    assertTrue("View menu should have 'Grid Mode' submenu", hasGridMenu);
  }

  @Test
  public void testHelpMenuHasAbout() {
    JMenu helpMenu = menuBar.getMenu(4);
    boolean hasAbout = false;
    for (int i = 0; i < helpMenu.getItemCount(); i++) {
      JMenuItem item = helpMenu.getItem(i);
      if (item != null && item.getText().contains("About")) hasAbout = true;
    }
    assertTrue("Help menu should have 'About' item", hasAbout);
  }
}
