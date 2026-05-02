package hibiki;

import hibiki.pb.commands.*;
import hibiki.pb.commands.Request;
import hibiki.pb.core.*;
import hibiki.pb.notifications.*;
import hibiki.pb.notifications.Notification;
import java.io.*;
import java.util.List;
import java.util.function.Consumer;
import java.util.logging.Level;
import java.util.logging.Logger;

public class BackendManager {
  private static final Logger LOG = Logger.getLogger(BackendManager.class.getName());
  private static final BackendManager instance = new BackendManager();
  private final EngineProcess engineProcess = new EngineProcess();
  private final IpcClient ipcClient = new IpcClient();
  private boolean isPlaying = false; // Track playback state for toggle
  private boolean isRecording = false; // Track recording state
  private volatile HibikiConfig currentConfig = null;
  private String defaultInputDeviceId = ""; // Default input device from Settings

  private BackendManager() {}

  public void setEngineFlags(List<String> flags) {
    engineProcess.setEngineFlags(flags);
  }

  public static BackendManager getInstance() {
    return instance;
  }

  public void start() {
    try {
      engineProcess.start();
      ipcClient.start(
          engineProcess.getInputStream(),
          engineProcess.getErrorStream(),
          engineProcess.getOutputStream());

      ipcClient.addNotificationListener(
          notification -> {
            if (notification.getResponseCase() == Notification.ResponseCase.CONFIG) {
              currentConfig = notification.getConfig();
            }
          });

      Runtime.getRuntime().addShutdownHook(new Thread(this::stop));
    } catch (IOException e) {
      LOG.log(Level.SEVERE, "Failed to start backend", e);
    }
  }

  public synchronized void terminateProcess() {
    engineProcess.terminateProcess();
    ipcClient.stop();
    currentConfig = null;
  }

  public void stop() {
    terminateProcess();
  }

  public void restart() {
    LOG.info("Restarting backend...");
    terminateProcess();
    try {
      Thread.sleep(200);
    } catch (InterruptedException ignored) {
    }
    start();
  }

  public void restartPluginWorkers() {
    LOG.info("Requesting plugin worker restart...");
    sendRequest(
        Request.newBuilder()
            .setRestartWorkers(hibiki.pb.commands.RestartWorkers.getDefaultInstance())
            .build());
  }

  public void addNotificationListener(Consumer<Notification> listener) {
    ipcClient.addNotificationListener(listener);
  }

  public void removeNotificationListener(Consumer<Notification> listener) {
    ipcClient.removeNotificationListener(listener);
  }

  public HibikiConfig getCurrentConfig() {
    return currentConfig;
  }

  public synchronized void sendRequest(Request request) {
    ipcClient.sendRequest(request);
  }

  public void startPlayback() {
    sendRequest(
        Request.newBuilder()
            .setTransport(TransportCmd.newBuilder().setAction(TransportCmd.Action.ACTION_PLAY))
            .build());
    isPlaying = true;
  }

  public void stopPlayback() {
    sendRequest(
        Request.newBuilder()
            .setTransport(TransportCmd.newBuilder().setAction(TransportCmd.Action.ACTION_STOP))
            .build());
    isPlaying = false;
    isRecording = false;
  }

  /** Start recording on armed tracks */
  public void startRecording() {
    sendRequest(
        Request.newBuilder()
            .setTransport(TransportCmd.newBuilder().setAction(TransportCmd.Action.ACTION_RECORD))
            .build());
    isPlaying = true;
    isRecording = true;
  }

  /** Set or clear the loop region */
  public void sendSetLoop(boolean enabled, float startSec, float endSec) {
    sendRequest(
        Request.newBuilder()
            .setTransport(
                TransportCmd.newBuilder()
                    .setAction(TransportCmd.Action.ACTION_SET_LOOP)
                    .setLoopEnabled(enabled)
                    .setLoopStart(startSec)
                    .setLoopEnd(endSec))
            .build());
  }

  public boolean isRecording() {
    return isRecording;
  }

  /** Toggle record arm on a track */
  public void armTrack(int trackIndex) {
    sendRequest(
        Request.newBuilder()
            .setTrack(
                TrackCmd.newBuilder()
                    .setAction(TrackCmd.Action.ACTION_ARM_RECORD)
                    .setTarget(EntityRef.newBuilder().setTrackIndex(trackIndex)))
            .build());
  }

  /** Set input device and channel configuration for a track */
  public void setInputDevice(int trackIndex, String deviceId, int channelStart, boolean stereo) {
    sendRequest(
        Request.newBuilder()
            .setTrack(
                TrackCmd.newBuilder()
                    .setAction(TrackCmd.Action.ACTION_SET_INPUT_DEVICE)
                    .setTarget(EntityRef.newBuilder().setTrackIndex(trackIndex))
                    .setInputDeviceId(deviceId)
                    .setInputChannelStart(channelStart)
                    .setInputStereo(stereo))
            .build());
  }

