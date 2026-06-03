#pragma once

#include <memory>
#include <string>
#include <vector>

#include "engine/plugin/iplugin.hpp"

namespace hibiki {

/**
 * @brief A built-in polyphonic Electric Piano instrument.
 *
 * Ports the DPlug epiano2 instrument (based on MDA ePiano) which features
 * sample playback with LFO (tremolo/autopan), treble boost, and overdrive.
 */
class BuiltinEPiano : public IPlugin {
 public:
  static constexpr int kTotalParams = 14;
  static constexpr const char* kPath = "builtin://epiano";
  static constexpr const char* kName = "Electric Piano";

  enum ParamId {
    P_ENV_DECAY = 0,
    P_ENV_RELEASE = 1,
    P_HARDNESS = 2,
    P_TREBLE_BOOST = 3,
    P_MODULATION = 4,
    P_LFO_RATE = 5,
    P_VEL_SENSE = 6,
    P_STEREO_WIDTH = 7,
    P_POLYPHONY = 8,
    P_FINE_TUNING = 9,
    P_RANDOM_TUNING = 10,
    P_OVERDRIVE = 11,
    P_VOLUME = 12,
    P_ENABLE = 13,
  };

  /**
   * @brief Constructs a new BuiltinEPiano instrument.
   */
  BuiltinEPiano();

  /**
   * @brief Destructor.
   */
  ~BuiltinEPiano() override;

  // IPlugin interface
  bool load(const std::string& path, int plugin_index = 0,
            double sample_rate = 44100.0) override;
  void showEditor() override {}
  void stopEditor() override {}
  void process(float** inputs, float** outputs, int num_samples,
               const HostProcessContext& context,
               const std::vector<MidiNoteEvent>& events,
               float** sidechain = nullptr) override;
  int getParameterCount() const override;
  bool getParameterInfo(int index, VstParamInfo& info) const override;
  void setParameterValue(uint32_t id, double value) override;
  double getParameterValue(uint32_t id) const override;
  const std::string& getName() const override;
  const std::string& getPath() const override;
  int getPluginIndex() const override;
  bool isInstrument() const override;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace hibiki
