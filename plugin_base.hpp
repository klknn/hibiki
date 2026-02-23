#pragma once

#include <string>
#include <vector>
#include "audio_types.hpp"

class BasePlugin {
public:
    virtual ~BasePlugin() = default;

    virtual bool load(const std::string& path, int plugin_index = 0) = 0;
    virtual void showEditor() = 0;
    virtual void stopEditor() = 0;
    virtual void process(float** inputs, float** outputs, int numSamples, 
                         const HostProcessContext& context, 
                         const std::vector<MidiNoteEvent>& events) = 0;

    virtual int getParameterCount() const = 0;
    virtual bool getParameterInfo(int index, VstParamInfo& info) const = 0;
    virtual void setParameterValue(uint32_t id, double valueNormalized) = 0;
    virtual double getParameterValue(uint32_t id) const = 0;
    virtual const std::string& getName() const = 0;
    virtual const std::string& getPath() const = 0;
    virtual int getPluginIndex() const = 0;
    virtual bool isInstrument() const = 0;
};
