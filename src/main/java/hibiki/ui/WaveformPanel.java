package hibiki.ui;

import javax.swing.*;
import java.awt.*;
import java.util.List;

public class WaveformPanel extends JPanel {
    private float[] waveform;

    public WaveformPanel() {
        setBackground(new Color(40, 40, 40));
        setPreferredSize(new Dimension(300, 100));
    }

    public void setWaveform(float[] waveform) {
        this.waveform = waveform;
        repaint();
    }

    @Override
    protected void paintComponent(Graphics g) {
        super.paintComponent(g);
        if (waveform == null || waveform.length == 0) return;

        Graphics2D g2 = (Graphics2D) g;
        g2.setRenderingHint(RenderingHints.KEY_ANTIALIASING, RenderingHints.VALUE_ANTIALIAS_ON);
        g2.setColor(new Color(100, 200, 100));

        int w = getWidth();
        int h = getHeight();
        int centerY = h / 2;

        for (int i = 0; i < waveform.length - 1; i++) {
            int x1 = i * w / waveform.length;
            int x2 = (i + 1) * w / waveform.length;
            int y1 = (int) (waveform[i] * centerY);
            int y2 = (int) (waveform[i + 1] * centerY);

            g2.drawLine(x1, centerY - y1, x2, centerY - y2);
            g2.drawLine(x1, centerY + y1, x2, centerY + y2);
        }
    }
}
