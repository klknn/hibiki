package hibiki.ui;

import javax.swing.*;
import javax.swing.tree.*;
import java.awt.*;
import java.awt.event.*;
import java.io.File;
import java.io.BufferedReader;
import java.io.InputStreamReader;
import java.util.*;
import java.util.List;
import hibiki.BackendManager;
import com.google.flatbuffers.FlatBufferBuilder;
import hibiki.ipc.Request;
import hibiki.ipc.Command;
import hibiki.ipc.LoadPlugin;
import hibiki.ipc.LoadClip;
import java.io.PrintWriter;
import java.io.FileWriter;
import java.nio.file.Files;
import java.nio.file.Paths;
import java.util.stream.Collectors;

public class BrowserPane extends JPanel {
    private JTree tree;
    private DefaultTreeModel treeModel;
    private DefaultMutableTreeNode root;
    private Map<String, CacheEntry> cache = new HashMap<>();
    private final String HIBIKI_DIR = System.getProperty("user.home") + "/.hibiki";
    private final String CACHE_FILE = HIBIKI_DIR + "/vst3_cache";

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

    private static class CacheEntry {
        long timestamp;
        List<PluginMetadata> plugins;

        CacheEntry(long timestamp, List<PluginMetadata> plugins) {
            this.timestamp = timestamp;
            this.plugins = plugins;
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

        loadCache();
        populateTree();
        saveCache();

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

    private void populateTree() {
        DefaultMutableTreeNode pluginsNode = new DefaultMutableTreeNode("Plugins");
        DefaultMutableTreeNode midiNode = new DefaultMutableTreeNode("MIDI Files");
        DefaultMutableTreeNode audioNode = new DefaultMutableTreeNode("Audio Clips");
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

        // Apply vendor grouping and sorting
        sortAndGroupPlugins(pluginsNode);

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

    private void loadCache() {
        try {
            File file = new File(CACHE_FILE);
            if (!file.exists()) return;
            for (String line : Files.readAllLines(Paths.get(CACHE_FILE))) {
                String[] parts = line.split("\\|", 3);
                if (parts.length >= 2) {
                    String path = parts[0];
                    long ts = Long.parseLong(parts[1]);
                    List<PluginMetadata> plugins = new ArrayList<>();
                    if (parts.length == 3 && !parts[2].isEmpty()) {
                        for (String p : parts[2].split(";")) {
                            String[] kv = p.split(":", 3);
                            if (kv.length >= 2) {
                                plugins.add(new PluginMetadata(Integer.parseInt(kv[0]), kv[1], kv.length == 3 ? kv[2] : ""));
                            }
                        }
                    }
                    cache.put(path, new CacheEntry(ts, plugins));
                }
            }
        } catch (Exception e) {
            System.err.println("Failed to load VST3 cache: " + e.getMessage());
        }
    }

    private void saveCache() {
        try {
            File dir = new File(HIBIKI_DIR);
            if (!dir.exists()) dir.mkdirs();

            try (PrintWriter writer = new PrintWriter(new FileWriter(CACHE_FILE))) {
                for (Map.Entry<String, CacheEntry> entry : cache.entrySet()) {
                    String pluginsStr = entry.getValue().plugins.stream()
                            .map(p -> p.index + ":" + p.name + ":" + p.vendor)
                            .collect(Collectors.joining(";"));
                    writer.println(entry.getKey() + "|" + entry.getValue().timestamp + "|" + pluginsStr);
                }
            }
        } catch (Exception e) {
            System.err.println("Failed to save VST3 cache: " + e.getMessage());
        }
    }

    private List<PluginMetadata> getPluginsInBundle(File bundle) {
      String path = bundle.getAbsolutePath();
      long ts = bundle.lastModified();
      if (cache.containsKey(path)) {
          CacheEntry entry = cache.get(path);
          if (entry.timestamp == ts) {
              return entry.plugins;
          }
      }

      List<PluginMetadata> plugins = new ArrayList<>();
      try {
        // Find hbk-play binary
        String hbkPlayPath = "./hbk-play";
        if (!new File(hbkPlayPath).exists()) {
          hbkPlayPath = "bazel-bin/hbk-play";
        }
        
        if (!new File(hbkPlayPath).exists()) {
            String os = System.getProperty("os.name").toLowerCase();
            String binaryName = os.contains("win") ? "hbk-play.exe" : "hbk-play";
            File currentDir = new File(".");
            File bin = new File(currentDir, binaryName);
            if (bin.exists()) hbkPlayPath = bin.getAbsolutePath();
            else {
                bin = new File(currentDir, "bazel-bin/" + binaryName);
                if (bin.exists()) hbkPlayPath = bin.getAbsolutePath();
            }
        }

        if (!new File(hbkPlayPath).exists())
          return plugins;

        ProcessBuilder pb = new ProcessBuilder(hbkPlayPath, "--list", bundle.getAbsolutePath());
        Process p = pb.start();
        BufferedReader r = new BufferedReader(new InputStreamReader(p.getInputStream()));
        String line;
        while ((line = r.readLine()) != null) {
          if (line.contains(":")) {
            String[] parts = line.split(":", 3);
            try {
              int idx = Integer.parseInt(parts[0]);
              String name = parts[1];
              String vendor = parts.length == 3 ? parts[2] : "";
              plugins.add(new PluginMetadata(idx, name, vendor));
            } catch (NumberFormatException e) {
              // Ignore invalid lines
            }
          }
        }
        p.waitFor();
      } catch (Exception e) {
        // Fallback
      }
      
      cache.put(path, new CacheEntry(ts, plugins));
      return plugins;
    }

    private void scanDirectory(File dir, DefaultMutableTreeNode pluginsNode, DefaultMutableTreeNode midiNode,
        DefaultMutableTreeNode audioNode) {
        File[] files = dir.listFiles();
        if (files == null) return;

        for (File f : files) {
            if (f.isDirectory()) {
                if (f.getName().endsWith(".vst3")) {
                  List<PluginMetadata> plugins = getPluginsInBundle(f);
                  for (PluginMetadata meta : plugins) {
                    pluginsNode.add(new DefaultMutableTreeNode(new FileItem(f, "vst", meta.name, meta.vendor, meta.index)));
                  }
                  if (plugins.isEmpty()) {
                      pluginsNode.add(new DefaultMutableTreeNode(new FileItem(f, "vst", f.getName(), "", 0)));
                  }
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
              sendLoadClip(item.file.getAbsolutePath(), true); // Default to loop for audio clips from browser? Or
                                                               // follow a rule
            }
        }
    }

    private void sendLoadPlugin(String path, int pluginIndex) {
        FlatBufferBuilder builder = new FlatBufferBuilder(1024);
        int pathOffset = builder.createString(path);
        int loadPluginOffset = LoadPlugin.createLoadPlugin(builder, 1, pathOffset, pluginIndex); // Default to track 1
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

    private static class FileItem {
        File file;
        String type;
        String displayName;
        String vendor;
        int pluginIndex;

        FileItem(File file, String type, String displayName) {
          this(file, type, displayName, "", 0);
        }

        FileItem(File file, String type, String displayName, String vendor, int pluginIndex) {
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
