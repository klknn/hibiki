#include "engine/plugin/mock_plugin.hpp"

#include <algorithm>
#include <cstring>

namespace hibiki {

MockPlugin::MockPlugin() : mock_state_({0x12, 0x34, 0x56, 0x78}) {}

MockPlugin::~MockPlugin() = default;

bool MockPlugin::load(const std::string& path, int plugin_index,
                      double /*sample_rate*/) {
  path_ = path;
  plugin_index_ = plugin_index;
  return true;
}

void MockPlugin::showEditor() {}

void MockPlugin::stopEditor() {}

void MockPlugin::process(float** /*inputs*/, float** outputs, int num_samples,
                         const HostProcessContext& /*context*/,
                         const std::vector<MidiNoteEvent>& /*events*/,
                         float** /*sidechain*/) {
  // Mock outputs silence
  if (outputs && outputs[0]) {
    std::memset(outputs[0], 0, num_samples * sizeof(float));
  }
  if (outputs && outputs[1]) {
    std::memset(outputs[1], 0, num_samples * sizeof(float));
  }
}

int MockPlugin::getParameterCount() const { return 1; }

bool MockPlugin::getParameterInfo(int index, VstParamInfo& info) const {
  if (index != 0) return false;
  info.id = 0;
  info.name = "MockParam";
  info.defaultValue = 0.5;
  return true;
}

void MockPlugin::setParameterValue(uint32_t id, double valueNormalized) {
  if (id == 0) {
    param_value_ = valueNormalized;
  }
}

double MockPlugin::getParameterValue(uint32_t id) const {
  if (id == 0) return param_value_;
  return 0.0;
}

const std::string& MockPlugin::getName() const { return name_; }

const std::string& MockPlugin::getPath() const { return path_; }

int MockPlugin::getPluginIndex() const { return plugin_index_; }

bool MockPlugin::isInstrument() const { return true; }

bool MockPlugin::getState(std::vector<uint8_t>& state) const {
  state = mock_state_;
  return true;
}

bool MockPlugin::setState(const std::vector<uint8_t>& state) {
  mock_state_ = state;
  return true;
}

void MockPlugin::setInternalMockData(const std::vector<uint8_t>& data) {
  mock_state_ = data;
}

const std::vector<uint8_t>& MockPlugin::getInternalMockData() const {
  return mock_state_;
}

}  // namespace hibiki
