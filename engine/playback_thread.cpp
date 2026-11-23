#include "engine/playback_thread.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "engine/audio/sound.hpp"
#include "engine/core/audio_file.hpp"
#include "engine/core/clip.hpp"
#include "engine/core/math.hpp"
#include "engine/core/midi.hpp"
#include "engine/core/project.hpp"
#include "engine/core/track.hpp"
#include "engine/effects/builtin_compressor.hpp"
#include "engine/effects/builtin_delay.hpp"
#include "engine/effects/builtin_envelope_shaper.hpp"
#include "engine/effects/builtin_eq.hpp"
#include "engine/effects/builtin_hott.hpp"
#include "engine/effects/builtin_maxim.hpp"
#include "engine/effects/builtin_phaser.hpp"
#include "engine/effects/builtin_reverb.hpp"
#include "engine/instruments/builtin_drum_machine.hpp"
#include "engine/ipc/ipc.hpp"
#include "engine/vst3/vst3_host.hpp"
#include "pb/commands.pb.h"
#include "pb/notifications.pb.h"

extern void d_advance_playhead(double currentPos, double timePerBlock,
                               bool loopEnabled, double loopStart,
                               double loopEnd, double* outPos);
extern void d_apply_track_gain_and_pan(double* bufferL, double* bufferR,
                                       int blockSize, float volume, float pan,
                                       bool audible, float* outPeakL,
                                       float* outPeakR);
extern void d_accumulate_mix(const double* trackL, const double* trackR,
                             double* masterL, double* masterR, int blockSize);
extern void d_process_master_output(const double* masterL,
                                    const double* masterR,
                                    float* outInterleaved, int blockSize,
                                    float* outPeakL, float* outPeakR);
extern void d_render_audio_clip_block(
    const float* audioData, int audioLen, int numChannels, bool isLoop,
    int trimSamples, int startSample, int loopLen, double clipLocalTime,
    double clipDuration, double fadeInSec, double fadeOutSec, double sampleRate,
    double* bufferL, double* bufferR, int blockSize);
extern void d_accumulate_aux_send(const double* srcL, const double* srcR,
                                  double* dstL, double* dstR, int blockSize,
                                  float level);
extern void d_double_to_float_stereo(const double* srcL, const double* srcR,
                                     float* dstL, float* dstR, int blockSize);
extern void d_float_to_double_stereo(const float* srcL, const float* srcR,
                                     double* dstL, double* dstR, int blockSize);
extern void d_render_session_audio_clip_block(const float* audioData,
                                              int audioLen, int numChannels,
                                              bool isLoop, int startSample,
                                              double* bufferL, double* bufferR,
                                              int blockSize);
extern void d_compute_waveform_summary(const float* recordBuffer,
                                       int totalSamples, int numChannels,
                                       float* outSummary, int summarySize);
extern "C" {
void d_apply_track_eq_and_stereo_sep(double* bufL, double* bufR, int blockSize,
                                     double sampleRate, float eqLow,
                                     float eqMid, float eqHigh, float stereoSep,
                                     double* eqState24);
void d_blend_dry_wet(const double* dryL, const double* dryR, double* wetL,
                     double* wetR, int blockSize, float mix);
}

