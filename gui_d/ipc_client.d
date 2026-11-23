module gui_d.ipc_client;

import core.stdc.string : memcpy;
import dproto.proto : Arena, computeProtoSize, decodeProto, encodeProto;
import pb.commands : AudioEditorCmd, AutomationCmd, DrumPadCmd, ListAudioInputs,
  ListMidiInputs, MidiCmd, PluginCmd, PluginHostMode,
  ProjectCmd, Request, ScanRemotePlugins,
  SendVirtualMidi, SequencerCmd, SetAudioBufferSize, SetPluginHostMode,
  SetProcessingPrecision, TrackCmd, TransportCmd;
import pb.core : Clip, EntityRef, MidiEvent, PbAutoPoint = AutomationPoint,
  PbPlaybackMode = PlaybackMode, TrackType;
import pb.notifications : Notification;
import std.process : Pipe, ProcessPipes, pipeProcess, Redirect, tryWait, wait;
import std.stdio : stderr;

/// Magic header `0x48424B49` ("HBKI") written by `engine/ipc/ipc.cpp` in little-endian.
enum uint HBKI_MAGIC = 0x48424B49;

/// Maximum single IPC notification payload size (4 MiB).
enum uint MAX_IPC_FRAME_BYTES = 4 * 1024 * 1024;

/// Encodes a Protobuf `Request` with a 4-byte little-endian length prefix for `hbk-play` stdin.
ubyte[] encodeLengthPrefixedRequest(const ref Request req) @safe {
  const size_t needed = computeProtoSize(req);
  ubyte[] payload;
  if (needed > 0) {
    auto raw = new ubyte[needed];
    const size_t written = encodeProto(req, raw);
    payload = raw[0 .. written];
  }
  const uint len = cast(uint)payload.length;
  auto framed = new ubyte[4 + payload.length];
  framed[0] = cast(ubyte)(len & 0xFF);
  framed[1] = cast(ubyte)((len >> 8) & 0xFF);
  framed[2] = cast(ubyte)((len >> 16) & 0xFF);
  framed[3] = cast(ubyte)((len >> 24) & 0xFF);
  if (payload.length > 0) {
    framed[4 .. $] = payload[];
  }
  return framed;
}

/// Deserializes a Protobuf message into a per-message GC-owned Arena so queued notifications
/// never alias a shared static scratch buffer.
bool decodeProtoOwned(T)(const(ubyte)[] payload, out T msg) @safe {
  size_t cap = payload.length * 8 + 4096;
  if (cap < 4096)
    cap = 4096;
  while (cap <= 32 * 1024 * 1024) {
    T candidate;
    Arena arena = Arena(new ubyte[cap]);
    if (decodeProto(payload, candidate, arena)) {
      msg = candidate;
      return true;
    }
    cap *= 2;
  }
  return false;
}

/// Extracts one `HBKI`-framed payload from `rxBuf`, advancing `rxBuf` past the consumed frame.
/// Returns `true` if a complete frame was extracted, `false` if more bytes are needed.
bool extractNextFrame(ref ubyte[] rxBuf, out ubyte[] outPayload) @safe {
  while (rxBuf.length >= 8) {
    const uint magic = cast(uint)rxBuf[0] | (cast(uint)rxBuf[1] << 8) | (
        cast(uint)rxBuf[2] << 16) | (cast(uint)rxBuf[3] << 24);
    if (magic != HBKI_MAGIC) {
      // Resynchronize by scanning forward 1 byte.
      rxBuf = rxBuf[1 .. $];
      continue;
    }
    const uint len = cast(uint)rxBuf[4] | (cast(uint)rxBuf[5] << 8) | (
        cast(uint)rxBuf[6] << 16) | (cast(uint)rxBuf[7] << 24);
    if (len > MAX_IPC_FRAME_BYTES) {
      // Resynchronize by scanning forward 1 byte in case a valid HBKI header starts inside [1..3].
      rxBuf = rxBuf[1 .. $];
      continue;
    }
    if (rxBuf.length < 8 + len) {
      return false;
    }
    outPayload = rxBuf[8 .. 8 + len].dup;
    rxBuf = rxBuf[8 + len .. $];
    return true;
  }
  return false;
}

/// Helper constructors for common engine commands.
Request makeTransportRequest(TransportCmd.Action action) @safe {
  Request req;
  TransportCmd cmd;
  cmd.action = action;
  req.transport.set(cmd);
  return req;
}

Request makeSetLoopRequest(bool enabled, double startSec, double endSec) @safe {
  Request req;
  TransportCmd cmd;
  cmd.action = TransportCmd.Action.ACTION_SET_LOOP;
  cmd.loop_enabled = enabled;
  cmd.loop_start = startSec;
  cmd.loop_end = endSec;
  req.transport.set(cmd);
  return req;
}

Request makeSeekRequest(double posSec) @safe {
  Request req;
  TransportCmd cmd;
  cmd.action = TransportCmd.Action.ACTION_SEEK;
  cmd.seek_pos = cast(float)(posSec < 0.0 ? 0.0 : posSec);
  req.transport.set(cmd);
  return req;
}

Request makeSetMetronomeRequest(bool enabled) @safe {
  Request req;
  TransportCmd cmd;
  cmd.action = TransportCmd.Action.ACTION_SET_METRONOME;
  cmd.enabled = enabled;
  req.transport.set(cmd);
  return req;
}

Request makePanicRequest() @safe {
  Request req;
  TransportCmd cmd;
  cmd.action = TransportCmd.Action.ACTION_PANIC;
  req.transport.set(cmd);
  return req;
}

Request makeSetMasterVolumeRequest(float volume) @safe {
  Request req;
  TransportCmd cmd;
  cmd.action = TransportCmd.Action.ACTION_SET_MASTER_VOLUME;
  cmd.value = volume;
  req.transport.set(cmd);
  return req;
}

Request makeSetMasterPitchRequest(float semitones) @safe {
  Request req;
  TransportCmd cmd;
  cmd.action = TransportCmd.Action.ACTION_SET_MASTER_PITCH;
  cmd.value = semitones;
  req.transport.set(cmd);
  return req;
}

Request makeSetBpmRequest(float bpm) @safe {
  Request req;
  ProjectCmd cmd;
  cmd.action = ProjectCmd.Action.ACTION_SET_BPM;
  cmd.bpm = bpm;
  req.project.set(cmd);
  return req;
}

Request makeQuitRequest() @safe {
  Request req;
  ProjectCmd cmd;
  cmd.action = ProjectCmd.Action.ACTION_QUIT;
  req.project.set(cmd);
  return req;
}

Request makeTrackMuteRequest(int trackIdx, bool muted) @safe {
  Request req;
  TrackCmd cmd;
  cmd.action = TrackCmd.Action.ACTION_SET_MUTE;
  EntityRef target;
  target.track_index = trackIdx;
  cmd.target = target;
  cmd.flag = muted;
  req.track.set(cmd);
  return req;
}

Request makeTrackSoloRequest(int trackIdx, bool soloed) @safe {
  Request req;
  TrackCmd cmd;
  cmd.action = TrackCmd.Action.ACTION_SET_SOLO;
  EntityRef target;
  target.track_index = trackIdx;
  cmd.target = target;
  cmd.flag = soloed;
  req.track.set(cmd);
  return req;
}

