#include "engine/core/project.hpp"

#include <filesystem>
#include <fstream>
#include <set>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "engine/core/audio_file.hpp"
#include "engine/core/math.hpp"
#include "engine/core/midi.hpp"
#include "engine/instruments/builtin_drum_machine.hpp"
#include "engine/ipc/ipc.hpp"
#include "pb/core.pb.h"
#include "pb/notifications.pb.h"

extern void d_apply_track_gain_and_pan(double* bufferL, double* bufferR,
                                       int blockSize, float volume, float pan,
                                       bool audible, float* outPeakL,
                                       float* outPeakR);
extern void d_accumulate_mix(const double* trackL, const double* trackR,
                             double* masterL, double* masterR, int blockSize);
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

extern "C" {
void d_apply_track_eq_and_stereo_sep(double* bufL, double* bufR, int blockSize,
                                     double sampleRate, float eqLow,
                                     float eqMid, float eqHigh, float stereoSep,
                                     double* eqState24);
void d_blend_dry_wet(const double* dryL, const double* dryR, double* wetL,
                     double* wetR, int blockSize, float mix);
void d_apply_track_eq_and_stereo_sep_f32(float* bufL, float* bufR,
                                         int blockSize, double sampleRate,
                                         float eqLow, float eqMid, float eqHigh,
                                         float stereoSep, double* eqState24);
void d_blend_dry_wet_f32(const float* dryL, const float* dryR, float* wetL,
                         float* wetR, int blockSize, float mix);
}

