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
#include "history.hpp"
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

    while (!state.quit) {

        std::fill(mixBufferL.begin(), mixBufferL.end(), 0.0f);
        std::fill(mixBufferR.begin(), mixBufferR.end(), 0.0f);

        bool any_playing = false;
        if (state.is_timeline_playing) {
            any_playing = true;
        }

        {
            for (auto& pair : state.tracks) {
                Track* track = pair.second.get();
                std::fill(bufferL, bufferL + block_size, 0.0f);
                std::fill(bufferR, bufferR + block_size, 0.0f);
                bool track_playing = false;

                // 1. Process Session View Clip
                if (track->playing_slot != -1) {
                    track_playing = true;
                    any_playing = true;
                    auto& clip = track->clips[track->playing_slot];
                    
                    context.continuousTimeSamples = track->current_time_sec * sample_rate;
                    context.projectTimeMusic = track->current_time_sec * (context.tempo / 60.0);

                    if (clip->type == Clip::Type::MIDI) {
                        std::vector<MidiNoteEvent> blockEvents;
                        int search_idx = track->current_midi_idx;
                        double beats_per_sec = state.bpm / 60.0;  // Convert beats to seconds
                        while (search_idx < (int)clip->midi_events.size()) {
                            auto& me = clip->midi_events[search_idx];
                            double event_time_sec = me.beats / beats_per_sec;  // me.beats is in beats
                            if (event_time_sec >= track->current_time_sec + time_per_block) break;
                            if (event_time_sec >= track->current_time_sec) {
                                if (hibiki::isNoteOn(me) || hibiki::isNoteOff(me)) {
                                    MidiNoteEvent e;
                                    e.sampleOffset = std::max(0, (int)((event_time_sec - track->current_time_sec) * sample_rate));
                                    if (e.sampleOffset >= block_size) e.sampleOffset = block_size - 1;
                                    e.channel = me.channel;
                                    e.pitch = me.note;
                                    e.isNoteOn = hibiki::isNoteOn(me);
                                    e.velocity = e.isNoteOn ? me.velocity / 127.0f : 0.0f;
                                    blockEvents.push_back(e);
                                }
                            }
                            search_idx++;
                        }
                        track->current_midi_idx = search_idx;
                        if (!track->plugins.empty() && track->plugins[0]->isInstrument()) {
                            track->plugins[0]->process(nullptr, outChannels, block_size, context, blockEvents);
                        }
                    } else {
                        int start_sample = (int)(track->current_time_sec * sample_rate);
                        for (int i = 0; i < block_size; ++i) {
                            int sample_pos = start_sample + i;
                            if (clip->num_channels == 2 && sample_pos * 2 + 1 < (int)clip->audio_data.size()) {
                                bufferL[i] += clip->audio_data[sample_pos * 2];
                                bufferR[i] += clip->audio_data[sample_pos * 2 + 1];
                            } else if (clip->num_channels == 1 && sample_pos < (int)clip->audio_data.size()) {
                                float s = clip->audio_data[sample_pos];
                                bufferL[i] += s; bufferR[i] += s;
                            }
                        }
                    }

                    track->current_time_sec += time_per_block;
                    // Get clip duration in seconds - for MIDI clips, convert duration_beats using project BPM
                    double actual_clip_duration = (clip->type == Clip::Type::MIDI)
                        ? clip->duration_beats * 60.0 / state.bpm
                        : clip->duration_sec;
                    if (track->current_time_sec >= actual_clip_duration) {
                        if (clip->is_loop) {
                            track->current_time_sec = fmod(track->current_time_sec, actual_clip_duration);
                            track->current_midi_idx = 0;
                        } else {
                            track->playing_slot = -1;
                        }
                    }
                }

                // 2. Process Timeline Clips
                for (const auto& tc : track->timeline_clips) {
                    // Get clip duration - use duration_beats for MIDI clips, duration_sec for audio
                    double clip_duration = (tc->duration_beats > 0)
                        ? tc->duration_beats * 60.0 / state.bpm
                        : tc->duration_sec;
                    if (state.playhead_pos_sec + time_per_block > tc->start_time_sec &&
                        state.playhead_pos_sec < tc->start_time_sec + clip_duration) {
                        
                        track_playing = true;
                        any_playing = true;
                        double clip_local_time = state.playhead_pos_sec - tc->start_time_sec;
                        
                        if (tc->clip->type == Clip::Type::MIDI) {
                             std::vector<MidiNoteEvent> blockEvents;
                             double beats_per_sec = state.bpm / 60.0;  // Convert beats to seconds
                             // Convert clip_local_time to beats for comparison
                             double window_start_beats = clip_local_time * beats_per_sec;
                             double window_end_beats = (clip_local_time + time_per_block) * beats_per_sec;
                             for (size_t me_idx = 0; me_idx < tc->clip->midi_events.size(); ++me_idx) {
                                 const auto& me = tc->clip->midi_events[me_idx];
                                 if (me.beats >= window_end_beats) break; // Events are sorted by time (in beats)
                                 if (me.beats >= window_start_beats) {
                                     if (hibiki::isNoteOn(me) || hibiki::isNoteOff(me)) {
                                         MidiNoteEvent e;
                                         double event_local_sec = me.beats / beats_per_sec - clip_local_time;
                                         e.sampleOffset = std::max(0, (int)(event_local_sec * sample_rate));
                                         if (e.sampleOffset >= block_size) e.sampleOffset = block_size - 1;
                                         e.channel = me.channel;
                                         e.pitch = me.note;
                                         e.isNoteOn = hibiki::isNoteOn(me);
                                         e.velocity = e.isNoteOn ? me.velocity / 127.0f : 0.0f;
                                         blockEvents.push_back(e);
                                     }
                                 }
                             }
                             if (!track->plugins.empty() && track->plugins[0]->isInstrument()) {
                                 track->plugins[0]->process(nullptr, outChannels, block_size, context, blockEvents);
                             }
                        } else {
                            int start_sample = (int)(clip_local_time * sample_rate);
                            for (int i = 0; i < block_size; ++i) {
                                int sample_pos = start_sample + i;
                                if (sample_pos < 0) continue;
                                if (tc->clip->num_channels == 2 && sample_pos * 2 + 1 < (int)tc->clip->audio_data.size()) {
                                    bufferL[i] += tc->clip->audio_data[sample_pos * 2];
                                    bufferR[i] += tc->clip->audio_data[sample_pos * 2 + 1];
                                } else if (tc->clip->num_channels == 1 && sample_pos < (int)tc->clip->audio_data.size()) {
                                    float s = tc->clip->audio_data[sample_pos];
                                    bufferL[i] += s; bufferR[i] += s;
                                }
                            }
                        }
                    }
                }

                // 3. Apply Automation — set parameter values from curves
                if (state.is_timeline_playing) {
                    double current_beats = state.playhead_pos_sec * (state.bpm / 60.0);
                    for (const auto& lane : track->automation_lanes) {
                        if (!lane.points.empty() &&
                            lane.plugin_idx >= 0 && lane.plugin_idx < (int)track->plugins.size()) {
                            float val = GetAutomationValue(lane, current_beats);
                            track->plugins[lane.plugin_idx]->setParameterValue(lane.param_id, val);
                        }
                    }
                }

                // Apply Effects (skip instrument if already processed)
                for (size_t i = 0; i < track->plugins.size(); ++i) {
                    if (i == 0 && track->plugins[i]->isInstrument()) continue;
                    track->plugins[i]->process(outChannels, outChannels, block_size, context, {});
                }

                for (int i = 0; i < block_size; ++i) {
                    mixBufferL[i] += bufferL[i];
                    mixBufferR[i] += bufferR[i];
                }

                float peakL = 0, peakR = 0;
                for (int i = 0; i < block_size; i++) {
                    peakL = std::max(peakL, std::abs(bufferL[i]));
                    peakR = std::max(peakR, std::abs(bufferR[i]));
                }
                if (track_playing) {
                    std::lock_guard<std::mutex> llock(state.levels_mutex);
                    state.track_levels[track->index] = {peakL, peakR};
                }
            }
        }
        
        if (state.is_timeline_playing) {
            state.playhead_pos_sec += time_per_block;
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

        // NO IPC work here — notification_thread handles it separately

        if (actual_channels >= 2) {
            for (int i = 0; i < block_size; ++i) {
                interleaved[i * actual_channels + 0] = mixBufferL[i];
                interleaved[i * actual_channels + 1] = mixBufferR[i];
                for (int c = 2; c < actual_channels; ++c) {
                    interleaved[i * actual_channels + c] = 0.0f;
                }
            }
        } else {
            for (int i = 0; i < block_size; ++i) {
                interleaved[i] = (mixBufferL[i] + mixBufferR[i]) * 0.5f;
            }
        }

        alsa.write(interleaved, block_size);
    }
}

