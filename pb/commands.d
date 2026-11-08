module pb.commands;

import dproto.proto : ProtoField, ProtoOptional;
import pb.core : AutomationPoint, Clip, EntityRef, MidiEvent;

struct TransportCmd {
    enum Action : int {
        ACTION_UNSPECIFIED = 0,
        ACTION_PLAY = 1,
        ACTION_STOP = 2,
        ACTION_SEEK = 3,
        ACTION_RECORD = 4,
        ACTION_SET_LOOP = 5,
    }

    @ProtoField(1) Action action = Action.ACTION_UNSPECIFIED;
    @ProtoField(2) float seek_pos = 0.0f;
    @ProtoField(3) uint scene_index = 0;
    @ProtoField(4) bool loop_enabled = false;
    @ProtoField(5) float loop_start = 0.0f;
    @ProtoField(6) float loop_end = 0.0f;
}

struct TrackCmd {
    enum Action : int {
        ACTION_UNSPECIFIED = 0,
        ACTION_PLAY_SLOT = 1,
        ACTION_STOP = 2,
        ACTION_LOAD_CLIP = 3,
        ACTION_DELETE_CLIP = 4,
        ACTION_SET_CLIP_LOOP = 5,
        ACTION_ADD_TIMELINE_CLIP = 6,
        ACTION_REMOVE_TIMELINE_CLIP = 7,
        ACTION_RESIZE_TIMELINE_CLIP = 8,
        ACTION_ARM_RECORD = 9,
        ACTION_SET_INPUT_DEVICE = 10,
        ACTION_SET_MIDI_INPUT = 11,
        ACTION_SET_RECORD_MODE = 12,
        ACTION_SET_VOLUME = 13,
        ACTION_SET_PAN = 14,
        ACTION_SET_MUTE = 15,
        ACTION_SET_SOLO = 16,
        ACTION_MOVE_TIMELINE_CLIP = 17,
        ACTION_COPY_TIMELINE_CLIP = 18,
        ACTION_SET_CLIP_FADE = 19,
        ACTION_BOUNCE_IN_PLACE = 20,
        ACTION_SET_CLIP_MUTED = 21,
        ACTION_SET_TRACK_TYPE = 22,
        ACTION_SET_OUTPUT_ROUTING = 23,
        ACTION_SET_AUX_SEND = 24,
        ACTION_SET_GROUP_PARENT = 25,
    }

    @ProtoField(1) Action action = Action.ACTION_UNSPECIFIED;
    @ProtoField(2) EntityRef target;
    @ProtoField(3) Clip clip_data;
    @ProtoField(4) float value = 0.0f;
    @ProtoField(5) bool flag = false;
    @ProtoField(6) string input_device_id;
    @ProtoField(7) uint input_channel_start = 0;
    @ProtoField(8) bool input_stereo = false;
    @ProtoField(9) string midi_input_device_id;
    @ProtoField(10) int record_mode = 0;
    @ProtoField(11) int target_track_index = 0;
    @ProtoField(12) float fade_out_sec = 0.0f;
    @ProtoField(13) float bounce_tail_sec = 0.0f;
    @ProtoField(14) int aux_track_index = 0;
    @ProtoField(15) float send_level = 0.0f;
    @ProtoField(16) bool pre_fader = false;
}

struct PluginCmd {
    enum Action : int {
        ACTION_UNSPECIFIED = 0,
        ACTION_LOAD = 1,
        ACTION_REMOVE = 2,
        ACTION_SHOW_GUI = 3,
        ACTION_SET_PARAM = 4,
        ACTION_LIST = 5,
        ACTION_GET_EDITOR_FRAME = 6,
        ACTION_SEND_EDITOR_INPUT = 7,
        ACTION_STOP_GUI = 8,
        ACTION_LOAD_SAMPLE = 9,
        ACTION_SET_SIDECHAIN = 10,
        ACTION_SET_BYPASS = 11,
        ACTION_REORDER_PLUGIN = 12,
    }

    @ProtoField(1) Action action = Action.ACTION_UNSPECIFIED;
    @ProtoField(2) EntityRef target;
    @ProtoField(3) string path;
    @ProtoField(4) uint param_id = 0;
    @ProtoField(5) float param_value = 0.0f;
    @ProtoField(6) int input_type = 0;
    @ProtoField(7) int input_x = 0;
    @ProtoField(8) int input_y = 0;
    @ProtoField(9) int input_button = 0;
    @ProtoField(10) int input_key = 0;
    @ProtoField(11) int input_delta = 0;
    @ProtoField(12) string remote_host;
    @ProtoField(13) string[] paths;
    @ProtoField(14) string sample_path;
    @ProtoField(15) int sidechain_track_index = 0;
    @ProtoField(16) bool flag = false;
    @ProtoField(17) int target_plugin_index = 0;
}

