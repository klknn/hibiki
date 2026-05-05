package hibiki.ui;

import hibiki.BackendManager;
import hibiki.pb.commands.*;
import hibiki.pb.core.*;
import hibiki.pb.core.Clip;
import hibiki.pb.core.EntityRef;
import hibiki.pb.notifications.*;
import hibiki.pb.notifications.Notification;
import java.awt.*;
import java.awt.event.*;
import java.io.File;
import java.util.*;
import java.util.List;
import java.util.concurrent.ConcurrentHashMap;
import java.util.prefs.Preferences;
import javax.swing.*;
import javax.swing.tree.*;

public class BrowserPane extends JPanel {
  private static final String PREFS_KEY = "customSearchPaths";
  private static final Preferences PREFS = Preferences.userNodeForPackage(BrowserPane.class);

  private JTree tree;
  private DefaultTreeModel treeModel;
  private DefaultMutableTreeNode root;
  private DefaultMutableTreeNode pluginsNode;
  private DefaultMutableTreeNode midiNode;
  private DefaultMutableTreeNode audioNode;
  private DefaultMutableTreeNode userNode;
  private DefaultMutableTreeNode projectNode;

  private Map<String, List<PluginMetadata>> bundlesDiscovered = new ConcurrentHashMap<>();
  // Remote plugins: key = "host:port", value = list of plugins from that daemon
  private Map<String, List<PluginMetadata>> remoteDiscovered = new ConcurrentHashMap<>();
  // Tree nodes for each remote host
  private Map<String, DefaultMutableTreeNode> remoteHostNodes = new ConcurrentHashMap<>();
  private javax.swing.Timer refreshDebounceTimer;

  private static BrowserPane instance;

  /** Return the singleton instance (set during construction). */
  public static BrowserPane getInstance() {
    return instance;
  }

  // ── Custom search path persistence ──────────────────────────────────

  /** Get the persisted list of custom audio/MIDI search directories. */
  public static List<String> getCustomSearchPaths() {
    String raw = PREFS.get(PREFS_KEY, "");
    if (raw.isEmpty()) return new ArrayList<>();
    return new ArrayList<>(Arrays.asList(raw.split("\\|")));
  }

  /** Persist the list of custom audio/MIDI search directories. */
  public static void setCustomSearchPaths(List<String> paths) {
    PREFS.put(PREFS_KEY, String.join("|", paths));
  }

  private static class PluginMetadata {
    int index;
    String name;
    String vendor;
    String path; // .vst3 bundle path on the remote filesystem

    PluginMetadata(int index, String name, String vendor, String path) {
      this.index = index;
      this.name = name;
      this.vendor = vendor;
      this.path = path;
    }
  }