// Separate thread for sending GUI notifications (playhead, levels).
// Runs at ~30Hz, completely independent of the audio thread.
void notification_thread(ProjectState& state) {
    while (!state.quit) {
        std::this_thread::sleep_for(std::chrono::milliseconds(33)); // ~30Hz

        hibiki::sendPlayheadInfo((float)state.playhead_pos_sec, (float)state.bpm, state.is_timeline_playing);

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
        sendNotification(builder.GetBufferPointer(), builder.GetSize());
    }
}

void run_ipc_loop(ProjectState& state) {
    HistoryManager history;
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
            history.pushState(CaptureProjectState(state));
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
            history.pushState(CaptureProjectState(state));
            hibiki::LoadProject(state, cmd->path()->str());
            hibiki::SyncProjectToGui(state);
            hibiki::sendAck("LOAD_PROJECT", true);
        } else if (command_type == hibiki::ipc::Command_LoadClip) {
            auto cmd = request->command_as_LoadClip();
            int tidx = cmd->track_index();
            int sidx = cmd->slot_index();
            std::string mpath = cmd->path()->str();
            bool is_loop = cmd->is_loop();
            std::lock_guard<std::mutex> lock(state.tracks_mutex);
            history.pushState(CaptureProjectState(state));
            auto track = hibiki::GetOrCreateTrack(state, tidx);
            if (track->LoadClip(sidx, mpath, is_loop)) {
                hibiki::sendAck("LOAD_CLIP", true);
                std::string name = mpath;
                size_t last_slash = mpath.find_last_of("/\\");
                if (last_slash != std::string::npos) {
                    name = mpath.substr(last_slash + 1);
                }
                hibiki::sendClipInfo(tidx, sidx, name, mpath);
            } else {
                hibiki::sendLog("Failed to load clip: " + mpath);
            }
        } else if (command_type == hibiki::ipc::Command_SetClipLoop) {
            auto cmd = request->command_as_SetClipLoop();
            std::lock_guard<std::mutex> lock(state.tracks_mutex);
            history.pushState(CaptureProjectState(state));
            hibiki::GetOrCreateTrack(state, cmd->track_index())->SetClipLoop(cmd->slot_index(), cmd->is_loop());
            hibiki::sendAck("SET_CLIP_LOOP", true);
        } else if (command_type == hibiki::ipc::Command_Play) {
            state.is_timeline_playing = true;
            hibiki::sendAck("PLAY", true);
        } else if (command_type == hibiki::ipc::Command_Stop) {
            std::lock_guard<std::mutex> lock(state.tracks_mutex);
            state.is_timeline_playing = false;
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
            history.pushState(CaptureProjectState(state));
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
                    std::cerr << "BACKEND: Showing editor for " << plugins[plugin_idx]->getName() << std::endl;
                    plugins[plugin_idx]->showEditor();
                    hibiki::sendAck("SHOW_PLUGIN_GUI", true);
                } else {
                    hibiki::sendAck("SHOW_PLUGIN_GUI", false);
                }
            } else {
                hibiki::sendAck("SHOW_PLUGIN_GUI", false);
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
            std::lock_guard<std::mutex> lock(state.tracks_mutex);
            history.pushState(CaptureProjectState(state));
            if (hibiki::GetOrCreateTrack(state, track_idx)->DeleteClip(slot_index)) {
                hibiki::sendAck("DELETE_CLIP", true);
                hibiki::sendClipInfo(track_idx, slot_index, "", "");
            } else {
                hibiki::sendAck("DELETE_CLIP", false);
            }
        } else if (command_type == hibiki::ipc::Command_Seek) {
            auto cmd = request->command_as_Seek();
            state.playhead_pos_sec = cmd->position();
            hibiki::sendAck("SEEK", true);
        } else if (command_type == hibiki::ipc::Command_AddTimelineClip) {
            auto cmd = request->command_as_AddTimelineClip();
            int tidx = cmd->track_index();
            std::string path = cmd->path() ? cmd->path()->str() : "";
            double start = cmd->start_time();
            double dur_beats = cmd->duration_beats();
            std::lock_guard<std::mutex> lock(state.tracks_mutex);
            history.pushState(CaptureProjectState(state));
            hibiki::GetOrCreateTrack(state, tidx)->AddTimelineClip(path, start, state.bpm, dur_beats);
            hibiki::sendAck("ADD_TIMELINE_CLIP", true);
        } else if (command_type == hibiki::ipc::Command_RemoveTimelineClip) {
            auto cmd = request->command_as_RemoveTimelineClip();
            int tidx = cmd->track_index();
            int cidx = cmd->clip_index();
            std::lock_guard<std::mutex> lock(state.tracks_mutex);
            history.pushState(CaptureProjectState(state));
            hibiki::GetOrCreateTrack(state, tidx)->RemoveTimelineClip(cidx);
            hibiki::sendAck("REMOVE_TIMELINE_CLIP", true);
        } else if (command_type == hibiki::ipc::Command_ListPlugins) {
            auto cmd = request->command_as_ListPlugins();
            std::string path = cmd->path()->str();
            // Start a thread for each scan to avoid blocking IPC loop and allow parallelism
            std::thread([path]() {
                hibiki::sendPluginList(path, Vst3Plugin::listPluginsIsolated(path));
            }).detach();
        } else if (command_type == hibiki::ipc::Command_BounceProject) {
            auto cmd = request->command_as_BounceProject();
            std::string path = cmd->path()->str();
            std::thread([&state, path]() {
                hibiki::BounceProject(state, path);
            }).detach();
        } else if (command_type == hibiki::ipc::Command_Undo) {
            std::lock_guard<std::mutex> lock(state.tracks_mutex);
            std::vector<uint8_t> current = CaptureProjectState(state);
            std::vector<uint8_t> prev;
            if (history.undo(current, prev)) {
                ApplyProjectState(state, prev);
                SyncProjectToGui(state);
                hibiki::sendAck("UNDO", true);
            } else {
                hibiki::sendAck("UNDO", false);
            }
        } else if (command_type == hibiki::ipc::Command_Redo) {
            std::lock_guard<std::mutex> lock(state.tracks_mutex);
            std::vector<uint8_t> current = CaptureProjectState(state);
            std::vector<uint8_t> next;
            if (history.redo(current, next)) {
                ApplyProjectState(state, next);
                SyncProjectToGui(state);
                hibiki::sendAck("REDO", true);
            } else {
                hibiki::sendAck("REDO", false);
            }
        } else if (command_type == hibiki::ipc::Command_GetClipMidi) {
            auto cmd = request->command_as_GetClipMidi();
            int tidx = cmd->track_index();
            int sidx = cmd->slot_index();
            int cidx = cmd->clip_index();
            std::cerr << "[GetClipMidi] track=" << tidx << " slot=" << sidx << " clip=" << cidx << std::endl;
            std::lock_guard<std::mutex> lock(state.tracks_mutex);
            
            // Find the clip - either session clip (sidx >= 0) or timeline clip (cidx >= 0)
            Clip* clip = nullptr;
            if (state.tracks.count(tidx)) {
                auto& track = state.tracks[tidx];
                if (sidx >= 0 && track->clips.count(sidx) && track->clips[sidx]) {
                    clip = track->clips[sidx].get();
                } else if (cidx >= 0 && cidx < (int)track->timeline_clips.size() && track->timeline_clips[cidx]) {
                    clip = track->timeline_clips[cidx]->clip.get();
                }
            }
            
            if (clip && clip->type == Clip::Type::MIDI) {
                int ppq = 480; // Standard MIDI resolution
                std::vector<MidiNote> notes;
                // Convert internal midi_events to MidiNote format
                // Group NOTE_ON/OFF pairs into notes
                std::map<int, std::pair<long, int>> active_notes; // note -> (tick, velocity)
                for (const auto& ev : clip->midi_events) {
                    long tick = (long)(ev.beats * ppq);
                    if (isNoteOn(ev)) {
                        active_notes[ev.note] = {tick, ev.velocity};
                    } else if (isNoteOff(ev)) {
                        if (active_notes.count(ev.note)) {
                            auto [start_tick, vel] = active_notes[ev.note];
                            notes.push_back({start_tick, ev.note, tick - start_tick, vel});
                            active_notes.erase(ev.note);
                        }
                    }
                }
                std::cerr << "[GetClipMidi] Sending " << notes.size() << " notes" << std::endl;
                hibiki::sendClipMidiData(tidx, sidx, cidx, ppq, notes);
                hibiki::sendAck("GET_CLIP_MIDI", true);
            } else {
                std::cerr << "[GetClipMidi] Clip not found or not MIDI" << std::endl;
                hibiki::sendAck("GET_CLIP_MIDI", false);
            }
        } else if (command_type == hibiki::ipc::Command_UpdateClipMidi) {
            auto cmd = request->command_as_UpdateClipMidi();
            int tidx = cmd->track_index();
            int sidx = cmd->slot_index();
            int cidx = cmd->clip_index();
            int ppq = cmd->resolution();
            int numEvents = cmd->events() ? cmd->events()->size() : 0;
            std::cerr << "[UpdateClipMidi] track=" << tidx << " slot=" << sidx << " clip=" << cidx << " ppq=" << ppq << " events=" << numEvents << std::endl;
            if (ppq <= 0) ppq = 480;
            std::lock_guard<std::mutex> lock(state.tracks_mutex);
            history.pushState(CaptureProjectState(state));
            
            // Find the clip - either session clip (sidx >= 0) or timeline clip (cidx >= 0)
            Clip* clip = nullptr;
            if (state.tracks.count(tidx)) {
                auto& track = state.tracks[tidx];
                if (sidx >= 0 && track->clips.count(sidx) && track->clips[sidx]) {
                    clip = track->clips[sidx].get();
                } else if (cidx >= 0 && cidx < (int)track->timeline_clips.size() && track->timeline_clips[cidx]) {
                    clip = track->timeline_clips[cidx]->clip.get();
                }
            }
            
            if (clip && clip->type == Clip::Type::MIDI) {
                // Rebuild midi_events from the notes
                clip->midi_events.clear();
                if (cmd->events()) {
                    for (auto ev : *cmd->events()) {
                        double startBeats = (double)ev->tick() / ppq;
                        double endBeats = (double)(ev->tick() + ev->duration_ticks()) / ppq;
                        // Add NOTE_ON (status 0x90)
                        MidiEvent noteOn;
                        noteOn.beats = startBeats;
                        noteOn.type = 0x90;
                        noteOn.channel = 0;
                        noteOn.note = (uint8_t)ev->pitch();
                        noteOn.velocity = (uint8_t)ev->velocity();
                        clip->midi_events.push_back(noteOn);
                        // Add NOTE_OFF (status 0x80)
                        MidiEvent noteOff;
                        noteOff.beats = endBeats;
                        noteOff.type = 0x80;
                        noteOff.channel = 0;
                        noteOff.note = (uint8_t)ev->pitch();
                        noteOff.velocity = 0;
                        clip->midi_events.push_back(noteOff);
                    }
                }
                // Sort events by beats
                std::sort(clip->midi_events.begin(), clip->midi_events.end(), 
                          [](const MidiEvent& a, const MidiEvent& b) { return a.beats < b.beats; });
                
                // For timeline clips, regenerate waveform_summary and re-send TimelineClipInfo
                if (cidx >= 0 && state.tracks.count(tidx)) {
                    auto& track = state.tracks[tidx];
                    if (cidx < (int)track->timeline_clips.size() && track->timeline_clips[cidx]) {
                        auto& tc = track->timeline_clips[cidx];
                        // Only extend duration, never shrink (preserves clip length for looping)
                        if (!clip->midi_events.empty()) {
                            double note_end = clip->midi_events.back().beats + 0.1;
                            clip->duration_beats = std::max(clip->duration_beats, note_end);
                        }
                        // Sync to TimelineClip so playback engine uses updated duration
                        tc->duration_beats = clip->duration_beats;
                        // Regenerate waveform_summary (note preview data)
                        clip->waveform_summary.clear();
                        double total_beats = clip->duration_beats > 0 ? clip->duration_beats : 1.0;
                        for (size_t i = 0; i < clip->midi_events.size(); ++i) {
                            auto& ev = clip->midi_events[i];
                            if (hibiki::isNoteOn(ev)) {
                                double duration = 0.1;
                                for (size_t j = i + 1; j < clip->midi_events.size(); ++j) {
                                    auto& off_ev = clip->midi_events[j];
                                    if (off_ev.note == ev.note && off_ev.channel == ev.channel && hibiki::isNoteOff(off_ev)) {
                                        duration = off_ev.beats - ev.beats;
                                        break;
                                    }
                                }
                                clip->waveform_summary.push_back((float)(ev.beats / total_beats));
                                clip->waveform_summary.push_back((float)ev.note);
                                clip->waveform_summary.push_back((float)(duration / total_beats));
                            }
                        }
                        // Re-send TimelineClipInfo to update GUI preview
                        float duration_for_gui = (tc->duration_sec > 0) 
                            ? (float)tc->duration_sec 
                            : (float)(clip->duration_beats * 60.0 / (state.bpm > 0 ? state.bpm : 120.0));
                        std::string clipname = clip->path;
                        size_t pos = clipname.find_last_of("/\\");
                        if (pos != std::string::npos) clipname = clipname.substr(pos + 1);
                        hibiki::sendTimelineClipInfo(tidx, cidx, clipname, clip->path, (float)tc->start_time_sec, duration_for_gui, clip->waveform_summary);
                    }
                }
                hibiki::sendAck("UPDATE_CLIP_MIDI", true);
            } else {
                hibiki::sendAck("UPDATE_CLIP_MIDI", false);
            }
        } else if (command_type == hibiki::ipc::Command_ResizeTimelineClip) {
            auto cmd = request->command_as_ResizeTimelineClip();
            int tidx = cmd->track_index();
            int cidx = cmd->clip_index();
            float dur_beats = cmd->duration_beats();
            std::lock_guard<std::mutex> lock(state.tracks_mutex);
            history.pushState(CaptureProjectState(state));
            if (state.tracks.count(tidx)) {
                auto& track = state.tracks[tidx];
                if (cidx >= 0 && cidx < (int)track->timeline_clips.size() && track->timeline_clips[cidx]) {
                    auto& tc = track->timeline_clips[cidx];
                    tc->duration_beats = dur_beats;
                    tc->clip->duration_beats = dur_beats;
                    // Re-send TimelineClipInfo to refresh GUI
                    float duration_for_gui = (float)(dur_beats * 60.0 / (state.bpm > 0 ? state.bpm : 120.0));
                    std::string clipname = tc->clip->path;
                    if (clipname.empty()) clipname = "New Clip";
                    size_t pos = clipname.find_last_of("/\\");
                    if (pos != std::string::npos) clipname = clipname.substr(pos + 1);
                    hibiki::sendTimelineClipInfo(tidx, cidx, clipname, tc->clip->path, (float)tc->start_time_sec, duration_for_gui, tc->clip->waveform_summary);
                }
            }
            hibiki::sendAck("RESIZE_TIMELINE_CLIP", true);
        } else if (command_type == hibiki::ipc::Command_AddAutomationLane) {
            auto cmd = request->command_as_AddAutomationLane();
            int tidx = cmd->track_index();
            int pidx = cmd->plugin_index();
            uint32_t param_id = cmd->param_id();
            std::lock_guard<std::mutex> lock(state.tracks_mutex);
            history.pushState(CaptureProjectState(state));
            auto track = hibiki::GetOrCreateTrack(state, tidx);
            int lane_idx = track->AddAutomationLane(pidx, param_id);
            hibiki::sendAutomationLanesData(tidx, track->automation_lanes, track->plugins);
            hibiki::sendAck("ADD_AUTOMATION_LANE", true);
        } else if (command_type == hibiki::ipc::Command_RemoveAutomationLane) {
            auto cmd = request->command_as_RemoveAutomationLane();
            int tidx = cmd->track_index();
            int lidx = cmd->lane_index();
            std::lock_guard<std::mutex> lock(state.tracks_mutex);
            history.pushState(CaptureProjectState(state));
            auto track = hibiki::GetOrCreateTrack(state, tidx);
            track->RemoveAutomationLane(lidx);
            hibiki::sendAutomationLanesData(tidx, track->automation_lanes, track->plugins);
            hibiki::sendAck("REMOVE_AUTOMATION_LANE", true);
        } else if (command_type == hibiki::ipc::Command_UpdateAutomationLane) {
            auto cmd = request->command_as_UpdateAutomationLane();
            int tidx = cmd->track_index();
            int lidx = cmd->lane_index();
            std::lock_guard<std::mutex> lock(state.tracks_mutex);
            history.pushState(CaptureProjectState(state));
            if (state.tracks.count(tidx)) {
                auto& track = state.tracks[tidx];
                if (lidx >= 0 && lidx < (int)track->automation_lanes.size()) {
                    auto& lane = track->automation_lanes[lidx];
                    lane.points.clear();
                    if (cmd->points()) {
                        for (auto pt : *cmd->points()) {
                            AutomationPoint p;
                            p.time_beats = pt->time_beats();
                            p.value = std::max(0.0f, std::min(1.0f, pt->value()));
                            p.tension = std::max(-1.0f, std::min(1.0f, pt->tension()));
                            lane.points.push_back(p);
                        }
                        // Ensure sorted by time
                        std::sort(lane.points.begin(), lane.points.end(),
                            [](const AutomationPoint& a, const AutomationPoint& b) {
                                return a.time_beats < b.time_beats;
                            });
                    }
                    hibiki::sendAutomationLanesData(tidx, track->automation_lanes, track->plugins);
                    hibiki::sendAck("UPDATE_AUTOMATION_LANE", true);
                } else {
                    hibiki::sendAck("UPDATE_AUTOMATION_LANE", false);
                }
            } else {
                hibiki::sendAck("UPDATE_AUTOMATION_LANE", false);
            }
        } else if (command_type == hibiki::ipc::Command_GetAutomationLanes) {
            auto cmd = request->command_as_GetAutomationLanes();
            int tidx = cmd->track_index();
            std::lock_guard<std::mutex> lock(state.tracks_mutex);
            if (state.tracks.count(tidx)) {
                auto& track = state.tracks[tidx];
                hibiki::sendAutomationLanesData(tidx, track->automation_lanes, track->plugins);
                hibiki::sendAck("GET_AUTOMATION_LANES", true);
            } else {
                hibiki::sendAck("GET_AUTOMATION_LANES", false);
            }
        } else if (command_type == hibiki::ipc::Command_Quit) {
            state.quit = true;
            break;
        }
    }
}

} // namespace hibiki

