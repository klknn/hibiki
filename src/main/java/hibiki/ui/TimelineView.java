package hibiki.ui;

import hibiki.BackendManager;
import hibiki.pb.commands.*;
import hibiki.pb.core.*;
import hibiki.pb.core.EntityRef;
import hibiki.pb.notifications.*;
import hibiki.pb.notifications.Notification;
import java.awt.*;
import java.awt.event.*;
import java.io.File;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import javax.swing.*;

public class TimelineView extends JPanel implements Theme.ThemeListener {
  private static final int BASE_TRACK_HEIGHT = 80;
  private static final int AUTOMATION_LANE_HEIGHT = 60;
  static final int TIME_RULER_HEIGHT = 30;
  static final int LOOP_RULER_HEIGHT = 12;
  static final int MARKER_LANE_HEIGHT = 16;
  static final int TOTAL_RULER_HEIGHT = MARKER_LANE_HEIGHT + TIME_RULER_HEIGHT + LOOP_RULER_HEIGHT;
  private int trackLabelWidth = 140;
  private static final float BASE_PIXELS_PER_SECOND = 50.0f;

  // Zoom scales (adjustable via sliders)
  private float hZoomScale = 1.0f; // Horizontal zoom multiplier
  private float vZoomScale = 1.0f; // Vertical zoom multiplier

  // Convenience getters for zoom-scaled values
  int getTrackHeight() {
    return getBaseTrackHeight();
  }

  float getPixelsPerSecond() {
    return BASE_PIXELS_PER_SECOND * hZoomScale;
  }

  /** Height of just the clip area for one track (no automation). */
  int getBaseTrackHeight() {
    return (int) (BASE_TRACK_HEIGHT * vZoomScale);
  }

  /** Height of one track with per-track override. */
  int getBaseTrackHeight(int trackIdx) {
    if (trackIdx >= 0 && trackIdx < tracks.size() && tracks.get(trackIdx).hidden) return 0;
    if (trackIdx >= 0 && trackIdx < tracks.size()) {
      int custom = tracks.get(trackIdx).customHeight;
      if (custom > 0) return custom;
    }
    return getBaseTrackHeight();
  }

  /** Height of one automation lane sub-row. */
  int getAutomationLaneHeight() {
    return (int) (AUTOMATION_LANE_HEIGHT * vZoomScale);
  }

  /** Total height for a track including expanded automation lanes. */
  int getTotalTrackHeight(int trackIdx) {
    if (trackIdx >= 0 && trackIdx < tracks.size() && tracks.get(trackIdx).hidden) return 0;
    int h = getBaseTrackHeight(trackIdx);
    if (trackIdx >= 0 && trackIdx < tracks.size()) {
      TrackTimeline t = tracks.get(trackIdx);
      if (t.automationExpanded && !t.automationLanes.isEmpty()) {
        h += t.automationLanes.size() * getAutomationLaneHeight();
      }
    }
    return h;
  }

  /** Y offset of a track from top of content area (after time ruler). */
  int getTrackY(int trackIdx) {
    int y = 0;
    for (int i = 0; i < trackIdx && i < tracks.size(); i++) {
      y += getTotalTrackHeight(i);
    }
    return y;
  }

  /** Total height of all tracks. */
  int getTotalTracksHeight() {
    int h = 0;
    for (int i = 0; i < tracks.size(); i++) {
      h += getTotalTrackHeight(i);
    }
    return h;
  }

  /** Resolve a track index from a scaled Y offset (after time ruler). */
  int getTrackIdxAtY(int scaledY) {
    int cumY = 0;
    for (int i = 0; i < tracks.size(); i++) {
      if (tracks.get(i).hidden) continue;
      int th = Theme.getInstance().scale(getTotalTrackHeight(i));
      if (scaledY < cumY + th) return i;
      cumY += th;
    }
    // Fall back to last visible track
    for (int i = tracks.size() - 1; i >= 0; i--) {
      if (!tracks.get(i).hidden) return i;
    }
    return 0;
  }

  /** Count of visible (non-hidden) tracks. */
  int getVisibleTrackCount() {
    int count = 0;
    for (TrackTimeline t : tracks) {
      if (!t.hidden) count++;
    }
    return count;
  }

  // GridMode is shared - see GridMode.java

  private GridMode gridMode = GridMode.AUTO;

  GridMode getGridMode() {
    return gridMode;
  }

  /** Set the grid mode (used by MenuBarFactory). */
  public void setGridMode(GridMode mode) {
    this.gridMode = mode;
    repaint();
  }

  volatile float bpm = 120.0f;
  volatile boolean isPlaying = false;

  volatile float playheadPos = 0.0f; // volatile for thread-safe updates from notification thread
  volatile boolean loopEnabled = false;
  volatile float loopStartSec = 0.0f;
  volatile float loopEndSec = 0.0f;

  /** A named marker on the timeline with optional local BPM/time-sig overrides. */
  static class TimelineMarker implements Comparable<TimelineMarker> {
    String name;
    float positionSec;
    float bpm; // 0 = inherit global
    int beatsPerBar; // 0 = inherit global
    int beatDenominator; // 0 = inherit global
    Color color;

    TimelineMarker(String name, float positionSec) {
      this.name = name;
      this.positionSec = positionSec;
      this.color = Theme.getInstance().ACCENT_BLUE;
    }

    @Override
    public int compareTo(TimelineMarker o) {
      return Float.compare(positionSec, o.positionSec);
    }
  }

  final List<TimelineMarker> markers = new ArrayList<>();
  int draggingMarkerIdx = -1;

  final List<TrackTimeline> tracks = new ArrayList<>();
  private int selectedTrack = 0; // Currently selected track for plugin/clip operations
  private static TimelineView instance; // Static reference for global access
  final JScrollPane scrollPane;
  final JPanel contentPanel;
  private JPanel rowHeader; // Track labels panel (needs update on vZoom)
  private final Timer repaintTimer;
  boolean autoScroll = true; // Auto-scroll to follow playhead during playback
  int playheadScreenOffset = -1; // Screen X position to keep playhead at during auto-scroll

  // Renderer delegate
  private final TimelineRenderer renderer = new TimelineRenderer(this);

  // Drag interaction mode (replaces isDragging / creatingClip / resizingClip booleans)
  enum DragMode {
    NONE,
    MOVE_CLIP,
    CREATE_CLIP,
    RESIZE_CLIP,
    LOOP_EXTEND,
    TRIM_LEFT,
    DRAG_LOOP_REGION,
    DRAG_LOOP_MARKER,
    DRAG_MARKER
  }

  DragMode dragMode = DragMode.NONE;

  // Clip drag-and-drop state
  ClipRect draggingClip = null;
  int dragSourceTrack = -1;
  int dragStartX = 0;
  int dragStartY = 0;
  int dragCurrentY = 0; // Current Y position for rendering during cross-track drag
  float dragOriginalStartTime = 0;

