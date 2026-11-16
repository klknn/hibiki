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

  // Double-precision mixing buffers for accumulation
  alignas(32) double bufferL[512];
  alignas(32) double bufferR[512];
  alignas(32) double dryBufL[512];
  alignas(32) double dryBufR[512];
  // Float shim buffers for plugin I/O (plugins use float**)
  alignas(32) float plugBufL[512];
  alignas(32) float plugBufR[512];
  float* outChannels[] = {plugBufL, plugBufR};

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

  std::vector<double> mixBufferL(block_size, 0.0);
  std::vector<double> mixBufferR(block_size, 0.0);
  std::vector<float> interleaved(block_size * actual_channels, 0.0f);
  std::vector<MidiNoteEvent> clipMidiEvents;
  clipMidiEvents.reserve(512);
  std::vector<MidiNoteEvent> timelineMidiEvents;
  timelineMidiEvents.reserve(512);
  std::vector<MidiNoteEvent> allEvents;
  allEvents.reserve(512);
  std::vector<float> input_block;
  input_block.reserve(block_size * 8);

  while (!state.quit) {
    const auto block_start_time = std::chrono::steady_clock::now();
    std::fill(mixBufferL.begin(), mixBufferL.end(), 0.0);
    std::fill(mixBufferR.begin(), mixBufferR.end(), 0.0);

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

      bool any_normal_soloed = false;
      bool any_bus_soloed = false;
      for (const auto& sp : state.tracks) {
        if (!sp.second->soloed) continue;
        if (sp.second->track_type == Track::TrackType::NORMAL) {
          any_normal_soloed = true;
        } else if (sp.second->track_type == Track::TrackType::GROUP ||
                   sp.second->track_type == Track::TrackType::AUX) {
          any_bus_soloed = true;
        }
      }

      for (const auto& p : state.tracks) {
        p.second->rt_sc_valid = false;
        if (p.second->track_type == Track::TrackType::GROUP ||
            p.second->track_type == Track::TrackType::AUX) {
          std::fill_n(p.second->rt_bus_in_l.data(), block_size, 0.0);
          std::fill_n(p.second->rt_bus_in_r.data(), block_size, 0.0);
        }
      }

      auto renderTrack = [&](Track* track, int track_idx, bool render_content) {
        std::fill(bufferL, bufferL + block_size, 0.0);
        std::fill(bufferR, bufferR + block_size, 0.0);

        if (render_content) {
          // 1. Session clip playback
          clipMidiEvents.clear();
          if (track->playing_slot >= 0 &&
              track->clips.count(track->playing_slot) &&
              track->clips[track->playing_slot]) {
            auto& clip = track->clips[track->playing_slot];
            if (clip->type == Clip::Type::AUDIO) {
              int start_sample = (int)(track->current_time_sec * sample_rate);
              d_render_session_audio_clip_block(
                  clip->audio_data.data(), (int)clip->audio_data.size(),
                  clip->num_channels, clip->is_loop, start_sample, bufferL,
                  bufferR, block_size);
            } else if (clip->type == Clip::Type::MIDI) {
              double beats_per_sec = state.bpm / 60.0;
              double current_beats = track->current_time_sec * beats_per_sec;
              double block_end_beats =
                  (track->current_time_sec + time_per_block) * beats_per_sec;
              double loop_beats =
                  clip->duration_beats > 0 ? clip->duration_beats : 4.0;
              if (clip->is_loop && loop_beats > 0) {
                current_beats = std::fmod(current_beats, loop_beats);
                block_end_beats =
                    current_beats + time_per_block * beats_per_sec;
              }
              for (const auto& me : clip->midi_events) {
                if (me.beats >= current_beats && me.beats < block_end_beats) {
                  MidiNoteEvent e;
                  double event_sec =
                      me.beats / beats_per_sec - track->current_time_sec;
                  if (clip->is_loop && loop_beats > 0) {
                    event_sec = (me.beats - current_beats) / beats_per_sec;
                  }
                  e.sampleOffset = std::max(0, (int)(event_sec * sample_rate));
                  if (e.sampleOffset >= block_size)
                    e.sampleOffset = block_size - 1;
                  e.channel = me.channel;
                  e.pitch = me.note;
                  e.isNoteOn = isNoteOn(me);
                  e.velocity = e.isNoteOn ? me.velocity / 127.0f : 0.0f;
                  clipMidiEvents.push_back(e);
                }
              }
            }
            track->current_time_sec += time_per_block;
          }

          // 1b. Collect timeline MIDI events
          timelineMidiEvents.clear();
          if (cur_playing) {
            for (const auto& tc : track->timeline_clips) {
              if (tc->muted || tc->pattern_index >= 0 || !tc->clip ||
                  tc->clip->type != Clip::Type::MIDI)
                continue;
              double clip_duration = (tc->duration_beats > 0)
                                         ? tc->duration_beats * 60.0 / state.bpm
                                         : tc->duration_sec;
              if (state.playhead_pos_sec + time_per_block >
                      tc->start_time_sec &&
                  state.playhead_pos_sec < tc->start_time_sec + clip_duration) {
                double clip_local_time =
                    state.playhead_pos_sec - tc->start_time_sec;
                double beats_per_sec = state.bpm / 60.0;
                double content_dur_beats = tc->clip->duration_beats;
                double loop_length_beats =
                    (tc->loop_interval_beats > 0)
                        ? tc->loop_interval_beats
                        : (content_dur_beats - tc->trim_start_beats);
                double window_start_beats =
                    clip_local_time * beats_per_sec + tc->trim_start_beats;
                double window_end_beats =
                    (clip_local_time + time_per_block) * beats_per_sec +
                    tc->trim_start_beats;
                if (tc->clip->is_loop && loop_length_beats > 0) {
                  window_start_beats =
                      tc->trim_start_beats +
                      std::fmod(window_start_beats - tc->trim_start_beats,
                                loop_length_beats);
                  window_end_beats =
                      window_start_beats + time_per_block * beats_per_sec;
                }
                for (const auto& me : tc->clip->midi_events) {
                  double me_beats = me.beats;
                  if (me_beats < tc->trim_start_beats ||
                      me_beats >= content_dur_beats)
                    continue;
                  double loop_end_beats =
                      tc->trim_start_beats + loop_length_beats;
                  if (tc->clip->is_loop && loop_length_beats > 0 &&
                      window_end_beats > loop_end_beats) {
                    bool in_range =
                        (me_beats >= window_start_beats &&
                         me_beats < window_end_beats) ||
                        (me_beats >= tc->trim_start_beats &&
                         me_beats < tc->trim_start_beats +
                                        std::fmod(window_end_beats -
                                                      tc->trim_start_beats,
                                                  loop_length_beats));
                    if (!in_range) continue;
                  } else {
                    if (me_beats < window_start_beats ||
                        me_beats >= window_end_beats)
                      continue;
                  }
                  MidiNoteEvent e;
                  double event_local_sec =
                      me.beats / beats_per_sec - clip_local_time;
                  if (tc->clip->is_loop && loop_length_beats > 0) {
                    double rel_beat = me.beats - tc->trim_start_beats;
                    double loop_length_sec = loop_length_beats / beats_per_sec;
                    double local_in_content =
                        std::fmod(clip_local_time, loop_length_sec);
                    event_local_sec =
                        rel_beat / beats_per_sec - local_in_content;
                    if (event_local_sec < 0) event_local_sec += loop_length_sec;
                  }
                  e.sampleOffset =
                      std::max(0, (int)(event_local_sec * sample_rate));
                  if (e.sampleOffset >= block_size)
                    e.sampleOffset = block_size - 1;
                  e.channel = me.channel;
                  e.pitch = me.note;
                  e.isNoteOn = hibiki::isNoteOn(me);
                  e.velocity = e.isNoteOn ? me.velocity / 127.0f : 0.0f;
                  timelineMidiEvents.push_back(e);
                }
              }
            }

            // 1b-seq. Channel Rack PAT / SONG pattern playback (generator
            // tracks >= 100)
            if (track_idx >= 100 && !state.sequencer_patterns.empty()) {
              const int ch_idx = track_idx - 100;
              const double bps = state.bpm / 60.0;
              if (state.playback_mode == SequencerPlaybackMode::PAT) {
                for (const auto& pat : state.sequencer_patterns) {
                  if (pat.pattern_index != state.selected_pattern_index)
                    continue;
                  const double pat_loop_beats =
                      (pat.length_steps > 0 ? pat.length_steps : 16) * 0.25;
                  CollectPatternBlockMidiEvents(
                      pat, ch_idx, state.swing_amount, pat_loop_beats,
                      state.playhead_pos_sec * bps, time_per_block * bps,
                      state.bpm, sample_rate, block_size, true,
                      timelineMidiEvents);
                }
              } else {
                CollectSongPatternClipBlockEvents(
                    state, ch_idx, state.playhead_pos_sec, time_per_block,
                    sample_rate, block_size, timelineMidiEvents);
              }
            }
            std::stable_sort(
                timelineMidiEvents.begin(), timelineMidiEvents.end(),
                [](const MidiNoteEvent& a, const MidiNoteEvent& b) {
                  if (a.sampleOffset != b.sampleOffset) {
                    return a.sampleOffset < b.sampleOffset;
                  }
                  return !a.isNoteOn && b.isNoteOn;
                });
          }

          // 1c. Merge live MIDI + virtual MIDI + timeline MIDI
          if (!track->plugins.empty() && track->plugins[0]->isInstrument()) {
            allEvents.clear();
            allEvents.insert(allEvents.end(), clipMidiEvents.begin(),
                             clipMidiEvents.end());
            allEvents.insert(allEvents.end(), timelineMidiEvents.begin(),
                             timelineMidiEvents.end());
            if (track->midi_input_device) {
              auto hwEvents = track->midi_input_device->read();
              allEvents.insert(allEvents.end(), hwEvents.begin(),
                               hwEvents.end());
            }
            {
              std::lock_guard<std::mutex> mlock(track->virtual_midi_mutex);
              allEvents.insert(allEvents.end(),
                               track->virtual_midi_queue.begin(),
                               track->virtual_midi_queue.end());
              track->virtual_midi_queue.clear();
            }
            if (track->panic_requested_.exchange(false)) {
              for (int note = 0; note < 128; ++note) {
                MidiNoteEvent e;
                e.sampleOffset = 0;
                e.channel = 0;
                e.pitch = note;
                e.isNoteOn = false;
                e.velocity = 0.0f;
                allEvents.push_back(e);
              }
            }
            if (cur_recording && track->record_armed &&
                track->record_mode == Track::RecordMode::RECORD_MIDI) {
              for (const auto& ev : allEvents) {
                Track::TimestampedMidiEvent tev;
                tev.time_sec = cur_playhead +
                               ev.sampleOffset / state.sample_rate.load(
                                                     std::memory_order_relaxed);
                tev.event = ev;
                track->midi_record_buffer.push_back(tev);
              }
            }
            if (int pitch_shift = (int)std::lround(state.master_pitch);
                pitch_shift != 0 && dynamic_cast<BuiltinDrumMachine*>(
                                        track->plugins[0].get()) == nullptr) {
              for (auto& ev : allEvents) {
                ev.pitch = std::clamp(ev.pitch + pitch_shift, 0, 127);
              }
            }
            if (!track->plugin_bypass.count(0)) {
              track->plugins[0]->process(nullptr, outChannels, block_size,
                                         context, allEvents);
              for (int i = 0; i < block_size; ++i) {
                bufferL[i] += plugBufL[i];
                bufferR[i] += plugBufR[i];
              }
              std::fill(plugBufL, plugBufL + block_size, 0.0f);
              std::fill(plugBufR, plugBufR + block_size, 0.0f);
            }
          }

          // 2. Timeline clip playback
          if (state.is_timeline_playing) {
            for (const auto& tc : track->timeline_clips) {
              if (!tc->clip) continue;
              if (tc->muted) continue;
              double clip_duration = (tc->duration_beats > 0)
                                         ? tc->duration_beats * 60.0 / state.bpm
                                         : tc->duration_sec;
              if (state.playhead_pos_sec + time_per_block >
                      tc->start_time_sec &&
                  state.playhead_pos_sec < tc->start_time_sec + clip_duration) {
                double clip_local_time =
                    state.playhead_pos_sec - tc->start_time_sec;
                if (tc->clip->type == Clip::Type::MIDI) {
                } else {
                  int content_samples = (int)tc->clip->audio_data.size();
                  if (tc->clip->num_channels == 2) content_samples /= 2;
                  double bps = state.bpm / 60.0;
                  int trim_samples =
                      (bps > 0)
                          ? (int)(tc->trim_start_beats / bps * sample_rate)
                          : 0;
                  int start_sample =
                      trim_samples + (int)(clip_local_time * sample_rate);
                  int loop_len;
                  if (tc->loop_interval_beats > 0 && bps > 0) {
                    loop_len =
                        (int)(tc->loop_interval_beats / bps * sample_rate);
                  } else {
                    loop_len = content_samples - trim_samples;
                  }
                  d_render_audio_clip_block(
                      tc->clip->audio_data.data(),
                      static_cast<int>(tc->clip->audio_data.size()),
                      tc->clip->num_channels, tc->clip->is_loop, trim_samples,
                      start_sample, loop_len, clip_local_time, clip_duration,
                      tc->fade_in_sec, tc->fade_out_sec, sample_rate, bufferL,
                      bufferR, block_size);
                }
              }
            }
          }

          // 2a. Recording input capture
          if (cur_recording && track->record_armed && track->input_device &&
              track->input_device->is_ready()) {
            input_block.clear();
            int input_ch = track->input_device->get_channels();
            if (track->input_device->read(input_block, block_size)) {
              int ch_start = track->input_channel_start;
              bool stereo = track->input_stereo;
              for (int i = 0; i < block_size; ++i) {
                if (stereo) {
                  int idx_l = i * input_ch + ch_start;
                  int idx_r = i * input_ch + ch_start + 1;
                  float l = (idx_l < (int)input_block.size())
                                ? input_block[idx_l]
                                : 0.0f;
                  float r = (idx_r < (int)input_block.size())
                                ? input_block[idx_r]
                                : 0.0f;
                  track->record_buffer.push_back(l);
                  track->record_buffer.push_back(r);
                  bufferL[i] += l;
                  bufferR[i] += r;
                } else {
                  int idx = i * input_ch + ch_start;
                  float s =
                      (idx < (int)input_block.size()) ? input_block[idx] : 0.0f;
                  track->record_buffer.push_back(s);
                  bufferL[i] += s;
                  bufferR[i] += s;
                }
              }
            }
          }
        }  // end if (render_content)

        // For group/aux tracks: load their accumulated input from bus buffers
        if (!render_content) {
          for (int i = 0; i < block_size; ++i) {
            bufferL[i] = track->rt_bus_in_l[i];
            bufferR[i] = track->rt_bus_in_r[i];
          }
        }

        // 3. Apply Automation
        if (state.is_timeline_playing) {
          double current_beats = state.playhead_pos_sec * (state.bpm / 60.0);
          for (const auto& lane : track->automation_lanes) {
            if (!lane.clips.empty() && lane.plugin_idx >= 0 &&
                lane.plugin_idx < (int)track->plugins.size()) {
              float val = GetAutomationValue(lane, current_beats, state.bpm);
              track->plugins[lane.plugin_idx]->setParameterValue(lane.param_id,
                                                                 val);
            }
          }
        }

        // 3b. Apply Modulation
        for (auto& [pidx, pmod] : track->modulations) {
          if (pidx < 0 || pidx >= (int)track->plugins.size()) continue;
          auto* plugin = track->plugins[pidx].get();
          for (int s = 0; s < Modulator::kMaxSlots; ++s) {
            auto& mod = pmod.slots[s];
            if (!mod.assigned || mod.depth == 0.0f) continue;
            double base = plugin->getParameterValue(mod.param_id);
            float offset = mod.tick(block_size, state.sample_rate, state.bpm);
            double modulated = std::clamp(base + (double)offset, 0.0, 1.0);
            plugin->setParameterValue(mod.param_id, modulated);
          }
        }

        // 4. Process effects (skip instrument at slot 0) with per-slot wet/dry
        // mix
        for (size_t i = 0; i < track->plugins.size(); ++i) {
          if (i == 0 && track->plugins[i]->isInstrument()) continue;
          if (track->plugin_bypass.count((int)i)) continue;
          const float mix = track->GetPluginMix(static_cast<int>(i));
          if (mix <= 0.0001f) continue;
          if (mix < 0.9999f) {
            std::copy(bufferL, bufferL + block_size, dryBufL);
            std::copy(bufferR, bufferR + block_size, dryBufR);
          }
          float** sc_ptr = nullptr;
          float* sc_ch[2] = {nullptr, nullptr};
          auto sc_it = track->plugin_sidechain.find((int)i);
          if (sc_it != track->plugin_sidechain.end() &&
              sc_it->second.source_track_index >= 0) {
            auto src_it = state.tracks.find(sc_it->second.source_track_index);
            if (src_it != state.tracks.end() && src_it->second->rt_sc_valid) {
              sc_ch[0] = src_it->second->rt_sc_l.data();
              sc_ch[1] = src_it->second->rt_sc_r.data();
              sc_ptr = sc_ch;
            }
          }
          d_double_to_float_stereo(bufferL, bufferR, plugBufL, plugBufR,
                                   block_size);
          track->plugins[i]->process(outChannels, outChannels, block_size,
                                     context, {}, sc_ptr);
          d_float_to_double_stereo(plugBufL, plugBufR, bufferL, bufferR,
                                   block_size);
          if (mix < 0.9999f) {
            d_blend_dry_wet(dryBufL, dryBufR, bufferL, bufferR, block_size,
                            mix);
          }
        }

        // 4a. Strip 3-band EQ & Stereo Separation (via @nogc D DSP kernel)
        d_apply_track_eq_and_stereo_sep(
            bufferL, bufferR, block_size, state.sample_rate, track->eq_low,
            track->eq_mid, track->eq_high, track->stereo_sep,
            track->eq_filter_state);

        // 4b. Capture post-effects output for sidechain
        d_double_to_float_stereo(bufferL, bufferR, track->rt_sc_l.data(),
                                 track->rt_sc_r.data(), block_size);
        track->rt_sc_valid = true;

        // 4c. Send to aux buses (pre-fader sends use pre-volume signal)
        for (const auto& send : track->aux_sends) {
          auto it = state.tracks.find(send.aux_track_index);
          if (it == state.tracks.end() ||
              it->second->track_type != Track::TrackType::AUX)
            continue;
          d_accumulate_aux_send(
              bufferL, bufferR, it->second->rt_bus_in_l.data(),
              it->second->rt_bus_in_r.data(), block_size, send.level);
        }

        // 5. Apply volume and pan (via @nogc D DSP kernel)
        const bool scope_soloed =
            (track->track_type == Track::TrackType::NORMAL) ? any_normal_soloed
            : (track->track_type == Track::TrackType::GROUP ||
               track->track_type == Track::TrackType::AUX)
                ? any_bus_soloed
                : false;
        bool is_silenced = track->muted || (scope_soloed && !track->soloed);
        float peakL = 0.0f, peakR = 0.0f;
        d_apply_track_gain_and_pan(bufferL, bufferR, block_size, track->volume,
                                   track->pan, !is_silenced, &peakL, &peakR);

        // Determine output destination
        int out_idx = track->output_track_index;
        // Route child tracks to their group parent's bus
        if (out_idx < 0 && track->group_parent_index >= 0) {
          out_idx = track->group_parent_index;
        }
        double* dest_l = mixBufferL.data();
        double* dest_r = mixBufferR.data();
        if (out_idx >= 0) {
          auto git = state.tracks.find(out_idx);
          if (git != state.tracks.end() &&
              (git->second->track_type == Track::TrackType::GROUP ||
               git->second->track_type == Track::TrackType::AUX)) {
            dest_l = git->second->rt_bus_in_l.data();
            dest_r = git->second->rt_bus_in_r.data();
          }
        }

        d_accumulate_mix(bufferL, bufferR, dest_l, dest_r, block_size);
        {
          std::lock_guard<std::mutex> llock(state.levels_mutex);
          state.track_levels[track_idx] = {peakL, peakR};
        }
      };  // end renderTrack lambda

      // ---- Pass 1: Render normal tracks ----
      for (auto& pair : state.tracks) {
        if (pair.second->track_type == Track::TrackType::NORMAL) {
          renderTrack(pair.second.get(), pair.first, /*render_content=*/true);
        }
      }

      // ---- Pass 2: Render group and aux tracks ----
      for (auto& pair : state.tracks) {
        if (pair.second->track_type == Track::TrackType::GROUP) {
          renderTrack(pair.second.get(), pair.first, /*render_content=*/false);
        }
      }
      for (auto& pair : state.tracks) {
        if (pair.second->track_type == Track::TrackType::AUX) {
          renderTrack(pair.second.get(), pair.first, /*render_content=*/false);
        }
      }

      // ---- Metronome click synthesis ----
      if (state.metronome_enabled && state.is_timeline_playing &&
          state.bpm > 0.0 && sample_rate > 0.0f) {
        const double bps = state.bpm / 60.0;
        const double click_dur_beats = 0.03 * bps;
        for (int i = 0; i < block_size; ++i) {
          const double beat_pos =
              (state.playhead_pos_sec + (double)i / sample_rate) * bps;
          if (beat_pos < 0.0) continue;
          const double beat_idx_f = std::floor(beat_pos);
          const double frac = beat_pos - beat_idx_f;
          if (frac < click_dur_beats) {
            const int beat_idx = (int)beat_idx_f;
            const bool accent = (beat_idx % 4 == 0);
            const double freq = accent ? 1480.0 : 980.0;
            const double t_sec = frac / bps;
            const double env = std::exp(-t_sec * 140.0);
            const double click = std::sin(2.0 * hibiki::pi * freq * t_sec) *
                                 env * (accent ? 0.35 : 0.22);
            mixBufferL[i] += click;
            mixBufferR[i] += click;
          }
        }
      }

      // ---- Pass 3: Dedicated Master Bus track (if present) ----
      for (auto& pair : state.tracks) {
        Track* master_track = pair.second.get();
        if (master_track->track_type != Track::TrackType::MASTER) continue;
        std::copy(mixBufferL.begin(), mixBufferL.end(), bufferL);
        std::copy(mixBufferR.begin(), mixBufferR.end(), bufferR);
        for (size_t i = 0; i < master_track->plugins.size(); ++i) {
          if (i == 0 && master_track->plugins[i]->isInstrument()) continue;
          if (master_track->plugin_bypass.count((int)i)) continue;
          const float mix = master_track->GetPluginMix(static_cast<int>(i));
          if (mix <= 0.0001f) continue;
          if (mix < 0.9999f) {
            std::copy(bufferL, bufferL + block_size, dryBufL);
            std::copy(bufferR, bufferR + block_size, dryBufR);
          }
          d_double_to_float_stereo(bufferL, bufferR, plugBufL, plugBufR,
                                   block_size);
          master_track->plugins[i]->process(outChannels, outChannels,
                                            block_size, context, {});
          d_float_to_double_stereo(plugBufL, plugBufR, bufferL, bufferR,
                                   block_size);
          if (mix < 0.9999f) {
            d_blend_dry_wet(dryBufL, dryBufR, bufferL, bufferR, block_size,
                            mix);
          }
        }
        d_apply_track_eq_and_stereo_sep(
            bufferL, bufferR, block_size, state.sample_rate,
            master_track->eq_low, master_track->eq_mid, master_track->eq_high,
            master_track->stereo_sep, master_track->eq_filter_state);
        float pkL = 0.0f, pkR = 0.0f;
        d_apply_track_gain_and_pan(bufferL, bufferR, block_size,
                                   master_track->volume, master_track->pan,
                                   !master_track->muted, &pkL, &pkR);
        for (int i = 0; i < block_size; ++i) {
          mixBufferL[i] = bufferL[i];
          mixBufferR[i] = bufferR[i];
        }
      }
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

    // Apply Toolbar global master volume before output metering
    const float global_master_vol =
        state.master_volume.load(std::memory_order_relaxed);
    for (int i = 0; i < block_size; ++i) {
      mixBufferL[i] *= global_master_vol;
      mixBufferR[i] *= global_master_vol;
    }

    // Mix to output (via @nogc D DSP kernel) using pre-allocated buffer
    std::fill(interleaved.begin(), interleaved.end(), 0.0f);
    float masterPeakL = 0.0f, masterPeakR = 0.0f;
    d_process_master_output(mixBufferL.data(), mixBufferR.data(),
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
            0.5f * (float)(mixBufferL[idx] + mixBufferR[idx]);
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
