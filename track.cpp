#include "track.hpp"
#include "ipc.hpp"
#include "audio_file.hpp"
#include "hibiki_response_generated.h"
#include <algorithm>
#include <iostream>

namespace hibiki {

int Track::LoadPlugin(const std::string& path, int plugin_index, double sample_rate) {
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
            if (clip->type == Clip::Type::AUDIO) audio_slots.push_back(slot);
        }
        for (int slot : audio_slots) {
            clips.erase(slot);
            hibiki::sendClipInfo(index, slot, "", "");
        }
    }

    return target_idx;
}

bool Track::DeleteClip(int slot) {
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

bool Track::LoadClip(int slot, const std::string& path, bool is_loop) {
    std::lock_guard<std::mutex> lock(mutex);
    
    auto clip = hibiki::LoadClip(path, is_loop);
    if (!clip) return false;

    // Exclusivity rule: If loading an audio clip, clear instruments
    if (clip->type == Clip::Type::AUDIO) {
        for (size_t i = 0; i < plugins.size(); ++i) {
            if (plugins[i]->isInstrument()) {
                hibiki::sendParamList(index, (int)i, "", true, {});
            }
        }
        plugins.erase(std::remove_if(plugins.begin(), plugins.end(), [](const auto& p) {
            return p->isInstrument();
        }), plugins.end());
    }

    // Send waveform to GUI if generated
    if (clip->type == Clip::Type::AUDIO && !clip->waveform_summary.empty()) {
        flatbuffers::FlatBufferBuilder builder(1024 + clip->waveform_summary.size() * 4);
        auto wf_vec = builder.CreateVector(clip->waveform_summary);
        auto wf_off = hibiki::ipc::CreateClipWaveform(builder, index, slot, wf_vec);
        auto nf_off = hibiki::ipc::CreateNotification(builder, hibiki::ipc::Response_ClipWaveform, wf_off.Union());
        builder.Finish(nf_off);
        hibiki::sendNotification(builder.GetBufferPointer(), builder.GetSize());
    }

    clips[slot] = std::move(clip);

    // If we are currently playing this slot, reset playback
    if (playing_slot == slot) {
        current_time_sec = 0.0;
        current_midi_idx = 0;
    }
    return true;
}

void Track::SetClipLoop(int slot, bool is_loop) {
    std::lock_guard<std::mutex> lock(mutex);
    if (clips.count(slot)) {
        clips[slot]->is_loop = is_loop;
    }
}

void Track::PlayClip(int slot) {
    std::lock_guard<std::mutex> lock(mutex);
    if (clips.count(slot)) {
        playing_slot = slot;
        current_time_sec = 0.0;
        current_midi_idx = 0;
    }
}

void Track::Stop() {
    std::lock_guard<std::mutex> lock(mutex);
    playing_slot = -1;
}

bool Track::RemovePlugin(size_t pidx) {
    std::lock_guard<std::mutex> lock(mutex);
    if (pidx >= plugins.size()) return false;
    plugins.erase(plugins.begin() + pidx);
    return true;
}

} // namespace hibiki