struct AutomationCmd {
    enum Action : int {
        ACTION_UNSPECIFIED = 0,
        ACTION_ADD_LANE = 1,
        ACTION_REMOVE_LANE = 2,
        ACTION_UPDATE_POINTS = 3,
        ACTION_GET_LANES = 4,
        ACTION_ADD_CLIP = 5,
        ACTION_REMOVE_CLIP = 6,
        ACTION_MOVE_CLIP = 7,
        ACTION_RESIZE_CLIP = 8,
        ACTION_RENAME_CLIP = 9,
    }

    @ProtoField(1) Action action = Action.ACTION_UNSPECIFIED;
    @ProtoField(2) EntityRef target;
    @ProtoField(3) uint param_id = 0;
    @ProtoField(4) AutomationPoint[] points;
    @ProtoField(5) uint clip_index = 0;
    @ProtoField(6) float start_time_sec = 0.0f;
    @ProtoField(7) float duration_beats = 0.0f;
    @ProtoField(8) string clip_name;
}

struct MidiCmd {
    enum Action : int {
        ACTION_UNSPECIFIED = 0,
        ACTION_GET = 1,
        ACTION_UPDATE = 2,
        ACTION_PANIC = 3,
    }

    @ProtoField(1) Action action = Action.ACTION_UNSPECIFIED;
    @ProtoField(2) EntityRef target;
    @ProtoField(3) uint resolution = 0;
    @ProtoField(4) MidiEvent[] events;
}

struct ProjectCmd {
    enum Action : int {
        ACTION_UNSPECIFIED = 0,
        ACTION_SAVE = 1,
        ACTION_LOAD = 2,
        ACTION_BOUNCE = 3,
        ACTION_UNDO = 4,
        ACTION_REDO = 5,
        ACTION_QUIT = 6,
        ACTION_SET_BPM = 7,
        ACTION_COLLECT_FILES = 8,
    }

    @ProtoField(1) Action action = Action.ACTION_UNSPECIFIED;
    @ProtoField(2) string path;
    @ProtoField(3) float bpm = 0.0f;
}

enum PluginHostMode : int {
    PLUGIN_HOST_IN_PROCESS = 0,
    PLUGIN_HOST_LOCAL_SANDBOX = 1,
}

struct SetPluginHostMode {
    @ProtoField(1) PluginHostMode mode = PluginHostMode.PLUGIN_HOST_IN_PROCESS;
    @ProtoField(2) string[] remote_hosts;
}

struct ScanRemotePlugins {
    @ProtoField(1) string[] remote_hosts;
}

struct SetAudioBufferSize {
    @ProtoField(1) int buffer_size_ms = 0;
}

struct HibikiConfig {
    @ProtoField(1) PluginHostMode plugin_host_mode = PluginHostMode.PLUGIN_HOST_IN_PROCESS;
    @ProtoField(2) string[] remote_hosts;
    @ProtoField(3) int buffer_latency_ms = 0;
    @ProtoField(4) bool use_double_precision = false;
}

struct ListAudioInputs {}

struct ListMidiInputs {}

struct SendVirtualMidi {
    @ProtoField(1) int track_index = 0;
    @ProtoField(2) uint note = 0;
    @ProtoField(3) uint velocity = 0;
    @ProtoField(4) bool note_on = false;
}

struct ModulationCmd {
    enum Action : int {
        ACTION_UNSPECIFIED = 0,
        ACTION_ADD = 1,
        ACTION_REMOVE = 2,
        ACTION_CONFIGURE = 3,
        ACTION_ASSIGN = 4,
    }

    @ProtoField(1) Action action = Action.ACTION_UNSPECIFIED;
    @ProtoField(2) EntityRef target;
    @ProtoField(3) int slot_index = 0;
    @ProtoField(4) int waveform = 0;
    @ProtoField(5) float rate_hz = 0.0f;
    @ProtoField(6) float depth = 0.0f;
    @ProtoField(7) uint target_param_id = 0;
    @ProtoField(8) bool sync_to_tempo = false;
}

struct RestartWorkers {}

