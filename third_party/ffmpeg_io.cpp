#include "engine/core/audio_file.hpp"

#include <cstdint>
#include <fstream>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>
#include <libavutil/samplefmt.h>
#include <libswresample/swresample.h>
}

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

// AVIO read callback for in-memory buffer
struct MemBuffer {
  const uint8_t* data;
  size_t size;
  size_t pos;
};

static int mem_read(void* opaque, uint8_t* buf, int buf_size) {
  auto* mb = static_cast<MemBuffer*>(opaque);
  int remaining = (int)(mb->size - mb->pos);
  if (remaining <= 0) return AVERROR_EOF;
  int to_read = std::min(buf_size, remaining);
  memcpy(buf, mb->data + mb->pos, to_read);
  mb->pos += to_read;
  return to_read;
}

static int64_t mem_seek(void* opaque, int64_t offset, int whence) {
  auto* mb = static_cast<MemBuffer*>(opaque);
  if (whence == AVSEEK_SIZE) return (int64_t)mb->size;
  if (whence == SEEK_SET)
    mb->pos = (size_t)offset;
  else if (whence == SEEK_CUR)
    mb->pos += (size_t)offset;
  else if (whence == SEEK_END)
    mb->pos = mb->size + (size_t)offset;
  return (int64_t)mb->pos;
}