  public BrowserPane() {
    instance = this;
    setLayout(new BorderLayout());
    setBackground(Theme.getInstance().BG_DARK);
    setPreferredSize(new Dimension(Theme.getInstance().scale(220), 0));
    setBorder(BorderFactory.createMatteBorder(0, 0, 0, 1, Theme.getInstance().BORDER));

    JLabel header = new JLabel("BROWSER", SwingConstants.LEFT);
    header.setBorder(BorderFactory.createEmptyBorder(5, 10, 5, 0));
    header.setBackground(Theme.getInstance().TRACK_HEADER);
    header.setForeground(Theme.getInstance().TEXT_BRIGHT);
    header.setFont(Theme.getInstance().FONT_UI_BOLD);
    header.setOpaque(true);
    header.setPreferredSize(new Dimension(0, Theme.getInstance().scale(30)));
    add(header, BorderLayout.NORTH);

    root = new DefaultMutableTreeNode("Hibiki");
    treeModel = new DefaultTreeModel(root);
    tree = new JTree(treeModel);
    tree.setBackground(Theme.getInstance().BG_DARK);
    tree.setFont(Theme.getInstance().FONT_UI);
    tree.setRowHeight(Theme.getInstance().scale(20));

    // Custom renderer for visibility
    DefaultTreeCellRenderer renderer = new DefaultTreeCellRenderer();
    renderer.setBackgroundNonSelectionColor(Theme.getInstance().BG_DARK);
    renderer.setTextNonSelectionColor(Theme.getInstance().TEXT_NORMAL);
    renderer.setTextSelectionColor(Theme.getInstance().TEXT_BRIGHT);
    renderer.setBackgroundSelectionColor(Theme.getInstance().PANEL_BG_LIGHT);
    renderer.setBorderSelectionColor(Theme.getInstance().BORDER);
    renderer.setLeafIcon(null);
    renderer.setOpenIcon(null);
    renderer.setClosedIcon(null);
    tree.setCellRenderer(renderer);

    JScrollPane scrollPane = new JScrollPane(tree);
    scrollPane.getViewport().setBackground(Theme.getInstance().BG_DARK);
    scrollPane.setBorder(null);
    add(scrollPane, BorderLayout.CENTER);

    BackendManager.getInstance().addNotificationListener(this::handleNotification);

    populateTree();

    if (!java.awt.GraphicsEnvironment.isHeadless()) {
      tree.setDragEnabled(true);
    }
    tree.setTransferHandler(
        new TransferHandler() {
          @Override
          public int getSourceActions(JComponent c) {
            return COPY;
          }

          @Override
          protected java.awt.datatransfer.Transferable createTransferable(JComponent c) {
            TreePath path = tree.getSelectionPath();
            if (path != null) {
              DefaultMutableTreeNode node = (DefaultMutableTreeNode) path.getLastPathComponent();
              Object userObject = node.getUserObject();
              if (userObject instanceof FileItem) {
                FileItem item = (FileItem) userObject;
                return new java.awt.datatransfer.StringSelection(
                    item.type + ":" + item.file.getAbsolutePath());
              }
            }
            return null;
          }
        });

    tree.addMouseListener(
        new MouseAdapter() {
          public void mouseClicked(MouseEvent e) {
            if (e.getClickCount() == 2) {
              TreePath path = tree.getPathForLocation(e.getX(), e.getY());
              if (path != null) {
                DefaultMutableTreeNode node = (DefaultMutableTreeNode) path.getLastPathComponent();
                if (node.isLeaf()) {
                  onItemDoubleClicked(node);
                }
              }
            }
          }
        });
  }

  private void handleNotification(Notification n) {
    if (n.getResponseCase() == Notification.ResponseCase.PLUGIN_LIST) {
      var list = n.getPluginList();
      String remoteHost = list.getRemoteHost();
      List<PluginMetadata> plugins = new ArrayList<>();
      for (int i = 0; i < list.getPluginsCount(); i++) {
        var p = list.getPlugins(i);
        plugins.add(new PluginMetadata(p.getIndex(), p.getName(), p.getVendor(), p.getPath()));
      }
      if (!remoteHost.isEmpty()) {
        // Remote daemon — store under host key
        remoteDiscovered.computeIfAbsent(remoteHost, k -> new ArrayList<>()).addAll(plugins);
      } else {
        // Local bundle discovery
        bundlesDiscovered.put(list.getPath(), plugins);
      }
      // Debounce: wait 300ms after last notification before refreshing
      SwingUtilities.invokeLater(
          () -> {
            if (refreshDebounceTimer != null) {
              refreshDebounceTimer.stop();
            }
            refreshDebounceTimer = new javax.swing.Timer(300, ev -> refreshPluginsTree());
            refreshDebounceTimer.setRepeats(false);
            refreshDebounceTimer.start();
          });
    } else if (n.getResponseCase() == Notification.ResponseCase.PROJECT_INFO) {
      String dir = n.getProjectInfo().getProjectDir();
      SwingUtilities.invokeLater(() -> setProjectDir(dir));
    }
  }

  /** Refresh the "Project" tree node to show files in the given directory. */
  public void setProjectDir(String dir) {
    if (dir == null || dir.isEmpty()) return;
    if (projectNode != null) {
      root.remove(projectNode);
    }
    projectNode = new DefaultMutableTreeNode("Project");
    File projDir = new File(dir);
    if (projDir.exists() && projDir.isDirectory()) {
      DefaultMutableTreeNode dirTree = buildDirectoryTree(projDir);
      if (dirTree != null) {
        // Add children of the root dir node directly under "Project"
        for (int i = 0; i < dirTree.getChildCount(); ) {
          DefaultMutableTreeNode child = (DefaultMutableTreeNode) dirTree.getChildAt(i);
          dirTree.remove(child);
          projectNode.add(child);
        }
      }
    }
    root.insert(projectNode, 0); // Project always at top
    treeModel.reload();
  }

