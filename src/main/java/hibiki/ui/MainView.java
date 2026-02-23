package hibiki.ui;

import javax.swing.*;
import java.awt.*;
import hibiki.BackendManager;
import hibiki.ipc.Response;

public class MainView extends JPanel implements Theme.ThemeListener {
    private PluginPane pluginPane;

    public MainView() {
        Theme.getInstance().addListener(this);
        initUI();
    }

    private void initUI() {
        removeAll();
        setLayout(new BorderLayout());
        setBackground(Theme.getInstance().BG_DARK);

        TopBar topBar = new TopBar();
        add(topBar, BorderLayout.NORTH);

        SessionView sessionView = new SessionView();
        BrowserPane browserPane = new BrowserPane();
        pluginPane = new PluginPane();

        // Right side split: Session View (Top) / Plugin Pane (Bottom)
        JSplitPane verticalSplit = new JSplitPane(JSplitPane.VERTICAL_SPLIT, sessionView, pluginPane);
        verticalSplit.setDividerLocation(Theme.getInstance().scale(450));
        verticalSplit.setDividerSize(Theme.getInstance().scale(2));
        verticalSplit.setBorder(null);
        verticalSplit.setBackground(Theme.getInstance().BG_DARK);

        // Main split: Left=Browser, Right=CenterContent (Session + Plugin)
        JSplitPane mainSplit = new JSplitPane(JSplitPane.HORIZONTAL_SPLIT, browserPane, verticalSplit);
        mainSplit.setDividerLocation(Theme.getInstance().scale(220));
        mainSplit.setDividerSize(Theme.getInstance().scale(2));
        mainSplit.setBorder(null);
        mainSplit.setBackground(Theme.getInstance().BG_DARK);

        add(mainSplit, BorderLayout.CENTER);

        // Status bar or footer
        JPanel footer = new JPanel(new FlowLayout(FlowLayout.LEFT, 10, 2));
        footer.setBackground(Theme.getInstance().BG_DARKER);
        footer.setPreferredSize(new Dimension(0, Theme.getInstance().scale(20)));
        JLabel statusLabel = new JLabel("Status: Ready");
        statusLabel.setForeground(Theme.getInstance().TEXT_DIM);
        statusLabel.setFont(Theme.getInstance().FONT_UI.deriveFont(Theme.getInstance().scale(9.0f)));
        footer.add(statusLabel);
        add(footer, BorderLayout.SOUTH);
        revalidate();
        repaint();
    }

    @Override
    public void onThemeChanged() {
        SwingUtilities.invokeLater(this::initUI);
    }
}
