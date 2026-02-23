#pragma once

#include <string>
#include <vector>
#include <memory>
#include <optional>

#include "base_plugin.hpp"

struct Vst3PluginImpl;
class Vst3PluginParameters;
class Vst3PluginEditor;

class Vst3Plugin : public BasePlugin {
public:
    Vst3Plugin();
    ~Vst3Plugin() override;

    bool load(const std::string& path, int plugin_index = 0) override;
    void process(float** inputs, float** outputs, int numSamples, 
                 const HostProcessContext& context, 
                 const std::vector<MidiNoteEvent>& events) override;

    PluginParameters* getParameters() override;
    PluginEditor* getEditor() override;

    const std::string& getName() const override;
    const std::string& getPath() const override;
    int getPluginIndex() const override;
    bool isInstrument() const override;

    static void listPlugins(const std::string& path);

private:
    std::unique_ptr<Vst3PluginImpl> impl;
    std::unique_ptr<Vst3PluginParameters> parameters;
    std::unique_ptr<Vst3PluginEditor> editor;
};

class Vst3PluginParameters : public PluginParameters {
public:
    Vst3PluginParameters(Vst3PluginImpl& impl);
    int size() const override;
    std::optional<ParamInfo> info(int index) const override;
    void setNormalized(uint32_t id, double valueNormalized) override;
    double getNormalized(uint32_t id) const override;

private:
    Vst3PluginImpl& impl;
};

class Vst3PluginEditor : public PluginEditor {
public:
    Vst3PluginEditor(Vst3PluginImpl& impl);
    void open() override;
    void close() override;

private:
    Vst3PluginImpl& impl;
};