  /** Request list of available audio input devices */
  public void requestAudioInputs() {
    sendRequest(Request.newBuilder().setListAudioInputs(ListAudioInputs.newBuilder()).build());
  }

  /** Get the default input device ID set in Settings */
  public String getDefaultInputDeviceId() {
    return defaultInputDeviceId;
  }

  /** Set the default input device ID from Settings */
  public void setDefaultInputDeviceId(String id) {
    this.defaultInputDeviceId = id;
  }

  /** Request list of available MIDI input devices */
  public void requestMidiInputs() {
    sendRequest(
        Request.newBuilder()
            .setListMidiInputs(hibiki.pb.commands.ListMidiInputs.newBuilder())
            .build());
  }

  /** Set MIDI input device for a track */
  public void setMidiInput(int trackIndex, String midiDeviceId) {
    sendRequest(
        Request.newBuilder()
            .setTrack(
                TrackCmd.newBuilder()
                    .setAction(TrackCmd.Action.ACTION_SET_MIDI_INPUT)
                    .setTarget(hibiki.pb.core.EntityRef.newBuilder().setTrackIndex(trackIndex))
                    .setMidiInputDeviceId(midiDeviceId))
            .build());
  }

  /** Send a virtual MIDI note event (from PC keyboard) */
  public void sendVirtualMidi(int trackIndex, int note, int velocity, boolean noteOn) {
    sendRequest(
        Request.newBuilder()
            .setSendVirtualMidi(
                hibiki.pb.commands.SendVirtualMidi.newBuilder()
                    .setTrackIndex(trackIndex)
                    .setNote(note)
                    .setVelocity(velocity)
                    .setNoteOn(noteOn))
            .build());
  }

  /** Set recording mode for a track (0 = audio, 1 = MIDI) */
  public void setRecordMode(int trackIndex, boolean midiMode) {
    sendRequest(
        Request.newBuilder()
            .setTrack(
                TrackCmd.newBuilder()
                    .setAction(TrackCmd.Action.ACTION_SET_RECORD_MODE)
                    .setTarget(EntityRef.newBuilder().setTrackIndex(trackIndex))
                    .setRecordMode(midiMode ? 1 : 0))
            .build());
  }

  /** Set track volume (0.0 – 2.0, 1.0 = unity) */
  public void setTrackVolume(int trackIndex, float volume) {
    sendRequest(
        Request.newBuilder()
            .setTrack(
                TrackCmd.newBuilder()
                    .setAction(TrackCmd.Action.ACTION_SET_VOLUME)
                    .setTarget(EntityRef.newBuilder().setTrackIndex(trackIndex))
                    .setValue(volume))
            .build());
  }

  /** Set track pan (-1.0 left to 1.0 right, 0.0 = center) */
  public void setTrackPan(int trackIndex, float pan) {
    sendRequest(
        Request.newBuilder()
            .setTrack(
                TrackCmd.newBuilder()
                    .setAction(TrackCmd.Action.ACTION_SET_PAN)
                    .setTarget(EntityRef.newBuilder().setTrackIndex(trackIndex))
                    .setValue(pan))
            .build());
  }

  /** Set track mute state */
  public void setTrackMute(int trackIndex, boolean muted) {
    sendRequest(
        Request.newBuilder()
            .setTrack(
                TrackCmd.newBuilder()
                    .setAction(TrackCmd.Action.ACTION_SET_MUTE)
                    .setTarget(EntityRef.newBuilder().setTrackIndex(trackIndex))
                    .setFlag(muted))
            .build());
  }

  /** Set track solo state */
  public void setTrackSolo(int trackIndex, boolean soloed) {
    sendRequest(
        Request.newBuilder()
            .setTrack(
                TrackCmd.newBuilder()
                    .setAction(TrackCmd.Action.ACTION_SET_SOLO)
                    .setTarget(EntityRef.newBuilder().setTrackIndex(trackIndex))
                    .setFlag(soloed))
            .build());
  }

  /** Toggle play/stop state - triggered by Space key */
  public void togglePlay() {
    if (isPlaying) {
      stopPlayback();
    } else {
      startPlayback();
    }
  }

  public void seek(float position) {
    sendRequest(
        Request.newBuilder()
            .setTransport(
                TransportCmd.newBuilder()
                    .setAction(TransportCmd.Action.ACTION_SEEK)
                    .setSeekPos(position))
            .build());
  }

  public void addTimelineClip(int trackIndex, String path, float startTime, float durationBeats) {
    sendRequest(
        Request.newBuilder()
            .setTrack(
                TrackCmd.newBuilder()
                    .setAction(TrackCmd.Action.ACTION_ADD_TIMELINE_CLIP)
                    .setTarget(EntityRef.newBuilder().setTrackIndex(trackIndex))
                    .setClipData(Clip.newBuilder().setPath(path).setDurationBeats(durationBeats))
                    .setValue(startTime))
            .build());
  }

