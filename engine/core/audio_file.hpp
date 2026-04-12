#pragma once

#include <string>
#include <vector>

#include "absl/status/status.h"

namespace hibiki {

// Simple WAV loader (16-bit PCM)
absl::Status LoadWav(const std::string& path, std::vector<float>& out_data,
                     int& out_channels, double& out_duration_sec);

// Simple WAV saver (16-bit PCM)
absl::Status SaveWav(const std::string& path,
                     const std::vector<float>& interleaved_data, int channels,
                     int sample_rate);

}  // namespace hibiki
