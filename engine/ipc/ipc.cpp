#include "engine/ipc/ipc.hpp"

#include <condition_variable>
#include <iostream>
#include <mutex>
#include <queue>
#include <thread>

#include "engine/plugin/iplugin.hpp"
#include "pb/core.pb.h"
#include "pb/notifications.pb.h"

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
static std::mutex g_stdout_mutex;  // Protects all stdout writes
static const uint32_t IPC_MAGIC =
    0x48424B49;  // "HBKI" - Hibiki IPC magic header

static void senderLoop() {
  while (true) {
    std::vector<uint8_t> msg;
    {
      std::unique_lock<std::mutex> lock(g_queue_mutex);
      g_queue_cv.wait(lock,
                      [] { return !g_msg_queue.empty() || !g_sender_running; });
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
      std::cout.write(reinterpret_cast<const char*>(&msg_size),
                      sizeof(msg_size));
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

// Helper to serialize a Notification and send it
static void sendProto(
    const hibiki::pb::notifications::Notification& notification) {
  std::string data;
  notification.SerializeToString(&data);
  sendNotification(reinterpret_cast<const uint8_t*>(data.data()), data.size());
}

void sendAck(const char* cmd_type, bool success) {
  hibiki::pb::notifications::Notification notification;
  auto* ack = notification.mutable_acknowledge();
  ack->set_command_type(cmd_type);
  ack->set_success(success);
  sendProto(notification);
}

void sendParamList(int track_idx, int plugin_idx,
                   const std::string& plugin_name, bool is_instrument,
                   const std::vector<VstParamInfo>& params) {
  hibiki::pb::notifications::Notification notification;
  auto* list = notification.mutable_param_list();
  list->set_track_index(track_idx);
  list->set_plugin_index(plugin_idx);
  list->set_plugin_name(plugin_name);
  list->set_is_instrument(is_instrument);
  for (const auto& p : params) {
    auto* pi = list->add_params();
    pi->set_id(p.id);
    pi->set_name(p.name);
    pi->set_default_value(p.defaultValue);
  }
  sendProto(notification);
}

void sendLog(const std::string& msg) {
  hibiki::pb::notifications::Notification notification;
  notification.mutable_log()->set_message(msg);
  sendProto(notification);
}

void sendClipInfo(int track_idx, int slot_index, const std::string& name,
                  const std::string& path) {
  hibiki::pb::notifications::Notification notification;
  auto* ci = notification.mutable_clip_info();
  ci->set_track_index(track_idx);
  ci->set_slot_index(slot_index);
  ci->set_name(name);
  ci->set_path(path);
  sendProto(notification);
}

void sendClearProject() {
  hibiki::pb::notifications::Notification notification;
  notification.mutable_clear_project();  // Just set the oneof variant
  sendProto(notification);
}

void sendPluginList(const std::string& path,
                    const std::vector<PluginDescription>& plugins) {
  hibiki::pb::notifications::Notification notification;
  auto* list = notification.mutable_plugin_list();
  list->set_path(path);
  for (const auto& p : plugins) {
    auto* pd = list->add_plugins();
    pd->set_index(p.index);
    pd->set_name(p.name);
    pd->set_vendor(p.vendor);
  }
  sendProto(notification);
}

void sendTimelineClipInfo(int track_idx, int clip_idx, const std::string& name,
                          const std::string& path, float start_time,
                          float duration, const std::vector<float>& waveform) {
  hibiki::pb::notifications::Notification notification;
  auto* tci = notification.mutable_timeline_clip_info();
  tci->set_track_index(track_idx);
  tci->set_clip_index(clip_idx);
  tci->set_name(name);
  tci->set_path(path);
  tci->set_start_time(start_time);
  tci->set_duration(duration);
  for (float w : waveform) {
    tci->add_waveform(w);
  }
  sendProto(notification);
}

void sendPlayheadInfo(float position_sec, float bpm, bool is_playing) {
  hibiki::pb::notifications::Notification notification;
  auto* phi = notification.mutable_playhead_info();
  phi->set_position_sec(position_sec);
  phi->set_bpm(bpm);
  phi->set_transport_state(is_playing
                               ? hibiki::pb::core::TRANSPORT_STATE_PLAYING
                               : hibiki::pb::core::TRANSPORT_STATE_STOPPED);
  sendProto(notification);
}

void sendBounceFinished(const std::string& path, bool success) {
  hibiki::pb::notifications::Notification notification;
  auto* bf = notification.mutable_bounce_finished();
  bf->set_path(path);
  bf->set_success(success);
  sendProto(notification);
}

void sendTrackInfo(int track_idx, const std::string& name) {
  hibiki::pb::notifications::Notification notification;
  auto* ti = notification.mutable_track_info();
  ti->set_track_index(track_idx);
  ti->set_name(name);
  sendProto(notification);
}

void sendClipMidiData(int track_idx, int slot_idx, int clip_idx, int resolution,
                      const std::vector<hibiki::pb::core::MidiEvent>& notes) {
  hibiki::pb::notifications::Notification notification;
  auto* cmd = notification.mutable_clip_midi_data();
  cmd->set_track_index(track_idx);
  cmd->set_slot_index(slot_idx);
  cmd->set_clip_index(clip_idx);
  cmd->set_resolution(resolution);
  for (const auto& n : notes) {
    *cmd->add_events() = n;
  }
  sendProto(notification);
}

void sendParamValueChange(int track_idx, int plugin_idx, uint32_t param_id,
                          float value) {
  hibiki::pb::notifications::Notification notification;
  auto* pvc = notification.mutable_param_value_change();
  pvc->set_track_index(track_idx);
  pvc->set_plugin_index(plugin_idx);
  pvc->set_param_id(param_id);
  pvc->set_value(value);
  sendProto(notification);
}

void sendEditorFrameData(int track_idx, int plugin_idx, int width, int height,
                         const std::vector<uint8_t>& rgba) {
  hibiki::pb::notifications::Notification notification;
  auto* frame = notification.mutable_editor_frame_data();
  frame->set_track_index(track_idx);
  frame->set_plugin_index(plugin_idx);
  frame->set_width(width);
  frame->set_height(height);
  frame->set_image_data(rgba.data(), rgba.size());
  sendProto(notification);
}

void sendPluginSpectrumData(int track_idx, int plugin_idx,
                            const float* input_db, const float* output_db,
                            int num_bins) {
  hibiki::pb::notifications::Notification notification;
  auto* spec = notification.mutable_plugin_spectrum();
  spec->set_track_index(track_idx);
  spec->set_plugin_index(plugin_idx);
  for (int i = 0; i < num_bins; ++i) {
    spec->add_input_magnitudes(input_db[i]);
    spec->add_output_magnitudes(output_db[i]);
  }
  sendProto(notification);
}

void sendPluginMeteringData(int track_idx, int plugin_idx, float input_db,
                            float output_db, float gain_reduction_db) {
  hibiki::pb::notifications::Notification notification;
  auto* meter = notification.mutable_plugin_metering();
  meter->set_track_index(track_idx);
  meter->set_plugin_index(plugin_idx);
  meter->set_input_db(input_db);
  meter->set_output_db(output_db);
  meter->set_gain_reduction_db(gain_reduction_db);
  sendProto(notification);
}

}  // namespace hibiki