namespace hibiki {

double beatsToSec(double beats, double bpm) {
  CHECK_GT(bpm, 0);
  return beats * 60.0 / bpm;
}

Track* GetOrCreateTrack(ProjectState& state, int track_index) {
  if (state.tracks.find(track_index) == state.tracks.end()) {
    state.tracks[track_index] = std::make_unique<Track>(track_index);
  }
  return state.tracks[track_index].get();
}

static void SerializeClipMidiEvents(const Clip& clip,
                                    pb::core::Clip* proto_clip) {
  proto_clip->set_type(clip.type == Clip::Type::MIDI
                           ? pb::core::CLIP_TYPE_MIDI
                           : pb::core::CLIP_TYPE_AUDIO);
  proto_clip->set_path(clip.path);
  proto_clip->set_name(clip.name);
  proto_clip->set_is_loop(clip.is_loop);
  proto_clip->set_duration_beats(clip.duration_beats);
  proto_clip->set_duration_sec(clip.duration_sec);

  for (float val : clip.waveform_summary) {
    proto_clip->add_waveform_summary(val);
  }

  if (clip.type == Clip::Type::MIDI) {
    const double ppq = 480.0;
    proto_clip->set_resolution(ppq);

    for (size_t i = 0; i < clip.midi_events.size(); ++i) {
      const auto& ev = clip.midi_events[i];
      if (isNoteOn(ev)) {
        double duration = 0.1;
        for (size_t j = i + 1; j < clip.midi_events.size(); ++j) {
          const auto& off_ev = clip.midi_events[j];
          if (off_ev.note == ev.note && off_ev.channel == ev.channel &&
              isNoteOff(off_ev)) {
            duration = off_ev.beats - ev.beats;
            break;
          }
        }
        auto* pme = proto_clip->add_midi_events();
        pme->set_tick(std::round(ev.beats * ppq));
        pme->set_pitch(ev.note);
        pme->set_duration_ticks(std::round(duration * ppq));
        pme->set_velocity(ev.velocity);
      }
    }
  }
}

// Helper: populate a Project protobuf from a ProjectState
static hibiki::pb::core::Project BuildProjectProto(const ProjectState& state) {
  hibiki::pb::core::Project project;
  project.set_bpm(state.bpm);
  project.set_playhead_sec(state.playhead_pos_sec);

  for (const auto& [idx, track] : state.tracks) {
    auto* ts = project.add_tracks();
    ts->set_track_index(idx);
    ts->set_name(track->name);
    ts->set_record_armed(track->record_armed);
    ts->set_input_device_id(track->input_device_id);
    ts->set_input_channel_start(track->input_channel_start);
    ts->set_input_stereo(track->input_stereo);
    ts->set_track_type(
        static_cast<pb::core::TrackType>(static_cast<int>(track->track_type)));
    ts->set_output_track_index(track->output_track_index);
    for (const auto& send : track->aux_sends) {
      auto* as = ts->add_aux_sends();
      as->set_aux_track_index(send.aux_track_index);
      as->set_level(send.level);
      as->set_pre_fader(send.pre_fader);
    }
    ts->set_group_parent_index(track->group_parent_index);
    ts->set_volume(track->volume);
    ts->set_pan(track->pan);
    ts->set_muted(track->muted);
    ts->set_soloed(track->soloed);
    ts->set_eq_low(track->eq_low);
    ts->set_eq_mid(track->eq_mid);
    ts->set_eq_high(track->eq_high);
    ts->set_stereo_sep(track->stereo_sep);
    for (const auto& [bp_idx, is_bp] : track->plugin_bypass) {
      if (is_bp) ts->add_bypassed_plugins(bp_idx);
    }
    for (size_t pi = 0; pi < track->plugins.size(); ++pi) {
      ts->add_plugin_mix(track->GetPluginMix(static_cast<int>(pi)));
    }

    for (const auto& plugin : track->plugins) {
      auto* ps = ts->add_plugins();
      ps->set_path(plugin->getPath());
      ps->set_plugin_index(plugin->getPluginIndex());

      // Save binary plugin state
      std::vector<uint8_t> stateBytes;
      if (plugin->getState(stateBytes)) {
        ps->set_state(reinterpret_cast<const char*>(stateBytes.data()),
                      stateBytes.size());
      }

      int num_params = plugin->getParameterCount();
      for (int p = 0; p < num_params; ++p) {
        VstParamInfo info;
        if (plugin->getParameterInfo(p, info)) {
          double val = plugin->getParameterValue(info.id);
          if (val != info.defaultValue) {
            auto* param = ps->add_params();
            param->set_id(info.id);
            param->set_current_value(val);
          }
        }
      }

      // Save sidechain routes
      auto sc_it = track->plugin_sidechain.find((int)ps->plugin_index());
      if (sc_it != track->plugin_sidechain.end() &&
          sc_it->second.source_track_index >= 0) {
        ps->set_sidechain_track_index(sc_it->second.source_track_index);
      }

      if (auto* dm = dynamic_cast<BuiltinDrumMachine*>(plugin.get())) {
        dm->serializeState(ps->mutable_drum_machine_state());
      }
    }

    for (const auto& [slot, clip] : track->clips) {
      if (!clip) continue;
      auto* ss = ts->add_session_slots();
      ss->set_slot_index(slot);
      SerializeClipMidiEvents(*clip, ss->mutable_clip());
    }

    for (const auto& tc : track->timeline_clips) {
      if (!tc->clip) continue;
      auto* tcs = ts->add_timeline_clips();
      tcs->set_start_time_sec(tc->start_time_sec);
      tcs->set_alias_source(tc->alias_source);
      tcs->set_fade_in_sec(tc->fade_in_sec);
      tcs->set_fade_out_sec(tc->fade_out_sec);
      tcs->set_muted(tc->muted);
      tcs->set_duration_beats(tc->duration_beats);
      tcs->set_loop_interval_beats(tc->loop_interval_beats);
      if (tc->pattern_index >= 0) tcs->set_pattern_index(tc->pattern_index);

      auto* clip = tcs->mutable_clip();
      SerializeClipMidiEvents(*tc->clip, clip);
      clip->set_duration_sec(tc->duration_sec);
      clip->set_trim_start_beats(tc->trim_start_beats);
    }

    for (const auto& lane : track->automation_lanes) {
      auto* als = ts->add_automation_lanes();
      als->set_plugin_index(lane.plugin_idx);
      als->set_param_id(lane.param_id);
      for (const auto& tc : lane.clips) {
        if (!tc || !tc->clip) continue;
        auto* tcs = als->add_clips();
        auto* clip = tcs->mutable_clip();
        clip->set_type(hibiki::pb::core::CLIP_TYPE_AUTOMATION);
        clip->set_duration_beats(tc->duration_beats);
        tcs->set_start_time_sec(tc->start_time_sec);
        for (const auto& pt : tc->clip->automation_points) {
          *clip->add_automation_points() = pt;
        }
      }
    }
  }

  project.set_playback_mode(state.playback_mode == SequencerPlaybackMode::SONG
                                ? pb::core::PLAYBACK_MODE_SONG
                                : pb::core::PLAYBACK_MODE_PAT);
  project.set_swing_amount(state.swing_amount);
  project.set_selected_pattern_index(state.selected_pattern_index);
  project.set_metronome_enabled(state.metronome_enabled);
  project.set_master_volume(state.master_volume);
  project.set_master_pitch(state.master_pitch);
  project.set_loop_enabled(state.loop_enabled);
  project.set_loop_start_sec(state.loop_start_sec);
  project.set_loop_end_sec(state.loop_end_sec);
  for (const auto& ch : state.sequencer_channels) {
    auto* sc = project.add_channels();
    sc->set_channel_index(ch.channel_index);
    sc->set_name(ch.name);
    sc->set_uri(ch.uri);
    sc->set_category(ch.category);
    sc->set_color(ch.color);
    sc->set_mixer_insert(ch.mixer_insert);
    sc->set_volume(ch.volume);
    sc->set_pan(ch.pan);
    sc->set_muted(ch.muted);
    sc->set_soloed(ch.soloed);
  }
  for (const auto& pat : state.sequencer_patterns) {
    auto* sp = project.add_patterns();
    sp->set_pattern_index(pat.pattern_index);
    sp->set_name(pat.name);
    sp->set_color(pat.color);
    sp->set_length_steps(pat.length_steps);
    const int emit_steps = std::clamp(pat.length_steps, 16, 64);
    for (const auto& [ch_idx, data] : pat.channels) {
      auto* cp = sp->add_channels();
      cp->set_channel_index(ch_idx);
      const int ns = std::min(static_cast<int>(data.steps.size()), emit_steps);
      for (int s = 0; s < ns; ++s) cp->add_steps(data.steps[s]);
      for (int s = 0;
           s <
           std::min(static_cast<int>(data.step_velocities.size()), emit_steps);
           ++s) {
        cp->add_step_velocities(data.step_velocities[s]);
      }
      for (int s = 0;
           s < std::min(static_cast<int>(data.step_pitches.size()), emit_steps);
           ++s) {
        cp->add_step_pitches(data.step_pitches[s]);
      }
      for (int s = 0;
           s < std::min(static_cast<int>(data.step_pans.size()), emit_steps);
           ++s) {
        cp->add_step_pans(data.step_pans[s]);
      }
      for (int s = 0;
           s < std::min(static_cast<int>(data.step_shifts.size()), emit_steps);
           ++s) {
        cp->add_step_shifts(data.step_shifts[s]);
      }
      for (const auto& ev : data.notes) *cp->add_notes() = ev;
    }
  }
  return project;
}

// Helper: populate a ProjectState from a Project protobuf (no plugin loading)
static void LoadTracksFromProto(ProjectState& state,
                                const hibiki::pb::core::Project& project) {
  state.bpm = project.bpm();
  state.playhead_pos_sec = project.playhead_sec();
  state.tracks.clear();

  for (const auto& track_data : project.tracks()) {
    auto track = GetOrCreateTrack(state, track_data.track_index());

    if (!track_data.name().empty()) {
      track->name = track_data.name();
    }
    track->record_armed = track_data.record_armed();
    track->input_device_id = track_data.input_device_id();
    track->input_channel_start = track_data.input_channel_start();
    track->input_stereo = track_data.input_stereo();
    track->track_type = static_cast<Track::TrackType>(
        static_cast<int>(track_data.track_type()));
    track->output_track_index = track_data.output_track_index();
    track->aux_sends.clear();
    for (const auto& send_data : track_data.aux_sends()) {
      Track::AuxSend send;
      send.aux_track_index = send_data.aux_track_index();
      send.level = send_data.level();
      send.pre_fader = send_data.pre_fader();
      track->aux_sends.push_back(send);
    }
    track->group_parent_index = track_data.group_parent_index();
    if (track_data.has_volume()) {
      track->volume = track_data.volume();
    }
    if (track_data.has_pan()) {
      track->pan = track_data.pan();
    }
    track->muted = track_data.muted();
    track->soloed = track_data.soloed();
    if (track_data.has_eq_low()) track->eq_low = track_data.eq_low();
    if (track_data.has_eq_mid()) track->eq_mid = track_data.eq_mid();
    if (track_data.has_eq_high()) track->eq_high = track_data.eq_high();
    if (track_data.has_stereo_sep())
      track->stereo_sep = track_data.stereo_sep();
    std::fill(std::begin(track->eq_filter_state),
              std::end(track->eq_filter_state), 0.0);
    track->plugin_bypass.clear();
    for (int bp_idx : track_data.bypassed_plugins()) {
      track->plugin_bypass[bp_idx] = true;
    }
    track->plugin_mix.clear();
    for (int mi = 0; mi < track_data.plugin_mix_size(); ++mi) {
      track->plugin_mix[mi] =
          std::max(0.0f, std::min(1.0f, track_data.plugin_mix(mi)));
    }

    for (const auto& plugin_data : track_data.plugins()) {
      if (plugin_data.path().empty()) continue;
      auto result =
          track->LoadPlugin(plugin_data.path(), plugin_data.plugin_index(),
                            state.sample_rate, state.plugin_host_mode);
      int pidx = result.index;
      if (pidx >= 0 && pidx < static_cast<int>(track->plugins.size()) &&
          track->plugins[pidx]) {
        if (!plugin_data.state().empty()) {
          std::vector<uint8_t> state_bytes(plugin_data.state().begin(),
                                           plugin_data.state().end());
          track->plugins[pidx]->setState(state_bytes);
        }

        for (const auto& param_data : plugin_data.params()) {
          track->plugins[pidx]->setParameterValue(param_data.id(),
                                                  param_data.current_value());
        }
        // Restore sidechain route
        if (plugin_data.has_sidechain_track_index()) {
          track->plugin_sidechain[pidx] = {plugin_data.sidechain_track_index()};
        }

        if (plugin_data.has_drum_machine_state()) {
          if (auto* dm = dynamic_cast<BuiltinDrumMachine*>(
                  track->plugins[pidx].get())) {
            dm->deserializeState(plugin_data.drum_machine_state());
          }
        }
      }
    }

    for (const auto& slot_data : track_data.session_slots()) {
      if (!slot_data.has_clip()) continue;
      const auto& clip_data = slot_data.clip();
      std::unique_ptr<Clip> clip;
      if (clip_data.type() == pb::core::CLIP_TYPE_MIDI) {
        if (clip_data.path().empty()) {
          clip = std::make_unique<Clip>();
          clip->type = Clip::Type::MIDI;
          clip->path = "";
          clip->duration_beats = clip_data.duration_beats();
          clip->duration_sec = 0.0;
        } else {
          auto result = hibiki::LoadClip(clip_data.path());
          if (result.ok()) {
            clip = std::make_unique<Clip>(std::move(*result));
          } else {
            clip = std::make_unique<Clip>();
            clip->type = Clip::Type::MIDI;
            clip->path = clip_data.path();
            clip->duration_beats = clip_data.duration_beats();
            clip->duration_sec = 0.0;
          }
        }

        // Restore midi events
        clip->midi_events.clear();
        const double ppq = 480.0;
        for (const auto& ev : clip_data.midi_events()) {
          double startBeats = (double)ev.tick() / ppq;
          double endBeats = (double)(ev.tick() + ev.duration_ticks()) / ppq;
          MidiEvent noteOn;
          noteOn.beats = startBeats;
          noteOn.type = 0x90;
          noteOn.channel = 0;
          noteOn.note = (uint8_t)ev.pitch();
          noteOn.velocity = (uint8_t)ev.velocity();
          clip->midi_events.push_back(noteOn);

          MidiEvent noteOff;
          noteOff.beats = endBeats;
          noteOff.type = 0x80;
          noteOff.channel = 0;
          noteOff.note = (uint8_t)ev.pitch();
          noteOff.velocity = 0;
          clip->midi_events.push_back(noteOff);
        }
        std::sort(clip->midi_events.begin(), clip->midi_events.end(),
                  [](const MidiEvent& a, const MidiEvent& b) {
                    return a.beats < b.beats;
                  });
      } else {
        // AUDIO clip
        auto result = hibiki::LoadClip(clip_data.path(), clip_data.is_loop());
        if (result.ok()) {
          clip = std::make_unique<Clip>(std::move(*result));
        }
      }

      if (clip) {
        clip->name = clip_data.name();
        if (clip->name.empty() && !clip->path.empty()) {
          std::string basename = clip->path;
          size_t pos = basename.find_last_of("/\\");
          if (pos != std::string::npos) basename = basename.substr(pos + 1);
          clip->name = basename;
        }
        clip->is_loop = clip_data.is_loop();
        if (clip_data.waveform_summary_size() > 0) {
          clip->waveform_summary.clear();
          for (float val : clip_data.waveform_summary()) {
            clip->waveform_summary.push_back(val);
          }
        }
        track->clips[slot_data.slot_index()] = std::move(clip);
      }
    }

    for (const auto& tc_data : track_data.timeline_clips()) {
      if (!tc_data.has_clip()) continue;
      const auto& clip_data = tc_data.clip();
      auto tc = std::make_unique<TimelineClip>();

      std::unique_ptr<Clip> clip;
      if (clip_data.type() == pb::core::CLIP_TYPE_MIDI) {
        if (clip_data.path().empty()) {
          clip = std::make_unique<Clip>();
          clip->type = Clip::Type::MIDI;
          clip->path = "";
          clip->duration_beats = clip_data.duration_beats();
          clip->duration_sec = 0.0;
        } else {
          auto result = hibiki::LoadClip(clip_data.path());
          if (result.ok()) {
            clip = std::make_unique<Clip>(std::move(*result));
          } else {
            clip = std::make_unique<Clip>();
            clip->type = Clip::Type::MIDI;
            clip->path = clip_data.path();
            clip->duration_beats = clip_data.duration_beats();
            clip->duration_sec = 0.0;
          }
        }

        // Restore midi events
        clip->midi_events.clear();
        const double ppq = 480.0;
        for (const auto& ev : clip_data.midi_events()) {
          double startBeats = (double)ev.tick() / ppq;
          double endBeats = (double)(ev.tick() + ev.duration_ticks()) / ppq;
          MidiEvent noteOn;
          noteOn.beats = startBeats;
          noteOn.type = 0x90;
          noteOn.channel = 0;
          noteOn.note = (uint8_t)ev.pitch();
          noteOn.velocity = (uint8_t)ev.velocity();
          clip->midi_events.push_back(noteOn);

          MidiEvent noteOff;
          noteOff.beats = endBeats;
          noteOff.type = 0x80;
          noteOff.channel = 0;
          noteOff.note = (uint8_t)ev.pitch();
          noteOff.velocity = 0;
          clip->midi_events.push_back(noteOff);
        }
        std::sort(clip->midi_events.begin(), clip->midi_events.end(),
                  [](const MidiEvent& a, const MidiEvent& b) {
                    return a.beats < b.beats;
                  });
      } else {
        // AUDIO clip
        auto result = hibiki::LoadClip(clip_data.path());
        if (result.ok()) {
          clip = std::make_unique<Clip>(std::move(*result));
        }
      }

      if (clip) {
        clip->name = clip_data.name();
        if (clip->name.empty() && !clip->path.empty()) {
          std::string basename = clip->path;
          size_t pos = basename.find_last_of("/\\");
          if (pos != std::string::npos) basename = basename.substr(pos + 1);
          clip->name = basename;
        }
        clip->is_loop = clip_data.is_loop();
        if (clip_data.waveform_summary_size() > 0) {
          clip->waveform_summary.clear();
          for (float val : clip_data.waveform_summary()) {
            clip->waveform_summary.push_back(val);
          }
        }
        tc->clip = std::move(clip);
      }

      tc->start_time_sec = tc_data.start_time_sec();
      tc->duration_sec =
          tc->clip ? tc->clip->duration_sec : tc_data.clip().duration_sec();
      tc->duration_beats = tc_data.duration_beats() > 0
                               ? tc_data.duration_beats()
                               : (tc->clip ? tc->clip->duration_beats : 0.0);
      tc->loop_interval_beats = tc_data.loop_interval_beats();
      tc->alias_source = tc_data.alias_source();
      tc->trim_start_beats = tc_data.clip().trim_start_beats();
      if (tc_data.clip().is_loop() && tc->clip) {
        tc->clip->is_loop = true;
        if (tc->loop_interval_beats <= 0.0) {
          tc->loop_interval_beats = tc_data.clip().duration_beats();
        }
      }
      tc->fade_in_sec = tc_data.fade_in_sec();
      tc->fade_out_sec = tc_data.fade_out_sec();
      tc->muted = tc_data.muted();
      tc->pattern_index =
          tc_data.has_pattern_index() ? tc_data.pattern_index() : -1;
      track->timeline_clips.push_back(std::move(tc));
    }

    for (const auto& lane_data : track_data.automation_lanes()) {
      AutomationLane lane;
      lane.plugin_idx = lane_data.plugin_index();
      lane.param_id = lane_data.param_id();
      for (const auto& tc_data : lane_data.clips()) {
        if (!tc_data.has_clip()) continue;
        auto tc = std::make_unique<TimelineClip>();
        tc->clip = std::make_unique<Clip>();
        tc->clip->type = Clip::Type::AUTOMATION;
        for (const auto& pt : tc_data.clip().automation_points()) {
          tc->clip->automation_points.push_back(pt);
        }
        tc->start_time_sec = tc_data.start_time_sec();
        tc->duration_beats = tc_data.clip().duration_beats();
        lane.clips.push_back(std::move(tc));
      }
      track->automation_lanes.push_back(std::move(lane));
    }
  }

  state.playback_mode =
      (project.playback_mode() == pb::core::PLAYBACK_MODE_SONG)
          ? SequencerPlaybackMode::SONG
          : SequencerPlaybackMode::PAT;
  state.swing_amount = project.swing_amount();
  state.selected_pattern_index = project.selected_pattern_index();
  state.metronome_enabled = project.metronome_enabled();
  state.master_volume =
      project.has_master_volume() ? project.master_volume() : 0.82f;
  state.master_pitch = project.master_pitch();
  state.loop_enabled = project.loop_enabled();
  state.loop_start_sec = project.loop_start_sec();
  state.loop_end_sec = project.loop_end_sec();
  state.sequencer_channels.clear();
  for (const auto& sc : project.channels()) {
    SequencerChannel ch;
    ch.channel_index = static_cast<int>(sc.channel_index());
    ch.name = sc.name();
    ch.uri = sc.uri();
    ch.category = sc.category();
    ch.color = sc.color();
    ch.mixer_insert = sc.mixer_insert();
    ch.volume = sc.volume();
    ch.pan = sc.pan();
    ch.muted = sc.muted();
    ch.soloed = sc.soloed();
    state.sequencer_channels.push_back(std::move(ch));
  }
  state.sequencer_patterns.clear();
  for (const auto& sp : project.patterns()) {
    SequencerPattern pat;
    pat.pattern_index = static_cast<int>(sp.pattern_index());
    pat.name = sp.name();
    pat.color = sp.color();
    pat.length_steps = sp.length_steps() > 0 ? sp.length_steps() : 16;
    for (const auto& cp : sp.channels()) {
      ChannelPatternStepData data;
      for (int s = 0;
           s < ChannelPatternStepData::kMaxSteps && s < cp.steps_size(); ++s)
        data.steps[s] = cp.steps(s);
      for (int s = 0; s < ChannelPatternStepData::kMaxSteps &&
                      s < cp.step_velocities_size();
           ++s)
        data.step_velocities[s] = cp.step_velocities(s);
      for (int s = 0;
           s < ChannelPatternStepData::kMaxSteps && s < cp.step_pitches_size();
           ++s)
        data.step_pitches[s] = cp.step_pitches(s);
      for (int s = 0;
           s < ChannelPatternStepData::kMaxSteps && s < cp.step_pans_size();
           ++s)
        data.step_pans[s] = cp.step_pans(s);
      for (int s = 0;
           s < ChannelPatternStepData::kMaxSteps && s < cp.step_shifts_size();
           ++s)
        data.step_shifts[s] = cp.step_shifts(s);
      for (const auto& ev : cp.notes()) data.notes.push_back(ev);
      pat.channels[static_cast<int>(cp.channel_index())] = std::move(data);
    }
    state.sequencer_patterns.push_back(std::move(pat));
  }
}

absl::Status SaveProject(const ProjectState& state, const std::string& path) {
  hibiki::pb::core::Project project = BuildProjectProto(state);

  std::ofstream out(path, std::ios::binary);
  if (!out) {
    return absl::PermissionDeniedError(
        absl::StrCat("Failed to open project file for writing: ", path));
  }
  if (!project.SerializeToOstream(&out)) {
    return absl::InternalError(
        absl::StrCat("Failed to serialize project to: ", path));
  }
  return absl::OkStatus();
}

absl::Status LoadProject(ProjectState& state, const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    return absl::NotFoundError(
        absl::StrCat("Failed to open project file for reading: ", path));
  }

