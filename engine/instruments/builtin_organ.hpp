#pragma once

#include <memory>
#include <string>
#include <vector>

#include "engine/plugin/iplugin.hpp"

namespace hibiki {

/**
 * @brief A built-in polyphonic Simple Drawbar Organ instrument.
 *
 * Implements additive synthesis simulating drawbar organs by summing
 * fundamental and harmonic sine waves (9 drawbars). Features Leslie-style
 * rotary speaker emulation (spatial vibrato/tremolo) and a percussion key click
 * envelope.
 */
class BuiltinOrgan : public IPlugin {
 public:
  static constexpr int kTotalParams = 13;
  static constexpr const char* kPath = "builtin://organ";
  static constexpr const char* kName = "Drawbar Organ";

  enum ParamId {
    PARAM_DRAWBAR_1 = 0,  ///< 16' (sub-octave, fundamental * 0.5)
    PARAM_DRAWBAR_2 = 1,  ///< 5 1/3' (3rd harmonic of 16', fundamental * 1.5)
    PARAM_DRAWBAR_3 = 2,  ///< 8' (fundamental)
    PARAM_DRAWBAR_4 = 3,  ///< 4' (2nd harmonic, fundamental * 2.0)
    PARAM_DRAWBAR_5 = 4,  ///< 2 2/3' (3rd harmonic, fundamental * 3.0)
    PARAM_DRAWBAR_6 = 5,  ///< 2' (4th harmonic, fundamental * 4.0)
    PARAM_DRAWBAR_7 = 6,  ///< 1 3/5' (5th harmonic, fundamental * 5.0)
    PARAM_DRAWBAR_8 = 7,  ///< 1 1/3' (6th harmonic, fundamental * 6.0)
    PARAM_DRAWBAR_9 = 8,  ///< 1' (8th harmonic, fundamental * 8.0)
    PARAM_PERCUSSION_ENABLE = 9,  ///< Percussion enable (0.0 or 1.0)
    PARAM_PERCUSSION_DECAY = 10,  ///< Percussion decay time (0.05s to 1.0s)
    PARAM_ROTARY_SPEED = 11,  ///< Leslie rotary speed (0.0 = Slow, 1.0 = Fast)
    PARAM_VOLUME = 12,        ///< Master output volume (0.0 to 1.0)
  };

  /**
   * @brief Constructs a new BuiltinOrgan instrument.
   */
  BuiltinOrgan();

  /**
   * @brief Destructor.
   */
  ~BuiltinOrgan() override;

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

  // Parameter conversion helpers
  static float normToPercussionDecayS(double norm);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace hibiki
