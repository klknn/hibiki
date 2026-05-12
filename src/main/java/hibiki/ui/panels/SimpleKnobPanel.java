package hibiki.ui.panels;

import hibiki.ui.panels.devices.AbstractDevicePanel;
import java.awt.*;
import java.awt.event.*;
import javax.swing.*;

/**
 * A compact arc-knob without name/value labels, suitable for dense instrument UIs (3xOsc, Film,
 * Sampler). The knob directly updates the owning panel's params[] and sends to backend on drag.
 */
public class SimpleKnobPanel extends JPanel {
  protected double value;
  protected final int paramId;
  private int dragStartY;
  private final AbstractDevicePanel owner;

  public SimpleKnobPanel(
      AbstractDevicePanel owner, int paramId, double defaultVal, Color accentColor) {
    this.owner = owner;
    this.paramId = paramId;
    this.value = defaultVal;
    setOpaque(false);
    setPreferredSize(new Dimension(30, 30));
    setCursor(Cursor.getPredefinedCursor(Cursor.N_RESIZE_CURSOR));

    addMouseListener(
        new MouseAdapter() {
          @Override
          public void mousePressed(MouseEvent e) {
            dragStartY = e.getY();
          }
        });
    addMouseMotionListener(
        new MouseMotionAdapter() {
          @Override
          public void mouseDragged(MouseEvent e) {
            int dy = dragStartY - e.getY();
            dragStartY = e.getY();
            value = Math.max(0.0, Math.min(1.0, value + dy * 0.005));
            owner.params[paramId] = value;
            owner.sendParam(paramId, value);
            onValueChanged();
            repaint();
          }
        });
  }

  /** Override to react to value changes (e.g. update a linked label). */
  protected void onValueChanged() {}

  @Override
  protected void paintComponent(Graphics g) {
    super.paintComponent(g);
    AbstractDevicePanel.paintArcKnob(
        (Graphics2D) g.create(), getWidth(), getHeight(), value, getAccentColor());
  }

  /** Override to customize the accent color per instance. */
  protected Color getAccentColor() {
    return new Color(0x4CAF50);
  }

  public double getValue() {
    return value;
  }

  public void setValue(double v) {
    this.value = v;
    repaint();
  }
}
