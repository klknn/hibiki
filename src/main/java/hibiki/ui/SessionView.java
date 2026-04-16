package hibiki.ui;

import hibiki.BackendManager;
import hibiki.SimpleLaf;
import java.awt.*;
import java.awt.datatransfer.DataFlavor;
import java.awt.datatransfer.Transferable;
import java.awt.dnd.*;
import java.awt.event.MouseAdapter;
import java.awt.event.MouseEvent;
import java.io.File;
import java.util.ArrayList;
import java.util.List;
import javax.swing.*;

public class SessionView extends JPanel {
  private static SessionView instance;
  private final SessionViewIpc ipc = new SessionViewIpc(this);

  // Dynamic track data (was fixed arrays, now supports add/remove)
  private final ArrayList<JButton[]> slotButtons = new ArrayList<>(); // per-track, 5 slots each
  final ArrayList<String[]> slotPaths = new ArrayList<>(); // paths to loaded clips
  private final ArrayList<LevelMeter> trackMeters = new ArrayList<>();
  private final ArrayList<JPanel> trackStrips = new ArrayList<>();
  final ArrayList<JLabel> trackHeaders = new ArrayList<>();
  private int selectedTrack = 0;
  private JPanel trackPanel; // The scrollable panel containing track strips

  public static SessionView getInstance() {
    return instance;
  }

  /** Select track by index */
  public void selectTrackByIdx(int trackIdx) {
    selectTrack(trackIdx);
  }

  /** Get currently selected track index */
  public int getSelectedTrack() {
    return selectedTrack;
  }

  /** Get current number of tracks */
  public int getTrackCount() {
    return trackStrips.size();
  }

  public SessionView() {
    instance = this;
    setLayout(new BorderLayout());
    setBackground(Theme.getInstance().BG_DARK);

    JPanel master = createMasterStrip();

    trackPanel =
        new JPanel() {
          @Override
          public Dimension getPreferredSize() {
            Dimension d = super.getPreferredSize();
            if (getParent() instanceof JViewport) {
              d.height = Math.max(d.height, getParent().getHeight());
            }
            return d;
          }
        };
    trackPanel.setLayout(new BoxLayout(trackPanel, BoxLayout.X_AXIS));
    trackPanel.setBackground(Theme.getInstance().BG_DARK);

    for (int i = 0; i < 4; i++) {
      addTrackInternal(i, false);
    }

    // "+" button to add new tracks
    JButton addBtn = new JButton("+");
    addBtn.setFont(Theme.getInstance().FONT_UI_BOLD);
    addBtn.setFocusPainted(false);
    addBtn.setToolTipText("Add Track");
    addBtn.setBackground(Theme.getInstance().PANEL_BG);
    addBtn.setForeground(Theme.getInstance().TEXT_DIM);
    addBtn.setPreferredSize(
        new Dimension(Theme.getInstance().scale(30), Theme.getInstance().scale(400)));
    addBtn.setMaximumSize(new Dimension(Theme.getInstance().scale(30), 32767));
    addBtn.setBorder(BorderFactory.createMatteBorder(0, 0, 0, 1, Theme.getInstance().BORDER));
    addBtn.addActionListener(e -> addTrack());
    trackPanel.add(addBtn);

    JScrollPane scrollPane = new JScrollPane(trackPanel);
    scrollPane.setHorizontalScrollBarPolicy(JScrollPane.HORIZONTAL_SCROLLBAR_AS_NEEDED);
    scrollPane.setVerticalScrollBarPolicy(JScrollPane.VERTICAL_SCROLLBAR_NEVER);
    scrollPane.setBorder(null);
    scrollPane.setBackground(Theme.getInstance().BG_DARK);
    scrollPane.getHorizontalScrollBar().setUnitIncrement(Theme.getInstance().scale(16));

    add(scrollPane, BorderLayout.CENTER);
    add(master, BorderLayout.EAST);

    BackendManager.getInstance()
        .addNotificationListener(
            notification -> {
              switch (notification.getResponseCase()) {
                case CLIP_INFO:
                  {
                    var info = notification.getClipInfo();
                    updateSlotLabel(info.getTrackIndex(), info.getSlotIndex(), info.getName());
                    if (!info.getPath().isEmpty()) {
                      if (info.getTrackIndex() < slotPaths.size()) {
                        slotPaths.get(info.getTrackIndex())[info.getSlotIndex()] = info.getPath();
                      }
                    } else {
                      if (info.getTrackIndex() < slotPaths.size()) {
                        slotPaths.get(info.getTrackIndex())[info.getSlotIndex()] = null;
                      }
                    }
                    break;
                  }
                case CLEAR_PROJECT:
                  clearAllSlots();
                  break;
                case TRACK_LEVELS:
                  {
                    var tl = notification.getTrackLevels();
                    for (int i = 0; i < tl.getLevelsCount(); i++) {
                      var l = tl.getLevels(i);
                      updateLevel(l.getTrackIndex(), l.getPeakL(), l.getPeakR());
                    }
                    break;
                  }
                default:
                  break;
              }
            });
  }

