package hibiki.ui;

import javax.swing.*;
import java.awt.*;
import java.util.function.Consumer;

/**
 * Reusable control panel with grid mode selector, H/V zoom sliders,
 * and auto-scroll checkbox. Used by both PianoRoll and TimelineView.
 */
class ZoomControlPanel extends JPanel {

    ZoomControlPanel(GridMode[] modes, GridMode initialMode,
                     Consumer<GridMode> onGridChange,
                     Consumer<Float> onHZoom, int hMin, int hMax, int hInitial,
                     Consumer<Float> onVZoom, int vMin, int vMax, int vInitial,
                     Consumer<Boolean> onAutoScroll, boolean autoScroll) {
        setLayout(new FlowLayout(FlowLayout.RIGHT, 5, 2));
        setBackground(Theme.getInstance().PANEL_BG);
        setBorder(BorderFactory.createMatteBorder(1, 0, 0, 0, Theme.getInstance().BORDER));

        // Grid mode selector
        JLabel gridLabel = new JLabel("Grid:");
        gridLabel.setForeground(Theme.getInstance().TEXT_DIM);
        add(gridLabel);
        JComboBox<GridMode> gridCombo = new JComboBox<>(modes);
        gridCombo.setSelectedItem(initialMode);
        gridCombo.setPreferredSize(new Dimension(70, 20));
        gridCombo.addActionListener(e -> onGridChange.accept((GridMode) gridCombo.getSelectedItem()));
        add(gridCombo);

        // Horizontal zoom slider
        JLabel hLabel = new JLabel("H:");
        hLabel.setForeground(Theme.getInstance().TEXT_DIM);
        add(hLabel);
        JSlider hSlider = new JSlider(hMin, hMax, hInitial);
        hSlider.setPreferredSize(new Dimension(80, 20));
        hSlider.addChangeListener(e -> onHZoom.accept(hSlider.getValue() / 100.0f));
        add(hSlider);

        // Vertical zoom slider
        JLabel vLabel = new JLabel("V:");
        vLabel.setForeground(Theme.getInstance().TEXT_DIM);
        add(vLabel);
        JSlider vSlider = new JSlider(vMin, vMax, vInitial);
        vSlider.setPreferredSize(new Dimension(80, 20));
        vSlider.addChangeListener(e -> onVZoom.accept((float) vSlider.getValue()));
        add(vSlider);

        // Auto-scroll checkbox
        JCheckBox autoScrollCheck = new JCheckBox("Auto-scroll", autoScroll);
        autoScrollCheck.setForeground(Theme.getInstance().TEXT_DIM);
        autoScrollCheck.setOpaque(false);
        autoScrollCheck.addActionListener(e -> onAutoScroll.accept(autoScrollCheck.isSelected()));
        add(autoScrollCheck);
    }
}