struct SetProcessingPrecision {
    @ProtoField(1) bool use_double = false;
}

struct AudioEditorCmd {
    enum Action : int {
        AE_ACTION_UNSPECIFIED = 0,
        AE_ACTION_LOAD = 1,
        AE_ACTION_NORMALIZE = 2,
        AE_ACTION_REVERSE = 3,
        AE_ACTION_FADE_IN = 4,
        AE_ACTION_FADE_OUT = 5,
        AE_ACTION_TRIM = 6,
        AE_ACTION_SAVE = 7,
        AE_ACTION_GAIN = 8,
        AE_ACTION_CONVOLVE = 9,
        AE_ACTION_PREVIEW_PLAY = 10,
        AE_ACTION_PREVIEW_STOP = 11,
        AE_ACTION_BLUR = 12,
        AE_ACTION_APPLY_TO_CLIP = 13,
    }

    @ProtoField(1) Action action = Action.AE_ACTION_UNSPECIFIED;
    @ProtoField(2) string path;
    @ProtoField(3) float selection_start = 0.0f;
    @ProtoField(4) float selection_end = 0.0f;
    @ProtoField(5) float value = 0.0f;
    @ProtoField(6) string impulse_path;
    @ProtoField(7) float conv_dry = 0.0f;
    @ProtoField(8) float conv_wet = 0.0f;
    @ProtoField(9) bool conv_add_tail = false;
    @ProtoField(10) int track_index = 0;
    @ProtoField(11) int clip_index = 0;
    @ProtoField(12) float blur_amount = 0.0f;
    @ProtoField(13) int blur_envelope = 0;
}

struct DrumPadCmd {
    enum Action : int {
        ACTION_UNSPECIFIED = 0,
        ACTION_LOAD_PLUGIN = 1,
        ACTION_REMOVE_PLUGIN = 2,
        ACTION_SET_PARAM = 3,
        ACTION_LOAD_SAMPLE = 4,
        ACTION_SET_VOLUME = 5,
        ACTION_SET_PAN = 6,
        ACTION_SET_MUTE = 7,
        ACTION_SET_SOLO = 8,
        ACTION_SET_TRIGGER_NOTE = 9,
        ACTION_SHOW_GUI = 10,
    }

    @ProtoField(1) Action action = Action.ACTION_UNSPECIFIED;
    @ProtoField(2) uint track_index = 0;
    @ProtoField(3) uint plugin_index = 0;
    @ProtoField(4) uint pad_index = 0;
    @ProtoField(5) string plugin_path;
    @ProtoField(6) uint param_id = 0;
    @ProtoField(7) float param_value = 0.0f;
    @ProtoField(8) string sample_path;
    @ProtoField(9) uint trigger_note = 0;
    @ProtoField(10) bool target_effect = false;
    @ProtoField(11) uint effect_index = 0;
}

struct Request {
    @ProtoField(40, true, "command") ProtoOptional!TransportCmd transport;
    @ProtoField(41, true, "command") ProtoOptional!TrackCmd track;
    @ProtoField(42, true, "command") ProtoOptional!PluginCmd plugin;
    @ProtoField(43, true, "command") ProtoOptional!AutomationCmd automation;
    @ProtoField(44, true, "command") ProtoOptional!MidiCmd midi;
    @ProtoField(45, true, "command") ProtoOptional!ProjectCmd project;
    @ProtoField(46, true, "command") ProtoOptional!SetPluginHostMode set_plugin_host_mode;
    @ProtoField(47, true, "command") ProtoOptional!ScanRemotePlugins scan_remote_plugins;
    @ProtoField(48, true, "command") ProtoOptional!SetAudioBufferSize set_audio_buffer_size;
    @ProtoField(49, true, "command") ProtoOptional!ListAudioInputs list_audio_inputs;
    @ProtoField(50, true, "command") ProtoOptional!ListMidiInputs list_midi_inputs;
    @ProtoField(51, true, "command") ProtoOptional!SendVirtualMidi send_virtual_midi;
    @ProtoField(52, true, "command") ProtoOptional!ModulationCmd modulation;
    @ProtoField(53, true, "command") ProtoOptional!RestartWorkers restart_workers;
    @ProtoField(54, true, "command") ProtoOptional!SetProcessingPrecision set_processing_precision;
    @ProtoField(55, true, "command") ProtoOptional!AudioEditorCmd audio_editor;
    @ProtoField(56, true, "command") ProtoOptional!DrumPadCmd drum_pad;
}
