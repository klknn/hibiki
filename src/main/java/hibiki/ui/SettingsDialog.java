package hibiki.ui;

import java.awt.*;
import java.io.File;
import java.util.List;
import javax.swing.*;
import javax.swing.Timer;

public class SettingsDialog extends JDialog {
  private static final String[] HOST_MODES = {"In-Process", "Out-of-Process (Sandbox)"};
  static final int DEFAULT_BUFFER_MS = 200;

  /**
   * Build the audio device label based on the OS. The previous hardcoded value was Linux-specific
   * ("ALSA"). Returns a human-readable string for the Audio Engine field.
   */
  static String getAudioDeviceLabel() {
    String os = System.getProperty("os.name", "").toLowerCase();
    if (os.contains("win")) {
      return "Audio Engine: WASAPI";
    } else if (os.contains("mac")) {
      return "Audio Engine: CoreAudio";
    } else {
      return "Audio Engine: ALSA";
    }
  }

  /**
   * Resolve the buffer size from config, returning DEFAULT_BUFFER_MS only when the config is
   * missing entirely. A config with buffer_latency_ms == 0 means "not stored" in proto3 (default),
   * so we treat it as DEFAULT_BUFFER_MS too.
   */
  static int getBufferMs(hibiki.pb.commands.HibikiConfig cfg) {
    if (cfg != null && cfg.getBufferLatencyMs() > 0) {
      return cfg.getBufferLatencyMs();
    }
    return DEFAULT_BUFFER_MS;
  }

  /**
   * Resolve the mixer precision from config. Returns true for 64-bit double, false for 32-bit
   * float. Defaults to 32-bit if config is null.
   */
  static boolean getUseDoublePrecision(hibiki.pb.commands.HibikiConfig cfg) {
    return cfg != null && cfg.getUseDoublePrecision();
  }

  /**
   * Resolve the plugin host mode index for the combo box. 0 = In-Process, 1 = Sandbox. Defaults to
   * 0 (In-Process) if config is null.
   */
  static int getHostModeIndex(hibiki.pb.commands.HibikiConfig cfg) {
    if (cfg != null
        && cfg.getPluginHostMode() == hibiki.pb.commands.PluginHostMode.PLUGIN_HOST_LOCAL_SANDBOX) {
      return 1;
    }
    return 0;
  }

  /**
   * Resolve the remote hosts list from config. Returns the list from config, or a singleton
   * ["localhost:9100"] if config is null or has no hosts.
   */
  static java.util.List<String> getRemoteHosts(hibiki.pb.commands.HibikiConfig cfg) {
    if (cfg != null && cfg.getRemoteHostsCount() > 0) {
      return cfg.getRemoteHostsList();
    }
    return java.util.Collections.singletonList("localhost:9100");
  }

  /** Parse a scaling percentage string like "125%" into a float. Returns 1.0f on parse failure. */
  static float parseScaling(String scaleStr) {
    if (scaleStr == null || scaleStr.isEmpty()) return 1.0f;
    try {
      return Integer.parseInt(scaleStr.replace("%", "")) / 100.0f;
    } catch (NumberFormatException e) {
      return 1.0f;
    }
  }

  /**
   * Resolve a LAF display name to its class name for UIManager.setLookAndFeel(). Returns the fully
   * qualified class name, or null if not found.
   */
  static String resolveLafClassName(String selectedLaf, UIManager.LookAndFeelInfo[] installedLafs) {
    if ("SimpleLaf".equals(selectedLaf)) {
      return "hibiki.SimpleLaf";
    } else if ("FlatDarkLaf".equals(selectedLaf)) {
      return "com.formdev.flatlaf.FlatDarkLaf";
    } else if (installedLafs != null) {
      for (UIManager.LookAndFeelInfo info : installedLafs) {
        if (info.getName().equals(selectedLaf)) {
          return info.getClassName();
        }
      }
    }
    return null;
  }

  public SettingsDialog(Frame owner) {
    super(owner, "Settings", true);
    setLayout(new BorderLayout());
    setSize(Theme.getInstance().scale(450), Theme.getInstance().scale(450));
    setLocationRelativeTo(owner);

    // Auto-request input device list from backend
    hibiki.BackendManager.getInstance().requestAudioInputs();

    JTabbedPane tabs = new JTabbedPane();
    tabs.addTab("Audio", createAudioPanel());
    tabs.addTab("Paths", createPathsPanel());
    tabs.addTab("Plugins", createPluginsPanel());
    tabs.addTab("Appearance", createAppearancePanel());

    add(tabs, BorderLayout.CENTER);

    JPanel bottom = new JPanel(new FlowLayout(FlowLayout.RIGHT));
    JButton closeBtn = new JButton("Close");
    closeBtn.addActionListener(e -> dispose());
    bottom.add(closeBtn);
    add(bottom, BorderLayout.SOUTH);
  }

