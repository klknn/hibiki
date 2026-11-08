module pb.plugin_worker;

import dproto.proto : ProtoField, ProtoOptional;

struct LoadPlugin {
    @ProtoField(1) string path;
    @ProtoField(2) int plugin_index = 0;
    @ProtoField(3) double sample_rate = 0.0;
}

struct MidiEvent {
    @ProtoField(1) int sample_offset = 0;
    @ProtoField(2) uint channel = 0;
    @ProtoField(3) uint pitch = 0;
    @ProtoField(4) float velocity = 0.0f;
    @ProtoField(5) bool is_note_on = false;
}

struct ProcessAudio {
    @ProtoField(1) int num_samples = 0;
    @ProtoField(2) MidiEvent[] midi_events;
    @ProtoField(3) double sample_rate = 0.0;
    @ProtoField(4) double tempo = 0.0;
    @ProtoField(5) int time_sig_numerator = 0;
    @ProtoField(6) int time_sig_denominator = 0;
    @ProtoField(7) long continuous_time_samples = 0;
    @ProtoField(8) double project_time_music = 0.0;
    @ProtoField(9) bool has_inputs = false;
    @ProtoField(10) ubyte[] input_audio;
}

struct SetParam {
    @ProtoField(1) uint param_id = 0;
    @ProtoField(2) double value = 0.0;
}

struct GetParam {
    @ProtoField(1) uint param_id = 0;
}

struct GetParamCount {}

struct GetParamInfo {
    @ProtoField(1) int index = 0;
}

struct GetPluginInfo {}

struct ListPlugins {
    @ProtoField(1) string search_path;
}

struct Shutdown {}

struct ShowEditor {
    @ProtoField(1) bool headless = false;
}

struct StopEditor {}

struct GetEditorFrame {}

struct EditorInput {
    enum Type : int {
        MOUSE_MOVE = 0,
        MOUSE_DOWN = 1,
        MOUSE_UP = 2,
        KEY_DOWN = 3,
        KEY_UP = 4,
        MOUSE_WHEEL = 5,
    }

    @ProtoField(1) Type type = Type.MOUSE_MOVE;
    @ProtoField(2) int x = 0;
    @ProtoField(3) int y = 0;
    @ProtoField(4) int button = 0;
    @ProtoField(5) int key_code = 0;
    @ProtoField(6) int delta = 0;
}

struct WorkerConfig {
    @ProtoField(1) int block_size = 0;
    @ProtoField(2) int num_channels = 0;
    @ProtoField(3) bool use_shared_memory = false;
}

struct GetState {}

struct SetState {
    @ProtoField(1) ubyte[] state;
}

struct StateResult {
    @ProtoField(1) bool success = false;
    @ProtoField(2) ubyte[] state;
}

struct WorkerRequest {
    @ProtoField(1, true, "command") ProtoOptional!LoadPlugin load;
    @ProtoField(2, true, "command") ProtoOptional!ProcessAudio process;
    @ProtoField(3, true, "command") ProtoOptional!SetParam set_param;
    @ProtoField(4, true, "command") ProtoOptional!GetParam get_param;
    @ProtoField(5, true, "command") ProtoOptional!GetParamCount get_param_count;
    @ProtoField(6, true, "command") ProtoOptional!GetParamInfo get_param_info;
    @ProtoField(7, true, "command") ProtoOptional!GetPluginInfo get_plugin_info;
    @ProtoField(8, true, "command") ProtoOptional!Shutdown shutdown;
    @ProtoField(9, true, "command") ProtoOptional!ListPlugins list_plugins;
    @ProtoField(10, true, "command") ProtoOptional!WorkerConfig config;
    @ProtoField(11, true, "command") ProtoOptional!ShowEditor show_editor;
    @ProtoField(12, true, "command") ProtoOptional!StopEditor stop_editor;
    @ProtoField(13, true, "command") ProtoOptional!GetEditorFrame get_editor_frame;
    @ProtoField(14, true, "command") ProtoOptional!EditorInput editor_input;
    @ProtoField(15, true, "command") ProtoOptional!GetState get_state;
    @ProtoField(16, true, "command") ProtoOptional!SetState set_state;
}

struct LoadResult {
    @ProtoField(1) bool success = false;
    @ProtoField(2) string name;
    @ProtoField(3) bool is_instrument = false;
    @ProtoField(4) string error;
}

struct ProcessDone {
    @ProtoField(1) ubyte[] output_audio;
}

struct ParamValue {
    @ProtoField(1) double value = 0.0;
}

struct ParamCountResult {
    @ProtoField(1) int count = 0;
}

struct ParamInfoResult {
    @ProtoField(1) bool found = false;
    @ProtoField(2) uint id = 0;
    @ProtoField(3) string name;
    @ProtoField(4) double default_value = 0.0;
}

struct PluginInfoResult {
    @ProtoField(1) string name;
    @ProtoField(2) string path;
    @ProtoField(3) int plugin_index = 0;
    @ProtoField(4) bool is_instrument = false;
}

struct ListPluginsResult {
    @ProtoField(1) PluginInfoResult[] plugins;
    @ProtoField(2) bool is_complete = false;
}

struct ConfigAck {}

struct EditorResult {
    @ProtoField(1) bool success = false;
    @ProtoField(2) int width = 0;
    @ProtoField(3) int height = 0;
}

struct EditorFrame {
    @ProtoField(1) int width = 0;
    @ProtoField(2) int height = 0;
    @ProtoField(3) ubyte[] image_data;
}

struct WorkerResponse {
    @ProtoField(1, true, "result") ProtoOptional!LoadResult load_result;
    @ProtoField(2, true, "result") ProtoOptional!ProcessDone process_done;
    @ProtoField(3, true, "result") ProtoOptional!ParamValue param_value;
    @ProtoField(4, true, "result") ProtoOptional!ParamCountResult param_count;
    @ProtoField(5, true, "result") ProtoOptional!ParamInfoResult param_info;
    @ProtoField(6, true, "result") ProtoOptional!PluginInfoResult plugin_info;
    @ProtoField(7, true, "result") ProtoOptional!ListPluginsResult list_plugins_result;
    @ProtoField(8, true, "result") ProtoOptional!ConfigAck config_ack;
    @ProtoField(9, true, "result") ProtoOptional!EditorResult editor_result;
    @ProtoField(10, true, "result") ProtoOptional!EditorFrame editor_frame;
    @ProtoField(11, true, "result") ProtoOptional!StateResult state_result;
}
