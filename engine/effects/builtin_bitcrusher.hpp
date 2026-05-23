#pragma once

#include <memory>
#include <string>
#include <vector>

#include "engine/plugin/iplugin.hpp"

namespace hibiki {

/**
 * @brief A built-in Bitcrusher and Lo-Fi audio processor.
 *
 * This effect provides real-time bit-depth reduction (quantization) and
 * sample-rate reduction (downsampling/decimation), pre-gain input drive
 * control, and wet/dry mix balance.
 */
class BuiltinBitcrusher : public IPlugin {
 public:
  static constexpr int kTotalParams = 5;
  static constexpr const char* kPath = "builtin://bitcrusher";
  static constexpr const char* kName = "Bitcrusher";

  enum ParamId {
    PARAM_BIT_DEPTH = 0,    ///< Quantization bit depth (1 to 24 bits)
    PARAM_SAMPLE_RATE = 1,  ///< Target downsampled rate (20 Hz to Host SR)
    PARAM_DRIVE = 2,        ///< Input gain drive (0 to 24 dB)
    PARAM_WET_DRY = 3,      ///< Dry/Wet mix ratio (0.0 to 1.0)
    PARAM_ENABLE = 4,       ///< Bypass enable flag (0.0 or 1.0)
  };

  /**
   * @brief Constructs a new BuiltinBitcrusher with default parameters.
   */
  BuiltinBitcrusher();

  /**
   * @brief Destructor.
   */
  ~BuiltinBitcrusher() override;

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

  // Parameter mapping helpers
  static float normToBitDepth(double norm);
  static float normToSampleRate(double norm, double host_sample_rate);
  static float normToDriveDb(double norm);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace hibiki