  // Clip creation state (like Piano Roll note creation)
  int creatingTrackIdx = -1;
  float creatingStartTime = 0;
  ClipRect creatingClipRect = null;
  int creatingAutoLaneIdx = -1;

  // Clip resize state
  ClipRect resizeClip = null;
  int resizeTrackIdx = -1;
  float resizeOriginalDuration = 0;

  // Loop region drag state
  float loopDragStartSec = 0;
  boolean draggingLoopEnd = false; // true = dragging end marker, false = start

  // Mouse handler delegate
  private final TimelineMouseHandler mouseHandler = new TimelineMouseHandler(this);
  private final TimelineNotificationHandler notificationHandler =
      new TimelineNotificationHandler(this);

  /** Get the singleton TimelineView instance */
  public static TimelineView getInstance() {
    return instance;
  }

  public TimelineView() {
    instance = this; // Set the static reference
    Theme.getInstance().addListener(this);
    setLayout(new BorderLayout());
    setBackground(Theme.getInstance().BG_DARK);

    contentPanel =
        new JPanel() {
          @Override
          protected void paintComponent(Graphics g) {
            super.paintComponent(g);
            drawTimeline(g);
          }
        };
    contentPanel.setLayout(null);
    contentPanel.setBackground(Theme.getInstance().BG_DARK);

    // Create fixed row header for track labels
    rowHeader =
        new JPanel() {
          @Override
          protected void paintComponent(Graphics g) {
            super.paintComponent(g);
            drawTrackLabels(g);
          }

          @Override
          public Dimension getPreferredSize() {
            int scaleLabelWidth = Theme.getInstance().scale(trackLabelWidth);
            int scaleTimeRuler = Theme.getInstance().scale(TOTAL_RULER_HEIGHT);
            return new Dimension(
                scaleLabelWidth,
                scaleTimeRuler + Theme.getInstance().scale(getTotalTracksHeight()));
          }
        };
    rowHeader.setBackground(Theme.getInstance().BG_DARK);

    // Drag-to-resize the track header width (right edge) and per-track height
    // (bottom edge)
    rowHeader.addMouseListener(
        new MouseAdapter() {
          @Override
          public void mousePressed(MouseEvent e) {
            int scaleLabelWidth = Theme.getInstance().scale(trackLabelWidth);
            int scaleTimeRuler = Theme.getInstance().scale(TOTAL_RULER_HEIGHT);

            // Check bottom edge of each track for height resize
            if (e.getY() >= scaleTimeRuler) {
              int relY = e.getY() - scaleTimeRuler;
              for (int i = 0; i < tracks.size(); i++) {
                int trackBottom = Theme.getInstance().scale(getTrackY(i) + getTotalTrackHeight(i));
                if (relY >= trackBottom - 4 && relY <= trackBottom + 2) {
                  final int ti = i;
                  final int startY = e.getYOnScreen();
                  final int startH = getBaseTrackHeight(i);
                  javax.swing.event.MouseInputAdapter heightAdapter =
                      new javax.swing.event.MouseInputAdapter() {
                        @Override
                        public void mouseDragged(MouseEvent de) {
                          int dy = de.getYOnScreen() - startY;
                          int unscaledDy = (int) (dy / Theme.getInstance().getScaling());
                          tracks.get(ti).customHeight =
                              Math.max(50, Math.min(300, startH + unscaledDy));
                          rowHeader.revalidate();
                          rowHeader.repaint();
                          contentPanel.revalidate();
                          contentPanel.repaint();
                          updateContentSize();
                        }

                        @Override
                        public void mouseReleased(MouseEvent re) {
                          rowHeader.removeMouseMotionListener(this);
                          rowHeader.removeMouseListener(this);
                          rowHeader.setCursor(Cursor.getDefaultCursor());
                        }
                      };
                  rowHeader.addMouseMotionListener(heightAdapter);
                  rowHeader.addMouseListener(heightAdapter);
                  rowHeader.setCursor(Cursor.getPredefinedCursor(Cursor.S_RESIZE_CURSOR));
                  return;
                }
              }
            }

            // Right edge for width resize
            if (e.getX() >= scaleLabelWidth - 4) {
              final int startX = e.getXOnScreen();
              final int startWidth = trackLabelWidth;
              javax.swing.event.MouseInputAdapter resizeAdapter =
                  new javax.swing.event.MouseInputAdapter() {
                    @Override
                    public void mouseDragged(MouseEvent de) {
                      int dx = de.getXOnScreen() - startX;
                      int unscaledDx = (int) (dx / Theme.getInstance().getScaling());
                      trackLabelWidth = Math.max(100, Math.min(300, startWidth + unscaledDx));
                      rowHeader.revalidate();
                      rowHeader.repaint();
                      contentPanel.repaint();
                    }

                    @Override
                    public void mouseReleased(MouseEvent re) {
                      rowHeader.removeMouseMotionListener(this);
                      rowHeader.removeMouseListener(this);
                      rowHeader.setCursor(Cursor.getDefaultCursor());
                    }
                  };
              rowHeader.addMouseMotionListener(resizeAdapter);
              rowHeader.addMouseListener(resizeAdapter);
              rowHeader.setCursor(Cursor.getPredefinedCursor(Cursor.E_RESIZE_CURSOR));
            }
          }
        });
    rowHeader.addMouseMotionListener(
        new MouseAdapter() {
          @Override
          public void mouseMoved(MouseEvent e) {
            int scaleLabelWidth = Theme.getInstance().scale(trackLabelWidth);
            int scaleTimeRuler = Theme.getInstance().scale(TOTAL_RULER_HEIGHT);

            // Check bottom edge of each track
            if (e.getY() >= scaleTimeRuler) {
              int relY = e.getY() - scaleTimeRuler;
              for (int i = 0; i < tracks.size(); i++) {
                int trackBottom = Theme.getInstance().scale(getTrackY(i) + getTotalTrackHeight(i));
                if (relY >= trackBottom - 4 && relY <= trackBottom + 2) {
                  rowHeader.setCursor(Cursor.getPredefinedCursor(Cursor.S_RESIZE_CURSOR));
                  return;
                }
              }
            }

            if (e.getX() >= scaleLabelWidth - 4) {
              rowHeader.setCursor(Cursor.getPredefinedCursor(Cursor.E_RESIZE_CURSOR));
            } else {
              rowHeader.setCursor(Cursor.getDefaultCursor());
            }
          }
        });

    // Add mouse listener to rowHeader for track selection and rename
    rowHeader.addMouseListener(
        new MouseAdapter() {
          @Override
          public void mousePressed(MouseEvent e) {
            int scaleTimeRuler = Theme.getInstance().scale(TOTAL_RULER_HEIGHT);
            if (e.getY() >= scaleTimeRuler) {
              int trackIdx = getTrackIdxAtY(e.getY() - scaleTimeRuler);
              if (trackIdx >= 0 && trackIdx < tracks.size()) {
                // Right-click: show track header context menu
                if (SwingUtilities.isRightMouseButton(e)) {
                  showTrackHeaderContextMenu(trackIdx, e);
                  return;
                }
                TrackTimeline track = tracks.get(trackIdx);
                int scaleLabelWidth = Theme.getInstance().scale(trackLabelWidth);
                int trackTopY = Theme.getInstance().scale(getTrackY(trackIdx));
                int baseH = Theme.getInstance().scale(getBaseTrackHeight());
                int clickYInTrack = (e.getY() - scaleTimeRuler) - trackTopY;

                // Row 3 button positions (must match TimelineRenderer)
                int row3Y = 33; // relative to track top
                int btnH = 18;
                int armW = 30;
                int modeW = 34;
                int gap = 3;
                int inputW = scaleLabelWidth - armW - modeW - gap * 4;

                // Check if click is on the ARM button
                int armX = gap + inputW + gap + modeW + gap;
                if (e.getX() >= armX
                    && e.getX() <= armX + armW
                    && clickYInTrack >= row3Y
                    && clickYInTrack <= row3Y + btnH) {
                  track.recordArmed = !track.recordArmed;
                  BackendManager.getInstance().armTrack(trackIdx);
                  rowHeader.repaint();
                  contentPanel.repaint();
                  return;
                }

                // Check if click is on the MIDI/AUD toggle button
                int modeX = gap + inputW + gap;
                if (e.getX() >= modeX
                    && e.getX() <= modeX + modeW
                    && clickYInTrack >= row3Y
                    && clickYInTrack <= row3Y + btnH) {
                  track.midiRecordMode = !track.midiRecordMode;
                  BackendManager.getInstance().setRecordMode(trackIdx, track.midiRecordMode);
                  rowHeader.repaint();
                  return;
                }

                // Row 4: S (Solo) and M (Mute) buttons — after PAN knob
                int row4Y = 55;
                int knobD = 18;
                int panKnobX = scaleLabelWidth / 2 + 4;
                int smW = 16;
                int smBtnH = 16;
                int smGap = 3;
                int smY = row4Y + 1;
                // Approximate soloX position (matches renderer)
                int soloX = panKnobX + knobD + 20;
                if (e.getX() >= soloX
                    && e.getX() <= soloX + smW
                    && clickYInTrack >= smY
                    && clickYInTrack <= smY + smBtnH) {
                  track.soloed = !track.soloed;
                  BackendManager.getInstance().setTrackSolo(trackIdx, track.soloed);
                  rowHeader.repaint();
                  return;
                }

                // Check if click is on the M (Mute) button
                int muteX = soloX + smW + smGap;
                if (e.getX() >= muteX
                    && e.getX() <= muteX + smW
                    && clickYInTrack >= smY
                    && clickYInTrack <= smY + smBtnH) {
                  track.muted = !track.muted;
                  BackendManager.getInstance().setTrackMute(trackIdx, track.muted);
                  rowHeader.repaint();
                  return;
                }

                // Check if click is on a knob (VOL or PAN) — starts drag
                int volKnobX = 6;
                boolean onVolKnob =
                    e.getX() >= volKnobX
                        && e.getX() <= volKnobX + knobD
                        && clickYInTrack >= row4Y
                        && clickYInTrack <= row4Y + knobD;
                boolean onPanKnob =
                    e.getX() >= panKnobX
                        && e.getX() <= panKnobX + knobD
                        && clickYInTrack >= row4Y
                        && clickYInTrack <= row4Y + knobD;
                if (onVolKnob || onPanKnob) {
                  final boolean isDragVol = onVolKnob;
                  final int dragTrackIdx = trackIdx;
                  final int startY = e.getYOnScreen();
                  final float startVal = isDragVol ? track.volume : track.pan;
                  // For volume: convert to dB for the drag start reference
                  final float startDb =
                      isDragVol
                          ? (startVal <= 0.001f ? -60.0f : (float) (20.0 * Math.log10(startVal)))
                          : 0;
                  final TrackTimeline dragTrack = track;
                  javax.swing.event.MouseInputAdapter dragAdapter =
                      new javax.swing.event.MouseInputAdapter() {
                        @Override
                        public void mouseDragged(java.awt.event.MouseEvent de) {
                          int dy = startY - de.getYOnScreen(); // up = positive
                          if (isDragVol) {
                            // Drag in dB space: 0.5 dB per pixel
                            float newDb = startDb + dy * 0.5f;
                            newDb = Math.max(-60.0f, Math.min(6.0f, newDb));
                            float newVol =
                                newDb <= -60.0f ? 0.0f : (float) Math.pow(10, newDb / 20.0);
                            dragTrack.volume = Math.min(2.0f, newVol);
                            BackendManager.getInstance()
                                .setTrackVolume(dragTrackIdx, dragTrack.volume);
                          } else {
                            float newPan = Math.max(-1.0f, Math.min(1.0f, startVal + dy * 0.01f));
                            dragTrack.pan = newPan;
                            BackendManager.getInstance().setTrackPan(dragTrackIdx, newPan);
                          }
                          rowHeader.repaint();
                        }

                        @Override
                        public void mouseReleased(java.awt.event.MouseEvent re) {
                          rowHeader.removeMouseMotionListener(this);
                          rowHeader.removeMouseListener(this);
                        }
                      };
                  rowHeader.addMouseMotionListener(dragAdapter);
                  rowHeader.addMouseListener(dragAdapter);
                  return;
                }

                // Check if click is on the Input dropdown button
                if (e.getX() >= gap
                    && e.getX() <= gap + inputW
                    && clickYInTrack >= row3Y
                    && clickYInTrack <= row3Y + btnH) {
                  showInputChannelPopup(trackIdx, e);
                  return;
                }

                // Check if click is in the automation toggle area
                if (!track.automationLanes.isEmpty()
                    && clickYInTrack > baseH - 20
                    && clickYInTrack < baseH) {
                  track.automationExpanded = !track.automationExpanded;
                  updateContentSize();
                  rowHeader.revalidate();
                  rowHeader.repaint();
                  contentPanel.repaint();
                  return;
                }
                setSelectedTrack(trackIdx);
                rowHeader.repaint();
                contentPanel.repaint();
              }
            }
          }

          @Override
          public void mouseClicked(MouseEvent e) {
            if (e.getClickCount() == 2) {
              int scaleTimeRuler = Theme.getInstance().scale(TOTAL_RULER_HEIGHT);
              if (e.getY() >= scaleTimeRuler) {
                int trackIdx = getTrackIdxAtY(e.getY() - scaleTimeRuler);
                if (trackIdx >= 0 && trackIdx < tracks.size()) {
                  renameTrack(trackIdx);
                }
              }
            }
          }
        });

    scrollPane = new JScrollPane(contentPanel);
    scrollPane.setBorder(null);
    scrollPane.setRowHeaderView(rowHeader);
    scrollPane.getVerticalScrollBar().setUnitIncrement(16);
    add(scrollPane, BorderLayout.CENTER);

    // Initial tracks
    for (int i = 0; i < 4; i++) {
      tracks.add(new TrackTimeline(i));
    }
    updateContentSize();

    repaintTimer =
        new Timer(
            33,
            e -> {
              // Auto-scroll to follow playhead during playback
              if (isPlaying && autoScroll) {
                // No label offset - content panel starts at x=0
                int playheadX = (int) (playheadPos * getPixelsPerSecond());

                // Keep playhead at the same screen position where playback started
                int targetScrollX = playheadX - playheadScreenOffset;
                targetScrollX = Math.max(0, targetScrollX);
                scrollPane.getHorizontalScrollBar().setValue(targetScrollX);
              }
              contentPanel.repaint();
            });
    repaintTimer.start();

    BackendManager.getInstance().addNotificationListener(this::handleNotification);

    // Pre-populate audio + MIDI input device cache at startup
    BackendManager.getInstance().requestAudioInputs();
    BackendManager.getInstance().requestMidiInputs();
    // Retry after 1s in case backend wasn't ready yet
    javax.swing.Timer startupRetry =
        new javax.swing.Timer(
            1000,
            ev -> {
              BackendManager.getInstance().requestAudioInputs();
              BackendManager.getInstance().requestMidiInputs();
            });
    startupRetry.setRepeats(false);
    startupRetry.start();

    setupMouseListeners();
    setupDropTarget();
    setupControls();
  }

