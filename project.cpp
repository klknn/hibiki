#include "project.hpp"
#include "ipc.hpp"
#include "audio_file.hpp"
#include "hibiki_project_generated.h"
#include <fstream>
#include <iostream>

namespace hibiki {

Track* GetOrCreateTrack(ProjectState& state, int track_index) {
    if (state.tracks.find(track_index) == state.tracks.end()) {
        state.tracks[track_index] = std::make_unique<Track>(track_index);
    }
    return state.tracks[track_index].get();
}

bool SaveProject(const ProjectState& state, const std::string& path) {
    flatbuffers::FlatBufferBuilder builder;

    std::vector<flatbuffers::Offset<hibiki::project::Track>> track_offsets;
    for (const auto& [idx, track] : state.tracks) {
        std::vector<flatbuffers::Offset<hibiki::project::Plugin>> plugin_offsets;
        for (const auto& plugin : track->plugins) {
            auto path_str = builder.CreateString(plugin->getPath());
            std::vector<flatbuffers::Offset<hibiki::project::Parameter>> param_offsets;
            int num_params = plugin->getParameterCount();
            for (int p = 0; p < num_params; ++p) {
                VstParamInfo info;
                if (plugin->getParameterInfo(p, info)) {
                    double val = plugin->getParameterValue(info.id);
                    if (val != info.defaultValue) {
                        param_offsets.push_back(hibiki::project::CreateParameter(builder, info.id, val));
                    }
                }
            }
            auto params_vec = builder.CreateVector(param_offsets);
            plugin_offsets.push_back(hibiki::project::CreatePlugin(builder, path_str, plugin->getPluginIndex(), params_vec));
        }

        std::vector<flatbuffers::Offset<hibiki::project::Clip>> clip_offsets;
        for (const auto& [slot, clip] : track->clips) {
            auto path_str = builder.CreateString(clip->path);
            auto clip_type = clip->type == Clip::Type::MIDI ? hibiki::project::ClipType::ClipType_MIDI : hibiki::project::ClipType::ClipType_AUDIO;
            clip_offsets.push_back(hibiki::project::CreateClip(builder, slot, path_str, clip->is_loop, clip_type));
        }

        std::vector<flatbuffers::Offset<hibiki::project::TimelineClip>> timeline_clip_offsets;
        for (const auto& tc : track->timeline_clips) {
            auto path_str = builder.CreateString(tc->clip->path);
            timeline_clip_offsets.push_back(hibiki::project::CreateTimelineClip(builder, path_str, tc->start_time_sec, tc->duration_sec));
        }

        auto plugins_vec = builder.CreateVector(plugin_offsets);
        auto clips_vec = builder.CreateVector(clip_offsets);
        auto timeline_clips_vec = builder.CreateVector(timeline_clip_offsets);
        auto name_str = builder.CreateString(track->name);
        
        hibiki::project::TrackBuilder tb(builder);
        tb.add_index(idx);
        tb.add_name(name_str);
        tb.add_plugins(plugins_vec);
        tb.add_clips(clips_vec);
        tb.add_timeline_clips(timeline_clips_vec);
        track_offsets.push_back(tb.Finish());
    }

    auto tracks_vec = builder.CreateVector(track_offsets);
    auto project_data = hibiki::project::CreateProject(builder, state.bpm, state.playhead_pos_sec, tracks_vec);
    builder.Finish(project_data);

    std::ofstream out(path, std::ios::binary);
    if (!out) {
        std::cerr << "Failed to open project file for writing: " << path << "\n";
        return false;
    }
    out.write(reinterpret_cast<const char*>(builder.GetBufferPointer()), builder.GetSize());
    return true;
}

bool LoadProject(ProjectState& state, const std::string& path) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) {
        std::cerr << "Failed to open project file for reading: " << path << "\n";
        return false;
    }

    std::streamsize size = in.tellg();
    in.seekg(0, std::ios::beg);

    std::vector<char> buffer(size);
    if (!in.read(buffer.data(), size)) {
        std::cerr << "Failed to read project file: " << path << "\n";
        return false;
    }

    auto project_data = hibiki::project::GetProject(buffer.data());
    
    state.bpm = project_data->bpm();
    state.playhead_pos_sec = project_data->playhead_pos();
    state.tracks.clear();

    if (project_data->tracks()) {
        for (const auto* track_data : *project_data->tracks()) {
            auto track = GetOrCreateTrack(state, track_data->index());
            
            // Load track name and notify GUI
            if (track_data->name()) {
                track->name = track_data->name()->str();
                sendTrackInfo(track_data->index(), track->name);
            }

            if (track_data->plugins()) {
                for (const auto* plugin_data : *track_data->plugins()) {
                    int pidx = track->LoadPlugin(plugin_data->path()->str(), plugin_data->index(), state.sample_rate);
                    if (pidx >= 0 && plugin_data->parameters()) {
                        for(const auto* param_data : *plugin_data->parameters()) {
                            track->plugins[pidx]->setParameterValue(param_data->id(), param_data->value());
                        }
                    }
                }
            }
            if (track_data->clips()) {
                for (const auto* clip_data : *track_data->clips()) {
                    track->LoadClip(clip_data->slot_index(), clip_data->path()->str(), clip_data->is_loop());
                }
            }
            if (track_data->timeline_clips()) {
                for (const auto* tc_data : *track_data->timeline_clips()) {
                    auto tc = std::make_unique<TimelineClip>();
                    tc->clip = hibiki::LoadClip(tc_data->path()->str());
                    tc->start_time_sec = tc_data->start_time();
                    tc->duration_sec = tc->clip ? tc->clip->duration_sec : tc_data->duration();
                    tc->duration_beats = tc->clip ? tc->clip->duration_beats : 0.0;
                    track->timeline_clips.push_back(std::move(tc));
                }
            }
        }
    }
    return true;
}

