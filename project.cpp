#include "project.hpp"
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

        auto plugins_vec = builder.CreateVector(plugin_offsets);
        auto clips_vec = builder.CreateVector(clip_offsets);
        track_offsets.push_back(hibiki::project::CreateTrack(builder, idx, plugins_vec, clips_vec));
    }

    auto tracks_vec = builder.CreateVector(track_offsets);
    auto project_data = hibiki::project::CreateProject(builder, state.bpm, tracks_vec);
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
        }
    }
    return true;
}

} // namespace hibiki
