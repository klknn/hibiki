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

#include "pb/core.pb.h"
#include "pb/commands.pb.h"
#include "pb/notifications.pb.h"

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
    context.timeSigNumerator = 4;
    context.timeSigDenominator = 4;
    context.continuousTimeSamples = 0;
    context.projectTimeMusic = 0;
    context.tempo = state.bpm;
    double time_per_block = block_size / (double)sample_rate;

    while (!state.quit) {
        std::vector<float> mixBufferL(block_size, 0.0f);
        std::vector<float> mixBufferR(block_size, 0.0f);

        context.tempo = state.bpm;

        {
            std::lock_guard<std::mutex> lock(state.tracks_mutex);
            for (auto& pair : state.tracks) {
                Track* track = pair.second.get();
                std::fill(bufferL, bufferL + block_size, 0.0f);
                std::fill(bufferR, bufferR + block_size, 0.0f);

                // 1. Session clip playback
                if (track->playing_slot >= 0 &&
                    track->clips.count(track->playing_slot) &&
                    track->clips[track->playing_slot]) {
                    auto& clip = track->clips[track->playing_slot];
                    if (clip->type == Clip::Type::AUDIO) {
                        int start_sample = (int)(track->current_time_sec * sample_rate);
                        for (int i = 0; i < block_size; ++i) {
                            int sample_pos = start_sample + i;
                            if (clip->is_loop) {
                                int total_samples = clip->audio_data.size() / clip->num_channels;
                                if (total_samples > 0) sample_pos = sample_pos % total_samples;
                            }
                            if (clip->num_channels == 2 && sample_pos * 2 + 1 < (int)clip->audio_data.size()) {
                                bufferL[i] += clip->audio_data[sample_pos * 2];
                                bufferR[i] += clip->audio_data[sample_pos * 2 + 1];
                            } else if (clip->num_channels == 1 && sample_pos < (int)clip->audio_data.size()) {
                                float s = clip->audio_data[sample_pos];
                                bufferL[i] += s; bufferR[i] += s;
                            }
                        }
                    } else if (clip->type == Clip::Type::MIDI) {
                        // MIDI playback for session clips (looping)
                        double beats_per_sec = state.bpm / 60.0;
                        double current_beats = track->current_time_sec * beats_per_sec;
                        double block_end_beats = (track->current_time_sec + time_per_block) * beats_per_sec;
                        // Handle looping
                        double loop_beats = clip->duration_beats > 0 ? clip->duration_beats : 4.0;
                        if (clip->is_loop && loop_beats > 0) {
                            current_beats = std::fmod(current_beats, loop_beats);
                            block_end_beats = current_beats + time_per_block * beats_per_sec;
                        }
                        std::vector<MidiNoteEvent> blockEvents;
                        for (const auto& me : clip->midi_events) {
                            if (me.beats >= current_beats && me.beats < block_end_beats) {
                                MidiNoteEvent e;
                                double event_sec = me.beats / beats_per_sec - track->current_time_sec;
                                if (clip->is_loop && loop_beats > 0) {
                                    event_sec = (me.beats - current_beats) / beats_per_sec;
                                }
                                e.sampleOffset = std::max(0, (int)(event_sec * sample_rate));
                                if (e.sampleOffset >= block_size) e.sampleOffset = block_size - 1;
                                e.channel = me.channel;
                                e.pitch = me.note;
                                e.isNoteOn = isNoteOn(me);
                                e.velocity = e.isNoteOn ? me.velocity / 127.0f : 0.0f;
                                blockEvents.push_back(e);
                            }
                        }
                        if (!track->plugins.empty() && track->plugins[0]->isInstrument()) {
                            track->plugins[0]->process(nullptr, outChannels, block_size, context, blockEvents);
                        }
                    }
                    track->current_time_sec += time_per_block;
                }

                // 2. Timeline clip playback
                if (state.is_timeline_playing) {
                    for (const auto& tc : track->timeline_clips) {
                        if (!tc->clip) continue;
                        // Get clip duration - use duration_beats for MIDI clips, duration_sec for audio
                        double clip_duration = (tc->duration_beats > 0)
                            ? tc->duration_beats * 60.0 / state.bpm
                            : tc->duration_sec;
                        if (state.playhead_pos_sec + time_per_block > tc->start_time_sec &&
                            state.playhead_pos_sec < tc->start_time_sec + clip_duration) {

                            double clip_local_time = state.playhead_pos_sec - tc->start_time_sec;

                            if (tc->clip->type == Clip::Type::MIDI) {
                                std::vector<MidiNoteEvent> blockEvents;
                                double beats_per_sec = state.bpm / 60.0;
                                double window_start_beats = clip_local_time * beats_per_sec;
                                double window_end_beats = (clip_local_time + time_per_block) * beats_per_sec;
                                for (const auto& me : tc->clip->midi_events) {
                                    if (me.beats >= window_start_beats && me.beats < window_end_beats) {
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

                // 4. Process effects (skip instrument at slot 0 — already used above)
                for (size_t i = 0; i < track->plugins.size(); ++i) {
                    if (i == 0 && track->plugins[i]->isInstrument()) continue;
                    track->plugins[i]->process(outChannels, outChannels, block_size, context, {});
                }

                // 5. Track peak levels
                float peakL = 0.0f, peakR = 0.0f;
                for (int i = 0; i < block_size; ++i) {
                    mixBufferL[i] += bufferL[i];
                    mixBufferR[i] += bufferR[i];
                    peakL = std::max(peakL, std::abs(bufferL[i]));
                    peakR = std::max(peakR, std::abs(bufferR[i]));
                }
                {
                    std::lock_guard<std::mutex> llock(state.levels_mutex);
                    state.track_levels[pair.first] = {peakL, peakR};
                }
            }
        }

        // Mix to output
        std::vector<float> interleaved(block_size * actual_channels, 0.0f);
        for (int i = 0; i < block_size; ++i) {
            interleaved[i * actual_channels] = mixBufferL[i];
            interleaved[i * actual_channels + 1] = mixBufferR[i];
        }
        alsa.write(interleaved, block_size);

        if (state.is_timeline_playing) {
            state.playhead_pos_sec += time_per_block;
        }
        context.continuousTimeSamples += block_size;
        context.projectTimeMusic = state.playhead_pos_sec * (context.tempo / 60.0);
    }
}

// Separate thread for sending GUI notifications (playhead, levels).
// Runs at ~30Hz, completely independent of the audio thread.
void notification_thread(ProjectState& state) {
    while (!state.quit) {
        std::this_thread::sleep_for(std::chrono::milliseconds(33)); // ~30Hz

        hibiki::sendPlayheadInfo((float)state.playhead_pos_sec, (float)state.bpm, state.is_timeline_playing);

        hibiki::pb::notifications::Notification notification;
        auto* tl = notification.mutable_track_levels();
        {
            std::lock_guard<std::mutex> llock(state.levels_mutex);
            for (auto& pair : state.track_levels) {
                auto* level = tl->add_levels();
                level->set_track_index(pair.first);
                level->set_peak_l(pair.second.first);
                level->set_peak_r(pair.second.second);
            }
        }
        std::string data;
        notification.SerializeToString(&data);
        sendNotification(reinterpret_cast<const uint8_t*>(data.data()), data.size());
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

        hibiki::pb::commands::Request request;
        if (!request.ParseFromArray(buffer.get(), msg_size)) {
            std::cerr << "BACKEND ERROR: Failed to parse protobuf request" << std::endl;
            continue;
        }

                switch (request.command_case()) {
        case hibiki::pb::commands::Request::kProject: {
            const auto& cmd = request.project();
            switch (cmd.action()) {
                case hibiki::pb::commands::ProjectCmd::ACTION_SAVE: {
                    std::lock_guard<std::mutex> lock(state.tracks_mutex);
                    hibiki::SaveProject(state, cmd.path());
                    hibiki::sendAck("SAVE_PROJECT", true);
                    break;
                }
                case hibiki::pb::commands::ProjectCmd::ACTION_LOAD: {
                    std::lock_guard<std::mutex> lock(state.tracks_mutex);
                    history.pushState(CaptureProjectState(state));
                    hibiki::LoadProject(state, cmd.path());
                    hibiki::SyncProjectToGui(state);
                    hibiki::sendAck("LOAD_PROJECT", true);
                    break;
                }
                case hibiki::pb::commands::ProjectCmd::ACTION_BOUNCE: {
                    std::string path = cmd.path();
                    std::thread([&state, path]() {
                        hibiki::BounceProject(state, path);
                    }).detach();
                    break;
                }
                case hibiki::pb::commands::ProjectCmd::ACTION_UNDO: {
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
                    break;
                }
                case hibiki::pb::commands::ProjectCmd::ACTION_REDO: {
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
                    break;
                }
                case hibiki::pb::commands::ProjectCmd::ACTION_QUIT: {
                    state.quit = true;
                    break;
                }
                case hibiki::pb::commands::ProjectCmd::ACTION_SET_BPM: {
                    state.bpm = cmd.bpm();
                    hibiki::sendAck("SET_BPM", true);
                    break;
                }
                default: break;
            }
            break;
        }
        case hibiki::pb::commands::Request::kTransport: {
            const auto& cmd = request.transport();
            switch (cmd.action()) {
                case hibiki::pb::commands::TransportCmd::ACTION_PLAY:
                    state.is_timeline_playing = true;
                    hibiki::sendAck("PLAY", true);
                    break;
                case hibiki::pb::commands::TransportCmd::ACTION_STOP: {
                    std::lock_guard<std::mutex> lock(state.tracks_mutex);
                    state.is_timeline_playing = false;
                    for (auto& pair : state.tracks) {
                        pair.second->Stop();
                    }
                    hibiki::sendAck("STOP", true);
                    break;
                }
                case hibiki::pb::commands::TransportCmd::ACTION_SEEK:
                    state.playhead_pos_sec = cmd.seek_pos();
                    hibiki::sendAck("SEEK", true);
                    break;
                default: break;
            }
            break;
        }
        case hibiki::pb::commands::Request::kTrack: {
            const auto& cmd = request.track();
            int tidx = cmd.target().track_index();
            switch (cmd.action()) {
                case hibiki::pb::commands::TrackCmd::ACTION_PLAY_SLOT: {
                    std::lock_guard<std::mutex> lock(state.tracks_mutex);
                    if (cmd.target().has_session_slot()) {
                        int sidx = cmd.target().session_slot();
                        hibiki::GetOrCreateTrack(state, tidx)->PlayClip(sidx);
                    } else {
                        // Play scene (slot index in value field since no scene API exists yet)
                        int sidx = cmd.value();
                        for (auto& pair : state.tracks) pair.second->PlayClip(sidx);
                    }
                    hibiki::sendAck("PLAY_CLIP", true);
                    break;
                }
                case hibiki::pb::commands::TrackCmd::ACTION_STOP: {
                    std::lock_guard<std::mutex> lock(state.tracks_mutex);
                    hibiki::GetOrCreateTrack(state, tidx)->Stop();
                    hibiki::sendAck("STOP_TRACK", true);
                    break;
                }
                case hibiki::pb::commands::TrackCmd::ACTION_LOAD_CLIP: {
                    int sidx = cmd.target().session_slot();
                    std::string mpath = cmd.clip_data().path();
                    bool is_loop = cmd.clip_data().is_loop();
                    std::lock_guard<std::mutex> lock(state.tracks_mutex);
                    history.pushState(CaptureProjectState(state));
                    auto track = hibiki::GetOrCreateTrack(state, tidx);
                    if (track->LoadClip(sidx, mpath, is_loop)) {
                        hibiki::sendAck("LOAD_CLIP", true);
                        std::string name = mpath;
                        size_t last_slash = mpath.find_last_of("/\\");
                        if (last_slash != std::string::npos) name = mpath.substr(last_slash + 1);
                        hibiki::sendClipInfo(tidx, sidx, name, mpath);
                    } else {
                        hibiki::sendLog("Failed to load clip: " + mpath);
                    }
                    break;
                }
                case hibiki::pb::commands::TrackCmd::ACTION_DELETE_CLIP: {
                    int sidx = cmd.target().session_slot();
                    std::lock_guard<std::mutex> lock(state.tracks_mutex);
                    history.pushState(CaptureProjectState(state));
                    if (hibiki::GetOrCreateTrack(state, tidx)->DeleteClip(sidx)) {
                        hibiki::sendAck("DELETE_CLIP", true);
                        hibiki::sendClipInfo(tidx, sidx, "", "");
                    } else {
                        hibiki::sendAck("DELETE_CLIP", false);
                    }
                    break;
                }
                case hibiki::pb::commands::TrackCmd::ACTION_SET_CLIP_LOOP: {
                    std::lock_guard<std::mutex> lock(state.tracks_mutex);
                    history.pushState(CaptureProjectState(state));
                    hibiki::GetOrCreateTrack(state, tidx)->SetClipLoop(cmd.target().session_slot(), cmd.flag());
                    hibiki::sendAck("SET_CLIP_LOOP", true);
                    break;
                }
                case hibiki::pb::commands::TrackCmd::ACTION_ADD_TIMELINE_CLIP: {
                    std::string path = cmd.clip_data().path();
                    double start = cmd.value();
                    double dur_beats = cmd.clip_data().duration_beats();
                    std::lock_guard<std::mutex> lock(state.tracks_mutex);
                    history.pushState(CaptureProjectState(state));
                    hibiki::GetOrCreateTrack(state, tidx)->AddTimelineClip(path, start, state.bpm, dur_beats);
                    hibiki::sendAck("ADD_TIMELINE_CLIP", true);
                    break;
                }
                case hibiki::pb::commands::TrackCmd::ACTION_REMOVE_TIMELINE_CLIP: {
                    int cidx = cmd.target().timeline_clip();
                    std::lock_guard<std::mutex> lock(state.tracks_mutex);
                    history.pushState(CaptureProjectState(state));
                    hibiki::GetOrCreateTrack(state, tidx)->RemoveTimelineClip(cidx);
                    hibiki::sendAck("REMOVE_TIMELINE_CLIP", true);
                    break;
                }
                case hibiki::pb::commands::TrackCmd::ACTION_RESIZE_TIMELINE_CLIP: {
                    int cidx = cmd.target().timeline_clip();
                    float dur_beats = cmd.value();
                    std::lock_guard<std::mutex> lock(state.tracks_mutex);
                    history.pushState(CaptureProjectState(state));
                    if (state.tracks.count(tidx)) {
                        auto& track = state.tracks[tidx];
                        if (cidx >= 0 && cidx < (int)track->timeline_clips.size() && track->timeline_clips[cidx]) {
                            auto& tc = track->timeline_clips[cidx];
                            tc->duration_beats = dur_beats;
                            if (tc->clip) tc->clip->duration_beats = dur_beats;
                            float duration_for_gui = (float)(dur_beats * 60.0 / (state.bpm > 0 ? state.bpm : 120.0));
                            std::string clipname = tc->clip ? tc->clip->path : "";
                            if (clipname.empty()) clipname = "New Clip";
                            size_t pos = clipname.find_last_of("/\\");
                            if (pos != std::string::npos) clipname = clipname.substr(pos + 1);
                            hibiki::sendTimelineClipInfo(tidx, cidx, clipname, tc->clip ? tc->clip->path : "", (float)tc->start_time_sec, duration_for_gui, tc->clip ? tc->clip->waveform_summary : std::vector<float>{});
                        }
                    }
                    hibiki::sendAck("RESIZE_TIMELINE_CLIP", true);
                    break;
                }
                default: break;
            }
            break;
        }
        case hibiki::pb::commands::Request::kPlugin: {
            const auto& cmd = request.plugin();
            int tidx = cmd.target().track_index();
            switch (cmd.action()) {
                case hibiki::pb::commands::PluginCmd::ACTION_LOAD: {
                    std::string vpath = cmd.path();
                    int pidx = cmd.target().plugin_index();
                    std::lock_guard<std::mutex> lock(state.tracks_mutex);
                    history.pushState(CaptureProjectState(state));
                    auto track = hibiki::GetOrCreateTrack(state, tidx);
                    int target_idx = track->LoadPlugin(vpath, pidx, state.sample_rate);
                    if (target_idx != -1) {
                        std::vector<VstParamInfo> params;
                        auto& plugin = track->plugins[target_idx];
                        for (int i = 0; i < plugin->getParameterCount(); ++i) {
                            VstParamInfo info;
                            if (plugin->getParameterInfo(i, info)) params.push_back(info);
                        }
                        hibiki::sendParamList(tidx, target_idx, plugin->getName(), plugin->isInstrument(), params);
                    } else {
                        hibiki::sendLog("Failed to load plugin: " + vpath);
                    }
                    break;
                }
                case hibiki::pb::commands::PluginCmd::ACTION_REMOVE: {
                    int pidx = cmd.target().plugin_index();
                    std::lock_guard<std::mutex> lock(state.tracks_mutex);
                    history.pushState(CaptureProjectState(state));
                    auto track = hibiki::GetOrCreateTrack(state, tidx);
                    hibiki::sendAck("REMOVE_PLUGIN", track->RemovePlugin(pidx));
                    break;
                }
                case hibiki::pb::commands::PluginCmd::ACTION_SHOW_GUI: {
                    int pidx = cmd.target().plugin_index();
                    std::lock_guard<std::mutex> lock(state.tracks_mutex);
                    if (state.tracks.count(tidx)) {
                        auto& plugins = state.tracks[tidx]->plugins;
                        if (pidx >= 0 && pidx < (int)plugins.size()) {
                            plugins[pidx]->showEditor();
                            hibiki::sendAck("SHOW_PLUGIN_GUI", true);
                        } else hibiki::sendAck("SHOW_PLUGIN_GUI", false);
                    } else hibiki::sendAck("SHOW_PLUGIN_GUI", false);
                    break;
                }
                case hibiki::pb::commands::PluginCmd::ACTION_SET_PARAM: {
                    int pidx = cmd.target().plugin_index();
                    uint32_t param_id = cmd.param_id();
                    float value = cmd.param_value();
                    std::lock_guard<std::mutex> lock(state.tracks_mutex);
                    if (state.tracks.count(tidx)) {
                        auto& plugins = state.tracks[tidx]->plugins;
                        if (pidx >= 0 && pidx < (int)plugins.size()) {
                            plugins[pidx]->setParameterValue(param_id, value);
                        }
                    }
                    break;
                }
                case hibiki::pb::commands::PluginCmd::ACTION_LIST: {
                    std::string path = cmd.path();
                    std::thread([path]() {
                        hibiki::sendPluginList(path, Vst3Plugin::listPluginsIsolated(path));
                    }).detach();
                    break;
                }
                default: break;
            }
            break;
        }
        case hibiki::pb::commands::Request::kAutomation: {
            const auto& cmd = request.automation();
            int tidx = cmd.target().track_index();
            switch (cmd.action()) {
                case hibiki::pb::commands::AutomationCmd::ACTION_ADD_LANE: {
                    int pidx = cmd.target().plugin_index();
                    uint32_t param_id = cmd.param_id();
                    std::lock_guard<std::mutex> lock(state.tracks_mutex);
                    history.pushState(CaptureProjectState(state));
                    auto track = hibiki::GetOrCreateTrack(state, tidx);
                    track->AddAutomationLane(pidx, param_id);
                    hibiki::sendAutomationLanesData(tidx, track->automation_lanes, track->plugins);
                    hibiki::sendAck("ADD_AUTOMATION_LANE", true);
                    break;
                }
                case hibiki::pb::commands::AutomationCmd::ACTION_REMOVE_LANE: {
                    int lidx = cmd.target().lane_index();
                    std::lock_guard<std::mutex> lock(state.tracks_mutex);
                    history.pushState(CaptureProjectState(state));
                    auto track = hibiki::GetOrCreateTrack(state, tidx);
                    track->RemoveAutomationLane(lidx);
                    hibiki::sendAutomationLanesData(tidx, track->automation_lanes, track->plugins);
                    hibiki::sendAck("REMOVE_AUTOMATION_LANE", true);
                    break;
                }
                case hibiki::pb::commands::AutomationCmd::ACTION_UPDATE_POINTS: {
                    int lidx = cmd.target().lane_index();
                    std::lock_guard<std::mutex> lock(state.tracks_mutex);
                    history.pushState(CaptureProjectState(state));
                    if (state.tracks.count(tidx)) {
                        auto& track = state.tracks[tidx];
                        if (lidx >= 0 && lidx < (int)track->automation_lanes.size()) {
                            auto& lane = track->automation_lanes[lidx];
                            lane.points.clear();
                            for (const auto& pt : cmd.points()) {
                                hibiki::pb::core::AutomationPoint p;
                                p.set_time_beats(pt.time_beats());
                                p.set_value(std::max(0.0f, std::min(1.0f, pt.value())));
                                p.set_tension(std::max(-1.0f, std::min(1.0f, pt.tension())));
                                lane.points.push_back(p);
                            }
                            std::sort(lane.points.begin(), lane.points.end(), [](const hibiki::pb::core::AutomationPoint& a, const hibiki::pb::core::AutomationPoint& b) { return a.time_beats() < b.time_beats(); });
                            hibiki::sendAutomationLanesData(tidx, track->automation_lanes, track->plugins);
                            hibiki::sendAck("UPDATE_AUTOMATION_LANE", true);
                        } else { hibiki::sendAck("UPDATE_AUTOMATION_LANE", false); }
                    } else { hibiki::sendAck("UPDATE_AUTOMATION_LANE", false); }
                    break;
                }
                case hibiki::pb::commands::AutomationCmd::ACTION_GET_LANES: {
                    std::lock_guard<std::mutex> lock(state.tracks_mutex);
                    if (state.tracks.count(tidx)) {
                        auto& track = state.tracks[tidx];
                        hibiki::sendAutomationLanesData(tidx, track->automation_lanes, track->plugins);
                        hibiki::sendAck("GET_AUTOMATION_LANES", true);
                    } else hibiki::sendAck("GET_AUTOMATION_LANES", false);
                    break;
                }
                default: break;
            }
            break;
        }
        case hibiki::pb::commands::Request::kMidi:
         {
            const auto& cmd = request.midi();
            int tidx = cmd.target().track_index();
            int sidx = cmd.target().session_slot();
            int cidx = cmd.target().timeline_clip();
            switch (cmd.action()) {
                case hibiki::pb::commands::MidiCmd::ACTION_GET: {
                    std::lock_guard<std::mutex> lock(state.tracks_mutex);
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
                        int ppq = 480;
                        std::vector<hibiki::pb::core::MidiEvent> notes;
                        std::map<int, std::pair<long, int>> active_notes;
                        for (const auto& ev : clip->midi_events) {
                            long tick = (long)(ev.beats * ppq);
                            if (isNoteOn(ev)) {
                                active_notes[ev.note] = {tick, ev.velocity};
                            } else if (isNoteOff(ev)) {
                                if (active_notes.count(ev.note)) {
                                    auto [start_tick, vel] = active_notes[ev.note];
                                    hibiki::pb::core::MidiEvent me;
                                    me.set_tick(start_tick);
                                    me.set_pitch(ev.note);
                                    me.set_duration_ticks(tick - start_tick);
                                    me.set_velocity(vel);
                                    notes.push_back(me);
                                    active_notes.erase(ev.note);
                                }
                            }
                        }
                        hibiki::sendClipMidiData(tidx, sidx, cidx, ppq, notes);
                        hibiki::sendAck("GET_CLIP_MIDI", true);
                    } else hibiki::sendAck("GET_CLIP_MIDI", false);
                    break;
                }
                case hibiki::pb::commands::MidiCmd::ACTION_UPDATE: {
                    int ppq = cmd.resolution();
                    if (ppq <= 0) ppq = 480;
                    std::lock_guard<std::mutex> lock(state.tracks_mutex);
                    history.pushState(CaptureProjectState(state));
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
                        clip->midi_events.clear();
                        for (const auto& ev : cmd.events()) {
                            double startBeats = (double)ev.tick() / ppq;
                            double endBeats = (double)(ev.tick() + ev.duration_ticks()) / ppq;
                            MidiEvent noteOn; noteOn.beats = startBeats; noteOn.type = 0x90; noteOn.channel = 0; noteOn.note = (uint8_t)ev.pitch(); noteOn.velocity = (uint8_t)ev.velocity();
                            clip->midi_events.push_back(noteOn);
                            MidiEvent noteOff; noteOff.beats = endBeats; noteOff.type = 0x80; noteOff.channel = 0; noteOff.note = (uint8_t)ev.pitch(); noteOff.velocity = 0;
                            clip->midi_events.push_back(noteOff);
                        }
                        std::sort(clip->midi_events.begin(), clip->midi_events.end(), [](const MidiEvent& a, const MidiEvent& b) { return a.beats < b.beats; });
                        if (cidx >= 0 && state.tracks.count(tidx)) {
                            auto& track = state.tracks[tidx];
                            if (cidx < (int)track->timeline_clips.size() && track->timeline_clips[cidx]) {
                                auto& tc = track->timeline_clips[cidx];
                                if (!clip->midi_events.empty()) {
                                    double note_end = clip->midi_events.back().beats + 0.1;
                                    clip->duration_beats = std::max(clip->duration_beats, note_end);
                                }
                                tc->duration_beats = clip->duration_beats;
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
                                float duration_for_gui = (tc->duration_sec > 0) ? (float)tc->duration_sec : (float)(clip->duration_beats * 60.0 / (state.bpm > 0 ? state.bpm : 120.0));
                                std::string clipname = clip->path;
                                size_t pos = clipname.find_last_of("/\\");
                                if (pos != std::string::npos) clipname = clipname.substr(pos + 1);
                                hibiki::sendTimelineClipInfo(tidx, cidx, clipname, clip->path, (float)tc->start_time_sec, duration_for_gui, clip->waveform_summary);
                            }
                        }
                        hibiki::sendAck("UPDATE_CLIP_MIDI", true);
                    } else hibiki::sendAck("UPDATE_CLIP_MIDI", false);
                    break;
                }
                default: break;
            }
            break;
        }

        }
        if (state.quit) break;
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
