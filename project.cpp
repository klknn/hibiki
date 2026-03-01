#include "project.hpp"
#include "global_state.hpp"
#include "ipc.hpp"
#include "hibiki_project_generated.h"
#include <fstream>
#include <vector>

void save_project(GlobalState& state, const std::string& path) {
    flatbuffers::FlatBufferBuilder builder(1024);
    std::vector<flatbuffers::Offset<hibiki::project::Track>> track_offsets;

    std::lock_guard<std::mutex> lock(state.tracks_mutex);
    for (auto& pair : state.tracks) {
        Track* track = pair.second.get();
        std::lock_guard<std::mutex> tlock(track->mutex);

        std::vector<flatbuffers::Offset<hibiki::project::Plugin>> plugin_offsets;
        for (auto& plugin : track->plugins) {
            std::vector<flatbuffers::Offset<hibiki::project::Parameter>> params;
            for (int i = 0; i < plugin->getParameterCount(); i++) {
                VstParamInfo info;
                if (plugin->getParameterInfo(i, info)) {
                    params.push_back(hibiki::project::CreateParameter(builder, info.id, (float)plugin->getParameterValue(info.id)));
                }
            }
            auto params_vec = builder.CreateVector(params);
            auto path_off = builder.CreateString(plugin->getPath());
            auto p = hibiki::project::CreatePlugin(builder, path_off, plugin->getPluginIndex(), params_vec);
            plugin_offsets.push_back(p);
        }

        std::vector<flatbuffers::Offset<hibiki::project::Clip>> clip_offsets;
        for (auto const& [slot, clip] : track->clips) {
            auto path_off = builder.CreateString(clip->path);
            auto type = (clip->type == Clip::AUDIO) ? hibiki::project::ClipType_AUDIO : hibiki::project::ClipType_MIDI;
            clip_offsets.push_back(hibiki::project::CreateClip(builder, slot, path_off, clip->is_loop, type));
        }

        auto plugins_vec = builder.CreateVector(plugin_offsets);
        auto clips_vec = builder.CreateVector(clip_offsets);
        track_offsets.push_back(hibiki::project::CreateTrack(builder, track->index, plugins_vec, clips_vec));
    }

    auto tracks_vec = builder.CreateVector(track_offsets);
    auto project = hibiki::project::CreateProject(builder, (float)state.bpm, tracks_vec);
    builder.Finish(project);

    std::ofstream os(path, std::ios::binary);
    os.write((char*)builder.GetBufferPointer(), builder.GetSize());
}

void load_project(GlobalState& state, const std::string& path) {
    double sample_rate = state.sample_rate;
    std::ifstream is(path, std::ios::binary | std::ios::ate);
    if (!is) return;
    auto size = is.tellg();
    is.seekg(0, std::ios::beg);
    std::vector<char> buffer(size);
    is.read(buffer.data(), size);

    auto project = hibiki::project::GetProject(buffer.data());
    state.bpm = project->bpm();

    ipc::sendClearProject();

    {
        std::lock_guard<std::mutex> lock(state.tracks_mutex);
        state.tracks.clear();
    }

    if (project->tracks()) {
        for (auto const& track_fb : *project->tracks()) {
            Track* track = state.get_or_create_track(track_fb->index());
            if (track_fb->plugins()) {
                for (auto const& plugin_fb : *track_fb->plugins()) {
                    int idx = track->load_plugin(plugin_fb->path()->str(), plugin_fb->index(), sample_rate);
                    if (idx != -1) {
                        auto& plugin = track->plugins[idx];
                        if (plugin_fb->parameters()) {
                            for (auto param_fb : *plugin_fb->parameters()) {
                                plugin->setParameterValue(param_fb->id(), param_fb->value());
                            }
                        }
                        // Notify GUI about the loaded plugin
                        std::vector<VstParamInfo> params;
                        for (int i = 0; i < plugin->getParameterCount(); ++i) {
                            VstParamInfo info;
                            if (plugin->getParameterInfo(i, info)) params.push_back(info);
                        }
                        ipc::sendParamList(track->index, idx, plugin->getName(), plugin->isInstrument(), params);
                    }
                }
            }
            if (track_fb->clips()) {
                for (auto clip_fb : *track_fb->clips()) {
                    if (track->load_clip(clip_fb->slot_index(), clip_fb->path()->str(), clip_fb->is_loop())) {
                        std::string name = clip_fb->path()->str();
                        size_t last_slash = name.find_last_of("/\\");
                        if (last_slash != std::string::npos) name = name.substr(last_slash + 1);
                        ipc::sendClipInfo(track->index, clip_fb->slot_index(), name);
                    }
                }
            }
        }
    }
}