  private void setupControls() {
    ZoomControlPanel controlPanel =
        new ZoomControlPanel(
            GridMode.values(),
            gridMode,
            mode -> {
              gridMode = mode;
              repaint();
            },
            scale -> {
              hZoomScale = scale;
              updateContentSize();
              contentPanel.repaint();
            },
            5,
            400,
            100,
            val -> {
              vZoomScale = val / 100.0f;
              updateContentSize();
              if (rowHeader != null) {
                rowHeader.revalidate();
                rowHeader.repaint();
              }
              contentPanel.repaint();
            },
            5,
            200,
            100,
            auto -> autoScroll = auto,
            autoScroll);
    add(controlPanel, BorderLayout.SOUTH);
  }

  private void setupDropTarget() {
    if (java.awt.GraphicsEnvironment.isHeadless()) return;
    new java.awt.dnd.DropTarget(
        contentPanel,
        new java.awt.dnd.DropTargetAdapter() {
          @Override
          public void drop(java.awt.dnd.DropTargetDropEvent dtde) {
            try {
              if (dtde.isDataFlavorSupported(java.awt.datatransfer.DataFlavor.stringFlavor)) {
                dtde.acceptDrop(java.awt.dnd.DnDConstants.ACTION_COPY);
                String data =
                    (String)
                        dtde.getTransferable()
                            .getTransferData(java.awt.datatransfer.DataFlavor.stringFlavor);
                dtde.dropComplete(true);

                String[] parts = data.split(":", 2);
                if (parts.length == 2) {
                  String type = parts[0];
                  String path = parts[1];

                  Point p = dtde.getLocation();
                  int trackIndex =
                      getTrackIdxAtY(p.y - Theme.getInstance().scale(TOTAL_RULER_HEIGHT));
                  double timeSec = p.x / getPixelsPerSecond();

                  // Snap to nearest grid boundary
                  float secondsPerBeat = 60.0f / bpm;
                  float snapSeconds = getGridSnapSeconds(gridMode, secondsPerBeat);
                  if (snapSeconds > 0) {
                    timeSec = Math.round(timeSec / snapSeconds) * snapSeconds;
                  }

                  if (trackIndex >= 0 && trackIndex < tracks.size()) {
                    BackendManager.getInstance()
                        .addTimelineClip(trackIndex, path, (float) timeSec, 0);
                  }
                }
              } else {
                dtde.rejectDrop();
              }
            } catch (Exception e) {
              java.util.logging.Logger.getLogger(TimelineView.class.getName())
                  .log(java.util.logging.Level.WARNING, "Drop failed", e);
              dtde.rejectDrop();
            }
          }
        });

    // Alt+drag on audio clips → export for cross-panel DnD (e.g. to sampler)
    java.awt.dnd.DragSource ds = java.awt.dnd.DragSource.getDefaultDragSource();
    ds.createDefaultDragGestureRecognizer(
        contentPanel,
        java.awt.dnd.DnDConstants.ACTION_COPY,
        dge -> {
          if (!dge.getTriggerEvent().isAltDown()) return;
          Point p = dge.getDragOrigin();
          int trackIdx = getTrackIdxAtY(p.y - Theme.getInstance().scale(TOTAL_RULER_HEIGHT));
          if (trackIdx < 0 || trackIdx >= tracks.size()) return;
          ClipRect clip = findClipAtPosition(trackIdx, p.x);
          if (clip == null || clip.path == null || clip.path.isEmpty()) return;
          if (clip.isAutomation) return;
          dge.startDrag(
              java.awt.dnd.DragSource.DefaultCopyDrop,
              new java.awt.datatransfer.StringSelection("audio:" + clip.path));
        });
  }

