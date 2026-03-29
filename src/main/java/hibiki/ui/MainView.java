package hibiki.ui;

import javax.swing.*;
import java.awt.*;
import java.awt.event.KeyEvent;
import hibiki.BackendManager;
import hibiki.pb.HibikiProto;

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

        JPanel centerContainer = new JPanel(new CardLayout());
        SessionView sessionView = new SessionView();
        TimelineView timelineView = new TimelineView();
        centerContainer.add(sessionView, "SESSION");
        centerContainer.add(timelineView, "TIMELINE");

        topBar.setViewToggleListener(isTimeline -> {
            CardLayout cl = (CardLayout) centerContainer.getLayout();
            cl.show(centerContainer, isTimeline ? "TIMELINE" : "SESSION");
        });

        BrowserPane browserPane = new BrowserPane();
        pluginPane = new PluginPane();
        
        // Right side split: Center Content (Top) / Plugin Pane (Bottom)
        JSplitPane verticalSplit = new JSplitPane(JSplitPane.VERTICAL_SPLIT, centerContainer, pluginPane);
        verticalSplit.setDividerLocation(Theme.getInstance().scale(450));
        verticalSplit.setDividerSize(Theme.getInstance().scale(2));
        verticalSplit.setBorder(null);
        verticalSplit.setBackground(Theme.getInstance().BG_DARK);

        // Main split: Left=Browser, Right=CenterContent (Session/Timeline + Plugin)
        JSplitPane mainSplit = new JSplitPane(JSplitPane.HORIZONTAL_SPLIT, browserPane, verticalSplit);
        mainSplit.setDividerLocation(Theme.getInstance().scale(220));
        mainSplit.setDividerSize(Theme.getInstance().scale(2));
        mainSplit.setBorder(null);
        mainSplit.setBackground(Theme.getInstance().BG_DARK);

        add(mainSplit, BorderLayout.CENTER);

        // Disable focus traversal keys so Tab and Space can be used as shortcuts
        setFocusTraversalKeysEnabled(false);
        setFocusable(true);
        requestFocusInWindow();

        // Global shortcuts for Undo/Redo
        InputMap inputMap = getInputMap(JComponent.WHEN_IN_FOCUSED_WINDOW);
        ActionMap actionMap = getActionMap();

        inputMap.put(KeyStroke.getKeyStroke("control Z"), "undo");
        inputMap.put(KeyStroke.getKeyStroke("meta Z"), "undo"); // macOS Command+Z
        actionMap.put("undo", new AbstractAction() {
            @Override
            public void actionPerformed(java.awt.event.ActionEvent e) {
                BackendManager.getInstance().sendRequest(HibikiProto.Request.newBuilder()
                        .setUndo(HibikiProto.Undo.getDefaultInstance())
                        .build());
            }
        });

        inputMap.put(KeyStroke.getKeyStroke("control shift Z"), "redo");
        inputMap.put(KeyStroke.getKeyStroke("meta shift Z"), "redo"); // macOS Command+Shift+Z
        inputMap.put(KeyStroke.getKeyStroke("control Y"), "redo");
        inputMap.put(KeyStroke.getKeyStroke("meta Y"), "redo");
        actionMap.put("redo", new AbstractAction() {
            @Override
            public void actionPerformed(java.awt.event.ActionEvent e) {
                BackendManager.getInstance().sendRequest(HibikiProto.Request.newBuilder()
                        .setRedo(HibikiProto.Redo.getDefaultInstance())
                        .build());
            }
        });

        // Space = Play/Stop toggle (like Ableton Live)
        inputMap.put(KeyStroke.getKeyStroke(KeyEvent.VK_SPACE, 0), "playStop");
        actionMap.put("playStop", new AbstractAction() {
            @Override
            public void actionPerformed(java.awt.event.ActionEvent e) {
                BackendManager.getInstance().togglePlay();
            }
        });

        // Return/Enter = Reset playhead to start (like Ableton Live)
        inputMap.put(KeyStroke.getKeyStroke(KeyEvent.VK_ENTER, 0), "resetPlayhead");
        actionMap.put("resetPlayhead", new AbstractAction() {
            @Override
            public void actionPerformed(java.awt.event.ActionEvent e) {
                BackendManager.getInstance().seek(0);
            }
        });

        // Tab = Toggle between Session and Timeline view (like Ableton Live)
        inputMap.put(KeyStroke.getKeyStroke(KeyEvent.VK_TAB, 0), "toggleView");
        final boolean[] isTimelineView = { false };
        actionMap.put("toggleView", new AbstractAction() {
            @Override
            public void actionPerformed(java.awt.event.ActionEvent e) {
                isTimelineView[0] = !isTimelineView[0];
                CardLayout cl = (CardLayout) centerContainer.getLayout();
                cl.show(centerContainer, isTimelineView[0] ? "TIMELINE" : "SESSION");
            }
        });

        // Number keys 1-4 = Select track (like Ableton Live)
        int[] vkNumbers = { KeyEvent.VK_1, KeyEvent.VK_2, KeyEvent.VK_3, KeyEvent.VK_4 };
        for (int i = 1; i <= 4; i++) {
            final int trackNum = i;
            inputMap.put(KeyStroke.getKeyStroke(vkNumbers[i - 1], 0), "selectTrack" + i);
            actionMap.put("selectTrack" + i, new AbstractAction() {
                @Override
                public void actionPerformed(java.awt.event.ActionEvent e) {
                    if (TimelineView.getInstance() != null) {
                        TimelineView.getInstance().setSelectedTrack(trackNum - 1);
                    }
                    if (SessionView.getInstance() != null) {
                        SessionView.getInstance().selectTrackByIdx(trackNum);
                    }
                }
            });
        }

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
