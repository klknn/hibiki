#pragma once

#include <atomic>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "absl/status/status.h"
#include "engine/audio/sound.hpp"

namespace hibiki {

// Standalone audio editor session (Edison-style).
// Holds loaded audio data and provides destructive edit operations.
// Not thread-safe; all calls must be from the same thread (engine command
// thread).
class AudioEditor {
 public:
  // Load an audio file (WAV, FLAC, etc.) with optional resampling.
  absl::Status Load(const std::string& path, double target_sample_rate);

  // Save the current audio to a WAV file.
  absl::Status Save(const std::string& path) const;

  // Compute downsampled waveform peaks for display.
  std::vector<float> ComputeWaveform(int num_points) const;

  // Compute STFT spectrogram as a flattened time x freq matrix.
  // Returns the data + sets out_width and out_height.
  std::vector<float> ComputeSpectrogram(int& out_width, int& out_height) const;

  // --- Destructive edit operations ---
  // All selection ranges are normalized [0.0, 1.0] over the full audio length.

  // Normalize the full audio to peak amplitude.
  void Normalize();

  // Reverse audio within the selection range.
  void Reverse(float sel_start, float sel_end);

  // Apply a linear fade-in to the selection range.
  void FadeIn(float sel_start, float sel_end);

  // Apply a linear fade-out to the selection range.
  void FadeOut(float sel_start, float sel_end);

  // Trim audio to keep only the selection range.
  void Trim(float sel_start, float sel_end);

  // Apply gain adjustment in dB to the selection range.
  void ApplyGain(float sel_start, float sel_end, float gain_db);

  // Apply convolution reverb using an impulse response file.
  // dry/wet are mix levels [0, 1]. add_tail extends audio if true.
  absl::Status Convolve(const std::string& impulse_path, float dry, float wet,
                        bool add_tail, double target_sample_rate);

  // Apply blur tool: convolve with shaped noise impulse.
  // amount controls noise IR length (0.001–1.0 of audio duration).
  // envelope_type: 0=Flat, 1=Triangle, 2=Parabolic.
  void Blur(float amount, int envelope_type);

  // Preview playback transport.
  void PreviewPlay();
  void PreviewStop();

  // Accessors
  bool HasData() const { return !audio_data_.empty(); }
  double duration_sec() const { return duration_sec_; }
  int sample_rate() const { return sample_rate_; }
  int num_channels() const { return num_channels_; }
  const std::string& file_name() const { return file_name_; }
  const std::vector<float>& audio_data() const { return audio_data_; }
  void set_file_name(const std::string& name) { file_name_ = name; }

 private:
  // Resolve normalized [0,1] range to sample indices.
  void ResolveRange(float sel_start, float sel_end, int& out_start,
                    int& out_end) const;

  std::vector<float> audio_data_;  // Interleaved float samples
  int num_channels_ = 0;
  int sample_rate_ = 0;
  double duration_sec_ = 0.0;
  std::string file_name_;

  // Preview playback state
  std::atomic<bool> preview_playing_{false};
  std::unique_ptr<SoundDevice> preview_device_;
  std::thread preview_thread_;
};

}  // namespace hibiki