  private synchronized void refreshPluginsTree() {
    // Save current selection and expanded paths
    TreePath selectionPath = tree.getSelectionPath();
    String selectedNodeName = null;
    if (selectionPath != null) {
      DefaultMutableTreeNode selNode =
          (DefaultMutableTreeNode) selectionPath.getLastPathComponent();
      selectedNodeName = selNode.toString();
    }
    java.util.Enumeration<TreePath> expandedPaths = tree.getExpandedDescendants(new TreePath(root));
    Set<String> expandedNames = new java.util.HashSet<>();
    if (expandedPaths != null) {
      while (expandedPaths.hasMoreElements()) {
        TreePath ep = expandedPaths.nextElement();
        DefaultMutableTreeNode epNode = (DefaultMutableTreeNode) ep.getLastPathComponent();
        expandedNames.add(epNode.toString());
      }
    }

    // Rebuild local plugins
    pluginsNode.removeAllChildren();
    for (Map.Entry<String, List<PluginMetadata>> entry : bundlesDiscovered.entrySet()) {
      File bundleFile = new File(entry.getKey());
      List<PluginMetadata> plugins = entry.getValue();
      for (PluginMetadata meta : plugins) {
        pluginsNode.add(
            new DefaultMutableTreeNode(
                new FileItem(bundleFile, "vst", meta.name, meta.vendor, meta.index)));
      }
    }
    sortAndGroupPlugins(pluginsNode);
    treeModel.reload(pluginsNode);

    // Rebuild remote host nodes
    for (DefaultMutableTreeNode oldNode : remoteHostNodes.values()) {
      root.remove(oldNode);
    }
    remoteHostNodes.clear();
    for (Map.Entry<String, List<PluginMetadata>> entry : remoteDiscovered.entrySet()) {
      String host = entry.getKey();
      DefaultMutableTreeNode hostNode = new DefaultMutableTreeNode("\uD83D\uDCE1 " + host);
      for (PluginMetadata meta : entry.getValue()) {
        // Use the real .vst3 bundle path from the remote daemon, fall back to host
        File bundleFile =
            (meta.path != null && !meta.path.isEmpty()) ? new File(meta.path) : new File(host);
        hostNode.add(
            new DefaultMutableTreeNode(
                new FileItem(bundleFile, "remote-vst", meta.name, meta.vendor, meta.index, host)));
      }
      sortAndGroupPlugins(hostNode);
      root.add(hostNode);
      remoteHostNodes.put(host, hostNode);
    }
    treeModel.reload(root);

    // Restore expanded paths
    restoreExpansion(new TreePath(root), expandedNames);

    // Restore selection
    if (selectedNodeName != null) {
      restoreSelection(new TreePath(root), selectedNodeName);
    }
  }

  private void restoreExpansion(TreePath parent, Set<String> expandedNames) {
    DefaultMutableTreeNode node = (DefaultMutableTreeNode) parent.getLastPathComponent();
    if (expandedNames.contains(node.toString())) {
      tree.expandPath(parent);
    }
    for (int i = 0; i < node.getChildCount(); i++) {
      DefaultMutableTreeNode child = (DefaultMutableTreeNode) node.getChildAt(i);
      restoreExpansion(parent.pathByAddingChild(child), expandedNames);
    }
  }

  private void restoreSelection(TreePath parent, String targetName) {
    DefaultMutableTreeNode node = (DefaultMutableTreeNode) parent.getLastPathComponent();
    if (node.toString().equals(targetName) && node.isLeaf()) {
      tree.setSelectionPath(parent);
      return;
    }
    for (int i = 0; i < node.getChildCount(); i++) {
      DefaultMutableTreeNode child = (DefaultMutableTreeNode) node.getChildAt(i);
      restoreSelection(parent.pathByAddingChild(child), targetName);
    }
  }

