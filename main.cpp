#include <atomic>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "midi.hpp"

#if defined(__APPLE__)
#include "coreaudio_out.hpp"
#elif !defined(_WIN32)
#include "alsa_out.hpp"
#else
#include "win32_out.hpp"
#include <fcntl.h>
#include <io.h>
#endif
#include "vst3_host.hpp"

#include "hibiki_request_generated.h"
#include "hibiki_response_generated.h"
#include "hibiki_project_generated.h"

#include "ipc.hpp"
#include "audio_file.hpp"
#include "clip.hpp"
#include "track.hpp"
#include "project.hpp"

namespace hibiki {

void playback_thread(ProjectState& state) {
#if defined(__APPLE__)
  CoreAudioPlayback alsa(44100, 2);
  float sample_rate = 44100.0f;
  int actual_channels = 2;
#elif !defined(_WIN32)
  AlsaPlayback alsa(44100, 2);
  float sample_rate = 44100.0f;
  int actual_channels = 2;
#else
  Win32Playback alsa(44100, 2);
  float sample_rate = (float)alsa.get_sample_rate();
  int actual_channels = alsa.get_channels();
#endif
    state.sample_rate = (double)sample_rate;
    if (!alsa.is_ready()) return;

    int block_size = 512;

    alignas(32) float bufferL[512];
    alignas(32) float bufferR[512];
    float* outChannels[] = {bufferL, bufferR};

    HostProcessContext context;
    context.sampleRate = sample_rate;
    context.tempo = state.bpm;
    context.timeSigNumerator = 4;
    context.timeSigDenominator = 4;

    double time_per_block = block_size / (double)sample_rate;
    std::vector<float> mixBufferL(block_size);
    std::vector<float> mixBufferR(block_size);
    std::vector<float> interleaved(block_size * actual_channels);

    int level_counter = 0;

    while (!state.quit) {

        std::fill(mixBufferL.begin(), mixBufferL.end(), 0.0f);
        std::fill(mixBufferR.begin(), mixBufferR.end(), 0.0f);

        bool any_playing = false;

        {
            // std::lock_guard<std::mutex> lock(state.tracks_mutex); // Mutex removed from POD
            for (auto& pair : state.tracks) {
                Track* track = pair.second.get();
                // std::lock_guard<std::mutex> tlock(track->mutex);

                if (track->playing_slot == -1) continue;

                any_playing = true;
                auto& clip = track->clips[track->playing_slot];

                context.continuousTimeSamples = track->current_time_sec * sample_rate;
                context.projectTimeMusic = track->current_time_sec * (context.tempo / 60.0);

                std::fill(bufferL, bufferL + block_size, 0.0f);
                std::fill(bufferR, bufferR + block_size, 0.0f);

                if (clip->type == Clip::Type::MIDI) {
                    const auto& events = clip->midi_events;
                    int num_midi_events = events.size();

                    std::vector<MidiNoteEvent> blockEvents;
                    int search_idx = track->current_midi_idx;
                    
                    while (search_idx < num_midi_events) {
                        auto& me = events[search_idx];
                        if (me.seconds >= track->current_time_sec + time_per_block) break;

                        if (me.seconds >= track->current_time_sec) {
                            if (hibiki::isNoteOn(me) || hibiki::isNoteOff(me)) {
                                MidiNoteEvent e;
                                e.sampleOffset = std::max(0, (int)((me.seconds - track->current_time_sec) * sample_rate));
                                if (e.sampleOffset >= block_size) e.sampleOffset = block_size - 1;
                                e.channel = me.channel;
                                e.pitch = me.note;

                                if (hibiki::isNoteOff(me)) {
                                    e.isNoteOn = false;
                                    e.velocity = 0;
                                } else {
                                    e.isNoteOn = true;
                                    e.velocity = me.velocity / 127.0f;
                                }
                                blockEvents.push_back(e);
                            }
                        }
                        search_idx++;
                    }
                    track->current_midi_idx = search_idx;

                    for (size_t i = 0; i < track->plugins.size(); ++i) {
                        auto& p = track->plugins[i];
                        if (i == 0 && p->isInstrument()) {
                            p->process(nullptr, outChannels, block_size, context, blockEvents);
                        } else {
                            p->process(outChannels, outChannels, block_size, context, {});
                        }
                    }
                } else if (clip->type == Clip::Type::AUDIO) {
                    // Simple audio playback
                    int start_sample = (int)(track->current_time_sec * sample_rate);
                    for (int i = 0; i < block_size; ++i) {
                        int sample_pos = start_sample + i;
                        if (clip->num_channels == 2) {
                            if (sample_pos * 2 + 1 < (int)clip->audio_data.size()) {
                                bufferL[i] = clip->audio_data[sample_pos * 2];
                                bufferR[i] = clip->audio_data[sample_pos * 2 + 1];
                            }
                        } else if (clip->num_channels == 1) {
                            if (sample_pos < (int)clip->audio_data.size()) {
                                bufferL[i] = bufferR[i] = clip->audio_data[sample_pos];
                            }
                        }
                    }

                    // Process through effects
                    for (size_t i = 0; i < track->plugins.size(); ++i) {
                        auto& p = track->plugins[i];
                        if (p->isInstrument()) continue; // Audio clips bypass instruments
                        p->process(outChannels, outChannels, block_size, context, {});
                    }
                }

                for (int i = 0; i < block_size; ++i) {
                    mixBufferL[i] += bufferL[i];
                    mixBufferR[i] += bufferR[i];
                }

                track->current_time_sec += time_per_block;
                if (track->current_time_sec >= clip->duration_sec) {
                    if (clip->is_loop) {
                        track->current_time_sec = fmod(track->current_time_sec, clip->duration_sec);
                        track->current_midi_idx = 0; // Reset MIDI search for next block
                    } else {
                        track->playing_slot = -1;
                    }
                }

                // Calculate levels
                float peakL = 0, peakR = 0;
                for (int i = 0; i < block_size; i++) {
                    peakL = std::max(peakL, std::abs(bufferL[i]));
                    peakR = std::max(peakR, std::abs(bufferR[i]));
                }
                if (any_playing) {
                    std::lock_guard<std::mutex> llock(state.levels_mutex);
                    state.track_levels[track->index] = {peakL, peakR};
                }
            }
        }
        
        if (!any_playing) {
            if (state.is_playing) {
                std::lock_guard<std::mutex> llock(state.levels_mutex);
                for (auto& p : state.track_levels) p.second = {0, 0};
                state.is_playing = false;
            }
        } else {
            state.is_playing = true;
        }

        level_counter++;
        if (level_counter >= 10) { // Send levels periodically
            level_counter = 0;
            flatbuffers::FlatBufferBuilder builder(512);
            std::vector<flatbuffers::Offset<hibiki::ipc::TrackLevel>> level_offsets;
            {
                std::lock_guard<std::mutex> llock(state.levels_mutex);
                for (auto& pair : state.track_levels) {
                    level_offsets.push_back(hibiki::ipc::CreateTrackLevel(builder, pair.first, pair.second.first, pair.second.second));
                }
            }
            auto levels_vec = builder.CreateVector(level_offsets);
            auto levels_off = hibiki::ipc::CreateTrackLevels(builder, levels_vec);
            auto nf_off = hibiki::ipc::CreateNotification(builder, hibiki::ipc::Response_TrackLevels, levels_off.Union());
            builder.Finish(nf_off);
            // We need a thread-safe way to send notifications if main thread is also sending
            // For now, assume playback_thread can write to cout too, but we should be careful with mutex
            // Actually main.cpp sendNotification doesn't have a mutex. Let's add one.
            sendNotification(builder.GetBufferPointer(), builder.GetSize());
        }

        if (actual_channels >= 2) {
            for (int i = 0; i < block_size; ++i) {
                interleaved[i * actual_channels + 0] = mixBufferL[i];
                interleaved[i * actual_channels + 1] = mixBufferR[i];
                for (int c = 2; c < actual_channels; ++c) {
                    interleaved[i * actual_channels + c] = 0.0f;
                }
            }
        } else {
            // Mono
            for (int i = 0; i < block_size; ++i) {
                interleaved[i] = (mixBufferL[i] + mixBufferR[i]) * 0.5f;
            }
        }

        alsa.write(interleaved, block_size);
    }
}

} // namespace hibiki

