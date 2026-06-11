#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "engine/plugin/iplugin.hpp"

namespace hibiki {

// A mock plugin implementation for testing and inspecting internal states.
class MockPlugin : public IPlugin {
 public:
  static constexpr const char* kPath = "builtin://mock_plugin";

  MockPlugin();
  ~MockPlugin() override;

  // Loads the mock plugin with a given path and index.
  bool load(const std::string& path, int plugin_index = 0,
            double sample_rate = 44100.0) override;

  // Mock implementation of GUI editor control.
  void showEditor() override;
  void stopEditor() override;

  // Processes audio by generating silence.
  void process(float** inputs, float** outputs, int num_samples,
               const HostProcessContext& context,
               const std::vector<MidiNoteEvent>& events,
               float** sidechain = nullptr) override;

  // Returns the count of mocked parameters (always 1).
  int getParameterCount() const override;

  // Populates details of the mocked parameter.
  bool getParameterInfo(int index, VstParamInfo& info) const override;

  // Sets the value of the mocked parameter.
  void setParameterValue(uint32_t id, double valueNormalized) override;

  // Returns the current value of the mocked parameter.
  double getParameterValue(uint32_t id) const override;

  // Returns the display name of the plugin.
  const std::string& getName() const override;

  // Returns the virtual plugin path.
  const std::string& getPath() const override;

  // Returns the plugin's index in the track chain.
  int getPluginIndex() const override;

  // Always returns true as this is mocked as an instrument.
  bool isInstrument() const override;

  // Populates the state vector with the mocked internal state data.
  bool getState(std::vector<uint8_t>& state) const override;

  // Overwrites the mocked internal state data.
  bool setState(const std::vector<uint8_t>& state) override;

  // Overwrites the mock-specific state payload for testing.
  void setInternalMockData(const std::vector<uint8_t>& data);

  // Returns the mock-specific state payload.
  const std::vector<uint8_t>& getInternalMockData() const;

 private:
  std::string name_ = "Mock Plugin";
  std::string path_ = kPath;
  int plugin_index_ = 0;
  std::vector<uint8_t> mock_state_;
  double param_value_ = 0.5;
};

}  // namespace hibiki