std::vector<uint8_t> CaptureProjectState(const ProjectState& state) {
    flatbuffers::FlatBufferBuilder builder;

    std::vector<flatbuffers::Offset<hibiki::project::Track>> track_offsets;
    for (const auto& [idx, track] : state.tracks) {
        std::vector<flatbuffers::Offset<hibiki::project::Plugin>> plugin_offsets;
        for (const auto& plugin : track->plugins) {
            auto path_str = builder.CreateString(plugin->getPath());
            std::vector<flatbuffers::Offset<hibiki::project::Parameter>> param_offsets;
            int num_params = plugin->getParameterCount();
            for (int p = 0; p < num_params; ++p) {
                VstParamInfo info;
                if (plugin->getParameterInfo(p, info)) {
                    double val = plugin->getParameterValue(info.id);
                    if (val != info.defaultValue) {
                        param_offsets.push_back(hibiki::project::CreateParameter(builder, info.id, val));
                    }
                }
            }
            auto params_vec = builder.CreateVector(param_offsets);
            plugin_offsets.push_back(hibiki::project::CreatePlugin(builder, path_str, plugin->getPluginIndex(), params_vec));
        }

        std::vector<flatbuffers::Offset<hibiki::project::Clip>> clip_offsets;
        for (const auto& [slot, clip] : track->clips) {
            auto path_str = builder.CreateString(clip->path);
            auto clip_type = clip->type == Clip::Type::MIDI ? hibiki::project::ClipType::ClipType_MIDI : hibiki::project::ClipType::ClipType_AUDIO;
            clip_offsets.push_back(hibiki::project::CreateClip(builder, slot, path_str, clip->is_loop, clip_type));
        }

        std::vector<flatbuffers::Offset<hibiki::project::TimelineClip>> timeline_clip_offsets;
        for (const auto& tc : track->timeline_clips) {
            auto path_str = builder.CreateString(tc->clip->path);
            timeline_clip_offsets.push_back(hibiki::project::CreateTimelineClip(builder, path_str, tc->start_time_sec, tc->duration_sec));
        }

        auto plugins_vec = builder.CreateVector(plugin_offsets);
        auto clips_vec = builder.CreateVector(clip_offsets);
        auto timeline_clips_vec = builder.CreateVector(timeline_clip_offsets);
        auto name_str = builder.CreateString(track->name);
        
        hibiki::project::TrackBuilder tb(builder);
        tb.add_index(idx);
        tb.add_name(name_str);
        tb.add_plugins(plugins_vec);
        tb.add_clips(clips_vec);
        tb.add_timeline_clips(timeline_clips_vec);
        track_offsets.push_back(tb.Finish());
    }

    auto tracks_vec = builder.CreateVector(track_offsets);
    auto project_data = hibiki::project::CreateProject(builder, state.bpm, state.playhead_pos_sec, tracks_vec);
    builder.Finish(project_data);

    uint8_t* buf = builder.GetBufferPointer();
    size_t size = builder.GetSize();
    return std::vector<uint8_t>(buf, buf + size);
}