  void updateContentSize() {
    // Calculate content duration from longest track content
    float maxEndTime = 60.0f; // Minimum 60 seconds default
    for (TrackTimeline track : tracks) {
      for (ClipRect clip : track.clips) {
        float endTime = clip.startTime + clip.duration;
        if (endTime > maxEndTime) {
          maxEndTime = endTime;
        }
      }
    }
    // Add some padding (20% more)
    int width = (int) (getPixelsPerSecond() * maxEndTime * 1.2f);
    int height =
        Theme.getInstance().scale(getTotalTracksHeight())
            + Theme.getInstance().scale(TOTAL_RULER_HEIGHT);
    contentPanel.setPreferredSize(new Dimension(width, height));
    contentPanel.revalidate();
  }

  private void setupMouseListeners() {
    mouseHandler.install();
  }

  /** Snap time to nearest grid boundary based on current grid mode and BPM */
  float snapToGrid(float time) {
    float secondsPerBeat = 60.0f / bpm;
    float snapInterval = getGridSnapSeconds(gridMode, secondsPerBeat);
    return Math.round(time / snapInterval) * snapInterval;
  }

  /** Get the currently selected track index for plugin/clip operations (0-based) */
  public int getSelectedTrack() {
    return selectedTrack; // 0-based to match internal track array and backend notifications
  }