  private JPanel createAudioPanel() {
    JPanel p = new JPanel(new GridBagLayout());
    p.setBorder(BorderFactory.createEmptyBorder(20, 20, 20, 20));
    GridBagConstraints gbc = new GridBagConstraints();
    gbc.fill = GridBagConstraints.HORIZONTAL;
    gbc.insets = new Insets(5, 5, 5, 5);
    int row = 0;

    // Device info
    gbc.gridx = 0;
    gbc.gridy = row;
    gbc.gridwidth = 2;
    JLabel deviceLabel = new JLabel(getAudioDeviceLabel());
    deviceLabel.setFont(Theme.getInstance().FONT_UI);
    p.add(deviceLabel, gbc);
    gbc.gridwidth = 1;

    // Buffer Size — read from backend config if available
    hibiki.pb.commands.HibikiConfig cfg = hibiki.BackendManager.getInstance().getCurrentConfig();
    int bufferMs = getBufferMs(cfg);
    row++;
    gbc.gridx = 0;
    gbc.gridy = row;
    p.add(new JLabel("Buffer Size (ms):"), gbc);
    gbc.gridx = 1;
    JSpinner bufferSpinner = new JSpinner(new SpinnerNumberModel(bufferMs, 10, 2000, 10));
    p.add(bufferSpinner, gbc);

    // Description
    row++;
    gbc.gridx = 0;
    gbc.gridy = row;
    gbc.gridwidth = 2;
    JLabel desc =
        new JLabel(
            "<html><small>"
                + "Lower values = less latency but more glitches.<br>"
                + "Higher values = more stable but more latency.<br>"
                + "Recommended: 50ms (local), 200ms+ (remote plugins)"
                + "</small></html>");
    desc.setFont(Theme.getInstance().FONT_UI.deriveFont(11.0f));
    p.add(desc, gbc);
    gbc.gridwidth = 1;

    // Apply button for buffer
    row++;
    gbc.gridx = 1;
    gbc.gridy = row;
    JButton applyBtn = new JButton("Apply");
    applyBtn.addActionListener(
        e -> {
          int ms = (Integer) bufferSpinner.getValue();
          hibiki.pb.commands.Request request =
              hibiki.pb.commands.Request.newBuilder()
                  .setSetAudioBufferSize(
                      hibiki.pb.commands.SetAudioBufferSize.newBuilder().setBufferSizeMs(ms))
                  .build();
          hibiki.BackendManager.getInstance().sendRequest(request);
          JOptionPane.showMessageDialog(
              this, "Audio buffer set to " + ms + " ms.\nRestart the app to apply.");
        });
    p.add(applyBtn, gbc);

    // ── Processing Precision ──
    row++;
    gbc.gridx = 0;
    gbc.gridy = row;
    gbc.gridwidth = 2;
    JLabel precHeader = new JLabel("── Processing Precision ──");
    precHeader.setFont(Theme.getInstance().FONT_UI_BOLD);
    p.add(precHeader, gbc);
    gbc.gridwidth = 1;

    row++;
    gbc.gridx = 0;
    gbc.gridy = row;
    p.add(new JLabel("Mixer Precision:"), gbc);
    gbc.gridx = 1;
    String[] precisionOptions = {"32-bit float", "64-bit double"};
    JComboBox<String> precCombo = new JComboBox<>(precisionOptions);
    if (getUseDoublePrecision(cfg)) {
      precCombo.setSelectedIndex(1);
    }
    p.add(precCombo, gbc);

    row++;
    gbc.gridx = 0;
    gbc.gridy = row;
    gbc.gridwidth = 2;
    JLabel precDesc =
        new JLabel(
            "<html><small>"
                + "64-bit doubles improve dynamic range during mixing.<br>"
                + "Builtin effects and VST3 plugins remain 32-bit float."
                + "</small></html>");
    precDesc.setFont(Theme.getInstance().FONT_UI.deriveFont(11.0f));
    p.add(precDesc, gbc);
    gbc.gridwidth = 1;

    row++;
    gbc.gridx = 1;
    gbc.gridy = row;
    JButton precApplyBtn = new JButton("Apply");
    precApplyBtn.addActionListener(
        e -> {
          boolean useDouble = precCombo.getSelectedIndex() == 1;
          hibiki.pb.commands.Request request =
              hibiki.pb.commands.Request.newBuilder()
                  .setSetProcessingPrecision(
                      hibiki.pb.commands.SetProcessingPrecision.newBuilder()
                          .setUseDouble(useDouble))
                  .build();
          hibiki.BackendManager.getInstance().sendRequest(request);
          JOptionPane.showMessageDialog(
              this,
              "Mixer precision set to " + precisionOptions[precCombo.getSelectedIndex()] + ".");
        });
    p.add(precApplyBtn, gbc);

    // ── Audio Input Device ──
    row++;
    gbc.gridx = 0;
    gbc.gridy = row;
    gbc.gridwidth = 2;
    JLabel inputHeader = new JLabel("── Audio Input ──");
    inputHeader.setFont(Theme.getInstance().FONT_UI_BOLD);
    p.add(inputHeader, gbc);
    gbc.gridwidth = 1;

    row++;
    gbc.gridx = 0;
    gbc.gridy = row;
    p.add(new JLabel("Input Device:"), gbc);
    gbc.gridx = 1;
    JComboBox<String> inputCombo = new JComboBox<>();
    inputCombo.setPrototypeDisplayValue("ALSA Input Device Name (16 ch)");

    // Populate from cache
    java.util.List<hibiki.pb.notifications.AudioInputDevice> devices =
        TimelineNotificationHandler.cachedInputDevices;
    String currentDefault = hibiki.BackendManager.getInstance().getDefaultInputDeviceId();
    int selectedIdx = -1;
    for (int i = 0; i < devices.size(); i++) {
      var dev = devices.get(i);
      String label = dev.getName() + " (" + dev.getChannelCount() + " ch)";
      inputCombo.addItem(label);
      if (dev.getId().equals(currentDefault)) {
        selectedIdx = i;
      }
    }
    if (devices.isEmpty()) {
      inputCombo.addItem("(no devices — click Refresh)");
    }
    if (selectedIdx >= 0) {
      inputCombo.setSelectedIndex(selectedIdx);
    }
    p.add(inputCombo, gbc);

    // Auto-refresh after backend responds (request was sent in constructor)
    Timer autoRefresh =
        new Timer(
            500,
            ev -> {
              var freshDevs = TimelineNotificationHandler.cachedInputDevices;
              if (!freshDevs.isEmpty()) {
                inputCombo.removeAllItems();
                for (int i = 0; i < freshDevs.size(); i++) {
                  var d = freshDevs.get(i);
                  inputCombo.addItem(d.getName() + " (" + d.getChannelCount() + " ch)");
                }
              }
            });
    autoRefresh.setRepeats(false);
    autoRefresh.start();

    row++;
    gbc.gridx = 1;
    gbc.gridy = row;
    JPanel inputBtnPanel = new JPanel(new FlowLayout(FlowLayout.LEFT, 5, 0));
    JButton refreshBtn = new JButton("Refresh");
    refreshBtn.addActionListener(
        e -> {
          hibiki.BackendManager.getInstance().requestAudioInputs();
          // Re-populate after a short delay to let the notification arrive
          Timer timer =
              new Timer(
                  500,
                  ev -> {
                    inputCombo.removeAllItems();
                    var devs = TimelineNotificationHandler.cachedInputDevices;
                    for (int i = 0; i < devs.size(); i++) {
                      var d = devs.get(i);
                      inputCombo.addItem(d.getName() + " (" + d.getChannelCount() + " ch)");
                    }
                    if (devs.isEmpty()) {
                      inputCombo.addItem("(no devices found)");
                    }
                  });
          timer.setRepeats(false);
          timer.start();
        });
    inputBtnPanel.add(refreshBtn);

    JButton selectBtn = new JButton("Set Default");
    selectBtn.addActionListener(
        e -> {
          int idx = inputCombo.getSelectedIndex();
          var devs = TimelineNotificationHandler.cachedInputDevices;
          if (idx >= 0 && idx < devs.size()) {
            String devId = devs.get(idx).getId();
            hibiki.BackendManager.getInstance().setDefaultInputDeviceId(devId);
            JOptionPane.showMessageDialog(
                this, "Default input device set to: " + devs.get(idx).getName());
          }
        });
    inputBtnPanel.add(selectBtn);
    p.add(inputBtnPanel, gbc);

    return p;
  }

