#pragma once

#include <vector>
#include <string>

namespace hibiki {

// Simple WAV loader (16-bit PCM)
bool LoadWav(const std::string& path, std::vector<float>& out_data, int& out_channels, double& out_duration_sec);

// Simple WAV saver (16-bit PCM)
bool SaveWav(const std::string& path, const std::vector<float>& interleaved_data, int channels, int sample_rate);

} // namespace hibiki