  /** Set the selected track (for sync with SessionView) */
  public void setSelectedTrack(int trackIdx) {
    if (trackIdx >= 0 && trackIdx != selectedTrack) {
      selectedTrack = trackIdx;
      // Ensure track exists
      while (tracks.size() <= selectedTrack) {
        tracks.add(new TrackTimeline(tracks.size()));
      }
      // Sync with SessionView (0-based)
      if (SessionView.getInstance() != null) {
        SessionView.getInstance().selectTrackByIdx(trackIdx);
      }
      // Notify PluginPane about track selection change
      if (PluginPane.getInstance() != null) {
        PluginPane.getInstance().setSelectedTrack(trackIdx);
      }
      // Update virtual keyboard target track
      if (TopBar.getInstance() != null) {
        TopBar.getInstance().getVirtualKeyboard().setTargetTrackIndex(trackIdx);
      }
      repaint();
    }
  }

  /** Add a new track at the end, syncing with SessionView. */
  public void addTrack() {
    addTrackNoSync();
    if (SessionView.getInstance() != null
        && SessionView.getInstance().getTrackCount() < tracks.size()) {
      SessionView.getInstance().addTrackNoSync();
    }
  }

  /** Add a new track locally without syncing to SessionView. */
  void addTrackNoSync() {
    int newIdx = tracks.size();
    tracks.add(new TrackTimeline(newIdx));
    updateContentSize();
    if (rowHeader != null) {
      rowHeader.revalidate();
      rowHeader.repaint();
    }
    contentPanel.repaint();
  }

  /** Remove a track by index, syncing with SessionView. */
  public void removeTrack(int trackIdx) {
    if (trackIdx < 0 || trackIdx >= tracks.size() || getVisibleTrackCount() <= 1) return;
    if (tracks.get(trackIdx).hidden) return;
    removeTrackNoSync(trackIdx);
    if (SessionView.getInstance() != null && trackIdx < SessionView.getInstance().getTrackCount()) {
      SessionView.getInstance().removeTrackNoSync(trackIdx);
    }
  }

  /** Remove a track locally without syncing to SessionView. */
  void removeTrackNoSync(int trackIdx) {
    if (trackIdx < 0 || trackIdx >= tracks.size() || getVisibleTrackCount() <= 1) return;
    if (tracks.get(trackIdx).hidden) return;
    tracks.get(trackIdx).hidden = true;
    // Adjust selected track if needed — pick next visible
    if (selectedTrack == trackIdx || tracks.get(selectedTrack).hidden) {
      for (int i = 0; i < tracks.size(); i++) {
        if (!tracks.get(i).hidden) {
          selectedTrack = i;
          break;
        }
      }
    }
    updateContentSize();
    if (rowHeader != null) {
      rowHeader.revalidate();
      rowHeader.repaint();
    }
    contentPanel.repaint();
  }

  /** Show dialog to rename a track */
  private void renameTrack(int trackIdx) {
    if (trackIdx < 0 || trackIdx >= tracks.size()) return;
    TrackTimeline track = tracks.get(trackIdx);
    String currentName = track.customName != null ? track.customName : "Track " + trackIdx;
    String newName = JOptionPane.showInputDialog(this, "Enter track name:", currentName);
    if (newName != null) {
      track.customName = newName.isEmpty() ? null : newName;
      repaint();
    }
  }

  /** Find clip at the given x position in the specified track */
  ClipRect findClipAtPosition(int trackIdx, int x) {
    if (trackIdx < 0 || trackIdx >= tracks.size()) return null;
    TrackTimeline track = tracks.get(trackIdx);
    float clickTime = x / getPixelsPerSecond();
    for (ClipRect clip : track.clips) {
      if (clickTime >= clip.startTime && clickTime <= clip.startTime + clip.duration) {
        return clip;
      }
    }
    return null;
  }

  /** Check if x position is near the right edge of a clip (bottom half only — top half is loop) */
  boolean isNearRightEdge(ClipRect clip, int x, int mouseY, int clipY, int clipH) {
    int rightEdgeX = (int) ((clip.startTime + clip.duration) * getPixelsPerSecond());
    boolean nearX = Math.abs(x - rightEdgeX) <= TimelineConstants.RESIZE_EDGE_PX;
    // Bottom half only for trim/pad
    boolean bottomHalf = (mouseY - clipY) > clipH / 2;
    return nearX && bottomHalf;
  }

  /** Check if position is near the top-right corner of a clip (loop extend handle) */
  boolean isNearTopRightCorner(ClipRect clip, int x, int mouseY, int clipY, int clipH) {
    int rightEdgeX = (int) ((clip.startTime + clip.duration) * getPixelsPerSecond());
    boolean nearX = Math.abs(x - rightEdgeX) <= TimelineConstants.RESIZE_EDGE_PX;
    boolean topHalf = (mouseY - clipY) <= clipH / 2;
    return nearX && topHalf;
  }

  /** Legacy: check near right edge without Y (used by callers that don't have Y context) */
  boolean isNearRightEdge(ClipRect clip, int x) {
    int rightEdgeX = (int) ((clip.startTime + clip.duration) * getPixelsPerSecond());
    return Math.abs(x - rightEdgeX) <= TimelineConstants.RESIZE_EDGE_PX;
  }

  /** Check if x position is near the left edge of a clip */
  boolean isNearLeftEdge(ClipRect clip, int x) {
    int leftEdgeX = (int) (clip.startTime * getPixelsPerSecond());
    return Math.abs(x - leftEdgeX) <= TimelineConstants.RESIZE_EDGE_PX;
  }

  /** Show context menu for a timeline clip */
  void showClipContextMenu(int trackIdx, ClipRect clip, int x, int y) {
    JPopupMenu menu = new JPopupMenu();

    // Edit Clip (MIDI only)
    JMenuItem editItem = new JMenuItem("Edit Clip...");
    editItem.addActionListener(
        e -> {
          boolean isMidi =
              clip.path == null
                  || clip.path.isEmpty()
                  || clip.path.endsWith(".mid"); // Empty clips are treated as MIDI
          if (isMidi) {
            File file =
                (clip.path != null && !clip.path.isEmpty())
                    ? new File(clip.path)
                    : new File("New Clip.mid");
            JFrame ownerFrame = (JFrame) SwingUtilities.getWindowAncestor(this);
            // Find clip index in track's timeline clips
            TrackTimeline trackTimeline = tracks.get(trackIdx);
            int clipIndex = -1;
            for (int i = 0; i < trackTimeline.clips.size(); i++) {
              if (trackTimeline.clips.get(i) == clip) {
                clipIndex = i;
                break;
              }
            }
            // Use 6-arg constructor: slotIdx=-1 for timeline clips, clipIdx=actual index,
            // clipStartTime=clip.startTime
            PianoRoll pr = new PianoRoll(ownerFrame, file, trackIdx, -1, clipIndex, clip.startTime);
            pr.setVisible(true);
          } else {
            JOptionPane.showMessageDialog(
                this, "Can only edit MIDI (.mid) clips.", "Error", JOptionPane.ERROR_MESSAGE);
          }
        });
    menu.add(editItem);

    // Delete Clip
    menu.addSeparator();
    JMenuItem deleteItem = new JMenuItem("Delete Clip");
    deleteItem.addActionListener(
        e -> {
          TrackTimeline track = tracks.get(trackIdx);
          int clipIdx = track.clips.indexOf(clip);
          if (clipIdx >= 0) {
            // Notify engine to remove from playback state
            hibiki.BackendManager.getInstance().removeTimelineClip(trackIdx, clipIdx);
            // Remove from GUI
            track.clips.remove(clipIdx);
            track.clipMap.clear();
            for (int i = 0; i < track.clips.size(); i++) {
              track.clipMap.put(i, track.clips.get(i));
            }
            updateContentSize();
            repaint();
          }
        });
    menu.add(deleteItem);

    menu.show(contentPanel, x, y);
  }

