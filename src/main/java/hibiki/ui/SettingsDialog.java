package hibiki.ui;

import javax.swing.*;
import java.awt.*;
import hibiki.BackendManager;

public class SettingsDialog extends JDialog {
    public SettingsDialog(Frame owner) {
        super(owner, "Settings", true);
        setLayout(new BorderLayout());
        setSize(Theme.getInstance().scale(400), Theme.getInstance().scale(300));
        setLocationRelativeTo(owner);

        JTabbedPane tabs = new JTabbedPane();
        tabs.addTab("Audio", createAudioPanel());
        tabs.addTab("Appearance", createAppearancePanel());

        add(tabs, BorderLayout.CENTER);

        JPanel bottom = new JPanel(new FlowLayout(FlowLayout.RIGHT));
        JButton closeBtn = new JButton("Close");
        closeBtn.addActionListener(e -> dispose());
        bottom.add(closeBtn);
        add(bottom, BorderLayout.SOUTH);
    }

    private JPanel createAudioPanel() {
        JPanel p = new JPanel(new BorderLayout());
        p.setBorder(BorderFactory.createEmptyBorder(20, 20, 20, 20));

        // In a real app we'd fetch actual device name from backend info
        JLabel deviceLabel = new JLabel("Audio Engine: ALSA (alsa_playback.hbk-play)");
        deviceLabel.setFont(Theme.getInstance().FONT_UI);
        p.add(deviceLabel, BorderLayout.NORTH);

        return p;
    }

    private JPanel createAppearancePanel() {
        JPanel p = new JPanel(new GridBagLayout());
        p.setBorder(BorderFactory.createEmptyBorder(20, 20, 20, 20));
        GridBagConstraints gbc = new GridBagConstraints();
        gbc.fill = GridBagConstraints.HORIZONTAL;
        gbc.insets = new Insets(5, 5, 5, 5);
        int row = 0;

        // Theme Preset
        gbc.gridx = 0; gbc.gridy = row;
        p.add(new JLabel("Theme:"), gbc);
        gbc.gridx = 1;
        JComboBox<Theme.Preset> themeCombo = new JComboBox<>(Theme.Preset.values());
        themeCombo.setSelectedItem(Theme.getInstance().getCurrentPreset());
        p.add(themeCombo, gbc);

        // UI Scaling
        row++;
        gbc.gridx = 0; gbc.gridy = row;
        p.add(new JLabel("UI Scaling:"), gbc);
        gbc.gridx = 1;
        JComboBox<String> scaleCombo = new JComboBox<>(new String[]{"50%", "75%", "100%", "125%", "150%", "175%", "200%"});
        scaleCombo.setSelectedItem((int)(Theme.getInstance().getScaling() * 100) + "%");
        p.add(scaleCombo, gbc);

        // Font Size
        row++;
        gbc.gridx = 0; gbc.gridy = row;
        p.add(new JLabel("Font Size:"), gbc);
        gbc.gridx = 1;
        JSpinner fontSpinner = new JSpinner(new SpinnerNumberModel(Theme.getInstance().getBaseFontSize(), 8, 24, 1));
        p.add(fontSpinner, gbc);

        // Font Family
        row++;
        gbc.gridx = 0; gbc.gridy = row;
        p.add(new JLabel("Font:"), gbc);
        gbc.gridx = 1;
        String[] systemFonts = GraphicsEnvironment.getLocalGraphicsEnvironment().getAvailableFontFamilyNames();
        JComboBox<String> fontCombo = new JComboBox<>(systemFonts);
        fontCombo.setSelectedItem(Theme.getInstance().getFontFamily());
        fontCombo.setRenderer(new DefaultListCellRenderer() {
            @Override
            public Component getListCellRendererComponent(JList<?> list, Object value, int index, boolean isSelected, boolean cellHasFocus) {
                JLabel label = (JLabel) super.getListCellRendererComponent(list, value, index, isSelected, cellHasFocus);
                if (value instanceof String) {
                    label.setFont(new Font((String)value, Font.PLAIN, 13));
                }
                return label;
            }
        });
        p.add(fontCombo, gbc);

        // LookAndFeel
        row++;
        gbc.gridx = 0; gbc.gridy = row;
        p.add(new JLabel("Look & Feel:"), gbc);
        gbc.gridx = 1;
        UIManager.LookAndFeelInfo[] lafs = UIManager.getInstalledLookAndFeels();
        String[] lafNames = new String[lafs.length + 2];
        lafNames[0] = "SimpleLaf";
        lafNames[1] = "FlatDarkLaf";
        for (int i = 0; i < lafs.length; i++) {
            lafNames[i + 2] = lafs[i].getName();
        }
        JComboBox<String> lafCombo = new JComboBox<>(lafNames);
        // Select current
        String currentLafName = UIManager.getLookAndFeel().getName();
        lafCombo.setSelectedItem(currentLafName);
        p.add(lafCombo, gbc);

        // Apply button
        row++;
        gbc.gridx = 1; gbc.gridy = row;
        JButton applyBtn = new JButton("Apply");
        applyBtn.addActionListener(e -> {
            // Apply theme
            Theme.Preset preset = (Theme.Preset) themeCombo.getSelectedItem();
            String scaleStr = (String) scaleCombo.getSelectedItem();
            float scaling = Integer.parseInt(scaleStr.replace("%", "")) / 100.0f;
            int fontSize = (Integer) fontSpinner.getValue();
            String fontFamily = (String) fontCombo.getSelectedItem();

            Theme.getInstance().update(preset, scaling, fontSize, fontFamily);

            // Apply LookAndFeel
            String selectedLaf = (String) lafCombo.getSelectedItem();
            try {
                if ("SimpleLaf".equals(selectedLaf)) {
                    UIManager.setLookAndFeel(new hibiki.SimpleLaf());
                } else if ("FlatDarkLaf".equals(selectedLaf)) {
                    UIManager.setLookAndFeel(new com.formdev.flatlaf.FlatDarkLaf());
                } else {
                    for (UIManager.LookAndFeelInfo info : lafs) {
                        if (info.getName().equals(selectedLaf)) {
                            UIManager.setLookAndFeel(info.getClassName());
                            break;
                        }
                    }
                }
                // Update all windows
                for (Window w : Window.getWindows()) {
                    SwingUtilities.updateComponentTreeUI(w);
                }
            } catch (Exception ex) {
                JOptionPane.showMessageDialog(this, "Failed to apply Look & Feel: " + ex.getMessage());
            }
        });
        p.add(applyBtn, gbc);

        return p;
    }
}
