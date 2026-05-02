package hibiki.ui;

import static org.junit.Assert.*;

import org.junit.Test;

/** Safety-net tests for AutomationRenderer — ensures it doesn't crash with valid data. */
public class AutomationRendererTest {

  @Test
  public void testDrawEmptyLane() {
    // AutomationLaneData with no clips — should not crash
    TimelineView.AutomationLaneData lane = new TimelineView.AutomationLaneData();
    lane.laneIndex = 0;
    lane.paramName = "Cutoff";
    // lane.clips is null by default — drawAutomationCurve should handle this

    java.awt.image.BufferedImage img =
        new java.awt.image.BufferedImage(400, 100, java.awt.image.BufferedImage.TYPE_INT_ARGB);
    java.awt.Graphics2D g2 = img.createGraphics();
    AutomationRenderer.drawAutomationCurve(g2, lane, 0, 100, 0, 100.0f, 0.5f);
    g2.dispose();
  }

  @Test
  public void testDrawLaneWithClipsNoPoints() {
    TimelineView.AutomationLaneData lane = new TimelineView.AutomationLaneData();
    lane.laneIndex = 0;
    lane.paramName = "Vol";
    lane.clips = new java.util.ArrayList<>();
    TimelineView.ClipRect cr = new TimelineView.ClipRect();
    cr.startTime = 0;
    cr.duration = 4;
    cr.name = "Auto Clip";
    lane.clips.add(cr);

    java.awt.image.BufferedImage img =
        new java.awt.image.BufferedImage(400, 100, java.awt.image.BufferedImage.TYPE_INT_ARGB);
    java.awt.Graphics2D g2 = img.createGraphics();
    AutomationRenderer.drawAutomationCurve(g2, lane, 0, 100, 0, 100.0f, 0.5f);
    g2.dispose();
  }

  @Test
  public void testDrawLaneWithPoints() {
    TimelineView.AutomationLaneData lane = new TimelineView.AutomationLaneData();
    lane.laneIndex = 0;
    lane.paramName = "Pan";
    lane.clips = new java.util.ArrayList<>();
    TimelineView.ClipRect cr = new TimelineView.ClipRect();
    cr.startTime = 0;
    cr.duration = 8;
    cr.name = "Auto";
    cr.automationPoints = new java.util.ArrayList<>();
    cr.automationPoints.add(new AutomationEditor.AutoPoint(0, 0.0f, 0));
    cr.automationPoints.add(new AutomationEditor.AutoPoint(4, 1.0f, 0));
    cr.automationPoints.add(new AutomationEditor.AutoPoint(8, 0.5f, 0));
    lane.clips.add(cr);

    java.awt.image.BufferedImage img =
        new java.awt.image.BufferedImage(800, 100, java.awt.image.BufferedImage.TYPE_INT_ARGB);
    java.awt.Graphics2D g2 = img.createGraphics();
    AutomationRenderer.drawAutomationCurve(g2, lane, 0, 100, 0, 100.0f, 0.5f);
    g2.dispose();
  }
}