int main(int argc, char** argv) {
    if (argc >= 2 && std::string(argv[1]) == "--list") {
        if (argc < 3) return 1;
        auto plugins = Vst3Plugin::listPlugins(argv[2]);
        for (const auto& p : plugins) {
            std::cout << p.index << ":" << p.name << ":" << p.vendor << "\n";
        }
        return 0;
    }

#ifdef _WIN32
  // Ensure binary mode for IPC on Windows
  _setmode(_fileno(stdin), _O_BINARY);
  _setmode(_fileno(stdout), _O_BINARY);
#endif

    hibiki::ProjectState state;
    state.bpm = 140.0;  // Explicitly set default BPM
    state.sample_rate = 44100.0;  // Explicitly set default sample rate
    std::thread audio_thread(hibiki::playback_thread, std::ref(state));
    std::thread notif_thread(hibiki::notification_thread, std::ref(state));

#if defined(__APPLE__)
    std::thread ipc_thread(hibiki::run_ipc_loop, std::ref(state));
    Vst3Plugin::runMainLoop();
    if (ipc_thread.joinable()) ipc_thread.join();
#else
    hibiki::run_ipc_loop(state);
#endif

    if (notif_thread.joinable()) notif_thread.join();
    if (audio_thread.joinable()) audio_thread.join();
    return 0;
}
