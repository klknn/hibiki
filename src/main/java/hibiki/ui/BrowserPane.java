package hibiki.ui;

import javax.swing.*;
import javax.swing.tree.*;
import java.awt.*;
import java.awt.event.*;
import java.io.File;
import java.io.BufferedReader;
import java.io.InputStreamReader;
import java.util.*;
import hibiki.BackendManager;
import com.google.flatbuffers.FlatBufferBuilder;
import hibiki.ipc.Request;
import hibiki.ipc.Command;
import hibiki.ipc.LoadPlugin;
import hibiki.ipc.LoadClip;

public class BrowserPane extends JPanel {
    private JTree tree;
    private DefaultTreeModel treeModel;
    private DefaultMutableTreeNode root;

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

        populateTree();

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
        scanDirectory(new File("/usr/lib/vst3"), pluginsNode, null, null);
        scanDirectory(new File("/usr/local/lib/vst3"), pluginsNode, null, null);

        treeModel.reload();
    }

    private Map<Integer, String> getPluginsInBundle(File bundle) {
      Map<Integer, String> plugins = new LinkedHashMap<>();
      try {
        // Find hbk-play binary
        String hbkPlayPath = "./hbk-play";
        if (!new File(hbkPlayPath).exists()) {
          hbkPlayPath = "bazel-bin/hbk-play";
        }
        if (!new File(hbkPlayPath).exists())
          return plugins;

        ProcessBuilder pb = new ProcessBuilder(hbkPlayPath, "--list", bundle.getAbsolutePath());
        Process p = pb.start();
        BufferedReader r = new BufferedReader(new InputStreamReader(p.getInputStream()));
        String line;
        while ((line = r.readLine()) != null) {
          if (line.contains(":")) {
            String[] parts = line.split(":", 2);
            try {
              int idx = Integer.parseInt(parts[0]);
              plugins.put(idx, parts[1]);
            } catch (NumberFormatException e) {
              // Ignore invalid lines
            }
          }
        }
        p.waitFor();
      } catch (Exception e) {
        // Fallback
      }
      return plugins;
    }

    private void scanDirectory(File dir, DefaultMutableTreeNode pluginsNode, DefaultMutableTreeNode midiNode,
        DefaultMutableTreeNode audioNode) {
        File[] files = dir.listFiles();
        if (files == null) return;

        for (File f : files) {
            if (f.isDirectory()) {
                if (f.getName().endsWith(".vst3")) {
                  Map<Integer, String> plugins = getPluginsInBundle(f);
                  if (plugins.size() == 1) {
                    Map.Entry<Integer, String> entry = plugins.entrySet().iterator().next();
                    pluginsNode
                        .add(new DefaultMutableTreeNode(new FileItem(f, "vst", entry.getValue(), entry.getKey())));
                  } else if (plugins.size() > 1) {
                    DefaultMutableTreeNode bundleNode = new DefaultMutableTreeNode(f.getName());
                    for (Map.Entry<Integer, String> entry : plugins.entrySet()) {
                      bundleNode
                          .add(new DefaultMutableTreeNode(new FileItem(f, "vst", entry.getValue(), entry.getKey())));
                    }
                    pluginsNode.add(bundleNode);
                  } else {
                      // Fallback to filename if no plugins detected
                      pluginsNode.add(new DefaultMutableTreeNode(new FileItem(f, "vst", f.getName(), 0)));
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
        int pluginIndex;

        FileItem(File file, String type, String displayName) {
          this(file, type, displayName, 0);
        }

        FileItem(File file, String type, String displayName, int pluginIndex) {
            this.file = file;
            this.type = type;
            this.displayName = displayName;
            this.pluginIndex = pluginIndex;
        }

        @Override
        public String toString() {
          return displayName;
        }
    }
}