  /** Show context menu for empty track area */
  void showEmptyAreaContextMenu(int trackIdx, float clickTime, int x, int y) {
    JPopupMenu menu = new JPopupMenu();

    // Create New Clip
    JMenuItem createItem = new JMenuItem("Create New Clip");
    createItem.addActionListener(
        e -> {
          float snapTime = snapToGrid(clickTime);
          BackendManager.getInstance().addTimelineClip(trackIdx, "", snapTime, 0);
        });
    menu.add(createItem);

    // Add Automation Lane — use last touched param if available
    PluginPane.LastTouchedParam ltp = PluginPane.getLastTouchedParam();
    if (ltp != null && ltp.trackIndex == trackIdx) {
      menu.addSeparator();
      JMenuItem autoItem = new JMenuItem("Create Automation: " + ltp.paramName);
      autoItem.addActionListener(
          e -> {
            BackendManager.getInstance()
                .sendRequest(
                    Request.newBuilder()
                        .setAutomation(
                            AutomationCmd.newBuilder()
                                .setAction(AutomationCmd.Action.ACTION_ADD_LANE)
                                .setTarget(
                                    EntityRef.newBuilder()
                                        .setTrackIndex(trackIdx)
                                        .setPluginIndex(ltp.pluginIndex))
                                .setParamId((int) ltp.paramId))
                        .build());
          });
      menu.add(autoItem);
    } else if (trackIdx >= 0
        && trackIdx < tracks.size()
        && tracks.get(trackIdx).pluginName != null) {
      menu.addSeparator();
      JMenuItem autoItem = new JMenuItem("Add Automation Lane...");
      autoItem.addActionListener(e -> showAddAutomationDialog(trackIdx));
      menu.add(autoItem);
    }

    menu.show(contentPanel, x, y);
  }

  /** Show context menu when right-clicking a track header */
  private void showTrackHeaderContextMenu(int trackIdx, MouseEvent e) {
    JPopupMenu menu = new JPopupMenu();
    TrackTimeline track = tracks.get(trackIdx);

    // Rename track
    JMenuItem renameItem = new JMenuItem("Rename Track");
    renameItem.addActionListener(ev -> renameTrack(trackIdx));
    menu.add(renameItem);

    menu.addSeparator();

    // Record arm toggle
    JMenuItem armItem =
        new JMenuItem(track.recordArmed ? "✓ Disarm Recording" : "Arm for Recording");
    armItem.addActionListener(
        ev -> {
          track.recordArmed = !track.recordArmed;
          BackendManager.getInstance().armTrack(trackIdx);
          rowHeader.repaint();
          contentPanel.repaint();
        });
    menu.add(armItem);

    // Set Input Device
    JMenuItem inputItem = new JMenuItem("Set Input Device...");
    inputItem.addActionListener(ev -> showInputDeviceDialog(trackIdx));
    menu.add(inputItem);

    // Input Channel submenu
    JMenu chMenu = new JMenu("Input Channel");
    JRadioButtonMenuItem stereoItem = new JRadioButtonMenuItem("Stereo", track.inputStereo);
    JRadioButtonMenuItem monoItem = new JRadioButtonMenuItem("Mono", !track.inputStereo);
    ButtonGroup chGroup = new ButtonGroup();
    chGroup.add(stereoItem);
    chGroup.add(monoItem);
    stereoItem.addActionListener(
        ev -> {
          track.inputStereo = true;
          BackendManager.getInstance()
              .setInputDevice(trackIdx, track.inputDeviceId, track.inputChannelStart, true);
          rowHeader.repaint();
        });
    monoItem.addActionListener(
        ev -> {
          track.inputStereo = false;
          BackendManager.getInstance()
              .setInputDevice(trackIdx, track.inputDeviceId, track.inputChannelStart, false);
          rowHeader.repaint();
        });
    chMenu.add(stereoItem);
    chMenu.add(monoItem);
    chMenu.addSeparator();
    // Channel offset options (1-8)
    JMenu offsetMenu = new JMenu("Start Channel");
    for (int ch = 0; ch < 8; ch++) {
      final int chStart = ch;
      String label = track.inputStereo ? "Ch " + (ch + 1) + "-" + (ch + 2) : "Ch " + (ch + 1);
      JRadioButtonMenuItem chItem = new JRadioButtonMenuItem(label, ch == track.inputChannelStart);
      chItem.addActionListener(
          ev -> {
            track.inputChannelStart = chStart;
            BackendManager.getInstance()
                .setInputDevice(trackIdx, track.inputDeviceId, chStart, track.inputStereo);
            rowHeader.repaint();
          });
      offsetMenu.add(chItem);
    }
    chMenu.add(offsetMenu);
    menu.add(chMenu);

    menu.addSeparator();

    // Add Automation Lane (using last touched param if available)
    PluginPane.LastTouchedParam ltp = PluginPane.getLastTouchedParam();
    if (ltp != null && ltp.trackIndex == trackIdx) {
      JMenuItem autoItem = new JMenuItem("Add Automation: " + ltp.paramName);
      autoItem.addActionListener(
          ev -> {
            BackendManager.getInstance()
                .sendRequest(
                    Request.newBuilder()
                        .setAutomation(
                            AutomationCmd.newBuilder()
                                .setAction(AutomationCmd.Action.ACTION_ADD_LANE)
                                .setTarget(
                                    EntityRef.newBuilder()
                                        .setTrackIndex(trackIdx)
                                        .setPluginIndex(ltp.pluginIndex))
                                .setParamId((int) ltp.paramId))
                        .build());
          });
      menu.add(autoItem);
    }
    if (track.pluginName != null) {
      JMenuItem addAutoItem = new JMenuItem("Add Automation Lane...");
      addAutoItem.addActionListener(ev -> showAddAutomationDialog(trackIdx));
      menu.add(addAutoItem);
    }

    // Remove existing automation lanes
    if (!track.automationLanes.isEmpty()) {
      JMenu removeMenu = new JMenu("Remove Automation Lane");
      for (int j = 0; j < track.automationLanes.size(); j++) {
        AutomationLaneData lane = track.automationLanes.get(j);
        final int laneIdx = j;
        JMenuItem removeItem = new JMenuItem(lane.paramName);
        removeItem.addActionListener(
            ev -> {
              BackendManager.getInstance()
                  .sendRequest(
                      Request.newBuilder()
                          .setAutomation(
                              AutomationCmd.newBuilder()
                                  .setAction(AutomationCmd.Action.ACTION_REMOVE_LANE)
                                  .setTarget(
                                      EntityRef.newBuilder()
                                          .setTrackIndex(trackIdx)
                                          .setLaneIndex(laneIdx)))
                          .build());
            });
        removeMenu.add(removeItem);
      }
      menu.add(removeMenu);
    }

    menu.addSeparator();

    // Add / Delete track
    JMenuItem addTrackItem = new JMenuItem("Add Track");
    addTrackItem.addActionListener(ev -> addTrack());
    menu.add(addTrackItem);

    JMenuItem deleteTrackItem = new JMenuItem("Delete Track");
    deleteTrackItem.setEnabled(tracks.size() > 1);
    deleteTrackItem.addActionListener(ev -> removeTrack(trackIdx));
    menu.add(deleteTrackItem);

    menu.show(rowHeader, e.getX(), e.getY());
  }