bool ApplyProjectState(ProjectState& state, const std::vector<uint8_t>& data) {
    if (data.empty()) return false;
    auto project_data = hibiki::project::GetProject(data.data());
    
    state.bpm = project_data->bpm();
    state.playhead_pos_sec = project_data->playhead_pos();
    state.tracks.clear();

    if (project_data->tracks()) {
        for (const auto* track_data : *project_data->tracks()) {
            auto track = GetOrCreateTrack(state, track_data->index());
            if (track_data->plugins()) {
                for (const auto* plugin_data : *track_data->plugins()) {
                    int pidx = track->LoadPlugin(plugin_data->path()->str(), plugin_data->index(), state.sample_rate);
                    if (pidx >= 0 && plugin_data->parameters()) {
                        for(const auto* param_data : *plugin_data->parameters()) {
                            track->plugins[pidx]->setParameterValue(param_data->id(), param_data->value());
                        }
                    }
                }
            }
            if (track_data->clips()) {
                for (const auto* clip_data : *track_data->clips()) {
                    track->LoadClip(clip_data->slot_index(), clip_data->path()->str(), clip_data->is_loop());
                }
            }
            if (track_data->timeline_clips()) {
                for (const auto* tc_data : *track_data->timeline_clips()) {
                    auto tc = std::make_unique<TimelineClip>();
                    tc->clip = hibiki::LoadClip(tc_data->path()->str());
                    tc->start_time_sec = tc_data->start_time();
                    tc->duration_sec = tc->clip ? tc->clip->duration_sec : tc_data->duration();
                    tc->duration_beats = tc->clip ? tc->clip->duration_beats : 0.0;
                    track->timeline_clips.push_back(std::move(tc));
                }
            }
        }
    }
    return true;
}

void SyncProjectToGui(const ProjectState& state) {
    hibiki::sendClearProject();
    for (const auto& [tidx, track] : state.tracks) {
        // Sync Session Clips
        for (const auto& [sidx, clip] : track->clips) {
            std::string cname = clip->path;
            size_t last_slash = cname.find_last_of("/\\");
            if (last_slash != std::string::npos) {
                cname = cname.substr(last_slash + 1);
            }
            hibiki::sendClipInfo(tidx, sidx, cname, clip->path);
        }
        // Sync Plugins
        for (int pidx = 0; pidx < (int)track->plugins.size(); ++pidx) {
            auto& plugin = track->plugins[pidx];
            std::vector<VstParamInfo> params;
            for (int i = 0; i < plugin->getParameterCount(); ++i) {
                VstParamInfo info;
                if (plugin->getParameterInfo(i, info)) {
                    params.push_back(info);
                }
            }
            hibiki::sendParamList(tidx, pidx, plugin->getName(), plugin->isInstrument(), params);
        }
        // Sync Timeline Clips
        for (int tc_idx = 0; tc_idx < (int)track->timeline_clips.size(); ++tc_idx) {
            const auto& tc = track->timeline_clips[tc_idx];
            std::string cname = tc->clip->path;
            size_t last_slash = cname.find_last_of("/\\");
            if (last_slash != std::string::npos) {
                cname = cname.substr(last_slash + 1);
            }
            // For MIDI clips, convert duration_beats to seconds using project BPM
            float duration_for_gui = (tc->duration_beats > 0)
                ? (float)(tc->duration_beats * 60.0 / state.bpm)
                : (float)tc->duration_sec;
            hibiki::sendTimelineClipInfo(tidx, tc_idx, cname, tc->clip->path, tc->start_time_sec, duration_for_gui, tc->clip->waveform_summary);
        }
    }
}