Request makeAddTrackRequest(int trackIdx) @safe {
  Request req;
  TrackCmd cmd;
  cmd.action = TrackCmd.Action.ACTION_SET_TRACK_TYPE;
  EntityRef target;
  target.track_index = trackIdx;
  cmd.target = target;
  cmd.value = 0.0f;
  req.track.set(cmd);
  return req;
}

Request makeTrackVolumeRequest(int trackIdx, float volume) @safe {
  Request req;
  TrackCmd cmd;
  cmd.action = TrackCmd.Action.ACTION_SET_VOLUME;
  EntityRef target;
  target.track_index = trackIdx;
  cmd.target = target;
  cmd.value = volume;
  req.track.set(cmd);
  return req;
}

Request makeTrackPanRequest(int trackIdx, float pan) @safe {
  Request req;
  TrackCmd cmd;
  cmd.action = TrackCmd.Action.ACTION_SET_PAN;
  EntityRef target;
  target.track_index = trackIdx;
  cmd.target = target;
  cmd.value = pan;
  req.track.set(cmd);
  return req;
}

Request makeProjectLoadRequest(string path) @safe {
  Request req;
  ProjectCmd cmd;
  cmd.action = ProjectCmd.Action.ACTION_LOAD;
  cmd.path = path;
  req.project.set(cmd);
  return req;
}

Request makeProjectSaveRequest(string path) @safe {
  Request req;
  ProjectCmd cmd;
  cmd.action = ProjectCmd.Action.ACTION_SAVE;
  cmd.path = path;
  req.project.set(cmd);
  return req;
}

Request makeUndoRedoRequest(bool redo) @safe {
  Request req;
  ProjectCmd cmd;
  cmd.action = redo ? ProjectCmd.Action.ACTION_REDO : ProjectCmd.Action
    .ACTION_UNDO;
  req.project.set(cmd);
  return req;
}

Request makeAddTimelineClipRequest(int trackIdx, string path, float startSec, float durBeats) @safe {
  Request req;
  TrackCmd cmd;
  cmd.action = TrackCmd.Action.ACTION_ADD_TIMELINE_CLIP;
  EntityRef target;
  target.track_index = trackIdx;
  cmd.target = target;
  cmd.value = startSec;
  Clip clip;
  clip.path = path;
  clip.duration_beats = durBeats;
  cmd.clip_data = clip;
  req.track.set(cmd);
  return req;
}

Request makeRemoveTimelineClipRequest(int trackIdx, int clipIdx) @safe {
  Request req;
  TrackCmd cmd;
  cmd.action = TrackCmd.Action.ACTION_REMOVE_TIMELINE_CLIP;
  EntityRef target;
  target.track_index = trackIdx;
  target.timeline_clip.set(cast(uint)clipIdx);
  cmd.target = target;
  req.track.set(cmd);
  return req;
}

Request makeMidiGetRequest(int trackIdx, int clipIdx) @safe {
  Request req;
  MidiCmd cmd;
  cmd.action = MidiCmd.Action.ACTION_GET;
  EntityRef target;
  target.track_index = trackIdx;
  target.timeline_clip.set(cast(uint)clipIdx);
  cmd.target = target;
  req.midi.set(cmd);
  return req;
}

Request makeMidiUpdateRequest(int trackIdx, int clipIdx, uint resolution, const(MidiEvent)[] events) @safe {
  Request req;
  MidiCmd cmd;
  cmd.action = MidiCmd.Action.ACTION_UPDATE;
  EntityRef target;
  target.track_index = trackIdx;
  target.timeline_clip.set(cast(uint)clipIdx);
  cmd.target = target;
  cmd.resolution = (resolution > 0) ? resolution : 480;
  cmd.events = events.dup;
  req.midi.set(cmd);
  return req;
}

Request makePluginLoadRequest(int trackIdx, string path, int subPluginIndex = 0) @safe {
  Request req;
  PluginCmd cmd;
  cmd.action = PluginCmd.Action.ACTION_LOAD;
  EntityRef target;
  target.track_index = trackIdx;
  target.plugin_index.set(cast(uint)subPluginIndex);
  cmd.target = target;
  cmd.path = path;
  req.plugin.set(cmd);
  return req;
}

Request makePluginRemoveRequest(int trackIdx, int pluginIdx) @safe {
  Request req;
  PluginCmd cmd;
  cmd.action = PluginCmd.Action.ACTION_REMOVE;
  EntityRef target;
  target.track_index = trackIdx;
  target.plugin_index.set(cast(uint)pluginIdx);
  cmd.target = target;
  req.plugin.set(cmd);
  return req;
}

Request makePluginShowGuiRequest(int trackIdx, int pluginIdx) @safe {
  Request req;
  PluginCmd cmd;
  cmd.action = PluginCmd.Action.ACTION_SHOW_GUI;
  EntityRef target;
  target.track_index = trackIdx;
  target.plugin_index.set(cast(uint)pluginIdx);
  cmd.target = target;
  req.plugin.set(cmd);
  return req;
}

Request makePluginSetParamRequest(int trackIdx, int pluginIdx, uint paramId, float value) @safe {
  Request req;
  PluginCmd cmd;
  cmd.action = PluginCmd.Action.ACTION_SET_PARAM;
  EntityRef target;
  target.track_index = trackIdx;
  target.plugin_index.set(cast(uint)pluginIdx);
  cmd.target = target;
  cmd.param_id = paramId;
  cmd.param_value = value;
  req.plugin.set(cmd);
  return req;
}

/// Sends `ACTION_SET_BYPASS` where `cmd.flag = enabled` (`flag=true` means plugin active, `!flag` means bypassed).
Request makePluginBypassRequest(int trackIdx, int pluginIdx, bool enabled) @safe {
  Request req;
  PluginCmd cmd;
  cmd.action = PluginCmd.Action.ACTION_SET_BYPASS;
  EntityRef target;
  target.track_index = trackIdx;
  target.plugin_index.set(cast(uint)pluginIdx);
  cmd.target = target;
  cmd.flag = enabled;
  req.plugin.set(cmd);
  return req;
}

Request makePluginListRequest(string searchPath) @safe {
  Request req;
  PluginCmd cmd;
  cmd.action = PluginCmd.Action.ACTION_LIST;
  cmd.path = searchPath;
  req.plugin.set(cmd);
  return req;
}

Request makeSendVirtualMidiRequest(int trackIdx, uint note, uint velocity, bool noteOn) @safe {
  Request req;
  SendVirtualMidi cmd;
  cmd.track_index = trackIdx;
  cmd.note = note;
  cmd.velocity = velocity;
  cmd.note_on = noteOn;
  req.send_virtual_midi.set(cmd);
  return req;
}

Request makeSetTrackTypeRequest(int trackIdx, TrackType trackType) @safe {
  Request req;
  TrackCmd cmd;
  cmd.action = TrackCmd.Action.ACTION_SET_TRACK_TYPE;
  EntityRef target;
  target.track_index = trackIdx;
  cmd.target = target;
  cmd.record_mode = cast(int)trackType;
  req.track.set(cmd);
  return req;
}

Request makeSetOutputRoutingRequest(int trackIdx, int targetTrackIdx) @safe {
  Request req;
  TrackCmd cmd;
  cmd.action = TrackCmd.Action.ACTION_SET_OUTPUT_ROUTING;
  EntityRef target;
  target.track_index = trackIdx;
  cmd.target = target;
  cmd.target_track_index = targetTrackIdx;
  req.track.set(cmd);
  return req;
}

