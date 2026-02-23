package hibiki.ui;

import javax.swing.*;
import java.awt.*;
import java.awt.event.*;
import hibiki.BackendManager;
import com.google.flatbuffers.FlatBufferBuilder;
import hibiki.ipc.Request;
import hibiki.ipc.Command;
import hibiki.ipc.DeleteClip;

public class WaveformPanel extends JPanel {
    private float[] waveform;
    private int trackIdx = -1;
    private int slotIdx = -1;
    private JButton deleteBtn;

    public WaveformPanel() {
        setLayout(new BorderLayout());
        setBackground(Theme.getInstance().BG_DARKER);
        setPreferredSize(new Dimension(Theme.getInstance().scale(300), Theme.getInstance().scale(150)));
        setBorder(BorderFactory.createLineBorder(Theme.getInstance().BORDER));

        deleteBtn = new JButton("Delete Clip");
        deleteBtn.setFont(Theme.getInstance().FONT_UI);
        deleteBtn.setVisible(false);
        deleteBtn.addActionListener(e -> sendDeleteClip());

        JPanel topPanel = new JPanel(new FlowLayout(FlowLayout.RIGHT));
        topPanel.setOpaque(false);
        topPanel.add(deleteBtn);
        add(topPanel, BorderLayout.NORTH);

        addMouseListener(new MouseAdapter() {
            @Override
            public void mousePressed(MouseEvent e) {
                if (SwingUtilities.isRightMouseButton(e) && trackIdx != -1) {
                    showContextMenu(e.getX(), e.getY());
                }
            }
        });
    }

    public void setWaveform(int trackIdx, int slotIdx, float[] waveform) {
        this.trackIdx = trackIdx;
        this.slotIdx = slotIdx;
        this.waveform = waveform;
        boolean hasContent = waveform != null && waveform.length > 0;
        deleteBtn.setVisible(hasContent);
        setVisible(hasContent);
        revalidate();
        repaint();
    }

    public boolean hasData() {
        return waveform != null && waveform.length > 0;
    }

    private void showContextMenu(int x, int y) {
        JPopupMenu menu = new JPopupMenu();
        JMenuItem deleteItem = new JMenuItem("Delete Clip");
        deleteItem.addActionListener(e -> sendDeleteClip());
        menu.add(deleteItem);
        menu.show(this, x, y);
    }

    private void sendDeleteClip() {
        if (trackIdx == -1 || slotIdx == -1)
            return;

        FlatBufferBuilder builder = new FlatBufferBuilder(128);
        DeleteClip.startDeleteClip(builder);
        DeleteClip.addTrackIndex(builder, trackIdx);
        DeleteClip.addSlotIndex(builder, slotIdx);
        int deleteOff = DeleteClip.endDeleteClip(builder);
        int requestOffset = Request.createRequest(builder, Command.DeleteClip, deleteOff);
        builder.finish(requestOffset);
        BackendManager.getInstance().sendRequest(builder);

        // Clear view
        setWaveform(-1, -1, null);
    }

    @Override
    protected void paintComponent(Graphics g) {
        super.paintComponent(g);
        if (waveform == null || waveform.length == 0) return;

        Graphics2D g2 = (Graphics2D) g;
        g2.setRenderingHint(RenderingHints.KEY_ANTIALIASING, RenderingHints.VALUE_ANTIALIAS_ON);
        g2.setColor(Theme.getInstance().ACCENT_BLUE);

        int w = getWidth();
        int h = getHeight();
        int centerY = h / 2;

        for (int i = 0; i < waveform.length - 1; i++) {
            int x1 = i * w / waveform.length;
            int x2 = (i + 1) * w / waveform.length;
            int y1 = (int) (waveform[i] * (h / 3));
            int y2 = (int) (waveform[i + 1] * (h / 3));

            g2.drawLine(x1, centerY - y1, x2, centerY - y2);
            g2.drawLine(x1, centerY + y1, x2, centerY + y2);
        }
    }
}
