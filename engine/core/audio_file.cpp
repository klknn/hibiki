#include "engine/core/audio_file.hpp"

#include <cstdint>
#include <fstream>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"

namespace hibiki {

absl::Status LoadWav(const std::string& path, std::vector<float>& out_data,
                     int& out_channels, double& out_duration_sec) {
  std::ifstream f(path, std::ios::binary);
  if (!f) return absl::NotFoundError(absl::StrCat("Cannot open file: ", path));

  char chunkId[4];
  f.read(chunkId, 4);
  if (std::string(chunkId, 4) != "RIFF")
    return absl::InvalidArgumentError(absl::StrCat("Not a RIFF file: ", path));
  f.seekg(4, std::ios::cur);  // Skip size
  f.read(chunkId, 4);
  if (std::string(chunkId, 4) != "WAVE")
    return absl::InvalidArgumentError(absl::StrCat("Not a WAVE file: ", path));

  int sample_rate = 0;
  int bits_per_sample = 0;
  int channels = 0;

  while (f.read(chunkId, 4)) {
    uint32_t size;
    f.read((char*)&size, 4);
    if (std::string(chunkId, 4) == "fmt ") {
      uint16_t format;
      f.read((char*)&format, 2);
      if (format != 1)
        return absl::UnimplementedError(
            absl::StrCat("Non-PCM format (", format, ") in: ", path));
      uint16_t chans;
      f.read((char*)&chans, 2);
      channels = chans;
      f.read((char*)&sample_rate, 4);
      f.seekg(6, std::ios::cur);  // Skip byte rate and block align
      f.read((char*)&bits_per_sample, 2);
      if (bits_per_sample != 16)
        return absl::UnimplementedError(absl::StrCat(
            "Only 16-bit PCM supported, got ", bits_per_sample, " in: ", path));
      if (size > 16) f.seekg(size - 16, std::ios::cur);
    } else if (std::string(chunkId, 4) == "data") {
      int num_samples = size / 2;
      std::vector<int16_t> pcm(num_samples);
      f.read((char*)pcm.data(), size);
      out_data.resize(num_samples);
      for (int i = 0; i < num_samples; ++i) {
        out_data[i] = pcm[i] / 32768.0f;
      }
      float max_val = 0.0f;
      for (size_t i = 0; i < out_data.size(); ++i) {
        float abs_val = std::abs(out_data[i]);
        if (abs_val > max_val) max_val = abs_val;
      }
      // we don't normalize here, we just load

      out_channels = channels;
      out_duration_sec = (double)num_samples / (channels * sample_rate);
      return absl::OkStatus();
    } else {
      f.seekg(size, std::ios::cur);
    }
  }
  return absl::DataLossError(absl::StrCat("No 'data' chunk found in: ", path));
}

absl::Status SaveWav(const std::string& path,
                     const std::vector<float>& interleaved_data, int channels,
                     int sample_rate) {
  if (channels <= 0 || sample_rate <= 0)
    return absl::InvalidArgumentError(absl::StrCat(
        "Invalid params: channels=", channels, " rate=", sample_rate));

  std::ofstream out(path, std::ios::binary);
  if (!out)
    return absl::PermissionDeniedError(
        absl::StrCat("Cannot open file for writing: ", path));

  int num_samples = interleaved_data.size() / channels;
  int bits_per_sample = 16;
  int byte_rate = sample_rate * channels * bits_per_sample / 8;
  uint16_t block_align = channels * bits_per_sample / 8;
  int data_size = num_samples * channels * bits_per_sample / 8;
  int chunk_size = 36 + data_size;

  out.write("RIFF", 4);
  out.write(reinterpret_cast<const char*>(&chunk_size), 4);
  out.write("WAVE", 4);
  out.write("fmt ", 4);
  uint32_t fmt_size = 16;
  out.write(reinterpret_cast<const char*>(&fmt_size), 4);
  uint16_t audio_format = 1;
  out.write(reinterpret_cast<const char*>(&audio_format), 2);
  uint16_t num_channels = channels;
  out.write(reinterpret_cast<const char*>(&num_channels), 2);
  out.write(reinterpret_cast<const char*>(&sample_rate), 4);
  out.write(reinterpret_cast<const char*>(&byte_rate), 4);
  out.write(reinterpret_cast<const char*>(&block_align), 2);
  uint16_t bps = bits_per_sample;
  out.write(reinterpret_cast<const char*>(&bps), 2);

  out.write("data", 4);
  out.write(reinterpret_cast<const char*>(&data_size), 4);

  for (float f : interleaved_data) {
    f = std::max(-1.0f, std::min(1.0f, f));
    int16_t sample = static_cast<int16_t>(f * 32767.0f);
    out.write(reinterpret_cast<const char*>(&sample), 2);
  }
  return absl::OkStatus();
}

}  // namespace hibiki