Request makeAddPatternTimelineClipRequest(int trackIdx, int patternIdx,
    float startSec, float durationBeats) @safe {
  import std.format : format;

  Request req;
  TrackCmd cmd;
  cmd.action = TrackCmd.Action.ACTION_ADD_TIMELINE_CLIP;
  EntityRef target;
  target.track_index = trackIdx;
  cmd.target = target;
  Clip cd;
  cd.path = format("pattern://%d", patternIdx);
  cd.duration_beats = durationBeats;
  cmd.clip_data = cd;
  cmd.value = startSec;
  req.track.set(cmd);
  return req;
}

Request makeSequencerModeRequest(PbPlaybackMode mode) @safe {
  Request req;
  SequencerCmd cmd;
  cmd.action = SequencerCmd.Action.ACTION_SET_PLAYBACK_MODE;
  cmd.playback_mode = mode;
  req.sequencer.set(cmd);
  return req;
}

Request makeSequencerSwingRequest(float swing) @safe {
  Request req;
  SequencerCmd cmd;
  cmd.action = SequencerCmd.Action.ACTION_SET_SWING;
  cmd.value = swing;
  req.sequencer.set(cmd);
  return req;
}

Request makeSequencerSelectPatternRequest(int patternIdx) @safe {
  Request req;
  SequencerCmd cmd;
  cmd.action = SequencerCmd.Action.ACTION_SELECT_PATTERN;
  cmd.pattern_index = cast(uint)patternIdx;
  req.sequencer.set(cmd);
  return req;
}

Request makeSequencerAddPatternRequest(int patternIdx, string name, uint color, int lengthSteps) @safe {
  Request req;
  SequencerCmd cmd;
  cmd.action = SequencerCmd.Action.ACTION_ADD_PATTERN;
  cmd.pattern_index = cast(uint)patternIdx;
  cmd.name = name;
  cmd.color = color;
  cmd.length_steps = lengthSteps;
  req.sequencer.set(cmd);
  return req;
}

Request makeSequencerAddChannelRequest(int channelIdx, string name, string uri,
    string category, uint color, int mixerInsert) @safe {
  Request req;
  SequencerCmd cmd;
  cmd.action = SequencerCmd.Action.ACTION_ADD_CHANNEL;
  cmd.channel_index = cast(uint)channelIdx;
  cmd.name = name;
  cmd.uri = uri;
  cmd.category = category;
  cmd.color = color;
  cmd.mixer_insert = mixerInsert;
  req.sequencer.set(cmd);
  return req;
}

Request makeSequencerRouteChannelRequest(int channelIdx, int mixerInsert) @safe {
  Request req;
  SequencerCmd cmd;
  cmd.action = SequencerCmd.Action.ACTION_SET_CHANNEL_ROUTE;
  cmd.channel_index = cast(uint)channelIdx;
  cmd.mixer_insert = mixerInsert;
  req.sequencer.set(cmd);
  return req;
}

Request makeSequencerMuteChannelRequest(int channelIdx, bool muted) @safe {
  Request req;
  SequencerCmd cmd;
  cmd.action = SequencerCmd.Action.ACTION_SET_CHANNEL_MUTE;
  cmd.channel_index = cast(uint)channelIdx;
  cmd.enabled = muted;
  req.sequencer.set(cmd);
  return req;
}

Request makeSequencerSoloChannelRequest(int channelIdx, bool soloed) @safe {
  Request req;
  SequencerCmd cmd;
  cmd.action = SequencerCmd.Action.ACTION_SET_CHANNEL_SOLO;
  cmd.channel_index = cast(uint)channelIdx;
  cmd.enabled = soloed;
  req.sequencer.set(cmd);
  return req;
}

Request makeSequencerVolumeChannelRequest(int channelIdx, float volume) @safe {
  Request req;
  SequencerCmd cmd;
  cmd.action = SequencerCmd.Action.ACTION_SET_CHANNEL_VOLUME;
  cmd.channel_index = cast(uint)channelIdx;
  cmd.value = volume;
  req.sequencer.set(cmd);
  return req;
}

Request makeSequencerPanChannelRequest(int channelIdx, float pan) @safe {
  Request req;
  SequencerCmd cmd;
  cmd.action = SequencerCmd.Action.ACTION_SET_CHANNEL_PAN;
  cmd.channel_index = cast(uint)channelIdx;
  cmd.value = pan;
  req.sequencer.set(cmd);
  return req;
}

Request makeSequencerStepRequest(int patternIdx, int channelIdx, int stepIdx, bool active) @safe {
  Request req;
  SequencerCmd cmd;
  cmd.action = SequencerCmd.Action.ACTION_SET_STEP;
  cmd.pattern_index = cast(uint)patternIdx;
  cmd.channel_index = cast(uint)channelIdx;
  cmd.step_index = cast(uint)stepIdx;
  cmd.enabled = active;
  req.sequencer.set(cmd);
  return req;
}

Request makeSequencerStepGraphRequest(int patternIdx, int channelIdx, int stepIdx,
    int graphParam, float value) @safe {
  Request req;
  SequencerCmd cmd;
  cmd.action = SequencerCmd.Action.ACTION_SET_STEP_GRAPH;
  cmd.pattern_index = cast(uint)patternIdx;
  cmd.channel_index = cast(uint)channelIdx;
  cmd.step_index = cast(uint)stepIdx;
  cmd.graph_param = graphParam;
  cmd.value = value;
  req.sequencer.set(cmd);
  return req;
}

Request makeSequencerChannelNotesRequest(int patternIdx, int channelIdx, const(MidiEvent)[] notes) @safe {
  Request req;
  SequencerCmd cmd;
  cmd.action = SequencerCmd.Action.ACTION_SET_CHANNEL_NOTES;
  cmd.pattern_index = cast(uint)patternIdx;
  cmd.channel_index = cast(uint)channelIdx;
  cmd.notes = notes.dup;
  req.sequencer.set(cmd);
  return req;
}

Request makeTrackEqRequest(int trackIdx, float eqLow, float eqMid, float eqHigh) @safe {
  Request req;
  TrackCmd cmd;
  cmd.action = TrackCmd.Action.ACTION_SET_EQ;
  EntityRef target;
  target.track_index = trackIdx;
  cmd.target = target;
  cmd.eq_low = eqLow;
  cmd.eq_mid = eqMid;
  cmd.eq_high = eqHigh;
  req.track.set(cmd);
  return req;
}

Request makeTrackStereoSepRequest(int trackIdx, float stereoSep) @safe {
  Request req;
  TrackCmd cmd;
  cmd.action = TrackCmd.Action.ACTION_SET_STEREO_SEP;
  EntityRef target;
  target.track_index = trackIdx;
  cmd.target = target;
  cmd.value = stereoSep;
  req.track.set(cmd);
  return req;
}

Request makePluginMixRequest(int trackIdx, int pluginIdx, float mix) @safe {
  Request req;
  PluginCmd cmd;
  cmd.action = PluginCmd.Action.ACTION_SET_MIX;
  EntityRef target;
  target.track_index = trackIdx;
  target.plugin_index.set(cast(uint)pluginIdx);
  cmd.target = target;
  cmd.param_value = mix;
  req.plugin.set(cmd);
  return req;
}

