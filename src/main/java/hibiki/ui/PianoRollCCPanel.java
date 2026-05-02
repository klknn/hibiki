package hibiki.ui;

import java.awt.*;
import java.awt.event.*;
import java.util.List;
import javax.swing.*;

/**
 * Editable CC/Pitch Bend lane panel for the Piano Roll.
 *
 * <p>Renders CC events as a breakpoint line graph. Click to add/move points, right-click to delete.
 * Used for both pitch bend (ccNumber=128, range [-8192, +8191]) and modulation (ccNumber=1, range
 * [0, 127]).
 */
class PianoRollCCPanel extends JPanel {
  private final PianoRoll pianoRoll;
  private final int ccNumber;
  private final int minValue;
  private final int maxValue;
  private final int centerValue; // For pitch bend: 0. For CC: -1 (no center line)
  private final String label;

  private MidiDataModel.CCEvent draggingPoint = null;

  PianoRollCCPanel(PianoRoll pianoRoll, int ccNumber) {
    this.pianoRoll = pianoRoll;
    this.ccNumber = ccNumber;
    if (ccNumber == 128) {
      // Pitch bend
      this.minValue = -8192;
      this.maxValue = 8191;
      this.centerValue = 0;
      this.label = "Pitch Bend";
    } else {
      // CC (modulation, etc.)
      this.minValue = 0;
      this.maxValue = 127;
      this.centerValue = -1; // no center line
      this.label = "CC " + ccNumber;
    }
    setBackground(Theme.getInstance().BG_DARKER);
    setBorder(BorderFactory.createMatteBorder(1, 0, 0, 0, Theme.getInstance().BORDER));

    MouseAdapter mouse =
        new MouseAdapter() {
          @Override
          public void mousePressed(MouseEvent e) {
            handleMousePressed(e);
          }

          @Override
          public void mouseReleased(MouseEvent e) {
            if (draggingPoint != null) {
              draggingPoint = null;
              pianoRoll.syncToBackend();
            }
          }

          @Override
          public void mouseDragged(MouseEvent e) {
            handleMouseDragged(e);
          }
        };
    addMouseListener(mouse);
    addMouseMotionListener(mouse);
  }

  private List<MidiDataModel.CCEvent> getEvents() {
    return pianoRoll.midiModel.ccEvents;
  }

  private void handleMousePressed(MouseEvent e) {
    float tw = pianoRoll.getTickWidth();
    long tick = (long) (e.getX() / tw);
    int value = yToValue(e.getY());

    if (SwingUtilities.isRightMouseButton(e)) {
      // Delete nearest point
      MidiDataModel.CCEvent nearest = findNearest(e.getX(), e.getY());
      if (nearest != null) {
        getEvents().remove(nearest);
        repaint();
        pianoRoll.syncToBackend();
      }
    } else if (SwingUtilities.isLeftMouseButton(e)) {
      // Try to grab existing point
      MidiDataModel.CCEvent nearest = findNearest(e.getX(), e.getY());
      if (nearest != null && Math.abs(nearest.tick * tw - e.getX()) < 8) {
        draggingPoint = nearest;
        draggingPoint.value = value;
        repaint();
      } else {
        // Create new point
        MidiDataModel.CCEvent ev = new MidiDataModel.CCEvent(ccNumber, tick, value);
        getEvents().add(ev);
        draggingPoint = ev;
        sortEvents();
        repaint();
      }
    }
  }

  private void handleMouseDragged(MouseEvent e) {
    if (draggingPoint != null) {
      float tw = pianoRoll.getTickWidth();
      draggingPoint.tick = Math.max(0, (long) (e.getX() / tw));
      draggingPoint.value = yToValue(e.getY());
      sortEvents();
      repaint();
    }
  }

  private int yToValue(int y) {
    int h = getHeight() - 4;
    double normalized = 1.0 - (double) (y - 2) / h;
    normalized = Math.max(0.0, Math.min(1.0, normalized));
    return (int) (minValue + normalized * (maxValue - minValue));
  }

