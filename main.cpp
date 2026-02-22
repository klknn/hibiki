#include <atomic>
#include <chrono>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "midi.hpp"

#include "alsa_out.hpp"
#include "vst3_host.hpp"

#include "hibiki_request_generated.h"
#include "hibiki_response_generated.h"

struct Clip {
    std::vector<hbk::MidiEvent> events;
};

class Track {
public:
    int index;
    std::vector<std::unique_ptr<Vst3Plugin>> plugins;
    std::map<int, std::unique_ptr<Clip>> clips;

    int playing_slot = -1;
    double current_time_sec = 0.0;
    int current_midi_idx = 0;

    std::mutex mutex;

    Track(int idx) : index(idx) {}

    bool load_plugin(const std::string& path, int plugin_index) {
        std::lock_guard<std::mutex> lock(mutex);
        auto plugin = std::make_unique<Vst3Plugin>();
        if (!plugin->load(path, plugin_index)) {
            return false;
        }
        plugins.push_back(std::move(plugin));

        // If this is the first plugin, reset playback state
        if (plugins.size() == 1) {
            current_time_sec = 0.0;
            current_midi_idx = 0;
        }
        return true;
    }



    bool load_clip(int slot, const std::string& path) {
        std::lock_guard<std::mutex> lock(mutex);
        auto events = hbk::parseMidi(path);
        if (events.empty()) return false;

        auto clip = std::make_unique<Clip>();
        clip->events = std::move(events);
        clips[slot] = std::move(clip);

        // If we are currently playing this slot, reset playback
        if (playing_slot == slot) {
            current_time_sec = 0.0;
            current_midi_idx = 0;
        }
        return true;
    }


    void play_clip(int slot) {
        std::lock_guard<std::mutex> lock(mutex);
        if (clips.count(slot)) {
            playing_slot = slot;
            current_time_sec = 0.0;
            current_midi_idx = 0;
        }
    }

    void stop() {
        std::lock_guard<std::mutex> lock(mutex);
        playing_slot = -1;
    }

    bool remove_plugin(size_t pidx) {
        std::lock_guard<std::mutex> lock(mutex);
        if (pidx >= plugins.size()) return false;
        plugins.erase(plugins.begin() + pidx);
        return true;
    }
};

struct GlobalState {
    std::atomic<bool> quit{false};
    std::map<int, std::unique_ptr<Track>> tracks;
    std::mutex tracks_mutex;

    Track* get_or_create_track(int idx) {
        std::lock_guard<std::mutex> lock(tracks_mutex);
        if (!tracks.count(idx)) {
            tracks[idx] = std::make_unique<Track>(idx);
        }
        return tracks[idx].get();
    }
};

void playback_thread(GlobalState& state) {
    AlsaPlayback alsa(44100, 2);
    if (!alsa.is_ready()) return;

    int block_size = 512;
    float sample_rate = 44100.0f;
    int num_channels = 2;

    alignas(32) float bufferL[512];
    alignas(32) float bufferR[512];
    float* outChannels[] = {bufferL, bufferR};

    HostProcessContext context;
    context.sampleRate = sample_rate;
    context.tempo = 120.0;
    context.timeSigNumerator = 4;
    context.timeSigDenominator = 4;

    double time_per_block = block_size / (double)sample_rate;
    std::vector<float> mixBufferL(block_size);
    std::vector<float> mixBufferR(block_size);
    std::vector<float> interleaved(block_size * num_channels);

    while (!state.quit) {

        std::fill(mixBufferL.begin(), mixBufferL.end(), 0.0f);
        std::fill(mixBufferR.begin(), mixBufferR.end(), 0.0f);

        bool any_playing = false;

        {
            std::lock_guard<std::mutex> lock(state.tracks_mutex);
            for (auto& pair : state.tracks) {
                Track* track = pair.second.get();
                std::lock_guard<std::mutex> tlock(track->mutex);

                if (track->plugins.empty() || track->playing_slot == -1) continue;

                any_playing = true;
                auto& clip = track->clips[track->playing_slot];
                const auto& events = clip->events;
                int num_midi_events = events.size();

                context.continuousTimeSamples = track->current_time_sec * sample_rate;
                context.projectTimeMusic = track->current_time_sec * (context.tempo / 60.0);

                std::vector<MidiNoteEvent> blockEvents;
                while (track->current_midi_idx < num_midi_events) {
                    auto& me = events[track->current_midi_idx];
                    if (me.seconds >= track->current_time_sec + time_per_block) break;

                    if (hbk::isNoteOn(me) || hbk::isNoteOff(me)) {
                        MidiNoteEvent e;
                        e.sampleOffset = std::max(0, (int)((me.seconds - track->current_time_sec) * sample_rate));
                        if (e.sampleOffset >= block_size) e.sampleOffset = block_size - 1;
                        e.channel = me.channel;
                        e.pitch = me.note;

                        if (hbk::isNoteOff(me)) {
                            e.isNoteOn = false;
                            e.velocity = 0;
                        } else {
                            e.isNoteOn = true;
                            e.velocity = me.velocity / 127.0f;
                        }
                        blockEvents.push_back(e);
                    }
                    track->current_midi_idx++;
                }

                std::fill(bufferL, bufferL + block_size, 0.0f);
                std::fill(bufferR, bufferR + block_size, 0.0f);

                for (size_t i = 0; i < track->plugins.size(); ++i) {
                    auto& p = track->plugins[i];
                    if (i == 0) {
                        // First plugin gets the MIDI events
                        p->process(nullptr, outChannels, block_size, context, blockEvents);
                    } else {
                        // Subsequent plugins get previous audio output as input
                        p->process(outChannels, outChannels, block_size, context, {});
                    }
                }


                for (int i = 0; i < block_size; ++i) {
                    mixBufferL[i] += bufferL[i];
                    mixBufferR[i] += bufferR[i];
                }

                track->current_time_sec += time_per_block;
                if (track->current_midi_idx >= num_midi_events) {
                    track->playing_slot = -1; // Stop at end of clip for now
                }
            }
        }

        if (!any_playing) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        for (int i = 0; i < block_size; ++i) {
            interleaved[i * 2 + 0] = mixBufferL[i];
            interleaved[i * 2 + 1] = mixBufferR[i];
        }

        alsa.write(interleaved, block_size);
    }
}

