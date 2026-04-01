package hibiki.ui;

import java.awt.Color;

/**
 * Shared constants for the Timeline view components. Eliminates magic numbers duplicated across
 * renderers and mouse handlers.
 */
final class TimelineConstants {
  private TimelineConstants() {} // Non-instantiable

  /** Height of the clip header bar (px). */
  static final int CLIP_HEADER_HEIGHT = 15;

  /** Padding inside automation lanes (px). */
  static final int AUTOMATION_PAD = 4;

  /** Distance from clip right edge to trigger resize cursor (px). */
  static final int RESIZE_EDGE_PX = 8;

  /** Hit-test radius for automation point selection (px). */
  static final int POINT_HIT_RADIUS = 6;

  /** Create an accent color at a specific alpha level. */
  static Color withAlpha(Color base, int alpha) {
    return new Color(base.getRed(), base.getGreen(), base.getBlue(), alpha);
  }

  /** Create a darkened accent color (1/3 brightness) at a specific alpha. */
  static Color darkened(Color base, int alpha) {
    return new Color(base.getRed() / 3, base.getGreen() / 3, base.getBlue() / 3, alpha);
  }
}