  hibiki::pb::core::Project project;
  if (!project.ParseFromIstream(&in)) {
    return absl::DataLossError(
        absl::StrCat("Failed to parse project file: ", path));
  }

  LoadTracksFromProto(state, project);
  return absl::OkStatus();
}

std::vector<uint8_t> CaptureProjectState(const ProjectState& state) {
  hibiki::pb::core::Project project = BuildProjectProto(state);
  std::string data;
  project.SerializeToString(&data);
  return std::vector<uint8_t>(data.begin(), data.end());
}

absl::Status ApplyProjectState(ProjectState& state,
                               const std::vector<uint8_t>& data) {
  if (data.empty())
    return absl::InvalidArgumentError("Empty project state data");
  hibiki::pb::core::Project project;
  if (!project.ParseFromArray(data.data(), data.size()))
    return absl::DataLossError("Failed to parse project state from snapshot");
  LoadTracksFromProto(state, project);
  return absl::OkStatus();
}

void SyncProjectToGui(const ProjectState& state) {
  hibiki::sendClearProject();
  hibiki::sendPlayheadInfo((float)state.playhead_pos_sec, (float)state.bpm,
                           state.is_timeline_playing, state.loop_enabled,
                           (float)state.loop_start_sec,
                           (float)state.loop_end_sec, state.is_recording,
                           state.cpu_percent, nullptr,
                           static_cast<int>(state.playback_mode.load()),
                           state.swing_amount, state.selected_pattern_index);

  if (!state.sequencer_channels.empty() || !state.sequencer_patterns.empty()) {
    pb::notifications::SequencerSync sync;
    sync.set_playback_mode(state.playback_mode == SequencerPlaybackMode::SONG
                               ? pb::core::PLAYBACK_MODE_SONG
                               : pb::core::PLAYBACK_MODE_PAT);
    sync.set_swing_amount(state.swing_amount);
    sync.set_selected_pattern_index(state.selected_pattern_index);
    for (const auto& ch : state.sequencer_channels) {
      auto* sc = sync.add_channels();
      sc->set_channel_index(ch.channel_index);
      sc->set_name(ch.name);
      sc->set_uri(ch.uri);
      sc->set_category(ch.category);
      sc->set_color(ch.color);
      sc->set_mixer_insert(ch.mixer_insert);
      sc->set_volume(ch.volume);
      sc->set_pan(ch.pan);
      sc->set_muted(ch.muted);
      sc->set_soloed(ch.soloed);
    }
    for (const auto& pat : state.sequencer_patterns) {
      auto* sp = sync.add_patterns();
      sp->set_pattern_index(pat.pattern_index);
      sp->set_name(pat.name);
      sp->set_color(pat.color);
      sp->set_length_steps(pat.length_steps);
      const int emit_steps = std::clamp(pat.length_steps, 16, 64);
      for (const auto& [ch_idx, data] : pat.channels) {
        auto* cp = sp->add_channels();
        cp->set_channel_index(ch_idx);
        for (int s = 0;
             s < std::min(static_cast<int>(data.steps.size()), emit_steps); ++s)
          cp->add_steps(data.steps[s]);
        for (int s = 0;
             s < std::min(static_cast<int>(data.step_velocities.size()),
                          emit_steps);
             ++s)
          cp->add_step_velocities(data.step_velocities[s]);
        for (int s = 0; s < std::min(static_cast<int>(data.step_pitches.size()),
                                     emit_steps);
             ++s)
          cp->add_step_pitches(data.step_pitches[s]);
        for (int s = 0;
             s < std::min(static_cast<int>(data.step_pans.size()), emit_steps);
             ++s)
          cp->add_step_pans(data.step_pans[s]);
        for (int s = 0; s < std::min(static_cast<int>(data.step_shifts.size()),
                                     emit_steps);
             ++s)
          cp->add_step_shifts(data.step_shifts[s]);
        for (const auto& ev : data.notes) *cp->add_notes() = ev;
      }
    }
    hibiki::sendSequencerSync(sync);
  }

  auto emitMidiNotes = [](int tidx, int sidx, int cidx, const Clip& clip) {
    if (clip.type != Clip::Type::MIDI) return;
    constexpr int ppq = 480;
    std::vector<pb::core::MidiEvent> notes;
    std::map<int, std::pair<long, int>> active_notes;
    for (const auto& ev : clip.midi_events) {
      long tick = std::lround(ev.beats * ppq);
      if (isNoteOn(ev)) {
        active_notes[ev.note] = {tick, ev.velocity};
      } else if (isNoteOff(ev)) {
        auto it = active_notes.find(ev.note);
        if (it != active_notes.end()) {
          pb::core::MidiEvent me;
          me.set_tick(it->second.first);
          me.set_pitch(ev.note);
          me.set_duration_ticks(std::max(1L, tick - it->second.first));
          me.set_velocity(it->second.second);
          notes.push_back(me);
          active_notes.erase(it);
        }
      }
    }
    hibiki::sendClipMidiData(tidx, sidx, cidx, ppq, notes);
  };

  for (const auto& [tidx, track] : state.tracks) {
    std::vector<std::tuple<int, float, bool>> aux_tuples;
    for (const auto& send : track->aux_sends) {
      aux_tuples.emplace_back(send.aux_track_index, send.level, send.pre_fader);
    }
    hibiki::sendTrackInfoFull(
        tidx, track->name, static_cast<int>(track->track_type),
        track->output_track_index, aux_tuples, track->group_parent_index,
        track->volume, track->pan, true, track->muted, track->soloed,
        track->eq_low, track->eq_mid, track->eq_high, track->stereo_sep);

    // Sync Session Clips
    for (const auto& [sidx, clip] : track->clips) {
      if (!clip) continue;
      std::string cname = clip->name.empty() ? clip->path : clip->name;
      size_t last_slash = cname.find_last_of("/\\");
      if (last_slash != std::string::npos) {
        cname = cname.substr(last_slash + 1);
      }
      if (cname.empty()) {
        cname = "New Clip";
      }
      hibiki::sendClipInfo(tidx, sidx, cname, clip->path);
      emitMidiNotes(tidx, sidx, -1, *clip);
    }
    // Sync Plugins + current live parameter values
    for (int pidx = 0; pidx < (int)track->plugins.size(); ++pidx) {
      auto& plugin = track->plugins[pidx];
      if (!plugin) continue;
      std::vector<VstParamInfo> params;
      for (int i = 0; i < plugin->getParameterCount(); ++i) {
        VstParamInfo info;
        if (plugin->getParameterInfo(i, info)) {
          params.push_back(info);
        }
      }
      bool bypassed = track->plugin_bypass.count(pidx) > 0;
      hibiki::sendParamList(tidx, pidx, plugin->getName(),
                            plugin->isInstrument(), params, bypassed,
                            track->GetPluginMix(pidx));
      for (const auto& info : params) {
        hibiki::sendParamValueChange(tidx, pidx, info.id,
                                     (float)plugin->getParameterValue(info.id));
      }
      if (auto* dm = dynamic_cast<BuiltinDrumMachine*>(plugin.get())) {
        dm->sendAllPadStates();
      }
    }
    // Sync Timeline Clips
    for (int tc_idx = 0; tc_idx < (int)track->timeline_clips.size(); ++tc_idx) {
      const auto& tc = track->timeline_clips[tc_idx];
      if (!tc->clip) continue;
      std::string cname =
          tc->clip->name.empty() ? tc->clip->path : tc->clip->name;
      size_t last_slash = cname.find_last_of("/\\");
      if (last_slash != std::string::npos) {
        cname = cname.substr(last_slash + 1);
      }
      if (cname.empty()) {
        cname = "New Clip";
      }
      // For MIDI clips, convert duration_beats to seconds using project BPM
      float duration_for_gui =
          (tc->duration_beats > 0)
              ? (float)(tc->duration_beats * 60.0 / state.bpm)
              : (float)tc->duration_sec;
      float li_sec = (tc->loop_interval_beats > 0)
                         ? (float)(tc->loop_interval_beats * 60.0 / state.bpm)
                         : 0.0f;
      hibiki::sendTimelineClipInfo(
          tidx, tc_idx, cname, tc->clip->path, tc->start_time_sec,
          duration_for_gui, tc->clip->waveform_summary, tc->clip->is_loop,
          tc->alias_source, li_sec, (float)tc->fade_in_sec,
          (float)tc->fade_out_sec, tc->muted, tc->pattern_index,
          tc->clip->type == Clip::Type::MIDI);
      emitMidiNotes(tidx, -1, tc_idx, *tc->clip);
    }
    // Sync Automation Lanes
    if (!track->automation_lanes.empty()) {
      hibiki::sendAutomationLanesData(tidx, track->automation_lanes,
                                      track->plugins);
    }
  }
}

