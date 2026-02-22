package hibiki.ui;

import javax.swing.*;
import java.awt.*;
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
    private int currentTrackIndex = -1;

    public PluginPane() {
        setLayout(new BorderLayout());
        setBackground(new Color(25, 25, 25));
        setBorder(BorderFactory.createMatteBorder(1, 0, 0, 0, Color.BLACK));
        setPreferredSize(new Dimension(0, 250));

        deviceChainContent = new JPanel();
        deviceChainContent.setLayout(new FlowLayout(FlowLayout.LEFT, 5, 5));
        deviceChainContent.setBackground(new Color(25, 25, 25));

        JScrollPane scrollPane = new JScrollPane(deviceChainContent);
        scrollPane.setBorder(null);
        scrollPane.setBackground(new Color(25, 25, 25));
        scrollPane.getHorizontalScrollBar().setUnitIncrement(16);
        add(scrollPane, BorderLayout.CENTER);
    }

    public void updateParams(ParamList paramList) {
        SwingUtilities.invokeLater(() -> {
            if (currentTrackIndex != paramList.trackIndex()) {
                currentTrackIndex = paramList.trackIndex();
                devicePanels.clear();
                deviceChainContent.removeAll();
            }

            int pIdx = paramList.pluginIndex();
            DevicePanel panel = devicePanels.get(pIdx);
            if (panel == null) {
                panel = new DevicePanel(currentTrackIndex, pIdx, paramList.pluginName());
                devicePanels.put(pIdx, panel);
                rebuildDeviceChain();
            }
            panel.setParams(paramList);
        });
    }

    private void rebuildDeviceChain() {
        deviceChainContent.removeAll();
        for (DevicePanel panel : devicePanels.values()) {
            deviceChainContent.add(panel);
        }
        deviceChainContent.revalidate();
        deviceChainContent.repaint();
    }

    private static class DevicePanel extends JPanel {
        private final int trackIndex;
        private final int pluginIndex;
        private final String pluginName;
        private final JPanel paramListPanel;
        private final JTextField searchField;
        private final List<ParamPanel> allParams = new ArrayList<>();

        DevicePanel(int trackIndex, int pluginIndex, String pluginName) {
            this.trackIndex = trackIndex;
            this.pluginIndex = pluginIndex;
            this.pluginName = pluginName;

            setLayout(new BorderLayout());
            setPreferredSize(new Dimension(220, 220));
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

            JButton editBtn = new JButton("Edit");
            editBtn.setMargin(new Insets(0, 5, 0, 5));
            editBtn.addActionListener(e -> sendShowGui());
            header.add(editBtn, BorderLayout.EAST);
            add(header, BorderLayout.NORTH);

            // Search and Params
            JPanel body = new JPanel(new BorderLayout());
            body.setBackground(new Color(35, 35, 35));

            searchField = new JTextField();
            searchField.setBackground(new Color(50, 50, 50));
            searchField.setForeground(Color.WHITE);
            searchField.setCaretColor(Color.WHITE);
            searchField.setBorder(BorderFactory.createCompoundBorder(
                    BorderFactory.createMatteBorder(0, 0, 1, 0, Color.BLACK),
                    BorderFactory.createEmptyBorder(2, 5, 2, 5)));
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
            paramListPanel.setBackground(new Color(35, 35, 35));

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
    }

    private static class ParamPanel extends JPanel {
        final String name;

        ParamPanel(int trackIndex, int pluginIndex, hibiki.ipc.ParamInfo info) {
            this.name = info.name();
            setLayout(new BorderLayout());
            setBackground(new Color(35, 35, 35));
            setBorder(BorderFactory.createEmptyBorder(2, 5, 2, 5));
            setMaximumSize(new Dimension(Integer.MAX_VALUE, 45));

            JLabel label = new JLabel(name);
            label.setForeground(Color.LIGHT_GRAY);
            label.setFont(new Font("SansSerif", Font.PLAIN, 10));
            add(label, BorderLayout.NORTH);

            JSlider slider = new JSlider(0, 1000, (int) (info.defaultValue() * 1000));
            slider.setBackground(new Color(35, 35, 35));
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
