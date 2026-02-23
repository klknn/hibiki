package hibiki.ui;

import javax.swing.*;
import java.awt.*;
import java.awt.event.*;
import java.util.List;
import java.util.ArrayList;
import java.util.Map;
import java.util.TreeMap;
import hibiki.ipc.*;
import hibiki.BackendManager;
import com.google.flatbuffers.FlatBufferBuilder;

public class PluginPane extends JPanel {
    private final JPanel deviceChainContent;
    private final Map<Integer, DevicePanel> devicePanels = new TreeMap<>();
    private final WaveformPanel waveformPanel = new WaveformPanel();
    private int currentTrackIndex = -1;

    public PluginPane() {
        setLayout(new BorderLayout());
        setBackground(Theme.BG_DARK);
        setBorder(BorderFactory.createMatteBorder(1, 0, 0, 0, Theme.BORDER));
        setPreferredSize(new Dimension(0, 200));

        deviceChainContent = new JPanel();
        deviceChainContent.setLayout(new FlowLayout(FlowLayout.LEFT, 5, 5));
        deviceChainContent.setBackground(Theme.BG_DARK);

        JScrollPane scrollPane = new JScrollPane(deviceChainContent);
        scrollPane.setBorder(null);
        scrollPane.setBackground(Theme.BG_DARK);
        scrollPane.getViewport().setBackground(Theme.BG_DARK);
        scrollPane.getHorizontalScrollBar().setUnitIncrement(16);
        add(scrollPane, BorderLayout.CENTER);

        BackendManager.getInstance().addNotificationListener(notification -> {
            if (notification.responseType() == Response.ParamList) {
                updateParams((ParamList) notification.response(new ParamList()));
            } else if (notification.responseType() == Response.ClearProject) {
                clearPanels();
            } else if (notification.responseType() == Response.ClipWaveform) {
                ClipWaveform cw = (ClipWaveform) notification.response(new ClipWaveform());
                float[] wf = new float[cw.waveformLength()];
                for (int i = 0; i < wf.length; i++)
                    wf[i] = cw.waveform(i);
                waveformPanel.setWaveform(cw.trackIndex(), cw.slotIndex(), wf);
                rebuildDeviceChain();
            }
        });
    }

    private void clearPanels() {
        SwingUtilities.invokeLater(() -> {
            devicePanels.clear();
            deviceChainContent.removeAll();
            rebuildDeviceChain();
        });
    }

    public void updateParams(ParamList paramList) {
        SwingUtilities.invokeLater(() -> {
            if (currentTrackIndex != paramList.trackIndex()) {
                currentTrackIndex = paramList.trackIndex();
                devicePanels.clear();
                deviceChainContent.removeAll();
            }

            int pIdx = paramList.pluginIndex();
            if (paramList.pluginName().isEmpty()) {
                devicePanels.remove(pIdx);
                rebuildDeviceChain();
                return;
            }

            DevicePanel panel = devicePanels.get(pIdx);
            if (panel == null || !panel.pluginName.equals(paramList.pluginName())) {
                panel = new DevicePanel(currentTrackIndex, pIdx, paramList.pluginName(), paramList.isInstrument());
                devicePanels.put(pIdx, panel);
            }
            panel.setParams(paramList);
            rebuildDeviceChain();
        });
    }

    private void rebuildDeviceChain() {
        deviceChainContent.removeAll();

        List<DevicePanel> panels = new ArrayList<>(devicePanels.values());

        // Find instrument
        DevicePanel instrument = null;
        List<DevicePanel> effects = new ArrayList<>();
        for (DevicePanel p : panels) {
            if (p.isInstrument) {
                instrument = p;
            } else {
                effects.add(p);
            }
        }

        // Add waveform if any
        if (waveformPanel.hasData()) {
            deviceChainContent.add(waveformPanel);
        }

        // Add instrument if any (removed placeholder)
        if (instrument != null) {
            deviceChainContent.add(instrument);
        }

        // Add effects in their original order
        for (DevicePanel p : effects) {
            deviceChainContent.add(p);
        }

        deviceChainContent.revalidate();
        deviceChainContent.repaint();
    }