  /** Show dialog for selecting audio input device and channel configuration */
  /** Show popup from the input dropdown button in the track header */
  private void showInputChannelPopup(int trackIdx, MouseEvent e) {
    TrackTimeline track = tracks.get(trackIdx);
    JPopupMenu popup = new JPopupMenu();

    // Input Device submenu
    JMenu deviceMenu = new JMenu("Input Device");
    var devices = TimelineNotificationHandler.cachedInputDevices;
    if (devices.isEmpty()) {
      JMenuItem noDevices = new JMenuItem("(no devices — refresh in Settings)");
      noDevices.setEnabled(false);
      deviceMenu.add(noDevices);
    } else {
      for (int i = 0; i < devices.size(); i++) {
        var dev = devices.get(i);
        String label = dev.getName() + " (" + dev.getChannelCount() + " ch)";
        JRadioButtonMenuItem item =
            new JRadioButtonMenuItem(label, dev.getId().equals(track.inputDeviceId));
        final String devId = dev.getId();
        item.addActionListener(
            ev -> {
              track.inputDeviceId = devId;
              BackendManager.getInstance()
                  .setInputDevice(trackIdx, devId, track.inputChannelStart, track.inputStereo);
              rowHeader.repaint();
            });
        deviceMenu.add(item);
      }
    }
    popup.add(deviceMenu);
    popup.addSeparator();

    // MIDI Input Device submenu
    JMenu midiMenu = new JMenu("MIDI Input");
    var midiDevices = TimelineNotificationHandler.cachedMidiDevices;
    if (midiDevices.isEmpty()) {
      JMenuItem noMidi = new JMenuItem("(no MIDI devices found)");
      noMidi.setEnabled(false);
      midiMenu.add(noMidi);
    } else {
      for (int i = 0; i < midiDevices.size(); i++) {
        var mdev = midiDevices.get(i);
        String mlabel = mdev.getName();
        JRadioButtonMenuItem mitem =
            new JRadioButtonMenuItem(mlabel, mdev.getId().equals(track.midiInputDeviceId));
        final String mdevId = mdev.getId();
        mitem.addActionListener(
            ev -> {
              track.midiInputDeviceId = mdevId;
              BackendManager.getInstance().setMidiInput(trackIdx, mdevId);
              rowHeader.repaint();
            });
        midiMenu.add(mitem);
      }
    }
    popup.add(midiMenu);
    popup.addSeparator();

    // Stereo / Mono toggle
    JRadioButtonMenuItem stereoItem = new JRadioButtonMenuItem("Stereo", track.inputStereo);
    JRadioButtonMenuItem monoItem = new JRadioButtonMenuItem("Mono", !track.inputStereo);
    ButtonGroup chGroup = new ButtonGroup();
    chGroup.add(stereoItem);
    chGroup.add(monoItem);
    stereoItem.addActionListener(
        ev -> {
          track.inputStereo = true;
          BackendManager.getInstance()
              .setInputDevice(trackIdx, track.inputDeviceId, track.inputChannelStart, true);
          rowHeader.repaint();
        });
    monoItem.addActionListener(
        ev -> {
          track.inputStereo = false;
          BackendManager.getInstance()
              .setInputDevice(trackIdx, track.inputDeviceId, track.inputChannelStart, false);
          rowHeader.repaint();
        });
    popup.add(stereoItem);
    popup.add(monoItem);
    popup.addSeparator();

    // Channel offset (1-8)
    for (int ch = 0; ch < 8; ch++) {
      final int chStart = ch;
      String label = track.inputStereo ? "Ch " + (ch + 1) + "-" + (ch + 2) : "Ch " + (ch + 1);
      JRadioButtonMenuItem chItem = new JRadioButtonMenuItem(label, ch == track.inputChannelStart);
      chItem.addActionListener(
          ev -> {
            track.inputChannelStart = chStart;
            BackendManager.getInstance()
                .setInputDevice(trackIdx, track.inputDeviceId, chStart, track.inputStereo);
            rowHeader.repaint();
          });
      popup.add(chItem);
    }

    popup.show(e.getComponent(), e.getX(), e.getY());
  }

  private void showInputDeviceDialog(int trackIdx) {
    // Request fresh device list
    BackendManager.getInstance().requestAudioInputs();

    // Use cached list (will be populated from notification)
    var devices = TimelineNotificationHandler.cachedInputDevices;
    if (devices.isEmpty()) {
      JOptionPane.showMessageDialog(
          this,
          "No audio input devices found.\nTry again in a moment after the device list loads.",
          "Input Devices",
          JOptionPane.INFORMATION_MESSAGE);
      return;
    }

    String[] deviceNames = new String[devices.size()];
    for (int i = 0; i < devices.size(); i++) {
      var dev = devices.get(i);
      deviceNames[i] = dev.getName() + " (" + dev.getChannelCount() + " ch)";
    }

    String selected =
        (String)
            JOptionPane.showInputDialog(
                this,
                "Select audio input device:",
                "Input Device",
                JOptionPane.PLAIN_MESSAGE,
                null,
                deviceNames,
                deviceNames.length > 0 ? deviceNames[0] : null);

    if (selected != null) {
      for (int i = 0; i < deviceNames.length; i++) {
        if (deviceNames[i].equals(selected)) {
          var dev = devices.get(i);
          TrackTimeline track = tracks.get(trackIdx);
          track.inputDeviceId = dev.getId();
          BackendManager.getInstance()
              .setInputDevice(trackIdx, dev.getId(), track.inputChannelStart, track.inputStereo);
          break;
        }
      }
    }
  }

