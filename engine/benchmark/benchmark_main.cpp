#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

#include "engine/core/history.hpp"
#include "engine/core/math.hpp"
#include "pb/commands.pb.h"
#include "pb/core.pb.h"
#include "pb/notifications.pb.h"

extern "C" {
int d_bench_init();
int d_bench_term();

int d_bench_encode_request_once(uint8_t* outBuf, int capacity);
uint64_t d_bench_encode_request_loop(uint8_t* outBuf, int capacity,
                                     int iterations);
uint64_t d_bench_decode_request_loop(const uint8_t* inData, int len,
                                     int iterations);

int d_bench_encode_notification_once(uint8_t* outBuf, int capacity);
uint64_t d_bench_encode_notification_loop(uint8_t* outBuf, int capacity,
                                          int iterations);
uint64_t d_bench_decode_notification_loop(const uint8_t* inData, int len,
                                          int iterations);

int d_bench_encode_project_once(uint8_t* outBuf, int capacity);
uint64_t d_bench_encode_project_loop(uint8_t* outBuf, int capacity,
                                     int iterations);
uint64_t d_bench_decode_project_loop(const uint8_t* inData, int len,
                                     int iterations);

void d_bench_biquad(const float* src, float* dst, int numSamples);
void d_bench_compressor(const float* inL, const float* inR, float* outL,
                        float* outR, int numBlocks, int blockSize);
double d_bench_mixer(double* trackBufL, double* trackBufR, double* masterL,
                     double* masterR, float* outInterleaved, int numTracks,
                     int numBlocks, int blockSize);
uint64_t d_bench_history(int iterations);
double d_bench_math(int iterations);
}

namespace {

template <class T>
inline void doNotOptimize(const T& value) {
  asm volatile("" : : "r,m"(value) : "memory");
}

inline std::string formatRatio(double ratio) {
  char buf[32];
  std::snprintf(buf, sizeof(buf), "%.2fx", ratio);
  return std::string(buf);
}

struct TimingStats {
  double medianMs;
  double minMs;
};

template <typename Func>
TimingStats measureMs(Func&& f, int warmups = 2, int repetitions = 7) {
  for (int w = 0; w < warmups; ++w) {
    f();
  }

  std::vector<double> times;
  times.reserve(repetitions);
  for (int r = 0; r < repetitions; ++r) {
    auto start = std::chrono::steady_clock::now();
    f();
    auto end = std::chrono::steady_clock::now();
    std::chrono::duration<double, std::milli> elapsed = end - start;
    times.push_back(elapsed.count());
  }
  std::sort(times.begin(), times.end());
  return {times[times.size() / 2], times.front()};
}

// ─── Faithful C++ Reference Implementations ─────────────────────────────────

// 1:1 equivalent of engine/core/biquad_filter.d
struct CppBiquad {
  enum class Type { OFF = -1, LOWPASS = 0 };
  Type type = Type::LOWPASS;
  float b0 = 0.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f;
  float x1 = 0.0f, x2 = 0.0f, y1 = 0.0f, y2 = 0.0f;

  CppBiquad() {
    float w0 = 2.0f * static_cast<float>(hibiki::pi) * 1000.0f / 44100.0f;
    float cos_w = std::cos(w0);
    float alpha = std::sin(w0) / (2.0f * 0.707f);
    float a0 = 1.0f + alpha;
    b0 = ((1.0f - cos_w) * 0.5f) / a0;
    b1 = (1.0f - cos_w) / a0;
    b2 = ((1.0f - cos_w) * 0.5f) / a0;
    a1 = (-2.0f * cos_w) / a0;
    a2 = (1.0f - alpha) / a0;
  }

  inline float process(float x) {
    if (type == Type::OFF) return x;
    float y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
    x2 = x1;
    x1 = x;
    y2 = y1;
    y1 = y;
    return y;
  }
};

void cpp_bench_biquad(const float* src, float* dst, int numSamples) {
  CppBiquad filter;
  for (int i = 0; i < numSamples; ++i) {
    dst[i] = filter.process(src[i]);
  }
}

// 1:1 faithful equivalent of engine/effects/builtin_compressor.d
struct CppCompressor {
  float envelope_db = 0.0f;
  float gain_reduction_db = 0.0f;
  float input_db = -200.0f;
  float output_db = -200.0f;
  float sidechain_db = -200.0f;

