#pragma once

#include <vector>
#include <string>
#include <cstdint>
#include <cstddef>

#include "vst3_host.hpp"

namespace hibiki {

extern bool g_ipc_enabled;
void sendNotification(const uint8_t* buf, size_t size);
void sendAck(const char* cmd_type, bool success);
void sendParamList(int track_idx, int plugin_idx, const std::string& plugin_name, bool is_instrument, const std::vector<VstParamInfo>& params);
void sendLog(const std::string& msg);
void sendClipInfo(int track_idx, int slot_index, const std::string& name, const std::string& path);
void sendClearProject();
void sendPluginList(const std::string& path, const std::vector<PluginDescription>& plugins);
void sendTimelineClipInfo(int track_idx, int clip_idx, const std::string& name, const std::string& path, float start_time, float duration, const std::vector<float>& waveform);
void sendPlayheadInfo(float position_sec, float bpm, bool is_playing);
void sendBounceFinished(const std::string& path, bool success);
void sendTrackInfo(int track_idx, const std::string& name);
 
} // namespace hibiki