Request makeMoveTimelineClipRequest(int srcTrackIdx, int clipIdx,
    float newStartSec, int targetTrackIdx) @safe {
  Request req;
  TrackCmd cmd;
  cmd.action = TrackCmd.Action.ACTION_MOVE_TIMELINE_CLIP;
  EntityRef target;
  target.track_index = srcTrackIdx;
  target.timeline_clip.set(cast(uint)clipIdx);
  cmd.target = target;
  cmd.value = newStartSec;
  cmd.target_track_index = targetTrackIdx;
  req.track.set(cmd);
  return req;
}

Request makeResizeTimelineClipRequest(int trackIdx, int clipIdx, float durBeats,
    float trimStartBeats = 0.0f) @safe {
  Request req;
  TrackCmd cmd;
  cmd.action = TrackCmd.Action.ACTION_RESIZE_TIMELINE_CLIP;
  EntityRef target;
  target.track_index = trackIdx;
  target.timeline_clip.set(cast(uint)clipIdx);
  cmd.target = target;
  Clip cd;
  cd.duration_beats = durBeats;
  cd.trim_start_beats = trimStartBeats;
  cmd.clip_data = cd;
  req.track.set(cmd);
  return req;
}

Request makeSetClipMutedRequest(int trackIdx, int clipIdx, bool muted) @safe {
  Request req;
  TrackCmd cmd;
  cmd.action = TrackCmd.Action.ACTION_SET_CLIP_MUTED;
  EntityRef target;
  target.track_index = trackIdx;
  target.timeline_clip.set(cast(uint)clipIdx);
  cmd.target = target;
  cmd.flag = muted;
  req.track.set(cmd);
  return req;
}

Request makeCopyTimelineClipRequest(int srcTrackIdx, int clipIdx,
    float newStartSec, int targetTrackIdx, bool isAlias = false) @safe {
  Request req;
  TrackCmd cmd;
  cmd.action = TrackCmd.Action.ACTION_COPY_TIMELINE_CLIP;
  EntityRef target;
  target.track_index = srcTrackIdx;
  target.timeline_clip.set(cast(uint)clipIdx);
  cmd.target = target;
  cmd.value = newStartSec;
  cmd.target_track_index = targetTrackIdx;
  cmd.flag = isAlias;
  req.track.set(cmd);
  return req;
}

Request makeAutomationAddLaneRequest(int trackIdx, int pluginIdx = 0, uint paramId = 0) @safe {
  Request req;
  AutomationCmd cmd;
  cmd.action = AutomationCmd.Action.ACTION_ADD_LANE;
  EntityRef target;
  target.track_index = trackIdx;
  target.plugin_index.set(cast(uint)pluginIdx);
  cmd.target = target;
  cmd.param_id = paramId;
  req.automation.set(cmd);
  return req;
}

Request makeAutomationAddClipRequest(int trackIdx, int laneIdx, float startSec,
    float durBeats, string clipName = "") @safe {
  Request req;
  AutomationCmd cmd;
  cmd.action = AutomationCmd.Action.ACTION_ADD_CLIP;
  EntityRef target;
  target.track_index = trackIdx;
  target.lane_index.set(cast(uint)laneIdx);
  cmd.target = target;
  cmd.start_time_sec = startSec;
  cmd.duration_beats = durBeats;
  if (clipName.length > 0)
    cmd.clip_name = clipName;
  req.automation.set(cmd);
  return req;
}

Request makeTrackArmRecordRequest(int trackIdx) @safe {
  Request req;
  TrackCmd cmd;
  cmd.action = TrackCmd.Action.ACTION_ARM_RECORD;
  EntityRef target;
  target.track_index = trackIdx;
  cmd.target = target;
  req.track.set(cmd);
  return req;
}

Request makeTrackSetRecordModeRequest(int trackIdx, int recordMode) @safe {
  Request req;
  TrackCmd cmd;
  cmd.action = TrackCmd.Action.ACTION_SET_RECORD_MODE;
  EntityRef target;
  target.track_index = trackIdx;
  cmd.target = target;
  cmd.record_mode = recordMode;
  req.track.set(cmd);
  return req;
}

Request makeAutomationUpdatePointsRequest(int trackIdx, int laneIdx, uint clipIdx,
    const(PbAutoPoint)[] pts) @safe {
  Request req;
  AutomationCmd cmd;
  cmd.action = AutomationCmd.Action.ACTION_UPDATE_POINTS;
  EntityRef target;
  target.track_index = trackIdx;
  target.lane_index.set(cast(uint)laneIdx);
  cmd.target = target;
  cmd.clip_index = clipIdx;
  cmd.points = pts.dup;
  req.automation.set(cmd);
  return req;
}

Request makeAutomationRemoveClipRequest(int trackIdx, int laneIdx, uint clipIdx) @safe {
  Request req;
  AutomationCmd cmd;
  cmd.action = AutomationCmd.Action.ACTION_REMOVE_CLIP;
  EntityRef target;
  target.track_index = trackIdx;
  target.lane_index.set(cast(uint)laneIdx);
  cmd.target = target;
  cmd.clip_index = clipIdx;
  req.automation.set(cmd);
  return req;
}

Request makeAutomationMoveClipRequest(int trackIdx, int laneIdx, uint clipIdx, float startSec) @safe {
  Request req;
  AutomationCmd cmd;
  cmd.action = AutomationCmd.Action.ACTION_MOVE_CLIP;
  EntityRef target;
  target.track_index = trackIdx;
  target.lane_index.set(cast(uint)laneIdx);
  cmd.target = target;
  cmd.clip_index = clipIdx;
  cmd.start_time_sec = startSec;
  req.automation.set(cmd);
  return req;
}

Request makeAutomationResizeClipRequest(int trackIdx, int laneIdx, uint clipIdx, float durBeats) @safe {
  Request req;
  AutomationCmd cmd;
  cmd.action = AutomationCmd.Action.ACTION_RESIZE_CLIP;
  EntityRef target;
  target.track_index = trackIdx;
  target.lane_index.set(cast(uint)laneIdx);
  cmd.target = target;
  cmd.clip_index = clipIdx;
  cmd.duration_beats = durBeats;
  req.automation.set(cmd);
  return req;
}

Request makePluginGetEditorFrameRequest(int trackIdx, int pluginIdx) @safe {
  Request req;
  PluginCmd cmd;
  cmd.action = PluginCmd.Action.ACTION_GET_EDITOR_FRAME;
  EntityRef target;
  target.track_index = trackIdx;
  target.plugin_index.set(cast(uint)pluginIdx);
  cmd.target = target;
  req.plugin.set(cmd);
  return req;
}

Request makePluginSendEditorInputRequest(int trackIdx, int pluginIdx,
    int inputType, int x, int y, int button = 0, int key = 0, int delta = 0) @safe {
  Request req;
  PluginCmd cmd;
  cmd.action = PluginCmd.Action.ACTION_SEND_EDITOR_INPUT;
  EntityRef target;
  target.track_index = trackIdx;
  target.plugin_index.set(cast(uint)pluginIdx);
  cmd.target = target;
  cmd.input_type = inputType;
  cmd.input_x = x;
  cmd.input_y = y;
  cmd.input_button = button;
  cmd.input_key = key;
  cmd.input_delta = delta;
  req.plugin.set(cmd);
  return req;
}

Request makePluginStopGuiRequest(int trackIdx, int pluginIdx) @safe {
  Request req;
  PluginCmd cmd;
  cmd.action = PluginCmd.Action.ACTION_STOP_GUI;
  EntityRef target;
  target.track_index = trackIdx;
  target.plugin_index.set(cast(uint)pluginIdx);
  cmd.target = target;
  req.plugin.set(cmd);
  return req;
}