void CollectPatternBlockMidiEvents(const SequencerPattern& pat, int ch_idx,
                                   float swing_amount, double loop_beats,
                                   double win_start_beats,
                                   double block_dur_beats, double bpm,
                                   float sample_rate, int block_size,
                                   bool wrap_loop,
                                   std::vector<MidiNoteEvent>& out) {
  auto it = pat.channels.find(ch_idx);
  if (it == pat.channels.end() || loop_beats <= 0.0 || bpm <= 0.0 ||
      block_dur_beats <= 0.0) {
    return;
  }
  const auto& data = it->second;
  const int max_steps = std::min((int)data.steps.size(),
                                 pat.length_steps > 0 ? pat.length_steps : 16);

  double s0 =
      wrap_loop ? std::fmod(win_start_beats, loop_beats) : win_start_beats;
  if (s0 < 0.0) s0 += loop_beats;
  const double s1 = s0 + block_dur_beats;

  auto inWindow = [&](double ev_beat, int& out_offset) -> bool {
    double rel_beats = -1.0;
    if (!wrap_loop) {
      if (ev_beat >= s0 && ev_beat < s1) {
        rel_beats = ev_beat - s0;
      }
    } else {
      double b = std::fmod(ev_beat, loop_beats);
      if (b < 0.0) b += loop_beats;
      if (s1 <= loop_beats) {
        if (b >= s0 && b < s1) rel_beats = b - s0;
      } else {
        if (b >= s0 && b < loop_beats) {
          rel_beats = b - s0;
        } else if (b >= 0.0 && b < (s1 - loop_beats)) {
          rel_beats = (loop_beats - s0) + b;
        }
      }
    }
    if (rel_beats < 0.0) return false;
    int offset = (int)std::llround((rel_beats * 60.0 / bpm) * sample_rate);
    out_offset = std::clamp(offset, 0, std::max(0, block_size - 1));
    return true;
  };

  for (int s = 0; s < max_steps; ++s) {
    if (!data.steps[s]) continue;
    const double swing_beats =
        (s % 2 == 1) ? (std::clamp(swing_amount, 0.0f, 1.0f) * 0.5 * 0.25)
                     : 0.0;
    const double shift_beats =
        (s < (int)data.step_shifts.size())
            ? (std::clamp(data.step_shifts[s], 0.0f, 1.0f) * 0.5 * 0.25)
            : 0.0;
    const double on_beat = s * 0.25 + swing_beats + shift_beats;
    double off_beat = on_beat + 0.20;
    if (wrap_loop && loop_beats > 0.0) {
      if (off_beat >= loop_beats && on_beat < loop_beats &&
          (off_beat - on_beat) >= loop_beats - 1e-4) {
        off_beat = std::max(on_beat + 1e-4, loop_beats - 1e-4);
      }
    } else if (!wrap_loop && off_beat >= loop_beats) {
      off_beat = std::max(on_beat + 0.01, loop_beats - 0.001);
    }
    const float p_norm =
        (s < (int)data.step_pitches.size()) ? data.step_pitches[s] : 0.0f;
    const int pitch = std::clamp(60 + (int)std::lround(p_norm * 12.0f), 0, 127);
    const float vel = (s < (int)data.step_velocities.size())
                          ? std::clamp(data.step_velocities[s], 0.05f, 1.0f)
                          : 0.78f;
    const float pan = (s < (int)data.step_pans.size())
                          ? std::clamp(data.step_pans[s], -1.0f, 1.0f)
                          : 0.0f;
    int off = 0;
    if (inWindow(on_beat, off)) {
      MidiNoteEvent e;
      e.sampleOffset = off;
      e.channel = 0;
      e.pitch = pitch;
      e.isNoteOn = true;
      e.velocity = vel;
      e.pan = pan;
      out.push_back(e);
    }
    if (inWindow(off_beat, off)) {
      MidiNoteEvent e;
      e.sampleOffset = off;
      e.channel = 0;
      e.pitch = pitch;
      e.isNoteOn = false;
      e.velocity = 0.0f;
      e.pan = pan;
      out.push_back(e);
    }
  }

  constexpr double kPpq = 480.0;
  for (const auto& ev : data.notes) {
    const double on_beat = ev.tick() / kPpq;
    const double dur_beats = std::max(1, (int)ev.duration_ticks()) / kPpq;
    double off_beat = on_beat + dur_beats;
    if (wrap_loop && loop_beats > 0.0) {
      // Avoid modulo wrap colliding off_beat onto on_beat in the same block.
      double on_mod = std::fmod(on_beat, loop_beats);
      if (on_mod < 0.0) on_mod += loop_beats;
      double off_mod = std::fmod(off_beat, loop_beats);
      if (off_mod < 0.0) off_mod += loop_beats;
      if (std::abs(off_mod - on_mod) < 1e-4) {
        off_beat = std::max(on_beat + 1e-4, off_beat - 1e-3);
      }
    } else if (!wrap_loop && off_beat >= loop_beats) {
      off_beat = std::max(on_beat + 0.01, loop_beats - 0.001);
    }
    int off = 0;
    if (inWindow(on_beat, off)) {
      MidiNoteEvent e;
      e.sampleOffset = off;
      e.channel = 0;
      e.pitch = std::clamp((int)ev.pitch(), 0, 127);
      e.isNoteOn = true;
      e.velocity = std::clamp(ev.velocity() / 127.0f, 0.05f, 1.0f);
      out.push_back(e);
    }
    if (inWindow(off_beat, off)) {
      MidiNoteEvent e;
      e.sampleOffset = off;
      e.channel = 0;
      e.pitch = std::clamp((int)ev.pitch(), 0, 127);
      e.isNoteOn = false;
      e.velocity = 0.0f;
      out.push_back(e);
    }
  }
}

