package hibiki.ui;

import javax.swing.JPanel;
import java.awt.*;

/**
 * Stereo level meter widget for track strips in the SessionView.
 * Displays left and right signal levels as vertical green bars.
 */
class LevelMeter extends JPanel {
    private float levelL = 0;
    private float levelR = 0;

    LevelMeter() {
        setPreferredSize(new Dimension(Theme.getInstance().scale(12), Theme.getInstance().scale(100)));
        setBackground(Color.BLACK);
    }

    void setLevels(float l, float r) {
        this.levelL = l;
        this.levelR = r;
        repaint();
    }

    @Override
    protected void paintComponent(Graphics g) {
        super.paintComponent(g);
        int h = getHeight();
        int w = getWidth();

        // Draw level meter backgrounds
        g.setColor(Theme.getInstance().ACCENT_GREEN.darker().darker());
        g.fillRect(1, 0, w / 2 - 2, h);
        g.fillRect(w / 2 + 1, 0, w / 2 - 2, h);

        // Draw level bars
        g.setColor(Theme.getInstance().ACCENT_GREEN);
        int hL = (int) (levelL * h);
        int hR = (int) (levelR * h);
        g.fillRect(1, h - hL, w / 2 - 2, hL);
        g.fillRect(w / 2 + 1, h - hR, w / 2 - 2, hR);

        // Draw scale lines
        g.setColor(new Color(255, 255, 255, 50));
        for (int i = 1; i < 4; i++) {
            int y = i * h / 4;
            g.drawLine(1, y, w - 1, y);
        }
    }
}
