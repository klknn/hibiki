#include "engine/core/audio_file.hpp"

#include <cmath>
#include <cstdint>
#include <fstream>
#include <vector>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "engine/core/resample.hpp"

namespace hibiki {

extern int d_inspect_wav_bytes(const uint8_t* raw, size_t len,
                               int* out_channels, int* out_sample_rate,
                               int* out_bits_per_sample, int* out_audio_format,
                               int* out_total_samples, size_t* out_data_offset,
                               size_t* out_data_size);
extern void d_decode_wav_samples(const uint8_t* data_ptr, size_t avail_bytes,
                                 int audio_format, int bits_per_sample,
                                 int total_samples, float* out_floats);
extern void d_encode_wav_pcm16(const float* interleaved, int total_samples,
                               int channels, int sample_rate,
                               uint8_t* out_header44, int16_t* out_pcm16);

absl::Status LoadWav(const std::string& path, std::vector<float>& out_data,
                     int& out_channels, double& out_duration_sec) {
  std::ifstream f(path, std::ios::binary | std::ios::ate);
  if (!f) return absl::NotFoundError(absl::StrCat("Cannot open file: ", path));

  std::streamsize file_len = f.tellg();
  if (file_len <= 0)
    return absl::InvalidArgumentError(absl::StrCat("Not a RIFF file: ", path));
  f.seekg(0, std::ios::beg);

  std::vector<uint8_t> raw(static_cast<size_t>(file_len));
  f.read(reinterpret_cast<char*>(raw.data()), file_len);

  int channels = 0;
  int sample_rate = 0;
  int bits_per_sample = 0;
  int audio_format = 0;
  int total_samples = 0;
  size_t data_offset = 0;
  size_t data_size = 0;

  int err = d_inspect_wav_bytes(raw.data(), raw.size(), &channels, &sample_rate,
                                &bits_per_sample, &audio_format, &total_samples,
                                &data_offset, &data_size);

  switch (err) {
    case 0:
      break;
    case 1:
      return absl::InvalidArgumentError(
          absl::StrCat("Not a RIFF file: ", path));
    case 2:
      return absl::InvalidArgumentError(
          absl::StrCat("Not a WAVE file: ", path));
    case 3:
      return absl::FailedPreconditionError(
          absl::StrCat("'data' before valid 'fmt ': ", path));
    case 4:
      return absl::UnimplementedError(absl::StrCat(
          "Unsupported PCM bit depth: ", bits_per_sample, " in: ", path));
    case 5:
      return absl::UnimplementedError(absl::StrCat(
          "Unsupported float bit depth: ", bits_per_sample, " in: ", path));
    case 6:
      return absl::UnimplementedError(absl::StrCat(
          "Unsupported WAV format code: ", audio_format, " in: ", path));
    case 7:
    default:
      return absl::DataLossError(
          absl::StrCat("No 'data' chunk found in: ", path));
  }

  out_data.resize(static_cast<size_t>(total_samples));
  size_t avail = (data_offset < raw.size()) ? (raw.size() - data_offset) : 0;
  if (avail > data_size) avail = data_size;

  d_decode_wav_samples(raw.data() + data_offset, avail, audio_format,
                       bits_per_sample, total_samples, out_data.data());

  out_channels = channels;
  out_duration_sec =
      static_cast<double>(total_samples) / (channels * sample_rate);
  return absl::OkStatus();
}

absl::Status LoadAudioFile(const std::string& path, double target_sample_rate,
                           std::vector<float>& out_data, int& out_channels,
                           int& out_sample_rate, double& out_duration_sec) {
  double wav_duration = 0;
  auto status = LoadWav(path, out_data, out_channels, wav_duration);
  if (!status.ok()) return status;

  int num_samples = static_cast<int>(out_data.size());
  int src_rate =
      static_cast<int>(std::round(num_samples / (wav_duration * out_channels)));
  out_sample_rate = src_rate;
  out_duration_sec = wav_duration;

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
  uint8_t header[44] = {};
  std::vector<int16_t> pcm16(static_cast<size_t>(total_samples));

  d_encode_wav_pcm16(interleaved_data.data(), total_samples, channels,
                     sample_rate, header, pcm16.data());

  out.write(reinterpret_cast<const char*>(header), 44);
  if (!pcm16.empty()) {
    out.write(reinterpret_cast<const char*>(pcm16.data()),
              static_cast<std::streamsize>(pcm16.size() * sizeof(int16_t)));
  }
  return absl::OkStatus();
}

}  // namespace hibiki
