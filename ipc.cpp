#include "ipc.hpp"
#include "vst3_host.hpp"
#include <iostream>
#include <mutex>
#include <queue>
#include <thread>
#include <condition_variable>
#include "hibiki_request_generated.h"
#include "hibiki_response_generated.h"

namespace hibiki {

bool g_ipc_enabled = true;

// Async IPC queue to avoid blocking the audio thread on stdout writes.
// The audio thread pushes serialized messages into a queue, and a dedicated
// sender thread drains it to stdout asynchronously.
static std::mutex g_queue_mutex;
static std::condition_variable g_queue_cv;
static std::queue<std::vector<uint8_t>> g_msg_queue;
static bool g_sender_running = false;
static std::thread g_sender_thread;
static std::mutex g_stdout_mutex; // Protects all stdout writes
static const uint32_t IPC_MAGIC = 0x48424B49; // "HBKI" - Hibiki IPC magic header

static void senderLoop() {
    while (true) {
        std::vector<uint8_t> msg;
        {
            std::unique_lock<std::mutex> lock(g_queue_mutex);
            g_queue_cv.wait(lock, [] { return !g_msg_queue.empty() || !g_sender_running; });
            if (!g_sender_running && g_msg_queue.empty()) break;
            msg = std::move(g_msg_queue.front());
            g_msg_queue.pop();
        }
        // Protect stdout writes with mutex to prevent corruption
        {
            std::lock_guard<std::mutex> cout_lock(g_stdout_mutex);
            uint32_t magic = IPC_MAGIC;
            uint32_t msg_size = static_cast<uint32_t>(msg.size());
            std::cout.write(reinterpret_cast<const char*>(&magic), sizeof(magic));
            std::cout.write(reinterpret_cast<const char*>(&msg_size), sizeof(msg_size));
            std::cout.write(reinterpret_cast<const char*>(msg.data()), msg.size());
            std::cout.flush();
        }
    }
}

static void ensureSenderThread() {
    if (!g_sender_running) {
        g_sender_running = true;
        g_sender_thread = std::thread(senderLoop);
        g_sender_thread.detach();
    }
}

void sendNotification(const uint8_t* buf, size_t size) {
    if (!g_ipc_enabled) return;
    ensureSenderThread();
    {
        std::lock_guard<std::mutex> lock(g_queue_mutex);
        g_msg_queue.emplace(buf, buf + size);
    }
    g_queue_cv.notify_one();
}

void sendAck(const char* cmd_type, bool success) {
    flatbuffers::FlatBufferBuilder builder(128);
    auto cmd_type_off = builder.CreateString(cmd_type);
    auto ack_off = hibiki::ipc::CreateAcknowledge(builder, cmd_type_off, success);
    auto nf_off = hibiki::ipc::CreateNotification(builder, hibiki::ipc::Response_Acknowledge, ack_off.Union());
    builder.Finish(nf_off);
    sendNotification(builder.GetBufferPointer(), builder.GetSize());
}

void sendParamList(int track_idx, int plugin_idx, const std::string& plugin_name, bool is_instrument, const std::vector<VstParamInfo>& params) {
    flatbuffers::FlatBufferBuilder builder(1024);
    std::vector<flatbuffers::Offset<hibiki::ipc::ParamInfo>> param_offsets;
    for (const auto& p : params) {
        auto name_off = builder.CreateString(p.name.c_str());
        param_offsets.push_back(hibiki::ipc::CreateParamInfo(builder, p.id, name_off, p.defaultValue));
    }
    auto params_vec = builder.CreateVector(param_offsets);
    auto name_off = builder.CreateString(plugin_name.c_str());
    auto list_off = hibiki::ipc::CreateParamList(builder, track_idx, plugin_idx, name_off, is_instrument, params_vec);
    auto nf_off = hibiki::ipc::CreateNotification(builder, hibiki::ipc::Response_ParamList, list_off.Union());
    builder.Finish(nf_off);
    sendNotification(builder.GetBufferPointer(), builder.GetSize());
}

void sendLog(const std::string& msg) {
    flatbuffers::FlatBufferBuilder builder(512);
    auto msg_off = builder.CreateString(msg.c_str());
    auto log_off = hibiki::ipc::CreateLog(builder, msg_off);
    auto nf_off = hibiki::ipc::CreateNotification(builder, hibiki::ipc::Response_Log, log_off.Union());
    builder.Finish(nf_off);
    sendNotification(builder.GetBufferPointer(), builder.GetSize());
}

void sendClipInfo(int track_idx, int slot_index, const std::string& name, const std::string& path) {
    flatbuffers::FlatBufferBuilder builder(512);
    auto name_off = builder.CreateString(name.c_str());
    auto path_off = builder.CreateString(path.c_str());
    auto clip_off = hibiki::ipc::CreateClipInfo(builder, track_idx, slot_index, name_off, path_off);
    auto nf_off = hibiki::ipc::CreateNotification(builder, hibiki::ipc::Response_ClipInfo, clip_off.Union());
    builder.Finish(nf_off);
    sendNotification(builder.GetBufferPointer(), builder.GetSize());
}

void sendClearProject() {
    flatbuffers::FlatBufferBuilder builder(128);
    auto clear_off = hibiki::ipc::CreateClearProject(builder);
    auto nf_off = hibiki::ipc::CreateNotification(builder, hibiki::ipc::Response_ClearProject, clear_off.Union());
    builder.Finish(nf_off);
    sendNotification(builder.GetBufferPointer(), builder.GetSize());
}

void sendPluginList(const std::string& path, const std::vector<PluginDescription>& plugins) {
    flatbuffers::FlatBufferBuilder builder(2048);
    std::vector<flatbuffers::Offset<hibiki::ipc::PluginDescription>> plugin_offsets;
    for (const auto& p : plugins) {
        auto name_off = builder.CreateString(p.name);
        auto vendor_off = builder.CreateString(p.vendor);
        plugin_offsets.push_back(hibiki::ipc::CreatePluginDescription(builder, p.index, name_off, vendor_off));
    }
    auto plugins_vec = builder.CreateVector(plugin_offsets);
    auto path_off = builder.CreateString(path);
    auto list_off = hibiki::ipc::CreatePluginList(builder, path_off, plugins_vec);
    auto nf_off = hibiki::ipc::CreateNotification(builder, hibiki::ipc::Response_PluginList, list_off.Union());
    builder.Finish(nf_off);
    sendNotification(builder.GetBufferPointer(), builder.GetSize());
}

void sendTimelineClipInfo(int track_idx, int clip_idx, const std::string& name, const std::string& path, float start_time, float duration, const std::vector<float>& waveform) {
    flatbuffers::FlatBufferBuilder builder(1024 + waveform.size() * 4);
    auto name_off = builder.CreateString(name.c_str());
    auto path_off = builder.CreateString(path.c_str());
    auto wf_off = builder.CreateVector(waveform);
    auto timeline_off = hibiki::ipc::CreateTimelineClipInfo(builder, track_idx, clip_idx, name_off, path_off, start_time, duration, wf_off);
    auto nf_off = hibiki::ipc::CreateNotification(builder, hibiki::ipc::Response_TimelineClipInfo, timeline_off.Union());
    builder.Finish(nf_off);
    sendNotification(builder.GetBufferPointer(), builder.GetSize());
}

void sendPlayheadInfo(float position_sec, float bpm, bool is_playing) {
    flatbuffers::FlatBufferBuilder builder(128);
    auto playhead_off = hibiki::ipc::CreatePlayheadInfo(builder, position_sec, bpm, is_playing);
    auto nf_off = hibiki::ipc::CreateNotification(builder, hibiki::ipc::Response_PlayheadInfo, playhead_off.Union());
    builder.Finish(nf_off);
    sendNotification(builder.GetBufferPointer(), builder.GetSize());
}

void sendBounceFinished(const std::string& path, bool success) {
    flatbuffers::FlatBufferBuilder builder(512);
    auto path_off = builder.CreateString(path.c_str());
    auto bf_off = hibiki::ipc::CreateBounceFinished(builder, path_off, success);
    auto nf_off = hibiki::ipc::CreateNotification(builder, hibiki::ipc::Response_BounceFinished, bf_off.Union());
    builder.Finish(nf_off);
    sendNotification(builder.GetBufferPointer(), builder.GetSize());
}

void sendTrackInfo(int track_idx, const std::string& name) {
    flatbuffers::FlatBufferBuilder builder(512);
    auto name_off = builder.CreateString(name.c_str());
    auto ti_off = hibiki::ipc::CreateTrackInfo(builder, track_idx, name_off);
    auto nf_off = hibiki::ipc::CreateNotification(builder, hibiki::ipc::Response_TrackInfo, ti_off.Union());
    builder.Finish(nf_off);
    sendNotification(builder.GetBufferPointer(), builder.GetSize());
}

void sendClipMidiData(int track_idx, int slot_idx, int clip_idx, int resolution, const std::vector<MidiNote>& notes) {
    flatbuffers::FlatBufferBuilder builder(1024 + notes.size() * 32);
    std::vector<flatbuffers::Offset<hibiki::ipc::MidiEventData>> event_offsets;
    for (const auto& n : notes) {
        event_offsets.push_back(hibiki::ipc::CreateMidiEventData(builder, n.tick, n.pitch, n.duration_ticks, n.velocity));
    }
    auto events_vec = builder.CreateVector(event_offsets);
    auto midi_off = hibiki::ipc::CreateClipMidiData(builder, track_idx, slot_idx, clip_idx, resolution, events_vec);
    auto nf_off = hibiki::ipc::CreateNotification(builder, hibiki::ipc::Response_ClipMidiData, midi_off.Union());
    builder.Finish(nf_off);
    sendNotification(builder.GetBufferPointer(), builder.GetSize());
}

} // namespace hibiki