    private class DevicePanel extends JPanel {
        private final int trackIndex;
        private final int pluginIndex;
        private final String pluginName;
        private final boolean isInstrument;
        private final JPanel paramListPanel;
        private final JTextField searchField;
        private final List<ParamPanel> allParams = new ArrayList<>();

        DevicePanel(int trackIndex, int pluginIndex, String pluginName, boolean isInstrument) {
            this.trackIndex = trackIndex;
            this.pluginIndex = pluginIndex;
            this.pluginName = pluginName;
            this.isInstrument = isInstrument;

            setLayout(new BorderLayout());
            setPreferredSize(new Dimension(250, 220));
            setBackground(new Color(45, 45, 45));
            setBorder(BorderFactory.createLineBorder(Color.BLACK));

            // Header
            JPanel header = new JPanel(new BorderLayout());
            header.setBackground(new Color(60, 60, 60));
            header.setBorder(BorderFactory.createEmptyBorder(2, 5, 2, 5));

            JLabel nameLabel = new JLabel(pluginName);
            nameLabel.setForeground(Color.WHITE);
            nameLabel.setFont(new Font("SansSerif", Font.BOLD, 12));
            header.add(nameLabel, BorderLayout.CENTER);

            JPanel btnPanel = new JPanel(new FlowLayout(FlowLayout.RIGHT, 2, 0));
            btnPanel.setOpaque(false);

            JButton editBtn = new JButton("Edit");
            editBtn.setMargin(new Insets(0, 5, 0, 5));
            editBtn.addActionListener(e -> sendShowGui());
            btnPanel.add(editBtn);

            JButton delBtn = new JButton("❌");
            delBtn.setMargin(new Insets(0, 2, 0, 2));
            delBtn.addActionListener(e -> sendRemovePlugin());
            btnPanel.add(delBtn);

            header.add(btnPanel, BorderLayout.EAST);
            add(header, BorderLayout.NORTH);

            // Search and Params
            JPanel body = new JPanel(new BorderLayout());
            body.setBackground(new Color(35, 35, 35));
            body.setBackground(Theme.BG_MEDIUM);

            searchField = new JTextField() {
                @Override
                protected void paintComponent(Graphics g) {
                    super.paintComponent(g);
                    if (getText().isEmpty() && !isFocusOwner()) {
                        Graphics2D g2 = (Graphics2D) g.create();
                        g2.setRenderingHint(RenderingHints.KEY_ANTIALIASING, RenderingHints.VALUE_ANTIALIAS_ON);
                        g2.setColor(Theme.ACCENT_BLUE);
                        g2.setFont(getFont().deriveFont(Font.ITALIC));
                        int x = getInsets().left;
                        int y = (getHeight() - g2.getFontMetrics().getHeight()) / 2 + g2.getFontMetrics().getAscent();
                        g2.drawString("search...", x, y);
                        g2.dispose();
                    }
                }
            };
            searchField.setBackground(Theme.BG_DARK);
            searchField.setForeground(Theme.TEXT_LIGHT);
            searchField.setCaretColor(Theme.TEXT_LIGHT);
            searchField.setBorder(BorderFactory.createCompoundBorder(
                    BorderFactory.createMatteBorder(0, 0, 1, 0, Theme.BORDER),
                    BorderFactory.createEmptyBorder(2, 5, 2, 5)));
            // Repaint when focus changes to show/hide placeholder
            searchField.addFocusListener(new FocusAdapter() {
                @Override
                public void focusGained(FocusEvent e) {
                    searchField.repaint();
                }

                @Override
                public void focusLost(FocusEvent e) {
                    searchField.repaint();
                }
            });
            searchField.getDocument().addDocumentListener(new javax.swing.event.DocumentListener() {
                public void insertUpdate(javax.swing.event.DocumentEvent e) {
                    filterParams();
                }

                public void removeUpdate(javax.swing.event.DocumentEvent e) {
                    filterParams();
                }

                public void changedUpdate(javax.swing.event.DocumentEvent e) {
                    filterParams();
                }
            });
            body.add(searchField, BorderLayout.NORTH);

            paramListPanel = new JPanel();
            paramListPanel.setLayout(new BoxLayout(paramListPanel, BoxLayout.Y_AXIS));
            paramListPanel.setBackground(Theme.BG_MEDIUM);

            JScrollPane scroll = new JScrollPane(paramListPanel);
            scroll.setBorder(null);
            scroll.getVerticalScrollBar().setUnitIncrement(10);
            body.add(scroll, BorderLayout.CENTER);

            add(body, BorderLayout.CENTER);
        }