  /** Add a new track at the end, syncing with TimelineView. */
  public void addTrack() {
    int newIdx = trackStrips.size();
    addTrackNoSync();
    if (TimelineView.getInstance() != null && TimelineView.getInstance().tracks.size() <= newIdx) {
      TimelineView.getInstance().addTrackNoSync();
    }
  }

  /** Add a new track locally without syncing to TimelineView. */
  void addTrackNoSync() {
    int newIdx = trackStrips.size();
    addTrackInternal(newIdx, true);
  }

  /** Remove a track by index, syncing with TimelineView. */
  public void removeTrack(int trackIdx) {
    if (trackIdx < 0 || trackIdx >= trackStrips.size() || getVisibleTrackCount() <= 1) return;
    JPanel strip = trackStrips.get(trackIdx);
    if (strip == null || !strip.isVisible()) return;
    removeTrackNoSync(trackIdx);
    if (TimelineView.getInstance() != null && trackIdx < TimelineView.getInstance().tracks.size()) {
      TimelineView.getInstance().removeTrackNoSync(trackIdx);
    }
  }

  /** Remove a track locally without syncing to TimelineView. */
  void removeTrackNoSync(int trackIdx) {
    if (trackIdx < 0 || trackIdx >= trackStrips.size() || getVisibleTrackCount() <= 1) return;
    JPanel strip = trackStrips.get(trackIdx);
    if (strip == null || !strip.isVisible()) return;
    removeTrackInternal(trackIdx);
  }

  /** Count of visible (non-hidden) tracks. */
  int getVisibleTrackCount() {
    int count = 0;
    for (JPanel strip : trackStrips) {
      if (strip != null && strip.isVisible()) count++;
    }
    return count;
  }

  /** Internal: add a track strip to the panel without syncing. */
  private void addTrackInternal(int trackIdx, boolean rebuild) {
    JPanel strip = createTrackStrip("Track " + trackIdx, trackIdx);
    if (rebuild) {
      // Insert before the "+" button (last component)
      trackPanel.add(strip, trackPanel.getComponentCount() - 1);
      trackPanel.revalidate();
      trackPanel.repaint();
    } else {
      trackPanel.add(strip);
    }
  }

  /** Internal: hide a track strip (keep in data lists for stable indexing). */
  private void removeTrackInternal(int trackIdx) {
    if (trackIdx < 0 || trackIdx >= trackStrips.size()) return;

    // Hide the UI component (don't remove from lists)
    JPanel strip = trackStrips.get(trackIdx);
    if (strip != null) {
      strip.setVisible(false);
    }

    // Adjust selected track to next visible
    if (selectedTrack == trackIdx) {
      for (int i = 0; i < trackStrips.size(); i++) {
        JPanel s = trackStrips.get(i);
        if (s != null && s.isVisible()) {
          selectedTrack = i;
          break;
        }
      }
    }

    trackPanel.revalidate();
    trackPanel.repaint();
    updateSelectionHighlight();
  }