  public void removeTimelineClip(int trackIndex, int clipIndex) {
    sendRequest(
        Request.newBuilder()
            .setTrack(
                TrackCmd.newBuilder()
                    .setAction(TrackCmd.Action.ACTION_REMOVE_TIMELINE_CLIP)
                    .setTarget(
                        EntityRef.newBuilder()
                            .setTrackIndex(trackIndex)
                            .setTimelineClip(clipIndex)))
            .build());
  }

  /** Add a modulator to a device slot */
  public void addModulator(
      int trackIndex,
      int pluginIndex,
      int slotIndex,
      int waveform,
      float rateHz,
      float depth,
      boolean syncToTempo) {
    sendRequest(
        Request.newBuilder()
            .setModulation(
                ModulationCmd.newBuilder()
                    .setAction(ModulationCmd.Action.ACTION_ADD)
                    .setTarget(
                        EntityRef.newBuilder()
                            .setTrackIndex(trackIndex)
                            .setPluginIndex(pluginIndex))
                    .setSlotIndex(slotIndex)
                    .setWaveform(waveform)
                    .setRateHz(rateHz)
                    .setDepth(depth)
                    .setSyncToTempo(syncToTempo))
            .build());
  }

  /** Remove a modulator from a device slot */
  public void removeModulator(int trackIndex, int pluginIndex, int slotIndex) {
    sendRequest(
        Request.newBuilder()
            .setModulation(
                ModulationCmd.newBuilder()
                    .setAction(ModulationCmd.Action.ACTION_REMOVE)
                    .setTarget(
                        EntityRef.newBuilder()
                            .setTrackIndex(trackIndex)
                            .setPluginIndex(pluginIndex))
                    .setSlotIndex(slotIndex))
            .build());
  }

  /** Configure modulator parameters (rate, depth, waveform, sync) */
  public void configureModulator(
      int trackIndex,
      int pluginIndex,
      int slotIndex,
      int waveform,
      float rateHz,
      float depth,
      boolean syncToTempo) {
    sendRequest(
        Request.newBuilder()
            .setModulation(
                ModulationCmd.newBuilder()
                    .setAction(ModulationCmd.Action.ACTION_CONFIGURE)
                    .setTarget(
                        EntityRef.newBuilder()
                            .setTrackIndex(trackIndex)
                            .setPluginIndex(pluginIndex))
                    .setSlotIndex(slotIndex)
                    .setWaveform(waveform)
                    .setRateHz(rateHz)
                    .setDepth(depth)
                    .setSyncToTempo(syncToTempo))
            .build());
  }

  /** Assign a modulator to a target parameter */
  public void assignModulator(int trackIndex, int pluginIndex, int slotIndex, int targetParamId) {
    sendRequest(
        Request.newBuilder()
            .setModulation(
                ModulationCmd.newBuilder()
                    .setAction(ModulationCmd.Action.ACTION_ASSIGN)
                    .setTarget(
                        EntityRef.newBuilder()
                            .setTrackIndex(trackIndex)
                            .setPluginIndex(pluginIndex))
                    .setSlotIndex(slotIndex)
                    .setTargetParamId(targetParamId))
            .build());
  }

  public void resizeTimelineClip(
      int trackIndex, int clipIndex, float durationBeats, float trimStartBeats) {
    sendRequest(
        Request.newBuilder()
            .setTrack(
                TrackCmd.newBuilder()
                    .setAction(TrackCmd.Action.ACTION_RESIZE_TIMELINE_CLIP)
                    .setTarget(
                        EntityRef.newBuilder().setTrackIndex(trackIndex).setTimelineClip(clipIndex))
                    .setClipData(
                        Clip.newBuilder()
                            .setDurationBeats(durationBeats)
                            .setTrimStartBeats(trimStartBeats)))
            .build());
  }

  public void setTimelineClipLoop(
      int trackIndex, int clipIndex, boolean isLoop, float loopIntervalBeats) {
    sendRequest(
        Request.newBuilder()
            .setTrack(
                TrackCmd.newBuilder()
                    .setAction(TrackCmd.Action.ACTION_SET_CLIP_LOOP)
                    .setTarget(
                        EntityRef.newBuilder().setTrackIndex(trackIndex).setTimelineClip(clipIndex))
                    .setFlag(isLoop)
                    .setValue(loopIntervalBeats))
            .build());
  }

