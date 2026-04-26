#include "engine/core/audio_file.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "engine/core/resample.hpp"

namespace hibiki {
namespace {

// WAV format codes.
constexpr uint16_t kWavFormatPCM = 1;
constexpr uint16_t kWavFormatIEEEFloat = 3;
constexpr uint16_t kWavFormatExtensible = 0xFFFE;

// Read little-endian values from ifstream.
template <typename T>
T ReadLE(std::ifstream& f) {
  T val = 0;
  f.read(reinterpret_cast<char*>(&val), sizeof(T));
  return val;
}

// Write little-endian values to ofstream.
template <typename T>
void WriteLE(std::ofstream& f, T val) {
  f.write(reinterpret_cast<const char*>(&val), sizeof(T));
}

// Convert 24-bit PCM (3 bytes, little-endian, signed) to float.
float Int24ToFloat(const uint8_t* bytes) {
  int32_t val = bytes[0] | (bytes[1] << 8) | (bytes[2] << 16);
  if (val & 0x800000) val |= 0xFF000000;  // Sign extend.
  return val / 8388608.0f;                // 2^23
}

}  // namespace

absl::Status LoadWav(const std::string& path, std::vector<float>& out_data,
                     int& out_channels, double& out_duration_sec) {
  std::ifstream f(path, std::ios::binary);
  if (!f) return absl::NotFoundError(absl::StrCat("Cannot open file: ", path));

  // Read RIFF header.
  char id[4];
  f.read(id, 4);
  if (std::memcmp(id, "RIFF", 4) != 0)
    return absl::InvalidArgumentError(absl::StrCat("Not a RIFF file: ", path));
  f.seekg(4, std::ios::cur);  // Skip file size.
  f.read(id, 4);
  if (std::memcmp(id, "WAVE", 4) != 0)
    return absl::InvalidArgumentError(absl::StrCat("Not a WAVE file: ", path));

  int sample_rate = 0;
  int channels = 0;
  int bits_per_sample = 0;
  uint16_t audio_format = 0;

  // Parse chunks.
  while (f.read(id, 4)) {
    uint32_t chunk_size = ReadLE<uint32_t>(f);

    if (std::memcmp(id, "fmt ", 4) == 0) {
      std::streampos chunk_start = f.tellg();
      audio_format = ReadLE<uint16_t>(f);
      channels = ReadLE<uint16_t>(f);
      sample_rate = ReadLE<int32_t>(f);
      f.seekg(4, std::ios::cur);  // byte rate
      f.seekg(2, std::ios::cur);  // block align
      bits_per_sample = ReadLE<uint16_t>(f);

      if (audio_format == kWavFormatExtensible && chunk_size >= 40) {
        f.seekg(4, std::ios::cur);  // cbSize + validBitsPerSample
        f.seekg(4, std::ios::cur);  // channel mask
        // SubFormat GUID: first 2 bytes = actual format code.
        audio_format = ReadLE<uint16_t>(f);
      }

      // Seek to end of fmt chunk.
      f.seekg(chunk_start);
      f.seekg(chunk_size, std::ios::cur);

    } else if (std::memcmp(id, "data", 4) == 0) {
      if (channels <= 0 || sample_rate <= 0 || bits_per_sample <= 0)
        return absl::FailedPreconditionError(
            absl::StrCat("'data' before valid 'fmt ': ", path));

      int bytes_per_sample = bits_per_sample / 8;
      int total_samples = chunk_size / bytes_per_sample;
      out_data.resize(total_samples);

      if (audio_format == kWavFormatPCM) {
        if (bits_per_sample == 16) {
          std::vector<int16_t> pcm(total_samples);
          f.read(reinterpret_cast<char*>(pcm.data()), chunk_size);
          for (int i = 0; i < total_samples; ++i)
            out_data[i] = pcm[i] / 32768.0f;
        } else if (bits_per_sample == 24) {
          std::vector<uint8_t> raw(chunk_size);
          f.read(reinterpret_cast<char*>(raw.data()), chunk_size);
          for (int i = 0; i < total_samples; ++i)
            out_data[i] = Int24ToFloat(&raw[i * 3]);
        } else if (bits_per_sample == 32) {
          std::vector<int32_t> pcm(total_samples);
          f.read(reinterpret_cast<char*>(pcm.data()), chunk_size);
          for (int i = 0; i < total_samples; ++i)
            out_data[i] = pcm[i] / 2147483648.0f;  // 2^31
        } else {
          return absl::UnimplementedError(absl::StrCat(
              "Unsupported PCM bit depth: ", bits_per_sample, " in: ", path));
        }
      } else if (audio_format == kWavFormatIEEEFloat) {
        if (bits_per_sample == 32) {
          f.read(reinterpret_cast<char*>(out_data.data()), chunk_size);
        } else if (bits_per_sample == 64) {
          std::vector<double> dbuf(total_samples);
          f.read(reinterpret_cast<char*>(dbuf.data()), chunk_size);
          for (int i = 0; i < total_samples; ++i)
            out_data[i] = static_cast<float>(dbuf[i]);
        } else {
          return absl::UnimplementedError(absl::StrCat(
              "Unsupported float bit depth: ", bits_per_sample, " in: ", path));
        }
      } else {
        return absl::UnimplementedError(absl::StrCat(
            "Unsupported WAV format code: ", audio_format, " in: ", path));
      }

      out_channels = channels;
      out_duration_sec =
          static_cast<double>(total_samples) / (channels * sample_rate);
      return absl::OkStatus();

    } else {
      // Skip unknown chunk (pad to even boundary).
      f.seekg((chunk_size + 1) & ~1u, std::ios::cur);
    }
  }
  return absl::DataLossError(absl::StrCat("No 'data' chunk found in: ", path));
}

absl::Status LoadAudioFile(const std::string& path, double target_sample_rate,
                           std::vector<float>& out_data, int& out_channels,
                           int& out_sample_rate, double& out_duration_sec) {
  // Load WAV file.
  double wav_duration = 0;
  auto status = LoadWav(path, out_data, out_channels, wav_duration);
  if (!status.ok()) return status;

  // Infer source sample rate from loaded data.
  int num_samples = static_cast<int>(out_data.size());
  int src_rate =
      static_cast<int>(std::round(num_samples / (wav_duration * out_channels)));
  out_sample_rate = src_rate;
  out_duration_sec = wav_duration;

  // Resample if needed.
  if (target_sample_rate > 0 && std::abs(target_sample_rate - src_rate) > 0.5) {
    Resampler resampler(static_cast<float>(src_rate),
                        static_cast<float>(target_sample_rate), out_channels);
    int num_frames = num_samples / out_channels;
    out_data = resampler.Process(out_data.data(), num_frames);
    out_sample_rate = static_cast<int>(target_sample_rate);
    out_duration_sec =
        static_cast<double>(out_data.size()) / (out_channels * out_sample_rate);
  }

  return absl::OkStatus();
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

  int total_samples = static_cast<int>(interleaved_data.size());
  int bits_per_sample = 16;
  int byte_rate = sample_rate * channels * bits_per_sample / 8;
  uint16_t block_align = channels * bits_per_sample / 8;
  int data_size = total_samples * bits_per_sample / 8;
  int chunk_size = 36 + data_size;

  out.write("RIFF", 4);
  WriteLE(out, static_cast<uint32_t>(chunk_size));
  out.write("WAVE", 4);
  out.write("fmt ", 4);
  WriteLE(out, static_cast<uint32_t>(16));  // fmt chunk size
  WriteLE(out, static_cast<uint16_t>(kWavFormatPCM));
  WriteLE(out, static_cast<uint16_t>(channels));
  WriteLE(out, static_cast<uint32_t>(sample_rate));
  WriteLE(out, static_cast<uint32_t>(byte_rate));
  WriteLE(out, block_align);
  WriteLE(out, static_cast<uint16_t>(bits_per_sample));

  out.write("data", 4);
  WriteLE(out, static_cast<uint32_t>(data_size));

  for (float f : interleaved_data) {
    f = std::max(-1.0f, std::min(1.0f, f));
    int16_t sample = static_cast<int16_t>(f * 32767.0f);
    WriteLE(out, sample);
  }
  return absl::OkStatus();
}

}  // namespace hibiki
