#include "engine/ipc/ipc.hpp"

#include <condition_variable>
#include <cstring>
#include <iostream>
#include <mutex>
#include <queue>
#include <thread>

#include "engine/plugin/iplugin.hpp"
#include "pb/core.pb.h"
#include "pb/notifications.pb.h"

extern "C" {
int d_encode_ack(const char* cmd_type, size_t cmd_len, bool success,
                 uint8_t* out_buf, int cap);
int d_encode_log(const char* msg, size_t msg_len, uint8_t* out_buf, int cap);
int d_encode_clip_info(int track_idx, int slot_idx, const char* name,
                       size_t name_len, const char* path, size_t path_len,
                       uint8_t* out_buf, int cap);
int d_encode_clear_project(uint8_t* out_buf, int cap);
int d_encode_playhead_info(float pos_sec, float bpm, bool is_playing,
                           bool loop_enabled, float loop_start, float loop_end,
                           uint8_t* out_buf, int cap);
int d_encode_bounce_finished(const char* path, size_t path_len, bool success,
                             uint8_t* out_buf, int cap);
int d_encode_track_info(int track_idx, const char* name, size_t name_len,
                        uint8_t* out_buf, int cap);
int d_encode_param_value_change(int track_idx, int plugin_idx,
                                uint32_t param_id, float value,
                                uint8_t* out_buf, int cap);
int d_encode_plugin_spectrum(int track_idx, int plugin_idx, const float* in_db,
                             const float* out_db, int num_bins,
                             uint8_t* out_buf, int cap);
}

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

static NotificationHandler g_custom_handler = nullptr;
static std::mutex g_handler_mutex;

void setNotificationHandler(NotificationHandler handler) {
  std::lock_guard<std::mutex> lock(g_handler_mutex);
  g_custom_handler = std::move(handler);
}

void sendNotification(const uint8_t* buf, size_t size) {
  if (!g_ipc_enabled) return;
  {
    std::lock_guard<std::mutex> lock(g_handler_mutex);
    if (g_custom_handler) {
      g_custom_handler(buf, size);
      return;
    }
  }
  ensureSenderThread();
  {
    std::lock_guard<std::mutex> lock(g_queue_mutex);
    g_msg_queue.emplace(buf, buf + size);
  }
  g_queue_cv.notify_one();
}

// Helper to serialize a Notification in C++ (for complex/variable types)
static void sendProto(
    const hibiki::pb::notifications::Notification& notification) {
  std::string data;
  notification.SerializeToString(&data);
  sendNotification(reinterpret_cast<const uint8_t*>(data.data()), data.size());
}

template <typename Fn>
static void sendDEncoded(Fn&& encode_fn) {
  uint8_t buf[16384];
  int written = encode_fn(buf, static_cast<int>(sizeof(buf)));
  if (written > 0) {
    sendNotification(buf, static_cast<size_t>(written));
  }
}

void sendAck(const char* cmd_type, bool success) {
  size_t len = cmd_type ? std::strlen(cmd_type) : 0;
  sendDEncoded([&](uint8_t* b, int cap) {
    return d_encode_ack(cmd_type, len, success, b, cap);
  });
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
  sendDEncoded([&](uint8_t* b, int cap) {
    return d_encode_log(msg.data(), msg.size(), b, cap);
  });
}

void sendClipInfo(int track_idx, int slot_index, const std::string& name,
                  const std::string& path) {
  sendDEncoded([&](uint8_t* b, int cap) {
    return d_encode_clip_info(track_idx, slot_index, name.data(), name.size(),
                              path.data(), path.size(), b, cap);
  });
}

void sendClearProject() {
  sendDEncoded(
      [&](uint8_t* b, int cap) { return d_encode_clear_project(b, cap); });
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
    pd->set_is_instrument(p.is_instrument);
  }
  sendProto(notification);
}

void sendTimelineClipInfo(int track_idx, int clip_idx, const std::string& name,
                          const std::string& path, float start_time,
                          float duration, const std::vector<float>& waveform,
                          bool is_looped, int alias_source, float loop_interval,
                          float fade_in_sec, float fade_out_sec, bool muted) {
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
  tci->set_is_looped(is_looped);
  tci->set_alias_source(alias_source);
  tci->set_loop_interval(loop_interval);
  tci->set_fade_in_sec(fade_in_sec);
  tci->set_fade_out_sec(fade_out_sec);
  tci->set_muted(muted);
  sendProto(notification);
}

void sendPlayheadInfo(float position_sec, float bpm, bool is_playing,
                      bool loop_enabled, float loop_start, float loop_end) {
  sendDEncoded([&](uint8_t* b, int cap) {
    return d_encode_playhead_info(position_sec, bpm, is_playing, loop_enabled,
                                  loop_start, loop_end, b, cap);
  });
}

void sendBounceFinished(const std::string& path, bool success) {
  sendDEncoded([&](uint8_t* b, int cap) {
    return d_encode_bounce_finished(path.data(), path.size(), success, b, cap);
  });
}

void sendTrackInfo(int track_idx, const std::string& name) {
  sendDEncoded([&](uint8_t* b, int cap) {
    return d_encode_track_info(track_idx, name.data(), name.size(), b, cap);
  });
}

// Extended version with routing info
void sendTrackInfoFull(
    int track_idx, const std::string& name, int track_type,
    int output_track_index,
    const std::vector<std::tuple<int, float, bool>>& aux_sends,
    int group_parent_index) {
  hibiki::pb::notifications::Notification notification;
  auto* ti = notification.mutable_track_info();
  ti->set_track_index(track_idx);
  ti->set_name(name);
  ti->set_track_type(static_cast<hibiki::pb::core::TrackType>(track_type));
  ti->set_output_track_index(output_track_index);
  ti->set_group_parent_index(group_parent_index);
  for (const auto& [aux_idx, level, pre] : aux_sends) {
    auto* as = ti->add_aux_sends();
    as->set_aux_track_index(aux_idx);
    as->set_level(level);
    as->set_pre_fader(pre);
  }
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
  sendDEncoded([&](uint8_t* b, int cap) {
    return d_encode_param_value_change(track_idx, plugin_idx, param_id, value,
                                       b, cap);
  });
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
  sendDEncoded([&](uint8_t* b, int cap) {
    return d_encode_plugin_spectrum(track_idx, plugin_idx, input_db, output_db,
                                    num_bins, b, cap);
  });
}

void sendPluginMeteringData(
    const hibiki::pb::notifications::PluginMeteringData& meter_data) {
  hibiki::pb::notifications::Notification notification;
  *notification.mutable_plugin_metering() = meter_data;
  sendProto(notification);
}

}  // namespace hibiki