  void clearAllSlots() {
    SwingUtilities.invokeLater(
        () -> {
          int count = slotButtons.size();
          for (int t = 0; t < count; t++) {
            JButton[] buttons = slotButtons.get(t);
            String[] paths = slotPaths.get(t);
            for (int s = 0; s < 5; s++) {
              paths[s] = null;
              if (buttons[s] != null) {
                buttons[s].setText("");
                buttons[s].setBackground(Theme.getInstance().PANEL_BG_LIGHT);
                buttons[s].setForeground(Theme.getInstance().TEXT_NORMAL);
              }
            }
          }
        });
  }

  void updateSlotLabel(int trackIdx, int slotIdx, String name) {
    SwingUtilities.invokeLater(
        () -> {
          if (trackIdx >= 0 && trackIdx < slotButtons.size() && slotIdx >= 0 && slotIdx < 5) {
            JButton btn = slotButtons.get(trackIdx)[slotIdx];
            if (btn != null) {
              if (name.isEmpty()) {
                btn.setText("");
                btn.setBackground(Theme.getInstance().PANEL_BG_LIGHT);
                btn.setForeground(Theme.getInstance().TEXT_NORMAL);
              } else {
                btn.setText("<html><center>" + name + "<br>▶</center></html>");
                btn.setBackground(Theme.getInstance().CLIP_PLAYING);
                btn.setForeground(Color.BLACK);
              }
            }
          }
        });
  }

  void updateLevel(int trackIdx, float peakL, float peakR) {
    SwingUtilities.invokeLater(
        () -> {
          if (trackIdx >= 0 && trackIdx < trackMeters.size()) {
            LevelMeter meter = trackMeters.get(trackIdx);
            if (meter != null) {
              meter.setLevels(peakL, peakR);
            }
          }
        });
  }

  private void selectTrack(int trackIdx) {
    if (trackIdx == selectedTrack) return;
    selectedTrack = trackIdx;
    // Sync with TimelineView (both now use 0-based)
    if (TimelineView.getInstance() != null) {
      TimelineView.getInstance().setSelectedTrack(trackIdx);
    }
    updateSelectionHighlight();
  }

  private void updateSelectionHighlight() {
    int count = trackStrips.size();
    for (int i = 0; i < count; i++) {
      JPanel strip = trackStrips.get(i);
      JLabel header = trackHeaders.get(i);
      if (strip != null) {
        if (i == selectedTrack) {
          strip.setBackground(Theme.getInstance().ACCENT_BLUE.darker().darker());
          if (header != null) {
            header.setBackground(Theme.getInstance().ACCENT_BLUE.darker());
          }
        } else {
          strip.setBackground(Theme.getInstance().PANEL_BG);
          if (header != null) {
            header.setBackground(Theme.getInstance().TRACK_HEADER);
          }
        }
      }
    }
  }

  /** Show dialog to rename a track (syncs with TimelineView) */
  private void renameTrack(int trackIdx) {
    if (trackIdx < 0 || trackIdx >= trackStrips.size()) return;
    String currentName = "Track " + trackIdx;
    if (TimelineView.getInstance() != null && trackIdx < TimelineView.getInstance().tracks.size()) {
      TimelineView.TrackTimeline t = TimelineView.getInstance().tracks.get(trackIdx);
      currentName = t.getDisplayName();
    }
    String newName = JOptionPane.showInputDialog(this, "Enter track name:", currentName);
    if (newName != null) {
      if (TimelineView.getInstance() != null
          && trackIdx < TimelineView.getInstance().tracks.size()) {
        TimelineView.getInstance().tracks.get(trackIdx).customName =
            newName.isEmpty() ? null : newName;
        TimelineView.getInstance().repaint();
      }
      if (trackIdx < trackHeaders.size() && trackHeaders.get(trackIdx) != null) {
        String displayName = (newName == null || newName.isEmpty()) ? "Track " + trackIdx : newName;
        trackHeaders.get(trackIdx).setText(trackIdx + " " + displayName);
      }
    }
  }