Request makePluginLoadSampleRequest(int trackIdx, int pluginIdx, string samplePath) @safe {
  Request req;
  PluginCmd cmd;
  cmd.action = PluginCmd.Action.ACTION_LOAD_SAMPLE;
  EntityRef target;
  target.track_index = trackIdx;
  target.plugin_index.set(cast(uint)pluginIdx);
  cmd.target = target;
  cmd.sample_path = samplePath;
  req.plugin.set(cmd);
  return req;
}

Request makeDrumPadLoadSampleRequest(int trackIdx, int pluginIdx, int padIdx, string samplePath) @safe {
  Request req;
  DrumPadCmd cmd;
  cmd.action = DrumPadCmd.Action.ACTION_LOAD_SAMPLE;
  cmd.track_index = cast(uint)trackIdx;
  cmd.plugin_index = cast(uint)pluginIdx;
  cmd.pad_index = cast(uint)padIdx;
  cmd.sample_path = samplePath;
  req.drum_pad.set(cmd);
  return req;
}

Request makeDrumPadSetVolumeRequest(int trackIdx, int pluginIdx, int padIdx, float vol) @safe {
  Request req;
  DrumPadCmd cmd;
  cmd.action = DrumPadCmd.Action.ACTION_SET_VOLUME;
  cmd.track_index = cast(uint)trackIdx;
  cmd.plugin_index = cast(uint)pluginIdx;
  cmd.pad_index = cast(uint)padIdx;
  cmd.param_value = vol;
  req.drum_pad.set(cmd);
  return req;
}

Request makeDrumPadSetMuteRequest(int trackIdx, int pluginIdx, int padIdx, bool muted) @safe {
  Request req;
  DrumPadCmd cmd;
  cmd.action = DrumPadCmd.Action.ACTION_SET_MUTE;
  cmd.track_index = cast(uint)trackIdx;
  cmd.plugin_index = cast(uint)pluginIdx;
  cmd.pad_index = cast(uint)padIdx;
  cmd.param_value = muted ? 1.0f : 0.0f;
  req.drum_pad.set(cmd);
  return req;
}

Request makeDrumPadSetSoloRequest(int trackIdx, int pluginIdx, int padIdx, bool soloed) @safe {
  Request req;
  DrumPadCmd cmd;
  cmd.action = DrumPadCmd.Action.ACTION_SET_SOLO;
  cmd.track_index = cast(uint)trackIdx;
  cmd.plugin_index = cast(uint)pluginIdx;
  cmd.pad_index = cast(uint)padIdx;
  cmd.param_value = soloed ? 1.0f : 0.0f;
  req.drum_pad.set(cmd);
  return req;
}

Request makeProjectBounceRequest(string path) @safe {
  Request req;
  ProjectCmd cmd;
  cmd.action = ProjectCmd.Action.ACTION_BOUNCE;
  cmd.path = path;
  req.project.set(cmd);
  return req;
}

Request makeProjectCollectFilesRequest(string dirPath = "") @safe {
  Request req;
  ProjectCmd cmd;
  cmd.action = ProjectCmd.Action.ACTION_COLLECT_FILES;
  cmd.path = dirPath;
  req.project.set(cmd);
  return req;
}

Request makeClearHistoryRequest() @safe {
  Request req;
  ProjectCmd cmd;
  cmd.action = ProjectCmd.Action.ACTION_CLEAR_HISTORY;
  req.project.set(cmd);
  return req;
}

Request makeSequencerPatternLengthRequest(int patIdx, int lengthSteps) @safe {
  Request req;
  SequencerCmd cmd;
  cmd.action = SequencerCmd.Action.ACTION_SET_PATTERN_LENGTH;
  cmd.pattern_index = cast(uint)patIdx;
  cmd.length_steps = lengthSteps;
  req.sequencer.set(cmd);
  return req;
}

Request makeSetPluginHostModeRequest(PluginHostMode mode, const(string)[] remoteHosts = null) @safe {
  Request req;
  SetPluginHostMode cmd;
  cmd.mode = mode;
  if (remoteHosts.length > 0)
    cmd.remote_hosts = remoteHosts.dup;
  req.set_plugin_host_mode.set(cmd);
  return req;
}

Request makeSetAudioBufferSizeRequest(int bufferMs) @safe {
  Request req;
  SetAudioBufferSize cmd;
  cmd.buffer_size_ms = bufferMs;
  req.set_audio_buffer_size.set(cmd);
  return req;
}

Request makeSetProcessingPrecisionRequest(bool useDouble) @safe {
  Request req;
  SetProcessingPrecision cmd;
  cmd.use_double = useDouble;
  req.set_processing_precision.set(cmd);
  return req;
}

Request makeListAudioInputsRequest() @safe {
  Request req;
  req.list_audio_inputs.set(ListAudioInputs.init);
  return req;
}

Request makeListMidiInputsRequest() @safe {
  Request req;
  req.list_midi_inputs.set(ListMidiInputs.init);
  return req;
}

Request makeScanRemotePluginsRequest(const(string)[] hosts) @safe {
  Request req;
  ScanRemotePlugins cmd;
  cmd.remote_hosts = hosts.dup;
  req.scan_remote_plugins.set(cmd);
  return req;
}

Request makeAudioEditorLoadRequest(string path) @safe {
  Request req;
  AudioEditorCmd cmd;
  cmd.action = AudioEditorCmd.Action.AE_ACTION_LOAD;
  cmd.path = path;
  req.audio_editor.set(cmd);
  return req;
}

Request makeAudioEditorPreviewPlayRequest() @safe {
  Request req;
  AudioEditorCmd cmd;
  cmd.action = AudioEditorCmd.Action.AE_ACTION_PREVIEW_PLAY;
  req.audio_editor.set(cmd);
  return req;
}

Request makeAudioEditorPreviewStopRequest() @safe {
  Request req;
  AudioEditorCmd cmd;
  cmd.action = AudioEditorCmd.Action.AE_ACTION_PREVIEW_STOP;
  req.audio_editor.set(cmd);
  return req;
}

/// Manages the `hbk-play` child process and its binary Protobuf IPC pipes.
class EngineProcessClient {
  import core.atomic : atomicLoad, atomicStore, cas;
  import core.sync.mutex : Mutex;
  import core.thread : Thread;

  private ProcessPipes pipes_;
  private Mutex writeMutex_;
  private Thread readerThread_;
  private Thread stderrThread_;
  private shared bool running_ = false;
  private shared bool stopping_ = false;
  private void delegate(Notification) onNotification_;
  void delegate(string) onError;

  this(void delegate(Notification) onNotification, void delegate(string) onErrorCb = null) {
    writeMutex_ = new Mutex();
    onNotification_ = onNotification;
    onError = onErrorCb;
  }

  bool isRunning() const {
    return atomicLoad(running_) && !atomicLoad(stopping_);
  }

  bool start(string engineBinaryPath) {
    atomicStore(stopping_, false);
    try {
      pipes_ = pipeProcess([engineBinaryPath], Redirect.stdin | Redirect.stdout | Redirect
          .stderr);
    } catch (Exception e) {
      const string msg = "Failed to start engine " ~ engineBinaryPath ~ ": " ~ e
        .msg;
      stderr.writeln(msg);
      if (onError !is null && !atomicLoad(stopping_))
        onError(msg);
      return false;
    }
    atomicStore(running_, true);
    readerThread_ = new Thread(&readLoop);
    readerThread_.isDaemon = true;
    readerThread_.start();
    stderrThread_ = new Thread(&stderrLoop);
    stderrThread_.isDaemon = true;
    stderrThread_.start();
    return true;
  }

