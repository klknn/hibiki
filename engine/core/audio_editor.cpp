#include "engine/core/audio_editor.hpp"

#include <algorithm>
#include <cmath>
#include <complex>
#include <filesystem>
#include <numbers>

#include "engine/core/audio_file.hpp"
#include "pocketfft_hdronly.h"

namespace hibiki {

absl::Status AudioEditor::Load(const std::string& path,
                               double target_sample_rate) {
  std::vector<float> data;
  int channels = 0;
  int sr = 0;
  double dur = 0.0;
  auto status =
      LoadAudioFile(path, target_sample_rate, data, channels, sr, dur);
  if (!status.ok()) return status;

  audio_data_ = std::move(data);
  num_channels_ = channels;
  sample_rate_ = sr;
  duration_sec_ = dur;
  file_name_ = std::filesystem::path(path).filename().string();
  return absl::OkStatus();
}

absl::Status AudioEditor::Save(const std::string& path) const {
  if (audio_data_.empty()) {
    return absl::FailedPreconditionError("No audio data to save");
  }
  return SaveWav(path, audio_data_, num_channels_, sample_rate_);
}

std::vector<float> AudioEditor::ComputeWaveform(int num_points) const {
  if (audio_data_.empty() || num_channels_ == 0) return {};
  int total_frames = (int)(audio_data_.size() / num_channels_);
  std::vector<float> peaks(num_points, 0.0f);
  for (int i = 0; i < num_points; ++i) {
    int frame_start = (int)((int64_t)i * total_frames / num_points);
    int frame_end = (int)((int64_t)(i + 1) * total_frames / num_points);
    float max_val = 0.0f;
    for (int f = frame_start; f < frame_end; ++f) {
      for (int c = 0; c < num_channels_; ++c) {
        float v = std::abs(audio_data_[f * num_channels_ + c]);
        if (v > max_val) max_val = v;
      }
    }
    peaks[i] = max_val;
  }
  return peaks;
}

std::vector<float> AudioEditor::ComputeSpectrogram(int& out_width,
                                                   int& out_height) const {
  if (audio_data_.empty() || num_channels_ == 0) {
    out_width = out_height = 0;
    return {};
  }

  // Mix to mono for spectrogram
  int total_frames = (int)(audio_data_.size() / num_channels_);
  std::vector<float> mono(total_frames);
  for (int f = 0; f < total_frames; ++f) {
    float sum = 0;
    for (int c = 0; c < num_channels_; ++c)
      sum += audio_data_[f * num_channels_ + c];
    mono[f] = sum / num_channels_;
  }

  constexpr int kFftSize = 1024;
  constexpr int kHopSize = 256;
  constexpr int kFreqBins = kFftSize / 2 + 1;

  int num_windows = std::max(1, (total_frames - kFftSize) / kHopSize + 1);
  out_width = num_windows;
  out_height = kFreqBins;

  // Pre-compute Hann window
  std::vector<float> hann(kFftSize);
  for (int i = 0; i < kFftSize; ++i) {
    hann[i] = 0.5f * (1.0f - std::cos(2.0f * std::numbers::pi_v<float> * i /
                                      (kFftSize - 1)));
  }

  std::vector<float> spectrogram(num_windows * kFreqBins, -100.0f);

  pocketfft::shape_t shape = {(size_t)kFftSize};
  pocketfft::stride_t stride_in = {(ptrdiff_t)sizeof(double)};
  pocketfft::stride_t stride_out = {(ptrdiff_t)sizeof(std::complex<double>)};

  std::vector<double> fft_in(kFftSize);
  std::vector<std::complex<double>> fft_out(kFreqBins);

  for (int w = 0; w < num_windows; ++w) {
    int offset = w * kHopSize;
    // Window and copy
    for (int i = 0; i < kFftSize; ++i) {
      int idx = offset + i;
      fft_in[i] = (idx < total_frames) ? (double)mono[idx] * hann[i] : 0.0;
    }
    // FFT
    pocketfft::r2c(shape, stride_in, stride_out, {0}, pocketfft::FORWARD,
                   fft_in.data(), fft_out.data(), 1.0);
    // Magnitude in dB
    for (int k = 0; k < kFreqBins; ++k) {
      double mag = std::abs(fft_out[k]) * (2.0 / kFftSize);
      float db = (mag > 1e-10) ? 20.0f * (float)std::log10(mag) : -100.0f;
      spectrogram[w * kFreqBins + k] = db;
    }
  }
  return spectrogram;
}

void AudioEditor::ResolveRange(float sel_start, float sel_end, int& out_start,
                               int& out_end) const {
  int total_frames = (int)(audio_data_.size() / num_channels_);
  out_start = std::clamp((int)(sel_start * total_frames), 0, total_frames);
  out_end = std::clamp((int)(sel_end * total_frames), 0, total_frames);
  if (out_start > out_end) std::swap(out_start, out_end);
}

void AudioEditor::Normalize() {
  if (audio_data_.empty()) return;
  float peak = 0.0f;
  for (float v : audio_data_) {
    float abs_v = std::abs(v);
    if (abs_v > peak) peak = abs_v;
  }
  if (peak < 1e-10f) return;
  float gain = 1.0f / peak;
  for (float& v : audio_data_) v *= gain;
}

void AudioEditor::Reverse(float sel_start, float sel_end) {
  if (audio_data_.empty()) return;
  int start, end;
  ResolveRange(sel_start, sel_end, start, end);
  if (end - start < 2) return;

  // Reverse frame-by-frame (preserving channel order within each frame)
  int nc = num_channels_;
  for (int i = start, j = end - 1; i < j; ++i, --j) {
    for (int c = 0; c < nc; ++c) {
      std::swap(audio_data_[i * nc + c], audio_data_[j * nc + c]);
    }
  }
}

void AudioEditor::FadeIn(float sel_start, float sel_end) {
  if (audio_data_.empty()) return;
  int start, end;
  ResolveRange(sel_start, sel_end, start, end);
  int len = end - start;
  if (len < 1) return;

  int nc = num_channels_;
  for (int f = start; f < end; ++f) {
    float t = (float)(f - start) / (float)len;
    for (int c = 0; c < nc; ++c) {
      audio_data_[f * nc + c] *= t;
    }
  }
}

void AudioEditor::FadeOut(float sel_start, float sel_end) {
  if (audio_data_.empty()) return;
  int start, end;
  ResolveRange(sel_start, sel_end, start, end);
  int len = end - start;
  if (len < 1) return;

  int nc = num_channels_;
  for (int f = start; f < end; ++f) {
    float t = 1.0f - (float)(f - start) / (float)len;
    for (int c = 0; c < nc; ++c) {
      audio_data_[f * nc + c] *= t;
    }
  }
}

void AudioEditor::Trim(float sel_start, float sel_end) {
  if (audio_data_.empty()) return;
  int start, end;
  ResolveRange(sel_start, sel_end, start, end);
  if (start >= end) return;

  int nc = num_channels_;
  std::vector<float> trimmed(audio_data_.begin() + start * nc,
                             audio_data_.begin() + end * nc);
  audio_data_ = std::move(trimmed);
  int total_frames = (int)(audio_data_.size() / nc);
  duration_sec_ = (double)total_frames / sample_rate_;
}

void AudioEditor::ApplyGain(float sel_start, float sel_end, float gain_db) {
  if (audio_data_.empty()) return;
  int start, end;
  ResolveRange(sel_start, sel_end, start, end);
  float gain_linear = std::pow(10.0f, gain_db / 20.0f);

  int nc = num_channels_;
  for (int f = start; f < end; ++f) {
    for (int c = 0; c < nc; ++c) {
      audio_data_[f * nc + c] *= gain_linear;
    }
  }
}

absl::Status AudioEditor::Convolve(const std::string& impulse_path, float dry,
                                   float wet, bool add_tail,
                                   double target_sample_rate) {
  if (audio_data_.empty()) {
    return absl::FailedPreconditionError("No audio data loaded");
  }

  // Load impulse response
  std::vector<float> ir_data;
  int ir_channels = 0;
  int ir_sr = 0;
  double ir_dur = 0.0;
  auto status = LoadAudioFile(impulse_path, target_sample_rate, ir_data,
                              ir_channels, ir_sr, ir_dur);
  if (!status.ok()) return status;
  if (ir_data.empty()) {
    return absl::InvalidArgumentError("Impulse response is empty");
  }

  // Mix IR to mono
  int ir_frames = (int)(ir_data.size() / ir_channels);
  std::vector<float> ir_mono(ir_frames);
  for (int f = 0; f < ir_frames; ++f) {
    float sum = 0;
    for (int c = 0; c < ir_channels; ++c) sum += ir_data[f * ir_channels + c];
    ir_mono[f] = sum / ir_channels;
  }

  // Mix source to mono for convolution, then apply to all channels
  int src_frames = (int)(audio_data_.size() / num_channels_);
  int conv_len = src_frames + (add_tail ? ir_frames - 1 : 0);

  // FFT-based overlap-add convolution
  // Choose FFT size = next power of 2 >= src_frames + ir_frames - 1
  int fft_size = 1;
  int needed = src_frames + ir_frames - 1;
  while (fft_size < needed) fft_size <<= 1;
  int complex_size = fft_size / 2 + 1;

  pocketfft::shape_t shape = {(size_t)fft_size};
  pocketfft::stride_t stride_r = {(ptrdiff_t)sizeof(double)};
  pocketfft::stride_t stride_c = {(ptrdiff_t)sizeof(std::complex<double>)};

  // Process each channel
  std::vector<float> output(conv_len * num_channels_, 0.0f);

  // Pre-compute IR FFT (mono)
  std::vector<double> ir_padded(fft_size, 0.0);
  for (int i = 0; i < ir_frames; ++i) ir_padded[i] = ir_mono[i];
  std::vector<std::complex<double>> ir_fft(complex_size);
  pocketfft::r2c(shape, stride_r, stride_c, {0}, pocketfft::FORWARD,
                 ir_padded.data(), ir_fft.data(), 1.0);

  for (int ch = 0; ch < num_channels_; ++ch) {
    // Extract channel
    std::vector<double> src_padded(fft_size, 0.0);
    for (int f = 0; f < src_frames; ++f)
      src_padded[f] = audio_data_[f * num_channels_ + ch];

    // Forward FFT of source channel
    std::vector<std::complex<double>> src_fft(complex_size);
    pocketfft::r2c(shape, stride_r, stride_c, {0}, pocketfft::FORWARD,
                   src_padded.data(), src_fft.data(), 1.0);

    // Multiply in frequency domain
    for (int k = 0; k < complex_size; ++k) {
      src_fft[k] *= ir_fft[k];
    }

    // Inverse FFT
    std::vector<double> conv_out(fft_size, 0.0);
    pocketfft::c2r(shape, stride_c, stride_r, {0}, pocketfft::BACKWARD,
                   src_fft.data(), conv_out.data(), 1.0 / fft_size);

    // Mix dry/wet and write to output
    for (int f = 0; f < conv_len; ++f) {
      float dry_sample =
          (f < src_frames) ? audio_data_[f * num_channels_ + ch] : 0.0f;
      float wet_sample = (float)conv_out[f];
      output[f * num_channels_ + ch] = dry * dry_sample + wet * wet_sample;
    }
  }

  audio_data_ = std::move(output);
  int total_frames = (int)(audio_data_.size() / num_channels_);
  duration_sec_ = (double)total_frames / sample_rate_;
  return absl::OkStatus();
}

void AudioEditor::Blur(float amount, int envelope_type) {
  if (audio_data_.empty() || num_channels_ == 0) return;

  // Clamp amount to reasonable range
  amount = std::clamp(amount, 0.001f, 1.0f);

  // Compute IR length: amount * sample_rate (in frames)
  int ir_frames = std::max(2, (int)(amount * sample_rate_));

  // Generate shaped noise IR
  // Use a simple LCG PRNG for deterministic noise
  uint32_t seed = 12345;
  auto next_rand = [&seed]() -> float {
    seed = seed * 1664525u + 1013904223u;
    return (float)(seed & 0xFFFFFF) / (float)0xFFFFFF * 2.0f - 1.0f;
  };

  std::vector<float> noise_ir(ir_frames);
  for (int i = 0; i < ir_frames; ++i) {
    float noise = next_rand();
    float t = (float)i / (float)(ir_frames - 1);  // 0 to 1

    // Apply envelope shape
    float env = 1.0f;
    switch (envelope_type) {
      case 0:  // Flat
        env = 1.0f;
        break;
      case 1:  // Triangle — peak at center
        env = (t < 0.5f) ? (2.0f * t) : (2.0f * (1.0f - t));
        break;
      case 2:  // Parabolic — peak at center, smoother falloff
        env = 1.0f - (2.0f * t - 1.0f) * (2.0f * t - 1.0f);
        break;
      default:
        env = 1.0f;
        break;
    }
    noise_ir[i] = noise * env;
  }

  // Normalize the noise IR so it doesn't change overall level
  float ir_energy = 0.0f;
  for (float v : noise_ir) ir_energy += v * v;
  if (ir_energy > 1e-10f) {
    float scale = 1.0f / std::sqrt(ir_energy);
    for (float& v : noise_ir) v *= scale;
  }

  // Perform FFT convolution (reuse same approach as Convolve)
  int src_frames = (int)(audio_data_.size() / num_channels_);
  int conv_len = src_frames;  // blur doesn't extend

  int fft_size = 1;
  int needed = src_frames + ir_frames - 1;
  while (fft_size < needed) fft_size <<= 1;
  int complex_size = fft_size / 2 + 1;

  pocketfft::shape_t shape = {(size_t)fft_size};
  pocketfft::stride_t stride_r = {(ptrdiff_t)sizeof(double)};
  pocketfft::stride_t stride_c = {(ptrdiff_t)sizeof(std::complex<double>)};

  // Pre-compute noise IR FFT
  std::vector<double> ir_padded(fft_size, 0.0);
  for (int i = 0; i < ir_frames; ++i) ir_padded[i] = noise_ir[i];
  std::vector<std::complex<double>> ir_fft(complex_size);
  pocketfft::r2c(shape, stride_r, stride_c, {0}, pocketfft::FORWARD,
                 ir_padded.data(), ir_fft.data(), 1.0);

  std::vector<float> output(conv_len * num_channels_, 0.0f);

  for (int ch = 0; ch < num_channels_; ++ch) {
    std::vector<double> src_padded(fft_size, 0.0);
    for (int f = 0; f < src_frames; ++f)
      src_padded[f] = audio_data_[f * num_channels_ + ch];

    std::vector<std::complex<double>> src_fft(complex_size);
    pocketfft::r2c(shape, stride_r, stride_c, {0}, pocketfft::FORWARD,
                   src_padded.data(), src_fft.data(), 1.0);

    for (int k = 0; k < complex_size; ++k) src_fft[k] *= ir_fft[k];

    std::vector<double> conv_out(fft_size, 0.0);
    pocketfft::c2r(shape, stride_c, stride_r, {0}, pocketfft::BACKWARD,
                   src_fft.data(), conv_out.data(), 1.0 / fft_size);

    for (int f = 0; f < conv_len; ++f) {
      output[f * num_channels_ + ch] = (float)conv_out[f];
    }
  }

  audio_data_ = std::move(output);
}

void AudioEditor::PreviewPlay() {
  // Stop any existing preview first
  PreviewStop();

  if (audio_data_.empty() || sample_rate_ <= 0 || num_channels_ <= 0) return;

  preview_playing_ = true;

  // Create output device at the audio's sample rate, stereo output
  preview_device_ = SoundDevice::create(sample_rate_, 2, 100);
  if (!preview_device_ || !preview_device_->is_ready()) {
    preview_playing_ = false;
    preview_device_.reset();
    return;
  }

  // Capture data by reference (this outlives the thread)
  preview_thread_ = std::thread([this]() {
    constexpr int kBlockSize = 512;
    int total_frames = (int)audio_data_.size() / num_channels_;
    int pos = 0;
    std::vector<float> interleaved(kBlockSize * 2);  // Always output stereo

    while (preview_playing_ && pos < total_frames) {
      int frames_to_write = std::min(kBlockSize, total_frames - pos);
      // Fill interleaved stereo output
      for (int f = 0; f < frames_to_write; ++f) {
        int src = (pos + f) * num_channels_;
        float l = audio_data_[src];
        float r = (num_channels_ >= 2) ? audio_data_[src + 1] : l;
        interleaved[f * 2] = l;
        interleaved[f * 2 + 1] = r;
      }
      // Zero-fill remainder
      for (int f = frames_to_write; f < kBlockSize; ++f) {
        interleaved[f * 2] = 0.0f;
        interleaved[f * 2 + 1] = 0.0f;
      }
      preview_device_->write(interleaved, kBlockSize);
      pos += frames_to_write;
    }
    preview_playing_ = false;
  });
}

void AudioEditor::PreviewStop() {
  preview_playing_ = false;
  if (preview_thread_.joinable()) {
    preview_thread_.join();
  }
  preview_device_.reset();
}

}  // namespace hibiki
