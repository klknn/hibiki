package hibiki.ui;

import java.awt.*;
import java.awt.event.*;
import java.awt.geom.GeneralPath;
import java.util.ArrayList;
import java.util.List;
import javax.swing.*;

/**
 * Visual ADSR envelope editor inspired by Sytrus. Displays a draggable envelope curve with 5
 * control points (origin, attack peak, decay end, sustain hold, release end) and tension handles
 * between each segment.
 *
 * <p>All ADSR values are normalized 0..1. Tension values range from -1 (ease-in) to +1 (ease-out),
 * with 0 being linear.
 */
public class EnvelopeEditorPanel extends JPanel {

  /** Callback interface for envelope parameter changes. */
  public interface Listener {
    void onEnvelopeChanged(float attack, float decay, float sustain, float release);
  }

  // ADSR values, all 0..1 normalized
  private float attack = 0.0f;
  private float decay = 0.2f;
  private float sustain = 0.7f;
  private float release = 0.3f;

  // Per-segment tension: -1..+1, 0=linear
  private final double[] tension = {0, 0, 0, 0};

  // Colors — Sytrus-inspired dark theme with red/orange curve
  private static final Color BG_COLOR = new Color(0x1A1A22);
  private static final Color GRID_COLOR = new Color(0x2A2A35);
  private static final Color GRID_ACCENT = new Color(0x3A3A48);
  private static final Color CURVE_COLOR = new Color(0xE84040);
  private static final Color CURVE_FILL_TOP = new Color(0x60E84040, true);
  private static final Color CURVE_FILL_BOT = new Color(0x00E84040, true);
  private static final Color POINT_FILL = new Color(0xFFFFFF);
  private static final Color POINT_BORDER = new Color(0xE84040);
  private static final Color TENSION_COLOR = new Color(0xCC8888);
  private static final Color MARKER_COLOR = new Color(0x888855);
  private static final Color MARKER_TEXT = new Color(0xAAAA66);
  private static final Color LABEL_COLOR = new Color(0x666670);

  // Interaction state
  private static final int HIT_RADIUS = 8;
  private static final int POINT_RADIUS = 5;
  private static final int TENSION_RADIUS = 4;
  private static final int CURVE_STEPS = 50; // segments per curve section

  private int dragIndex = -1; // 1-4 for control points
  private int dragTensionIndex = -1; // 0-3 for tension handles
  private int hoverIndex = -1;
  private int hoverTensionIndex = -1;

  // Insets for drawing area
  private static final int PAD_LEFT = 20;
  private static final int PAD_RIGHT = 12;
  private static final int PAD_TOP = 10;
  private static final int PAD_BOTTOM = 20;

  private final List<Listener> listeners = new ArrayList<>();

  public EnvelopeEditorPanel() {
    setOpaque(true);
    setBackground(BG_COLOR);
    setCursor(Cursor.getPredefinedCursor(Cursor.CROSSHAIR_CURSOR));

    addMouseListener(
        new MouseAdapter() {
          @Override
          public void mousePressed(MouseEvent e) {
            handleMousePressed(e);
          }

          @Override
          public void mouseReleased(MouseEvent e) {
            dragIndex = -1;
            dragTensionIndex = -1;
          }
        });

    addMouseMotionListener(
        new MouseMotionAdapter() {
          @Override
          public void mouseDragged(MouseEvent e) {
            handleMouseDragged(e);
          }

          @Override
          public void mouseMoved(MouseEvent e) {
            updateHover(e);
          }
        });
  }

  // --- Public API ---

  public void addListener(Listener l) {
    listeners.add(l);
  }

  public void setValues(float a, float d, float s, float r) {
    this.attack = clamp(a);
    this.decay = clamp(d);
    this.sustain = clamp(s);
    this.release = clamp(r);
    repaint();
  }

  public float getAttack() {
    return attack;
  }

  public float getDecay() {
    return decay;
  }

  public float getSustain() {
    return sustain;
  }

  public float getRelease() {
    return release;
  }

  public double getTension(int segment) {
    if (segment < 0 || segment >= 4) return 0;
    return tension[segment];
  }

  public void setTension(int segment, double t) {
    if (segment >= 0 && segment < 4) {
      tension[segment] = Math.max(-1, Math.min(1, t));
      repaint();
    }
  }

  // --- Control point positions ---

