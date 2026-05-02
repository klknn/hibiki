package hibiki.ui;

import static org.junit.Assert.*;

import javax.swing.*;
import org.junit.Assume;
import org.junit.Before;
import org.junit.Test;

/** Safety-net tests for MenuBarFactory — verifies menu structure is created without crashing. */
public class MenuBarFactoryTest {

  @Before
  public void skipIfHeadless() {
    Assume.assumeFalse(java.awt.GraphicsEnvironment.isHeadless());
  }

  @Test
  public void testCreateMenuBar() {
    JFrame frame = new JFrame();
    MenuBarFactory.MenuActions actions =
        new MenuBarFactory.MenuActions() {
          public void showSaveDialog() {}

          public void showLoadDialog() {}

          public void showSettings() {}

          public void toggleRepl() {}

          public void switchToView(boolean isTimeline) {}

          public void selectTrack(int trackIdx) {}
        };
    JMenuBar bar = MenuBarFactory.createMenuBar(frame, actions);
    assertNotNull(bar);
    assertTrue("Should have multiple menus", bar.getMenuCount() >= 4);
    frame.dispose();
  }

  @Test
  public void testMenuNames() {
    JFrame frame = new JFrame();
    MenuBarFactory.MenuActions actions =
        new MenuBarFactory.MenuActions() {
          public void showSaveDialog() {}

          public void showLoadDialog() {}

          public void showSettings() {}

          public void toggleRepl() {}

          public void switchToView(boolean isTimeline) {}

          public void selectTrack(int trackIdx) {}
        };
    JMenuBar bar = MenuBarFactory.createMenuBar(frame, actions);
    // Verify standard menu structure exists
    boolean hasFile = false, hasEdit = false, hasTransport = false;
    for (int i = 0; i < bar.getMenuCount(); i++) {
      String text = bar.getMenu(i).getText();
      if (text.equals("File")) hasFile = true;
      if (text.equals("Edit")) hasEdit = true;
      if (text.equals("Transport")) hasTransport = true;
    }
    assertTrue("Should have File menu", hasFile);
    assertTrue("Should have Edit menu", hasEdit);
    assertTrue("Should have Transport menu", hasTransport);
    frame.dispose();
  }
}
