#include <iostream>
#include <vector>
#include <thread>
#include <chrono>

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
        if (index < 0 || index >= events.size()) return Steinberg::kResultFalse;
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

int main(int argc, char** argv) {
    if (argc < 3) {

        std::cerr << "Usage: hbk-play <vst3_path> <midi_path> [options]\n"
                  << "Options:\n"
                  << "  -o, --output <file>    Dump audio to WAV file\n"
                  << "  -d, --duration <sec>   Maximum playback duration in seconds\n";
        return 1;
    }

    std::string vst3_path = argv[1];
    std::string midi_path = argv[2];
    std::string wav_path;
    double max_duration = -1.0;

    for (int i = 3; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-o" || arg == "--output") {
            if (i + 1 < argc) wav_path = argv[++i];
        } else if (arg == "-d" || arg == "--duration") {
            if (i + 1 < argc) max_duration = std::stod(argv[++i]);
        }
    }

    std::cout << "Loading VST3: " << vst3_path << "\n" << std::flush;
    Vst3Plugin plugin;
    if (!plugin.load(vst3_path)) {
        return 1;
    }

    std::cout << "Loading MIDI: " << midi_path << "\n" << std::flush;
    smf::MidiFile midifile;
    if (!midifile.read(midi_path)) {
        std::cerr << "Failed to read MIDI file\n";
        return 1;
    }
    
    midifile.joinTracks(); // Merge all tracks
    midifile.doTimeAnalysis(); // Calculate timestamps in seconds

    AlsaPlayback alsa(44100, 2);
    if (!alsa.is_ready()) {
        return 1;
    }

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

    // Add valid stereo input bus to avoid JUCE wrapper crashes
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
    context.projectTimeMusic = 0;
    context.systemTime = 0;
    context.continousTimeSamples = 0;
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

    int current_midi_idx = 0;
    int num_midi_events = midifile.getEventCount(0);
    double current_time_sec = 0.0;
    double time_per_block = block_size / sample_rate;

    std::cout << "Playing...\n" << std::flush;
    std::vector<float> interleaved(block_size * num_channels);

    // Prepare WAV file if path is provided
    FILE* wav_file = nullptr;
    if (!wav_path.empty()) {
        wav_file = fopen(wav_path.c_str(), "wb");
        if (wav_file) {
            std::cout << "Dumping to WAV: " << wav_path << "\n" << std::flush;
            // Write temporary header, we will fill sizes later
            uint32_t header[11] = {
                0x46464952, // "RIFF"
                0, // Chunk size
                0x45564157, // "WAVE"
                0x20746d66, // "fmt "
                16, // Subchunk1Size
                (uint32_t)((num_channels << 16) | 3), // IEEE Float
                (uint32_t)sample_rate,
                (uint32_t)(sample_rate * num_channels * sizeof(float)), // ByteRate
                (uint32_t)(((sizeof(float) * 8) << 16) | (num_channels * sizeof(float))), // BitsPerSample | BlockAlign
                0x61746164, // "data"
                0 // Subchunk2Size
            };
            fwrite(header, sizeof(header), 1, wav_file);
        }
    }

    uint32_t total_frames_written = 0;

    while (current_midi_idx < num_midi_events || (current_midi_idx >= num_midi_events && (max_duration < 0 || current_time_sec < max_duration))) {
        // Stop if max duration is reached
        if (max_duration > 0 && current_time_sec >= max_duration) {
            break;
        }

        // If we ran out of MIDI events, we might still want to play for max_duration (e.g. reverb tail)
        // But if max_duration is not set, we stop when MIDI events are done (and maybe a bit of tail?)
        // The original condition was `current_midi_idx < num_midi_events`. 
        // Let's stick to playing at least until MIDI ends, OR until max_duration if set.
        // If MIDI ends but max_duration is set and we haven't reached it, we continue.
        // If max_duration is NOT set, we stop when MIDI ends (original behavior).
        if (max_duration < 0 && current_midi_idx >= num_midi_events) {
             // Add a small tail? For now just break to match old behavior unless we want a default tail.
             break;
        }

        context.continousTimeSamples = current_time_sec * sample_rate;
        context.projectTimeMusic = current_time_sec * (context.tempo / 60.0);

        eventList.clear();

        // Add events falling into this block
        while (current_midi_idx < num_midi_events) {
            auto& me = midifile[0][current_midi_idx];
            if (me.seconds >= current_time_sec + time_per_block) {
                break; // Event is in the future
            }

            if (me.isNoteOn() || me.isNoteOff()) {
                Steinberg::Vst::Event e = {};
                e.sampleOffset = std::max(0, (int)((me.seconds - current_time_sec) * sample_rate));
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
            current_midi_idx++;
        }

        // Clear output buffers
        for(int i=0; i<num_channels; ++i) {
            std::fill(outChannels[i], outChannels[i] + block_size, 0.0f);
        }

        plugin.processor->process(data);

        // Interleave for ALSA
        for (int i = 0; i < block_size; ++i) {
            for (int ch = 0; ch < num_channels; ++ch) {
                interleaved[i * num_channels + ch] = outChannels[ch][i];
            }
        }

        alsa.write(interleaved, block_size);
        if (wav_file) {
            fwrite(interleaved.data(), sizeof(float), interleaved.size(), wav_file);
            total_frames_written += block_size;
        }

        current_time_sec += time_per_block;
    }

    if (wav_file) {
        // Finalize WAV Header Sizes
        uint32_t data_size = total_frames_written * num_channels * sizeof(float);
        uint32_t file_size = 36 + data_size;
        fseek(wav_file, 4, SEEK_SET);
        fwrite(&file_size, sizeof(uint32_t), 1, wav_file);
        fseek(wav_file, 40, SEEK_SET);
        fwrite(&data_size, sizeof(uint32_t), 1, wav_file);
        fclose(wav_file);
    }

    std::cout << "Finished playback.\n";
    return 0;
}