        void setParams(ParamList paramList) {
            paramListPanel.removeAll();
            allParams.clear();
            for (int i = 0; i < paramList.paramsLength(); i++) {
                ParamPanel pp = new ParamPanel(trackIndex, pluginIndex, paramList.params(i));
                allParams.add(pp);
            }
            filterParams();
        }

        private void filterParams() {
            String query = searchField.getText().toLowerCase();
            paramListPanel.removeAll();
            for (ParamPanel pp : allParams) {
                if (pp.name.toLowerCase().contains(query)) {
                    paramListPanel.add(pp);
                }
            }
            paramListPanel.revalidate();
            paramListPanel.repaint();
        }

        private void sendShowGui() {
            FlatBufferBuilder builder = new FlatBufferBuilder(128);
            int showGuiOffset = hibiki.ipc.ShowPluginGui.createShowPluginGui(builder, trackIndex, pluginIndex);
            int requestOffset = Request.createRequest(builder, Command.ShowPluginGui, showGuiOffset);
            builder.finish(requestOffset);
            BackendManager.getInstance().sendRequest(builder);
        }

        private void sendRemovePlugin() {
            FlatBufferBuilder builder = new FlatBufferBuilder(128);
            int removeOff = hibiki.ipc.RemovePlugin.createRemovePlugin(builder, trackIndex, pluginIndex);
            int requestOffset = Request.createRequest(builder, Command.RemovePlugin, removeOff);
            builder.finish(requestOffset);
            BackendManager.getInstance().sendRequest(builder);

            // Immediate local feedback
            devicePanels.remove(pluginIndex);
            rebuildDeviceChain();
        }
    }

    private class ParamPanel extends JPanel {
        final String name;

        ParamPanel(int trackIndex, int pluginIndex, hibiki.ipc.ParamInfo info) {
            this.name = info.name();
            setLayout(new BorderLayout());
            setBackground(Theme.BG_MEDIUM);
            setBorder(BorderFactory.createEmptyBorder(2, 5, 2, 5));
            setMaximumSize(new Dimension(Integer.MAX_VALUE, 45));

            JLabel label = new JLabel(name);
            label.setForeground(Theme.TEXT_LIGHT);
            label.setFont(new Font("SansSerif", Font.PLAIN, 10));
            add(label, BorderLayout.NORTH);

            JSlider slider = new JSlider(0, 1000, (int) (info.defaultValue() * 1000));
            slider.setBackground(Theme.BG_MEDIUM);
            slider.setPreferredSize(new Dimension(150, 20));
            slider.addChangeListener(e -> {
                if (!slider.getValueIsAdjusting()) {
                    float val = slider.getValue() / 1000.0f;
                    sendParamChange(trackIndex, pluginIndex, info.id(), val);
                }
            });
            add(slider, BorderLayout.CENTER);
        }

        private void sendParamChange(int trackIndex, int pluginIndex, long paramId, float value) {
            FlatBufferBuilder builder = new FlatBufferBuilder(128);
            int setParamOffset = SetParamValue.createSetParamValue(builder,
                    trackIndex, pluginIndex, (int) paramId, value);
            int requestOffset = Request.createRequest(builder, Command.SetParamValue, setParamOffset);
            builder.finish(requestOffset);
            BackendManager.getInstance().sendRequest(builder);
        }
    }
}
