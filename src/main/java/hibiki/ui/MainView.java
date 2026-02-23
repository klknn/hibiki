package hibiki.ui;

import javax.swing.*;
import java.awt.*;
import hibiki.BackendManager;
import hibiki.ipc.Response;

public class MainView extends JPanel {
    private final PluginPane pluginPane;

    public MainView() {
        setLayout(new BorderLayout());
        setBackground(Theme.BG_DARK);

        TopBar topBar = new TopBar();
        add(topBar, BorderLayout.NORTH);

        SessionView sessionView = new SessionView();
        BrowserPane browserPane = new BrowserPane();
        pluginPane = new PluginPane();

        // Right side split: Session View (Top) / Plugin Pane (Bottom)
        JSplitPane verticalSplit = new JSplitPane(JSplitPane.VERTICAL_SPLIT, sessionView, pluginPane);
        verticalSplit.setDividerLocation(500);
        verticalSplit.setDividerSize(2);
        verticalSplit.setBorder(null);
        verticalSplit.setBackground(Theme.BG_DARK);

        // Main split: Left=Browser, Right=CenterContent (Session + Plugin)
        JSplitPane mainSplit = new JSplitPane(JSplitPane.HORIZONTAL_SPLIT, browserPane, verticalSplit);
        mainSplit.setDividerLocation(220);
        mainSplit.setDividerSize(2);
        mainSplit.setBorder(null);
        mainSplit.setBackground(Theme.BG_DARK);

        add(mainSplit, BorderLayout.CENTER);

        // Status bar or footer
        JPanel footer = new JPanel(new FlowLayout(FlowLayout.LEFT, 10, 2));
        footer.setBackground(Theme.BG_DARKER);
        footer.setPreferredSize(new Dimension(0, 20));
        JLabel statusLabel = new JLabel("Status: Ready");
        statusLabel.setForeground(Theme.TEXT_DIM);
        statusLabel.setFont(new Font("SansSerif", Font.PLAIN, 9));
        footer.add(statusLabel);
        add(footer, BorderLayout.SOUTH);
    }
}