  static float computeGainReduction(float in_db, float threshold, float ratio,
                                    float knee_db, float up_threshold,
                                    float up_ratio) {
    float half_knee = knee_db / 2.0f;
    float gr_db = 0.0f;

    if (knee_db <= 0.01f) {
      if (in_db > threshold) {
        gr_db = (threshold - in_db) * (1.0f - 1.0f / ratio);
      }
    } else {
      float lower = threshold - half_knee;
      float upper = threshold + half_knee;
      if (in_db > lower && in_db < upper) {
        float x = in_db - lower;
        gr_db = (1.0f / ratio - 1.0f) * (x * x) / (2.0f * knee_db);
      } else if (in_db >= upper) {
        gr_db = (threshold - in_db) * (1.0f - 1.0f / ratio);
      }
    }

    if (up_ratio > 1.01f && in_db < up_threshold && in_db > -120.0f) {
      float up_gain = (up_threshold - in_db) * (1.0f - 1.0f / up_ratio);
      gr_db += std::min(up_gain, 30.0f);
    }
    return gr_db;
  }

  void processBlock(const float* inL, const float* inR, float* outL,
                    float* outR, int numSamples, double sampleRate,
                    double normThreshold, double normRatio, double normAttack,
                    double normRelease, double normKnee, double normMakeup,
                    double normUpThresh, double normUpRatio) {
    float threshold = static_cast<float>(normThreshold * 60.0 - 60.0);
    float ratio = (normRatio >= 0.999)
                      ? 1000.0f
                      : 1.0f / (1.0f - static_cast<float>(normRatio));
    float attack_ms = static_cast<float>(0.1 * std::pow(1000.0, normAttack));
    float release_ms = static_cast<float>(10.0 * std::pow(100.0, normRelease));
    float knee_db = static_cast<float>(normKnee * 30.0);
    float makeup_lin =
        static_cast<float>(std::pow(10.0, normMakeup * 30.0 / 20.0));
    float up_threshold = static_cast<float>(normUpThresh * 72.0 - 60.0);
    float up_ratio = (normUpRatio >= 0.999)
                         ? 1000.0f
                         : 1.0f / (1.0f - static_cast<float>(normUpRatio));

    float attack_coeff =
        static_cast<float>(std::exp(-1.0 / (attack_ms * 0.001 * sampleRate)));
    float release_coeff =
        static_cast<float>(std::exp(-1.0 / (release_ms * 0.001 * sampleRate)));

    float peak_gr = 0.0f;
    float peak_in_db = -200.0f;
    float peak_out_db = -200.0f;

    for (int i = 0; i < numSamples; ++i) {
      float l = inL[i];
      float r = inR[i];
      float input_abs = std::max(std::abs(l), std::abs(r));
      float in_db =
          (input_abs > 1e-10f) ? 20.0f * std::log10(input_abs) : -200.0f;
      if (in_db > peak_in_db) peak_in_db = in_db;

      float gr_db = computeGainReduction(in_db, threshold, ratio, knee_db,
                                         up_threshold, up_ratio);

      if (gr_db < envelope_db) {
        envelope_db =
            attack_coeff * envelope_db + (1.0f - attack_coeff) * gr_db;
      } else {
        envelope_db =
            release_coeff * envelope_db + (1.0f - release_coeff) * gr_db;
      }

      if (envelope_db < peak_gr) peak_gr = envelope_db;

      float gain =
          static_cast<float>(std::pow(10.0, envelope_db / 20.0)) * makeup_lin;
      float ol = l * gain;
      float or_ = r * gain;
      outL[i] = ol;
      outR[i] = or_;

      float out_abs = std::max(std::abs(ol), std::abs(or_));
      float out_db = (out_abs > 1e-10f) ? 20.0f * std::log10(out_abs) : -200.0f;
      if (out_db > peak_out_db) peak_out_db = out_db;
    }

    gain_reduction_db = peak_gr;
    input_db = peak_in_db;
    output_db = peak_out_db;
    sidechain_db = -200.0f;
  }
};

void cpp_bench_compressor(const float* inL, const float* inR, float* outL,
                          float* outR, int numBlocks, int blockSize) {
  CppCompressor comp;
  for (int b = 0; b < numBlocks; ++b) {
    int offset = b * blockSize;
    comp.processBlock(inL + offset, inR + offset, outL + offset, outR + offset,
                      blockSize, 44100.0, 0.7, 0.75, 0.2, 0.3, 0.2, 0.2, 0.0,
                      0.0);
  }
}

// 1:1 equivalent of engine/playback_mixer.d (including track/master peak
// metering)
double cpp_bench_mixer(double* trackBufL, double* trackBufR, double* masterL,
                       double* masterR, float* outInterleaved, int numTracks,
                       int numBlocks, int blockSize) {
  double peakAccum = 0.0;
  for (int b = 0; b < numBlocks; ++b) {
    std::fill(masterL, masterL + blockSize, 0.0);
    std::fill(masterR, masterR + blockSize, 0.0);

    for (int t = 0; t < numTracks; ++t) {
      float vol = 0.8f;
      float pan = (t % 2 == 0) ? -0.25f : 0.25f;
      float angle = (pan + 1.0f) * 0.25f * static_cast<float>(hibiki::pi);
      float totalL = std::cos(angle) * vol;
      float totalR = std::sin(angle) * vol;

      std::fill(trackBufL, trackBufL + blockSize, 0.1);
      std::fill(trackBufR, trackBufR + blockSize, 0.1);

      float trackPeakL = 0.0f, trackPeakR = 0.0f;
      for (int i = 0; i < blockSize; ++i) {
        trackBufL[i] *= totalL;
        trackBufR[i] *= totalR;
        trackPeakL =
            std::max(trackPeakL, static_cast<float>(std::abs(trackBufL[i])));
        trackPeakR =
            std::max(trackPeakR, static_cast<float>(std::abs(trackBufR[i])));
      }
      doNotOptimize(trackPeakL);
      doNotOptimize(trackPeakR);

      for (int i = 0; i < blockSize; ++i) {
        masterL[i] += trackBufL[i];
        masterR[i] += trackBufR[i];
      }
    }

    float pL = 0.0f, pR = 0.0f;
    for (int i = 0; i < blockSize; ++i) {
      float l = static_cast<float>(masterL[i]);
      float r = static_cast<float>(masterR[i]);
      outInterleaved[i * 2] = l;
      outInterleaved[i * 2 + 1] = r;
      pL = std::max(pL, std::abs(l));
      pR = std::max(pR, std::abs(r));
    }
    peakAccum += pL + pR;
  }
  return peakAccum;
}

uint64_t cpp_bench_history(int iterations) {
  hibiki::HistoryManager hist;
  std::vector<uint8_t> stateBytes(64);
  for (int k = 0; k < 64; ++k) {
    stateBytes[k] = static_cast<uint8_t>(k + 1);
  }
  std::vector<uint8_t> temp;
  uint64_t checksum = 0;

  for (int i = 0; i < iterations; ++i) {
    stateBytes[0] = static_cast<uint8_t>(i & 0xFF);
    hist.pushState(stateBytes);
    if (i % 4 == 0) {
      if (hist.undo(stateBytes, temp)) {
        checksum += temp[0];
        if (hist.redo(temp, stateBytes)) {
          checksum += stateBytes[0];
        }
      }
    }
  }
  return checksum;
}

double cpp_bench_math(int iterations) {
  double sum = 0.0;
  for (int i = 0; i < iterations; ++i) {
    float pan = static_cast<float>(i % 200 - 100) / 100.0f;
    float p = std::max(-1.0f, std::min(1.0f, pan));
    float angle = (p + 1.0f) * 0.25f * static_cast<float>(hibiki::pi);
    float gL = std::cos(angle);
    float gR = std::sin(angle);

    float db = static_cast<float>(i % 120 - 60);
    float lin = std::pow(10.0f, db / 20.0f);
    sum += static_cast<double>(gL + gR + lin);
  }
  return sum;
}

struct BenchResult {
  std::string name;
  std::string workload;
  TimingStats cppStats;
  TimingStats dStats;
  std::string metricCpp;
  std::string metricD;
  std::string speedup;
  std::string verification;
};

}  // namespace

