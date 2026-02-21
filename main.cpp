#include <atomic>
#include <chrono>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "MidiFile.h"

#include "alsa_out.hpp"
#include "vst3_host.hpp"

#include "hibiki_ipc_generated.h"

struct Clip {
    std::unique_ptr<smf::MidiFile> midi;
};

class Track {
public:
    int index;
    std::unique_ptr<Vst3Plugin> plugin;
    std::map<int, std::unique_ptr<Clip>> clips;

    int playing_slot = -1;
    double current_time_sec = 0.0;
    int current_midi_idx = 0;

    std::mutex mutex;

    Track(int idx) : index(idx) {}

    bool load_instrument(const std::string& path, int plugin_index) {
        std::lock_guard<std::mutex> lock(mutex);
        plugin = std::make_unique<Vst3Plugin>();
        if (!plugin->load(path, plugin_index)) {
            plugin.reset();
            return false;
        }
        // Reset playback state when instrument changes
        current_time_sec = 0.0;
        current_midi_idx = 0;
        return true;
    }



    bool load_clip(int slot, const std::string& path) {
        std::lock_guard<std::mutex> lock(mutex);
        auto midi = std::make_unique<smf::MidiFile>();
        if (!midi->read(path)) return false;
        midi->joinTracks();
        midi->doTimeAnalysis();

        auto clip = std::make_unique<Clip>();
        clip->midi = std::move(midi);
        clips[slot] = std::move(clip);

        // If we are currently playing this slot, reset playback
        if (playing_slot == slot) {
            current_time_sec = 0.0;
            current_midi_idx = 0;
        }
        return true;
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
};

struct GlobalState {
    std::atomic<bool> quit{false};
    std::map<int, std::unique_ptr<Track>> tracks;
    std::mutex tracks_mutex;

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
    context.tempo = 120.0;
    context.timeSigNumerator = 4;
    context.timeSigDenominator = 4;

    double time_per_block = block_size / (double)sample_rate;
    std::vector<float> mixBufferL(block_size);
    std::vector<float> mixBufferR(block_size);
    std::vector<float> interleaved(block_size * num_channels);