void CollectSongPatternClipBlockEvents(const ProjectState& state, int ch_idx,
                                       double block_start_sec,
                                       double time_per_block, float sample_rate,
                                       int block_size,
                                       std::vector<MidiNoteEvent>& out) {
  if (state.bpm <= 0.0 || time_per_block <= 0.0 || block_size <= 0 ||
      state.sequencer_patterns.empty()) {
    return;
  }
  const double bps = state.bpm / 60.0;
  const double block_end_sec = block_start_sec + time_per_block;

  for (const auto& [lane_idx, lane_track] : state.tracks) {
    for (const auto& tc : lane_track->timeline_clips) {
      if (tc->muted || tc->pattern_index < 0) continue;
      const double clip_dur = (tc->duration_beats > 0)
                                  ? tc->duration_beats * 60.0 / state.bpm
                                  : tc->duration_sec;
      if (clip_dur <= 0.0) continue;
      const double clip_end_sec = tc->start_time_sec + clip_dur;
      if (block_end_sec <= tc->start_time_sec ||
          block_start_sec >= clip_end_sec) {
        continue;
      }

      for (const auto& pat : state.sequencer_patterns) {
        if (pat.pattern_index != tc->pattern_index) continue;
        const double pat_loop_beats =
            (pat.length_steps > 0 ? pat.length_steps : 16) * 0.25;
        const double ov_start_sec =
            std::max(block_start_sec, tc->start_time_sec);
        const double ov_end_sec = std::min(block_end_sec, clip_end_sec);
        if (ov_end_sec > ov_start_sec) {
          const int sample_shift = std::clamp(
              (int)std::llround((ov_start_sec - block_start_sec) * sample_rate),
              0, std::max(0, block_size - 1));
          const int sub_block = std::max(1, block_size - sample_shift);
          std::vector<MidiNoteEvent> clip_events;
          CollectPatternBlockMidiEvents(
              pat, ch_idx, state.swing_amount, pat_loop_beats,
              (ov_start_sec - tc->start_time_sec) * bps,
              (ov_end_sec - ov_start_sec) * bps, state.bpm, sample_rate,
              sub_block, true, clip_events);
          for (auto& ev : clip_events) {
            ev.sampleOffset =
                std::clamp(ev.sampleOffset + sample_shift, 0, block_size - 1);
            out.push_back(ev);
          }
        }

        // If this block crosses the end of the pattern clip, emit NoteOffs for
        // all pitches used by this channel in the pattern so long notes stop.
        if (block_start_sec < clip_end_sec && block_end_sec >= clip_end_sec) {
          auto it = pat.channels.find(ch_idx);
          if (it != pat.channels.end()) {
            const int end_offset =
                std::clamp((int)std::llround((clip_end_sec - block_start_sec) *
                                             sample_rate),
                           0, block_size - 1);
            bool active_pitches[128]{};
            const int max_steps =
                std::min((int)it->second.steps.size(),
                         pat.length_steps > 0 ? pat.length_steps : 16);
            for (int s = 0; s < max_steps; ++s) {
              if (!it->second.steps[s]) continue;
              const float p_norm = (s < (int)it->second.step_pitches.size())
                                       ? it->second.step_pitches[s]
                                       : 0.0f;
              active_pitches[std::clamp(60 + (int)std::lround(p_norm * 12.0f),
                                        0, 127)] = true;
            }
            for (const auto& nev : it->second.notes) {
              active_pitches[std::clamp((int)nev.pitch(), 0, 127)] = true;
            }
            for (int pitch = 0; pitch < 128; ++pitch) {
              if (!active_pitches[pitch]) continue;
              MidiNoteEvent off_ev;
              off_ev.sampleOffset = end_offset;
              off_ev.channel = 0;
              off_ev.pitch = pitch;
              off_ev.isNoteOn = false;
              off_ev.velocity = 0.0f;
              out.push_back(off_ev);
            }
          }
        }
      }
    }
  }
}

double GetProjectDuration(const ProjectState& state) {
  double max_duration = 0.0;
  for (const auto& [idx, track] : state.tracks) {
    for (const auto& tc : track->timeline_clips) {
      // For MIDI clips, convert duration_beats to seconds using project BPM
      double clip_duration_sec = (tc->duration_beats > 0)
                                     ? tc->duration_beats * 60.0 / state.bpm
                                     : tc->duration_sec;
      double end_time = tc->start_time_sec + clip_duration_sec;
      if (end_time > max_duration) {
        max_duration = end_time;
      }
    }
  }
  if (max_duration <= 0.0 &&
      state.playback_mode == SequencerPlaybackMode::PAT &&
      !state.sequencer_patterns.empty() && state.bpm > 0.0) {
    int psteps = 16;
    for (const auto& pat : state.sequencer_patterns) {
      if (pat.pattern_index == state.selected_pattern_index) {
        psteps = pat.length_steps > 0 ? pat.length_steps : 16;
        break;
      }
    }
    return (psteps * 0.25) * (60.0 / state.bpm);
  }
  return max_duration > 0.0 ? max_duration + 2.0 : 0.0;
}