  void sendRequest(Request req) {
    if (!atomicLoad(running_) || atomicLoad(stopping_))
      return;
    ubyte[] framed = encodeLengthPrefixedRequest(req);
    bool failed = false;
    synchronized (writeMutex_) {
      if (!atomicLoad(running_) || atomicLoad(stopping_))
        return;
      try {
        pipes_.stdin.rawWrite(framed);
        pipes_.stdin.flush();
      } catch (Exception) {
        atomicStore(running_, false);
        failed = true;
      }
    }
    if (failed && onError !is null && !atomicLoad(stopping_)) {
      onError("Engine IPC pipe disconnected while sending command");
    }
  }

  void stop() {
    if (!cas(&stopping_, false, true))
      return;
    if (!atomicLoad(running_)) {
      atomicStore(stopping_, false);
      return;
    }
    synchronized (writeMutex_) {
      try {
        auto quitReq = makeQuitRequest();
        pipes_.stdin.rawWrite(encodeLengthPrefixedRequest(quitReq));
        pipes_.stdin.flush();
      } catch (Exception) {
      }
    }
    atomicStore(running_, false);
    try {
      pipes_.stdin.close();
    } catch (Exception) {
    }
    try {
      wait(pipes_.pid);
    } catch (Exception) {
    }
    if (readerThread_ !is null) {
      try {
        readerThread_.join(false);
      } catch (Exception) {
      }
      readerThread_ = null;
    }
    if (stderrThread_ !is null) {
      try {
        stderrThread_.join(false);
      } catch (Exception) {
      }
      stderrThread_ = null;
    }
    try {
      pipes_.stdout.close();
    } catch (Exception) {
    }
    try {
      pipes_.stderr.close();
    } catch (Exception) {
    }
    atomicStore(stopping_, false);
  }

  private void stderrLoop() {
    import std.string : strip;

    char[2048] chunk;
    string pending;
    while (!atomicLoad(stopping_)) {
      char[] got;
      try {
        got = pipes_.stderr.rawRead(chunk[]);
      } catch (Exception) {
        break;
      }
      if (got.length == 0 || atomicLoad(stopping_))
        break;
      pending ~= got;
      while (!atomicLoad(stopping_)) {
        ptrdiff_t nl = -1;
        foreach (idx, c; pending) {
          if (c == '\n') {
            nl = cast(ptrdiff_t)idx;
            break;
          }
        }
        if (nl < 0)
          break;
        const string line = strip(pending[0 .. nl]).idup;
        pending = pending[nl + 1 .. $];
        if (line.length > 0 && onError !is null && !atomicLoad(stopping_)) {
          onError(line);
        }
      }
    }
    const string tail = strip(pending).idup;
    if (tail.length > 0 && onError !is null && !atomicLoad(stopping_)) {
      onError(tail);
    }
  }

  private void readLoop() {
    ubyte[4096] chunk;
    ubyte[] rxBuf;
    while (atomicLoad(running_) && !atomicLoad(stopping_)) {
      ubyte[] readSlice;
      try {
        readSlice = pipes_.stdout.rawRead(chunk[]);
      } catch (Exception) {
        break;
      }
      if (readSlice.length == 0 || atomicLoad(stopping_))
        break;
      rxBuf ~= readSlice;

      ubyte[] payload;
      while (atomicLoad(running_) && !atomicLoad(stopping_) && extractNextFrame(rxBuf,
          payload)) {
        Notification notif;
        if (decodeProtoOwned(payload, notif) && onNotification_ !is null &&
            atomicLoad(running_) && !atomicLoad(stopping_)) {
          onNotification_(notif);
        }
      }
    }
    atomicStore(running_, false);
  }
}

