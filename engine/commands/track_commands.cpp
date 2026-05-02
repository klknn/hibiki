#include <algorithm>
#include <filesystem>
#include <mutex>
#include <string>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "engine/commands/commands.hpp"
#include "engine/core/audio_file.hpp"
#include "engine/core/clip.hpp"
#include "engine/core/midi.hpp"
#include "engine/core/track.hpp"
#include "engine/ipc/ipc.hpp"
#include "pb/commands.pb.h"
#include "pb/notifications.pb.h"

namespace hibiki {

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
      std::lock_guard<std::mutex> lock(state.tracks_mutex);
      history.pushState(CaptureProjectState(state));
      auto track = GetOrCreateTrack(state, tidx);
      if (track->LoadClip(sidx, mpath, is_loop, state.sample_rate)) {
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
            sendTimelineClipInfo(tidx, tcidx, clipname, tc->clip->path,
                                 (float)tc->start_time_sec, duration_for_gui,
                                 tc->clip->waveform_summary, tc->clip->is_loop,
                                 tc->alias_source, li_sec);
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
      std::lock_guard<std::mutex> lock(state.tracks_mutex);
      history.pushState(CaptureProjectState(state));
      GetOrCreateTrack(state, tidx)
          ->AddTimelineClip(path, start, state.bpm, dur_beats,
                            state.sample_rate);
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
              tc->clip ? tc->clip->is_loop : false, tc->alias_source, li_sec_r);
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
          sendTimelineClipInfo(target_tidx, new_cidx, clipname,
                               added->clip->path, (float)added->start_time_sec,
                               duration_for_gui, added->clip->waveform_summary,
                               added->clip->is_loop, added->alias_source,
                               li_sec_c);
        }
      }
      sendAck("COPY_TIMELINE_CLIP", true);
      break;
    }
    case pb::commands::TrackCmd::ACTION_ARM_RECORD: {
      std::lock_guard<std::mutex> lock(state.tracks_mutex);
      auto track = GetOrCreateTrack(state, tidx);
      track->record_armed = !track->record_armed;
      sendAck("ARM_RECORD", true);
      break;
    }
    case pb::commands::TrackCmd::ACTION_SET_INPUT_DEVICE: {
      std::lock_guard<std::mutex> lock(state.tracks_mutex);
      auto track = GetOrCreateTrack(state, tidx);
      track->input_device_id = cmd.input_device_id();
      track->input_channel_start = cmd.input_channel_start();
      track->input_stereo = cmd.input_stereo();
      // Reset existing device so next recording opens with new settings
      track->input_device.reset();
      sendAck("SET_INPUT_DEVICE", true);
      break;
    }
    case pb::commands::TrackCmd::ACTION_SET_MIDI_INPUT: {
      std::lock_guard<std::mutex> lock(state.tracks_mutex);
      auto track = GetOrCreateTrack(state, tidx);
      track->midi_input_device_id = cmd.midi_input_device_id();
      // Reset existing MIDI device so it reopens with new settings
      track->midi_input_device.reset();
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
    default:
      break;
  }
}

}  // namespace hibiki
