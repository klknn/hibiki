#pragma once
#include <string>

#include <vector>
#include <memory>
#include <cstdint>

struct HostProcessContext {
    double sampleRate;
    double tempo;
    int32_t timeSigNumerator;
    int32_t timeSigDenominator;
    int64_t continuousTimeSamples;
    double projectTimeMusic;
};

struct MidiNoteEvent {
    int32_t sampleOffset;
    uint8_t channel;
    uint8_t pitch;
    float velocity; // 0.0 - 1.0
    bool isNoteOn;
};

struct Vst3PluginImpl;

class Vst3Plugin {
public:
    Vst3Plugin();
    ~Vst3Plugin();

    bool load(const std::string& path, int plugin_index = 0);
    void showEditor();
    void stopEditor();
    void process(float** inputs, float** outputs, int numSamples, 
                 const HostProcessContext& context, 
                 const std::vector<MidiNoteEvent>& events);

    static void listPlugins(const std::string& path);

private:
    std::unique_ptr<Vst3PluginImpl> impl;
};

