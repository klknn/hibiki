#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "engine/plugin/iplugin.hpp"
#include "pb/core.pb.h"
#include "pb/notifications.pb.h"

namespace hibiki {

extern bool g_ipc_enabled;
void sendNotification(const uint8_t* buf, size_t size);
void sendAck(const char* cmd_type, bool success);
void sendParamList(int track_idx, int plugin_idx,
                   const std::string& plugin_name, bool is_instrument,
                   const std::vector<VstParamInfo>& params);
void sendLog(const std::string& msg);
void sendClipInfo(int track_idx, int slot_index, const std::string& name,
                  const std::string& path);
void sendClearProject();
void sendPluginList(const std::string& path,
                    const std::vector<PluginDescription>& plugins);
void sendTimelineClipInfo(int track_idx, int clip_idx, const std::string& name,
                          const std::string& path, float start_time,
                          float duration, const std::vector<float>& waveform,
                          bool is_looped = false, int alias_source = -1,
                          float loop_interval = 0.0f);
void sendPlayheadInfo(float position_sec, float bpm, bool is_playing,
                      bool loop_enabled = false, float loop_start = 0,
                      float loop_end = 0);
void sendBounceFinished(const std::string& path, bool success);
void sendTrackInfo(int track_idx, const std::string& name);

// Send clip MIDI data to GUI. Use slot_idx >= 0 for session clips, clip_idx >=
// 0 for timeline clips
void sendClipMidiData(int track_idx, int slot_idx, int clip_idx, int resolution,
                      const std::vector<hibiki::pb::core::MidiEvent>& notes);

// Send live parameter value change to GUI
void sendParamValueChange(int track_idx, int plugin_idx, uint32_t param_id,
                          float value);

// Send editor framebuffer data to GUI
void sendEditorFrameData(int track_idx, int plugin_idx, int width, int height,
                         const std::vector<uint8_t>& rgba);

// Send plugin spectrum data for EQ visualization
void sendPluginSpectrumData(int track_idx, int plugin_idx,
                            const float* input_db, const float* output_db,
                            int num_bins);

// Send plugin metering data for compressor visualization
void sendPluginMeteringData(int track_idx, int plugin_idx, float input_db,
                            float output_db, float gain_reduction_db,
                            float sidechain_db = -200.0f);

}  // namespace hibiki
