#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "vst3_host.hpp" // For VstParamInfo

namespace ipc {

void sendNotification(const uint8_t* buf, size_t size);
void sendAck(const char* cmd_type, bool success);
void sendParamList(int track_idx, int plugin_idx, const std::string& plugin_name, bool is_instrument, const std::vector<VstParamInfo>& params);
void sendLog(const std::string& msg);
void sendClipInfo(int track_idx, int slot_index, const std::string& name);
void sendClearProject();

} // namespace ipc
