package hibiki.ui;

import static org.junit.Assert.*;

import org.junit.Assume;
import org.junit.Before;
import org.junit.Test;

/** Safety-net tests for AutomationEditorDialog — construction and title verification. */
public class AutomationEditorDialogTest {

  @Before
  public void skipIfHeadless() {
    Assume.assumeFalse(java.awt.GraphicsEnvironment.isHeadless());
  }

  @Test
  public void testConstruction() {
    java.util.List<AutomationEditor.AutoPoint> points = new java.util.ArrayList<>();
    points.add(new AutomationEditor.AutoPoint(0, 0.0f, 0));
    points.add(new AutomationEditor.AutoPoint(4, 1.0f, 0));
    AutomationEditorDialog dialog =
        new AutomationEditorDialog(null, 0, 0, "Test Param", points, 120.0f);
    assertNotNull(dialog);
    assertEquals("Automation: Test Param", dialog.getTitle());
    dialog.dispose();
  }

  @Test
  public void testConstruction_emptyPoints() {
    java.util.List<AutomationEditor.AutoPoint> points = new java.util.ArrayList<>();
    AutomationEditorDialog dialog = new AutomationEditorDialog(null, 0, 0, "Empty", points, 140.0f);
    assertNotNull(dialog);
    dialog.dispose();
  }

  @Test
  public void testConstruction_manyPoints() {
    java.util.List<AutomationEditor.AutoPoint> points = new java.util.ArrayList<>();
    for (int i = 0; i < 20; i++) {
      points.add(new AutomationEditor.AutoPoint(i, i / 20.0f, 0));
    }
    AutomationEditorDialog dialog = new AutomationEditorDialog(null, 1, 2, "Many", points, 90.0f);
    assertNotNull(dialog);
    dialog.dispose();
  }
}
