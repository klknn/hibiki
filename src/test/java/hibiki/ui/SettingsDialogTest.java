package hibiki.ui;

import static org.junit.Assert.*;

import org.junit.Test;

public class SettingsDialogTest {

  @Test
  public void testConstruction() {
    if (java.awt.GraphicsEnvironment.isHeadless()) return;
    SettingsDialog dialog = new SettingsDialog(null);
    assertNotNull(dialog);
    assertEquals("Settings", dialog.getTitle());
    assertTrue(dialog.isModal());
    dialog.dispose();
  }

  @Test
  public void testDialogSize() {
    if (java.awt.GraphicsEnvironment.isHeadless()) return;
    SettingsDialog dialog = new SettingsDialog(null);
    assertTrue(dialog.getWidth() > 0);
    assertTrue(dialog.getHeight() > 0);
    dialog.dispose();
  }

  @Test
  public void testDialogContentPane() {
    if (java.awt.GraphicsEnvironment.isHeadless()) return;
    SettingsDialog dialog = new SettingsDialog(null);
    assertNotNull(dialog.getContentPane());
    assertTrue(dialog.getContentPane().getComponentCount() > 0);
    dialog.dispose();
  }
}