  private void populateTree() {
    pluginsNode = new DefaultMutableTreeNode("Plugins");
    midiNode = new DefaultMutableTreeNode("MIDI Files");
    audioNode = new DefaultMutableTreeNode("Audio Clips");

    // Built-in effects node (always at the top)
    DefaultMutableTreeNode builtinNode = new DefaultMutableTreeNode("Built-in");
    FileItem eqItem = new FileItem(new File("builtin"), "builtin", "EQ Eight", "Hibiki", 0);
    eqItem.rawPath = "builtin://eq";
    builtinNode.add(new DefaultMutableTreeNode(eqItem));
    FileItem compItem = new FileItem(new File("builtin"), "builtin", "Compressor", "Hibiki", 0);
    compItem.rawPath = "builtin://compressor";
    builtinNode.add(new DefaultMutableTreeNode(compItem));
    FileItem oscItem = new FileItem(new File("builtin"), "builtin", "3xOsc", "Hibiki", 0);
    oscItem.rawPath = "builtin://3xosc";
    builtinNode.add(new DefaultMutableTreeNode(oscItem));
    FileItem samplerItem = new FileItem(new File("builtin"), "builtin", "Sampler", "Hibiki", 0);
    samplerItem.rawPath = "builtin://sampler";
    builtinNode.add(new DefaultMutableTreeNode(samplerItem));
    FileItem delayItem = new FileItem(new File("builtin"), "builtin", "Delay", "Hibiki", 0);
    delayItem.rawPath = "builtin://delay";
    builtinNode.add(new DefaultMutableTreeNode(delayItem));
    FileItem reverbItem = new FileItem(new File("builtin"), "builtin", "Reverb", "Hibiki", 0);
    reverbItem.rawPath = "builtin://reverb";
    builtinNode.add(new DefaultMutableTreeNode(reverbItem));
    FileItem limiterItem = new FileItem(new File("builtin"), "builtin", "Limiter", "Hibiki", 0);
    limiterItem.rawPath = "builtin://limiter";
    builtinNode.add(new DefaultMutableTreeNode(limiterItem));
    FileItem hottItem = new FileItem(new File("builtin"), "builtin", "Hott", "Hibiki", 0);
    hottItem.rawPath = "builtin://hott";
    builtinNode.add(new DefaultMutableTreeNode(hottItem));
    FileItem envShaperItem = new FileItem(new File("builtin"), "builtin", "EnvShaper", "Hibiki", 0);
    envShaperItem.rawPath = "builtin://envelope_shaper";
    builtinNode.add(new DefaultMutableTreeNode(envShaperItem));
    FileItem phaserItem = new FileItem(new File("builtin"), "builtin", "Phaser", "Hibiki", 0);
    phaserItem.rawPath = "builtin://phaser";
    builtinNode.add(new DefaultMutableTreeNode(phaserItem));
    FileItem convolverItem = new FileItem(new File("builtin"), "builtin", "Convolver", "Hibiki", 0);
    convolverItem.rawPath = "builtin://convolver";
    builtinNode.add(new DefaultMutableTreeNode(convolverItem));
    FileItem filmItem = new FileItem(new File("builtin"), "builtin", "FilM", "Hibiki", 0);
    filmItem.rawPath = "builtin://film";
    builtinNode.add(new DefaultMutableTreeNode(filmItem));
    FileItem auxItem = new FileItem(new File("builtin"), "builtin", "Aux", "Hibiki", 0);
    auxItem.rawPath = "builtin://aux";
    builtinNode.add(new DefaultMutableTreeNode(auxItem));

    // DX7 SysEx presets: scan testdata/ for .syx files and list patches.
    DefaultMutableTreeNode dx7Node = new DefaultMutableTreeNode("DX7 Presets");
    File testDataDir = new File("testdata");
    if (testDataDir.exists() && testDataDir.isDirectory()) {
      File[] syxFiles = testDataDir.listFiles((dir, name) -> name.toLowerCase().endsWith(".syx"));
      if (syxFiles != null) {
        Arrays.sort(syxFiles);
        for (File syxFile : syxFiles) {
          String[] names = readDx7PatchNames(syxFile);
          if (names != null && names.length > 0) {
            DefaultMutableTreeNode bankNode =
                new DefaultMutableTreeNode(syxFile.getName().replace(".syx", ""));
            for (int i = 0; i < names.length; i++) {
              String patchName = String.format("%02d: %s", i + 1, names[i].trim());
              FileItem presetItem = new FileItem(syxFile, "builtin", patchName, "DX7", 0);
              presetItem.rawPath =
                  "builtin://film?syx=" + syxFile.getAbsolutePath() + "&voice=" + i;
              bankNode.add(new DefaultMutableTreeNode(presetItem));
            }
            dx7Node.add(bankNode);
          }
        }
      }
    }
    if (dx7Node.getChildCount() > 0) {
      builtinNode.add(dx7Node);
    }

    root.add(builtinNode);

    root.add(pluginsNode);
    root.add(midiNode);
    root.add(audioNode);

    // Scan testdata directory
    File testData = new File("testdata");
    if (testData.exists() && testData.isDirectory()) {
      scanDirectory(testData, pluginsNode, midiNode, audioNode);
    }

    // Scan standard VST3 directories
    String home = System.getProperty("user.home");
    scanDirectory(new File(home + "/.vst3"), pluginsNode, null, null);

    String os = System.getProperty("os.name").toLowerCase();
    if (os.contains("win")) {
      scanDirectory(new File("C:\\Program Files\\Common Files\\VST3"), pluginsNode, null, null);
    } else if (os.contains("mac")) {
      scanDirectory(new File("/Library/Audio/Plug-ins/VST3"), pluginsNode, null, null);
      scanDirectory(new File(home + "/Library/Audio/Plug-ins/VST3"), pluginsNode, null, null);
    } else {
      scanDirectory(new File("/usr/lib/vst3"), pluginsNode, null, null);
      scanDirectory(new File("/usr/local/lib/vst3"), pluginsNode, null, null);
    }

    // User-defined search paths — shown in a separate "User" tree with dir structure
    userNode = new DefaultMutableTreeNode("User");
    for (String customPath : getCustomSearchPaths()) {
      File customDir = new File(customPath);
      if (customDir.exists() && customDir.isDirectory()) {
        DefaultMutableTreeNode dirNode = buildDirectoryTree(customDir);
        if (dirNode != null) {
          userNode.add(dirNode);
        }
      }
    }
    if (userNode.getChildCount() > 0) {
      root.add(userNode);
    }

    treeModel.reload();
  }