  /**
   * Returns the 5 control points in pixel coordinates: [origin, attack, decay, sustain, release].
   */
  private Point[] getControlPoints() {
    int w = getWidth() - PAD_LEFT - PAD_RIGHT;
    int h = getHeight() - PAD_TOP - PAD_BOTTOM;
    if (w <= 0 || h <= 0)
      return new Point[] {new Point(), new Point(), new Point(), new Point(), new Point()};

    // Time allocation: attack gets 25%, decay 25%, sustain-hold 25%, release 25%
    // (each ADSR value scales within its allocation)
    float aFrac = attack * 0.25f;
    float dFrac = aFrac + decay * 0.25f;
    float sFrac = dFrac + 0.25f; // sustain hold always fills its section
    float rFrac = sFrac + release * 0.25f;

    Point[] pts = new Point[5];
    pts[0] = new Point(PAD_LEFT, PAD_TOP + h); // origin (0, 0)
    pts[1] = new Point(PAD_LEFT + (int) (aFrac * w), PAD_TOP); // attack peak (a, 1.0)
    pts[2] =
        new Point(PAD_LEFT + (int) (dFrac * w), PAD_TOP + (int) ((1 - sustain) * h)); // decay end
    pts[3] =
        new Point(PAD_LEFT + (int) (sFrac * w), PAD_TOP + (int) ((1 - sustain) * h)); // sustain end
    pts[4] = new Point(PAD_LEFT + (int) (rFrac * w), PAD_TOP + h); // release end (0)
    return pts;
  }

  /** Returns the tension handle positions (midpoint of each segment). */
  private Point[] getTensionHandles(Point[] pts) {
    Point[] handles = new Point[4];
    for (int i = 0; i < 4; i++) {
      // Place tension handle at the parametric midpoint of the curved segment
      float t = 0.5f;
      float fromY = pts[i].y;
      float toY = pts[i + 1].y;
      float curvedY = applyTension(t, fromY, toY, tension[i]);
      handles[i] = new Point((pts[i].x + pts[i + 1].x) / 2, (int) curvedY);
    }
    return handles;
  }

  // --- Painting ---

  @Override
  protected void paintComponent(Graphics g) {
    super.paintComponent(g);
    Graphics2D g2 = (Graphics2D) g.create();
    g2.setRenderingHint(RenderingHints.KEY_ANTIALIASING, RenderingHints.VALUE_ANTIALIAS_ON);
    g2.setRenderingHint(RenderingHints.KEY_STROKE_CONTROL, RenderingHints.VALUE_STROKE_PURE);

    int w = getWidth();
    int h = getHeight();
    int drawW = w - PAD_LEFT - PAD_RIGHT;
    int drawH = h - PAD_TOP - PAD_BOTTOM;

    // Background
    g2.setColor(BG_COLOR);
    g2.fillRect(0, 0, w, h);

    if (drawW <= 0 || drawH <= 0) {
      g2.dispose();
      return;
    }

    // Grid lines
    drawGrid(g2, drawW, drawH);

    Point[] pts = getControlPoints();
    Point[] tensionPts = getTensionHandles(pts);

    // Section markers (D and S)
    drawSectionMarkers(g2, pts, drawH);

    // Filled area under curve
    drawFilledCurve(g2, pts, drawH);

    // Curve
    drawCurve(g2, pts);

    // Tension handles
    for (int i = 0; i < 4; i++) {
      boolean hover = (i == hoverTensionIndex && dragIndex == -1);
      g2.setColor(hover ? POINT_FILL : TENSION_COLOR);
      g2.setStroke(new BasicStroke(1.5f));
      int r = hover ? TENSION_RADIUS + 1 : TENSION_RADIUS;
      g2.drawOval(tensionPts[i].x - r, tensionPts[i].y - r, r * 2, r * 2);
    }

    // Control points
    for (int i = 0; i < 5; i++) {
      boolean hover = (i == hoverIndex && dragTensionIndex == -1);
      int r = (hover || i == dragIndex) ? POINT_RADIUS + 2 : POINT_RADIUS;
      g2.setColor(POINT_FILL);
      g2.fillOval(pts[i].x - r, pts[i].y - r, r * 2, r * 2);
      g2.setColor(POINT_BORDER);
      g2.setStroke(new BasicStroke(1.5f));
      g2.drawOval(pts[i].x - r, pts[i].y - r, r * 2, r * 2);
    }

    // ADSR labels at bottom
    drawLabels(g2, pts, h);

    g2.dispose();
  }