void RenderProjectAudioBlock(ProjectState& state, HostProcessContext& context,
                             RenderProjectWorkspace& ws, int block_size,
                             const RenderBlockOptions& opts) {
  const int bs = std::min(block_size, RenderProjectWorkspace::kMaxBlockSize);
  double* bufferL = ws.bufferL.data();
  double* bufferR = ws.bufferR.data();
  double* dryBufL = ws.dryBufL.data();
  double* dryBufR = ws.dryBufR.data();
  float* plugBufL = ws.plugBufL.data();
  float* plugBufR = ws.plugBufR.data();
  float* outChannels[] = {plugBufL, plugBufR};

  std::fill_n(ws.mixBufferL.data(), bs, 0.0);
  std::fill_n(ws.mixBufferR.data(), bs, 0.0);
  std::fill_n(plugBufL, bs, 0.0f);
  std::fill_n(plugBufR, bs, 0.0f);

  const double cur_bpm = opts.cur_bpm > 0.0 ? opts.cur_bpm : 120.0;
  const double cur_playhead = opts.cur_playhead_sec;
  const float sample_rate =
      opts.sample_rate > 0.0f ? opts.sample_rate : 44100.0f;
  const double time_per_block = bs / static_cast<double>(sample_rate);
  const double bps = cur_bpm / 60.0;
  const bool cur_playing = opts.cur_playing;
  const bool cur_recording = opts.cur_recording;
  const bool use_double =
      state.use_double_precision.load(std::memory_order_relaxed);
  const int master_pitch_shift = static_cast<int>(
      std::lround(state.master_pitch.load(std::memory_order_relaxed)));

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
      std::fill_n(p.second->rt_bus_in_l.data(), bs, 0.0);
      std::fill_n(p.second->rt_bus_in_r.data(), bs, 0.0);
    }
  }

  const bool allow_timeline_clips =
      (state.playback_mode.load(std::memory_order_relaxed) !=
           SequencerPlaybackMode::PAT ||
       state.sequencer_patterns.empty());

  auto renderTrack = [&](Track* track, int track_idx, bool render_content) {
    if (track->track_type == Track::TrackType::GROUP ||
        track->track_type == Track::TrackType::AUX) {
      for (int i = 0; i < bs; ++i) {
        bufferL[i] = track->rt_bus_in_l[i];
        bufferR[i] = track->rt_bus_in_r[i];
      }
    } else {
      std::fill_n(bufferL, bs, 0.0);
      std::fill_n(bufferR, bs, 0.0);
    }

    if (render_content) {
      // 1. Session clip playback
      ws.clipMidiEvents.clear();
      if (track->playing_slot >= 0 && track->clips.count(track->playing_slot) &&
          track->clips[track->playing_slot]) {
        auto& clip = track->clips[track->playing_slot];
        if (clip->type == Clip::Type::AUDIO) {
          int start_sample =
              static_cast<int>(track->current_time_sec * sample_rate);
          d_render_session_audio_clip_block(
              clip->audio_data.data(),
              static_cast<int>(clip->audio_data.size()), clip->num_channels,
              clip->is_loop, start_sample, bufferL, bufferR, bs);
        } else if (clip->type == Clip::Type::MIDI) {
          double current_beats = track->current_time_sec * bps;
          double block_end_beats =
              (track->current_time_sec + time_per_block) * bps;
          double loop_beats =
              clip->duration_beats > 0 ? clip->duration_beats : 4.0;
          if (clip->is_loop && loop_beats > 0) {
            current_beats = std::fmod(current_beats, loop_beats);
            block_end_beats = current_beats + time_per_block * bps;
          }
          for (const auto& me : clip->midi_events) {
            if (me.beats >= current_beats && me.beats < block_end_beats) {
              MidiNoteEvent e;
              double event_sec = me.beats / bps - track->current_time_sec;
              if (clip->is_loop && loop_beats > 0) {
                event_sec = (me.beats - current_beats) / bps;
              }
              e.sampleOffset =
                  std::max(0, static_cast<int>(event_sec * sample_rate));
              if (e.sampleOffset >= bs) e.sampleOffset = bs - 1;
              e.channel = me.channel;
              e.pitch = me.note;
              e.isNoteOn = isNoteOn(me);
              e.velocity = e.isNoteOn ? me.velocity / 127.0f : 0.0f;
              ws.clipMidiEvents.push_back(e);
            }
          }
        }
        track->current_time_sec += time_per_block;
      }

      // 1b. Collect timeline MIDI & Sequencer events
      ws.timelineMidiEvents.clear();
      if (cur_playing) {
        if (allow_timeline_clips) {
          for (const auto& tc : track->timeline_clips) {
            if (tc->muted || tc->pattern_index >= 0 || !tc->clip ||
                tc->clip->type != Clip::Type::MIDI)
              continue;
            double clip_duration = (tc->duration_beats > 0)
                                       ? tc->duration_beats * 60.0 / cur_bpm
                                       : tc->duration_sec;
            if (cur_playhead + time_per_block > tc->start_time_sec &&
                cur_playhead < tc->start_time_sec + clip_duration) {
              double clip_local_time = cur_playhead - tc->start_time_sec;
              double content_dur_beats = tc->clip->duration_beats;
              double loop_length_beats =
                  (tc->loop_interval_beats > 0)
                      ? tc->loop_interval_beats
                      : (content_dur_beats - tc->trim_start_beats);
              double window_start_beats =
                  clip_local_time * bps + tc->trim_start_beats;
              double window_end_beats =
                  (clip_local_time + time_per_block) * bps +
                  tc->trim_start_beats;
              if (tc->clip->is_loop && loop_length_beats > 0) {
                window_start_beats =
                    tc->trim_start_beats +
                    std::fmod(window_start_beats - tc->trim_start_beats,
                              loop_length_beats);
                window_end_beats = window_start_beats + time_per_block * bps;
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
                double event_local_sec = me.beats / bps - clip_local_time;
                if (tc->clip->is_loop && loop_length_beats > 0) {
                  double rel_beat = me.beats - tc->trim_start_beats;
                  double loop_length_sec = loop_length_beats / bps;
                  double local_in_content =
                      std::fmod(clip_local_time, loop_length_sec);
                  event_local_sec = rel_beat / bps - local_in_content;
                  if (event_local_sec < 0) event_local_sec += loop_length_sec;
                }
                e.sampleOffset = std::max(
                    0, static_cast<int>(event_local_sec * sample_rate));
                if (e.sampleOffset >= bs) e.sampleOffset = bs - 1;
                e.channel = me.channel;
                e.pitch = me.note;
                e.isNoteOn = isNoteOn(me);
                e.velocity = e.isNoteOn ? me.velocity / 127.0f : 0.0f;
                ws.timelineMidiEvents.push_back(e);
              }
            }
          }
        }  // end if (allow_timeline_clips)

        // 1b-seq. Channel Rack PAT / SONG pattern playback (generator >= 100)
        if (track_idx >= 100 && !state.sequencer_patterns.empty()) {
          const int ch_idx = track_idx - 100;
          const auto mode = state.playback_mode.load(std::memory_order_relaxed);
          const float swing =
              state.swing_amount.load(std::memory_order_relaxed);
          const int sel_pat =
              state.selected_pattern_index.load(std::memory_order_relaxed);
          if (mode == SequencerPlaybackMode::PAT) {
            for (const auto& pat : state.sequencer_patterns) {
              if (pat.pattern_index != sel_pat) continue;
              const double pat_loop_beats =
                  (pat.length_steps > 0 ? pat.length_steps : 16) * 0.25;
              CollectPatternBlockMidiEvents(
                  pat, ch_idx, swing, pat_loop_beats, cur_playhead * bps,
                  time_per_block * bps, cur_bpm, sample_rate, bs, true,
                  ws.timelineMidiEvents);
            }
          } else {
            CollectSongPatternClipBlockEvents(state, ch_idx, cur_playhead,
                                              time_per_block, sample_rate, bs,
                                              ws.timelineMidiEvents);
          }
        }
        std::stable_sort(ws.timelineMidiEvents.begin(),
                         ws.timelineMidiEvents.end(),
                         [](const MidiNoteEvent& a, const MidiNoteEvent& b) {
                           if (a.sampleOffset != b.sampleOffset) {
                             return a.sampleOffset < b.sampleOffset;
                           }
                           return !a.isNoteOn && b.isNoteOn;
                         });
      }

      // 1c. Merge MIDI & run instrument plugin
      if (!track->plugins.empty() && track->plugins[0]->isInstrument()) {
        ws.allEvents.clear();
        ws.allEvents.insert(ws.allEvents.end(), ws.clipMidiEvents.begin(),
                            ws.clipMidiEvents.end());
        ws.allEvents.insert(ws.allEvents.end(), ws.timelineMidiEvents.begin(),
                            ws.timelineMidiEvents.end());
        if (opts.capture_live_inputs) {
          if (track->midi_input_device) {
            auto hwEvents = track->midi_input_device->read();
            ws.allEvents.insert(ws.allEvents.end(), hwEvents.begin(),
                                hwEvents.end());
          }
          {
            std::lock_guard<std::mutex> mlock(track->virtual_midi_mutex);
            ws.allEvents.insert(ws.allEvents.end(),
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
              ws.allEvents.push_back(e);
            }
          }
          if (cur_recording && track->record_armed &&
              track->record_mode == Track::RecordMode::RECORD_MIDI) {
            for (const auto& ev : ws.allEvents) {
              Track::TimestampedMidiEvent tev;
              tev.time_sec = cur_playhead + ev.sampleOffset / sample_rate;
              tev.event = ev;
              track->midi_record_buffer.push_back(tev);
            }
          }
        }
        if (master_pitch_shift != 0 &&
            dynamic_cast<BuiltinDrumMachine*>(track->plugins[0].get()) ==
                nullptr) {
          for (auto& ev : ws.allEvents) {
            ev.pitch = std::clamp(ev.pitch + master_pitch_shift, 0, 127);
          }
        }
        if (!track->plugin_bypass.count(0)) {
          track->plugins[0]->process(nullptr, outChannels, bs, context,
                                     ws.allEvents);
          for (int i = 0; i < bs; ++i) {
            bufferL[i] += plugBufL[i];
            bufferR[i] += plugBufR[i];
          }
          std::fill_n(plugBufL, bs, 0.0f);
          std::fill_n(plugBufR, bs, 0.0f);
        }
      }
    }  // end if (render_content)

    // 2. Timeline audio clip playback (Playlist Audio clips on NORMAL or GROUP
    // tracks)
    if (cur_playing && allow_timeline_clips) {
      for (const auto& tc : track->timeline_clips) {
        if (!tc->clip || tc->muted || tc->pattern_index >= 0) continue;
        if (tc->clip->type == Clip::Type::MIDI) continue;
        double clip_duration = (tc->duration_beats > 0)
                                   ? tc->duration_beats * 60.0 / cur_bpm
                                   : tc->duration_sec;
        if (cur_playhead + time_per_block > tc->start_time_sec &&
            cur_playhead < tc->start_time_sec + clip_duration) {
          double clip_local_time = cur_playhead - tc->start_time_sec;
          int content_samples = static_cast<int>(tc->clip->audio_data.size());
          if (tc->clip->num_channels == 2) content_samples /= 2;
          int trim_samples =
              (bps > 0)
                  ? static_cast<int>(tc->trim_start_beats / bps * sample_rate)
                  : 0;
          int start_sample =
              trim_samples + static_cast<int>(clip_local_time * sample_rate);
          int loop_len;
          if (tc->loop_interval_beats > 0 && bps > 0) {
            loop_len =
                static_cast<int>(tc->loop_interval_beats / bps * sample_rate);
          } else {
            loop_len = content_samples - trim_samples;
          }
          d_render_audio_clip_block(
              tc->clip->audio_data.data(),
              static_cast<int>(tc->clip->audio_data.size()),
              tc->clip->num_channels, tc->clip->is_loop, trim_samples,
              start_sample, loop_len, clip_local_time, clip_duration,
              tc->fade_in_sec, tc->fade_out_sec, sample_rate, bufferL, bufferR,
              bs);
        }
      }
    }

    if (render_content) {
      // 2a. Recording audio input capture
      if (opts.capture_live_inputs && cur_recording && track->record_armed &&
          track->input_device && track->input_device->is_ready()) {
        ws.input_block.clear();
        int input_ch = track->input_device->get_channels();
        if (track->input_device->read(ws.input_block, bs)) {
          int ch_start = track->input_channel_start;
          bool stereo = track->input_stereo;
          for (int i = 0; i < bs; ++i) {
            if (stereo) {
              int idx_l = i * input_ch + ch_start;
              int idx_r = i * input_ch + ch_start + 1;
              float l = (idx_l < static_cast<int>(ws.input_block.size()))
                            ? ws.input_block[idx_l]
                            : 0.0f;
              float r = (idx_r < static_cast<int>(ws.input_block.size()))
                            ? ws.input_block[idx_r]
                            : 0.0f;
              track->record_buffer.push_back(l);
              track->record_buffer.push_back(r);
              bufferL[i] += l;
              bufferR[i] += r;
            } else {
              int idx = i * input_ch + ch_start;
              float s = (idx < static_cast<int>(ws.input_block.size()))
                            ? ws.input_block[idx]
                            : 0.0f;
              track->record_buffer.push_back(s);
              bufferL[i] += s;
              bufferR[i] += s;
            }
          }
        }
      }
    }  // end if (render_content)

    // 3. Apply Automation
    if (cur_playing) {
      double current_beats = cur_playhead * bps;
      for (const auto& lane : track->automation_lanes) {
        if (!lane.clips.empty() && lane.plugin_idx >= 0 &&
            lane.plugin_idx < static_cast<int>(track->plugins.size())) {
          float val = GetAutomationValue(lane, current_beats, cur_bpm);
          track->plugins[lane.plugin_idx]->setParameterValue(lane.param_id,
                                                             val);
        }
      }
    }

    // 3b. Apply Modulation
    for (auto& [pidx, pmod] : track->modulations) {
      if (pidx < 0 || pidx >= static_cast<int>(track->plugins.size())) continue;
      auto* plugin = track->plugins[pidx].get();
      for (int s = 0; s < Modulator::kMaxSlots; ++s) {
        auto& mod = pmod.slots[s];
        if (!mod.assigned || mod.depth == 0.0f) continue;
        double base = plugin->getParameterValue(mod.param_id);
        float offset = mod.tick(bs, sample_rate, cur_bpm);
        double modulated =
            std::clamp(base + static_cast<double>(offset), 0.0, 1.0);
        plugin->setParameterValue(mod.param_id, modulated);
      }
    }

    // 4. Process effects (skip instrument at slot 0) with per-slot wet/dry mix
    for (size_t i = 0; i < track->plugins.size(); ++i) {
      if (i == 0 && track->plugins[i]->isInstrument()) continue;
      if (track->plugin_bypass.count(static_cast<int>(i))) continue;
      const float mix = track->GetPluginMix(static_cast<int>(i));
      if (mix <= 0.0001f) continue;
      if (mix < 0.9999f) {
        std::copy(bufferL, bufferL + bs, dryBufL);
        std::copy(bufferR, bufferR + bs, dryBufR);
      }
      float** sc_ptr = nullptr;
      float* sc_ch[2] = {nullptr, nullptr};
      auto sc_it = track->plugin_sidechain.find(static_cast<int>(i));
      if (sc_it != track->plugin_sidechain.end() &&
          sc_it->second.source_track_index >= 0) {
        auto src_it = state.tracks.find(sc_it->second.source_track_index);
        if (src_it != state.tracks.end() && src_it->second->rt_sc_valid) {
          sc_ch[0] = src_it->second->rt_sc_l.data();
          sc_ch[1] = src_it->second->rt_sc_r.data();
          sc_ptr = sc_ch;
        }
      }
      d_double_to_float_stereo(bufferL, bufferR, plugBufL, plugBufR, bs);
      track->plugins[i]->process(outChannels, outChannels, bs, context, {},
                                 sc_ptr);
      d_float_to_double_stereo(plugBufL, plugBufR, bufferL, bufferR, bs);
      if (mix < 0.9999f) {
        d_blend_dry_wet(dryBufL, dryBufR, bufferL, bufferR, bs, mix);
      }
    }

    // 4a. Strip 3-band EQ & Stereo Separation
    d_apply_track_eq_and_stereo_sep(
        bufferL, bufferR, bs, sample_rate, track->eq_low, track->eq_mid,
        track->eq_high, track->stereo_sep, track->eq_filter_state);

    // 4b. Capture post-effects output for sidechain
    d_double_to_float_stereo(bufferL, bufferR, track->rt_sc_l.data(),
                             track->rt_sc_r.data(), bs);
    track->rt_sc_valid = true;

    // 4c. Send to aux buses (pre-fader sends use pre-volume signal)
    for (const auto& send : track->aux_sends) {
      auto it = state.tracks.find(send.aux_track_index);
      if (it == state.tracks.end() ||
          it->second->track_type != Track::TrackType::AUX)
        continue;
      d_accumulate_aux_send(bufferL, bufferR, it->second->rt_bus_in_l.data(),
                            it->second->rt_bus_in_r.data(), bs, send.level);
    }

    // 5. Apply volume, pan, and mute/solo scope
    const bool scope_soloed = (track->track_type == Track::TrackType::NORMAL)
                                  ? any_normal_soloed
                              : (track->track_type == Track::TrackType::GROUP ||
                                 track->track_type == Track::TrackType::AUX)
                                  ? any_bus_soloed
                                  : false;
    bool is_silenced = track->muted || (scope_soloed && !track->soloed);
    float peakL = 0.0f, peakR = 0.0f;
    d_apply_track_gain_and_pan(bufferL, bufferR, bs, track->volume, track->pan,
                               !is_silenced, &peakL, &peakR);

    if (!use_double) {
      for (int i = 0; i < bs; ++i) {
        bufferL[i] = static_cast<float>(bufferL[i]);
        bufferR[i] = static_cast<float>(bufferR[i]);
      }
    }

    int out_idx = track->output_track_index;
    if (out_idx < 0 && track->group_parent_index >= 0) {
      out_idx = track->group_parent_index;
    }
    double* dest_l = ws.mixBufferL.data();
    double* dest_r = ws.mixBufferR.data();
    if (out_idx >= 0) {
      auto git = state.tracks.find(out_idx);
      if (git != state.tracks.end() &&
          (git->second->track_type == Track::TrackType::GROUP ||
           git->second->track_type == Track::TrackType::AUX)) {
        dest_l = git->second->rt_bus_in_l.data();
        dest_r = git->second->rt_bus_in_r.data();
      }
    }
    d_accumulate_mix(bufferL, bufferR, dest_l, dest_r, bs);

    if (opts.update_meters) {
      std::lock_guard<std::mutex> llock(state.levels_mutex);
      state.track_levels[track_idx] = {peakL, peakR};
    }
  };

  // ---- Pass 1: Render normal tracks ----
  for (auto& pair : state.tracks) {
    if (pair.second->track_type == Track::TrackType::NORMAL) {
      renderTrack(pair.second.get(), pair.first, /*render_content=*/true);
    }
  }

  // ---- Pass 2: Render group and aux tracks in topological DAG order ----
  int bus_count = 0;
  for (const auto& pair : state.tracks) {
    if (pair.second->track_type == Track::TrackType::GROUP ||
        pair.second->track_type == Track::TrackType::AUX) {
      if (bus_count < RenderProjectWorkspace::kMaxBusTracks) {
        ws.bus_indices[bus_count] = pair.first;
        ws.bus_indeg[bus_count] = 0;
        ws.bus_out[bus_count] = -1;
        ws.bus_visited[bus_count] = false;
        ++bus_count;
      }
    }
  }
  auto findBusSlot = [&](int tid) -> int {
    for (int i = 0; i < bus_count; ++i) {
      if (ws.bus_indices[i] == tid) return i;
    }
    return -1;
  };
  for (int u = 0; u < bus_count; ++u) {
    const Track* tr = state.tracks[ws.bus_indices[u]].get();
    int out_idx = tr->output_track_index >= 0 ? tr->output_track_index
                                              : tr->group_parent_index;
    int v = (out_idx >= 0) ? findBusSlot(out_idx) : -1;
    if (v == u) v = -1;
    ws.bus_out[u] = v;
    if (v >= 0) ws.bus_indeg[v]++;
    for (const auto& send : tr->aux_sends) {
      auto aux_it = state.tracks.find(send.aux_track_index);
      if (aux_it == state.tracks.end() ||
          aux_it->second->track_type != Track::TrackType::AUX)
        continue;
      int av = findBusSlot(send.aux_track_index);
      if (av >= 0 && av != u) ws.bus_indeg[av]++;
    }
  }
  int q_head = 0;
  int q_tail = 0;
  for (int u = 0; u < bus_count; ++u) {
    if (ws.bus_indeg[u] == 0) {
      ws.bus_queue[q_tail++] = u;
    }
  }
  while (q_head < q_tail) {
    int u = ws.bus_queue[q_head++];
    if (ws.bus_visited[u]) continue;
    ws.bus_visited[u] = true;
    int tid = ws.bus_indices[u];
    Track* tr = state.tracks[tid].get();
    renderTrack(tr, tid, /*render_content=*/false);
    int v = ws.bus_out[u];
    if (v >= 0 && !ws.bus_visited[v]) {
      if (--ws.bus_indeg[v] == 0) {
        ws.bus_queue[q_tail++] = v;
      }
    }
    for (const auto& send : tr->aux_sends) {
      auto aux_it = state.tracks.find(send.aux_track_index);
      if (aux_it == state.tracks.end() ||
          aux_it->second->track_type != Track::TrackType::AUX)
        continue;
      int av = findBusSlot(send.aux_track_index);
      if (av >= 0 && av != u && !ws.bus_visited[av]) {
        if (--ws.bus_indeg[av] == 0) {
          ws.bus_queue[q_tail++] = av;
        }
      }
    }
  }
  // Cycle fallback: ensure any malformed cyclic bus still renders
  for (int u = 0; u < bus_count; ++u) {
    if (!ws.bus_visited[u]) {
      ws.bus_visited[u] = true;
      int tid = ws.bus_indices[u];
      renderTrack(state.tracks[tid].get(), tid, /*render_content=*/false);
    }
  }

  // ---- Metronome click synthesis ----
  if (opts.render_metronome && cur_playing &&
      state.metronome_enabled.load(std::memory_order_relaxed) &&
      cur_bpm > 0.0 && sample_rate > 0.0f) {
    const double click_dur_beats = 0.03 * bps;
    for (int i = 0; i < bs; ++i) {
      const double beat_pos =
          (cur_playhead + static_cast<double>(i) / sample_rate) * bps;
      if (beat_pos < 0.0) continue;
      const double beat_idx_f = std::floor(beat_pos);
      const double frac = beat_pos - beat_idx_f;
      if (frac < click_dur_beats) {
        const int beat_idx = static_cast<int>(beat_idx_f);
        const bool accent = (beat_idx % 4 == 0);
        const double freq = accent ? 1480.0 : 980.0;
        const double t_sec = frac / bps;
        const double env = std::exp(-t_sec * 140.0);
        const double click = std::sin(2.0 * hibiki::pi * freq * t_sec) * env *
                             (accent ? 0.35 : 0.22);
        ws.mixBufferL[i] += click;
        ws.mixBufferR[i] += click;
      }
    }
  }

  // ---- Pass 3: Dedicated Master Bus track (if present) ----
  for (auto& pair : state.tracks) {
    Track* master_track = pair.second.get();
    if (master_track->track_type != Track::TrackType::MASTER) continue;
    std::copy_n(ws.mixBufferL.data(), bs, bufferL);
    std::copy_n(ws.mixBufferR.data(), bs, bufferR);
    for (size_t i = 0; i < master_track->plugins.size(); ++i) {
      if (i == 0 && master_track->plugins[i]->isInstrument()) continue;
      if (master_track->plugin_bypass.count(static_cast<int>(i))) continue;
      const float mix = master_track->GetPluginMix(static_cast<int>(i));
      if (mix <= 0.0001f) continue;
      if (mix < 0.9999f) {
        std::copy(bufferL, bufferL + bs, dryBufL);
        std::copy(bufferR, bufferR + bs, dryBufR);
      }
      d_double_to_float_stereo(bufferL, bufferR, plugBufL, plugBufR, bs);
      master_track->plugins[i]->process(outChannels, outChannels, bs, context,
                                        {});
      d_float_to_double_stereo(plugBufL, plugBufR, bufferL, bufferR, bs);
      if (mix < 0.9999f) {
        d_blend_dry_wet(dryBufL, dryBufR, bufferL, bufferR, bs, mix);
      }
    }
    d_apply_track_eq_and_stereo_sep(
        bufferL, bufferR, bs, sample_rate, master_track->eq_low,
        master_track->eq_mid, master_track->eq_high, master_track->stereo_sep,
        master_track->eq_filter_state);
    float pkL = 0.0f, pkR = 0.0f;
    d_apply_track_gain_and_pan(bufferL, bufferR, bs, master_track->volume,
                               master_track->pan, !master_track->muted, &pkL,
                               &pkR);
    for (int i = 0; i < bs; ++i) {
      ws.mixBufferL[i] = bufferL[i];
      ws.mixBufferR[i] = bufferR[i];
    }
  }

  const float global_master_vol =
      state.master_volume.load(std::memory_order_relaxed);
  for (int i = 0; i < bs; ++i) {
    ws.mixBufferL[i] *= global_master_vol;
    ws.mixBufferR[i] *= global_master_vol;
  }
}

