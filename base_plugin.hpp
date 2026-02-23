#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <optional>

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

struct ParamInfo {
    uint32_t id;
    std::string name;
    double defaultValue;
};

class PluginParameters {
public:
    virtual ~PluginParameters() = default;
    virtual int size() const = 0;
    virtual std::optional<ParamInfo> info(int index) const = 0;
    virtual void setNormalized(uint32_t id, double valueNormalized) = 0;
    virtual double getNormalized(uint32_t id) const = 0;
};

class PluginEditor {
public:
    virtual ~PluginEditor() = default;
    virtual void open() = 0;
    virtual void close() = 0;
};

class BasePlugin {
public:
    virtual ~BasePlugin() = default;

    virtual bool load(const std::string& path, int plugin_index = 0) = 0;
    virtual void process(float** inputs, float** outputs, int numSamples, 
                         const HostProcessContext& context, 
                         const std::vector<MidiNoteEvent>& events) = 0;

    virtual PluginParameters* getParameters() = 0;
    virtual PluginEditor* getEditor() = 0;

    virtual const std::string& getName() const = 0;
    virtual const std::string& getPath() const = 0;
    virtual int getPluginIndex() const = 0;
    virtual bool isInstrument() const = 0;
};