double GetProjectDuration(const ProjectState& state) {
    double max_duration = 0.0;
    for (const auto& [idx, track] : state.tracks) {
        for (const auto& tc : track->timeline_clips) {
            // For MIDI clips, convert duration_beats to seconds using project BPM
            double clip_duration_sec = (tc->duration_beats > 0)
                ? tc->duration_beats * 60.0 / state.bpm
                : tc->duration_sec;
            double end_time = tc->start_time_sec + clip_duration_sec;
            if (end_time > max_duration) {
                max_duration = end_time;
            }
        }
    }
    return max_duration > 0.0 ? max_duration + 2.0 : 0.0;
}

void BounceProject(ProjectState& live_state, const std::string& path) {
    std::vector<uint8_t> snapshot;
    double duration = 0.0;
    {
        std::lock_guard<std::mutex> lock(live_state.tracks_mutex);
        snapshot = CaptureProjectState(live_state);
        duration = GetProjectDuration(live_state);
    }
    
    if (duration <= 0.0) {
        hibiki::sendBounceFinished(path, false);
        return;
    }
    
    ProjectState state;
    state.sample_rate = live_state.sample_rate;
    ApplyProjectState(state, snapshot);
    state.is_timeline_playing = true;
    state.playhead_pos_sec = 0.0;
    
    int block_size = 512;
    float sample_rate = state.sample_rate;
    int actual_channels = 2;
    std::vector<float> output_buffer;
    
    HostProcessContext context;
    context.sampleRate = sample_rate;
    context.tempo = state.bpm;
    context.timeSigNumerator = 4;
    context.timeSigDenominator = 4;
    
    alignas(32) float bufferL[512];
    alignas(32) float bufferR[512];
    float* outChannels[] = {bufferL, bufferR};
    
    double time_per_block = block_size / (double)sample_rate;
    
    while (state.playhead_pos_sec < duration) {
        std::vector<float> mixBufferL(block_size, 0.0f);
        std::vector<float> mixBufferR(block_size, 0.0f);
        
        for (auto& pair : state.tracks) {
            Track* track = pair.second.get();
            std::fill(bufferL, bufferL + block_size, 0.0f);
            std::fill(bufferR, bufferR + block_size, 0.0f);
            
            for (const auto& tc : track->timeline_clips) {
                // Get clip duration - use duration_beats for MIDI clips, duration_sec for audio
                double clip_duration = (tc->duration_beats > 0)
                    ? tc->duration_beats * 60.0 / state.bpm
                    : tc->duration_sec;
                if (state.playhead_pos_sec + time_per_block > tc->start_time_sec &&
                    state.playhead_pos_sec < tc->start_time_sec + clip_duration) {
                    
                    double clip_local_time = state.playhead_pos_sec - tc->start_time_sec;
                    
                    if (tc->clip->type == Clip::Type::MIDI) {
                         std::vector<MidiNoteEvent> blockEvents;
                         double beats_per_sec = state.bpm / 60.0;  // Convert beats to seconds
                         // Convert clip_local_time to beats for comparison
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
            
            for (size_t i = 0; i < track->plugins.size(); ++i) {
                if (i == 0 && track->plugins[i]->isInstrument()) continue;
                track->plugins[i]->process(outChannels, outChannels, block_size, context, {});
            }
            
            for (int i = 0; i < block_size; ++i) {
                mixBufferL[i] += bufferL[i];
                mixBufferR[i] += bufferR[i];
            }
        }
        
        for (int i = 0; i < block_size; ++i) {
            output_buffer.push_back(mixBufferL[i]);
            output_buffer.push_back(mixBufferR[i]);
        }
        
        state.playhead_pos_sec += time_per_block;
        context.continuousTimeSamples = state.playhead_pos_sec * sample_rate;
        context.projectTimeMusic = state.playhead_pos_sec * (context.tempo / 60.0);
    }
    
    bool success = SaveWav(path, output_buffer, actual_channels, sample_rate);
    hibiki::sendBounceFinished(path, success);
}

} // namespace hibiki