namespace hibiki {

ProjectState* createProjectState() {
  auto* state = new ProjectState();
  state->bpm = 140.0;
  state->sample_rate = 44100.0;
  return state;
}

void destroyProjectState(ProjectState* state) { delete state; }

HistoryManager* createHistoryManager() { return new HistoryManager(); }

void destroyHistoryManager(HistoryManager* history) { delete history; }

bool isStateQuit(const ProjectState* state) {
  return state ? state->quit.load() : true;
}

void setStateQuit(ProjectState* state, bool quit) {
  if (state) state->quit = quit;
}

void setTimelinePlaying(ProjectState* state, bool playing) {
  if (state) state->is_timeline_playing = playing;
}

bool isTimelinePlaying(const ProjectState* state) {
  return state ? state->is_timeline_playing.load() : false;
}

bool isRecording(const ProjectState* state) {
  return state ? state->is_recording.load() : false;
}

void setPlayheadPos(ProjectState* state, double pos) {
  if (state) state->playhead_pos_sec = pos;
}

double getPlayheadPos(const ProjectState* state) {
  return state ? state->playhead_pos_sec.load() : 0.0;
}

void setBpm(ProjectState* state, double bpm) {
  if (state && bpm > 0) state->bpm = bpm;
}

double getBpm(const ProjectState* state) {
  return state ? state->bpm.load() : 120.0;
}

void setLoop(ProjectState* state, bool enabled, double start_sec,
             double end_sec) {
  if (!state) return;
  state->loop_enabled = enabled;
  state->loop_start_sec = start_sec;
  state->loop_end_sec = end_sec;
}

void setMetronomeEnabled(ProjectState* state, bool enabled) {
  if (state) state->metronome_enabled = enabled;
}

void triggerPanic(ProjectState* state) {
  if (!state) return;
  std::lock_guard<std::mutex> lock(state->tracks_mutex);
  for (auto& pair : state->tracks) {
    if (pair.second) {
      pair.second->Stop();
      pair.second->panic_requested_.store(true);
    }
  }
}

void setMasterVolume(ProjectState* state, float vol) {
  if (state) state->master_volume = std::clamp(vol, 0.0f, 1.5f);
}

void setMasterPitch(ProjectState* state, float pitch) {
  if (state) state->master_pitch = std::clamp(pitch, -12.0f, 12.0f);
}

void stopPlaybackTracks(ProjectState* state) {
  if (!state) return;
  std::lock_guard<std::mutex> lock(state->tracks_mutex);
  for (auto& pair : state->tracks) {
    if (pair.second) pair.second->Stop();
  }
}

void listVst3Plugins(const char* path_ptr, size_t path_len) {
  std::string path(path_ptr, path_len);
  auto plugins = Vst3Plugin::listPlugins(path);
  for (const auto& p : plugins) {
    std::cout << p.index << ":" << p.name << ":" << p.vendor << ":"
              << (p.is_instrument ? "1" : "0") << "\n";
  }
}

void runVst3MainLoop() {
#if defined(__APPLE__)
  Vst3Plugin::runMainLoop();
#endif
}

void stopVst3MainLoop() {
#if defined(__APPLE__)
  Vst3Plugin::stopMainLoop();
#endif
}

void sendInitialConfig(const ProjectState& state) {
  pb::commands::HibikiConfig config;
  config.set_plugin_host_mode(
      (state.plugin_host_mode == PluginHostMode::LOCAL_SANDBOX)
          ? pb::commands::PLUGIN_HOST_LOCAL_SANDBOX
          : pb::commands::PLUGIN_HOST_IN_PROCESS);
  for (const auto& host : state.remote_hosts) {
    config.add_remote_hosts(host);
  }
  config.set_buffer_latency_ms(state.buffer_latency_ms);
  pb::notifications::Notification notif;
  *notif.mutable_config() = config;
  std::string data;
  notif.SerializeToString(&data);
  sendNotification(reinterpret_cast<const uint8_t*>(data.data()), data.size());
}

void sendNotificationTick(ProjectState& state) {
  int cpu_pct = 0;
  std::vector<float> osc_copy;
  {
    std::lock_guard<std::mutex> llock(state.levels_mutex);
    cpu_pct = state.cpu_percent;
    osc_copy = state.oscilloscope;
  }
  hibiki::sendPlayheadInfo((float)state.playhead_pos_sec, (float)state.bpm,
                           state.is_timeline_playing, state.loop_enabled,
                           (float)state.loop_start_sec,
                           (float)state.loop_end_sec, state.is_recording,
                           cpu_pct, &osc_copy,
                           static_cast<int>(state.playback_mode.load()),
                           state.swing_amount, state.selected_pattern_index);

  hibiki::pb::notifications::Notification notification;
  auto* tl = notification.mutable_track_levels();
  {
    std::lock_guard<std::mutex> llock(state.levels_mutex);
    for (auto& pair : state.track_levels) {
      auto* level = tl->add_levels();
      level->set_track_index(pair.first);
      level->set_peak_l(pair.second.first);
      level->set_peak_r(pair.second.second);
    }
  }
  std::string data;
  notification.SerializeToString(&data);
  sendNotification(reinterpret_cast<const uint8_t*>(data.data()), data.size());

  // Send builtin plugin metering data (~30Hz)
  {
    std::lock_guard<std::mutex> lock(state.tracks_mutex);
    for (auto& pair : state.tracks) {
      auto& track = pair.second;
      int track_idx = pair.first;
      for (size_t p = 0; p < track->plugins.size(); ++p) {
        auto& plugin = track->plugins[p];
        if (!plugin) continue;
        if (auto* eq = dynamic_cast<BuiltinEq*>(plugin.get())) {
          auto spec = eq->getSpectrumData();
          sendPluginSpectrumData(track_idx, (int)p, spec.input_db,
                                 spec.output_db, BuiltinEq::kSpectrumBins);
        } else if (auto* comp =
                       dynamic_cast<BuiltinCompressor*>(plugin.get())) {
          hibiki::pb::notifications::PluginMeteringData meter;
          meter.set_track_index(track_idx);
          meter.set_plugin_index((int)p);
          meter.set_input_db(comp->getInputDb());
          meter.set_output_db(comp->getOutputDb());
          meter.set_gain_reduction_db(comp->getGainReductionDb());
          meter.set_sidechain_db(comp->getSidechainDb());
          sendPluginMeteringData(meter);
        } else if (auto* maxim = dynamic_cast<BuiltinMaxim*>(plugin.get())) {
          hibiki::pb::notifications::PluginMeteringData meter;
          meter.set_track_index(track_idx);
          meter.set_plugin_index((int)p);
          meter.set_input_db(maxim->getBandInputDb(3));
          meter.set_output_db(maxim->getBandOutputDb(3));
          meter.set_gain_reduction_db(maxim->getBandGainReductionDb(3));
          meter.set_sidechain_db(-200.0f);
          meter.set_low_gr_db(maxim->getBandGainReductionDb(0));
          meter.set_mid_gr_db(maxim->getBandGainReductionDb(1));
          meter.set_high_gr_db(maxim->getBandGainReductionDb(2));
          meter.set_master_gr_db(maxim->getBandGainReductionDb(3));
          meter.set_low_in_db(maxim->getBandInputDb(0));
          meter.set_low_out_db(maxim->getBandOutputDb(0));
          meter.set_mid_in_db(maxim->getBandInputDb(1));
          meter.set_mid_out_db(maxim->getBandOutputDb(1));
          meter.set_high_in_db(maxim->getBandInputDb(2));
          meter.set_high_out_db(maxim->getBandOutputDb(2));
          sendPluginMeteringData(meter);
        } else if (auto* hott = dynamic_cast<BuiltinHott*>(plugin.get())) {
          hibiki::pb::notifications::PluginMeteringData meter;
          meter.set_track_index(track_idx);
          meter.set_plugin_index((int)p);
          meter.set_input_db(hott->getInputDb());
          meter.set_output_db(hott->getOutputDb());
          meter.set_gain_reduction_db(hott->getBandGainReduction(0));
          meter.set_sidechain_db(-200.0f);
          sendPluginMeteringData(meter);
        } else if (auto* delay = dynamic_cast<BuiltinDelay*>(plugin.get())) {
          hibiki::pb::notifications::PluginMeteringData meter;
          meter.set_track_index(track_idx);
          meter.set_plugin_index((int)p);
          meter.set_input_db(delay->getInputDb());
          meter.set_output_db(delay->getOutputDb());
          meter.set_gain_reduction_db(0.0f);
          meter.set_sidechain_db(-200.0f);
          sendPluginMeteringData(meter);
        } else if (auto* reverb = dynamic_cast<BuiltinReverb*>(plugin.get())) {
          hibiki::pb::notifications::PluginMeteringData meter;
          meter.set_track_index(track_idx);
          meter.set_plugin_index((int)p);
          meter.set_input_db(reverb->getInputDb());
          meter.set_output_db(reverb->getOutputDb());
          meter.set_gain_reduction_db(0.0f);
          meter.set_sidechain_db(-200.0f);
          sendPluginMeteringData(meter);
        } else if (auto* envs =
                       dynamic_cast<BuiltinEnvelopeShaper*>(plugin.get())) {
          hibiki::pb::notifications::PluginMeteringData meter;
          meter.set_track_index(track_idx);
          meter.set_plugin_index((int)p);
          meter.set_input_db(envs->getInputDb());
          meter.set_output_db(envs->getOutputDb());
          meter.set_gain_reduction_db(0.0f);
          meter.set_sidechain_db(-200.0f);
          sendPluginMeteringData(meter);
        } else if (auto* phaser = dynamic_cast<BuiltinPhaser*>(plugin.get())) {
          hibiki::pb::notifications::PluginMeteringData meter;
          meter.set_track_index(track_idx);
          meter.set_plugin_index((int)p);
          meter.set_input_db(phaser->getInputDb());
          meter.set_output_db(phaser->getOutputDb());
          meter.set_gain_reduction_db(0.0f);
          meter.set_sidechain_db(-200.0f);
          sendPluginMeteringData(meter);
          // Send scope data for Lissajous visualization
          {
            hibiki::pb::notifications::Notification notif;
            auto* scope = notif.mutable_plugin_scope_data();
            scope->set_track_index(track_idx);
            scope->set_plugin_index((int)p);
            float scope_l[256], scope_r[256];
            phaser->getScopeData(scope_l, scope_r, 256);
            for (int si = 0; si < 256; ++si) {
              scope->add_left_samples(scope_l[si]);
              scope->add_right_samples(scope_r[si]);
            }
            std::string data;
            notif.SerializeToString(&data);
            sendNotification(reinterpret_cast<const uint8_t*>(data.data()),
                             data.size());
          }
        }
      }
    }
  }

  // Live waveform updates during recording (~5Hz)
  static int wf_counter = 0;
  if (state.is_recording && ++wf_counter >= 6) {
    wf_counter = 0;
    std::lock_guard<std::mutex> lock(state.tracks_mutex);
    for (auto& pair : state.tracks) {
      auto& track = pair.second;
      if (!track->record_armed || track->record_buffer.empty()) continue;
      int rec_ch = track->input_stereo ? 2 : 1;
      int total_samples = (int)track->record_buffer.size();
      double duration_sec =
          (double)total_samples / (rec_ch * state.sample_rate);

      int summary_size = 200;
      std::vector<float> waveform(summary_size, 0.0f);
      d_compute_waveform_summary(track->record_buffer.data(), total_samples,
                                 rec_ch, waveform.data(), summary_size);

      int live_clip_idx = (int)track->timeline_clips.size();
      sendTimelineClipInfo(pair.first, live_clip_idx, "Recording...", "",
                           (float)state.record_start_sec, (float)duration_sec,
                           waveform);
    }
  }
}

void playback_thread(ProjectState& state) {
  auto audio = SoundDevice::create(44100, 2, state.buffer_latency_ms);
  CHECK(audio != nullptr) << "Failed to create SoundDevice";
  float sample_rate = (float)audio->get_sample_rate();
  int actual_channels = audio->get_channels();
  CHECK_GT(sample_rate, 0.0f) << "Invalid sample rate from audio device";
  CHECK_GT(actual_channels, 0) << "Invalid channel count from audio device";
  state.sample_rate = (double)sample_rate;
  if (!audio->is_ready()) return;

  int block_size = 512;
  CHECK_GT(block_size, 0);
  CHECK_LE(block_size, 4096) << "Block size unreasonably large";

  auto ws = std::make_unique<RenderProjectWorkspace>();
  ws->reserve(block_size);

  HostProcessContext context;
  context.sampleRate = sample_rate;
  context.timeSigNumerator = 4;
  context.timeSigDenominator = 4;
  context.continuousTimeSamples = 0;
  context.projectTimeMusic = 0;
  context.tempo = state.bpm;
  CHECK(!std::isnan(state.bpm)) << "BPM must be initialized before playback";
  double time_per_block = block_size / (double)sample_rate;
  CHECK_GT(time_per_block, 0.0);

  std::vector<float> interleaved(block_size * actual_channels, 0.0f);

  while (!state.quit) {
    const auto block_start_time = std::chrono::steady_clock::now();

    const double cur_bpm = state.bpm.load(std::memory_order_relaxed);
    const double cur_playhead =
        state.playhead_pos_sec.load(std::memory_order_relaxed);
    const bool cur_playing =
        state.is_timeline_playing.load(std::memory_order_relaxed);
    const bool cur_recording =
        state.is_recording.load(std::memory_order_relaxed);
    context.tempo = cur_bpm;
    bool track0_is_master_or_absent = false;
    bool pat_loop_active = false;
    double pat_end_sec = 0.0;

    {
      std::lock_guard<std::mutex> lock(state.tracks_mutex);

      RenderBlockOptions opts;
      opts.capture_live_inputs = true;
      opts.update_meters = true;
      opts.render_metronome = true;
      opts.cur_bpm = cur_bpm;
      opts.cur_playhead_sec = cur_playhead;
      opts.sample_rate = sample_rate;
      opts.cur_playing = cur_playing;
      opts.cur_recording = cur_recording;
      RenderProjectAudioBlock(state, context, *ws, block_size, opts);
      auto it0 = state.tracks.find(0);
      track0_is_master_or_absent =
          (it0 == state.tracks.end() ||
           it0->second->track_type == Track::TrackType::MASTER);

      const bool loop_en = state.loop_enabled.load(std::memory_order_relaxed);
      pat_loop_active = (!loop_en &&
                         state.playback_mode.load(std::memory_order_relaxed) ==
                             SequencerPlaybackMode::PAT &&
                         !state.sequencer_patterns.empty() && cur_bpm > 0.0);
      if (pat_loop_active) {
        int psteps = 16;
        const int sel_pat =
            state.selected_pattern_index.load(std::memory_order_relaxed);
        for (const auto& pat : state.sequencer_patterns) {
          if (pat.pattern_index == sel_pat) {
            psteps = pat.length_steps > 0 ? pat.length_steps : 16;
            break;
          }
        }
        pat_end_sec = (psteps * 0.25) * (60.0 / cur_bpm);
      }
    }

    // Mix to output (via @nogc D DSP kernel) using pre-allocated buffer
    std::fill(interleaved.begin(), interleaved.end(), 0.0f);
    float masterPeakL = 0.0f, masterPeakR = 0.0f;
    d_process_master_output(ws->mixBufferL.data(), ws->mixBufferR.data(),
                            interleaved.data(), block_size, &masterPeakL,
                            &masterPeakR);

    // Measure DSP CPU% and capture 56-point oscilloscope snapshot
    const auto dsp_end_time = std::chrono::steady_clock::now();
    const double elapsed_sec =
        std::chrono::duration<double>(dsp_end_time - block_start_time).count();
    const int inst_cpu = std::clamp(
        (int)std::lround((elapsed_sec / time_per_block) * 100.0), 0, 100);
    {
      std::lock_guard<std::mutex> llock(state.levels_mutex);
      state.cpu_percent = (state.cpu_percent * 3 + inst_cpu) / 4;
      constexpr int kOscPoints = 56;
      state.oscilloscope.resize(kOscPoints);
      const int step = std::max(1, block_size / kOscPoints);
      for (int i = 0; i < kOscPoints; ++i) {
        const int idx = std::min(block_size - 1, i * step);
        state.oscilloscope[i] =
            0.5f * (float)(ws->mixBufferL[idx] + ws->mixBufferR[idx]);
      }
      if (track0_is_master_or_absent) {
        state.track_levels[0] = {masterPeakL, masterPeakR};
      }
    }

    CHECK_GE((int)interleaved.size(), block_size * actual_channels);
    audio->write(interleaved, block_size);

    double updated_pos = cur_playhead;
    if (cur_playing) {
      const bool loop_en = state.loop_enabled.load(std::memory_order_relaxed);
      double next_pos = cur_playhead;
      d_advance_playhead(
          cur_playhead, time_per_block, loop_en || pat_loop_active,
          loop_en ? state.loop_start_sec.load(std::memory_order_relaxed) : 0.0,
          loop_en ? state.loop_end_sec.load(std::memory_order_relaxed)
                  : pat_end_sec,
          &next_pos);
      state.playhead_pos_sec.store(next_pos, std::memory_order_relaxed);
      updated_pos = next_pos;
    }
    context.continuousTimeSamples = (int64_t)(updated_pos * context.sampleRate);
    context.projectTimeMusic = updated_pos * (context.tempo / 60.0);

    if (state.audio_restart_requested.exchange(false)) {
      auto new_audio = SoundDevice::create(
          (int)sample_rate, actual_channels,
          state.buffer_latency_ms.load(std::memory_order_relaxed));
      if (new_audio && new_audio->is_ready()) {
        audio = std::move(new_audio);
        sample_rate = (float)audio->get_sample_rate();
        actual_channels = audio->get_channels();
        state.sample_rate.store((double)sample_rate, std::memory_order_relaxed);
        context.sampleRate = sample_rate;
        time_per_block = block_size / (double)sample_rate;
        interleaved.resize(block_size * actual_channels, 0.0f);
      }
    }
  }
}

}  // namespace hibiki
