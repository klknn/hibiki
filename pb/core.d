module pb.core;

import dproto.proto : ProtoField, ProtoOptional;

enum ClipType : int {
    CLIP_TYPE_UNSPECIFIED = 0,
    CLIP_TYPE_MIDI = 1,
    CLIP_TYPE_AUDIO = 2,
    CLIP_TYPE_AUTOMATION = 3,
}

enum TrackType : int {
    TRACK_NORMAL = 0,
    TRACK_GROUP = 1,
    TRACK_AUX = 2,
}

enum TransportState : int {
    TRANSPORT_STATE_UNSPECIFIED = 0,
    TRANSPORT_STATE_STOPPED = 1,
    TRANSPORT_STATE_PLAYING = 2,
    TRANSPORT_STATE_RECORDING = 3,
}

struct AutomationPoint {
    @ProtoField(1) float time_beats = 0.0f;
    @ProtoField(2) float value = 0.0f;
    @ProtoField(3) float tension = 0.0f;
}

struct MidiEvent {
    @ProtoField(1) long tick = 0;
    @ProtoField(2) uint pitch = 0;
    @ProtoField(3) long duration_ticks = 0;
    @ProtoField(4) uint velocity = 0;
    @ProtoField(5) uint cc_number = 0;
    @ProtoField(6) int cc_value = 0;
}

struct ParamInfo {
    @ProtoField(1) uint id = 0;
    @ProtoField(2) string name;
    @ProtoField(3) float default_value = 0.0f;
    @ProtoField(4) float current_value = 0.0f;
}

struct PadEffectState {
    @ProtoField(1) string effect_path;
    @ProtoField(2) ParamInfo[] params;
}

struct DrumPadState {
    @ProtoField(1) uint pad_index = 0;
    @ProtoField(2) string plugin_path;
    @ProtoField(3) ParamInfo[] params;
    @ProtoField(4) float volume = 0.0f;
    @ProtoField(5) float pan = 0.0f;
    @ProtoField(6) bool mute = false;
    @ProtoField(7) bool solo = false;
    @ProtoField(8) string sample_path;
    @ProtoField(9) ProtoOptional!uint trigger_note;
    @ProtoField(10) string effect_path;
    @ProtoField(11) ParamInfo[] effect_params;
    @ProtoField(12) PadEffectState[] effects;
}

struct DrumMachineState {
    @ProtoField(1) DrumPadState[] pads;
}

struct Plugin {
    @ProtoField(1) uint plugin_index = 0;
    @ProtoField(2) string path;
    @ProtoField(3) uint sub_plugin_index = 0;
    @ProtoField(4) string name;
    @ProtoField(5) string vendor;
    @ProtoField(6) bool is_instrument = false;
    @ProtoField(7) ParamInfo[] params;
    @ProtoField(8) ProtoOptional!int sidechain_track_index;
    @ProtoField(9) ubyte[] state;
    @ProtoField(10) ProtoOptional!DrumMachineState drum_machine_state;
}

struct Clip {
    @ProtoField(1) ClipType type = ClipType.CLIP_TYPE_UNSPECIFIED;
    @ProtoField(2) string path;
    @ProtoField(3) string name;
    @ProtoField(4) bool is_loop = false;
    @ProtoField(5) uint resolution = 0;
    @ProtoField(6) float duration_beats = 0.0f;
    @ProtoField(7) float duration_sec = 0.0f;
    @ProtoField(8) MidiEvent[] midi_events;
    @ProtoField(9) float[] waveform_summary;
    @ProtoField(10) AutomationPoint[] automation_points;
    @ProtoField(11) float trim_start_beats = 0.0f;
}

struct TimelineClip {
    @ProtoField(1) uint clip_index = 0;
    @ProtoField(2) Clip clip;
    @ProtoField(3) float start_time_sec = 0.0f;
    @ProtoField(4) int alias_source = 0;
    @ProtoField(5) float fade_in_sec = 0.0f;
    @ProtoField(6) float fade_out_sec = 0.0f;
    @ProtoField(7) bool muted = false;
    @ProtoField(8) float duration_beats = 0.0f;
    @ProtoField(9) float loop_interval_beats = 0.0f;
}

struct AutomationLane {
    @ProtoField(1) uint lane_id = 0;
    @ProtoField(2) uint plugin_index = 0;
    @ProtoField(3) uint param_id = 0;
    @ProtoField(4) string param_name;
    @ProtoField(5) TimelineClip[] clips;
}

struct SessionSlot {
    @ProtoField(1) uint slot_index = 0;
    @ProtoField(2) Clip clip;
}

struct AuxSend {
    @ProtoField(1) int aux_track_index = 0;
    @ProtoField(2) float level = 0.0f;
    @ProtoField(3) bool pre_fader = false;
}

struct Track {
    @ProtoField(1) uint track_index = 0;
    @ProtoField(2) string name;
    @ProtoField(3) Plugin[] plugins;
    @ProtoField(4) SessionSlot[] session_slots;
    @ProtoField(5) TimelineClip[] timeline_clips;
    @ProtoField(6) AutomationLane[] automation_lanes;
    @ProtoField(7) float peak_l = 0.0f;
    @ProtoField(8) float peak_r = 0.0f;
    @ProtoField(9) bool record_armed = false;
    @ProtoField(10) string input_device_id;
    @ProtoField(11) uint input_channel_start = 0;
    @ProtoField(12) bool input_stereo = false;
    @ProtoField(13) string midi_input_device_id;
    @ProtoField(14) TrackType track_type = TrackType.TRACK_NORMAL;
    @ProtoField(15) int output_track_index = 0;
    @ProtoField(16) AuxSend[] aux_sends;
    @ProtoField(17) int group_parent_index = 0;
}

struct Project {
    @ProtoField(1) float bpm = 0.0f;
    @ProtoField(2) float playhead_sec = 0.0f;
    @ProtoField(3) TransportState transport_state = TransportState.TRANSPORT_STATE_UNSPECIFIED;
    @ProtoField(4) Track[] tracks;
}

struct EntityRef {
    @ProtoField(1) uint track_index = 0;
    @ProtoField(2, true, "target") ProtoOptional!uint session_slot;
    @ProtoField(3, true, "target") ProtoOptional!uint timeline_clip;
    @ProtoField(4, true, "target") ProtoOptional!uint plugin_index;
    @ProtoField(5, true, "target") ProtoOptional!uint lane_index;
}