  private int valueToY(int value) {
    int h = getHeight() - 4;
    double normalized = (double) (value - minValue) / (maxValue - minValue);
    return 2 + (int) ((1.0 - normalized) * h);
  }

  private MidiDataModel.CCEvent findNearest(int x, int y) {
    float tw = pianoRoll.getTickWidth();
    MidiDataModel.CCEvent best = null;
    double bestDist = 12.0; // max grab distance in pixels
    for (MidiDataModel.CCEvent ev : getEvents()) {
      if (ev.ccNumber != ccNumber) continue;
      double dx = ev.tick * tw - x;
      double dy = valueToY(ev.value) - y;
      double dist = Math.sqrt(dx * dx + dy * dy);
      if (dist < bestDist) {
        bestDist = dist;
        best = ev;
      }
    }
    return best;
  }

  private void sortEvents() {
    getEvents().sort((a, b) -> Long.compare(a.tick, b.tick));
  }

  @Override
  protected void paintComponent(Graphics g) {
    super.paintComponent(g);
    Graphics2D g2 = (Graphics2D) g;
    g2.setRenderingHint(RenderingHints.KEY_ANTIALIASING, RenderingHints.VALUE_ANTIALIAS_ON);
    float tw = pianoRoll.getTickWidth();

    // Center line for pitch bend
    if (centerValue >= minValue && centerValue <= maxValue) {
      int cy = valueToY(centerValue);
      g2.setColor(new Color(80, 80, 80));
      g2.setStroke(
          new BasicStroke(
              1.0f,
              BasicStroke.CAP_BUTT,
              BasicStroke.JOIN_MITER,
              10.0f,
              new float[] {4.0f, 4.0f},
              0.0f));
      g2.drawLine(0, cy, getWidth(), cy);
      g2.setStroke(new BasicStroke(1.0f));
    }

    // Label
    g2.setColor(Theme.getInstance().TEXT_DIM);
    g2.setFont(new Font("SansSerif", Font.PLAIN, 9));
    g2.drawString(label, 4, 12);

    // Filter events for this CC number
    java.util.List<MidiDataModel.CCEvent> events = new java.util.ArrayList<>();
    for (MidiDataModel.CCEvent ev : getEvents()) {
      if (ev.ccNumber == ccNumber) events.add(ev);
    }

    if (events.isEmpty()) return;

    // Draw line segments
    g2.setColor(new Color(100, 180, 255, 180));
    g2.setStroke(new BasicStroke(1.5f));
    for (int i = 0; i < events.size() - 1; i++) {
      MidiDataModel.CCEvent a = events.get(i);
      MidiDataModel.CCEvent b = events.get(i + 1);
      int x1 = (int) (a.tick * tw), y1 = valueToY(a.value);
      int x2 = (int) (b.tick * tw), y2 = valueToY(b.value);
      // Step: horizontal then vertical
      g2.drawLine(x1, y1, x2, y1);
      g2.drawLine(x2, y1, x2, y2);
    }
    // Extend last point to the right
    if (!events.isEmpty()) {
      MidiDataModel.CCEvent last = events.get(events.size() - 1);
      int lx = (int) (last.tick * tw), ly = valueToY(last.value);
      g2.drawLine(lx, ly, getWidth(), ly);
    }
    g2.setStroke(new BasicStroke(1.0f));

    // Draw points
    for (MidiDataModel.CCEvent ev : events) {
      int px = (int) (ev.tick * tw);
      int py = valueToY(ev.value);
      g2.setColor(
          ev == draggingPoint
              ? Theme.getInstance().ACCENT_ORANGE
              : Theme.getInstance().ACCENT_BLUE);
      g2.fillOval(px - 4, py - 4, 8, 8);
      g2.setColor(Color.WHITE);
      g2.drawOval(px - 4, py - 4, 8, 8);
    }
  }
}
