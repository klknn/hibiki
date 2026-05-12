package hibiki.ui;

import hibiki.BackendManager;
import hibiki.pb.commands.*;
import hibiki.pb.core.EntityRef;
import java.awt.event.MouseEvent;
import java.io.File;
import javax.swing.*;

/**
 * Extracted context menu logic from TimelineView. Static utility class — each method takes a
 * TimelineView reference to access shared state.
 */
final class TimelineContextMenu {

  private TimelineContextMenu() {}

  /** Show context menu for a timeline clip */
  static void showClipContextMenu(
      TimelineView view, int trackIdx, TimelineView.ClipRect clip, int x, int y) {
    JPopupMenu menu = new JPopupMenu();

    // Edit Clip (MIDI → Piano Roll, Audio → Audio Editor)
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
            JFrame ownerFrame = (JFrame) SwingUtilities.getWindowAncestor(view);
            // Find clip index in track's timeline clips
            TimelineView.TrackTimeline trackTimeline = view.tracks.get(trackIdx);
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
            // Audio clip — open in Audio Editor with clip context for in-place editing
            JFrame ownerFrame = (JFrame) SwingUtilities.getWindowAncestor(view);
            TimelineView.TrackTimeline trackTimeline = view.tracks.get(trackIdx);
            int clipIndex = trackTimeline.clips.indexOf(clip);
            AudioEditorDialog dlg =
                new AudioEditorDialog(ownerFrame, clip.path, trackIdx, clipIndex);
            dlg.setVisible(true);
          }
        });
    menu.add(editItem);

    // Delete Clip
    menu.addSeparator();
    JMenuItem deleteItem = new JMenuItem("Delete Clip");
    deleteItem.addActionListener(
        e -> {
          TimelineView.TrackTimeline track = view.tracks.get(trackIdx);
          int clipIdx = track.clips.indexOf(clip);
          if (clipIdx >= 0) {
            // Notify engine to remove from playback state
            BackendManager.getInstance().removeTimelineClip(trackIdx, clipIdx);
            // Remove from GUI
            track.clips.remove(clipIdx);
            track.clipMap.clear();
            for (int i = 0; i < track.clips.size(); i++) {
              track.clipMap.put(i, track.clips.get(i));
            }
            view.updateContentSize();
            view.repaint();
          }
        });
    menu.add(deleteItem);

    // Mute/Unmute toggle
    TimelineView.TrackTimeline muteTrack = view.tracks.get(trackIdx);
    int muteClipIdx = muteTrack.clips.indexOf(clip);
    boolean currentlyMuted = clip.muted;
    JMenuItem muteItem = new JMenuItem(currentlyMuted ? "Unmute" : "Mute");
    muteItem.addActionListener(
        e -> {
          if (muteClipIdx >= 0) {
            BackendManager.getInstance().setClipMuted(trackIdx, muteClipIdx, !currentlyMuted);
          }
        });
    menu.add(muteItem);

    // Bounce In Place
    menu.addSeparator();
    JMenuItem bounceItem = new JMenuItem("Bounce In Place...");
    bounceItem.addActionListener(
        e -> {
          String tailStr = JOptionPane.showInputDialog(view, "Tail duration (seconds):", "0");
          if (tailStr != null) {
            try {
              float tailSec = Float.parseFloat(tailStr);
              TimelineView.TrackTimeline track = view.tracks.get(trackIdx);
              int clipIdx = track.clips.indexOf(clip);
              if (clipIdx >= 0) {
                BackendManager.getInstance().bounceClipInPlace(trackIdx, clipIdx, tailSec);
              }
            } catch (NumberFormatException ex) {
              // ignore invalid input
            }
          }
        });
    menu.add(bounceItem);

    menu.show(view.contentPanel, x, y);
  }

  /** Show context menu for empty track area */
  static void showEmptyAreaContextMenu(
      TimelineView view, int trackIdx, float clickTime, int x, int y) {
    JPopupMenu menu = new JPopupMenu();

    // Create New Clip
    JMenuItem createItem = new JMenuItem("Create New Clip");
    createItem.addActionListener(
        e -> {
          float snapTime = view.snapToGrid(clickTime);
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
        && trackIdx < view.tracks.size()
        && view.tracks.get(trackIdx).pluginName != null) {
      menu.addSeparator();
      JMenuItem autoItem = new JMenuItem("Add Automation Lane...");
      autoItem.addActionListener(e -> showAddAutomationDialog(view, trackIdx));
      menu.add(autoItem);
    }

    menu.show(view.contentPanel, x, y);
  }

  /** Show context menu when right-clicking a track header */
  static void showTrackHeaderContextMenu(TimelineView view, int trackIdx, MouseEvent e) {
    JPopupMenu menu = new JPopupMenu();
    TimelineView.TrackTimeline track = view.tracks.get(trackIdx);

    // Rename track
    JMenuItem renameItem = new JMenuItem("Rename Track");
    renameItem.addActionListener(ev -> view.renameTrack(trackIdx));
    menu.add(renameItem);

    menu.addSeparator();

    // Record arm toggle
    JMenuItem armItem =
        new JMenuItem(track.recordArmed ? "✓ Disarm Recording" : "Arm for Recording");
    armItem.addActionListener(
        ev -> {
          track.recordArmed = !track.recordArmed;
          BackendManager.getInstance().armTrack(trackIdx);
          view.repaintRowHeader();
          view.contentPanel.repaint();
        });
    menu.add(armItem);

    // Set Input Device
    JMenuItem inputItem = new JMenuItem("Set Input Device...");
    inputItem.addActionListener(ev -> showInputDeviceDialog(view, trackIdx));
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
          view.repaintRowHeader();
        });
    monoItem.addActionListener(
        ev -> {
          track.inputStereo = false;
          BackendManager.getInstance()
              .setInputDevice(trackIdx, track.inputDeviceId, track.inputChannelStart, false);
          view.repaintRowHeader();
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
            view.repaintRowHeader();
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
      addAutoItem.addActionListener(ev -> showAddAutomationDialog(view, trackIdx));
      menu.add(addAutoItem);
    }

    // Remove existing automation lanes
    if (!track.automationLanes.isEmpty()) {
      JMenu removeMenu = new JMenu("Remove Automation Lane");
      for (int j = 0; j < track.automationLanes.size(); j++) {
        TimelineView.AutomationLaneData lane = track.automationLanes.get(j);
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
    addTrackItem.addActionListener(ev -> view.addTrack());
    menu.add(addTrackItem);

    JMenuItem deleteTrackItem = new JMenuItem("Delete Track");
    deleteTrackItem.setEnabled(view.tracks.size() > 1);
    deleteTrackItem.addActionListener(ev -> view.removeTrack(trackIdx));
    menu.add(deleteTrackItem);

    menu.addSeparator();

    // Add Aux Track (creates track in both views via SessionView.addTrack)
    JMenuItem addAuxItem = new JMenuItem("Add Aux Track");
    addAuxItem.addActionListener(
        ev -> {
          int auxIdx = view.tracks.size();
          if (SessionView.getInstance() != null) {
            SessionView.getInstance().addTrack(); // syncs both views
          } else {
            view.addTrack();
          }
          BackendManager.getInstance().setTrackType(auxIdx, 2); // AUX = 2
          BackendManager.getInstance().loadPlugin(auxIdx, "builtin://aux");
        });
    menu.add(addAuxItem);

    // Add Group Track
    JMenuItem addGroupItem = new JMenuItem("Add Group Track");
    addGroupItem.addActionListener(
        ev -> {
          int groupIdx = view.tracks.size();
          if (SessionView.getInstance() != null) {
            SessionView.getInstance().addTrack();
          } else {
            view.addTrack();
          }
          BackendManager.getInstance().setTrackType(groupIdx, 1); // GROUP = 1
        });
    menu.add(addGroupItem);

    menu.show(view.getRowHeader(), e.getX(), e.getY());
  }

  /** Show popup from the input dropdown button in the track header */
  static void showInputChannelPopup(TimelineView view, int trackIdx, MouseEvent e) {
    TimelineView.TrackTimeline track = view.tracks.get(trackIdx);
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
              view.repaintRowHeader();
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
              view.repaintRowHeader();
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
          view.repaintRowHeader();
        });
    monoItem.addActionListener(
        ev -> {
          track.inputStereo = false;
          BackendManager.getInstance()
              .setInputDevice(trackIdx, track.inputDeviceId, track.inputChannelStart, false);
          view.repaintRowHeader();
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
            view.repaintRowHeader();
          });
      popup.add(chItem);
    }

    popup.show(e.getComponent(), e.getX(), e.getY());
  }

  /** Show dialog for selecting audio input device */
  static void showInputDeviceDialog(TimelineView view, int trackIdx) {
    // Request fresh device list
    BackendManager.getInstance().requestAudioInputs();

    // Use cached list (will be populated from notification)
    var devices = TimelineNotificationHandler.cachedInputDevices;
    if (devices.isEmpty()) {
      JOptionPane.showMessageDialog(
          view,
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
                view,
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
          TimelineView.TrackTimeline track = view.tracks.get(trackIdx);
          track.inputDeviceId = dev.getId();
          BackendManager.getInstance()
              .setInputDevice(trackIdx, dev.getId(), track.inputChannelStart, track.inputStereo);
          break;
        }
      }
    }
  }

  /** Show dialog to pick a parameter for automation (fallback when no param was touched) */
  static void showAddAutomationDialog(TimelineView view, int trackIdx) {
    String input =
        JOptionPane.showInputDialog(
            view,
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
        JOptionPane.showMessageDialog(view, "Invalid input format.");
      }
    }
  }
}
