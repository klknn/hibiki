#include <iostream>
#include <vector>
#include <thread>
#include <chrono>
#include <atomic>
#include <string>
#include <sstream>


#include "vst3_host.hpp"
#include "alsa_out.hpp"

#include "MidiFile.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"

// Very basic event list
class VstEventList : public Steinberg::Vst::IEventList {
    std::vector<Steinberg::Vst::Event> events;
public:
    VstEventList() {}
    virtual ~VstEventList() {}
    
    DECLARE_FUNKNOWN_METHODS
    Steinberg::int32 PLUGIN_API getEventCount() override { return events.size(); }
    Steinberg::tresult PLUGIN_API getEvent(Steinberg::int32 index, Steinberg::Vst::Event& e) override {
        if (index < 0 || index >= (int)events.size()) return Steinberg::kResultFalse;
        e = events[index];
        return Steinberg::kResultTrue;
    }
    Steinberg::tresult PLUGIN_API addEvent(Steinberg::Vst::Event& e) override {
        events.push_back(e);
        return Steinberg::kResultTrue;
    }
    void clear() { events.clear(); }
};

// We must provide an implementation of IEventList methods for fUnknown
IMPLEMENT_FUNKNOWN_METHODS(VstEventList, Steinberg::Vst::IEventList, Steinberg::Vst::IEventList::iid)

#include <mutex>
#include <map>
#include <memory>

// Forward declaration of VstEventList if needed, but it's defined below.

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

void playback_thread(GlobalState* state) {
    AlsaPlayback alsa(44100, 2);
    if (!alsa.is_ready()) return;

    int block_size = 512;
    float sample_rate = 44100.0f;
    int num_channels = 2;

    alignas(32) float bufferL[512];
    alignas(32) float bufferR[512];
    std::vector<float*> outChannels = {bufferL, bufferR};

    alignas(32) float inBufferL[512] = {0};
    alignas(32) float inBufferR[512] = {0};
    std::vector<float*> inChannels = {inBufferL, inBufferR};

    Steinberg::Vst::AudioBusBuffers inputs;
    inputs.numChannels = 2;
    inputs.silenceFlags = 0;
    inputs.channelBuffers32 = inChannels.data();

    Steinberg::Vst::AudioBusBuffers outputs;
    outputs.numChannels = num_channels;
    outputs.silenceFlags = 0;
    outputs.channelBuffers32 = outChannels.data();

    VstEventList eventList;

    Steinberg::Vst::ProcessContext context = {};
    context.state = Steinberg::Vst::ProcessContext::kPlaying;
    context.sampleRate = sample_rate;
    context.tempo = 120.0;
    context.timeSigNumerator = 4;
    context.timeSigDenominator = 4;

    Steinberg::Vst::ProcessData data;
    data.processMode = Steinberg::Vst::kRealtime;
    data.symbolicSampleSize = Steinberg::Vst::kSample32;
    data.numSamples = block_size;
    data.numInputs = 1;
    data.inputs = &inputs;
    data.numOutputs = 1;
    data.outputs = &outputs;
    data.inputParameterChanges = nullptr;
    data.outputParameterChanges = nullptr;
    data.inputEvents = &eventList;
    data.outputEvents = nullptr;
    data.processContext = &context;

    double time_per_block = block_size / (double)sample_rate;
    std::vector<float> mixBufferL(block_size);
    std::vector<float> mixBufferR(block_size);
    std::vector<float> interleaved(block_size * num_channels);

    while (!state->quit) {
        std::fill(mixBufferL.begin(), mixBufferL.end(), 0.0f);
        std::fill(mixBufferR.begin(), mixBufferR.end(), 0.0f);

        bool any_playing = false;
        
        {
            std::lock_guard<std::mutex> lock(state->tracks_mutex);
            for (auto& pair : state->tracks) {
                Track* track = pair.second.get();
                std::lock_guard<std::mutex> tlock(track->mutex);
                
                if (!track->plugin || track->playing_slot == -1) continue;
                
                any_playing = true;
                auto& clip = track->clips[track->playing_slot];
                smf::MidiFile* midifile = clip->midi.get();
                int num_midi_events = (*midifile)[0].getEventCount();

                context.continousTimeSamples = track->current_time_sec * sample_rate;
                context.projectTimeMusic = track->current_time_sec * (context.tempo / 60.0);

                eventList.clear();
                while (track->current_midi_idx < num_midi_events) {
                    auto& me = (*midifile)[0][track->current_midi_idx];
                    if (me.seconds >= track->current_time_sec + time_per_block) break;

                    if (me.isNoteOn() || me.isNoteOff()) {
                        Steinberg::Vst::Event e = {};
                        e.sampleOffset = std::max(0, (int)((me.seconds - track->current_time_sec) * sample_rate));
                        if (e.sampleOffset >= block_size) e.sampleOffset = block_size - 1;

                        if (me.isNoteOff() || (me.isNoteOn() && me.getVelocity() == 0)) {
                            e.type = Steinberg::Vst::Event::kNoteOffEvent;
                            e.noteOff.channel = me.getChannel();
                            e.noteOff.pitch = me.getKeyNumber();
                        } else {
                            e.type = Steinberg::Vst::Event::kNoteOnEvent;
                            e.noteOn.channel = me.getChannel();
                            e.noteOn.pitch = me.getKeyNumber();
                            e.noteOn.velocity = me.getVelocity() / 127.0f;
                        }
                        eventList.addEvent(e);
                    }
                    track->current_midi_idx++;
                }

                std::fill(bufferL, bufferL + block_size, 0.0f);
                std::fill(bufferR, bufferR + block_size, 0.0f);
                
                track->plugin->processor->process(data);

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
    std::thread audio_thread(playback_thread, &state);

    std::string line;
    while (std::getline(std::cin, line)) {
        std::stringstream ss(line);
        std::string cmd;
        ss >> cmd;
        
        if (cmd == "LOAD_INST") {
            int tidx, pidx = 0;
            std::string vpath;
            ss >> tidx >> vpath;
            if (ss >> pidx) {}
            if (state.get_or_create_track(tidx)->load_instrument(vpath, pidx)) {
                std::cout << "ACK LOAD_INST " << tidx << "\n" << std::flush;
            } else {
                std::cout << "ERR LOAD_INST " << tidx << "\n" << std::flush;
            }
        } else if (cmd == "LOAD_CLIP") {
            int tidx, sidx;
            std::string mpath;
            ss >> tidx >> sidx >> mpath;
            if (state.get_or_create_track(tidx)->load_clip(sidx, mpath)) {
                std::cout << "ACK LOAD_CLIP " << tidx << " " << sidx << "\n" << std::flush;
            } else {
                std::cout << "ERR LOAD_CLIP " << tidx << " " << sidx << "\n" << std::flush;
            }
        } else if (cmd == "PLAY_CLIP") {
            int tidx, sidx;
            if (ss >> tidx >> sidx) {
                state.get_or_create_track(tidx)->play_clip(sidx);
                std::cout << "ACK PLAY_CLIP " << tidx << " " << sidx << "\n" << std::flush;
            }
        } else if (cmd == "STOP_TRACK") {
            int tidx;
            if (ss >> tidx) {
                state.get_or_create_track(tidx)->stop();
                std::cout << "ACK STOP_TRACK " << tidx << "\n" << std::flush;
            }
        } else if (cmd == "QUIT") {
            state.quit = true;
            break;
        }
    }

    if (audio_thread.joinable()) audio_thread.join();
    return 0;
}