  /** Re-scan all directories and rebuild the tree (called after custom paths change). */
  public void rescan() {
    root.removeAllChildren();
    bundlesDiscovered.clear();
    populateTree();
  }

  private void sortAndGroupPlugins(DefaultMutableTreeNode pluginsRoot) {
    List<DefaultMutableTreeNode> allPluginNodes = new ArrayList<>();
    collectPluginNodes(pluginsRoot, allPluginNodes);
    pluginsRoot.removeAllChildren();

    Map<String, List<DefaultMutableTreeNode>> byVendor = new TreeMap<>();
    for (DefaultMutableTreeNode node : allPluginNodes) {
      FileItem item = (FileItem) node.getUserObject();
      String vendor =
          (item.vendor != null && !item.vendor.isEmpty()) ? item.vendor : "Unknown Vendor";
      byVendor.computeIfAbsent(vendor, k -> new ArrayList<>()).add(node);
    }

    for (Map.Entry<String, List<DefaultMutableTreeNode>> entry : byVendor.entrySet()) {
      DefaultMutableTreeNode vendorNode = new DefaultMutableTreeNode(entry.getKey());
      List<DefaultMutableTreeNode> nodes = entry.getValue();
      nodes.sort(Comparator.comparing(n -> n.toString().toLowerCase()));
      for (DefaultMutableTreeNode node : nodes) {
        vendorNode.add(node);
      }
      pluginsRoot.add(vendorNode);
    }
  }

  private void collectPluginNodes(
      DefaultMutableTreeNode node, List<DefaultMutableTreeNode> result) {
    for (int i = 0; i < node.getChildCount(); i++) {
      DefaultMutableTreeNode child = (DefaultMutableTreeNode) node.getChildAt(i);
      if (child.getUserObject() instanceof FileItem) {
        result.add(child);
      } else {
        collectPluginNodes(child, result);
      }
    }
  }

