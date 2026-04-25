#pragma once

#include <string>
#include <vector>

#include "absl/status/status.h"

namespace hibiki {

// Simple WAV loader (16-bit PCM)
absl::Status LoadWav(const std::string& path, std::vector<float>& out_data,
                     int& out_channels, double& out_duration_sec);

// Load any audio file via ffmpeg (WAV, MP3, FLAC, AIFF, OGG, etc.)
// and optionally resample to target_sample_rate.
// If target_sample_rate <= 0, no resampling is performed.
absl::Status LoadAudioFile(const std::string& path, double target_sample_rate,
                           std::vector<float>& out_data, int& out_channels,
                           int& out_sample_rate, double& out_duration_sec);

// Simple WAV saver (16-bit PCM)
absl::Status SaveWav(const std::string& path,
                     const std::vector<float>& interleaved_data, int channels,
                     int sample_rate);

}  // namespace hibiki