  private JPanel createTrackStrip(String name, int trackIdx) {
    JPanel strip = new JPanel();
    strip.setLayout(new BoxLayout(strip, BoxLayout.Y_AXIS));
    strip.setBackground(Theme.getInstance().PANEL_BG);
    strip.setPreferredSize(
        new Dimension(Theme.getInstance().scale(110), Theme.getInstance().scale(400)));
    strip.setMaximumSize(new Dimension(Theme.getInstance().scale(110), 32767));
    strip.setBorder(BorderFactory.createMatteBorder(0, 0, 0, 1, Theme.getInstance().BORDER));

    // Ensure lists have space for this index (append if needed)
    while (trackStrips.size() <= trackIdx) {
      trackStrips.add(null);
      slotButtons.add(new JButton[5]);
      slotPaths.add(new String[5]);
      trackMeters.add(null);
      trackHeaders.add(null);
    }
    trackStrips.set(trackIdx, strip);

    // Header (clickable for track selection, right-click for context menu)
    JLabel header = new JLabel(trackIdx + " " + name, SwingConstants.CENTER);
    header.setAlignmentX(Component.CENTER_ALIGNMENT);
    header.setMinimumSize(
        new Dimension(Theme.getInstance().scale(110), Theme.getInstance().scale(30)));
    header.setMaximumSize(
        new Dimension(Theme.getInstance().scale(110), Theme.getInstance().scale(30)));
    header.setPreferredSize(
        new Dimension(Theme.getInstance().scale(110), Theme.getInstance().scale(30)));
    header.setBackground(Theme.getInstance().TRACK_HEADER);
    header.setForeground(Theme.getInstance().TEXT_BRIGHT);
    header.setFont(Theme.getInstance().FONT_UI_BOLD);
    header.setOpaque(true);
    header.setBorder(BorderFactory.createMatteBorder(0, 0, 1, 0, Theme.getInstance().BORDER));
    header.setCursor(Cursor.getPredefinedCursor(Cursor.HAND_CURSOR));
    trackHeaders.set(trackIdx, header);

    int finalTrackIdx = trackIdx;
    header.addMouseListener(
        new MouseAdapter() {
          @Override
          public void mousePressed(MouseEvent e) {
            if (SwingUtilities.isRightMouseButton(e)) {
              showTrackContextMenu(finalTrackIdx, e);
              return;
            }
            selectTrack(finalTrackIdx);
          }

          @Override
          public void mouseClicked(MouseEvent e) {
            if (e.getClickCount() == 2) {
              renameTrack(finalTrackIdx);
            }
          }
        });
    strip.add(header);

    // Clips
    JButton[] buttons = slotButtons.get(trackIdx);
    for (int i = 0; i < 5; i++) {
      JButton clipBtn = new JButton("");
      clipBtn.setAlignmentX(Component.CENTER_ALIGNMENT);
      clipBtn.setMinimumSize(
          new Dimension(Theme.getInstance().scale(100), Theme.getInstance().scale(30)));
      clipBtn.setMaximumSize(
          new Dimension(Theme.getInstance().scale(100), Theme.getInstance().scale(30)));
      clipBtn.setPreferredSize(
          new Dimension(Theme.getInstance().scale(100), Theme.getInstance().scale(30)));
      clipBtn.setFont(Theme.getInstance().FONT_UI);
      clipBtn.setBackground(Theme.getInstance().PANEL_BG_LIGHT);
      clipBtn.setForeground(Theme.getInstance().TEXT_NORMAL);
      clipBtn.setBorder(BorderFactory.createLineBorder(Theme.getInstance().BORDER));
      clipBtn.setFocusPainted(false);

      int slotIdx = i;
      clipBtn.addActionListener(e -> ipc.sendPlayClip(finalTrackIdx, slotIdx));

      clipBtn.addMouseListener(
          new MouseAdapter() {
            public void mousePressed(MouseEvent e) {
              if (SwingUtilities.isRightMouseButton(e)) {
                showClipContextMenu(clipBtn, finalTrackIdx, slotIdx, e.getX(), e.getY());
              }
            }
          });

      // Setup drop target for drag-drop clips (skip in headless mode for tests)
      if (!java.awt.GraphicsEnvironment.isHeadless()) {
        new DropTarget(
            clipBtn,
            new DropTargetAdapter() {
              @Override
              public void drop(DropTargetDropEvent dtde) {
                try {
                  dtde.acceptDrop(DnDConstants.ACTION_COPY);
                  Transferable t = dtde.getTransferable();
                  if (t.isDataFlavorSupported(DataFlavor.javaFileListFlavor)) {
                    @SuppressWarnings("unchecked")
                    List<File> files =
                        (List<File>) t.getTransferData(DataFlavor.javaFileListFlavor);
                    if (!files.isEmpty()) {
                      File file = files.get(0);
                      String path = file.getAbsolutePath();
                      String lower = path.toLowerCase();
                      if (lower.endsWith(".mid")
                          || lower.endsWith(".midi")
                          || lower.endsWith(".wav")
                          || lower.endsWith(".mp3")
                          || lower.endsWith(".ogg")
                          || lower.endsWith(".flac")) {
                        sendLoadClip(finalTrackIdx, slotIdx, path, false);
                      }
                    }
                  }
                  dtde.dropComplete(true);
                } catch (Exception ex) {
                  dtde.dropComplete(false);
                }
              }

              @Override
              public void dragEnter(DropTargetDragEvent dtde) {
                clipBtn.setBackground(Theme.getInstance().ACCENT_BLUE);
              }

              @Override
              public void dragExit(DropTargetEvent dte) {
                clipBtn.setBackground(Theme.getInstance().PANEL_BG_LIGHT);
              }
            });
      }

      buttons[slotIdx] = clipBtn;
      strip.add(Box.createVerticalStrut(Theme.getInstance().scale(2)));
      strip.add(clipBtn);
    }

    strip.add(Box.createVerticalGlue());

    // Level Meter and Controls
    JPanel controls = new JPanel();
    controls.setLayout(new BoxLayout(controls, BoxLayout.X_AXIS));
    controls.setOpaque(false);
    controls.setMaximumSize(
        new Dimension(Theme.getInstance().scale(110), Theme.getInstance().scale(150)));

    LevelMeter meter = new LevelMeter();
    meter.setPreferredSize(
        new Dimension(Theme.getInstance().scale(12), Theme.getInstance().scale(100)));
    trackMeters.set(trackIdx, meter);

    JPanel volPanel = new JPanel();
    volPanel.setLayout(new BoxLayout(volPanel, BoxLayout.Y_AXIS));
    volPanel.setOpaque(false);

    // Vol
    JSlider volSlider = new JSlider(JSlider.VERTICAL, -70, 6, -10);
    volSlider.setMaximumSize(
        new Dimension(Theme.getInstance().scale(30), Theme.getInstance().scale(100)));
    volSlider.setBackground(Theme.getInstance().PANEL_BG);
    volSlider.addChangeListener(
        e -> {
          int db = volSlider.getValue();
          float gain = db <= -70 ? 0.0f : (float) Math.pow(10, db / 20.0);
          BackendManager.getInstance().setTrackVolume(finalTrackIdx, Math.min(2.0f, gain));
          if (TimelineView.getInstance() != null
              && finalTrackIdx < TimelineView.getInstance().tracks.size()) {
            TimelineView.getInstance().tracks.get(finalTrackIdx).volume = gain;
            TimelineView.getInstance().repaint();
          }
        });
    volPanel.add(volSlider);

    controls.add(Box.createHorizontalStrut(Theme.getInstance().scale(10)));
    controls.add(meter);
    controls.add(Box.createHorizontalStrut(Theme.getInstance().scale(5)));
    controls.add(volPanel);
    controls.add(Box.createHorizontalStrut(Theme.getInstance().scale(10)));

    strip.add(controls);
    strip.add(Box.createVerticalStrut(Theme.getInstance().scale(5)));

    // Pan
    JSlider panSlider = new JSlider(-50, 50, 0);
    panSlider.setMaximumSize(
        new Dimension(Theme.getInstance().scale(90), Theme.getInstance().scale(20)));
    panSlider.setBackground(Theme.getInstance().PANEL_BG);
    panSlider.addChangeListener(
        e -> {
          float pan = panSlider.getValue() / 50.0f;
          BackendManager.getInstance().setTrackPan(finalTrackIdx, pan);
          if (TimelineView.getInstance() != null
              && finalTrackIdx < TimelineView.getInstance().tracks.size()) {
            TimelineView.getInstance().tracks.get(finalTrackIdx).pan = pan;
            TimelineView.getInstance().repaint();
          }
        });
    JLabel panLabel = new JLabel("C", SwingConstants.CENTER);
    panLabel.setAlignmentX(Component.CENTER_ALIGNMENT);
    panLabel.setFont(new Font("SansSerif", Font.PLAIN, Theme.getInstance().scale(8)));
    panLabel.setForeground(Theme.getInstance().TEXT_DIM);
    panSlider.addChangeListener(
        e -> {
          int v = panSlider.getValue();
          String txt = v == 0 ? "C" : (v < 0 ? "L" + (-v) : "R" + v);
          panLabel.setText(txt);
        });
    strip.add(createControlLabel("Pan"));
    strip.add(panSlider);
    strip.add(panLabel);

    // Vol value label (dB)
    JLabel volLabel = new JLabel("-10.0 dB", SwingConstants.CENTER);
    volLabel.setAlignmentX(Component.CENTER_ALIGNMENT);
    volLabel.setFont(new Font("SansSerif", Font.PLAIN, Theme.getInstance().scale(8)));
    volLabel.setForeground(Theme.getInstance().TEXT_DIM);
    volSlider.addChangeListener(
        e -> {
          int db = volSlider.getValue();
          String txt = db <= -70 ? "-∞ dB" : (db >= 0 ? "+" + db + " dB" : db + " dB");
          volLabel.setText(txt);
        });
    strip.add(volLabel);

    // Solo / Mute buttons
    JPanel smPanel = new JPanel(new FlowLayout(FlowLayout.CENTER, 4, 0));
    smPanel.setOpaque(false);
    smPanel.setMaximumSize(
        new Dimension(Theme.getInstance().scale(100), Theme.getInstance().scale(22)));

    JButton soloBtn = new JButton("S");
    soloBtn.setFont(Theme.getInstance().FONT_UI_BOLD);
    soloBtn.setFocusPainted(false);
    soloBtn.setPreferredSize(
        new Dimension(Theme.getInstance().scale(36), Theme.getInstance().scale(18)));
    soloBtn.setBackground(new Color(50, 50, 55));
    soloBtn.setForeground(new Color(160, 150, 60));
    soloBtn.setBorder(BorderFactory.createLineBorder(Theme.getInstance().BORDER));
    soloBtn.addActionListener(
        e -> {
          boolean newState = soloBtn.getBackground().getRGB() != new Color(200, 180, 40).getRGB();
          soloBtn.setBackground(newState ? new Color(200, 180, 40) : new Color(50, 50, 55));
          soloBtn.setForeground(newState ? Color.BLACK : new Color(160, 150, 60));
          BackendManager.getInstance().setTrackSolo(finalTrackIdx, newState);
          if (TimelineView.getInstance() != null
              && finalTrackIdx < TimelineView.getInstance().tracks.size()) {
            TimelineView.getInstance().tracks.get(finalTrackIdx).soloed = newState;
            TimelineView.getInstance().repaint();
          }
        });
    smPanel.add(soloBtn);

    JButton muteBtn = new JButton("M");
    muteBtn.setFont(Theme.getInstance().FONT_UI_BOLD);
    muteBtn.setFocusPainted(false);
    muteBtn.setPreferredSize(
        new Dimension(Theme.getInstance().scale(36), Theme.getInstance().scale(18)));
    muteBtn.setBackground(new Color(50, 50, 55));
    muteBtn.setForeground(new Color(160, 60, 60));
    muteBtn.setBorder(BorderFactory.createLineBorder(Theme.getInstance().BORDER));
    muteBtn.addActionListener(
        e -> {
          boolean newState = muteBtn.getBackground().getRGB() != new Color(200, 60, 60).getRGB();
          muteBtn.setBackground(newState ? new Color(200, 60, 60) : new Color(50, 50, 55));
          muteBtn.setForeground(newState ? Color.WHITE : new Color(160, 60, 60));
          BackendManager.getInstance().setTrackMute(finalTrackIdx, newState);
          if (TimelineView.getInstance() != null
              && finalTrackIdx < TimelineView.getInstance().tracks.size()) {
            TimelineView.getInstance().tracks.get(finalTrackIdx).muted = newState;
            TimelineView.getInstance().repaint();
          }
        });
    smPanel.add(muteBtn);
    strip.add(Box.createVerticalStrut(Theme.getInstance().scale(3)));
    strip.add(smPanel);

    // Activator
    JButton activeBtn = createFlatButton("" + finalTrackIdx, e -> ipc.sendStopTrack(finalTrackIdx));
    activeBtn.setBackground(new Color(200, 160, 50));
    activeBtn.setForeground(Color.BLACK);
    strip.add(Box.createVerticalStrut(Theme.getInstance().scale(5)));
    strip.add(activeBtn);
    strip.add(Box.createVerticalStrut(Theme.getInstance().scale(5)));

    return strip;
  }