  private void requestPluginsInBundles(java.util.List<String> paths) {
    if (paths.isEmpty()) return;
    PluginCmd.Builder cmd = PluginCmd.newBuilder().setAction(PluginCmd.Action.ACTION_LIST);
    for (String p : paths) {
      cmd.addPaths(p);
    }
    BackendManager.getInstance().sendRequest(Request.newBuilder().setPlugin(cmd).build());
  }

  private void scanDirectory(
      File dir,
      DefaultMutableTreeNode pluginsNode,
      DefaultMutableTreeNode midiNode,
      DefaultMutableTreeNode audioNode) {
    java.util.List<String> bundlePaths = new java.util.ArrayList<>();
    collectFiles(dir, pluginsNode, midiNode, audioNode, bundlePaths);
    // Send all .vst3 bundles as a single batch request for parallel scanning
    requestPluginsInBundles(bundlePaths);
  }

  /**
   * Build a tree node that mirrors the real directory structure under {@code dir}. Leaf nodes are
   * FileItems for supported file types (.wav, .mid/.midi, .vst3). Empty directories are pruned.
   */
  private DefaultMutableTreeNode buildDirectoryTree(File dir) {
    DefaultMutableTreeNode dirNode = new DefaultMutableTreeNode(dir.getName());
    File[] children = dir.listFiles();
    if (children == null) return null;

    // Sort: directories first, then files, both alphabetically
    java.util.Arrays.sort(
        children,
        (a, b) -> {
          if (a.isDirectory() != b.isDirectory()) return a.isDirectory() ? -1 : 1;
          return a.getName().compareToIgnoreCase(b.getName());
        });

    java.util.List<String> bundlePaths = new java.util.ArrayList<>();
    for (File f : children) {
      String name = f.getName().toLowerCase();
      if (f.isDirectory()) {
        if (name.endsWith(".vst3")) {
          // VST3 bundles are directories — treat as plugin leaf
          bundlePaths.add(f.getAbsolutePath());
        } else {
          DefaultMutableTreeNode childDir = buildDirectoryTree(f);
          if (childDir != null && childDir.getChildCount() > 0) {
            dirNode.add(childDir);
          }
        }
      } else if (name.endsWith(".mid") || name.endsWith(".midi")) {
        dirNode.add(new DefaultMutableTreeNode(new FileItem(f, "midi", f.getName())));
      } else if (name.endsWith(".wav")
          || name.endsWith(".flac")
          || name.endsWith(".mp3")
          || name.endsWith(".ogg")
          || name.endsWith(".aiff")) {
        dirNode.add(new DefaultMutableTreeNode(new FileItem(f, "audio", f.getName())));
      }
    }
    // Kick off VST3 bundle scanning for any found in this user directory
    requestPluginsInBundles(bundlePaths);

    return dirNode.getChildCount() > 0 ? dirNode : null;
  }

  private void collectFiles(
      File dir,
      DefaultMutableTreeNode pluginsNode,
      DefaultMutableTreeNode midiNode,
      DefaultMutableTreeNode audioNode,
      java.util.List<String> bundlePaths) {
    File[] files = dir.listFiles();
    if (files == null) return;

    for (File f : files) {
      String name = f.getName().toLowerCase();
      if (name.endsWith(".vst3")) {
        bundlePaths.add(f.getAbsolutePath());
      } else if (f.isDirectory()) {
        collectFiles(f, pluginsNode, midiNode, audioNode, bundlePaths);
      } else if (midiNode != null && (name.endsWith(".mid") || name.endsWith(".midi"))) {
        midiNode.add(new DefaultMutableTreeNode(new FileItem(f, "midi", f.getName())));
      } else if (audioNode != null && name.endsWith(".wav")) {
        audioNode.add(new DefaultMutableTreeNode(new FileItem(f, "audio", f.getName())));
      }
    }
  }