  private JPanel createPathsPanel() {
    JPanel p = new JPanel(new GridBagLayout());
    p.setBorder(BorderFactory.createEmptyBorder(20, 20, 20, 20));
    GridBagConstraints gbc = new GridBagConstraints();
    gbc.fill = GridBagConstraints.HORIZONTAL;
    gbc.insets = new Insets(5, 5, 5, 5);
    int row = 0;

    // Header
    gbc.gridx = 0;
    gbc.gridy = row;
    gbc.gridwidth = 2;
    JLabel header = new JLabel("Custom Audio / MIDI Search Paths");
    header.setFont(Theme.getInstance().FONT_UI_BOLD);
    p.add(header, gbc);
    gbc.gridwidth = 1;

    // Description
    row++;
    gbc.gridx = 0;
    gbc.gridy = row;
    gbc.gridwidth = 2;
    JLabel desc =
        new JLabel(
            "<html><small>"
                + "Add directories containing .wav, .mid, or .vst3 files.<br>"
                + "These will appear in the Browser alongside built-in paths."
                + "</small></html>");
    desc.setFont(Theme.getInstance().FONT_UI.deriveFont(11.0f));
    p.add(desc, gbc);
    gbc.gridwidth = 1;

    // Path list
    DefaultListModel<String> pathListModel = new DefaultListModel<>();
    List<String> currentPaths = BrowserPane.getCustomSearchPaths();
    for (String path : currentPaths) {
      pathListModel.addElement(path);
    }
    JList<String> pathList = new JList<>(pathListModel);
    pathList.setVisibleRowCount(8);
    pathList.setFont(Theme.getInstance().FONT_UI);
    JScrollPane pathScroll = new JScrollPane(pathList);
    pathScroll.setPreferredSize(new Dimension(350, 160));
    row++;
    gbc.gridx = 0;
    gbc.gridy = row;
    gbc.gridwidth = 2;
    gbc.fill = GridBagConstraints.BOTH;
    gbc.weighty = 1.0;
    p.add(pathScroll, gbc);
    gbc.fill = GridBagConstraints.HORIZONTAL;
    gbc.weighty = 0;
    gbc.gridwidth = 1;

    // Buttons
    row++;
    gbc.gridx = 0;
    gbc.gridy = row;
    gbc.gridwidth = 2;
    JPanel btnPanel = new JPanel(new FlowLayout(FlowLayout.LEFT, 5, 0));

    JButton addBtn = new JButton("Add…");
    addBtn.addActionListener(
        e -> {
          JFileChooser chooser = new JFileChooser();
          chooser.setFileSelectionMode(JFileChooser.DIRECTORIES_ONLY);
          chooser.setDialogTitle("Select Search Directory");
          if (chooser.showOpenDialog(this) == JFileChooser.APPROVE_OPTION) {
            File dir = chooser.getSelectedFile();
            String path = dir.getAbsolutePath();
            // Avoid duplicates
            for (int i = 0; i < pathListModel.size(); i++) {
              if (pathListModel.get(i).equals(path)) return;
            }
            pathListModel.addElement(path);
            applyPathChanges(pathListModel);
          }
        });
    btnPanel.add(addBtn);

    JButton removeBtn = new JButton("Remove");
    removeBtn.addActionListener(
        e -> {
          int sel = pathList.getSelectedIndex();
          if (sel >= 0) {
            pathListModel.remove(sel);
            applyPathChanges(pathListModel);
          }
        });
    btnPanel.add(removeBtn);

    p.add(btnPanel, gbc);
    gbc.gridwidth = 1;

    return p;
  }