  /** Copy (or alias) a timeline clip from source track/clip to target track at given time. */
  public void copyTimelineClip(
      int sourceTrackIndex,
      int clipIndex,
      float newStartTimeSec,
      int targetTrackIndex,
      boolean isAlias) {
    sendRequest(
        Request.newBuilder()
            .setTrack(
                TrackCmd.newBuilder()
                    .setAction(TrackCmd.Action.ACTION_COPY_TIMELINE_CLIP)
                    .setTarget(
                        EntityRef.newBuilder()
                            .setTrackIndex(sourceTrackIndex)
                            .setTimelineClip(clipIndex))
                    .setValue(newStartTimeSec)
                    .setTargetTrackIndex(targetTrackIndex)
                    .setFlag(isAlias))
            .build());
  }

  public void moveTimelineClip(
      int sourceTrackIndex, int clipIndex, float newStartTimeSec, int targetTrackIndex) {
    sendRequest(
        Request.newBuilder()
            .setTrack(
                TrackCmd.newBuilder()
                    .setAction(TrackCmd.Action.ACTION_MOVE_TIMELINE_CLIP)
                    .setTarget(
                        EntityRef.newBuilder()
                            .setTrackIndex(sourceTrackIndex)
                            .setTimelineClip(clipIndex))
                    .setValue(newStartTimeSec)
                    .setTargetTrackIndex(targetTrackIndex))
            .build());
  }

  public void moveAutomationClip(int trackIndex, int laneIndex, int clipIndex, float startTimeSec) {
    sendRequest(
        Request.newBuilder()
            .setAutomation(
                AutomationCmd.newBuilder()
                    .setAction(AutomationCmd.Action.ACTION_MOVE_CLIP)
                    .setTarget(
                        EntityRef.newBuilder().setTrackIndex(trackIndex).setLaneIndex(laneIndex))
                    .setClipIndex(clipIndex)
                    .setStartTimeSec(startTimeSec))
            .build());
  }

  public void resizeAutomationClip(
      int trackIndex, int laneIndex, int clipIndex, float durationBeats) {
    sendRequest(
        Request.newBuilder()
            .setAutomation(
                AutomationCmd.newBuilder()
                    .setAction(AutomationCmd.Action.ACTION_RESIZE_CLIP)
                    .setTarget(
                        EntityRef.newBuilder().setTrackIndex(trackIndex).setLaneIndex(laneIndex))
                    .setClipIndex(clipIndex)
                    .setDurationBeats(durationBeats))
            .build());
  }

  public void renameAutomationClip(int trackIndex, int laneIndex, int clipIndex, String name) {
    sendRequest(
        Request.newBuilder()
            .setAutomation(
                AutomationCmd.newBuilder()
                    .setAction(AutomationCmd.Action.ACTION_RENAME_CLIP)
                    .setTarget(
                        EntityRef.newBuilder().setTrackIndex(trackIndex).setLaneIndex(laneIndex))
                    .setClipIndex(clipIndex)
                    .setClipName(name))
            .build());
  }

  /**
   * Request MIDI data for a clip (for Piano Roll editing) Use slotIdx >= 0 for session clips,
   * clipIdx >= 0 for timeline clips
   */
  public void requestClipMidi(int trackIdx, int slotIdx, int clipIdx) {
    sendRequest(
        Request.newBuilder()
            .setMidi(
                MidiCmd.newBuilder()
                    .setAction(MidiCmd.Action.ACTION_GET)
                    .setTarget(
                        EntityRef.newBuilder()
                            .setTrackIndex(trackIdx)
                            .setSessionSlot(slotIdx)
                            .setTimelineClip(clipIdx)))
            .build());
  }

  /**
   * Update clip's MIDI data (from Piano Roll edits) Use slotIdx >= 0 for session clips, clipIdx >=
   * 0 for timeline clips
   */
  public void updateClipMidi(
      int trackIdx,
      int slotIdx,
      int clipIdx,
      int resolution,
      long[] ticks,
      int[] pitches,
      long[] durationTicks,
      int[] velocities) {
    MidiCmd.Builder cmdBuilder =
        MidiCmd.newBuilder()
            .setAction(MidiCmd.Action.ACTION_UPDATE)
            .setTarget(
                EntityRef.newBuilder()
                    .setTrackIndex(trackIdx)
                    .setSessionSlot(slotIdx)
                    .setTimelineClip(clipIdx))
            .setResolution(resolution);
    for (int i = 0; i < ticks.length; i++) {
      cmdBuilder.addEvents(
          MidiEvent.newBuilder()
              .setTick(ticks[i])
              .setPitch(pitches[i])
              .setDurationTicks(durationTicks[i])
              .setVelocity(velocities[i]));
    }
    sendRequest(Request.newBuilder().setMidi(cmdBuilder).build());
  }

  public void sendPanic() {
    sendRequest(
        Request.newBuilder()
            .setMidi(MidiCmd.newBuilder().setAction(MidiCmd.Action.ACTION_PANIC))
            .build());
  }
}