int main(int argc, char** argv) {
    if (argc >= 2 && std::string(argv[1]) == "--list") {
        if (argc < 3) return 1;
        Vst3Plugin::listPlugins(argv[2]);
        return 0;
    }

#ifdef _WIN32
  // Ensure binary mode for IPC on Windows
  _setmode(_fileno(stdin), _O_BINARY);
  _setmode(_fileno(stdout), _O_BINARY);
#endif

    hibiki::ProjectState state;
    std::thread audio_thread(hibiki::playback_thread, std::ref(state));

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
            std::lock_guard<std::mutex> lock(state.tracks_mutex);
            auto track = hibiki::GetOrCreateTrack(state, tidx);
            int target_idx = track->LoadPlugin(vpath, pidx, state.sample_rate);
            if (target_idx != -1) {
                std::vector<VstParamInfo> params;
                auto& plugin = track->plugins[target_idx];
                for (int i = 0; i < plugin->getParameterCount(); ++i) {
                    VstParamInfo info;
                    if (plugin->getParameterInfo(i, info)) {
                        params.push_back(info);
                    }
                }
                hibiki::sendParamList(tidx, target_idx, plugin->getName(), plugin->isInstrument(), params);
            } else {
                hibiki::sendLog("Failed to load plugin: " + vpath);
            }
        } else if (command_type == hibiki::ipc::Command_SaveProject) {
            auto cmd = request->command_as_SaveProject();
            std::lock_guard<std::mutex> lock(state.tracks_mutex);
            hibiki::SaveProject(state, cmd->path()->str());
            hibiki::sendAck("SAVE_PROJECT", true);
        } else if (command_type == hibiki::ipc::Command_LoadProject) {
            auto cmd = request->command_as_LoadProject();
            std::lock_guard<std::mutex> lock(state.tracks_mutex);
            hibiki::LoadProject(state, cmd->path()->str());
            hibiki::sendAck("LOAD_PROJECT", true);
        } else if (command_type == hibiki::ipc::Command_LoadClip) {
            auto cmd = request->command_as_LoadClip();
            int tidx = cmd->track_index();
            int sidx = cmd->slot_index();
            std::string mpath = cmd->path()->str();
            bool is_loop = cmd->is_loop();
            std::lock_guard<std::mutex> lock(state.tracks_mutex);
            auto track = hibiki::GetOrCreateTrack(state, tidx);
            if (track->LoadClip(sidx, mpath, is_loop)) {
                hibiki::sendAck("LOAD_CLIP", true);
                // Extract filename from path
                std::string name = mpath;
                size_t last_slash = mpath.find_last_of("/\\");
                if (last_slash != std::string::npos) {
                    name = mpath.substr(last_slash + 1);
                }
                hibiki::sendClipInfo(tidx, sidx, name);
            } else {
                hibiki::sendLog("Failed to load clip: " + mpath);
            }
        } else if (command_type == hibiki::ipc::Command_SetClipLoop) {
            auto cmd = request->command_as_SetClipLoop();
            std::lock_guard<std::mutex> lock(state.tracks_mutex);
            hibiki::GetOrCreateTrack(state, cmd->track_index())->SetClipLoop(cmd->slot_index(), cmd->is_loop());
            hibiki::sendAck("SET_CLIP_LOOP", true);
        } else if (command_type == hibiki::ipc::Command_Play) {
            hibiki::sendAck("PLAY", true);
        } else if (command_type == hibiki::ipc::Command_Stop) {
            std::lock_guard<std::mutex> lock(state.tracks_mutex);
            for (auto& pair : state.tracks) {
                pair.second->Stop();
            }
            hibiki::sendAck("STOP", true);
        } else if (command_type == hibiki::ipc::Command_PlayClip) {
            auto cmd = request->command_as_PlayClip();
            int tidx = cmd->track_index();
            int sidx = cmd->slot_index();
            std::lock_guard<std::mutex> lock(state.tracks_mutex);
            auto track = hibiki::GetOrCreateTrack(state, tidx);
            track->PlayClip(sidx);
            hibiki::sendAck("PLAY_CLIP", true);
        } else if (command_type == hibiki::ipc::Command_StopTrack) {
            auto cmd = request->command_as_StopTrack();
            int tidx = cmd->track_index();
            std::lock_guard<std::mutex> lock(state.tracks_mutex);
            auto track = hibiki::GetOrCreateTrack(state, tidx);
            track->Stop();
            hibiki::sendAck("STOP_TRACK", true);
        } else if (command_type == hibiki::ipc::Command_RemovePlugin) {
            auto cmd = request->command_as_RemovePlugin();
            int tidx = cmd->track_index();
            int pidx = cmd->plugin_index();
            std::lock_guard<std::mutex> lock(state.tracks_mutex);
            auto track = hibiki::GetOrCreateTrack(state, tidx);
            if (track->RemovePlugin(pidx)) {
                hibiki::sendAck("REMOVE_PLUGIN", true);
            } else {
                hibiki::sendAck("REMOVE_PLUGIN", false);
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
        // Disable Scrub, UpdateParams, ClearProject routing to track temporary
        // } else if (command_type == hibiki::ipc::Command_UpdateParams) {
        //     auto cmd = request->command_as_UpdateParams();
        //     std::lock_guard<std::mutex> lock(state.tracks_mutex);
        //     auto track = hibiki::GetOrCreateTrack(state, cmd->track_index());
        //     if (cmd->plugin_index() < (int)track->plugins.size()) {
        //         track->plugins[cmd->plugin_index()]->setParameterValue(cmd->param_index(), cmd->value());
        //     }
        // } else if (command_type == hibiki::ipc::Command_ClearProject) {
        //     std::lock_guard<std::mutex> lock(state.tracks_mutex);
        //     state.tracks.clear();
        //     hibiki::sendAck("CLEAR_PROJECT", true);
        // } else if (command_type == hibiki::ipc::Command_Scrub) {
        //     auto cmd = request->command_as_Scrub();
        //     std::lock_guard<std::mutex> lock(state.tracks_mutex);
        //     for (auto& pair : state.tracks) {
        //         pair.second->Scrub(cmd->time_sec());
        //     }
        //     hibiki::sendAck("SCRUB", true);
        } else if (command_type == hibiki::ipc::Command_SetBpm) {
            auto cmd = request->command_as_SetBpm();
            state.bpm = cmd->bpm();
            hibiki::sendAck("SET_BPM", true);
        } else if (command_type == hibiki::ipc::Command_PlayScene) {
            auto cmd = request->command_as_PlayScene();
            int sidx = cmd->slot_index();
            std::lock_guard<std::mutex> lock(state.tracks_mutex);
            for (auto& pair : state.tracks) {
                pair.second->PlayClip(sidx);
            }
            hibiki::sendAck("PLAY_SCENE", true);
        } else if (command_type == hibiki::ipc::Command_DeleteClip) {
            auto cmd = request->command_as_DeleteClip();
            int track_idx = cmd->track_index();
            int slot_index = cmd->slot_index();
            if (hibiki::GetOrCreateTrack(state, track_idx)->DeleteClip(slot_index)) {
                hibiki::sendAck("DELETE_CLIP", true);
                hibiki::sendClipInfo(track_idx, slot_index, "");
            } else {
                hibiki::sendAck("DELETE_CLIP", false);
            }
        } else if (command_type == hibiki::ipc::Command_Quit) {
            state.quit = true;
            break;
        }
    }

    if (audio_thread.joinable()) audio_thread.join();
    return 0;
}