  /** Show context menu for track header (right-click) */
  private void showTrackContextMenu(int trackIdx, MouseEvent e) {
    JPopupMenu menu = new JPopupMenu();

    JMenuItem renameItem = new JMenuItem("Rename Track");
    renameItem.addActionListener(ev -> renameTrack(trackIdx));
    menu.add(renameItem);

    menu.addSeparator();

    JMenuItem addItem = new JMenuItem("Add Track");
    addItem.addActionListener(ev -> addTrack());
    menu.add(addItem);

    JMenuItem deleteItem = new JMenuItem("Delete Track");
    deleteItem.setEnabled(trackStrips.size() > 1);
    deleteItem.addActionListener(ev -> removeTrack(trackIdx));
    menu.add(deleteItem);

    menu.show(e.getComponent(), e.getX(), e.getY());
  }

  private JPanel createMasterStrip() {
    JPanel strip = new JPanel();
    strip.setLayout(new BoxLayout(strip, BoxLayout.Y_AXIS));
    strip.setBackground(Theme.getInstance().PANEL_BG);
    strip.setPreferredSize(
        new Dimension(Theme.getInstance().scale(110), Theme.getInstance().scale(400)));
    strip.setMaximumSize(new Dimension(Theme.getInstance().scale(110), 32767));
    strip.setBorder(BorderFactory.createMatteBorder(0, 0, 0, 1, Theme.getInstance().BORDER));

    JLabel header = new JLabel("Master", SwingConstants.CENTER);
    header.setAlignmentX(Component.CENTER_ALIGNMENT);
    header.setMinimumSize(
        new Dimension(Theme.getInstance().scale(110), Theme.getInstance().scale(30)));
    header.setMaximumSize(
        new Dimension(Theme.getInstance().scale(110), Theme.getInstance().scale(30)));
    header.setPreferredSize(
        new Dimension(Theme.getInstance().scale(110), Theme.getInstance().scale(30)));
    header.setBackground(Theme.getInstance().TRACK_HEADER);
    header.setForeground(Theme.getInstance().TEXT_BRIGHT);
    header.setFont(Theme.getInstance().FONT_UI_BOLD);
    header.setOpaque(true);
    header.setBorder(BorderFactory.createMatteBorder(0, 0, 1, 0, Theme.getInstance().BORDER));
    strip.add(header);

    for (int i = 0; i < 5; i++) {
      int sceneIdx = i;
      JButton sceneBtn = createFlatButton((i + 1) + " ►", e -> ipc.sendPlayScene(sceneIdx));
      sceneBtn.setMinimumSize(
          new Dimension(Theme.getInstance().scale(100), Theme.getInstance().scale(30)));
      sceneBtn.setMaximumSize(
          new Dimension(Theme.getInstance().scale(100), Theme.getInstance().scale(30)));
      sceneBtn.setPreferredSize(
          new Dimension(Theme.getInstance().scale(100), Theme.getInstance().scale(30)));
      strip.add(Box.createVerticalStrut(Theme.getInstance().scale(2)));
      strip.add(sceneBtn);
    }

    strip.add(Box.createVerticalGlue());

    JSlider masterVol = new JSlider(JSlider.VERTICAL, -70, 6, 0);
    masterVol.setMaximumSize(
        new Dimension(Theme.getInstance().scale(30), Theme.getInstance().scale(100)));
    masterVol.setBackground(Theme.getInstance().PANEL_BG);
    strip.add(createControlLabel("Master"));
    strip.add(masterVol);

    return strip;
  }