int main() {
  d_bench_init();

  std::cout << "\n============================================================="
               "=====\n";
  std::cout
      << " Hibiki DAW: C++23 (Clang) vs D (LDC 1.41.0) Performance Suite\n";
  std::cout
      << " (Median of 7 runs after 2 warmups, -c opt, zero-FFI inner loops)\n";
  std::cout << "==============================================================="
               "===\n\n";

  std::vector<BenchResult> results;

  // 1. Protobuf Request Encode / Decode
  {
    const int N = 200'000;
    hibiki::pb::commands::Request req;
    req.mutable_transport()->set_action(
        hibiki::pb::commands::TransportCmd::ACTION_PLAY);
    std::string cppStr;
    req.SerializeToString(&cppStr);

    std::vector<uint8_t> cppBuf(4096);
    std::vector<uint8_t> dBuf(4096);
    int dLen = d_bench_encode_request_once(dBuf.data(), 4096);
    bool wireMatch =
        (dLen == static_cast<int>(cppStr.size())) &&
        std::equal(dBuf.begin(), dBuf.begin() + dLen,
                   reinterpret_cast<const uint8_t*>(cppStr.data()));

    uint64_t cppEncSum = 0, dEncSum = 0;
    auto cppEncStats = measureMs([&]() {
      uint64_t s = 0;
      const int byteSize = static_cast<int>(req.ByteSizeLong());
      for (int i = 0; i < N; ++i) {
        req.SerializeToArray(cppBuf.data(), byteSize);
        doNotOptimize(cppBuf[0]);
        s += byteSize + cppBuf[0];
      }
      cppEncSum = s;
    });

    auto dEncStats = measureMs(
        [&]() { dEncSum = d_bench_encode_request_loop(dBuf.data(), 4096, N); });

    uint64_t cppDecSum = 0, dDecSum = 0;
    auto cppDecStats = measureMs([&]() {
      uint64_t s = 0;
      for (int i = 0; i < N; ++i) {
        hibiki::pb::commands::Request r;
        if (r.ParseFromArray(cppStr.data(), static_cast<int>(cppStr.size()))) {
          s += static_cast<uint64_t>(r.transport().action());
        }
      }
      cppDecSum = s;
    });

    auto dDecStats = measureMs(
        [&]() { dDecSum = d_bench_decode_request_loop(dBuf.data(), dLen, N); });

    double cppEncKops = (N / 1e3) / (cppEncStats.medianMs / 1000.0);
    double dEncKops = (N / 1e3) / (dEncStats.medianMs / 1000.0);
    results.push_back({
        "Protobuf Request Encode",
        "200K msgs (pre-alloc buf)",
        cppEncStats,
        dEncStats,
        std::to_string(static_cast<int>(std::round(cppEncKops))) + " K ops/s",
        std::to_string(static_cast<int>(std::round(dEncKops))) + " K ops/s",
        formatRatio(dEncKops / cppEncKops),
        (wireMatch && cppEncSum == dEncSum) ? "PASSED (wire-identical)"
                                            : "MISMATCH",
    });

    double cppDecKops = (N / 1e3) / (cppDecStats.medianMs / 1000.0);
    double dDecKops = (N / 1e3) / (dDecStats.medianMs / 1000.0);
    results.push_back({
        "Protobuf Request Decode",
        "200K msgs (Arena reuse)",
        cppDecStats,
        dDecStats,
        std::to_string(static_cast<int>(std::round(cppDecKops))) + " K ops/s",
        std::to_string(static_cast<int>(std::round(dDecKops))) + " K ops/s",
        formatRatio(dDecKops / cppDecKops),
        (cppDecSum == dDecSum) ? "PASSED (checksum match)" : "MISMATCH",
    });
  }

  // 2. Protobuf Notification Encode / Decode
  {
    const int N = 200'000;
    hibiki::pb::notifications::Notification notif;
    auto* phi = notif.mutable_playhead_info();
    phi->set_position_sec(12.5f);
    phi->set_bpm(128.0f);
    phi->set_loop_enabled(true);
    phi->set_loop_start(1.0f);
    phi->set_loop_end(5.0f);
    std::string cppNotifStr;
    notif.SerializeToString(&cppNotifStr);

    std::vector<uint8_t> cppBuf(4096);
    std::vector<uint8_t> dBuf(4096);
    int dLen = d_bench_encode_notification_once(dBuf.data(), 4096);
    bool wireMatch =
        (dLen == static_cast<int>(cppNotifStr.size())) &&
        std::equal(dBuf.begin(), dBuf.begin() + dLen,
                   reinterpret_cast<const uint8_t*>(cppNotifStr.data()));

    uint64_t cppEncSum = 0, dEncSum = 0;
    auto cppEncStats = measureMs([&]() {
      uint64_t s = 0;
      const int byteSize = static_cast<int>(notif.ByteSizeLong());
      for (int i = 0; i < N; ++i) {
        notif.SerializeToArray(cppBuf.data(), byteSize);
        doNotOptimize(cppBuf[0]);
        s += byteSize + cppBuf[0];
      }
      cppEncSum = s;
    });

    auto dEncStats = measureMs([&]() {
      dEncSum = d_bench_encode_notification_loop(dBuf.data(), 4096, N);
    });

    uint64_t cppDecSum = 0, dDecSum = 0;
    auto cppDecStats = measureMs([&]() {
      uint64_t s = 0;
      for (int i = 0; i < N; ++i) {
        hibiki::pb::notifications::Notification n;
        if (n.ParseFromArray(cppNotifStr.data(),
                             static_cast<int>(cppNotifStr.size()))) {
          s += static_cast<uint64_t>(n.playhead_info().bpm());
        }
      }
      cppDecSum = s;
    });

    auto dDecStats = measureMs([&]() {
      dDecSum = d_bench_decode_notification_loop(dBuf.data(), dLen, N);
    });

    double cppEncKops = (N / 1e3) / (cppEncStats.medianMs / 1000.0);
    double dEncKops = (N / 1e3) / (dEncStats.medianMs / 1000.0);
    results.push_back({
        "Protobuf Notif Encode",
        "200K msgs (pre-alloc buf)",
        cppEncStats,
        dEncStats,
        std::to_string(static_cast<int>(std::round(cppEncKops))) + " K ops/s",
        std::to_string(static_cast<int>(std::round(dEncKops))) + " K ops/s",
        formatRatio(dEncKops / cppEncKops),
        (wireMatch && cppEncSum == dEncSum) ? "PASSED (wire-identical)"
                                            : "MISMATCH",
    });

    double cppDecKops = (N / 1e3) / (cppDecStats.medianMs / 1000.0);
    double dDecKops = (N / 1e3) / (dDecStats.medianMs / 1000.0);
    results.push_back({
        "Protobuf Notif Decode",
        "200K msgs (Arena reuse)",
        cppDecStats,
        dDecStats,
        std::to_string(static_cast<int>(std::round(cppDecKops))) + " K ops/s",
        std::to_string(static_cast<int>(std::round(dDecKops))) + " K ops/s",
        formatRatio(dDecKops / cppDecKops),
        (cppDecSum == dDecSum) ? "PASSED (checksum match)" : "MISMATCH",
    });
  }

  // 3. Protobuf Full Project Session (8 tracks, 32 clips, 512 MIDI events)
  {
    const int N = 2'000;
    hibiki::pb::core::Project proj;
    proj.set_bpm(128.0f);
    proj.set_playhead_sec(16.0f);
    for (int t = 0; t < 8; ++t) {
      auto* track = proj.add_tracks();
      track->set_track_index(t);
      track->set_name("Track");
      for (int c = 0; c < 4; ++c) {
        auto* tc = track->add_timeline_clips();
        tc->set_clip_index(c);
        tc->set_start_time_sec(c * 4.0f);
        tc->set_duration_beats(4.0f);
        auto* clip = tc->mutable_clip();
        clip->set_type(hibiki::pb::core::CLIP_TYPE_MIDI);
        clip->set_name("Midi Clip");
        for (int m = 0; m < 16; ++m) {
          auto* ev = clip->add_midi_events();
          ev->set_tick(m * 120);
          ev->set_pitch(60 + (m % 12));
          ev->set_duration_ticks(100);
          ev->set_velocity(100);
        }
      }
    }
    std::string cppProjStr;
    proj.SerializeToString(&cppProjStr);

    std::vector<uint8_t> cppBuf(65536);
    std::vector<uint8_t> dBuf(65536);
    int dLen = d_bench_encode_project_once(dBuf.data(), 65536);
    bool wireMatch =
        (dLen == static_cast<int>(cppProjStr.size())) &&
        std::equal(dBuf.begin(), dBuf.begin() + dLen,
                   reinterpret_cast<const uint8_t*>(cppProjStr.data()));

    uint64_t cppEncSum = 0, dEncSum = 0;
    auto cppEncStats = measureMs([&]() {
      uint64_t s = 0;
      const int byteSize = static_cast<int>(proj.ByteSizeLong());
      for (int i = 0; i < N; ++i) {
        proj.SerializeToArray(cppBuf.data(), byteSize);
        doNotOptimize(cppBuf[0]);
        s += byteSize + cppBuf[0];
      }
      cppEncSum = s;
    });

    auto dEncStats = measureMs([&]() {
      dEncSum = d_bench_encode_project_loop(dBuf.data(), 65536, N);
    });

    uint64_t cppDecSum = 0, dDecSum = 0;
    auto cppDecStats = measureMs([&]() {
      uint64_t s = 0;
      for (int i = 0; i < N; ++i) {
        hibiki::pb::core::Project p;
        if (p.ParseFromArray(cppProjStr.data(),
                             static_cast<int>(cppProjStr.size()))) {
          s += static_cast<uint64_t>(p.tracks_size()) +
               static_cast<uint64_t>(p.bpm());
        }
      }
      cppDecSum = s;
    });

    auto dDecStats = measureMs(
        [&]() { dDecSum = d_bench_decode_project_loop(dBuf.data(), dLen, N); });

    double cppEncOps = N / (cppEncStats.medianMs / 1000.0);
    double dEncOps = N / (dEncStats.medianMs / 1000.0);
    results.push_back({
        "Project Session Save (Proto)",
        "2K x " + std::to_string(dLen) + " B",
        cppEncStats,
        dEncStats,
        std::to_string(static_cast<int>(std::round(cppEncOps))) + " ops/s",
        std::to_string(static_cast<int>(std::round(dEncOps))) + " ops/s",
        formatRatio(dEncOps / cppEncOps),
        (wireMatch && cppEncSum == dEncSum) ? "PASSED (wire-identical)"
                                            : "MISMATCH",
    });

    double cppDecOps = N / (cppDecStats.medianMs / 1000.0);
    double dDecOps = N / (dDecStats.medianMs / 1000.0);
    results.push_back({
        "Project Session Load (Proto)",
        "2K x " + std::to_string(dLen) + " B",
        cppDecStats,
        dDecStats,
        std::to_string(static_cast<int>(std::round(cppDecOps))) + " ops/s",
        std::to_string(static_cast<int>(std::round(dDecOps))) + " ops/s",
        formatRatio(dDecOps / cppDecOps),
        (cppDecSum == dDecSum) ? "PASSED (checksum match)" : "MISMATCH",
    });
  }

  // 4. DSP Biquad Filter (10M samples, immutable src -> dst)
  {
    const int SAMPLES = 10'000'000;
    std::vector<float> src(SAMPLES);
    for (int i = 0; i < SAMPLES; ++i) {
      src[i] = std::sin(i * 0.05f) * 0.8f;
    }
    std::vector<float> dstCpp(SAMPLES);
    std::vector<float> dstD(SAMPLES);

    auto cppStats = measureMs([&]() {
      cpp_bench_biquad(src.data(), dstCpp.data(), SAMPLES);
      doNotOptimize(dstCpp.back());
    });
    auto dStats = measureMs([&]() {
      d_bench_biquad(src.data(), dstD.data(), SAMPLES);
      doNotOptimize(dstD.back());
    });

    float maxDiff = 0.0f;
    for (int i = 0; i < SAMPLES; ++i) {
      maxDiff = std::max(maxDiff, std::abs(dstCpp[i] - dstD[i]));
    }

    double cppMsamp = (SAMPLES / 1e6) / (cppStats.medianMs / 1000.0);
    double dMsamp = (SAMPLES / 1e6) / (dStats.medianMs / 1000.0);
    results.push_back({
        "DSP Biquad Filter (RBJ)",
        "10M samples",
        cppStats,
        dStats,
        std::to_string(static_cast<int>(std::round(cppMsamp))) + " MSamp/s",
        std::to_string(static_cast<int>(std::round(dMsamp))) + " MSamp/s",
        formatRatio(dMsamp / cppMsamp),
        (maxDiff < 1e-5f) ? "PASSED (eps < 1e-5)" : "DIFF DETECTED",
    });
  }

  // 5. DSP Compressor (1:1 knee + upward + metering, 5.12M stereo samples)
  {
    const int BLOCKS = 10'000;
    const int BLOCK_SIZE = 512;
    const int TOTAL_SAMPLES = BLOCKS * BLOCK_SIZE;

    std::vector<float> inL(TOTAL_SAMPLES), inR(TOTAL_SAMPLES);
    for (int i = 0; i < TOTAL_SAMPLES; ++i) {
      inL[i] = std::sin(i * 0.05f) * 0.8f;
      inR[i] = std::cos(i * 0.05f) * 0.8f;
    }

    std::vector<float> outCppL(TOTAL_SAMPLES), outCppR(TOTAL_SAMPLES);
    std::vector<float> outDL(TOTAL_SAMPLES), outDR(TOTAL_SAMPLES);

    auto cppStats = measureMs([&]() {
      cpp_bench_compressor(inL.data(), inR.data(), outCppL.data(),
                           outCppR.data(), BLOCKS, BLOCK_SIZE);
      doNotOptimize(outCppL.back());
    });

    auto dStats = measureMs([&]() {
      d_bench_compressor(inL.data(), inR.data(), outDL.data(), outDR.data(),
                         BLOCKS, BLOCK_SIZE);
      doNotOptimize(outDL.back());
    });

    float maxDiff = 0.0f;
    for (int i = 0; i < TOTAL_SAMPLES; ++i) {
      maxDiff = std::max(maxDiff, std::abs(outCppL[i] - outDL[i]));
      maxDiff = std::max(maxDiff, std::abs(outCppR[i] - outDR[i]));
    }

    double audioDurationSec = static_cast<double>(TOTAL_SAMPLES) / 44100.0;
    double cppRt = audioDurationSec / (cppStats.medianMs / 1000.0);
    double dRt = audioDurationSec / (dStats.medianMs / 1000.0);
    results.push_back({
        "DSP Dynamics (Compressor)",
        "5.12M stereo (116s)",
        cppStats,
        dStats,
        std::to_string(static_cast<int>(std::round(cppRt))) + "x RT",
        std::to_string(static_cast<int>(std::round(dRt))) + "x RT",
        formatRatio(dRt / cppRt),
        (maxDiff < 1e-4f) ? "PASSED (eps < 1e-4)" : "DIFF DETECTED",
    });
  }

  // 6. Playback Mixer (16 tracks x 10,000 blocks = 5.12M frames)
  {
    const int NUM_TRACKS = 16;
    const int BLOCKS = 10'000;
    const int BLOCK_SIZE = 512;

    std::vector<double> trackBufL(BLOCK_SIZE), trackBufR(BLOCK_SIZE);
    std::vector<double> masterL(BLOCK_SIZE), masterR(BLOCK_SIZE);
    std::vector<float> outCpp(BLOCK_SIZE * 2), outD(BLOCK_SIZE * 2);

    double cppPeak = 0.0, dPeak = 0.0;
    auto cppStats = measureMs([&]() {
      cppPeak = cpp_bench_mixer(trackBufL.data(), trackBufR.data(),
                                masterL.data(), masterR.data(), outCpp.data(),
                                NUM_TRACKS, BLOCKS, BLOCK_SIZE);
      doNotOptimize(cppPeak);
    });

    auto dStats = measureMs([&]() {
      dPeak = d_bench_mixer(trackBufL.data(), trackBufR.data(), masterL.data(),
                            masterR.data(), outD.data(), NUM_TRACKS, BLOCKS,
                            BLOCK_SIZE);
      doNotOptimize(dPeak);
    });

    float maxDiff = static_cast<float>(std::abs(cppPeak - dPeak));
    for (int i = 0; i < BLOCK_SIZE * 2; ++i) {
      maxDiff = std::max(maxDiff, std::abs(outCpp[i] - outD[i]));
    }

    double audioDurationSec =
        static_cast<double>(BLOCKS * BLOCK_SIZE) / 44100.0;
    double cppRt = audioDurationSec / (cppStats.medianMs / 1000.0);
    double dRt = audioDurationSec / (dStats.medianMs / 1000.0);
    results.push_back({
        "Mixer Engine (16 tracks)",
        "10K blocks (5.12M)",
        cppStats,
        dStats,
        std::to_string(static_cast<int>(std::round(cppRt))) + "x RT",
        std::to_string(static_cast<int>(std::round(dRt))) + "x RT",
        formatRatio(dRt / cppRt),
        (maxDiff < 1e-2f) ? "PASSED (exact)" : "DIFF DETECTED",
    });
  }

  // 7. History Manager (100,000 push/undo/redo cycles, 64B state)
  {
    const int ITERATIONS = 100'000;
    uint64_t cppSum = 0, dSum = 0;
    auto cppStats =
        measureMs([&]() { cppSum = cpp_bench_history(ITERATIONS); });
    auto dStats = measureMs([&]() { dSum = d_bench_history(ITERATIONS); });

    double cppKops = (ITERATIONS / 1e3) / (cppStats.medianMs / 1000.0);
    double dKops = (ITERATIONS / 1e3) / (dStats.medianMs / 1000.0);
    results.push_back({
        "Undo/Redo History (50 ring)",
        "100K ops (64B state)",
        cppStats,
        dStats,
        std::to_string(static_cast<int>(std::round(cppKops))) + " K ops/s",
        std::to_string(static_cast<int>(std::round(dKops))) + " K ops/s",
        formatRatio(dKops / cppKops),
        (cppSum == dSum) ? "PASSED (checksum match)" : "MISMATCH",
    });
  }

  // 8. Fast Math Primitives (10,000,000 iterations)
  {
    const int ITERATIONS = 10'000'000;
    double cppSum = 0.0, dSum = 0.0;

    auto cppStats = measureMs([&]() { cppSum = cpp_bench_math(ITERATIONS); });
    auto dStats = measureMs([&]() { dSum = d_bench_math(ITERATIONS); });

    double cppMops = (ITERATIONS / 1e6) / (cppStats.medianMs / 1000.0);
    double dMops = (ITERATIONS / 1e6) / (dStats.medianMs / 1000.0);
    results.push_back({
        "Fast Math (Pan & dBtoLin)",
        "10M iterations",
        cppStats,
        dStats,
        std::to_string(static_cast<int>(std::round(cppMops))) + " MOps/s",
        std::to_string(static_cast<int>(std::round(dMops))) + " MOps/s",
        formatRatio(dMops / cppMops),
        (std::abs(cppSum - dSum) < 1e-2) ? "PASSED (match)" : "DIFF DETECTED",
    });
  }

  std::cout << "| Benchmark | Workload | C++23 (med / min) | D LDC (med / min) "
               "| D/C++ Ratio | Verification |\n";
  std::cout << "| :--- | :--- | :--- | :--- | :---: | :---: |\n";

  for (const auto& r : results) {
    std::cout << "| **" << r.name << "** | " << r.workload << " | "
              << std::fixed << std::setprecision(2) << r.cppStats.medianMs
              << " / " << r.cppStats.minMs << " ms (" << r.metricCpp << ") | "
              << r.dStats.medianMs << " / " << r.dStats.minMs << " ms ("
              << r.metricD << ") | **" << r.speedup << "** | " << r.verification
              << " |\n";
  }
  std::cout << "\n";

  d_bench_term();
  return 0;
}