  private void drawGrid(Graphics2D g2, int drawW, int drawH) {
    g2.setStroke(new BasicStroke(0.5f));
    // Horizontal grid lines (value levels)
    for (int i = 0; i <= 4; i++) {
      int y = PAD_TOP + (int) (drawH * i / 4.0);
      g2.setColor(i == 0 || i == 4 ? GRID_ACCENT : GRID_COLOR);
      g2.drawLine(PAD_LEFT, y, PAD_LEFT + drawW, y);
    }
    // Vertical grid lines (time divisions)
    for (int i = 0; i <= 4; i++) {
      int x = PAD_LEFT + (int) (drawW * i / 4.0);
      g2.setColor(i == 0 ? GRID_ACCENT : GRID_COLOR);
      g2.drawLine(x, PAD_TOP, x, PAD_TOP + drawH);
    }
  }

  private void drawSectionMarkers(Graphics2D g2, Point[] pts, int drawH) {
    g2.setStroke(
        new BasicStroke(
            1.0f, BasicStroke.CAP_BUTT, BasicStroke.JOIN_MITER, 10.0f, new float[] {4, 4}, 0));
    g2.setColor(MARKER_COLOR);

    // D marker at attack peak (transition from attack to decay)
    g2.drawLine(pts[1].x, PAD_TOP, pts[1].x, PAD_TOP + drawH);
    // S marker at sustain end (transition from sustain to release)
    g2.drawLine(pts[3].x, PAD_TOP, pts[3].x, PAD_TOP + drawH);

    g2.setFont(getFont().deriveFont(Font.PLAIN, 9.0f));
    g2.setColor(MARKER_TEXT);
    g2.drawString("D", pts[1].x + 2, PAD_TOP + drawH - 2);
    g2.drawString("S", pts[3].x + 2, PAD_TOP + drawH - 2);
  }

  private void drawCurve(Graphics2D g2, Point[] pts) {
    g2.setColor(CURVE_COLOR);
    g2.setStroke(new BasicStroke(2.0f, BasicStroke.CAP_ROUND, BasicStroke.JOIN_ROUND));

    GeneralPath path = new GeneralPath();
    path.moveTo(pts[0].x, pts[0].y);

    for (int seg = 0; seg < 4; seg++) {
      for (int step = 1; step <= CURVE_STEPS; step++) {
        float t = step / (float) CURVE_STEPS;
        float x = pts[seg].x + t * (pts[seg + 1].x - pts[seg].x);
        float y = applyTension(t, pts[seg].y, pts[seg + 1].y, tension[seg]);
        path.lineTo(x, y);
      }
    }
    g2.draw(path);
  }

  private void drawFilledCurve(Graphics2D g2, Point[] pts, int drawH) {
    GeneralPath fill = new GeneralPath();
    fill.moveTo(pts[0].x, pts[0].y);

    for (int seg = 0; seg < 4; seg++) {
      for (int step = 1; step <= CURVE_STEPS; step++) {
        float t = step / (float) CURVE_STEPS;
        float x = pts[seg].x + t * (pts[seg + 1].x - pts[seg].x);
        float y = applyTension(t, pts[seg].y, pts[seg + 1].y, tension[seg]);
        fill.lineTo(x, y);
      }
    }
    // Close back along the bottom
    fill.lineTo(pts[4].x, PAD_TOP + drawH);
    fill.lineTo(pts[0].x, PAD_TOP + drawH);
    fill.closePath();

    GradientPaint gp =
        new GradientPaint(0, PAD_TOP, CURVE_FILL_TOP, 0, PAD_TOP + drawH, CURVE_FILL_BOT);
    g2.setPaint(gp);
    g2.fill(fill);
  }

  private void drawLabels(Graphics2D g2, Point[] pts, int totalH) {
    g2.setFont(getFont().deriveFont(Font.PLAIN, 9.0f));
    g2.setColor(LABEL_COLOR);

    String[] labels = {"A", "D", "S", "R"};
    for (int i = 0; i < 4; i++) {
      int cx = (pts[i].x + pts[i + 1].x) / 2;
      g2.drawString(labels[i], cx - 3, totalH - 3);
    }
  }

  // --- Tension curve math ---