  private JLabel createControlLabel(String text) {
    JLabel l = new JLabel(text, SwingConstants.CENTER);
    l.setAlignmentX(Component.CENTER_ALIGNMENT);
    l.setFont(new Font("SansSerif", Font.PLAIN, Theme.getInstance().scale(9)));
    l.setForeground(Theme.getInstance().TEXT_DIM);
    return l;
  }

  private JButton createFlatButton(String text, java.awt.event.ActionListener listener) {
    JButton btn = new JButton(text);
    btn.setAlignmentX(Component.CENTER_ALIGNMENT);
    btn.setFont(Theme.getInstance().FONT_UI_BOLD);
    btn.setFocusPainted(false);
    btn.setBorder(BorderFactory.createLineBorder(Theme.getInstance().BORDER));
    if (listener != null) btn.addActionListener(listener);
    return btn;
  }

  private void showClipContextMenu(JButton btn, int trackIdx, int slotIdx, int x, int y) {
    JPopupMenu menu = new JPopupMenu();

    JMenuItem loadItem = new JMenuItem("Load Clip...");
    loadItem.addActionListener(
        ev -> {
          if (UIManager.getLookAndFeel() instanceof SimpleLaf) {
            Frame frame = (Frame) SwingUtilities.getWindowAncestor(this);
            FileDialog dialog = new FileDialog(frame, "Load Clip", FileDialog.LOAD);
            dialog.setDirectory("testdata");
            dialog.setVisible(true);
            String dir = dialog.getDirectory();
            String file = dialog.getFile();
            if (dir != null && file != null) {
              sendLoadClip(trackIdx, slotIdx, dir + file, false);
            }
          } else {
            JFileChooser chooser = new JFileChooser("testdata");
            if (chooser.showOpenDialog(this) == JFileChooser.APPROVE_OPTION) {
              File file = chooser.getSelectedFile();
              sendLoadClip(trackIdx, slotIdx, file.getAbsolutePath(), false);
            }
          }
        });
    menu.add(loadItem);

    JMenuItem editItem = new JMenuItem("Edit Clip...");
    editItem.addActionListener(
        e -> {
          String path = (trackIdx < slotPaths.size()) ? slotPaths.get(trackIdx)[slotIdx] : null;
          if (path != null && path.endsWith(".mid")) {
            File file = new File(path);
            JFrame ownerFrame = (JFrame) SwingUtilities.getWindowAncestor(this);
            PianoRoll pr = new PianoRoll(ownerFrame, file, trackIdx, slotIdx);
            pr.setVisible(true);
          } else {
            JOptionPane.showMessageDialog(
                this, "Can only edit MIDI (.mid) clips.", "Error", JOptionPane.ERROR_MESSAGE);
          }
        });
    menu.add(editItem);

    JCheckBoxMenuItem loopItem = new JCheckBoxMenuItem("Loop");
    loopItem.addActionListener(
        e -> {
          ipc.sendSetClipLoop(trackIdx, slotIdx, loopItem.isSelected());
        });
    menu.add(loopItem);

    menu.addSeparator();
    JMenuItem deleteItem = new JMenuItem("Delete Clip");
    deleteItem.addActionListener(e -> ipc.sendDeleteClip(trackIdx, slotIdx));
    menu.add(deleteItem);

    menu.show(btn, x, y);
  }

  void sendLoadClip(int trackIdx, int slotIdx, String path, boolean isLoop) {
    ipc.sendLoadClip(trackIdx, slotIdx, path, isLoop);
  }
}