void sendNotification(const uint8_t* buf, size_t size) {
    uint32_t msg_size = static_cast<uint32_t>(size);
    std::cout.write(reinterpret_cast<const char*>(&msg_size), sizeof(msg_size));
    std::cout.write(reinterpret_cast<const char*>(buf), size);
    std::cout.flush();
}

void sendAck(const char* cmd_type, bool success) {
    flatbuffers::FlatBufferBuilder builder(128);
    auto cmd_type_off = builder.CreateString(cmd_type);
    auto ack_off = hibiki::ipc::CreateAcknowledge(builder, cmd_type_off, success);
    auto nf_off = hibiki::ipc::CreateNotification(builder, hibiki::ipc::Response_Acknowledge, ack_off.Union());
    builder.Finish(nf_off);
    sendNotification(builder.GetBufferPointer(), builder.GetSize());
}

void sendParamList(int track_idx, int plugin_idx, const std::vector<VstParamInfo>& params) {
    flatbuffers::FlatBufferBuilder builder(1024);
    std::vector<flatbuffers::Offset<hibiki::ipc::ParamInfo>> param_offsets;
    for (const auto& p : params) {
        auto name_off = builder.CreateString(p.name);
        param_offsets.push_back(hibiki::ipc::CreateParamInfo(builder, p.id, name_off, p.defaultValue));
    }
    auto params_vec = builder.CreateVector(param_offsets);
    auto list_off = hibiki::ipc::CreateParamList(builder, track_idx, plugin_idx, params_vec);
    auto nf_off = hibiki::ipc::CreateNotification(builder, hibiki::ipc::Response_ParamList, list_off.Union());
    builder.Finish(nf_off);
    sendNotification(builder.GetBufferPointer(), builder.GetSize());
}

void sendLog(const std::string& msg) {
    flatbuffers::FlatBufferBuilder builder(512);
    auto msg_off = builder.CreateString(msg);
    auto log_off = hibiki::ipc::CreateLog(builder, msg_off);
    auto nf_off = hibiki::ipc::CreateNotification(builder, hibiki::ipc::Response_Log, log_off.Union());
    builder.Finish(nf_off);
    sendNotification(builder.GetBufferPointer(), builder.GetSize());
}

void sendClipInfo(int track_idx, int slot_index, const std::string& name) {
    flatbuffers::FlatBufferBuilder builder(512);
    auto name_off = builder.CreateString(name);
    auto clip_off = hibiki::ipc::CreateClipInfo(builder, track_idx, slot_index, name_off);
    auto nf_off = hibiki::ipc::CreateNotification(builder, hibiki::ipc::Response_ClipInfo, clip_off.Union());
    builder.Finish(nf_off);
    sendNotification(builder.GetBufferPointer(), builder.GetSize());
}

