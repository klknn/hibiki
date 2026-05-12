package hibiki.ui;

import hibiki.BackendManager;
import hibiki.pb.commands.*;
import hibiki.pb.core.*;
import hibiki.pb.notifications.*;
import hibiki.pb.notifications.Notification;
import java.awt.*;
import java.awt.event.*;
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
    // Collapsed children take zero height
    if (trackIdx >= 0 && trackIdx < tracks.size()) {
      TrackTimeline t = tracks.get(trackIdx);
      if (t.groupParentIndex >= 0
          && t.groupParentIndex < tracks.size()
          && tracks.get(t.groupParentIndex).collapsed) {
        return 0;
      }
    }
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

  /**
   * Find the ArrayList position of the track with the given engine index. Returns the engine index
   * itself if no track with that index is found (fallback for tracks not yet reordered).
   */
  int findDisplayPosition(int engineIndex) {
    for (int i = 0; i < tracks.size(); i++) {
      if (tracks.get(i).index == engineIndex) return i;
    }
    return engineIndex; // fallback: assume position == engine index (no reorder happened)
  }

  /**
   * Reorder tracks locally in the UI by moving a track from fromIdx to toIdx. This is purely a
   * display operation — track.index (engine slot ID) is NOT changed. Only the ArrayList position
   * and display-side groupParentIndex references are updated.
   */
  void reorderTrackLocally(int fromIdx, int toIdx) {
    if (fromIdx < 0 || fromIdx >= tracks.size()) return;
    toIdx = Math.max(0, Math.min(toIdx, tracks.size() - 1));
    if (fromIdx == toIdx) return;

    // Remove and re-insert the TrackTimeline object
    TrackTimeline moving = tracks.remove(fromIdx);
    int insertAt = Math.min(toIdx, tracks.size());
    tracks.add(insertAt, moving);

    // Update all existing groupParentIndex values to track the position shift.
    // Old position → new position mapping:
    //   fromIdx < insertAt: pos in (fromIdx, insertAt] shifts down by 1
    //   fromIdx > insertAt: pos in [insertAt, fromIdx) shifts up by 1
    //   fromIdx itself → insertAt
    for (TrackTimeline t : tracks) {
      int gpi = t.groupParentIndex;
      if (gpi < 0) continue;
      if (fromIdx < insertAt) {
        if (gpi == fromIdx) t.groupParentIndex = insertAt;
        else if (gpi > fromIdx && gpi <= insertAt) t.groupParentIndex = gpi - 1;
      } else {
        if (gpi == fromIdx) t.groupParentIndex = insertAt;
        else if (gpi >= insertAt && gpi < fromIdx) t.groupParentIndex = gpi + 1;
      }
    }

    // Update selected track position
    if (selectedTrack == fromIdx) {
      selectedTrack = insertAt;
    } else if (fromIdx < toIdx) {
      if (selectedTrack > fromIdx && selectedTrack <= toIdx) selectedTrack--;
    } else {
      if (selectedTrack >= toIdx && selectedTrack < fromIdx) selectedTrack++;
    }

    updateContentSize();
    repaint();
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
    DRAG_MARKER,
    FADE_IN_DRAG,
    FADE_OUT_DRAG
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

  // Fade marker drag state
  ClipRect fadeDragClip = null;
  int fadeDragTrackIdx = -1;

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
                TrackTimeline clickedTrack = tracks.get(trackIdx);
                int trackTopYDbg = Theme.getInstance().scale(getTrackY(trackIdx));
                int clickYInTrackDbg = (e.getY() - scaleTimeRuler) - trackTopYDbg;
                // Right-click: show track header context menu
                if (SwingUtilities.isRightMouseButton(e)) {
                  showTrackHeaderContextMenu(trackIdx, e);
                  return;
                }

                // Check if click is on ▶/▼ toggle for group tracks (left 16px area)
                if (clickedTrack.isGroupTrack() && e.getX() < 16) {
                  clickedTrack.collapsed = !clickedTrack.collapsed;
                  rowHeader.revalidate();
                  rowHeader.repaint();
                  updateContentSize();
                  contentPanel.repaint();
                  // Also toggle SessionView child strip visibility
                  if (SessionView.getInstance() != null) {
                    for (int ci = 0; ci < tracks.size(); ci++) {
                      if (tracks.get(ci).groupParentIndex == trackIdx) {
                        SessionView.getInstance().setTrackVisible(ci, !clickedTrack.collapsed);
                      }
                    }
                  }
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
                // Start DnD-aware track selection (drag to reorder/group)
                final int dndSourceTrack = trackIdx;
                final int dndStartY = e.getYOnScreen();
                setSelectedTrack(trackIdx);
                rowHeader.repaint();
                contentPanel.repaint();

                // Install drag adapter for reorder/group
                java.awt.event.MouseAdapter dndAdapter =
                    new java.awt.event.MouseAdapter() {
                      boolean dragging = false;

                      @Override
                      public void mouseDragged(java.awt.event.MouseEvent de) {
                        int dy = Math.abs(de.getYOnScreen() - dndStartY);
                        if (dy > 5 && !dragging) {
                          dragging = true;
                          rowHeader.setCursor(Cursor.getPredefinedCursor(Cursor.MOVE_CURSOR));
                        }
                      }

                      @Override
                      public void mouseReleased(java.awt.event.MouseEvent re) {
                        rowHeader.removeMouseMotionListener(this);
                        rowHeader.removeMouseListener(this);
                        rowHeader.setCursor(Cursor.getDefaultCursor());
                        if (!dragging) {
                          return;
                        }

                        // Compute insertion position from Y
                        int scaleRuler = Theme.getInstance().scale(TOTAL_RULER_HEIGHT);
                        int relY = re.getY() - scaleRuler;

                        // Find which track the mouse is over AND whether in top/bottom half
                        int cumY = 0;
                        int insertPos = -1;
                        int hoverTrack = -1;
                        for (int ti = 0; ti < tracks.size(); ti++) {
                          if (tracks.get(ti).hidden) continue;
                          int th = Theme.getInstance().scale(getTotalTrackHeight(ti));
                          if (th == 0) continue;
                          if (relY < cumY + th) {
                            hoverTrack = ti;
                            // Top half → insert before this track
                            // Bottom half → insert after this track
                            if (relY < cumY + th / 2) {
                              insertPos = ti;
                            } else {
                              insertPos = ti + 1;
                            }
                            break;
                          }
                          cumY += th;
                        }
                        if (insertPos < 0) {
                          // Dropped below all tracks → insert at end
                          insertPos = tracks.size();
                        }
                        if (hoverTrack < 0) hoverTrack = tracks.size() - 1;

                        // Check if dropping onto a group track (hovering over it)
                        if (hoverTrack >= 0
                            && hoverTrack < tracks.size()
                            && tracks.get(hoverTrack).isGroupTrack()
                            && hoverTrack != dndSourceTrack) {
                          // Save references and engine indices BEFORE reorder
                          int srcEngineIdx = tracks.get(dndSourceTrack).index;
                          int grpEngineIdx = tracks.get(hoverTrack).index;
                          TrackTimeline groupRef = tracks.get(hoverTrack);
                          TrackTimeline childRef = tracks.get(dndSourceTrack);
                          int destIdx = dndSourceTrack < hoverTrack ? hoverTrack : hoverTrack + 1;
                          if (destIdx != dndSourceTrack) {
                            reorderTrackLocally(dndSourceTrack, destIdx);
                          }
                          // Set groupParentIndex AFTER reorder using group's new position
                          childRef.groupParentIndex = tracks.indexOf(groupRef);
                          // Notify engine for audio routing (stable engine indices)
                          BackendManager.getInstance().setGroupParent(srcEngineIdx, grpEngineIdx);
                          updateContentSize();
                          repaint();
                        } else if (insertPos != dndSourceTrack && insertPos != dndSourceTrack + 1) {
                          // Normal reorder (UI-only, engine indices unchanged)
                          int targetPos = insertPos > dndSourceTrack ? insertPos - 1 : insertPos;
                          reorderTrackLocally(dndSourceTrack, targetPos);
                        }
                      }
                    };
                rowHeader.addMouseMotionListener(dndAdapter);
                rowHeader.addMouseListener(dndAdapter);
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
  void renameTrack(int trackIdx) {
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
    TimelineContextMenu.showClipContextMenu(this, trackIdx, clip, x, y);
  }

  /** Show context menu for empty track area */
  void showEmptyAreaContextMenu(int trackIdx, float clickTime, int x, int y) {
    TimelineContextMenu.showEmptyAreaContextMenu(this, trackIdx, clickTime, x, y);
  }

  /** Show context menu when right-clicking a track header */
  void showTrackHeaderContextMenu(int trackIdx, MouseEvent e) {
    TimelineContextMenu.showTrackHeaderContextMenu(this, trackIdx, e);
  }

  /** Show popup from the input dropdown button in the track header */
  void showInputChannelPopup(int trackIdx, MouseEvent e) {
    TimelineContextMenu.showInputChannelPopup(this, trackIdx, e);
  }

  /** Get the row header panel (for context menu positioning) */
  JPanel getRowHeader() {
    return rowHeader;
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
    int groupParentIndex = -1; // -1 = no parent, >= 0 = index of group parent track
    int trackType = 0; // 0 = NORMAL, 1 = GROUP, 2 = AUX
    boolean collapsed = false; // For group tracks: whether children are hidden

    TrackTimeline(int index) {
      this.index = index;
    }

    boolean isGroupTrack() {
      return trackType == 1;
    }

    boolean isChildOf(int parentIdx) {
      return groupParentIndex == parentIdx;
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
      cr.fadeInSec = info.getFadeInSec();
      cr.fadeOutSec = info.getFadeOutSec();
      cr.muted = info.getMuted();
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
    float fadeInSec = 0; // Linear fade-in duration in seconds
    float fadeOutSec = 0; // Linear fade-out duration in seconds
    boolean muted = false; // Clip is muted (bounced-in-place)
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
