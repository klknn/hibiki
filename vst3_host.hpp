#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "audio_types.hpp"
#include "plugin_base.hpp"

struct Vst3PluginImpl;

class Vst3Plugin : public BasePlugin {
public:
    Vst3Plugin();
    ~Vst3Plugin() override;

    bool load(const std::string& path, int plugin_index = 0) override;
    void showEditor() override;
    void stopEditor() override;
    void process(float** inputs, float** outputs, int numSamples, 
                 const HostProcessContext& context, 
                 const std::vector<MidiNoteEvent>& events) override;

    int getParameterCount() const override;
    bool getParameterInfo(int index, VstParamInfo& info) const override;
    void setParameterValue(uint32_t id, double valueNormalized) override;
    double getParameterValue(uint32_t id) const override;
    const std::string& getName() const override;
    const std::string& getPath() const override;
    int getPluginIndex() const override;
    bool isInstrument() const override;

    static void listPlugins(const std::string& path);

private:
    std::unique_ptr<Vst3PluginImpl> impl;
};

