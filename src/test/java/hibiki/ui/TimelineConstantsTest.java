package hibiki.ui;

import static org.junit.Assert.*;

import java.awt.*;
import org.junit.Test;

/** Safety-net tests for TimelineConstants — verify constants and color helpers. */
public class TimelineConstantsTest {

  @Test
  public void testConstants() {
    assertEquals(15, TimelineConstants.CLIP_HEADER_HEIGHT);
    assertEquals(4, TimelineConstants.AUTOMATION_PAD);
    assertEquals(8, TimelineConstants.RESIZE_EDGE_PX);
    assertEquals(6, TimelineConstants.POINT_HIT_RADIUS);
  }

  @Test
  public void testWithAlpha() {
    Color base = new Color(100, 150, 200);
    Color result = TimelineConstants.withAlpha(base, 128);
    assertEquals(100, result.getRed());
    assertEquals(150, result.getGreen());
    assertEquals(200, result.getBlue());
    assertEquals(128, result.getAlpha());
  }

  @Test
  public void testDarkened() {
    Color base = new Color(90, 150, 210);
    Color result = TimelineConstants.darkened(base, 80);
    assertEquals(30, result.getRed());
    assertEquals(50, result.getGreen());
    assertEquals(70, result.getBlue());
    assertEquals(80, result.getAlpha());
  }
}
