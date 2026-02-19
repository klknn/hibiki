#include <iostream>
#include <vector>
#include <thread>
#include <chrono>
#include <atomic>
#include <string>

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

struct PlaybackState {
    std::atomic<bool> playing{false};
    std::atomic<bool> quit{false};
    std::atomic<double> current_time_sec{0.0};
    std::atomic<int> current_midi_idx{0};
};

void playback_thread(Vst3Plugin* plugin, smf::MidiFile* midifile, PlaybackState* state) {
    AlsaPlayback alsa(44100, 2);
    if (!alsa.is_ready()) return;

    int block_size = 512;
    float sample_rate = 44100.0f;
    int num_channels = 2;

    alignas(32) float bufferL[512] = {0};
    alignas(32) float bufferR[512] = {0};
    std::vector<float*> outChannels = {bufferL, bufferR};

    Steinberg::Vst::AudioBusBuffers outputs;
    outputs.numChannels = num_channels;
    outputs.silenceFlags = 0;
    outputs.channelBuffers32 = outChannels.data();

    alignas(32) float inBufferL[512] = {0};
    alignas(32) float inBufferR[512] = {0};
    std::vector<float*> inChannels = {inBufferL, inBufferR};

    Steinberg::Vst::AudioBusBuffers inputs;
    inputs.numChannels = 2;
    inputs.silenceFlags = 0;
    inputs.channelBuffers32 = inChannels.data();

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
    std::vector<float> interleaved(block_size * num_channels);

    while (!state->quit) {
        if (!state->playing) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        int num_midi_events = (*midifile)[0].getEventCount();
        
        context.continousTimeSamples = state->current_time_sec * sample_rate;
        context.projectTimeMusic = state->current_time_sec * (context.tempo / 60.0);

        eventList.clear();

        while (state->current_midi_idx < num_midi_events) {
            auto& me = (*midifile)[0][state->current_midi_idx];
            if (me.seconds >= state->current_time_sec + time_per_block) break;

            if (me.isNoteOn() || me.isNoteOff()) {
                Steinberg::Vst::Event e = {};
                e.sampleOffset = std::max(0, (int)((me.seconds - state->current_time_sec) * sample_rate));
                if (e.sampleOffset >= block_size) e.sampleOffset = block_size - 1;

                if (me.isNoteOff() || (me.isNoteOn() && me.getVelocity() == 0)) {
                    e.type = Steinberg::Vst::Event::kNoteOffEvent;
                    e.noteOff.channel = me.getChannel();
                    e.noteOff.pitch = me.getKeyNumber();
                    e.noteOff.velocity = 0.0f;
                    e.noteOff.noteId = -1;
                    e.noteOff.tuning = 0.0f;
                } else {
                    e.type = Steinberg::Vst::Event::kNoteOnEvent;
                    e.noteOn.channel = me.getChannel();
                    e.noteOn.pitch = me.getKeyNumber();
                    e.noteOn.velocity = me.getVelocity() / 127.0f;
                    e.noteOn.noteId = -1;
                    e.noteOn.tuning = 0.0f;
                    e.noteOn.length = 0;
                }
                eventList.addEvent(e);
            }
            state->current_midi_idx++;
        }

        for (int i = 0; i < num_channels; ++i) std::fill(outChannels[i], outChannels[i] + block_size, 0.0f);

        plugin->processor->process(data);

        for (int i = 0; i < block_size; ++i) {
            for (int ch = 0; ch < num_channels; ++ch) {
                interleaved[i * num_channels + ch] = outChannels[ch][i];
            }
        }

        alsa.write(interleaved, block_size);
        state->current_time_sec = state->current_time_sec + time_per_block;

        if (state->current_midi_idx >= num_midi_events) {
            state->playing = false;
        }
    }
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage:\n"
                  << "  " << argv[0] << " --list <vst3_path>\n"
                  << "  " << argv[0] << " <vst3_path> <midi_path> [plugin_index]\n";
        return 1;
    }

    if (std::string(argv[1]) == "--list") {
        if (argc < 3) {
            std::cerr << "Usage: " << argv[0] << " --list <vst3_path>\n";
            return 1;
        }
        Vst3Plugin::listPlugins(argv[2]);
        return 0;
    }

    if (argc < 3) {
        std::cerr << "Usage: " << argv[0] << " <vst3_path> <midi_path> [plugin_index]\n";
        return 1;
    }

    std::string vst3_path = argv[1];
    std::string midi_path = argv[2];
    int plugin_index = 0;
    if (argc >= 4) {
        plugin_index = std::stoi(argv[3]);
    }

    Vst3Plugin plugin;
    if (!plugin.load(vst3_path, plugin_index)) return 1;


    smf::MidiFile midifile;
    if (!midifile.read(midi_path)) {
        std::cerr << "Failed to read MIDI file\n";
        return 1;
    }
    midifile.joinTracks();
    midifile.doTimeAnalysis();

    PlaybackState state;
    std::thread audio_thread(playback_thread, &plugin, &midifile, &state);

    std::string line;
    while (std::getline(std::cin, line)) {
        if (line == "PLAY") {
            state.current_midi_idx = 0;
            state.current_time_sec = 0.0;
            state.playing = true;
            std::cout << "ACK PLAY\n" << std::flush;
        } else if (line == "STOP") {
            state.playing = false;
            std::cout << "ACK STOP\n" << std::flush;
        } else if (line == "QUIT") {
            state.quit = true;
            break;
        }
    }

    if (audio_thread.joinable()) audio_thread.join();
    return 0;
}

