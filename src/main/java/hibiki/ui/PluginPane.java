package hibiki.ui;

import javax.swing.*;
import java.awt.*;
import java.util.List;
import hibiki.ipc.ParamInfo;
import hibiki.ipc.ParamList;
import hibiki.BackendManager;
import com.google.flatbuffers.FlatBufferBuilder;
import hibiki.ipc.Request;
import hibiki.ipc.Command;
import hibiki.ipc.SetParamValue;

public class PluginPane extends JPanel {
    private final JPanel scrollContent;
    private int currentTrackIndex = -1;
    private int currentPluginIndex = -1;

    public PluginPane() {
        setLayout(new BorderLayout());
        setPreferredSize(new Dimension(300, 0));
        setBackground(new Color(35, 35, 35));
        setBorder(BorderFactory.createMatteBorder(0, 1, 0, 0, Color.BLACK));

        JLabel title = new JLabel("Plugin Parameters", SwingConstants.CENTER);
        title.setForeground(Color.LIGHT_GRAY);
        title.setBorder(BorderFactory.createEmptyBorder(10, 10, 10, 10));
        add(title, BorderLayout.NORTH);

        scrollContent = new JPanel();
        scrollContent.setLayout(new BoxLayout(scrollContent, BoxLayout.Y_AXIS));
        scrollContent.setBackground(new Color(35, 35, 35));

        JScrollPane scrollPane = new JScrollPane(scrollContent);
        scrollPane.setBorder(null);
        scrollPane.getVerticalScrollBar().setUnitIncrement(16);
        add(scrollPane, BorderLayout.CENTER);
    }

    public void updateParams(ParamList paramList) {
        SwingUtilities.invokeLater(() -> {
            scrollContent.removeAll();
            currentTrackIndex = paramList.trackIndex();
            currentPluginIndex = paramList.pluginIndex();

            for (int i = 0; i < paramList.paramsLength(); i++) {
                ParamInfo param = paramList.params(i);
                addParamSlider(param);
            }
            scrollContent.revalidate();
            scrollContent.repaint();
        });
    }

    private void addParamSlider(ParamInfo param) {
        JPanel paramPanel = new JPanel(new BorderLayout());
        paramPanel.setBackground(new Color(35, 35, 35));
        paramPanel.setBorder(BorderFactory.createEmptyBorder(5, 10, 5, 10));
        paramPanel.setMaximumSize(new Dimension(Integer.MAX_VALUE, 60));

        JLabel nameLabel = new JLabel(param.name());
        nameLabel.setForeground(Color.WHITE);
        paramPanel.add(nameLabel, BorderLayout.NORTH);

        JSlider slider = new JSlider(0, 1000, (int) (param.defaultValue() * 1000));
        slider.setBackground(new Color(35, 35, 35));
        slider.addChangeListener(e -> {
            if (!slider.getValueIsAdjusting()) {
                float value = slider.getValue() / 1000.0f;
                sendParamChange(param.id(), value);
            }
        });
        paramPanel.add(slider, BorderLayout.CENTER);

        scrollContent.add(paramPanel);
    }

    private void sendParamChange(long paramId, float value) {
        FlatBufferBuilder builder = new FlatBufferBuilder(128);
        int setParamOffset = SetParamValue.createSetParamValue(builder, 
            currentTrackIndex, currentPluginIndex, (int)paramId, value);
        
        int requestOffset = Request.createRequest(builder, Command.SetParamValue, setParamOffset);
        builder.finish(requestOffset);
        
        BackendManager.getInstance().sendRequest(builder);
    }
}
