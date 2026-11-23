#include <algorithm>
#include <filesystem>
#include <mutex>
#include <string>
#include <unordered_set>
#include <vector>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "engine/commands/commands.hpp"
#include "engine/core/audio_file.hpp"
#include "engine/core/clip.hpp"
#include "engine/core/midi.hpp"
#include "engine/core/project.hpp"
#include "engine/core/track.hpp"
#include "engine/ipc/ipc.hpp"
#include "pb/commands.pb.h"

namespace hibiki {

namespace {

bool WouldCreateRoutingCycle(const ProjectState& state, int src_idx,
                             int target_idx) {
  if (target_idx < 0) return false;
  if (target_idx == src_idx) return true;
  std::vector<int> stack{target_idx};
  std::unordered_set<int> visited;
  while (!stack.empty()) {
    int cur = stack.back();
    stack.pop_back();
    if (cur == src_idx) return true;
    if (!visited.insert(cur).second) continue;
    auto it = state.tracks.find(cur);
    if (it == state.tracks.end() || !it->second) continue;
    const Track* tr = it->second.get();
    int out_idx = tr->output_track_index >= 0 ? tr->output_track_index
                                              : tr->group_parent_index;
    if (out_idx >= 0) stack.push_back(out_idx);
    for (const auto& send : tr->aux_sends) {
      if (send.aux_track_index >= 0 && send.level > 0.0f) {
        stack.push_back(send.aux_track_index);
      }
    }
  }
  return false;
}

}  // namespace

void handleTrackCmd(const pb::commands::TrackCmd& cmd, ProjectState& state,
                    HistoryManager& history) {
  int tidx = cmd.target().track_index();
  switch (cmd.action()) {
    case pb::commands::TrackCmd::ACTION_PLAY_SLOT: {
      std::lock_guard<std::mutex> lock(state.tracks_mutex);
      if (cmd.target().has_session_slot()) {
        int sidx = cmd.target().session_slot();
        GetOrCreateTrack(state, tidx)->PlayClip(sidx);
      } else {
        int sidx = cmd.value();
        for (auto& pair : state.tracks) pair.second->PlayClip(sidx);
      }
      sendAck("PLAY_CLIP", true);
      break;
    }
    case pb::commands::TrackCmd::ACTION_STOP: {
      std::lock_guard<std::mutex> lock(state.tracks_mutex);
      GetOrCreateTrack(state, tidx)->Stop();
      sendAck("STOP_TRACK", true);
      break;
    }
    case pb::commands::TrackCmd::ACTION_LOAD_CLIP: {
      int sidx = cmd.target().session_slot();
      std::string mpath = cmd.clip_data().path();
      bool is_loop = cmd.clip_data().is_loop();
      double sr = state.sample_rate.load();
      auto loaded = hibiki::LoadClip(mpath, is_loop, sr);
      if (!loaded.ok()) {
        sendLog("Failed to load clip: " + mpath);
        break;
      }
      auto clip = std::make_unique<Clip>(std::move(*loaded));
      std::lock_guard<std::mutex> lock(state.tracks_mutex);
      history.pushState(CaptureProjectState(state));
      auto track = GetOrCreateTrack(state, tidx);
      if (track->InsertLoadedClip(sidx, std::move(clip), mpath)) {
        sendAck("LOAD_CLIP", true);
        std::string name = mpath;
        size_t last_slash = mpath.find_last_of("/\\");
        if (last_slash != std::string::npos)
          name = mpath.substr(last_slash + 1);
        sendClipInfo(tidx, sidx, name, mpath);
      } else {
        sendLog("Failed to load clip: " + mpath);
      }
      break;
    }
    case pb::commands::TrackCmd::ACTION_DELETE_CLIP: {
      int sidx = cmd.target().session_slot();
      std::lock_guard<std::mutex> lock(state.tracks_mutex);
      history.pushState(CaptureProjectState(state));
      if (GetOrCreateTrack(state, tidx)->DeleteClip(sidx)) {
        sendAck("DELETE_CLIP", true);
        sendClipInfo(tidx, sidx, "", "");
      } else {
        sendAck("DELETE_CLIP", false);
      }
      break;
    }
    case pb::commands::TrackCmd::ACTION_SET_CLIP_LOOP: {
      std::lock_guard<std::mutex> lock(state.tracks_mutex);
      history.pushState(CaptureProjectState(state));
      auto* track = GetOrCreateTrack(state, tidx);
      if (cmd.target().timeline_clip() > 0 ||
          cmd.target().session_slot() == 0) {
        // Timeline clip loop
        int tcidx = cmd.target().timeline_clip();
        if (tcidx >= 0 && tcidx < (int)track->timeline_clips.size()) {
          auto& tc = track->timeline_clips[tcidx];
          if (tc->clip) {
            tc->clip->is_loop = cmd.flag();
            // Store loop interval from cmd.value() (in beats)
            if (cmd.flag() && cmd.value() > 0) {
              tc->loop_interval_beats = cmd.value();
            } else if (!cmd.flag()) {
              tc->loop_interval_beats = 0.0;
            }
            // Notify GUI of the change
            std::string clipname = tc->clip->path;
            if (clipname.empty())
              clipname = tc->clip->name.empty() ? "Clip" : tc->clip->name;
            clipname = pathBasename(clipname);
            CHECK_GT(state.bpm, 0);
            float duration_for_gui =
                (tc->duration_beats > 0)
                    ? (float)beatsToSec(tc->duration_beats, state.bpm)
                    : (float)tc->duration_sec;
            float li_sec =
                (tc->loop_interval_beats > 0)
                    ? (float)beatsToSec(tc->loop_interval_beats, state.bpm)
                    : 0.0f;
            sendTimelineClipInfo(
                tidx, tcidx, clipname, tc->clip->path,
                (float)tc->start_time_sec, duration_for_gui,
                tc->clip->waveform_summary, tc->clip->is_loop, tc->alias_source,
                li_sec, (float)tc->fade_in_sec, (float)tc->fade_out_sec,
                tc->muted, tc->pattern_index,
                tc->clip->type == Clip::Type::MIDI);
          }
        }
      } else {
        // Session clip loop
        track->SetClipLoop(cmd.target().session_slot(), cmd.flag());
      }
      sendAck("SET_CLIP_LOOP", true);
      break;
    }
    case pb::commands::TrackCmd::ACTION_ADD_TIMELINE_CLIP: {
      std::string path = cmd.clip_data().path();
      double start = cmd.value();
      double dur_beats = cmd.clip_data().duration_beats();
      double sr = state.sample_rate.load();
      int pattern_idx = -1;
      auto clip = Track::PrepareTimelineClip(path, dur_beats, sr, pattern_idx);
      std::lock_guard<std::mutex> lock(state.tracks_mutex);
      history.pushState(CaptureProjectState(state));
      auto* track = GetOrCreateTrack(state, tidx);
      if (clip) {
        track->InsertPreparedTimelineClip(std::move(clip), path, start,
                                          state.bpm.load(), pattern_idx);
      }
      sendAck("ADD_TIMELINE_CLIP", true);
      break;
    }
    case pb::commands::TrackCmd::ACTION_REMOVE_TIMELINE_CLIP: {
      int cidx = cmd.target().timeline_clip();
      std::lock_guard<std::mutex> lock(state.tracks_mutex);
      history.pushState(CaptureProjectState(state));
      GetOrCreateTrack(state, tidx)->RemoveTimelineClip(cidx);
      sendAck("REMOVE_TIMELINE_CLIP", true);
      break;
    }
    case pb::commands::TrackCmd::ACTION_RESIZE_TIMELINE_CLIP: {
      int cidx = cmd.target().timeline_clip();
      float dur_beats = cmd.clip_data().duration_beats();
      float trim_start = cmd.clip_data().trim_start_beats();
      std::lock_guard<std::mutex> lock(state.tracks_mutex);
      history.pushState(CaptureProjectState(state));
      if (state.tracks.count(tidx)) {
        auto& track = state.tracks[tidx];
        if (cidx >= 0 && cidx < (int)track->timeline_clips.size() &&
            track->timeline_clips[cidx]) {
          auto& tc = track->timeline_clips[cidx];
          if (dur_beats > 0) {
            tc->duration_beats = dur_beats;
          }
          tc->trim_start_beats = trim_start;
          CHECK_GT(state.bpm, 0);
          float duration_for_gui =
              (float)beatsToSec(tc->duration_beats, state.bpm);
          std::string clipname = tc->clip ? tc->clip->path : "";
          if (clipname.empty()) clipname = "New Clip";
          clipname = pathBasename(clipname);
          float li_sec_r =
              (tc->loop_interval_beats > 0)
                  ? (float)beatsToSec(tc->loop_interval_beats, state.bpm)
                  : 0.0f;
          sendTimelineClipInfo(
              tidx, cidx, clipname, tc->clip ? tc->clip->path : "",
              (float)tc->start_time_sec, duration_for_gui,
              tc->clip ? tc->clip->waveform_summary : std::vector<float>{},
              tc->clip ? tc->clip->is_loop : false, tc->alias_source, li_sec_r,
              (float)tc->fade_in_sec, (float)tc->fade_out_sec, tc->muted,
              tc->pattern_index,
              tc->clip && tc->clip->type == Clip::Type::MIDI);
        }
      }
      sendAck("RESIZE_TIMELINE_CLIP", true);
      break;
    }
    case pb::commands::TrackCmd::ACTION_MOVE_TIMELINE_CLIP: {
      int cidx = cmd.target().timeline_clip();
      float new_start_sec = cmd.value();
      int target_tidx = cmd.target_track_index();
      std::lock_guard<std::mutex> lock(state.tracks_mutex);
      history.pushState(CaptureProjectState(state));
      if (state.tracks.count(tidx)) {
        auto& src_track = state.tracks[tidx];
        if (cidx >= 0 && cidx < (int)src_track->timeline_clips.size() &&
            src_track->timeline_clips[cidx]) {
          if (target_tidx == tidx || !state.tracks.count(target_tidx)) {
            // Same-track move: just update start time
            src_track->timeline_clips[cidx]->start_time_sec = new_start_sec;
          } else {
            // Cross-track move: transfer clip to target track
            auto tc = std::move(src_track->timeline_clips[cidx]);
            src_track->timeline_clips.erase(src_track->timeline_clips.begin() +
                                            cidx);
            tc->start_time_sec = new_start_sec;
            state.tracks[target_tidx]->timeline_clips.push_back(std::move(tc));
          }
        }
      }
      sendAck("MOVE_TIMELINE_CLIP", true);
      break;
    }
    case pb::commands::TrackCmd::ACTION_COPY_TIMELINE_CLIP: {
      int src_cidx = cmd.target().timeline_clip();
      float new_start_sec = cmd.value();
      int target_tidx = cmd.target_track_index();
      bool is_alias = cmd.flag();
      std::lock_guard<std::mutex> lock(state.tracks_mutex);
      history.pushState(CaptureProjectState(state));
      if (state.tracks.count(tidx)) {
        auto& src_track = state.tracks[tidx];
        if (src_cidx >= 0 && src_cidx < (int)src_track->timeline_clips.size() &&
            src_track->timeline_clips[src_cidx] &&
            src_track->timeline_clips[src_cidx]->clip) {
          auto& src_tc = src_track->timeline_clips[src_cidx];
          auto new_tc = std::make_unique<TimelineClip>();
          new_tc->clip = std::make_unique<Clip>();
          // Copy clip data
          new_tc->clip->path = src_tc->clip->path;
          new_tc->clip->type = src_tc->clip->type;
          new_tc->clip->audio_data = src_tc->clip->audio_data;
          new_tc->clip->sample_rate = src_tc->clip->sample_rate;
          new_tc->clip->num_channels = src_tc->clip->num_channels;
          new_tc->clip->duration_sec = src_tc->clip->duration_sec;
          new_tc->clip->duration_beats = src_tc->clip->duration_beats;
          new_tc->clip->midi_events = src_tc->clip->midi_events;
          new_tc->clip->waveform_summary = src_tc->clip->waveform_summary;
          new_tc->clip->is_loop = src_tc->clip->is_loop;
          new_tc->clip->name = src_tc->clip->name;
          // Copy timeline position
          new_tc->start_time_sec = new_start_sec;
          new_tc->duration_sec = src_tc->duration_sec;
          new_tc->duration_beats = src_tc->duration_beats;
          new_tc->trim_start_beats = src_tc->trim_start_beats;
          if (is_alias) {
            new_tc->alias_source = src_cidx;
          }
          // Add to target track
          auto* dest_track = GetOrCreateTrack(state, target_tidx);
          dest_track->timeline_clips.push_back(std::move(new_tc));
          int new_cidx = (int)dest_track->timeline_clips.size() - 1;
          auto& added = dest_track->timeline_clips[new_cidx];
          // Compute display info
          std::string clipname = added->clip->path;
          if (clipname.empty())
            clipname = added->clip->name.empty() ? "Clip" : added->clip->name;
          clipname = pathBasename(clipname);
          CHECK_GT(state.bpm, 0);
          float duration_for_gui =
              (added->duration_beats > 0)
                  ? (float)beatsToSec(added->duration_beats, state.bpm)
                  : (float)added->duration_sec;
          float li_sec_c =
              (added->loop_interval_beats > 0)
                  ? (float)beatsToSec(added->loop_interval_beats, state.bpm)
                  : 0.0f;
          sendTimelineClipInfo(
              target_tidx, new_cidx, clipname, added->clip->path,
              (float)added->start_time_sec, duration_for_gui,
              added->clip->waveform_summary, added->clip->is_loop,
              added->alias_source, li_sec_c, (float)added->fade_in_sec,
              (float)added->fade_out_sec, added->muted, added->pattern_index,
              added->clip->type == Clip::Type::MIDI);
        }
      }
      sendAck("COPY_TIMELINE_CLIP", true);
      break;
    }
    case pb::commands::TrackCmd::ACTION_SET_CLIP_FADE: {
      int cidx = cmd.target().timeline_clip();
      float fade_in = cmd.value();
      float fade_out = cmd.fade_out_sec();
      std::lock_guard<std::mutex> lock(state.tracks_mutex);
      history.pushState(CaptureProjectState(state));
      if (state.tracks.count(tidx)) {
        auto& track = state.tracks[tidx];
        if (cidx >= 0 && cidx < (int)track->timeline_clips.size() &&
            track->timeline_clips[cidx]) {
          auto& tc = track->timeline_clips[cidx];
          tc->fade_in_sec = std::max(0.0, (double)fade_in);
          tc->fade_out_sec = std::max(0.0, (double)fade_out);
          CHECK_GT(state.bpm, 0);
          float duration_for_gui =
              (tc->duration_beats > 0)
                  ? (float)beatsToSec(tc->duration_beats, state.bpm)
                  : (float)tc->duration_sec;
          std::string clipname = tc->clip ? tc->clip->path : "";
          if (clipname.empty()) clipname = "Clip";
          clipname = pathBasename(clipname);
          float li_sec =
              (tc->loop_interval_beats > 0)
                  ? (float)beatsToSec(tc->loop_interval_beats, state.bpm)
                  : 0.0f;
          sendTimelineClipInfo(
              tidx, cidx, clipname, tc->clip ? tc->clip->path : "",
              (float)tc->start_time_sec, duration_for_gui,
              tc->clip ? tc->clip->waveform_summary : std::vector<float>{},
              tc->clip ? tc->clip->is_loop : false, tc->alias_source, li_sec,
              (float)tc->fade_in_sec, (float)tc->fade_out_sec, tc->muted,
              tc->pattern_index,
              tc->clip && tc->clip->type == Clip::Type::MIDI);
        }
      }
      sendAck("SET_CLIP_FADE", true);
      break;
    }
    case pb::commands::TrackCmd::ACTION_BOUNCE_IN_PLACE: {
      int cidx = cmd.target().timeline_clip();
      float tail_sec = cmd.bounce_tail_sec();

      // Phase 1: Under mutex, capture snapshot and clip time info
      std::vector<uint8_t> snapshot;
      double clip_start = 0, bounce_end = 0;
      double bpm = 0;
      float sr = 0;
      std::string bounce_path;
      std::vector<std::unique_ptr<TimelineClip>> clips_copy;
      {
        std::lock_guard<std::mutex> lock(state.tracks_mutex);
        if (!state.tracks.count(tidx)) {
          sendAck("BOUNCE_IN_PLACE", false);
          break;
        }
        auto& track = state.tracks[tidx];
        if (cidx < 0 || cidx >= (int)track->timeline_clips.size() ||
            !track->timeline_clips[cidx]) {
          sendAck("BOUNCE_IN_PLACE", false);
          break;
        }
        auto& tc = track->timeline_clips[cidx];
        clip_start = tc->start_time_sec;
        double clip_duration = (tc->duration_beats > 0)
                                   ? tc->duration_beats * 60.0 / state.bpm
                                   : tc->duration_sec;
        bounce_end = clip_start + clip_duration + tail_sec;
        bpm = state.bpm;
        sr = state.sample_rate;
        bounce_path = "/tmp/hibiki_bounce_" + std::to_string(tidx) + "_" +
                      std::to_string(cidx) + ".wav";
        snapshot = CaptureProjectState(state);

        // Deep-copy timeline clips to preserve MIDI events and audio data
        for (const auto& src : track->timeline_clips) {
          if (!src || !src->clip) continue;
          auto copy = std::make_unique<TimelineClip>();
          copy->start_time_sec = src->start_time_sec;
          copy->duration_sec = src->duration_sec;
          copy->duration_beats = src->duration_beats;
          copy->trim_start_beats = src->trim_start_beats;
          copy->alias_source = src->alias_source;
          copy->loop_interval_beats = src->loop_interval_beats;
          copy->fade_in_sec = src->fade_in_sec;
          copy->fade_out_sec = src->fade_out_sec;
          copy->muted = src->muted;
          copy->clip = std::make_unique<Clip>();
          *copy->clip = *src->clip;  // Deep copy clip data
          clips_copy.push_back(std::move(copy));
        }
      }

      // Phase 2: Without mutex, render single track offline
      bool ok = BounceTrackClip(snapshot, sr, bpm, tidx, clips_copy, clip_start,
                                bounce_end, bounce_path);
      if (!ok) {
        LOG(ERROR) << "BounceTrackClip failed for track " << tidx;
        sendAck("BOUNCE_IN_PLACE", false);
        break;
      }

      // Phase 3: Re-acquire mutex, mute original, load bounced clip
      {
        std::lock_guard<std::mutex> lock(state.tracks_mutex);
        history.pushState(CaptureProjectState(state));

        // Mute the original clip
        auto& tc = state.tracks[tidx]->timeline_clips[cidx];
        tc->muted = true;

        // Notify GUI about muted source clip
        std::string clipname = tc->clip ? tc->clip->path : "";
        if (clipname.empty()) clipname = "Clip";
        clipname = pathBasename(clipname);
        float dur_gui = (tc->duration_beats > 0)
                            ? (float)beatsToSec(tc->duration_beats, state.bpm)
                            : (float)tc->duration_sec;
        float li_sec =
            (tc->loop_interval_beats > 0)
                ? (float)beatsToSec(tc->loop_interval_beats, state.bpm)
                : 0.0f;
        sendTimelineClipInfo(
            tidx, cidx, clipname, tc->clip ? tc->clip->path : "",
            (float)tc->start_time_sec, dur_gui,
            tc->clip ? tc->clip->waveform_summary : std::vector<float>{},
            tc->clip ? tc->clip->is_loop : false, tc->alias_source, li_sec,
            (float)tc->fade_in_sec, (float)tc->fade_out_sec, tc->muted,
            tc->pattern_index, tc->clip && tc->clip->type == Clip::Type::MIDI);

        // Find next available track index
        int new_tidx = 0;
        for (auto& [idx, t] : state.tracks) {
          new_tidx = std::max(new_tidx, idx + 1);
        }
        auto* new_track = GetOrCreateTrack(state, new_tidx);
        new_track->name = "Bounced";

        // Load bounced WAV
        auto new_tc = std::make_unique<TimelineClip>();
        auto clip_result = LoadClip(bounce_path, false, state.sample_rate);
        if (clip_result.ok()) {
          new_tc->clip = std::make_unique<Clip>(std::move(*clip_result));
          new_tc->start_time_sec = clip_start;
          new_tc->duration_sec = new_tc->clip->duration_sec;
        } else {
          LOG(ERROR) << "Failed to load bounced audio: "
                     << clip_result.status();
          sendAck("BOUNCE_IN_PLACE", false);
          break;
        }
        int new_cidx = (int)new_track->timeline_clips.size();
        new_track->timeline_clips.push_back(std::move(new_tc));

        // Notify GUI about new track and clip
        sendTrackInfo(new_tidx, "Bounced");
        auto& added = new_track->timeline_clips[new_cidx];
        if (added && added->clip) {
          sendTimelineClipInfo(new_tidx, new_cidx,
                               pathBasename(added->clip->path),
                               added->clip->path, (float)added->start_time_sec,
                               (float)added->duration_sec,
                               added->clip->waveform_summary, false, -1, 0.0f);
        }
      }

      sendAck("BOUNCE_IN_PLACE", true);
      break;
    }
    case pb::commands::TrackCmd::ACTION_SET_CLIP_MUTED: {
      int cidx = cmd.target().timeline_clip();
      bool muted = cmd.flag();
      std::lock_guard<std::mutex> lock(state.tracks_mutex);
      if (state.tracks.count(tidx)) {
        auto& track = state.tracks[tidx];
        if (cidx >= 0 && cidx < (int)track->timeline_clips.size() &&
            track->timeline_clips[cidx]) {
          history.pushState(CaptureProjectState(state));
          auto& tc = track->timeline_clips[cidx];
          tc->muted = muted;
          std::string clipname = tc->clip ? tc->clip->path : "";
          if (clipname.empty()) clipname = "Clip";
          clipname = pathBasename(clipname);
          float dur_gui = (tc->duration_beats > 0)
                              ? (float)beatsToSec(tc->duration_beats, state.bpm)
                              : (float)tc->duration_sec;
          float li_sec =
              (tc->loop_interval_beats > 0)
                  ? (float)beatsToSec(tc->loop_interval_beats, state.bpm)
                  : 0.0f;
          sendTimelineClipInfo(
              tidx, cidx, clipname, tc->clip ? tc->clip->path : "",
              (float)tc->start_time_sec, dur_gui,
              tc->clip ? tc->clip->waveform_summary : std::vector<float>{},
              tc->clip ? tc->clip->is_loop : false, tc->alias_source, li_sec,
              (float)tc->fade_in_sec, (float)tc->fade_out_sec, tc->muted,
              tc->pattern_index,
              tc->clip && tc->clip->type == Clip::Type::MIDI);
        }
      }
      sendAck("SET_CLIP_MUTED", true);
      break;
    }
    case pb::commands::TrackCmd::ACTION_ARM_RECORD: {
      bool should_arm = false;
      std::string dev_id;
      bool needs_midi_open = false;
      {
        std::lock_guard<std::mutex> lock(state.tracks_mutex);
        auto track = GetOrCreateTrack(state, tidx);
        should_arm = !track->record_armed;
        track->record_armed = should_arm;
        if (should_arm) {
          track->record_buffer.reserve(44100 * 16);
          track->midi_record_buffer.reserve(4096);
          if (!track->midi_input_device &&
              !track->midi_input_device_id.empty()) {
            dev_id = track->midi_input_device_id;
            needs_midi_open = true;
          }
        }
      }
      if (needs_midi_open) {
        auto midi = MidiInput::create();
        if (midi && midi->open(dev_id)) {
          std::lock_guard<std::mutex> lock(state.tracks_mutex);
          auto* track = GetOrCreateTrack(state, tidx);
          if (!track->midi_input_device &&
              track->midi_input_device_id == dev_id) {
            track->midi_input_device = std::move(midi);
          }
        }
      }
      sendAck("ARM_RECORD", true);
      break;
    }
    case pb::commands::TrackCmd::ACTION_SET_INPUT_DEVICE: {
      std::unique_ptr<SoundDevice> old_dev;
      {
        std::lock_guard<std::mutex> lock(state.tracks_mutex);
        auto track = GetOrCreateTrack(state, tidx);
        track->input_device_id = cmd.input_device_id();
        track->input_channel_start = cmd.input_channel_start();
        track->input_stereo = cmd.input_stereo();
        old_dev = std::move(track->input_device);
      }
      sendAck("SET_INPUT_DEVICE", true);
      break;
    }
    case pb::commands::TrackCmd::ACTION_SET_MIDI_INPUT: {
      const std::string dev_id = cmd.midi_input_device_id();
      std::unique_ptr<MidiInput> new_midi;
      if (!dev_id.empty()) {
        new_midi = MidiInput::create();
        if (new_midi && !new_midi->open(dev_id)) {
          new_midi.reset();
        }
      }
      std::unique_ptr<MidiInput> old_midi;
      {
        std::lock_guard<std::mutex> lock(state.tracks_mutex);
        auto track = GetOrCreateTrack(state, tidx);
        track->midi_input_device_id = dev_id;
        old_midi = std::move(track->midi_input_device);
        track->midi_input_device = std::move(new_midi);
      }
      sendAck("SET_MIDI_INPUT", true);
      break;
    }
    case pb::commands::TrackCmd::ACTION_SET_RECORD_MODE: {
      std::lock_guard<std::mutex> lock(state.tracks_mutex);
      auto track = GetOrCreateTrack(state, tidx);
      track->record_mode = cmd.record_mode() == 1
                               ? Track::RecordMode::RECORD_MIDI
                               : Track::RecordMode::RECORD_AUDIO;
      sendAck("SET_RECORD_MODE", true);
      break;
    }
    case pb::commands::TrackCmd::ACTION_SET_VOLUME: {
      std::lock_guard<std::mutex> lock(state.tracks_mutex);
      auto track = GetOrCreateTrack(state, tidx);
      track->volume = std::max(0.0f, std::min(2.0f, cmd.value()));
      sendAck("SET_VOLUME", true);
      break;
    }
    case pb::commands::TrackCmd::ACTION_SET_PAN: {
      std::lock_guard<std::mutex> lock(state.tracks_mutex);
      auto track = GetOrCreateTrack(state, tidx);
      track->pan = std::max(-1.0f, std::min(1.0f, cmd.value()));
      sendAck("SET_PAN", true);
      break;
    }
    case pb::commands::TrackCmd::ACTION_SET_MUTE: {
      std::lock_guard<std::mutex> lock(state.tracks_mutex);
      auto track = GetOrCreateTrack(state, tidx);
      track->muted = cmd.flag();
      sendAck("SET_MUTE", true);
      break;
    }
    case pb::commands::TrackCmd::ACTION_SET_SOLO: {
      std::lock_guard<std::mutex> lock(state.tracks_mutex);
      auto track = GetOrCreateTrack(state, tidx);
      track->soloed = cmd.flag();
      sendAck("SET_SOLO", true);
      break;
    }
    case pb::commands::TrackCmd::ACTION_SET_TRACK_TYPE: {
      std::lock_guard<std::mutex> lock(state.tracks_mutex);
      auto track = GetOrCreateTrack(state, tidx);
      int type_val = cmd.record_mode();  // Reuse record_mode field for type int
      track->track_type = static_cast<Track::TrackType>(type_val);
      std::vector<std::tuple<int, float, bool>> sends;
      for (const auto& s : track->aux_sends)
        sends.emplace_back(s.aux_track_index, s.level, s.pre_fader);
      sendTrackInfoFull(tidx, track->name, static_cast<int>(track->track_type),
                        track->output_track_index, sends,
                        track->group_parent_index, track->volume, track->pan,
                        true, track->muted, track->soloed, track->eq_low,
                        track->eq_mid, track->eq_high, track->stereo_sep);
      sendAck("SET_TRACK_TYPE", true);
      break;
    }
    case pb::commands::TrackCmd::ACTION_SET_OUTPUT_ROUTING: {
      std::lock_guard<std::mutex> lock(state.tracks_mutex);
      int target_idx = cmd.target_track_index();
      if (WouldCreateRoutingCycle(state, tidx, target_idx)) {
        sendLog("Rejected routing cycle on track " + std::to_string(tidx) +
                " -> " + std::to_string(target_idx));
        sendAck("SET_OUTPUT_ROUTING", false);
        break;
      }
      auto track = GetOrCreateTrack(state, tidx);
      track->output_track_index = target_idx;
      std::vector<std::tuple<int, float, bool>> sends;
      for (const auto& s : track->aux_sends)
        sends.emplace_back(s.aux_track_index, s.level, s.pre_fader);
      sendTrackInfoFull(tidx, track->name, static_cast<int>(track->track_type),
                        track->output_track_index, sends,
                        track->group_parent_index, track->volume, track->pan,
                        true, track->muted, track->soloed, track->eq_low,
                        track->eq_mid, track->eq_high, track->stereo_sep);
      sendAck("SET_OUTPUT_ROUTING", true);
      break;
    }
    case pb::commands::TrackCmd::ACTION_SET_AUX_SEND: {
      std::lock_guard<std::mutex> lock(state.tracks_mutex);
      int aux_idx = cmd.aux_track_index();
      float level = cmd.send_level();
      bool pre = cmd.pre_fader();
      if (level > 0.0f && WouldCreateRoutingCycle(state, tidx, aux_idx)) {
        sendLog("Rejected aux routing cycle on track " + std::to_string(tidx) +
                " -> " + std::to_string(aux_idx));
        sendAck("SET_AUX_SEND", false);
        break;
      }
      auto track = GetOrCreateTrack(state, tidx);
      bool found = false;
      for (auto& send : track->aux_sends) {
        if (send.aux_track_index == aux_idx) {
          send.level = level;
          send.pre_fader = pre;
          found = true;
          break;
        }
      }
      if (!found && level > 0.0f) {
        track->aux_sends.push_back({aux_idx, level, pre});
      }
      track->aux_sends.erase(
          std::remove_if(
              track->aux_sends.begin(), track->aux_sends.end(),
              [](const Track::AuxSend& s) { return s.level <= 0.0f; }),
          track->aux_sends.end());
      std::vector<std::tuple<int, float, bool>> sends;
      for (const auto& s : track->aux_sends)
        sends.emplace_back(s.aux_track_index, s.level, s.pre_fader);
      sendTrackInfoFull(tidx, track->name, static_cast<int>(track->track_type),
                        track->output_track_index, sends,
                        track->group_parent_index, track->volume, track->pan,
                        true, track->muted, track->soloed, track->eq_low,
                        track->eq_mid, track->eq_high, track->stereo_sep);
      sendAck("SET_AUX_SEND", true);
      break;
    }
    case pb::commands::TrackCmd::ACTION_SET_GROUP_PARENT: {
      std::lock_guard<std::mutex> lock(state.tracks_mutex);
      int parent_idx = cmd.target_track_index();
      if (WouldCreateRoutingCycle(state, tidx, parent_idx)) {
        sendLog("Rejected group parent cycle on track " + std::to_string(tidx) +
                " -> " + std::to_string(parent_idx));
        sendAck("SET_GROUP_PARENT", false);
        break;
      }
      auto track = GetOrCreateTrack(state, tidx);
      track->group_parent_index = parent_idx;
      std::vector<std::tuple<int, float, bool>> sends;
      for (const auto& s : track->aux_sends)
        sends.emplace_back(s.aux_track_index, s.level, s.pre_fader);
      sendTrackInfoFull(tidx, track->name, static_cast<int>(track->track_type),
                        track->output_track_index, sends,
                        track->group_parent_index, track->volume, track->pan,
                        true, track->muted, track->soloed, track->eq_low,
                        track->eq_mid, track->eq_high, track->stereo_sep);
      sendAck("SET_GROUP_PARENT", true);
      break;
    }
    case pb::commands::TrackCmd::ACTION_SET_EQ: {
      std::lock_guard<std::mutex> lock(state.tracks_mutex);
      auto track = GetOrCreateTrack(state, tidx);
      track->eq_low = std::max(0.0f, std::min(1.0f, cmd.eq_low()));
      track->eq_mid = std::max(0.0f, std::min(1.0f, cmd.eq_mid()));
      track->eq_high = std::max(0.0f, std::min(1.0f, cmd.eq_high()));
      sendAck("SET_EQ", true);
      break;
    }
    case pb::commands::TrackCmd::ACTION_SET_STEREO_SEP: {
      std::lock_guard<std::mutex> lock(state.tracks_mutex);
      auto track = GetOrCreateTrack(state, tidx);
      track->stereo_sep = std::max(-1.0f, std::min(1.0f, cmd.value()));
      sendAck("SET_STEREO_SEP", true);
      break;
    }
    default:
      break;
  }
}

}  // namespace hibiki