  /**
   * Apply tension to interpolate from fromY to toY at parameter t (0..1). tension=0 → linear,
   * tension>0 → ease-out (quick start), tension<0 → ease-in (slow start).
   */
  static float applyTension(float t, float fromY, float toY, double tension) {
    double curved;
    if (Math.abs(tension) < 0.001) {
      curved = t;
    } else {
      // Use exponential curve: t^exp where exp = 2^(-tension*2)
      double exp = Math.pow(2.0, -tension * 2.0);
      curved = Math.pow(t, exp);
    }
    return (float) (fromY + curved * (toY - fromY));
  }

  // --- Mouse interaction ---

  private void handleMousePressed(MouseEvent e) {
    Point[] pts = getControlPoints();
    Point[] tensionPts = getTensionHandles(pts);

    // Check control points first (skip P0 which is fixed)
    for (int i = 1; i <= 4; i++) {
      if (dist(e.getPoint(), pts[i]) <= HIT_RADIUS) {
        dragIndex = i;
        return;
      }
    }

    // Check tension handles — right-click resets
    for (int i = 0; i < 4; i++) {
      if (dist(e.getPoint(), tensionPts[i]) <= HIT_RADIUS) {
        if (SwingUtilities.isRightMouseButton(e)) {
          tension[i] = 0;
          repaint();
          return;
        }
        dragTensionIndex = i;
        return;
      }
    }
  }

  private void handleMouseDragged(MouseEvent e) {
    int drawW = getWidth() - PAD_LEFT - PAD_RIGHT;
    int drawH = getHeight() - PAD_TOP - PAD_BOTTOM;
    if (drawW <= 0 || drawH <= 0) return;

    if (dragIndex > 0) {
      float mx = (e.getX() - PAD_LEFT) / (float) drawW; // 0..1 in draw area
      float my = (e.getY() - PAD_TOP) / (float) drawH; // 0..1 in draw area (0=top=1.0)

      mx = Math.max(0, Math.min(1, mx));
      my = Math.max(0, Math.min(1, my));

      switch (dragIndex) {
        case 1: // Attack peak — only horizontal movement
          attack = clamp(mx / 0.25f); // scale back from 25% allocation
          break;
        case 2: // Decay end — horizontal + vertical
          float decayStart = attack * 0.25f;
          float decayX = mx - decayStart;
          decay = clamp(Math.max(0, decayX) / 0.25f);
          sustain = clamp(1.0f - my);
          break;
        case 3: // Sustain end — horizontal only (sustain level locked to P2)
          // Sustain hold section is fixed at 25%, no parameter changes
          break;
        case 4: // Release end — only horizontal
          float releaseStart = attack * 0.25f + decay * 0.25f + 0.25f;
          float relX = mx - releaseStart;
          release = clamp(Math.max(0, relX) / 0.25f);
          break;
      }
      repaint();
      fireEnvelopeChanged();
    } else if (dragTensionIndex >= 0) {
      // Tension: vertical drag changes tension value
      Point[] pts = getControlPoints();
      float midY = (pts[dragTensionIndex].y + pts[dragTensionIndex + 1].y) / 2.0f;
      float delta = (e.getY() - midY) / (float) (getHeight() / 4);
      tension[dragTensionIndex] = Math.max(-1, Math.min(1, -delta));
      repaint();
    }
  }

  private void updateHover(MouseEvent e) {
    Point[] pts = getControlPoints();
    Point[] tensionPts = getTensionHandles(pts);

    int oldHover = hoverIndex;
    int oldTensionHover = hoverTensionIndex;
    hoverIndex = -1;
    hoverTensionIndex = -1;

    for (int i = 1; i <= 4; i++) {
      if (dist(e.getPoint(), pts[i]) <= HIT_RADIUS) {
        hoverIndex = i;
        break;
      }
    }
    if (hoverIndex == -1) {
      for (int i = 0; i < 4; i++) {
        if (dist(e.getPoint(), tensionPts[i]) <= HIT_RADIUS) {
          hoverTensionIndex = i;
          break;
        }
      }
    }

    if (hoverIndex != oldHover || hoverTensionIndex != oldTensionHover) {
      repaint();
    }
  }

  private void fireEnvelopeChanged() {
    for (Listener l : listeners) {
      l.onEnvelopeChanged(attack, decay, sustain, release);
    }
  }

  private static float clamp(float v) {
    return Math.max(0, Math.min(1, v));
  }

  private static double dist(Point a, Point b) {
    double dx = a.x - b.x;
    double dy = a.y - b.y;
    return Math.sqrt(dx * dx + dy * dy);
  }
}
