#include "engine/effects/builtin_convolver.hpp"

#include <algorithm>
#include <cmath>
#include <complex>

#include "engine/core/audio_file.hpp"
#include "pocketfft_hdronly.h"

namespace hibiki {

static const std::string kConvolverName = "Convolver";
static const std::string kConvolverPath = "builtin://convolver";

BuiltinConvolver::BuiltinConvolver() {
  params_[PARAM_DRY] = 0.0;
  params_[PARAM_WET] = 1.0;
  params_[PARAM_PRE_DELAY] = 0.0;
  params_[PARAM_ENABLE] = 1.0;
}

bool BuiltinConvolver::load(const std::string& path, int /*plugin_index*/,
                            double sample_rate) {
  sample_rate_ = sample_rate;
  max_predelay_samples_ = (int)(0.1 * sample_rate_);  // 100ms max
  predelay_l_.resize(max_predelay_samples_ + 1, 0.0f);
  predelay_r_.resize(max_predelay_samples_ + 1, 0.0f);

  // Parse ?ir=<path> from the load path
  auto q = path.find("?ir=");
  if (q != std::string::npos) {
    std::string ir_file = path.substr(q + 4);
    // Strip &... if any
    auto amp = ir_file.find('&');
    if (amp != std::string::npos) ir_file = ir_file.substr(0, amp);
    loadIR(ir_file, sample_rate_);
  }

  reset();
  return true;
}

bool BuiltinConvolver::loadIR(const std::string& path, double sample_rate) {
  std::vector<float> wav_data;
  int ir_channels = 0;
  double wav_duration = 0;
  auto status = LoadWav(path, wav_data, ir_channels, wav_duration);
  if (!status.ok()) return false;

  ir_path_ = path;
  int ir_frames = (int)wav_data.size() / ir_channels;

  // Deinterleave to mono/stereo
  std::vector<float> ir_l(ir_frames), ir_r(ir_frames);
  for (int i = 0; i < ir_frames; ++i) {
    ir_l[i] = wav_data[i * ir_channels];
    ir_r[i] = (ir_channels >= 2) ? wav_data[i * ir_channels + 1] : ir_l[i];
  }

  // Partition IR into blocks and compute FFT of each
  num_partitions_ = (ir_frames + kBlockSize - 1) / kBlockSize;
  int fft_complex = kFFTSize / 2 + 1;

  ir_fft_l_.resize(num_partitions_);
  ir_fft_r_.resize(num_partitions_);
  fdl_l_.resize(num_partitions_);
  fdl_r_.resize(num_partitions_);

  pocketfft::shape_t shape = {(size_t)kFFTSize};
  pocketfft::stride_t stride_r = {sizeof(float)};
  pocketfft::stride_t stride_c = {sizeof(std::complex<float>)};

  for (int p = 0; p < num_partitions_; ++p) {
    // Zero-padded block
    std::vector<float> block_l(kFFTSize, 0.0f);
    std::vector<float> block_r(kFFTSize, 0.0f);
    int start = p * kBlockSize;
    int len = std::min(kBlockSize, ir_frames - start);
    std::copy_n(ir_l.data() + start, len, block_l.data());
    std::copy_n(ir_r.data() + start, len, block_r.data());

    // Compute FFT (store as interleaved real/imag)
    std::vector<std::complex<float>> fft_l(fft_complex);
    std::vector<std::complex<float>> fft_r(fft_complex);
    pocketfft::r2c(shape, stride_r, stride_c, {0}, pocketfft::FORWARD,
                   block_l.data(), fft_l.data(), 1.0f);
    pocketfft::r2c(shape, stride_r, stride_c, {0}, pocketfft::FORWARD,
                   block_r.data(), fft_r.data(), 1.0f);

    // Store as flat float pairs (real, imag)
    ir_fft_l_[p].resize(fft_complex * 2);
    ir_fft_r_[p].resize(fft_complex * 2);
    for (int i = 0; i < fft_complex; ++i) {
      ir_fft_l_[p][i * 2] = fft_l[i].real();
      ir_fft_l_[p][i * 2 + 1] = fft_l[i].imag();
      ir_fft_r_[p][i * 2] = fft_r[i].real();
      ir_fft_r_[p][i * 2 + 1] = fft_r[i].imag();
    }

    // Initialize frequency-domain delay line
    fdl_l_[p].assign(fft_complex * 2, 0.0f);
    fdl_r_[p].assign(fft_complex * 2, 0.0f);
  }

  // Reset processing state
  input_buf_l_.assign(kBlockSize, 0.0f);
  input_buf_r_.assign(kBlockSize, 0.0f);
  overlap_l_.assign(kBlockSize, 0.0f);
  overlap_r_.assign(kBlockSize, 0.0f);
  input_pos_ = 0;
  fdl_pos_ = 0;

  return true;
}

void BuiltinConvolver::reset() {
  input_buf_l_.assign(kBlockSize, 0.0f);
  input_buf_r_.assign(kBlockSize, 0.0f);
  overlap_l_.assign(kBlockSize, 0.0f);
  overlap_r_.assign(kBlockSize, 0.0f);
  input_pos_ = 0;
  fdl_pos_ = 0;
  predelay_pos_ = 0;
  std::fill(predelay_l_.begin(), predelay_l_.end(), 0.0f);
  std::fill(predelay_r_.begin(), predelay_r_.end(), 0.0f);
  for (auto& f : fdl_l_) std::fill(f.begin(), f.end(), 0.0f);
  for (auto& f : fdl_r_) std::fill(f.begin(), f.end(), 0.0f);
}

void BuiltinConvolver::processBlock(float* in_l, float* in_r, float* out_l,
                                    float* out_r, int num_samples) {
  if (num_partitions_ == 0 || ir_fft_l_.empty()) {
    // No IR loaded — pass through
    for (int i = 0; i < num_samples; ++i) {
      out_l[i] = in_l[i];
      out_r[i] = in_r[i];
    }
    return;
  }

  int fft_complex = kFFTSize / 2 + 1;
  pocketfft::shape_t shape = {(size_t)kFFTSize};
  pocketfft::stride_t stride_r_t = {sizeof(float)};
  pocketfft::stride_t stride_c_t = {sizeof(std::complex<float>)};

  for (int s = 0; s < num_samples; ++s) {
    input_buf_l_[input_pos_] = in_l[s];
    input_buf_r_[input_pos_] = in_r[s];
    input_pos_++;

    if (input_pos_ >= kBlockSize) {
      input_pos_ = 0;

      // FFT the input block (zero-padded to kFFTSize)
      std::vector<float> padded_l(kFFTSize, 0.0f);
      std::vector<float> padded_r(kFFTSize, 0.0f);
      std::copy_n(input_buf_l_.data(), kBlockSize, padded_l.data());
      std::copy_n(input_buf_r_.data(), kBlockSize, padded_r.data());

      std::vector<std::complex<float>> X_l(fft_complex);
      std::vector<std::complex<float>> X_r(fft_complex);
      pocketfft::r2c(shape, stride_r_t, stride_c_t, {0}, pocketfft::FORWARD,
                     padded_l.data(), X_l.data(), 1.0f);
      pocketfft::r2c(shape, stride_r_t, stride_c_t, {0}, pocketfft::FORWARD,
                     padded_r.data(), X_r.data(), 1.0f);

      // Store in FDL at current position
      for (int i = 0; i < fft_complex; ++i) {
        fdl_l_[fdl_pos_][i * 2] = X_l[i].real();
        fdl_l_[fdl_pos_][i * 2 + 1] = X_l[i].imag();
        fdl_r_[fdl_pos_][i * 2] = X_r[i].real();
        fdl_r_[fdl_pos_][i * 2 + 1] = X_r[i].imag();
      }

      // Multiply-accumulate: sum over partitions
      std::vector<std::complex<float>> Y_l(fft_complex, {0, 0});
      std::vector<std::complex<float>> Y_r(fft_complex, {0, 0});
      for (int p = 0; p < num_partitions_; ++p) {
        int fdl_idx = (fdl_pos_ - p + num_partitions_) % num_partitions_;
        for (int i = 0; i < fft_complex; ++i) {
          float xr_l = fdl_l_[fdl_idx][i * 2];
          float xi_l = fdl_l_[fdl_idx][i * 2 + 1];
          float hr_l = ir_fft_l_[p][i * 2];
          float hi_l = ir_fft_l_[p][i * 2 + 1];
          Y_l[i] += std::complex<float>(xr_l * hr_l - xi_l * hi_l,
                                        xr_l * hi_l + xi_l * hr_l);

          float xr_r = fdl_r_[fdl_idx][i * 2];
          float xi_r = fdl_r_[fdl_idx][i * 2 + 1];
          float hr_r = ir_fft_r_[p][i * 2];
          float hi_r = ir_fft_r_[p][i * 2 + 1];
          Y_r[i] += std::complex<float>(xr_r * hr_r - xi_r * hi_r,
                                        xr_r * hi_r + xi_r * hr_r);
        }
      }

      // IFFT
      std::vector<float> y_l(kFFTSize);
      std::vector<float> y_r(kFFTSize);
      pocketfft::c2r(shape, stride_c_t, stride_r_t, {0}, pocketfft::BACKWARD,
                     Y_l.data(), y_l.data(), 1.0f / kFFTSize);
      pocketfft::c2r(shape, stride_c_t, stride_r_t, {0}, pocketfft::BACKWARD,
                     Y_r.data(), y_r.data(), 1.0f / kFFTSize);

      // Overlap-add: first half = output + saved overlap, second half = new
      // overlap
      for (int i = 0; i < kBlockSize; ++i) {
        input_buf_l_[i] = y_l[i] + overlap_l_[i];
        input_buf_r_[i] = y_r[i] + overlap_r_[i];
        overlap_l_[i] = y_l[i + kBlockSize];
        overlap_r_[i] = y_r[i + kBlockSize];
      }

      fdl_pos_ = (fdl_pos_ + 1) % num_partitions_;
    }

    // Output from the overlap-add result buffer
    // We use the previous block's result at position input_pos_
    int read_pos = (input_pos_ == 0) ? kBlockSize - 1 : input_pos_ - 1;
    out_l[s] = input_buf_l_[read_pos];
    out_r[s] = input_buf_r_[read_pos];
  }
}

void BuiltinConvolver::process(float** inputs, float** outputs, int num_samples,
                               const HostProcessContext& /*context*/,
                               const std::vector<MidiNoteEvent>& /*events*/,
                               float** /*sidechain*/) {
  if (!enabled_ || num_partitions_ == 0) {
    for (int i = 0; i < num_samples; ++i) {
      outputs[0][i] = inputs[0][i];
      outputs[1][i] = inputs[1][i];
    }
    return;
  }

  float dry = std::clamp((float)params_[PARAM_DRY], 0.0f, 1.0f);
  float wet = std::clamp((float)params_[PARAM_WET], 0.0f, 1.0f);
  float pre_delay_ms = (float)(params_[PARAM_PRE_DELAY] * 100.0);  // 0-100ms
  int pre_delay_samples =
      std::clamp((int)(pre_delay_ms * 0.001f * (float)sample_rate_), 0,
                 max_predelay_samples_);
  int pd_size = (int)predelay_l_.size();

  // Process convolution into scratch buffers
  std::vector<float> conv_l(num_samples), conv_r(num_samples);
  processBlock(inputs[0], inputs[1], conv_l.data(), conv_r.data(), num_samples);

  float in_sum_sq = 0, out_sum_sq = 0;

  for (int i = 0; i < num_samples; ++i) {
    float in_l = inputs[0][i];
    float in_r = inputs[1][i];

    // Pre-delay on wet signal
    predelay_l_[predelay_pos_] = conv_l[i];
    predelay_r_[predelay_pos_] = conv_r[i];
    int pd_read = (predelay_pos_ - pre_delay_samples + pd_size) % pd_size;
    float wet_l = predelay_l_[pd_read];
    float wet_r = predelay_r_[pd_read];
    predelay_pos_ = (predelay_pos_ + 1) % pd_size;

    // Mix
    float final_l = in_l * dry + wet_l * wet;
    float final_r = in_r * dry + wet_r * wet;
    outputs[0][i] = final_l;
    outputs[1][i] = final_r;

    in_sum_sq += in_l * in_l + in_r * in_r;
    out_sum_sq += final_l * final_l + final_r * final_r;
  }

  // Smoothed RMS metering
  float in_rms = std::sqrt(in_sum_sq / (2.0f * num_samples));
  float out_rms = std::sqrt(out_sum_sq / (2.0f * num_samples));
  constexpr float kSmooth = 0.15f;
  input_rms_ = input_rms_ * (1.0f - kSmooth) + in_rms * kSmooth;
  output_rms_ = output_rms_ * (1.0f - kSmooth) + out_rms * kSmooth;
}

// --- IPlugin interface ---

int BuiltinConvolver::getParameterCount() const { return kTotalParams; }

bool BuiltinConvolver::getParameterInfo(int index, VstParamInfo& info) const {
  if (index < 0 || index >= kTotalParams) return false;
  static const char* names[] = {"Dry", "Wet", "Pre-Delay", "Enable"};
  static const double defaults[] = {0.0, 1.0, 0.0, 1.0};
  info.id = index;
  info.name = names[index];
  info.defaultValue = defaults[index];
  return true;
}

void BuiltinConvolver::setParameterValue(uint32_t id, double value) {
  if (id < kTotalParams) {
    params_[id] = value;
    if (id == PARAM_ENABLE) enabled_ = value >= 0.5;
  }
}

double BuiltinConvolver::getParameterValue(uint32_t id) const {
  return id < kTotalParams ? params_[id] : 0.0;
}

const std::string& BuiltinConvolver::getName() const { return kConvolverName; }
const std::string& BuiltinConvolver::getPath() const { return kConvolverPath; }
int BuiltinConvolver::getPluginIndex() const { return 0; }
bool BuiltinConvolver::isInstrument() const { return false; }

float BuiltinConvolver::getInputDb() const {
  return input_rms_ > 0 ? 20.0f * std::log10(input_rms_) : -100.0f;
}

float BuiltinConvolver::getOutputDb() const {
  return output_rms_ > 0 ? 20.0f * std::log10(output_rms_) : -100.0f;
}

}  // namespace hibiki