    while (!state.quit) {

        std::fill(mixBufferL.begin(), mixBufferL.end(), 0.0f);
        std::fill(mixBufferR.begin(), mixBufferR.end(), 0.0f);

        bool any_playing = false;

        {
            std::lock_guard<std::mutex> lock(state.tracks_mutex);
            for (auto& pair : state.tracks) {
                Track* track = pair.second.get();
                std::lock_guard<std::mutex> tlock(track->mutex);

                if (!track->plugin || track->playing_slot == -1) continue;

                any_playing = true;
                auto& clip = track->clips[track->playing_slot];
                smf::MidiFile* midifile = clip->midi.get();
                int num_midi_events = (*midifile)[0].getEventCount();

                context.continuousTimeSamples = track->current_time_sec * sample_rate;
                context.projectTimeMusic = track->current_time_sec * (context.tempo / 60.0);

                std::vector<MidiNoteEvent> blockEvents;
                while (track->current_midi_idx < num_midi_events) {
                    auto& me = (*midifile)[0][track->current_midi_idx];
                    if (me.seconds >= track->current_time_sec + time_per_block) break;

                    if (me.isNoteOn() || me.isNoteOff()) {
                        MidiNoteEvent e;
                        e.sampleOffset = std::max(0, (int)((me.seconds - track->current_time_sec) * sample_rate));
                        if (e.sampleOffset >= block_size) e.sampleOffset = block_size - 1;
                        e.channel = me.getChannel();
                        e.pitch = me.getKeyNumber();

                        if (me.isNoteOff() || (me.isNoteOn() && me.getVelocity() == 0)) {
                            e.isNoteOn = false;
                            e.velocity = 0;
                        } else {
                            e.isNoteOn = true;
                            e.velocity = me.getVelocity() / 127.0f;
                        }
                        blockEvents.push_back(e);
                    }
                    track->current_midi_idx++;
                }

                std::fill(bufferL, bufferL + block_size, 0.0f);
                std::fill(bufferR, bufferR + block_size, 0.0f);

                track->plugin->process(nullptr, outChannels, block_size, context, blockEvents);


                for (int i = 0; i < block_size; ++i) {
                    mixBufferL[i] += bufferL[i];
                    mixBufferR[i] += bufferR[i];
                }

                track->current_time_sec += time_per_block;
                if (track->current_midi_idx >= num_midi_events) {
                    track->playing_slot = -1; // Stop at end of clip for now
                }
            }
        }

        if (!any_playing) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        for (int i = 0; i < block_size; ++i) {
            interleaved[i * 2 + 0] = mixBufferL[i];
            interleaved[i * 2 + 1] = mixBufferR[i];
        }

        alsa.write(interleaved, block_size);
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

        auto message = hibiki::ipc::GetMessage(buffer.get());
        auto command_type = message->command_type();

        if (command_type == hibiki::ipc::Command_LoadInstrument) {
            auto cmd = static_cast<const hibiki::ipc::LoadInstrument*>(message->command());
            int tidx = cmd->track_index();
            std::string vpath = cmd->path()->str();
            int pidx = cmd->plugin_index();
            if (state.get_or_create_track(tidx)->load_instrument(vpath, pidx)) {
                std::cout << "ACK LOAD_INST " << tidx << "\n" << std::flush;
            } else {
                std::cout << "ERR LOAD_INST " << tidx << "\n" << std::flush;
            }
        } else if (command_type == hibiki::ipc::Command_LoadClip) {
            auto cmd = static_cast<const hibiki::ipc::LoadClip*>(message->command());
            int tidx = cmd->track_index();
            int sidx = cmd->slot_index();
            std::string mpath = cmd->path()->str();
            if (state.get_or_create_track(tidx)->load_clip(sidx, mpath)) {
                std::cout << "ACK LOAD_CLIP " << tidx << " " << sidx << "\n" << std::flush;
            } else {
                std::cout << "ERR LOAD_CLIP " << tidx << " " << sidx << "\n" << std::flush;
            }
        } else if (command_type == hibiki::ipc::Command_Play) {
            std::cout << "ACK PLAY\n" << std::flush;
        } else if (command_type == hibiki::ipc::Command_Stop) {
            std::lock_guard<std::mutex> lock(state.tracks_mutex);
            for (auto& pair : state.tracks) {
                pair.second->stop();
            }
            std::cout << "ACK STOP\n" << std::flush;
        } else if (command_type == hibiki::ipc::Command_PlayClip) {
            auto cmd = static_cast<const hibiki::ipc::PlayClip*>(message->command());
            int tidx = cmd->track_index();
            int sidx = cmd->slot_index();
            state.get_or_create_track(tidx)->play_clip(sidx);
            std::cout << "ACK PLAY_CLIP " << tidx << " " << sidx << "\n" << std::flush;
        } else if (command_type == hibiki::ipc::Command_StopTrack) {
            auto cmd = static_cast<const hibiki::ipc::StopTrack*>(message->command());
            int tidx = cmd->track_index();
            state.get_or_create_track(tidx)->stop();
            std::cout << "ACK STOP_TRACK " << tidx << "\n" << std::flush;
        } else if (command_type == hibiki::ipc::Command_ShowGui) {
            auto cmd = static_cast<const hibiki::ipc::ShowGui*>(message->command());
            int track_idx = cmd->track_index();
            std::cout << "Received SHOW_GUI for track " << track_idx << std::endl;
            std::lock_guard<std::mutex> lock(state.tracks_mutex);
            if (state.tracks.count(track_idx)) {
                if (state.tracks[track_idx]->plugin) {
                    state.tracks[track_idx]->plugin->showEditor();
                } else {
                    std::cout << "Track " << track_idx << " has no plugin" << std::endl;
                }
            } else {
                std::cout << "Track " << track_idx << " does not exist" << std::endl;
            }
        } else if (command_type == hibiki::ipc::Command_Quit) {
            state.quit = true;
            break;
        }
    }

    if (audio_thread.joinable()) audio_thread.join();
    return 0;
}
