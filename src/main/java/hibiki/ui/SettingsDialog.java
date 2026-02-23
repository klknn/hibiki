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
        gbc.gridx = 0; gbc.gridy = 0;

        p.add(new JLabel("Theme:"), gbc);
        gbc.gridx = 1;
        JComboBox<Theme.Preset> themeCombo = new JComboBox<>(Theme.Preset.values());
        themeCombo.setSelectedItem(Theme.getInstance().getCurrentPreset());
        p.add(themeCombo, gbc);

        gbc.gridx = 0; gbc.gridy = 1;
        p.add(new JLabel("UI Scaling:"), gbc);
        gbc.gridx = 1;
        JComboBox<String> scaleCombo = new JComboBox<>(new String[]{"50%", "75%", "100%", "125%", "150%", "175%", "200%"});
        scaleCombo.setSelectedItem((int)(Theme.getInstance().getScaling() * 100) + "%");
        p.add(scaleCombo, gbc);

        gbc.gridx = 0; gbc.gridy = 2;
        p.add(new JLabel("Font Size:"), gbc);
        gbc.gridx = 1;
        JSpinner fontSpinner = new JSpinner(new SpinnerNumberModel(Theme.getInstance().getBaseFontSize(), 8, 24, 1));
        p.add(fontSpinner, gbc);

        gbc.gridx = 1; gbc.gridy = 3;
        JButton applyBtn = new JButton("Apply");
        applyBtn.addActionListener(e -> {
            Theme.Preset preset = (Theme.Preset) themeCombo.getSelectedItem();
            String scaleStr = (String) scaleCombo.getSelectedItem();
            float scaling = Integer.parseInt(scaleStr.replace("%", "")) / 100.0f;
            int fontSize = (Integer) fontSpinner.getValue();

            Theme.getInstance().update(preset, scaling, fontSize);
            // Re-apply to self to show scaling in dialog too if desired, though usually dialog closes
            JOptionPane.showMessageDialog(this, "Theme updated. Some changes may require a restart for full effect.");
        });
        p.add(applyBtn, gbc);

        return p;
    }
}