absl::Status LoadAudioFile(const std::string& path, double target_sample_rate,
                           std::vector<float>& out_data, int& out_channels,
                           int& out_sample_rate, double& out_duration_sec) {
  // Read file into memory (works with bazel sandbox symlinks)
  std::ifstream f(path, std::ios::binary | std::ios::ate);
  if (!f) {
    return absl::NotFoundError(absl::StrCat("Cannot open audio file: ", path));
  }
  size_t file_size = f.tellg();
  f.seekg(0);
  std::vector<uint8_t> file_data(file_size);
  f.read(reinterpret_cast<char*>(file_data.data()), file_size);

  MemBuffer mb{file_data.data(), file_size, 0};

  // Create custom AVIO context
  const int avio_buf_size = 32768;
  auto* avio_buf = static_cast<uint8_t*>(av_malloc(avio_buf_size));
  AVIOContext* avio_ctx = avio_alloc_context(avio_buf, avio_buf_size, 0, &mb,
                                             mem_read, nullptr, mem_seek);
  if (!avio_ctx) {
    av_free(avio_buf);
    return absl::InternalError("Failed to allocate AVIO context");
  }

  // Open format context with custom I/O
  AVFormatContext* fmt_ctx = avformat_alloc_context();
  fmt_ctx->pb = avio_ctx;
  // Hint the format demuxer using file extension
  const AVInputFormat* ifmt = nullptr;
  std::string ext;
  if (path.size() > 4) {
    ext = path.substr(path.size() - 4);
    for (auto& c : ext) c = std::tolower(c);
    if (ext == ".wav")
      ifmt = av_find_input_format("wav");
    else if (ext == ".mp3")
      ifmt = av_find_input_format("mp3");
    else if (ext == "flac" || ext == "lac\0")
      ifmt = av_find_input_format("flac");
    else if (ext == ".ogg")
      ifmt = av_find_input_format("ogg");
    else if (ext == ".aif" || ext == "aiff")
      ifmt = av_find_input_format("aiff");
  }
  if (avformat_open_input(&fmt_ctx, nullptr, ifmt, nullptr) < 0) {
    avio_context_free(&avio_ctx);
    return absl::InvalidArgumentError(
        absl::StrCat("Cannot parse audio file: ", path));
  }
  // RAII cleanup for format context
  struct FmtGuard {
    AVFormatContext* ctx;
    ~FmtGuard() { avformat_close_input(&ctx); }
  } fmt_guard{fmt_ctx};

  if (avformat_find_stream_info(fmt_ctx, nullptr) < 0) {
    return absl::InvalidArgumentError(
        absl::StrCat("Cannot find stream info in: ", path));
  }

  // Find audio stream
  int audio_stream_idx = -1;
  const AVCodec* codec = nullptr;
  for (unsigned i = 0; i < fmt_ctx->nb_streams; ++i) {
    if (fmt_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
      audio_stream_idx = (int)i;
      codec = avcodec_find_decoder(fmt_ctx->streams[i]->codecpar->codec_id);
      break;
    }
  }
  if (audio_stream_idx < 0 || !codec) {
    return absl::InvalidArgumentError(
        absl::StrCat("No audio stream found in: ", path));
  }

  AVCodecParameters* codecpar = fmt_ctx->streams[audio_stream_idx]->codecpar;

  // Open codec
  AVCodecContext* codec_ctx = avcodec_alloc_context3(codec);
  if (!codec_ctx) {
    return absl::InternalError("Failed to allocate codec context");
  }
  struct CodecGuard {
    AVCodecContext* ctx;
    ~CodecGuard() { avcodec_free_context(&ctx); }
  } codec_guard{codec_ctx};

  if (avcodec_parameters_to_context(codec_ctx, codecpar) < 0) {
    return absl::InternalError("Failed to copy codec parameters");
  }
  if (avcodec_open2(codec_ctx, codec, nullptr) < 0) {
    return absl::InternalError(absl::StrCat("Cannot open codec for: ", path));
  }

  int src_sample_rate = codec_ctx->sample_rate;
  int src_channels = codec_ctx->ch_layout.nb_channels;
  int dst_sample_rate =
      (target_sample_rate > 0) ? (int)target_sample_rate : src_sample_rate;

  // Set up resampler: convert to interleaved float at target rate
  SwrContext* swr_ctx = nullptr;
  AVChannelLayout dst_layout;
  if (src_channels == 1) {
    dst_layout = AV_CHANNEL_LAYOUT_MONO;
  } else {
    dst_layout = AV_CHANNEL_LAYOUT_STEREO;
    src_channels = 2;  // Force stereo output for multi-channel
  }

  if (swr_alloc_set_opts2(&swr_ctx, &dst_layout, AV_SAMPLE_FMT_FLT,
                          dst_sample_rate, &codec_ctx->ch_layout,
                          codec_ctx->sample_fmt, src_sample_rate, 0,
                          nullptr) < 0) {
    return absl::InternalError("Failed to set resampler options");
  }
  struct SwrGuard {
    SwrContext* ctx;
    ~SwrGuard() { swr_free(&ctx); }
  } swr_guard{swr_ctx};

  if (swr_init(swr_ctx) < 0) {
    return absl::InternalError("Failed to init resampler");
  }

  // Decode and resample
  AVPacket* pkt = av_packet_alloc();
  AVFrame* frame = av_frame_alloc();
  struct PktFrameGuard {
    AVPacket* pkt;
    AVFrame* frame;
    ~PktFrameGuard() {
      av_packet_free(&pkt);
      av_frame_free(&frame);
    }
  } pf_guard{pkt, frame};

  std::vector<float> all_samples;

  while (av_read_frame(fmt_ctx, pkt) >= 0) {
    if (pkt->stream_index == audio_stream_idx) {
      if (avcodec_send_packet(codec_ctx, pkt) >= 0) {
        while (avcodec_receive_frame(codec_ctx, frame) >= 0) {
          // Calculate output size
          int out_nb_samples = swr_get_out_samples(swr_ctx, frame->nb_samples);
          if (out_nb_samples <= 0) continue;

          std::vector<float> tmp(out_nb_samples * src_channels);
          uint8_t* out_buf = reinterpret_cast<uint8_t*>(tmp.data());
          int converted = swr_convert(swr_ctx, &out_buf, out_nb_samples,
                                      (const uint8_t**)frame->extended_data,
                                      frame->nb_samples);
          if (converted > 0) {
            all_samples.insert(all_samples.end(), tmp.begin(),
                               tmp.begin() + converted * src_channels);
          }
        }
      }
    }
    av_packet_unref(pkt);
  }

  // Flush decoder
  avcodec_send_packet(codec_ctx, nullptr);
  while (avcodec_receive_frame(codec_ctx, frame) >= 0) {
    int out_nb_samples = swr_get_out_samples(swr_ctx, frame->nb_samples);
    if (out_nb_samples <= 0) continue;
    std::vector<float> tmp(out_nb_samples * src_channels);
    uint8_t* out_buf = reinterpret_cast<uint8_t*>(tmp.data());
    int converted =
        swr_convert(swr_ctx, &out_buf, out_nb_samples,
                    (const uint8_t**)frame->extended_data, frame->nb_samples);
    if (converted > 0) {
      all_samples.insert(all_samples.end(), tmp.begin(),
                         tmp.begin() + converted * src_channels);
    }
  }

  // Flush resampler
  {
    int out_nb_samples = swr_get_out_samples(swr_ctx, 0);
    if (out_nb_samples > 0) {
      std::vector<float> tmp(out_nb_samples * src_channels);
      uint8_t* out_buf = reinterpret_cast<uint8_t*>(tmp.data());
      int converted =
          swr_convert(swr_ctx, &out_buf, out_nb_samples, nullptr, 0);
      if (converted > 0) {
        all_samples.insert(all_samples.end(), tmp.begin(),
                           tmp.begin() + converted * src_channels);
      }
    }
  }

  if (all_samples.empty()) {
    return absl::DataLossError(
        absl::StrCat("No audio data decoded from: ", path));
  }

  out_data = std::move(all_samples);
  out_channels = src_channels;
  out_sample_rate = dst_sample_rate;
  out_duration_sec = (double)out_data.size() / (src_channels * dst_sample_rate);

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
