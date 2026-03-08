#include "project.hpp"
#include "ipc.hpp"
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
        track_offsets.push_back(hibiki::project::CreateTrack(builder, idx, plugins_vec, clips_vec, timeline_clips_vec));
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
                    tc->duration_sec = tc_data->duration();
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
        track_offsets.push_back(hibiki::project::CreateTrack(builder, idx, plugins_vec, clips_vec, timeline_clips_vec));
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
                    tc->duration_sec = tc_data->duration();
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
            hibiki::sendTimelineClipInfo(tidx, tc_idx, cname, tc->clip->path, tc->start_time_sec, tc->duration_sec, tc->clip->waveform_summary);
        }
    }
}

} // namespace hibiki
