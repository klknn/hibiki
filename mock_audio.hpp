#pragma once

#include "playback_base.hpp"
#include "plugin_base.hpp"
#include <cmath>
#include <iostream>

class MockPlayback : public BasePlayback {
public:
    std::vector<float> recorded_data;
    bool is_ready() const override { return true; }
    void write(const std::vector<float>& interleaved_data, int num_frames) override {
        recorded_data.insert(recorded_data.end(), interleaved_data.begin(), interleaved_data.end());
    }
};

class MockPlugin : public BasePlugin {
    std::string name = "MockPlugin";
    std::string path = "mock://plugin";
    bool note_on = false;
    float phase = 0.0f;
    float freq = 440.0f;

public:
    bool load(const std::string& p, int idx = 0) override {
        path = p;
        return true;
    }
    void showEditor() override {}
    void stopEditor() override {}
    void process(float** inputs, float** outputs, int numSamples, 
                 const HostProcessContext& context, 
                 const std::vector<MidiNoteEvent>& events) override {
        for (const auto& e : events) {
            if (e.isNoteOn) {
                note_on = true;
                freq = 440.0f * pow(2.0f, (e.pitch - 69.0f) / 12.0f);
            } else {
                note_on = false;
            }
        }

        for (int i = 0; i < numSamples; ++i) {
            float val = 0.0f;
            if (note_on) {
                val = 0.5f * sin(phase);
                phase += 2.0f * M_PI * freq / context.sampleRate;
                if (phase > 2.0f * M_PI) phase -= 2.0f * M_PI;
            }
            if (outputs[0]) outputs[0][i] = val;
            if (outputs[1]) outputs[1][i] = val;
        }
    }

    int getParameterCount() const override { return 0; }
    bool getParameterInfo(int index, VstParamInfo& info) const override { return false; }
    void setParameterValue(uint32_t id, double valueNormalized) override {}
    double getParameterValue(uint32_t id) const override { return 0.0; }
    const std::string& getName() const override { return name; }
    const std::string& getPath() const override { return path; }
    int getPluginIndex() const override { return 0; }
    bool isInstrument() const override { return true; }
};
