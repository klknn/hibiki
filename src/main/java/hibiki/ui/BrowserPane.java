package hibiki.ui;

import javax.swing.*;
import javax.swing.tree.*;
import java.awt.*;
import java.awt.event.*;
import java.io.File;
import java.util.*;
import java.util.List;
import java.util.concurrent.ConcurrentHashMap;
import hibiki.BackendManager;
import com.google.flatbuffers.FlatBufferBuilder;
import hibiki.ipc.Request;
import hibiki.ipc.Command;
import hibiki.ipc.LoadPlugin;
import hibiki.ipc.LoadClip;
import hibiki.ipc.ListPlugins;
import hibiki.ipc.PluginList;
import hibiki.ipc.PluginDescription;
import hibiki.ipc.Response;
import hibiki.ipc.Notification;

public class BrowserPane extends JPanel {
    private JTree tree;
    private DefaultTreeModel treeModel;
    private DefaultMutableTreeNode root;
    private DefaultMutableTreeNode pluginsNode;
    private DefaultMutableTreeNode midiNode;
    private DefaultMutableTreeNode audioNode;
    
    private Map<String, List<PluginMetadata>> bundlesDiscovered = new ConcurrentHashMap<>();
    private javax.swing.Timer refreshDebounceTimer;

    private static class PluginMetadata {
        int index;
        String name;
        String vendor;

        PluginMetadata(int index, String name, String vendor) {
            this.index = index;
            this.name = name;
            this.vendor = vendor;
        }
    }