int main(int argc, char** argv) {
    if (argc >= 2 && std::string(argv[1]) == "--list") {
        if (argc < 3) return 1;
        Vst3Plugin::listPlugins(argv[2]);
        return 0;
    }

    GlobalState state;
    std::thread audio_thread(playback_thread, std::ref(state));

    while (true) {
        uint32_t msg_size = 0;
        std::cin.read(reinterpret_cast<char*>(&msg_size), sizeof(msg_size));
        if (std::cin.eof()) break;
        if (std::cin.fail()) {
            std::cerr << "BACKEND ERROR: Failed to read message size from stdin" << std::endl;
            break;
        }

        if (msg_size > 1024 * 1024) { // 1MB limit for safety
            std::cerr << "BACKEND ERROR: Message size too large: " << msg_size << std::endl;
            break;
        }

        std::unique_ptr<uint8_t[]> buffer(new uint8_t[msg_size]);
        std::cin.read(reinterpret_cast<char*>(buffer.get()), msg_size);
        if (std::cin.fail()) {
            std::cerr << "BACKEND ERROR: Failed to read message payload from stdin" << std::endl;
            break;
        }

        auto request = hibiki::ipc::GetRequest(buffer.get());
        auto command_type = request->command_type();

        if (command_type == hibiki::ipc::Command_LoadPlugin) {
            auto cmd = request->command_as_LoadPlugin();
            int tidx = cmd->track_index();
            std::string vpath = cmd->path()->str();
            int pidx = cmd->plugin_index();
            auto track = state.get_or_create_track(tidx);
            if (track->load_plugin(vpath, pidx)) {
                std::vector<VstParamInfo> params;
                auto& plugin = track->plugins.back();
                for (int i = 0; i < plugin->getParameterCount(); ++i) {
                    VstParamInfo info;
                    if (plugin->getParameterInfo(i, info)) {
                        params.push_back(info);
                    }
                }
                sendParamList(tidx, (int)track->plugins.size() - 1, params);
            } else {
                sendLog("Failed to load plugin: " + vpath);
            }
        } else if (command_type == hibiki::ipc::Command_LoadClip) {
            auto cmd = request->command_as_LoadClip();
            int tidx = cmd->track_index();
            int sidx = cmd->slot_index();
            std::string mpath = cmd->path()->str();
            if (state.get_or_create_track(tidx)->load_clip(sidx, mpath)) {
                sendAck("LOAD_CLIP", true);
                // Extract filename from path
                std::string name = mpath;
                size_t last_slash = mpath.find_last_of("/\\");
                if (last_slash != std::string::npos) {
                    name = mpath.substr(last_slash + 1);
                }
                sendClipInfo(tidx, sidx, name);
            } else {
                sendLog("Failed to load clip: " + mpath);
            }
        } else if (command_type == hibiki::ipc::Command_Play) {
            sendAck("PLAY", true);
        } else if (command_type == hibiki::ipc::Command_Stop) {
            std::lock_guard<std::mutex> lock(state.tracks_mutex);
            for (auto& pair : state.tracks) {
                pair.second->stop();
            }
            sendAck("STOP", true);
        } else if (command_type == hibiki::ipc::Command_PlayClip) {
            auto cmd = request->command_as_PlayClip();
            int tidx = cmd->track_index();
            int sidx = cmd->slot_index();
            state.get_or_create_track(tidx)->play_clip(sidx);
            sendAck("PLAY_CLIP", true);
        } else if (command_type == hibiki::ipc::Command_StopTrack) {
            auto cmd = request->command_as_StopTrack();
            int tidx = cmd->track_index();
            state.get_or_create_track(tidx)->stop();
            sendAck("STOP_TRACK", true);
        } else if (command_type == hibiki::ipc::Command_RemovePlugin) {
            auto cmd = request->command_as_RemovePlugin();
            int tidx = cmd->track_index();
            int pidx = cmd->plugin_index();
            if (state.get_or_create_track(tidx)->remove_plugin(pidx)) {
                sendAck("REMOVE_PLUGIN", true);
            } else {
                sendAck("REMOVE_PLUGIN", false);
            }
        } else if (command_type == hibiki::ipc::Command_ShowPluginGui) {
            auto cmd = request->command_as_ShowPluginGui();
            int track_idx = cmd->track_index();
            int plugin_idx = cmd->plugin_index();
            std::lock_guard<std::mutex> lock(state.tracks_mutex);
            if (state.tracks.count(track_idx)) {
                auto& plugins = state.tracks[track_idx]->plugins;
                if (plugin_idx >= 0 && plugin_idx < (int)plugins.size()) {
                    plugins[plugin_idx]->showEditor();
                }
            }
        } else if (command_type == hibiki::ipc::Command_SetParamValue) {
            auto cmd = request->command_as_SetParamValue();
            int track_idx = cmd->track_index();
            int plugin_idx = cmd->plugin_index();
            uint32_t param_id = cmd->param_id();
            float value = cmd->value();
            std::lock_guard<std::mutex> lock(state.tracks_mutex);
            if (state.tracks.count(track_idx)) {
                auto& plugins = state.tracks[track_idx]->plugins;
                if (plugin_idx >= 0 && plugin_idx < (int)plugins.size()) {
                    plugins[plugin_idx]->setParameterValue(param_id, value);
                }
            }
        } else if (command_type == hibiki::ipc::Command_Quit) {
            state.quit = true;
            break;
        }
    }

    if (audio_thread.joinable()) audio_thread.join();
    return 0;
}