void BounceProject(ProjectState& live_state, const std::string& path) {
  std::vector<uint8_t> snapshot;
  double duration = 0.0;
  double start_sec = 0.0;
  {
    std::lock_guard<std::mutex> lock(live_state.tracks_mutex);
    snapshot = CaptureProjectState(live_state);
    duration = GetProjectDuration(live_state);
  }

  // Use loop region for bounce if markers are set
  if (live_state.loop_end_sec > live_state.loop_start_sec) {
    start_sec = live_state.loop_start_sec;
    duration = live_state.loop_end_sec;
  }

  if (duration <= 0.0) {
    hibiki::sendBounceFinished(path, false);
    return;
  }

  ProjectState state;
  state.sample_rate = live_state.sample_rate.load();
  if (auto status = ApplyProjectState(state, snapshot); !status.ok()) {
    LOG(ERROR) << "Bounce ApplyProjectState failed: " << status.message();
    hibiki::sendBounceFinished(path, false);
    return;
  }
  state.is_timeline_playing = true;
  state.playhead_pos_sec = start_sec;

  const int block_size = 512;
  const float sample_rate = state.sample_rate.load();
  const int actual_channels = 2;
  std::vector<float> output_buffer;

  HostProcessContext context;
  context.sampleRate = sample_rate;
  context.tempo = state.bpm.load();
  context.timeSigNumerator = 4;
  context.timeSigDenominator = 4;

  auto ws = std::make_unique<RenderProjectWorkspace>();
  ws->reserve(block_size);

  RenderBlockOptions opts;
  opts.capture_live_inputs = false;
  opts.update_meters = false;
  opts.render_metronome = false;
  opts.cur_bpm = state.bpm.load();
  opts.sample_rate = sample_rate;
  opts.cur_playing = true;
  opts.cur_recording = false;

  const double time_per_block = block_size / static_cast<double>(sample_rate);
  while (state.playhead_pos_sec < duration) {
    opts.cur_playhead_sec = state.playhead_pos_sec.load();
    RenderProjectAudioBlock(state, context, *ws, block_size, opts);

    for (int i = 0; i < block_size; ++i) {
      output_buffer.push_back(static_cast<float>(ws->mixBufferL[i]));
      output_buffer.push_back(static_cast<float>(ws->mixBufferR[i]));
    }

    state.playhead_pos_sec.store(state.playhead_pos_sec.load() +
                                 time_per_block);
    context.continuousTimeSamples = state.playhead_pos_sec * sample_rate;
    context.projectTimeMusic = state.playhead_pos_sec * (context.tempo / 60.0);
  }

  const auto parent_dir = std::filesystem::path(path).parent_path();
  if (!parent_dir.empty()) {
    std::error_code ec;
    std::filesystem::create_directories(parent_dir, ec);
  }
  auto status = SaveWav(path, output_buffer, actual_channels, sample_rate);
  if (!status.ok()) {
    LOG(ERROR) << "Bounce save failed: " << status.message();
  }
  hibiki::sendBounceFinished(path, status.ok());
}

