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

#include "alsa_out.hpp"
#include "vst3_host.hpp"

#include "hibiki_request_generated.h"
#include "hibiki_response_generated.h"
#include "hibiki_project_generated.h"

void sendNotification(const uint8_t* buf, size_t size) {
    static std::mutex cout_mutex;
    std::lock_guard<std::mutex> lock(cout_mutex);
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

void sendParamList(int track_idx, int plugin_idx, const std::string& plugin_name, bool is_instrument, const std::vector<VstParamInfo>& params) {
    flatbuffers::FlatBufferBuilder builder(1024);
    std::vector<flatbuffers::Offset<hibiki::ipc::ParamInfo>> param_offsets;
    for (const auto& p : params) {
        auto name_off = builder.CreateString(p.name);
        param_offsets.push_back(hibiki::ipc::CreateParamInfo(builder, p.id, name_off, p.defaultValue));
    }
    auto params_vec = builder.CreateVector(param_offsets);
    auto name_off = builder.CreateString(plugin_name);
    auto list_off = hibiki::ipc::CreateParamList(builder, track_idx, plugin_idx, name_off, is_instrument, params_vec);
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

void sendClearProject() {
    flatbuffers::FlatBufferBuilder builder(128);
    auto clear_off = hibiki::ipc::CreateClearProject(builder);
    auto nf_off = hibiki::ipc::CreateNotification(builder, hibiki::ipc::Response_ClearProject, clear_off.Union());
    builder.Finish(nf_off);
    sendNotification(builder.GetBufferPointer(), builder.GetSize());
}


struct Clip {
    enum Type { MIDI, AUDIO } type;
    std::vector<hbk::MidiEvent> midi_events;
    std::vector<float> audio_data; // Mono or interleaved stereo, but we'll assume stereo for simplicity or handle both
    int num_channels = 0;
    double duration_sec = 0.0;
    std::string path;
    bool is_loop = false;
    std::vector<float> waveform_summary;
};

// Simple WAV loader (16-bit PCM)
bool loadWav(const std::string& path, std::vector<float>& out_data, int& out_channels, double& out_duration) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;

    char chunkId[4];
    f.read(chunkId, 4);
    if (std::string(chunkId, 4) != "RIFF") return false;
    f.seekg(4, std::ios::cur); // Skip size
    f.read(chunkId, 4);
    if (std::string(chunkId, 4) != "WAVE") return false;

    int sample_rate = 0;
    int bits_per_sample = 0;
    int channels = 0;

    while (f.read(chunkId, 4)) {
        uint32_t size;
        f.read((char*)&size, 4);
        if (std::string(chunkId, 4) == "fmt ") {
            uint16_t format;
            f.read((char*)&format, 2);
            if (format != 1) return false; // Only PCM supported
            uint16_t chans;
            f.read((char*)&chans, 2);
            channels = chans;
            f.read((char*)&sample_rate, 4);
            f.seekg(6, std::ios::cur); // Skip byte rate and block align
            f.read((char*)&bits_per_sample, 2);
            if (bits_per_sample != 16) return false; // Only 16-bit supported
            if (size > 16) f.seekg(size - 16, std::ios::cur);
        } else if (std::string(chunkId, 4) == "data") {
            int num_samples = size / 2;
            std::vector<int16_t> pcm(num_samples);
            f.read((char*)pcm.data(), size);
            out_data.resize(num_samples);
            for (int i = 0; i < num_samples; ++i) {
                out_data[i] = pcm[i] / 32768.0f;
            }
            out_channels = channels;
            out_duration = (double)num_samples / (channels * sample_rate);
            return true;
        } else {
            f.seekg(size, std::ios::cur);
        }
    }
    return false;
}

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

    int load_plugin(const std::string& path, int plugin_index) {
        std::lock_guard<std::mutex> lock(mutex);
        auto plugin = std::make_unique<Vst3Plugin>();
        if (!plugin->load(path, plugin_index)) {
            return -1;
        }

        bool is_instrument = plugin->isInstrument();
        int target_idx = -1;
        if (is_instrument) {
            // Find existing instrument to replace
            for (size_t i = 0; i < plugins.size(); ++i) {
                if (plugins[i]->isInstrument()) {
                    target_idx = (int)i;
                    break;
                }
            }
        }

        if (target_idx != -1) {
            plugins[target_idx] = std::move(plugin);
        } else if (is_instrument) {
            // New instrument, insert at 0
            plugins.insert(plugins.begin(), std::move(plugin));
            target_idx = 0;
        } else {
            // Effect, append
            target_idx = (int)plugins.size();
            plugins.push_back(std::move(plugin));
        }

        // If this is the first plugin, reset playback state
        if (plugins.size() == 1) {
            current_time_sec = 0.0;
            current_midi_idx = 0;
        }

        // Exclusivity rule: If loading an instrument, clear audio clips
        if (is_instrument) {
            std::vector<int> audio_slots;
            for (auto const& [slot, clip] : clips) {
                if (clip->type == Clip::AUDIO) audio_slots.push_back(slot);
            }
            for (int slot : audio_slots) {
                clips.erase(slot);
                sendClipInfo(index, slot, "");
            }
        }

        return target_idx;
    }

    bool delete_clip(int slot) {
        std::lock_guard<std::mutex> lock(mutex);
        if (clips.count(slot)) {
            clips.erase(slot);
            if (playing_slot == slot) {
                playing_slot = -1;
            }
            return true;
        }
        return false;
    }



    bool load_clip(int slot, const std::string& path, bool is_loop = false) {
        std::lock_guard<std::mutex> lock(mutex);
        
        auto clip = std::make_unique<Clip>();
        clip->path = path;
        clip->is_loop = is_loop;

        if (path.size() > 4 && path.substr(path.size() - 4) == ".wav") {
            if (!loadWav(path, clip->audio_data, clip->num_channels, clip->duration_sec)) {
                return false;
            }
            clip->type = Clip::AUDIO;
        } else {
            auto events = hbk::parseMidi(path);
            if (events.empty()) return false;
            clip->midi_events = std::move(events);
            clip->type = Clip::MIDI;
            if (!clip->midi_events.empty()) {
                clip->duration_sec = clip->midi_events.back().seconds + 0.1; // Small buffer
            }
        }


        // Exclusivity rule: If loading an audio clip, clear instruments
        if (clip->type == Clip::AUDIO) {
            for (size_t i = 0; i < plugins.size(); ++i) {
                if (plugins[i]->isInstrument()) {
                    sendParamList(index, (int)i, "", true, {});
                }
            }
            plugins.erase(std::remove_if(plugins.begin(), plugins.end(), [](const auto& p) {
                return p->isInstrument();
            }), plugins.end());
        }

        // Generate waveform summary for AUDIO clips
        if (clip->type == Clip::AUDIO && !clip->audio_data.empty()) {
            int num_points = 256;
            clip->waveform_summary.resize(num_points);
            int samples_per_point = clip->audio_data.size() / (clip->num_channels * num_points);
            if (samples_per_point < 1) samples_per_point = 1;

            for (int i = 0; i < num_points; i++) {
                float max_val = 0;
                for (int j = 0; j < samples_per_point; j++) {
                    int idx = (i * samples_per_point + j) * clip->num_channels;
                    if (idx < (int)clip->audio_data.size()) {
                        max_val = std::max(max_val, std::abs(clip->audio_data[idx]));
                    }
                }
                clip->waveform_summary[i] = max_val;
            }

            // Send waveform to GUI
            flatbuffers::FlatBufferBuilder builder(1024 + num_points * 4);
            auto wf_vec = builder.CreateVector(clip->waveform_summary);
            auto wf_off = hibiki::ipc::CreateClipWaveform(builder, index, slot, wf_vec);
            auto nf_off = hibiki::ipc::CreateNotification(builder, hibiki::ipc::Response_ClipWaveform, wf_off.Union());
            builder.Finish(nf_off);
            sendNotification(builder.GetBufferPointer(), builder.GetSize());
        }

        clips[slot] = std::move(clip);

        // If we are currently playing this slot, reset playback
        if (playing_slot == slot) {
            current_time_sec = 0.0;
            current_midi_idx = 0;
        }
        return true;
    }

    void set_clip_loop(int slot, bool is_loop) {
        std::lock_guard<std::mutex> lock(mutex);
        if (clips.count(slot)) {
            clips[slot]->is_loop = is_loop;
        }
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
    double bpm = 140.0;
    std::map<int, std::unique_ptr<Track>> tracks;
    std::mutex tracks_mutex;
    std::map<int, std::pair<float, float>> track_levels; // Peak L/R
    std::mutex levels_mutex;

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
    context.tempo = state.bpm;
    context.timeSigNumerator = 4;
    context.timeSigDenominator = 4;

    double time_per_block = block_size / (double)sample_rate;
    std::vector<float> mixBufferL(block_size);
    std::vector<float> mixBufferR(block_size);
    std::vector<float> interleaved(block_size * num_channels);

    int level_counter = 0;

    while (!state.quit) {

        std::fill(mixBufferL.begin(), mixBufferL.end(), 0.0f);
        std::fill(mixBufferR.begin(), mixBufferR.end(), 0.0f);

        bool any_playing = false;

        {
            std::lock_guard<std::mutex> lock(state.tracks_mutex);
            for (auto& pair : state.tracks) {
                Track* track = pair.second.get();
                std::lock_guard<std::mutex> tlock(track->mutex);

                if (track->playing_slot == -1) continue;

                any_playing = true;
                auto& clip = track->clips[track->playing_slot];

                context.continuousTimeSamples = track->current_time_sec * sample_rate;
                context.projectTimeMusic = track->current_time_sec * (context.tempo / 60.0);

                std::fill(bufferL, bufferL + block_size, 0.0f);
                std::fill(bufferR, bufferR + block_size, 0.0f);

                if (clip->type == Clip::MIDI) {
                    const auto& events = clip->midi_events;
                    int num_midi_events = events.size();

                    std::vector<MidiNoteEvent> blockEvents;
                    int search_idx = track->current_midi_idx;
                    
                    while (search_idx < num_midi_events) {
                        auto& me = events[search_idx];
                        if (me.seconds >= track->current_time_sec + time_per_block) break;

                        if (me.seconds >= track->current_time_sec) {
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
                } else if (clip->type == Clip::AUDIO) {
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
                {
                    std::lock_guard<std::mutex> llock(state.levels_mutex);
                    state.track_levels[track->index] = {peakL, peakR};
                }
            }
        }

        if (!any_playing) {
            // Clear levels if nothing playing? Or just let them decay
            {
                std::lock_guard<std::mutex> llock(state.levels_mutex);
                for (auto& p : state.track_levels) p.second = {0, 0};
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        // Send levels periodically
        if (++level_counter >= 4) {
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

        for (int i = 0; i < block_size; ++i) {
            interleaved[i * 2 + 0] = mixBufferL[i];
            interleaved[i * 2 + 1] = mixBufferR[i];
        }

        alsa.write(interleaved, block_size);
    }
}


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
    std::ifstream is(path, std::ios::binary | std::ios::ate);
    if (!is) return;
    auto size = is.tellg();
    is.seekg(0, std::ios::beg);
    std::vector<char> buffer(size);
    is.read(buffer.data(), size);

    auto project = hibiki::project::GetProject(buffer.data());
    state.bpm = project->bpm();

    sendClearProject();

    {
        std::lock_guard<std::mutex> lock(state.tracks_mutex);
        state.tracks.clear();
    }

    if (project->tracks()) {
        for (auto const& track_fb : *project->tracks()) {
            Track* track = state.get_or_create_track(track_fb->index());
            if (track_fb->plugins()) {
                for (auto const& plugin_fb : *track_fb->plugins()) {
                    int idx = track->load_plugin(plugin_fb->path()->str(), plugin_fb->index());
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
                        sendParamList(track->index, idx, plugin->getName(), plugin->isInstrument(), params);
                    }
                }
            }
            if (track_fb->clips()) {
                for (auto clip_fb : *track_fb->clips()) {
                    if (track->load_clip(clip_fb->slot_index(), clip_fb->path()->str(), clip_fb->is_loop())) {
                        std::string name = clip_fb->path()->str();
                        size_t last_slash = name.find_last_of("/\\");
                        if (last_slash != std::string::npos) name = name.substr(last_slash + 1);
                        sendClipInfo(track->index, clip_fb->slot_index(), name);
                    }
                }
            }
        }
    }
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
            int target_idx = track->load_plugin(vpath, pidx);
            if (target_idx != -1) {
                std::vector<VstParamInfo> params;
                auto& plugin = track->plugins[target_idx];
                for (int i = 0; i < plugin->getParameterCount(); ++i) {
                    VstParamInfo info;
                    if (plugin->getParameterInfo(i, info)) {
                        params.push_back(info);
                    }
                }
                sendParamList(tidx, target_idx, plugin->getName(), plugin->isInstrument(), params);
            } else {
                sendLog("Failed to load plugin: " + vpath);
            }
        } else if (command_type == hibiki::ipc::Command_SaveProject) {
            auto cmd = request->command_as_SaveProject();
            save_project(state, cmd->path()->str());
            sendAck("SAVE_PROJECT", true);
        } else if (command_type == hibiki::ipc::Command_LoadProject) {
            auto cmd = request->command_as_LoadProject();
            load_project(state, cmd->path()->str());
            sendAck("LOAD_PROJECT", true);
        } else if (command_type == hibiki::ipc::Command_LoadClip) {
            auto cmd = request->command_as_LoadClip();
            int tidx = cmd->track_index();
            int sidx = cmd->slot_index();
            std::string mpath = cmd->path()->str();
            bool is_loop = cmd->is_loop();
            if (state.get_or_create_track(tidx)->load_clip(sidx, mpath, is_loop)) {
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
        } else if (command_type == hibiki::ipc::Command_SetClipLoop) {
            auto cmd = request->command_as_SetClipLoop();
            state.get_or_create_track(cmd->track_index())->set_clip_loop(cmd->slot_index(), cmd->is_loop());
            sendAck("SET_CLIP_LOOP", true);
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
        } else if (command_type == hibiki::ipc::Command_SetBpm) {
            auto cmd = request->command_as_SetBpm();
            state.bpm = cmd->bpm();
            sendAck("SET_BPM", true);
        } else if (command_type == hibiki::ipc::Command_PlayScene) {
            auto cmd = request->command_as_PlayScene();
            int sidx = cmd->slot_index();
            std::lock_guard<std::mutex> lock(state.tracks_mutex);
            for (auto& pair : state.tracks) {
                pair.second->play_clip(sidx);
            }
            sendAck("PLAY_SCENE", true);
        } else if (command_type == hibiki::ipc::Command_DeleteClip) {
            auto cmd = request->command_as_DeleteClip();
            int tidx = cmd->track_index();
            int sidx = cmd->slot_index();
            if (state.get_or_create_track(tidx)->delete_clip(sidx)) {
                sendAck("DELETE_CLIP", true);
                sendClipInfo(tidx, sidx, "");
            } else {
                sendAck("DELETE_CLIP", false);
            }
        } else if (command_type == hibiki::ipc::Command_Quit) {
            state.quit = true;
            break;
        }
    }

    if (audio_thread.joinable()) audio_thread.join();
    return 0;
}