  private void onItemDoubleClicked(DefaultMutableTreeNode node) {
    Object userObject = node.getUserObject();
    if (userObject instanceof FileItem) {
      FileItem item = (FileItem) userObject;
      if ("builtin".equals(item.type)) {
        sendLoadPlugin(item.rawPath, 0);
      } else if ("vst".equals(item.type)) {
        sendLoadPlugin(item.file.getAbsolutePath(), item.pluginIndex);
      } else if ("remote-vst".equals(item.type)) {
        // For remote plugins, use getPath() not getAbsolutePath() to avoid
        // Linux CWD being prepended to Windows paths (e.g. C:\...).
        sendLoadPlugin(item.file.getPath(), item.pluginIndex, item.remoteHost);
      } else if ("midi".equals(item.type)) {
        sendLoadClip(item.file.getAbsolutePath(), false);
      } else if ("audio".equals(item.type)) {
        sendLoadClip(item.file.getAbsolutePath(), true);
      }
    }
  }

  private void sendLoadPlugin(String path, int pluginIndex) {
    sendLoadPlugin(path, pluginIndex, "");
  }

  private void sendLoadPlugin(String path, int pluginIndex, String remoteHost) {
    int trackIndex =
        SessionView.getInstance() != null ? SessionView.getInstance().getSelectedTrack() : 0;
    PluginCmd.Builder pluginCmd =
        PluginCmd.newBuilder()
            .setAction(PluginCmd.Action.ACTION_LOAD)
            .setTarget(EntityRef.newBuilder().setTrackIndex(trackIndex).setPluginIndex(pluginIndex))
            .setPath(path);
    if (remoteHost != null && !remoteHost.isEmpty()) {
      pluginCmd.setRemoteHost(remoteHost);
    }
    BackendManager.getInstance().sendRequest(Request.newBuilder().setPlugin(pluginCmd).build());
  }

  private void sendLoadClip(String path, boolean isLoop) {
    int trackIndex =
        SessionView.getInstance() != null ? SessionView.getInstance().getSelectedTrack() : 0;
    BackendManager.getInstance()
        .sendRequest(
            Request.newBuilder()
                .setTrack(
                    TrackCmd.newBuilder()
                        .setAction(TrackCmd.Action.ACTION_LOAD_CLIP)
                        .setTarget(
                            EntityRef.newBuilder().setTrackIndex(trackIndex).setSessionSlot(0))
                        .setClipData(Clip.newBuilder().setPath(path).setIsLoop(isLoop)))
                .build());
  }

  // --- DX7 SysEx patch name reader ---
  // Reads a 32-voice bulk dump and returns 32 patch name strings.
  private static String[] readDx7PatchNames(File syxFile) {
    try {
      java.io.FileInputStream fis = new java.io.FileInputStream(syxFile);
      byte[] data = fis.readAllBytes();
      fis.close();
      if (data.length < 4104) return null;
      // Validate header: F0 43 0x 09 20 00
      if ((data[0] & 0xFF) != 0xF0 || (data[1] & 0xFF) != 0x43) return null;
      if ((data[3] & 0xFF) != 0x09) return null;

      String[] names = new String[32];
      for (int v = 0; v < 32; v++) {
        int offset = 6 + v * 128; // skip 6-byte header
        char[] nameChars = new char[10];
        for (int c = 0; c < 10; c++) {
          int b = data[offset + 118 + c] & 0x7F;
          nameChars[c] = (b >= 32 && b < 127) ? (char) b : ' ';
        }
        names[v] = new String(nameChars);
      }
      return names;
    } catch (Exception e) {
      return null;
    }
  }

  public static class FileItem {
    public File file;
    public String type;
    public String displayName;
    public String vendor;
    public int pluginIndex;
    public String remoteHost; // non-empty for remote plugins ("host:port")
    public String rawPath; // for builtin:// paths that File would mangle

    public FileItem(File file, String type, String displayName) {
      this(file, type, displayName, "", 0, "");
    }

    public FileItem(File file, String type, String displayName, String vendor, int pluginIndex) {
      this(file, type, displayName, vendor, pluginIndex, "");
    }

    public FileItem(
        File file,
        String type,
        String displayName,
        String vendor,
        int pluginIndex,
        String remoteHost) {
      this.file = file;
      this.type = type;
      this.displayName = displayName;
      this.vendor = vendor;
      this.pluginIndex = pluginIndex;
      this.remoteHost = remoteHost != null ? remoteHost : "";
    }

    @Override
    public String toString() {
      return displayName;
    }
  }
}