  /** Persist the path list model and trigger a browser rescan. */
  private void applyPathChanges(DefaultListModel<String> model) {
    java.util.List<String> paths = new java.util.ArrayList<>();
    for (int i = 0; i < model.size(); i++) {
      paths.add(model.get(i));
    }
    BrowserPane.setCustomSearchPaths(paths);
    // Live-refresh the browser tree
    BrowserPane browser = BrowserPane.getInstance();
    if (browser != null) {
      browser.rescan();
    }
  }

  private JPanel createPluginsPanel() {
    JPanel p = new JPanel(new GridBagLayout());
    p.setBorder(BorderFactory.createEmptyBorder(20, 20, 20, 20));
    GridBagConstraints gbc = new GridBagConstraints();
    gbc.fill = GridBagConstraints.HORIZONTAL;
    gbc.insets = new Insets(5, 5, 5, 5);
    int row = 0;

    // Read config once for this panel
    hibiki.pb.commands.HibikiConfig pcfg = hibiki.BackendManager.getInstance().getCurrentConfig();

    // Hosting Mode
    gbc.gridx = 0;
    gbc.gridy = row;
    p.add(new JLabel("Hosting Mode:"), gbc);
    gbc.gridx = 1;
    gbc.gridwidth = 2;
    JComboBox<String> modeCombo = new JComboBox<>(HOST_MODES);
    // Pre-select from backend config
    modeCombo.setSelectedIndex(getHostModeIndex(pcfg));
    p.add(modeCombo, gbc);
    gbc.gridwidth = 1;

    // Remote Hosts list
    row++;
    gbc.gridx = 0;
    gbc.gridy = row;
    gbc.anchor = GridBagConstraints.NORTH;
    JLabel hostsLabel = new JLabel("Remote Hosts:");
    p.add(hostsLabel, gbc);

    DefaultListModel<String> hostListModel = new DefaultListModel<>();
    // Pre-populate from backend config
    for (String host : getRemoteHosts(pcfg)) {
      hostListModel.addElement(host);
    }
    JList<String> hostList = new JList<>(hostListModel);
    hostList.setVisibleRowCount(4);
    JScrollPane hostScroll = new JScrollPane(hostList);
    hostScroll.setPreferredSize(new java.awt.Dimension(200, 80));
    gbc.gridx = 1;
    gbc.gridwidth = 2;
    p.add(hostScroll, gbc);
    gbc.gridwidth = 1;
    gbc.anchor = GridBagConstraints.CENTER;

    // Add/Remove buttons
    row++;
    gbc.gridx = 1;
    gbc.gridy = row;
    JPanel hostBtnPanel = new JPanel(new FlowLayout(FlowLayout.LEFT, 5, 0));
    JButton addBtn = new JButton("+");
    addBtn.addActionListener(
        e -> {
          String host = JOptionPane.showInputDialog(this, "Enter host:port", "localhost:9100");
          if (host != null && !host.isEmpty()) {
            hostListModel.addElement(host);
          }
        });
    JButton removeBtn = new JButton("−");
    removeBtn.addActionListener(
        e -> {
          int sel = hostList.getSelectedIndex();
          if (sel >= 0) hostListModel.remove(sel);
        });
    hostBtnPanel.add(addBtn);
    hostBtnPanel.add(removeBtn);
    p.add(hostBtnPanel, gbc);

    // Description
    row++;
    gbc.gridx = 0;
    gbc.gridy = row;
    gbc.gridwidth = 3;
    JLabel desc =
        new JLabel(
            "<html><small>"
                + "In-Process: plugins run in the audio engine (lowest latency)<br>"
                + "Sandbox: isolated process per plugin (crash protection)<br>"
                + "Remote hosts: plugins from other machines via TCP (always available)"
                + "</small></html>");
    desc.setFont(Theme.getInstance().FONT_UI.deriveFont(11.0f));
    p.add(desc, gbc);
    gbc.gridwidth = 1;

    // Apply + Scan buttons
    row++;
    gbc.gridx = 1;
    gbc.gridy = row;
    JPanel actionPanel = new JPanel(new FlowLayout(FlowLayout.LEFT, 5, 0));
    JButton applyBtn = new JButton("Apply");
    applyBtn.addActionListener(
        e -> {
          int idx = modeCombo.getSelectedIndex();
          hibiki.pb.commands.PluginHostMode mode;
          switch (idx) {
            case 1:
              mode = hibiki.pb.commands.PluginHostMode.PLUGIN_HOST_LOCAL_SANDBOX;
              break;
            default:
              mode = hibiki.pb.commands.PluginHostMode.PLUGIN_HOST_IN_PROCESS;
              break;
          }

          // Always send remote hosts alongside the local mode
          hibiki.pb.commands.SetPluginHostMode.Builder builder =
              hibiki.pb.commands.SetPluginHostMode.newBuilder().setMode(mode);
          for (int i = 0; i < hostListModel.size(); i++) {
            builder.addRemoteHosts(hostListModel.get(i));
          }

          hibiki.pb.commands.Request request =
              hibiki.pb.commands.Request.newBuilder().setSetPluginHostMode(builder.build()).build();
          hibiki.BackendManager.getInstance().sendRequest(request);

          JOptionPane.showMessageDialog(this, "Plugin hosting mode set to: " + HOST_MODES[idx]);
        });
    actionPanel.add(applyBtn);

    JButton scanBtn = new JButton("Scan Remote");
    scanBtn.addActionListener(
        e -> {
          hibiki.pb.commands.ScanRemotePlugins.Builder scanBuilder =
              hibiki.pb.commands.ScanRemotePlugins.newBuilder();
          for (int i = 0; i < hostListModel.size(); i++) {
            scanBuilder.addRemoteHosts(hostListModel.get(i));
          }
          hibiki.pb.commands.Request request =
              hibiki.pb.commands.Request.newBuilder()
                  .setScanRemotePlugins(scanBuilder.build())
                  .build();
          hibiki.BackendManager.getInstance().sendRequest(request);
        });
    actionPanel.add(scanBtn);
    p.add(actionPanel, gbc);

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
    gbc.gridx = 0;
    gbc.gridy = row;
    p.add(new JLabel("Theme:"), gbc);
    gbc.gridx = 1;
    JComboBox<Theme.Preset> themeCombo = new JComboBox<>(Theme.Preset.values());
    themeCombo.setSelectedItem(Theme.getInstance().getCurrentPreset());
    p.add(themeCombo, gbc);

    // UI Scaling
    row++;
    gbc.gridx = 0;
    gbc.gridy = row;
    p.add(new JLabel("UI Scaling:"), gbc);
    gbc.gridx = 1;
    JComboBox<String> scaleCombo =
        new JComboBox<>(new String[] {"50%", "75%", "100%", "125%", "150%", "175%", "200%"});
    scaleCombo.setSelectedItem((int) (Theme.getInstance().getScaling() * 100) + "%");
    p.add(scaleCombo, gbc);

    // Font Size
    row++;
    gbc.gridx = 0;
    gbc.gridy = row;
    p.add(new JLabel("Font Size:"), gbc);
    gbc.gridx = 1;
    JSpinner fontSpinner =
        new JSpinner(new SpinnerNumberModel(Theme.getInstance().getBaseFontSize(), 8, 24, 1));
    p.add(fontSpinner, gbc);

    // Font Family
    row++;
    gbc.gridx = 0;
    gbc.gridy = row;
    p.add(new JLabel("Font:"), gbc);
    gbc.gridx = 1;
    String[] systemFonts =
        GraphicsEnvironment.getLocalGraphicsEnvironment().getAvailableFontFamilyNames();
    JComboBox<String> fontCombo = new JComboBox<>(systemFonts);
    fontCombo.setSelectedItem(Theme.getInstance().getFontFamily());
    fontCombo.setRenderer(
        new DefaultListCellRenderer() {
          @Override
          public Component getListCellRendererComponent(
              JList<?> list, Object value, int index, boolean isSelected, boolean cellHasFocus) {
            JLabel label =
                (JLabel)
                    super.getListCellRendererComponent(
                        list, value, index, isSelected, cellHasFocus);
            if (value instanceof String) {
              label.setFont(new Font((String) value, Font.PLAIN, 13));
            }
            return label;
          }
        });
    p.add(fontCombo, gbc);

    // LookAndFeel
    row++;
    gbc.gridx = 0;
    gbc.gridy = row;
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
    gbc.gridx = 1;
    gbc.gridy = row;
    JButton applyBtn = new JButton("Apply");
    applyBtn.addActionListener(
        e -> {
          // Apply theme
          Theme.Preset preset = (Theme.Preset) themeCombo.getSelectedItem();
          float scaling = parseScaling((String) scaleCombo.getSelectedItem());
          int fontSize = (Integer) fontSpinner.getValue();
          String fontFamily = (String) fontCombo.getSelectedItem();

          Theme.getInstance().update(preset, scaling, fontSize, fontFamily);

          // Apply LookAndFeel
          String selectedLaf = (String) lafCombo.getSelectedItem();
          String lafClass = resolveLafClassName(selectedLaf, lafs);
          if (lafClass != null) {
            try {
              UIManager.setLookAndFeel(lafClass);
              for (Window w : Window.getWindows()) {
                SwingUtilities.updateComponentTreeUI(w);
              }
            } catch (Exception ex) {
              JOptionPane.showMessageDialog(
                  this, "Failed to apply Look & Feel: " + ex.getMessage());
            }
          }
        });
    p.add(applyBtn, gbc);

    return p;
  }
}
