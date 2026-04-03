package hibiki.ui;

import hibiki.BackendManager;
import hibiki.pb.commands.*;
import hibiki.pb.core.*;
import hibiki.pb.notifications.*;
import java.awt.*;
import java.awt.event.ActionEvent;
import java.awt.event.KeyEvent;
import javax.swing.*;

public class MainView extends JPanel implements Theme.ThemeListener {
  private PluginPane pluginPane;
  private ReplPanel replPanel;
  private JSplitPane replSplit;
  private JPanel mainContent;
  private boolean replVisible = false;
  private TopBar topBar;
  private JPanel centerContainer;
  private boolean isTimelineView = false;

  public MainView() {
    Theme.getInstance().addListener(this);
    initUI();
  }

  private void initUI() {
    removeAll();
    setLayout(new BorderLayout());
    setBackground(Theme.getInstance().BG_DARK);

    topBar = new TopBar();

    centerContainer = new JPanel(new CardLayout());
    SessionView sessionView = new SessionView();
    TimelineView timelineView = new TimelineView();
    centerContainer.add(sessionView, "SESSION");
    centerContainer.add(timelineView, "TIMELINE");

    topBar.setViewToggleListener(
        isTimeline -> {
          CardLayout cl = (CardLayout) centerContainer.getLayout();
          cl.show(centerContainer, isTimeline ? "TIMELINE" : "SESSION");
        });

    BrowserPane browserPane = new BrowserPane();
    pluginPane = new PluginPane();

    // Right side split: Center Content (Top) / Plugin Pane (Bottom)
    JSplitPane verticalSplit =
        new JSplitPane(JSplitPane.VERTICAL_SPLIT, centerContainer, pluginPane);
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

    // Store the main content panel for REPL split toggling
    mainContent = new JPanel(new BorderLayout());
    mainContent.add(topBar, BorderLayout.NORTH);
    mainContent.add(mainSplit, BorderLayout.CENTER);

    // REPL panel + split
    if (replPanel == null) {
      replPanel = new ReplPanel();
    }
    replSplit = new JSplitPane(JSplitPane.HORIZONTAL_SPLIT);
    replSplit.setBorder(null);
    replSplit.setDividerSize(3);
    replSplit.setContinuousLayout(true);

    // Wire REPL toggle from TopBar
    topBar.setReplToggleListener(this::toggleRepl);

    // Show either the mainContent alone or in a split with REPL
    if (replVisible) {
      replSplit.setLeftComponent(mainContent);
      replSplit.setRightComponent(replPanel);
      add(replSplit, BorderLayout.CENTER);
    } else {
      add(mainContent, BorderLayout.CENTER);
    }

    // Disable focus traversal keys so Tab and Space can be used as shortcuts
    setFocusTraversalKeysEnabled(false);
    setFocusable(true);
    requestFocusInWindow();

    // Global shortcuts for Undo/Redo
    InputMap inputMap = getInputMap(JComponent.WHEN_IN_FOCUSED_WINDOW);
    ActionMap actionMap = getActionMap();

    inputMap.put(KeyStroke.getKeyStroke("control Z"), "undo");
    inputMap.put(KeyStroke.getKeyStroke("meta Z"), "undo"); // macOS Command+Z
    actionMap.put(
        "undo",
        new AbstractAction() {
          @Override
          public void actionPerformed(java.awt.event.ActionEvent e) {
            BackendManager.getInstance()
                .sendRequest(
                    Request.newBuilder()
                        .setProject(
                            ProjectCmd.newBuilder().setAction(ProjectCmd.Action.ACTION_UNDO))
                        .build());
          }
        });

    inputMap.put(KeyStroke.getKeyStroke("control shift Z"), "redo");
    inputMap.put(KeyStroke.getKeyStroke("meta shift Z"), "redo"); // macOS Command+Shift+Z
    inputMap.put(KeyStroke.getKeyStroke("control Y"), "redo");
    inputMap.put(KeyStroke.getKeyStroke("meta Y"), "redo");
    actionMap.put(
        "redo",
        new AbstractAction() {
          @Override
          public void actionPerformed(java.awt.event.ActionEvent e) {
            BackendManager.getInstance()
                .sendRequest(
                    Request.newBuilder()
                        .setProject(
                            ProjectCmd.newBuilder().setAction(ProjectCmd.Action.ACTION_REDO))
                        .build());
          }
        });

    // Space = Play/Stop toggle (like Ableton Live)
    inputMap.put(KeyStroke.getKeyStroke(KeyEvent.VK_SPACE, 0), "playStop");
    actionMap.put(
        "playStop",
        new AbstractAction() {
          @Override
          public void actionPerformed(java.awt.event.ActionEvent e) {
            BackendManager.getInstance().togglePlay();
          }
        });

    // Return/Enter = Reset playhead to start (like Ableton Live)
    inputMap.put(KeyStroke.getKeyStroke(KeyEvent.VK_ENTER, 0), "resetPlayhead");
    actionMap.put(
        "resetPlayhead",
        new AbstractAction() {
          @Override
          public void actionPerformed(java.awt.event.ActionEvent e) {
            BackendManager.getInstance().seek(0);
          }
        });

    // Tab = Toggle between Session and Timeline view (like Ableton Live)
    inputMap.put(KeyStroke.getKeyStroke(KeyEvent.VK_TAB, 0), "toggleView");
    actionMap.put(
        "toggleView",
        new AbstractAction() {
          @Override
          public void actionPerformed(java.awt.event.ActionEvent e) {
            switchToView(!isTimelineView);
          }
        });

    // Number keys 1-4 = Select track (like Ableton Live)
    int[] vkNumbers = {KeyEvent.VK_1, KeyEvent.VK_2, KeyEvent.VK_3, KeyEvent.VK_4};
    for (int i = 1; i <= 4; i++) {
      final int trackNum = i;
      inputMap.put(KeyStroke.getKeyStroke(vkNumbers[i - 1], 0), "selectTrack" + i);
      actionMap.put(
          "selectTrack" + i,
          new AbstractAction() {
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

    // Ctrl+R = Toggle REPL panel
    inputMap.put(KeyStroke.getKeyStroke(KeyEvent.VK_R, KeyEvent.CTRL_DOWN_MASK), "toggleRepl");
    inputMap.put(KeyStroke.getKeyStroke(KeyEvent.VK_R, KeyEvent.META_DOWN_MASK), "toggleRepl");
    actionMap.put(
        "toggleRepl",
        new AbstractAction() {
          @Override
          public void actionPerformed(ActionEvent e) {
            toggleRepl();
          }
        });

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

  public void toggleRepl() {
    replVisible = !replVisible;
    removeAll();

    // Footer is always at SOUTH of this panel
    JPanel footer = new JPanel(new FlowLayout(FlowLayout.LEFT, 10, 2));
    footer.setBackground(Theme.getInstance().BG_DARKER);
    footer.setPreferredSize(new Dimension(0, Theme.getInstance().scale(20)));
    JLabel statusLabel = new JLabel("Status: Ready");
    statusLabel.setForeground(Theme.getInstance().TEXT_DIM);
    statusLabel.setFont(Theme.getInstance().FONT_UI.deriveFont(Theme.getInstance().scale(9.0f)));
    footer.add(statusLabel);

    if (replVisible) {
      replSplit.setLeftComponent(mainContent);
      replSplit.setRightComponent(replPanel);
      replSplit.setDividerLocation(getWidth() - 400);
      add(replSplit, BorderLayout.CENTER);
      replPanel.focusInput();
    } else {
      add(mainContent, BorderLayout.CENTER);
    }
    add(footer, BorderLayout.SOUTH);
    revalidate();
    repaint();
  }

  /** Switch between Session and Timeline views. */
  public void switchToView(boolean isTimeline) {
    isTimelineView = isTimeline;
    CardLayout cl = (CardLayout) centerContainer.getLayout();
    cl.show(centerContainer, isTimeline ? "TIMELINE" : "SESSION");
  }

  /** Get the TopBar instance for menu bar delegation. */
  public TopBar getTopBar() {
    return topBar;
  }

  @Override
  public void onThemeChanged() {
    SwingUtilities.invokeLater(this::initUI);
  }
}