bool BounceTrackClip(
    const std::vector<uint8_t>& snapshot, float sample_rate, double bpm,
    int track_idx,
    const std::vector<std::unique_ptr<TimelineClip>>& timeline_clips,
    double start_sec, double end_sec, const std::string& path) {
  ProjectState state;
  state.sample_rate = sample_rate;
  auto s = ApplyProjectState(state, snapshot);
  if (!s.ok()) return false;
  state.bpm = bpm;

  Track* track = nullptr;
  if (state.tracks.find(track_idx) != state.tracks.end()) {
    track = state.tracks[track_idx].get();
  }

  state.is_timeline_playing = true;
  state.playhead_pos_sec = start_sec;

  const int block_size = 512;
  std::vector<float> output_buffer;

  HostProcessContext context;
  context.sampleRate = sample_rate;
  context.tempo = bpm;
  context.timeSigNumerator = 4;
  context.timeSigDenominator = 4;

  alignas(32) double dBufferL[512];
  alignas(32) double dBufferR[512];
  alignas(32) float bufferL[512];
  alignas(32) float bufferR[512];
  alignas(32) float dryBufL[512];
  alignas(32) float dryBufR[512];
  float* outChannels[] = {bufferL, bufferR};

  const double time_per_block = block_size / static_cast<double>(sample_rate);
  const double bps = bpm / 60.0;

  while (state.playhead_pos_sec < end_sec) {
    std::fill(dBufferL, dBufferL + block_size, 0.0);
    std::fill(dBufferR, dBufferR + block_size, 0.0);
    std::fill(bufferL, bufferL + block_size, 0.0f);
    std::fill(bufferR, bufferR + block_size, 0.0f);

    for (const auto& tc : timeline_clips) {
      if (!tc || !tc->clip || tc->muted) continue;
      double clip_duration = (tc->duration_beats > 0)
                                 ? tc->duration_beats * 60.0 / bpm
                                 : tc->duration_sec;
      if (state.playhead_pos_sec + time_per_block > tc->start_time_sec &&
          state.playhead_pos_sec < tc->start_time_sec + clip_duration) {
        double clip_local_time = state.playhead_pos_sec - tc->start_time_sec;

        if (tc->clip->type == Clip::Type::MIDI) {
          std::vector<MidiNoteEvent> blockEvents;
          double window_start_beats =
              clip_local_time * bps + tc->trim_start_beats;
          double window_end_beats =
              (clip_local_time + time_per_block) * bps + tc->trim_start_beats;
          for (const auto& me : tc->clip->midi_events) {
            if (me.beats >= window_start_beats && me.beats < window_end_beats) {
              MidiNoteEvent e;
              double event_local_sec =
                  (me.beats - tc->trim_start_beats) / bps - clip_local_time;
              e.sampleOffset =
                  std::max(0, static_cast<int>(event_local_sec * sample_rate));
              if (e.sampleOffset >= block_size) e.sampleOffset = block_size - 1;
              e.channel = me.channel;
              e.pitch = me.note;
              e.isNoteOn = hibiki::isNoteOn(me);
              e.velocity = e.isNoteOn ? me.velocity / 127.0f : 0.0f;
              blockEvents.push_back(e);
            }
          }
          if (track && !track->plugins.empty() &&
              track->plugins[0]->isInstrument() &&
              !track->plugin_bypass.count(0)) {
            track->plugins[0]->process(nullptr, outChannels, block_size,
                                       context, blockEvents);
            for (int i = 0; i < block_size; ++i) {
              dBufferL[i] += bufferL[i];
              dBufferR[i] += bufferR[i];
            }
          }
        } else {
          int content_samples = static_cast<int>(tc->clip->audio_data.size());
          if (tc->clip->num_channels == 2) content_samples /= 2;
          int trim_samples =
              (bps > 0)
                  ? static_cast<int>(tc->trim_start_beats / bps * sample_rate)
                  : 0;
          int start_sample =
              trim_samples + static_cast<int>(clip_local_time * sample_rate);
          int loop_len = (tc->loop_interval_beats > 0 && bps > 0)
                             ? static_cast<int>(tc->loop_interval_beats / bps *
                                                sample_rate)
                             : (content_samples - trim_samples);
          d_render_audio_clip_block(
              tc->clip->audio_data.data(),
              static_cast<int>(tc->clip->audio_data.size()),
              tc->clip->num_channels, tc->clip->is_loop, trim_samples,
              start_sample, loop_len, clip_local_time, clip_duration,
              tc->fade_in_sec, tc->fade_out_sec, sample_rate, dBufferL,
              dBufferR, block_size);
        }
      }
    }

    d_double_to_float_stereo(dBufferL, dBufferR, bufferL, bufferR, block_size);

    if (track) {
      double current_beats = state.playhead_pos_sec * bps;
      for (const auto& lane : track->automation_lanes) {
        if (!lane.clips.empty() && lane.plugin_idx >= 0 &&
            lane.plugin_idx < static_cast<int>(track->plugins.size())) {
          float val = GetAutomationValue(lane, current_beats, bpm);
          track->plugins[lane.plugin_idx]->setParameterValue(lane.param_id,
                                                             val);
        }
      }

      for (size_t i = 0; i < track->plugins.size(); ++i) {
        if (i == 0 && track->plugins[i]->isInstrument()) continue;
        if (track->plugin_bypass.count(static_cast<int>(i))) continue;
        const float mix = track->GetPluginMix(static_cast<int>(i));
        if (mix <= 0.0001f) continue;
        if (mix < 0.9999f) {
          std::copy(bufferL, bufferL + block_size, dryBufL);
          std::copy(bufferR, bufferR + block_size, dryBufR);
        }
        track->plugins[i]->process(outChannels, outChannels, block_size,
                                   context, {});
        if (mix < 0.9999f) {
          d_blend_dry_wet_f32(dryBufL, dryBufR, bufferL, bufferR, block_size,
                              mix);
        }
      }

      d_apply_track_eq_and_stereo_sep_f32(
          bufferL, bufferR, block_size, sample_rate, track->eq_low,
          track->eq_mid, track->eq_high, track->stereo_sep,
          track->eq_filter_state);
    }

    for (int i = 0; i < block_size; ++i) {
      output_buffer.push_back(bufferL[i]);
      output_buffer.push_back(bufferR[i]);
    }

    state.playhead_pos_sec.store(state.playhead_pos_sec.load() +
                                 time_per_block);
    context.continuousTimeSamples = state.playhead_pos_sec * sample_rate;
    context.projectTimeMusic = state.playhead_pos_sec * (bpm / 60.0);
  }

  auto status = SaveWav(path, output_buffer, 2, static_cast<int>(sample_rate));
  return status.ok();
}

void sendAutomationLanesData(
    int track_idx, const std::vector<AutomationLane>& lanes,
    const std::vector<std::unique_ptr<IPlugin>>& plugins) {
  hibiki::pb::notifications::Notification notification;
  auto* ald = notification.mutable_automation_lanes_data();
  ald->set_track_index(track_idx);
  for (int i = 0; i < (int)lanes.size(); ++i) {
    const auto& lane = lanes[i];
    auto* li = ald->add_lanes();
    li->set_track_index(track_idx);
    li->set_lane_index(i);
    li->set_plugin_index(lane.plugin_idx);
    li->set_param_id(lane.param_id);
    // Get parameter name from plugin
    std::string param_name = "param " + std::to_string(lane.param_id);
    if (lane.plugin_idx >= 0 && lane.plugin_idx < (int)plugins.size()) {
      VstParamInfo info;
      for (int pi = 0; pi < plugins[lane.plugin_idx]->getParameterCount();
           ++pi) {
        if (plugins[lane.plugin_idx]->getParameterInfo(pi, info) &&
            info.id == lane.param_id) {
          param_name = info.name;
          break;
        }
      }
    }
    li->set_param_name(param_name);
    for (const auto& tc : lane.clips) {
      if (!tc || !tc->clip) continue;
      auto* tcs = li->add_clips();
      auto* clip = tcs->mutable_clip();
      clip->set_type(hibiki::pb::core::CLIP_TYPE_AUTOMATION);
      clip->set_name(tc->clip->name);
      tcs->set_start_time_sec(tc->start_time_sec);
      clip->set_duration_beats(tc->duration_beats);
      for (const auto& pt : tc->clip->automation_points) {
        *clip->add_automation_points() = pt;
      }
    }
  }
  std::string data;
  notification.SerializeToString(&data);
  sendNotification(reinterpret_cast<const uint8_t*>(data.data()), data.size());
}

}  // namespace hibiki
