package hibiki.ui;

import javax.swing.*;
import java.awt.*;
import hibiki.BackendManager;
import hibiki.ipc.Response;

public class MainView extends JPanel {
    private final PluginPane pluginPane;

    public MainView() {
        setLayout(new BorderLayout());

        TopBar topBar = new TopBar();
        add(topBar, BorderLayout.NORTH);

        // Main split: Browser | (Session / Detail)
        JSplitPane horizontalSplit = new JSplitPane(JSplitPane.HORIZONTAL_SPLIT);
        horizontalSplit.setDividerLocation(250);

        BrowserPane browserPane = new BrowserPane();
        horizontalSplit.setLeftComponent(browserPane);

        // Right side split: Session View (Top) / Plugin Pane (Bottom)
        JSplitPane verticalSplit = new JSplitPane(JSplitPane.VERTICAL_SPLIT);
        verticalSplit.setDividerLocation(500);

        SessionView sessionView = new SessionView();
        verticalSplit.setTopComponent(sessionView);

        pluginPane = new PluginPane();
        verticalSplit.setBottomComponent(pluginPane);

        horizontalSplit.setRightComponent(verticalSplit);
        add(horizontalSplit, BorderLayout.CENTER);

        // Status Bar
        JPanel statusBar = new JPanel(new FlowLayout(FlowLayout.LEFT));
        statusBar.setBackground(new Color(160, 160, 160));
        statusBar.setPreferredSize(new Dimension(0, 25));
        statusBar.add(new JLabel("Ready"));
        add(statusBar, BorderLayout.SOUTH);

        // Listen for backend notifications
        BackendManager.getInstance().addNotificationListener(notification -> {
            if (notification.responseType() == Response.ParamList) {
                pluginPane.updateParams((hibiki.ipc.ParamList) notification.response(new hibiki.ipc.ParamList()));
            }
        });
    }
}
