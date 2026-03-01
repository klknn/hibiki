#include "track.hpp"
#include "audio_io.hpp"
#include "ipc.hpp"
#include "hibiki_response_generated.h"
#include <algorithm>

Track::Track(int idx) : index(idx) {}

int Track::load_plugin(const std::string& path, int plugin_index, double sample_rate) {
    std::lock_guard<std::mutex> lock(mutex);
    auto plugin = std::make_unique<Vst3Plugin>();
    if (!plugin->load(path, plugin_index, sample_rate)) {
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
            ipc::sendClipInfo(index, slot, "");
        }
    }

    return target_idx;
}

bool Track::delete_clip(int slot) {
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

bool Track::load_clip(int slot, const std::string& path, bool is_loop) {
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
                ipc::sendParamList(index, (int)i, "", true, {});
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
        ipc::sendNotification(builder.GetBufferPointer(), builder.GetSize());
    }

    clips[slot] = std::move(clip);

    // If we are currently playing this slot, reset playback
    if (playing_slot == slot) {
        current_time_sec = 0.0;
        current_midi_idx = 0;
    }
    return true;
}

void Track::set_clip_loop(int slot, bool is_loop) {
    std::lock_guard<std::mutex> lock(mutex);
    if (clips.count(slot)) {
        clips[slot]->is_loop = is_loop;
    }
}

void Track::play_clip(int slot) {
    std::lock_guard<std::mutex> lock(mutex);
    if (clips.count(slot)) {
        playing_slot = slot;
        current_time_sec = 0.0;
        current_midi_idx = 0;
    }
}

void Track::stop() {
    std::lock_guard<std::mutex> lock(mutex);
    playing_slot = -1;
}

bool Track::remove_plugin(size_t pidx) {
    std::lock_guard<std::mutex> lock(mutex);
    if (pidx >= plugins.size()) return false;
    plugins.erase(plugins.begin() + pidx);
    return true;
}
