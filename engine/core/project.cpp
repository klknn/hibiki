#include "engine/core/project.hpp"

#include <filesystem>
#include <fstream>
#include <set>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "engine/core/audio_file.hpp"
#include "engine/core/midi.hpp"
#include "engine/instruments/builtin_drum_machine.hpp"
#include "engine/ipc/ipc.hpp"
#include "pb/core.pb.h"
#include "pb/notifications.pb.h"

extern "C" {
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
    for (const auto& [ch_idx, data] : pat.channels) {
      auto* cp = sp->add_channels();
      cp->set_channel_index(ch_idx);
      for (bool b : data.steps) cp->add_steps(b);
      for (float v : data.step_velocities) cp->add_step_velocities(v);
      for (float p : data.step_pitches) cp->add_step_pitches(p);
      for (float pa : data.step_pans) cp->add_step_pans(pa);
      for (float sh : data.step_shifts) cp->add_step_shifts(sh);
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
      for (int s = 0; s < 16 && s < cp.steps_size(); ++s)
        data.steps[s] = cp.steps(s);
      for (int s = 0; s < 16 && s < cp.step_velocities_size(); ++s)
        data.step_velocities[s] = cp.step_velocities(s);
      for (int s = 0; s < 16 && s < cp.step_pitches_size(); ++s)
        data.step_pitches[s] = cp.step_pitches(s);
      for (int s = 0; s < 16 && s < cp.step_pans_size(); ++s)
        data.step_pans[s] = cp.step_pans(s);
      for (int s = 0; s < 16 && s < cp.step_shifts_size(); ++s)
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
                           static_cast<int>(state.playback_mode),
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
      for (const auto& [ch_idx, data] : pat.channels) {
        auto* cp = sp->add_channels();
        cp->set_channel_index(ch_idx);
        for (bool b : data.steps) cp->add_steps(b);
        for (float v : data.step_velocities) cp->add_step_velocities(v);
        for (float p : data.step_pitches) cp->add_step_pitches(p);
        for (float pa : data.step_pans) cp->add_step_pans(pa);
        for (float sh : data.step_shifts) cp->add_step_shifts(sh);
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
          (float)tc->fade_out_sec, tc->muted, tc->pattern_index);
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
    int off = 0;
    if (inWindow(on_beat, off)) {
      MidiNoteEvent e;
      e.sampleOffset = off;
      e.channel = 0;
      e.pitch = pitch;
      e.isNoteOn = true;
      e.velocity = vel;
      out.push_back(e);
    }
    if (inWindow(off_beat, off)) {
      MidiNoteEvent e;
      e.sampleOffset = off;
      e.channel = 0;
      e.pitch = pitch;
      e.isNoteOn = false;
      e.velocity = 0.0f;
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
            std::set<int> pitches;
            const int max_steps =
                std::min((int)it->second.steps.size(),
                         pat.length_steps > 0 ? pat.length_steps : 16);
            for (int s = 0; s < max_steps; ++s) {
              if (!it->second.steps[s]) continue;
              const float p_norm = (s < (int)it->second.step_pitches.size())
                                       ? it->second.step_pitches[s]
                                       : 0.0f;
              pitches.insert(
                  std::clamp(60 + (int)std::lround(p_norm * 12.0f), 0, 127));
            }
            for (const auto& nev : it->second.notes) {
              pitches.insert(std::clamp((int)nev.pitch(), 0, 127));
            }
            for (int pitch : pitches) {
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
  state.sample_rate = live_state.sample_rate;
  ApplyProjectState(state, snapshot);
  state.is_timeline_playing = true;
  state.playhead_pos_sec = start_sec;

  int block_size = 512;
  float sample_rate = state.sample_rate;
  int actual_channels = 2;
  std::vector<float> output_buffer;

  HostProcessContext context;
  context.sampleRate = sample_rate;
  context.tempo = state.bpm;
  context.timeSigNumerator = 4;
  context.timeSigDenominator = 4;

  alignas(32) float bufferL[512];
  alignas(32) float bufferR[512];
  alignas(32) float dryBufL[512];
  alignas(32) float dryBufR[512];
  float* outChannels[] = {bufferL, bufferR};

  double time_per_block = block_size / (double)sample_rate;
  const double bps = state.bpm / 60.0;

  while (state.playhead_pos_sec < duration) {
    std::vector<float> mixBufferL(block_size, 0.0f);
    std::vector<float> mixBufferR(block_size, 0.0f);

    // Group bus accumulation buffers for bounce
    std::map<int, std::pair<std::vector<float>, std::vector<float>>>
        group_bus_buffers;
    for (auto& pair : state.tracks) {
      if (pair.second->track_type == Track::TrackType::GROUP) {
        group_bus_buffers[pair.first] = {std::vector<float>(block_size, 0.0f),
                                         std::vector<float>(block_size, 0.0f)};
      }
    }

    // Pass 1: Process normal tracks
    for (auto& pair : state.tracks) {
      Track* track = pair.second.get();
      if (track->track_type != Track::TrackType::NORMAL) continue;
      std::fill(bufferL, bufferL + block_size, 0.0f);
      std::fill(bufferR, bufferR + block_size, 0.0f);

      std::vector<MidiNoteEvent> combinedMidi;
      for (const auto& tc : track->timeline_clips) {
        if (tc->muted || tc->pattern_index >= 0 || !tc->clip) continue;
        // Get clip duration - use duration_beats for MIDI clips, duration_sec
        // for audio
        double clip_duration = (tc->duration_beats > 0)
                                   ? tc->duration_beats * 60.0 / state.bpm
                                   : tc->duration_sec;
        if (state.playhead_pos_sec + time_per_block > tc->start_time_sec &&
            state.playhead_pos_sec < tc->start_time_sec + clip_duration) {
          double clip_local_time = state.playhead_pos_sec - tc->start_time_sec;

          if (tc->clip->type == Clip::Type::MIDI) {
            double window_start_beats = clip_local_time * bps;
            double window_end_beats = (clip_local_time + time_per_block) * bps;
            for (const auto& me : tc->clip->midi_events) {
              if (me.beats >= window_start_beats &&
                  me.beats < window_end_beats) {
                MidiNoteEvent e;
                double event_local_sec = me.beats / bps - clip_local_time;
                e.sampleOffset =
                    std::max(0, (int)(event_local_sec * sample_rate));
                if (e.sampleOffset >= block_size)
                  e.sampleOffset = block_size - 1;
                e.channel = me.channel;
                e.pitch = me.note;
                e.isNoteOn = hibiki::isNoteOn(me);
                e.velocity = e.isNoteOn ? me.velocity / 127.0f : 0.0f;
                combinedMidi.push_back(e);
              }
            }
          } else {
            // Audio playback with loop support
            int content_samples = (int)tc->clip->audio_data.size();
            if (tc->clip->num_channels == 2) content_samples /= 2;
            int start_sample = (int)(clip_local_time * sample_rate);
            for (int i = 0; i < block_size; ++i) {
              int sample_pos = start_sample + i;
              if (sample_pos < 0) continue;
              // Loop wrapping
              if (tc->clip->is_loop && content_samples > 0) {
                sample_pos = sample_pos % content_samples;
              }
              // Compute per-sample fade gain (linear in dB domain)
              double fade_gain = 1.0;
              constexpr double kFadeMinDb = -60.0;
              double sample_time_in_clip =
                  clip_local_time + (double)i / sample_rate;
              if (tc->fade_in_sec > 0 &&
                  sample_time_in_clip < tc->fade_in_sec) {
                double t = sample_time_in_clip / tc->fade_in_sec;
                double db = kFadeMinDb * (1.0 - t);
                fade_gain *= std::pow(10.0, db / 20.0);
              }
              if (tc->fade_out_sec > 0 &&
                  sample_time_in_clip > clip_duration - tc->fade_out_sec) {
                double t = std::max(0.0, (clip_duration - sample_time_in_clip) /
                                             tc->fade_out_sec);
                double db = kFadeMinDb * (1.0 - t);
                fade_gain *= std::pow(10.0, db / 20.0);
              }
              if (tc->clip->num_channels == 2 &&
                  sample_pos * 2 + 1 < (int)tc->clip->audio_data.size()) {
                bufferL[i] += tc->clip->audio_data[sample_pos * 2] * fade_gain;
                bufferR[i] +=
                    tc->clip->audio_data[sample_pos * 2 + 1] * fade_gain;
              } else if (tc->clip->num_channels == 1 &&
                         sample_pos < (int)tc->clip->audio_data.size()) {
                float s = tc->clip->audio_data[sample_pos] * fade_gain;
                bufferL[i] += s;
                bufferR[i] += s;
              }
            }
          }
        }
      }

      // Channel Rack sequencer events (for generator tracks >= 100)
      if (pair.first >= 100 && !state.sequencer_patterns.empty()) {
        const int ch_idx = pair.first - 100;
        if (state.playback_mode == SequencerPlaybackMode::PAT) {
          for (const auto& pat : state.sequencer_patterns) {
            if (pat.pattern_index != state.selected_pattern_index) continue;
            const double pat_loop_beats =
                (pat.length_steps > 0 ? pat.length_steps : 16) * 0.25;
            CollectPatternBlockMidiEvents(
                pat, ch_idx, state.swing_amount, pat_loop_beats,
                state.playhead_pos_sec * bps, time_per_block * bps, state.bpm,
                sample_rate, block_size, true, combinedMidi);
          }
        } else {
          CollectSongPatternClipBlockEvents(
              state, ch_idx, state.playhead_pos_sec, time_per_block,
              sample_rate, block_size, combinedMidi);
        }
      }

      std::stable_sort(combinedMidi.begin(), combinedMidi.end(),
                       [](const MidiNoteEvent& a, const MidiNoteEvent& b) {
                         if (a.sampleOffset != b.sampleOffset) {
                           return a.sampleOffset < b.sampleOffset;
                         }
                         return !a.isNoteOn && b.isNoteOn;
                       });

      if (!track->plugins.empty() && track->plugins[0]->isInstrument() &&
          !track->plugin_bypass.count(0)) {
        track->plugins[0]->process(nullptr, outChannels, block_size, context,
                                   combinedMidi);
      }

      // Apply automation during bounce
      double current_beats = state.playhead_pos_sec * (state.bpm / 60.0);
      for (const auto& lane : track->automation_lanes) {
        if (!lane.clips.empty() && lane.plugin_idx >= 0 &&
            lane.plugin_idx < (int)track->plugins.size()) {
          float val = GetAutomationValue(lane, current_beats, state.bpm);
          track->plugins[lane.plugin_idx]->setParameterValue(lane.param_id,
                                                             val);
        }
      }

      for (size_t i = 0; i < track->plugins.size(); ++i) {
        if (i == 0 && track->plugins[i]->isInstrument()) continue;
        if (track->plugin_bypass.count((int)i)) continue;  // bypassed
        const float mix = track->GetPluginMix((int)i);
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

      // Route to group bus or master
      int out_idx = track->output_track_index;
      if (out_idx < 0 && track->group_parent_index >= 0) {
        out_idx = track->group_parent_index;
      }
      if (out_idx >= 0) {
        auto git = group_bus_buffers.find(out_idx);
        if (git != group_bus_buffers.end()) {
          for (int i = 0; i < block_size; ++i) {
            git->second.first[i] += bufferL[i];
            git->second.second[i] += bufferR[i];
          }
          continue;  // Don't sum to master
        }
      }
      for (int i = 0; i < block_size; ++i) {
        mixBufferL[i] += bufferL[i];
        mixBufferR[i] += bufferR[i];
      }
    }

    // Pass 2: Process group tracks (sum their bus into master)
    for (auto& pair : state.tracks) {
      Track* track = pair.second.get();
      if (track->track_type != Track::TrackType::GROUP) continue;
      auto git = group_bus_buffers.find(pair.first);
      if (git == group_bus_buffers.end()) continue;

      // Load bus input into buffers
      for (int i = 0; i < block_size; ++i) {
        bufferL[i] = git->second.first[i];
        bufferR[i] = git->second.second[i];
      }

      // Apply group track effects
      for (size_t i = 0; i < track->plugins.size(); ++i) {
        if (i == 0 && track->plugins[i]->isInstrument()) continue;
        if (track->plugin_bypass.count((int)i)) continue;
        const float mix = track->GetPluginMix((int)i);
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

      for (int i = 0; i < block_size; ++i) {
        mixBufferL[i] += bufferL[i];
        mixBufferR[i] += bufferR[i];
      }
    }

    // Pass 3: Process Master bus track effects if configured
    for (auto& pair : state.tracks) {
      Track* track = pair.second.get();
      if (track->track_type != Track::TrackType::MASTER) continue;
      for (int i = 0; i < block_size; ++i) {
        bufferL[i] = mixBufferL[i];
        bufferR[i] = mixBufferR[i];
      }
      for (size_t i = 0; i < track->plugins.size(); ++i) {
        if (i == 0 && track->plugins[i]->isInstrument()) continue;
        if (track->plugin_bypass.count((int)i)) continue;
        const float mix = track->GetPluginMix((int)i);
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
      const float gain = track->muted ? 0.0f : track->volume;
      for (int i = 0; i < block_size; ++i) {
        mixBufferL[i] = bufferL[i] * gain;
        mixBufferR[i] = bufferR[i] * gain;
      }
    }

    for (int i = 0; i < block_size; ++i) {
      output_buffer.push_back(mixBufferL[i]);
      output_buffer.push_back(mixBufferR[i]);
    }

    state.playhead_pos_sec += time_per_block;
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
  // Load plugins from snapshot (we only need the plugin chain)
  ProjectState state;
  state.sample_rate = sample_rate;
  auto s = ApplyProjectState(state, snapshot);
  if (!s.ok()) return false;
  state.bpm = bpm;

  // Get the plugin chain for the target track
  Track* track = nullptr;
  if (state.tracks.find(track_idx) != state.tracks.end()) {
    track = state.tracks[track_idx].get();
  }

  state.is_timeline_playing = true;
  state.playhead_pos_sec = start_sec;

  int block_size = 512;
  std::vector<float> output_buffer;

  HostProcessContext context;
  context.sampleRate = sample_rate;
  context.tempo = bpm;
  context.timeSigNumerator = 4;
  context.timeSigDenominator = 4;

  alignas(32) float bufferL[512];
  alignas(32) float bufferR[512];
  float* outChannels[] = {bufferL, bufferR};

  double time_per_block = block_size / (double)sample_rate;

  while (state.playhead_pos_sec < end_sec) {
    std::fill(bufferL, bufferL + block_size, 0.0f);
    std::fill(bufferR, bufferR + block_size, 0.0f);

    // Use the passed-in timeline_clips (deep-copied from live state)
    for (const auto& tc : timeline_clips) {
      if (!tc || !tc->clip) continue;
      double clip_duration = (tc->duration_beats > 0)
                                 ? tc->duration_beats * 60.0 / bpm
                                 : tc->duration_sec;
      if (state.playhead_pos_sec + time_per_block > tc->start_time_sec &&
          state.playhead_pos_sec < tc->start_time_sec + clip_duration) {
        double clip_local_time = state.playhead_pos_sec - tc->start_time_sec;

        if (tc->clip->type == Clip::Type::MIDI) {
          std::vector<MidiNoteEvent> blockEvents;
          double beats_per_sec = bpm / 60.0;
          double window_start_beats = clip_local_time * beats_per_sec;
          double window_end_beats =
              (clip_local_time + time_per_block) * beats_per_sec;
          for (const auto& me : tc->clip->midi_events) {
            if (me.beats >= window_start_beats && me.beats < window_end_beats) {
              MidiNoteEvent e;
              double event_local_sec =
                  me.beats / beats_per_sec - clip_local_time;
              e.sampleOffset =
                  std::max(0, (int)(event_local_sec * sample_rate));
              if (e.sampleOffset >= block_size) e.sampleOffset = block_size - 1;
              e.channel = me.channel;
              e.pitch = me.note;
              e.isNoteOn = hibiki::isNoteOn(me);
              e.velocity = e.isNoteOn ? me.velocity / 127.0f : 0.0f;
              blockEvents.push_back(e);
            }
          }
          // Use the plugin chain from the snapshot-restored track
          if (track && !track->plugins.empty() &&
              track->plugins[0]->isInstrument() &&
              !track->plugin_bypass.count(0)) {
            track->plugins[0]->process(nullptr, outChannels, block_size,
                                       context, blockEvents);
          }
        } else {
          // Audio playback
          int content_samples = (int)tc->clip->audio_data.size();
          if (tc->clip->num_channels == 2) content_samples /= 2;
          int start_sample = (int)(clip_local_time * sample_rate);
          for (int i = 0; i < block_size; ++i) {
            int sample_pos = start_sample + i;
            if (sample_pos < 0) continue;
            if (tc->clip->is_loop && content_samples > 0) {
              sample_pos = sample_pos % content_samples;
            }
            // Compute per-sample fade gain (linear in dB domain)
            double fade_gain = 1.0;
            constexpr double kFadeMinDb = -30.0;
            double sample_time_in_clip =
                clip_local_time + (double)i / sample_rate;
            if (tc->fade_in_sec > 0 && sample_time_in_clip < tc->fade_in_sec) {
              double t = sample_time_in_clip / tc->fade_in_sec;
              double db = kFadeMinDb * (1.0 - t);
              fade_gain *= std::pow(10.0, db / 20.0);
            }
            if (tc->fade_out_sec > 0 &&
                sample_time_in_clip > clip_duration - tc->fade_out_sec) {
              double t = std::max(0.0, (clip_duration - sample_time_in_clip) /
                                           tc->fade_out_sec);
              double db = kFadeMinDb * (1.0 - t);
              fade_gain *= std::pow(10.0, db / 20.0);
            }
            if (tc->clip->num_channels == 2 &&
                sample_pos * 2 + 1 < (int)tc->clip->audio_data.size()) {
              bufferL[i] += tc->clip->audio_data[sample_pos * 2] * fade_gain;
              bufferR[i] +=
                  tc->clip->audio_data[sample_pos * 2 + 1] * fade_gain;
            } else if (tc->clip->num_channels == 1 &&
                       sample_pos < (int)tc->clip->audio_data.size()) {
              bufferL[i] += tc->clip->audio_data[sample_pos] * fade_gain;
              bufferR[i] += tc->clip->audio_data[sample_pos] * fade_gain;
            }
          }
        }
      }
    }

    // Process effects chain from the snapshot-restored track
    if (track) {
      // Apply automation
      double current_beats = state.playhead_pos_sec * (bpm / 60.0);
      for (const auto& lane : track->automation_lanes) {
        if (!lane.clips.empty() && lane.plugin_idx >= 0 &&
            lane.plugin_idx < (int)track->plugins.size()) {
          float val = GetAutomationValue(lane, current_beats, bpm);
          track->plugins[lane.plugin_idx]->setParameterValue(lane.param_id,
                                                             val);
        }
      }

      for (size_t i = 0; i < track->plugins.size(); ++i) {
        if (i == 0 && track->plugins[i]->isInstrument()) continue;
        if (track->plugin_bypass.count((int)i)) continue;
        track->plugins[i]->process(outChannels, outChannels, block_size,
                                   context, {});
      }
    }

    for (int i = 0; i < block_size; ++i) {
      output_buffer.push_back(bufferL[i]);
      output_buffer.push_back(bufferR[i]);
    }

    state.playhead_pos_sec += time_per_block;
    context.continuousTimeSamples = state.playhead_pos_sec * sample_rate;
    context.projectTimeMusic = state.playhead_pos_sec * (bpm / 60.0);
  }

  auto status = SaveWav(path, output_buffer, 2, (int)sample_rate);
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