  /** Show dialog to pick a parameter for automation (fallback when no param was touched) */
  private void showAddAutomationDialog(int trackIdx) {
    String input =
        JOptionPane.showInputDialog(
            this,
            "Adjust a plugin parameter first, then right-click.\n"
                + "Or enter plugin_index,param_id manually (e.g. 0,42):",
            "Add Automation Lane",
            JOptionPane.PLAIN_MESSAGE);
    if (input != null && input.contains(",")) {
      String[] parts = input.split(",");
      try {
        int pluginIdx = Integer.parseInt(parts[0].trim());
        int paramId = Integer.parseInt(parts[1].trim());
        BackendManager.getInstance()
            .sendRequest(
                Request.newBuilder()
                    .setAutomation(
                        AutomationCmd.newBuilder()
                            .setAction(AutomationCmd.Action.ACTION_ADD_LANE)
                            .setTarget(
                                EntityRef.newBuilder()
                                    .setTrackIndex(trackIdx)
                                    .setPluginIndex(pluginIdx))
                            .setParamId(paramId))
                    .build());
      } catch (NumberFormatException ex) {
        JOptionPane.showMessageDialog(this, "Invalid input format.");
      }
    }
  }

  void updatePlayhead(int x) {
    // Content panel starts at x=0, no label offset needed
    float timelineX = Math.max(0, x);
    playheadPos = timelineX / getPixelsPerSecond();
    BackendManager.getInstance().seek(playheadPos);
    repaint();
  }

  public void handleNotification(Notification n) {
    notificationHandler.handleNotification(n);
  }

  void repaintRowHeader() {
    if (rowHeader != null) rowHeader.repaint();
  }

  private void drawTrackLabels(Graphics g) {
    renderer.drawTrackLabels(
        g, tracks, selectedTrack, getTrackHeight(), TOTAL_RULER_HEIGHT, trackLabelWidth);
  }

  private void drawTimeline(Graphics g) {
    renderer.drawTimeline(
        g,
        contentPanel,
        tracks,
        selectedTrack,
        bpm,
        gridMode,
        playheadPos,
        dragMode,
        draggingClip,
        dragSourceTrack,
        dragOriginalStartTime,
        dragCurrentY,
        creatingTrackIdx,
        creatingClipRect,
        creatingAutoLaneIdx,
        getTrackHeight(),
        TOTAL_RULER_HEIGHT,
        loopEnabled,
        loopStartSec,
        loopEndSec);
  }

  @Override
  public void onThemeChanged() {
    SwingUtilities.invokeLater(
        () -> {
          setBackground(Theme.getInstance().BG_DARK);
          contentPanel.setBackground(Theme.getInstance().BG_DARK);
          updateContentSize();
          repaint();
        });
  }

  static class TrackTimeline {
    int index;
    List<ClipRect> clips = new ArrayList<>();
    Map<Integer, ClipRect> clipMap = new HashMap<>();
    String pluginName = null;
    boolean isInstrument = false;
    String customName = null; // User-defined track name
    List<AutomationLaneData> automationLanes = new ArrayList<>();
    boolean automationExpanded = true; // Whether automation sub-rows are visible
    boolean recordArmed = false; // Whether track is armed for recording
    boolean midiRecordMode = true; // true = MIDI recording, false = audio recording
    boolean hidden = false; // Whether track is hidden (deleted from GUI but index preserved)
    float volume = 0.31623f; // linear gain; default = -10 dB
    float pan = 0.0f; // -1.0 (left) to 1.0 (right)
    boolean muted = false;
    boolean soloed = false;
    int customHeight = 0; // 0 = use global default
    float peakL = 0f; // current peak level for meter
    float peakR = 0f;
    String inputDeviceId = ""; // Selected input device ID
    int inputChannelStart = 0; // Starting input channel
    boolean inputStereo = true; // Mono vs stereo input
    String midiInputDeviceId = "__global__"; // MIDI input device (default: global)

    TrackTimeline(int index) {
      this.index = index;
    }

    String getDisplayName() {
      if (customName != null && !customName.isEmpty()) {
        return customName;
      }
      return "Track " + index;
    }

    void addOrUpdateClip(TimelineClipInfo info) {
      int cidx = info.getClipIndex();
      ClipRect cr = clipMap.get(cidx);
      if (cr == null) {
        cr = new ClipRect();
        clips.add(cr);
        clipMap.put(cidx, cr);
      }
      cr.name = info.getName();
      cr.path = info.getPath();
      cr.startTime = info.getStartTime();
      cr.duration = info.getDuration();
      // Set content duration on first meaningful duration (original clip length)
      if (cr.contentDuration <= 0 && cr.duration > 0) {
        cr.contentDuration = cr.duration;
      }
      cr.isLooped = info.getIsLooped();
      cr.isAlias = info.getAliasSource() >= 0;
      cr.aliasSourceIndex = info.getAliasSource();
      // Update loopInterval from engine notification (loop_interval is in seconds)
      if (info.getLoopInterval() > 0) {
        cr.loopInterval = info.getLoopInterval();
      }
      // Extract waveform data
      int wfLen = info.getWaveformCount();
      if (wfLen > 0) {
        cr.waveform = new float[wfLen];
        for (int i = 0; i < wfLen; i++) {
          cr.waveform[i] = info.getWaveform(i);
        }
      }
    }
  }

  /** Data for one automation lane on a track */
  static class AutomationLaneData {
    int laneIndex;
    int pluginIndex;
    long paramId;
    String paramName = "Parameter";
    List<ClipRect> clips = new ArrayList<>();
  }

  static class ClipRect {
    String name;
    String path;
    float startTime;
    float duration; // Visible window in seconds (changes with trim)
    float contentDuration; // Original content duration in seconds (set once on first load)
    float loopInterval; // Loop repeat duration in seconds (set during loop extend)
    float trimStartSec; // Head-trim offset in seconds
    float[] waveform;
    boolean isAutomation = false;
    boolean isLooped = false; // Clip content repeats (loop-extended)
    boolean isAlias = false; // Clip is an alias (shallow copy)
    int aliasSourceIndex = -1; // Source clip index for aliases
    List<AutomationEditor.AutoPoint> automationPoints = new ArrayList<>();
  }

  /**
   * Get the snap interval in seconds for the given grid mode. For AUTO mode, adapts based on pixel
   * threshold.
   */
  float getGridSnapSeconds(GridMode mode, float secondsPerBeat) {
    if (mode == GridMode.AUTO) {
      return GridMode.autoSecondsInterval(secondsPerBeat, getPixelsPerSecond(), 15);
    }
    return mode.getSecondsInterval(secondsPerBeat);
  }
}