    public BrowserPane() {
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
        tree.setTransferHandler(new TransferHandler() {
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
                        return new java.awt.datatransfer.StringSelection(item.type + ":" + item.file.getAbsolutePath());
                    }
                }
                return null;
            }
        });

        tree.addMouseListener(new MouseAdapter() {
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
        if (n.responseType() == Response.PluginList) {
            PluginList list = (PluginList) n.response(new PluginList());
            String path = list.path();
            List<PluginMetadata> plugins = new ArrayList<>();
            for (int i = 0; i < list.pluginsLength(); i++) {
                PluginDescription p = list.plugins(i);
                plugins.add(new PluginMetadata(p.index(), p.name(), p.vendor()));
            }
            bundlesDiscovered.put(path, plugins);
            // Debounce: wait 300ms after last notification before refreshing
            SwingUtilities.invokeLater(() -> {
                if (refreshDebounceTimer != null) {
                    refreshDebounceTimer.stop();
                }
                refreshDebounceTimer = new javax.swing.Timer(300, ev -> refreshPluginsTree());
                refreshDebounceTimer.setRepeats(false);
                refreshDebounceTimer.start();
            });
        }
    }

    private synchronized void refreshPluginsTree() {
        // Save current selection and expanded paths
        TreePath selectionPath = tree.getSelectionPath();
        String selectedNodeName = null;
        if (selectionPath != null) {
            DefaultMutableTreeNode selNode = (DefaultMutableTreeNode) selectionPath.getLastPathComponent();
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

        pluginsNode.removeAllChildren();
        
        for (Map.Entry<String, List<PluginMetadata>> entry : bundlesDiscovered.entrySet()) {
            File bundleFile = new File(entry.getKey());
            List<PluginMetadata> plugins = entry.getValue();
            for (PluginMetadata meta : plugins) {
                pluginsNode.add(new DefaultMutableTreeNode(new FileItem(bundleFile, "vst", meta.name, meta.vendor, meta.index)));
            }
        }

        sortAndGroupPlugins(pluginsNode);
        treeModel.reload(pluginsNode);

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

        treeModel.reload();
    }

    private void sortAndGroupPlugins(DefaultMutableTreeNode pluginsRoot) {
        List<DefaultMutableTreeNode> allPluginNodes = new ArrayList<>();
        collectPluginNodes(pluginsRoot, allPluginNodes);
        pluginsRoot.removeAllChildren();

        Map<String, List<DefaultMutableTreeNode>> byVendor = new TreeMap<>();
        for (DefaultMutableTreeNode node : allPluginNodes) {
            FileItem item = (FileItem) node.getUserObject();
            String vendor = (item.vendor != null && !item.vendor.isEmpty()) ? item.vendor : "Unknown Vendor";
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

    private void collectPluginNodes(DefaultMutableTreeNode node, List<DefaultMutableTreeNode> result) {
        for (int i = 0; i < node.getChildCount(); i++) {
            DefaultMutableTreeNode child = (DefaultMutableTreeNode) node.getChildAt(i);
            if (child.getUserObject() instanceof FileItem) {
                result.add(child);
            } else {
                collectPluginNodes(child, result);
            }
        }
    }

    private void requestPluginsInBundle(File bundle) {
        String path = bundle.getAbsolutePath();
        // Send IPC request
        FlatBufferBuilder builder = new FlatBufferBuilder(1024);
        int pathOffset = builder.createString(path);
        int listPluginsOffset = ListPlugins.createListPlugins(builder, pathOffset);
        int requestOffset = Request.createRequest(builder, Command.ListPlugins, listPluginsOffset);
        builder.finish(requestOffset);
        BackendManager.getInstance().sendRequest(builder);
    }

    private void scanDirectory(File dir, DefaultMutableTreeNode pluginsNode, DefaultMutableTreeNode midiNode,
        DefaultMutableTreeNode audioNode) {
        File[] files = dir.listFiles();
        if (files == null) return;

        for (File f : files) {
            if (f.isDirectory()) {
                if (f.getName().endsWith(".vst3")) {
                  requestPluginsInBundle(f);
                } else {
                    scanDirectory(f, pluginsNode, midiNode, audioNode);
                }
            } else {
                String name = f.getName().toLowerCase();
                if (name.endsWith(".mid") || name.endsWith(".midi")) {
                  if (midiNode != null) {
                    midiNode.add(new DefaultMutableTreeNode(new FileItem(f, "midi", f.getName())));
                  }
                } else if (name.endsWith(".wav")) {
                  if (audioNode != null) {
                    audioNode.add(new DefaultMutableTreeNode(new FileItem(f, "audio", f.getName())));
                  }
                }
            }
        }
    }

    private void onItemDoubleClicked(DefaultMutableTreeNode node) {
        Object userObject = node.getUserObject();
        if (userObject instanceof FileItem) {
            FileItem item = (FileItem) userObject;
            if ("vst".equals(item.type)) {
              sendLoadPlugin(item.file.getAbsolutePath(), item.pluginIndex);
            } else if ("midi".equals(item.type)) {
              sendLoadClip(item.file.getAbsolutePath(), false);
            } else if ("audio".equals(item.type)) {
              sendLoadClip(item.file.getAbsolutePath(), true);
            }
        }
    }

    private void sendLoadPlugin(String path, int pluginIndex) {
        FlatBufferBuilder builder = new FlatBufferBuilder(1024);
        int pathOffset = builder.createString(path);
        // Use the selected track from TimelineView
        int trackIndex = TimelineView.getInstance() != null ? TimelineView.getInstance().getSelectedTrack() : 0;
        int loadPluginOffset = LoadPlugin.createLoadPlugin(builder, trackIndex, pathOffset, pluginIndex);
        int requestOffset = Request.createRequest(builder, Command.LoadPlugin, loadPluginOffset);
        builder.finish(requestOffset);
        BackendManager.getInstance().sendRequest(builder);
    }

    private void sendLoadClip(String path, boolean isLoop) {
        FlatBufferBuilder builder = new FlatBufferBuilder(1024);
        int pathOffset = builder.createString(path);
        int loadClipOffset = LoadClip.createLoadClip(builder, 1, 0, pathOffset, isLoop); // Default to track 1, slot 0
        int requestOffset = Request.createRequest(builder, Command.LoadClip, loadClipOffset);
        builder.finish(requestOffset);
        BackendManager.getInstance().sendRequest(builder);
    }

    public static class FileItem {
        public File file;
        public String type;
        public String displayName;
        public String vendor;
        public int pluginIndex;

        public FileItem(File file, String type, String displayName) {
          this(file, type, displayName, "", 0);
        }

        public FileItem(File file, String type, String displayName, String vendor, int pluginIndex) {
            this.file = file;
            this.type = type;
            this.displayName = displayName;
            this.vendor = vendor;
            this.pluginIndex = pluginIndex;
        }

        @Override
        public String toString() {
          return displayName;
        }
    }
}
