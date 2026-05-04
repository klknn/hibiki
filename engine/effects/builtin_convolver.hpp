#pragma once

#include <string>
#include <vector>

#include "engine/plugin/iplugin.hpp"

namespace hibiki {

// Real-time convolution reverb effect using partitioned overlap-add FFT.
// Loads an arbitrary WAV file as impulse response.
// Path: builtin://convolver (or builtin://convolver?ir=<path>)
class BuiltinConvolver : public IPlugin {
 public:
  static constexpr int kTotalParams = 4;
  static constexpr const char* kPath = "builtin://convolver";
  static constexpr const char* kName = "Convolver";

  enum ParamId {
    PARAM_DRY = 0,
    PARAM_WET = 1,
    PARAM_PRE_DELAY = 2,
    PARAM_ENABLE = 3,
  };

  BuiltinConvolver();

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

  // Load an impulse response WAV file
  bool loadIR(const std::string& path, double sample_rate);

  // Metering
  float getInputDb() const;
  float getOutputDb() const;

 private:
  static constexpr int kBlockSize = 512;
  static constexpr int kFFTSize = 1024;  // 2 * kBlockSize

  double params_[kTotalParams] = {};
  double sample_rate_ = 44100.0;
  bool enabled_ = true;
  std::string ir_path_;

  // Per-channel IR FFT partitions: ir_fft_[partition][fft_bin_pair]
  std::vector<std::vector<float>> ir_fft_l_;
  std::vector<std::vector<float>> ir_fft_r_;
  int num_partitions_ = 0;

  // Overlap-add state per channel
  std::vector<float> input_buf_l_, input_buf_r_;  // kBlockSize ring
  std::vector<float> overlap_l_, overlap_r_;      // kBlockSize tail
  std::vector<std::vector<float>> fdl_l_,
      fdl_r_;  // Frequency-domain delay line
  int input_pos_ = 0;
  int fdl_pos_ = 0;

  // Pre-delay ring buffer
  std::vector<float> predelay_l_, predelay_r_;
  int predelay_pos_ = 0;
  int max_predelay_samples_ = 0;

  // Metering
  float input_rms_ = 0;
  float output_rms_ = 0;

  void reset();
  void processBlock(float* in_l, float* in_r, float* out_l, float* out_r,
                    int num_samples);
};

}  // namespace hibiki
