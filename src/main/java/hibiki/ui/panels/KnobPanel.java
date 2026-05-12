package hibiki.ui.panels;

import hibiki.ui.Theme;
import hibiki.ui.panels.devices.AbstractDevicePanel;
import java.awt.*;
import java.awt.event.*;
import java.util.ArrayList;
import java.util.List;
import javax.swing.*;
import javax.swing.event.ChangeEvent;
import javax.swing.event.ChangeListener;

/**
 * A reusable arc-knob component with drag interaction, name label on top, formatted value label on
 * bottom, and ChangeListener support. Each panel provides a ValueFormatter for custom display and
 * an accent color for the arc.
 */
public class KnobPanel extends JPanel {
  private double value;
  private final List<ChangeListener> listeners = new ArrayList<>();
  private int dragStartY;
  private final JLabel valLabel;
  private final AbstractDevicePanel.ValueFormatter formatter;
  private final JPanel knobCanvas;

  public KnobPanel(
      String name,
      double initialValue,
      AbstractDevicePanel.ValueFormatter formatter,
      Color accentColor) {
    this.value = initialValue;
    this.formatter = formatter;
    Theme theme = Theme.getInstance();
    setBackground(theme.BG_DARK);
    setLayout(new BorderLayout());

    JLabel nameLabel = new JLabel(name, SwingConstants.CENTER);
    nameLabel.setForeground(theme.TEXT_DIM);
    nameLabel.setFont(theme.FONT_UI.deriveFont(theme.scale(9.0f)));
    add(nameLabel, BorderLayout.NORTH);

    knobCanvas =
        new JPanel() {
          @Override
          protected void paintComponent(Graphics g) {
            super.paintComponent(g);
            AbstractDevicePanel.paintArcKnob(
                (Graphics2D) g.create(), getWidth(), getHeight(), value, accentColor);
          }
        };
    knobCanvas.setOpaque(false);
    knobCanvas.setPreferredSize(new Dimension(theme.scale(32), theme.scale(32)));
    knobCanvas.setCursor(Cursor.getPredefinedCursor(Cursor.N_RESIZE_CURSOR));
    knobCanvas.addMouseListener(
        new MouseAdapter() {
          @Override
          public void mousePressed(MouseEvent e) {
            dragStartY = e.getY();
          }
        });
    knobCanvas.addMouseMotionListener(
        new MouseMotionAdapter() {
          @Override
          public void mouseDragged(MouseEvent e) {
            int dy = dragStartY - e.getY();
            dragStartY = e.getY();
            value = Math.max(0.0, Math.min(1.0, value + dy * 0.005));
            valLabel.setText(formatter.format(value));
            knobCanvas.repaint();
            for (ChangeListener l : listeners) l.stateChanged(new ChangeEvent(KnobPanel.this));
          }
        });
    add(knobCanvas, BorderLayout.CENTER);

    valLabel = new JLabel(formatter.format(initialValue), SwingConstants.CENTER);
    valLabel.setForeground(theme.TEXT_LIGHT);
    valLabel.setFont(theme.FONT_UI.deriveFont(theme.scale(8.0f)));
    add(valLabel, BorderLayout.SOUTH);
  }

  public double getValue() {
    return value;
  }

  public void setValue(double v) {
    this.value = v;
    valLabel.setText(formatter.format(v));
    repaint();
  }

  public void addChangeListener(ChangeListener l) {
    listeners.add(l);
  }

  /** Refresh the value label text (e.g. when formatter output depends on external state). */
  public void refreshLabel() {
    valLabel.setText(formatter.format(value));
    repaint();
  }
}