unittest {
  // 1. Test length-prefixed request encoding
  auto req = makeTransportRequest(TransportCmd.Action.ACTION_PLAY);
  ubyte[] framed = encodeLengthPrefixedRequest(req);
  assert(framed.length > 4);
  uint len = cast(uint)framed[0] | (cast(uint)framed[1] << 8) | (
      cast(uint)framed[2] << 16) | (cast(uint)framed[3] << 24);
  assert(len == framed.length - 4);
  Request decoded;
  assert(decodeProto(framed[4 .. $], decoded));
  assert(decoded.transport.hasValue);
  assert(decoded.transport.value.action == TransportCmd.Action.ACTION_PLAY);

  // 2. Test HBKI frame extraction across fragmented chunks and garbage prefix
  import pb.notifications : PlayheadInfo;

  Notification notif;
  PlayheadInfo pi;
  pi.position_sec = 1.25f;
  pi.bpm = 135.0f;
  notif.playhead_info.set(pi);
  ubyte[] payload = encodeProto(notif);

  ubyte[] stream;
  stream ~= [0x00, 0xFF]; // noise before magic
  stream ~= [
    cast(ubyte)(HBKI_MAGIC & 0xFF), cast(ubyte)((HBKI_MAGIC >> 8) & 0xFF),
    cast(ubyte)((HBKI_MAGIC >> 16) & 0xFF), cast(ubyte)((HBKI_MAGIC >> 24) & 0xFF),
  ];
  const uint plen = cast(uint)payload.length;
  stream ~= [
    cast(ubyte)(plen & 0xFF), cast(ubyte)((plen >> 8) & 0xFF),
    cast(ubyte)((plen >> 16) & 0xFF), cast(ubyte)((plen >> 24) & 0xFF),
  ];
  stream ~= payload;

  // Feed partial first half, expect false
  ubyte[] rx = stream[0 .. 8].dup;
  ubyte[] outP;
  assert(!extractNextFrame(rx, outP));
  // Feed remaining bytes, expect true and matching decoded notification
  rx ~= stream[8 .. $];
  assert(extractNextFrame(rx, outP));
  assert(rx.length == 0);
  Notification decodedNotif;
  assert(decodeProto(outP, decodedNotif));
  assert(decodedNotif.playhead_info.hasValue);
  assert(decodedNotif.playhead_info.value.bpm == 135.0f);

  // 3. Test makeTrackPanRequest & makePluginBypassRequest round-trip
  auto panReq = makeTrackPanRequest(2, -0.4f);
  ubyte[] panFramed = encodeLengthPrefixedRequest(panReq);
  Request panDec;
  assert(decodeProto(panFramed[4 .. $], panDec));
  assert(panDec.track.hasValue);
  assert(panDec.track.value.action == TrackCmd.Action.ACTION_SET_PAN);
  assert(panDec.track.value.target.track_index == 2);

  auto bypReq = makePluginBypassRequest(3, 1, false);
  ubyte[] bypFramed = encodeLengthPrefixedRequest(bypReq);
  Request bypDec;
  assert(decodeProto(bypFramed[4 .. $], bypDec));
  assert(bypDec.plugin.hasValue);
  assert(bypDec.plugin.value.action == PluginCmd.Action.ACTION_SET_BYPASS);
  assert(bypDec.plugin.value.target.track_index == 3);
  assert(bypDec.plugin.value.target.plugin_index.value == 1);
  assert(!bypDec.plugin.value.flag);

  // 4. Test Metronome / Panic / Master Volume / Master Pitch round-trip
  auto rMetro = makeSetMetronomeRequest(true);
  Request metroDec;
  assert(decodeProto(encodeLengthPrefixedRequest(rMetro)[4 .. $], metroDec));
  assert(metroDec.transport.hasValue &&
      metroDec.transport.value.action == TransportCmd.Action.ACTION_SET_METRONOME);
  assert(metroDec.transport.value.enabled);

  auto rPanic = makePanicRequest();
  Request panicDec;
  assert(decodeProto(encodeLengthPrefixedRequest(rPanic)[4 .. $], panicDec));
  assert(panicDec.transport.hasValue &&
      panicDec.transport.value.action == TransportCmd.Action.ACTION_PANIC);

  auto rMv = makeSetMasterVolumeRequest(0.64f);
  Request mvDec;
  assert(decodeProto(encodeLengthPrefixedRequest(rMv)[4 .. $], mvDec));
  assert(mvDec.transport.hasValue &&
      mvDec.transport.value.action == TransportCmd.Action
        .ACTION_SET_MASTER_VOLUME);
  assert(mvDec.transport.value.value > 0.63f && mvDec.transport.value.value < 0.65f);

  auto rMp = makeSetMasterPitchRequest(3.0f);
  Request mpDec;
  assert(decodeProto(encodeLengthPrefixedRequest(rMp)[4 .. $], mpDec));
  assert(mpDec.transport.hasValue &&
      mpDec.transport.value.action == TransportCmd.Action.ACTION_SET_MASTER_PITCH);
  assert(mpDec.transport.value.value == 3.0f);

  // 5. Test Phase 2 SequencerCmd & pattern clip request round-trips
  auto rPatClip = makeAddPatternTimelineClipRequest(2, 3, 1.5f, 8.0f);
  Request patClipDec;
  assert(decodeProto(encodeLengthPrefixedRequest(rPatClip)[4 .. $], patClipDec));
  assert(patClipDec.track.hasValue &&
      patClipDec.track.value.action == TrackCmd.Action.ACTION_ADD_TIMELINE_CLIP);
  assert(patClipDec.track.value.clip_data.path == "pattern://3");

  auto rAddCh = makeSequencerAddChannelRequest(0, "Kick", "builtin://dr8_kick",
      "Drums", 0x00466480, 4);
  Request addChDec;
  assert(decodeProto(encodeLengthPrefixedRequest(rAddCh)[4 .. $], addChDec));
  assert(addChDec.sequencer.hasValue &&
      addChDec.sequencer.value.action == SequencerCmd.Action.ACTION_ADD_CHANNEL);
  assert(addChDec.sequencer.value.uri == "builtin://dr8_kick");
  assert(addChDec.sequencer.value.mixer_insert == 4);

  auto rStep = makeSequencerStepRequest(1, 0, 12, true);
  Request stepDec;
  assert(decodeProto(encodeLengthPrefixedRequest(rStep)[4 .. $], stepDec));
  assert(stepDec.sequencer.hasValue && stepDec.sequencer.value.pattern_index == 1);
  assert(stepDec.sequencer.value.step_index == 12 && stepDec.sequencer.value
      .enabled);

  auto rGraph = makeSequencerStepGraphRequest(0, 2, 5, 1, -0.5f);
  Request graphDec;
  assert(decodeProto(encodeLengthPrefixedRequest(rGraph)[4 .. $], graphDec));
  assert(graphDec.sequencer.hasValue && graphDec.sequencer.value.graph_param == 1);
  assert(graphDec.sequencer.value.value < -0.49f);

  auto rMode = makeSequencerModeRequest(PbPlaybackMode.PLAYBACK_MODE_PAT);
  Request modeDec;
  assert(decodeProto(encodeLengthPrefixedRequest(rMode)[4 .. $], modeDec));
  assert(modeDec.sequencer.hasValue &&
      modeDec.sequencer.value.playback_mode == PbPlaybackMode.PLAYBACK_MODE_PAT);

  // 6. Test Phase 3 Mixer EQ/Sep/Mix, Playlist Move/Resize/Mute/Copy, and Automation requests
  auto rEq = makeTrackEqRequest(3, 0.2f, 0.5f, 0.9f);
  Request eqDec;
  assert(decodeProto(encodeLengthPrefixedRequest(rEq)[4 .. $], eqDec));
  assert(eqDec.track.hasValue && eqDec.track.value.action == TrackCmd.Action
      .ACTION_SET_EQ);
  assert(eqDec.track.value.target.track_index == 3 && eqDec.track.value.eq_high > 0.89f);

  auto rSep = makeTrackStereoSepRequest(2, -0.75f);
  Request sepDec;
  assert(decodeProto(encodeLengthPrefixedRequest(rSep)[4 .. $], sepDec));
  assert(sepDec.track.hasValue && sepDec.track.value.action == TrackCmd.Action
      .ACTION_SET_STEREO_SEP);
  assert(sepDec.track.value.value < -0.74f);

  auto rMix = makePluginMixRequest(1, 2, 0.45f);
  Request mixDec;
  assert(decodeProto(encodeLengthPrefixedRequest(rMix)[4 .. $], mixDec));
  assert(mixDec.plugin.hasValue && mixDec.plugin.value.action == PluginCmd.Action
      .ACTION_SET_MIX);
  assert(mixDec.plugin.value.target.plugin_index.value == 2 &&
      mixDec.plugin.value.param_value < 0.46f);

  auto rMove = makeMoveTimelineClipRequest(1, 0, 4.5f, 3);
  Request moveDec;
  assert(decodeProto(encodeLengthPrefixedRequest(rMove)[4 .. $], moveDec));
  assert(moveDec.track.hasValue &&
      moveDec.track.value.action == TrackCmd.Action.ACTION_MOVE_TIMELINE_CLIP);
  assert(moveDec.track.value.target_track_index == 3 && moveDec.track.value.value == 4.5f);

  auto rRes = makeResizeTimelineClipRequest(2, 1, 12.0f, 4.0f);
  Request resDec;
  assert(decodeProto(encodeLengthPrefixedRequest(rRes)[4 .. $], resDec));
  assert(resDec.track.hasValue && resDec.track.value.clip_data.duration_beats == 12.0f);
  assert(resDec.track.value.clip_data.trim_start_beats == 4.0f);

  auto rMuteClip = makeSetClipMutedRequest(1, 0, true);
  Request muteDec;
  assert(decodeProto(encodeLengthPrefixedRequest(rMuteClip)[4 .. $], muteDec));
  assert(muteDec.track.hasValue &&
      muteDec.track.value.action == TrackCmd.Action.ACTION_SET_CLIP_MUTED);
  assert(muteDec.track.value.flag);

  auto rCopy = makeCopyTimelineClipRequest(1, 0, 8.0f, 2, false);
  Request copyDec;
  assert(decodeProto(encodeLengthPrefixedRequest(rCopy)[4 .. $], copyDec));
  assert(copyDec.track.hasValue &&
      copyDec.track.value.action == TrackCmd.Action.ACTION_COPY_TIMELINE_CLIP);

  auto rAuto = makeAutomationUpdatePointsRequest(0, 0, 1, [
    PbAutoPoint(2.0, 0.8f, 0.0f)
  ]);
  Request autoDec;
  assert(decodeProto(encodeLengthPrefixedRequest(rAuto)[4 .. $], autoDec));
  assert(autoDec.automation.hasValue && autoDec.automation.value.clip_index == 1);
  assert(autoDec.automation.value.points.length == 1);

  // 7. Test Phase 4 remote VST3 editor, sampler WAV load, and DrumPad IPC builders
  auto rFrame = makePluginGetEditorFrameRequest(1, 0);
  Request frameDec;
  assert(decodeProto(encodeLengthPrefixedRequest(rFrame)[4 .. $], frameDec));
  assert(frameDec.plugin.hasValue &&
      frameDec.plugin.value.action == PluginCmd.Action.ACTION_GET_EDITOR_FRAME);

  auto rIn = makePluginSendEditorInputRequest(1, 0, 1, 120, 45, 0, 0, 0);
  Request inDec;
  assert(decodeProto(encodeLengthPrefixedRequest(rIn)[4 .. $], inDec));
  assert(inDec.plugin.hasValue &&
      inDec.plugin.value.action == PluginCmd.Action.ACTION_SEND_EDITOR_INPUT);
  assert(inDec.plugin.value.input_x == 120 && inDec.plugin.value.input_y == 45);

  auto rStopGui = makePluginStopGuiRequest(1, 0);
  Request stopGuiDec;
  assert(decodeProto(encodeLengthPrefixedRequest(rStopGui)[4 .. $], stopGuiDec));
  assert(stopGuiDec.plugin.hasValue &&
      stopGuiDec.plugin.value.action == PluginCmd.Action.ACTION_STOP_GUI);

  auto rSmp = makePluginLoadSampleRequest(100, 0, "testdata/kick.wav");
  Request smpDec;
  assert(decodeProto(encodeLengthPrefixedRequest(rSmp)[4 .. $], smpDec));
  assert(smpDec.plugin.hasValue && smpDec.plugin.value.sample_path == "testdata/kick.wav");

  auto rDp = makeDrumPadLoadSampleRequest(100, 0, 2, "testdata/snare.wav");
  Request dpDec;
  assert(decodeProto(encodeLengthPrefixedRequest(rDp)[4 .. $], dpDec));
  assert(dpDec.drum_pad.hasValue && dpDec.drum_pad.value.pad_index == 2);
  assert(dpDec.drum_pad.value.sample_path == "testdata/snare.wav");

  // 8. Test Phase 5 bounce, collect, settings, device list, and sample preview requests
  auto rBounce = makeProjectBounceRequest("out/mix.wav");
  Request bncDec;
  assert(decodeProto(encodeLengthPrefixedRequest(rBounce)[4 .. $], bncDec));
  assert(bncDec.project.hasValue && bncDec.project.value.action == ProjectCmd
      .Action.ACTION_BOUNCE);
  assert(bncDec.project.value.path == "out/mix.wav");

  auto rCol = makeProjectCollectFilesRequest("proj/collected");
  Request colDec;
  assert(decodeProto(encodeLengthPrefixedRequest(rCol)[4 .. $], colDec));
  assert(colDec.project.hasValue &&
      colDec.project.value.action == ProjectCmd.Action.ACTION_COLLECT_FILES);

  auto rBuf = makeSetAudioBufferSizeRequest(20);
  Request bufDec;
  assert(decodeProto(encodeLengthPrefixedRequest(rBuf)[4 .. $], bufDec));
  assert(bufDec.set_audio_buffer_size.hasValue &&
      bufDec.set_audio_buffer_size.value.buffer_size_ms == 20);

  auto rPrec = makeSetProcessingPrecisionRequest(true);
  Request precDec;
  assert(decodeProto(encodeLengthPrefixedRequest(rPrec)[4 .. $], precDec));
  assert(precDec.set_processing_precision.hasValue &&
      precDec.set_processing_precision.value.use_double);

  auto rModeHost = makeSetPluginHostModeRequest(
      PluginHostMode.PLUGIN_HOST_LOCAL_SANDBOX, ["127.0.0.1:9000"]);
  Request mhDec;
  assert(decodeProto(encodeLengthPrefixedRequest(rModeHost)[4 .. $], mhDec));
  assert(mhDec.set_plugin_host_mode.hasValue &&
      mhDec.set_plugin_host_mode.value.remote_hosts.length == 1);

  auto rAud = makeAudioEditorLoadRequest("testdata/loop140.wav");
  Request audDec;
  assert(decodeProto(encodeLengthPrefixedRequest(rAud)[4 .. $], audDec));
  assert(audDec.audio_editor.hasValue &&
      audDec.audio_editor.value.action == AudioEditorCmd.Action.AE_ACTION_LOAD);

  // 9. Test resynchronization by 1 byte when a false HBKI header with oversized length precedes a real shifted frame
  Notification notif2;
  PlayheadInfo pi2;
  pi2.bpm = 152.0f;
  notif2.playhead_info.set(pi2);
  ubyte[] payload2 = encodeProto(notif2);
  const uint plen2 = cast(uint)payload2.length;

  // Bytes [0..3] = HBKI, byte 4..7 = length > MAX_IPC_FRAME_BYTES, Wait: put a real HBKI at byte offset 1..4!
  // HBKI bytes are 0x49 ('I'), 0x4B ('K'), 0x42 ('B'), 0x48 ('H').
  // Construct buffer starting with 0x49, then immediately HBKI header at offset 1!
  // Wait: if offset 0..3 had 0x49, 0x4B, 0x42, 0x48 AND offset 3..6 had another? No, simpler:
  // Offset 0..3 = HBKI, offset 4..7 = bogus oversized length whose byte [4..7] starts with 'H','B' or offset 2..5 starts HBKI:
  // If we use 0xFF garbage + fake HBKI at index 0 with bogus len where real HBKI starts at index 2 (not aligned to 4!):
  ubyte[] resyncBuf;
  resyncBuf ~= [0x49, 0x4B, 0x42, 0x48]; // bogus HBKI at index 0
  // index 4..7 = 0xFFFFFFFF (> MAX_IPC_FRAME_BYTES); index 5..8 starts a REAL frame if wait:
  // Wait: if offset 1..4 is NOT HBKI, what if index 5..8 is HBKI (shift 1 mod 4 from 4)!
  resyncBuf ~= [0xFF]; // index 4 (high length byte, makes len > 4MiB since uint is 0x424B49FF or similar)
  resyncBuf ~= [0x49, 0x4B, 0x42, 0x48]; // real HBKI at index 5! len bytes at index 5..7 are >= 0x42000000 > MAX
  resyncBuf ~= [
    cast(ubyte)(plen2 & 0xFF), cast(ubyte)((plen2 >> 8) & 0xFF),
    cast(ubyte)((plen2 >> 16) & 0xFF), cast(ubyte)((plen2 >> 24) & 0xFF),
  ];
  resyncBuf ~= payload2;
  ubyte[] outP2;
  assert(extractNextFrame(resyncBuf, outP2),
      "Expected extractNextFrame to recover shifted HBKI frame via 1-byte stride");
  Notification decoded2;
  assert(decodeProto(outP2, decoded2) && decoded2.playhead_info.hasValue);
  assert(decoded2.playhead_info.value.bpm > 151.0f && decoded2.playhead_info
      .value.bpm < 153.0f);

  // 10. Repeated stop() on idle EngineProcessClient must be safely idempotent without firing onError
  bool idleErr = false;
  auto idleClient = new EngineProcessClient((Notification n) {}, (string e) {
    idleErr = true;
  });
  idleClient.stop();
  idleClient.stop();
  assert(!idleErr && !idleClient.isRunning(), "Expected idempotent stop() on unstarted client");
}
