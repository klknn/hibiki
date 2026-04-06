#include "commands.hpp"

#include <algorithm>
#include <iostream>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "audio_file.hpp"
#include "clip.hpp"
#include "ipc.hpp"
#include "midi.hpp"
#include "pb/commands.pb.h"
#include "pb/core.pb.h"
#include "pb/notifications.pb.h"
#include "pb/plugin_worker.pb.h"
#include "tcp.hpp"
#include "track.hpp"
#include "vst3_host.hpp"

namespace hibiki {

void handleProjectCmd(const pb::commands::ProjectCmd& cmd, ProjectState& state,
                      HistoryManager& history) {
  switch (cmd.action()) {
    case pb::commands::ProjectCmd::ACTION_SAVE: {
      std::lock_guard<std::mutex> lock(state.tracks_mutex);
      SaveProject(state, cmd.path());
      sendAck("SAVE_PROJECT", true);
      break;
    }
    case pb::commands::ProjectCmd::ACTION_LOAD: {
      std::lock_guard<std::mutex> lock(state.tracks_mutex);
      history.pushState(CaptureProjectState(state));
      LoadProject(state, cmd.path());
      SyncProjectToGui(state);
      sendAck("LOAD_PROJECT", true);
      break;
    }
    case pb::commands::ProjectCmd::ACTION_BOUNCE: {
      std::string path = cmd.path();
      std::thread([&state, path]() { BounceProject(state, path); }).detach();
      break;
    }
    case pb::commands::ProjectCmd::ACTION_UNDO: {
      std::lock_guard<std::mutex> lock(state.tracks_mutex);
      std::vector<uint8_t> current = CaptureProjectState(state);
      std::vector<uint8_t> prev;
      if (history.undo(current, prev)) {
        ApplyProjectState(state, prev);
        SyncProjectToGui(state);
        sendAck("UNDO", true);
      } else {
        sendAck("UNDO", false);
      }
      break;
    }
    case pb::commands::ProjectCmd::ACTION_REDO: {
      std::lock_guard<std::mutex> lock(state.tracks_mutex);
      std::vector<uint8_t> current = CaptureProjectState(state);
      std::vector<uint8_t> next;
      if (history.redo(current, next)) {
        ApplyProjectState(state, next);
        SyncProjectToGui(state);
        sendAck("REDO", true);
      } else {
        sendAck("REDO", false);
      }
      break;
    }
    case pb::commands::ProjectCmd::ACTION_QUIT: {
      state.quit = true;
      break;
    }
    case pb::commands::ProjectCmd::ACTION_SET_BPM: {
      state.bpm = cmd.bpm();
      sendAck("SET_BPM", true);
      break;
    }
    default:
      break;
  }
}

void handleTransportCmd(const pb::commands::TransportCmd& cmd,
                        ProjectState& state) {
  switch (cmd.action()) {
    case pb::commands::TransportCmd::ACTION_PLAY:
      state.is_timeline_playing = true;
      sendAck("PLAY", true);
      break;
    case pb::commands::TransportCmd::ACTION_STOP: {
      std::lock_guard<std::mutex> lock(state.tracks_mutex);
      state.is_timeline_playing = false;
      for (auto& pair : state.tracks) {
        pair.second->Stop();
      }
      sendAck("STOP", true);
      break;
    }
    case pb::commands::TransportCmd::ACTION_SEEK:
      state.playhead_pos_sec = cmd.seek_pos();
      sendAck("SEEK", true);
      break;
    default:
      break;
  }
}

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
      if (track->LoadClip(sidx, mpath, is_loop)) {
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
      GetOrCreateTrack(state, tidx)
          ->SetClipLoop(cmd.target().session_slot(), cmd.flag());
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
          ->AddTimelineClip(path, start, state.bpm, dur_beats);
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
      float dur_beats = cmd.value();
      std::lock_guard<std::mutex> lock(state.tracks_mutex);
      history.pushState(CaptureProjectState(state));
      if (state.tracks.count(tidx)) {
        auto& track = state.tracks[tidx];
        if (cidx >= 0 && cidx < (int)track->timeline_clips.size() &&
            track->timeline_clips[cidx]) {
          auto& tc = track->timeline_clips[cidx];
          tc->duration_beats = dur_beats;
          if (tc->clip) tc->clip->duration_beats = dur_beats;
          float duration_for_gui =
              (float)(dur_beats * 60.0 / (state.bpm > 0 ? state.bpm : 120.0));
          std::string clipname = tc->clip ? tc->clip->path : "";
          if (clipname.empty()) clipname = "New Clip";
          size_t pos = clipname.find_last_of("/\\");
          if (pos != std::string::npos) clipname = clipname.substr(pos + 1);
          sendTimelineClipInfo(
              tidx, cidx, clipname, tc->clip ? tc->clip->path : "",
              (float)tc->start_time_sec, duration_for_gui,
              tc->clip ? tc->clip->waveform_summary : std::vector<float>{});
        }
      }
      sendAck("RESIZE_TIMELINE_CLIP", true);
      break;
    }
    default:
      break;
  }
}

void handlePluginCmd(const pb::commands::PluginCmd& cmd, ProjectState& state,
                     HistoryManager& history) {
  int tidx = cmd.target().track_index();
  switch (cmd.action()) {
    case pb::commands::PluginCmd::ACTION_LOAD: {
      std::string vpath = cmd.path();
      int pidx = cmd.target().plugin_index();
      std::lock_guard<std::mutex> lock(state.tracks_mutex);
      history.pushState(CaptureProjectState(state));
      auto track = GetOrCreateTrack(state, tidx);
      int target_idx =
          track->LoadPlugin(vpath, pidx, state.sample_rate,
                            state.plugin_host_mode, state.remote_hosts);
      if (target_idx != -1) {
        std::vector<VstParamInfo> params;
        auto& plugin = track->plugins[target_idx];
        for (int i = 0; i < plugin->getParameterCount(); ++i) {
          VstParamInfo info;
          if (plugin->getParameterInfo(i, info)) params.push_back(info);
        }
        sendParamList(tidx, target_idx, plugin->getName(),
                      plugin->isInstrument(), params);
      } else {
        sendLog("Failed to load plugin: " + vpath);
      }
      break;
    }
    case pb::commands::PluginCmd::ACTION_REMOVE: {
      int pidx = cmd.target().plugin_index();
      std::lock_guard<std::mutex> lock(state.tracks_mutex);
      history.pushState(CaptureProjectState(state));
      auto track = GetOrCreateTrack(state, tidx);
      sendAck("REMOVE_PLUGIN", track->RemovePlugin(pidx));
      break;
    }
    case pb::commands::PluginCmd::ACTION_SHOW_GUI: {
      int pidx = cmd.target().plugin_index();
      std::lock_guard<std::mutex> lock(state.tracks_mutex);
      if (state.tracks.count(tidx)) {
        auto& plugins = state.tracks[tidx]->plugins;
        if (pidx >= 0 && pidx < (int)plugins.size()) {
          plugins[pidx]->showEditor();
          sendAck("SHOW_PLUGIN_GUI", true);
        } else
          sendAck("SHOW_PLUGIN_GUI", false);
      } else
        sendAck("SHOW_PLUGIN_GUI", false);
      break;
    }
    case pb::commands::PluginCmd::ACTION_SET_PARAM: {
      int pidx = cmd.target().plugin_index();
      uint32_t param_id = cmd.param_id();
      float value = cmd.param_value();
      std::lock_guard<std::mutex> lock(state.tracks_mutex);
      if (state.tracks.count(tidx)) {
        auto& plugins = state.tracks[tidx]->plugins;
        if (pidx >= 0 && pidx < (int)plugins.size()) {
          plugins[pidx]->setParameterValue(param_id, value);
          sendParamValueChange(tidx, pidx, param_id, value);
        }
      }
      break;
    }
    case pb::commands::PluginCmd::ACTION_LIST: {
      std::string path = cmd.path();
      std::thread([path]() {
        sendPluginList(path, Vst3Plugin::listPluginsIsolated(path));
      }).detach();
      break;
    }
    case pb::commands::PluginCmd::ACTION_GET_EDITOR_FRAME: {
      int pidx = cmd.target().plugin_index();
      std::lock_guard<std::mutex> lock(state.tracks_mutex);
      if (state.tracks.count(tidx)) {
        auto& plugins = state.tracks[tidx]->plugins;
        if (pidx >= 0 && pidx < (int)plugins.size()) {
          std::vector<uint8_t> rgba;
          int w = 0, h = 0;
          if (plugins[pidx]->captureEditorFrame(rgba, w, h)) {
            sendEditorFrameData(tidx, pidx, w, h, rgba);
          }
        }
      }
      break;
    }
    case pb::commands::PluginCmd::ACTION_SEND_EDITOR_INPUT: {
      int pidx = cmd.target().plugin_index();
      std::lock_guard<std::mutex> lock(state.tracks_mutex);
      if (state.tracks.count(tidx)) {
        auto& plugins = state.tracks[tidx]->plugins;
        if (pidx >= 0 && pidx < (int)plugins.size()) {
          plugins[pidx]->sendEditorInput(cmd.input_type(), cmd.input_x(),
                                         cmd.input_y(), cmd.input_button(),
                                         cmd.input_key(), cmd.input_delta());
        }
      }
      break;
    }
    case pb::commands::PluginCmd::ACTION_STOP_GUI: {
      int pidx = cmd.target().plugin_index();
      std::lock_guard<std::mutex> lock(state.tracks_mutex);
      if (state.tracks.count(tidx)) {
        auto& plugins = state.tracks[tidx]->plugins;
        if (pidx >= 0 && pidx < (int)plugins.size()) {
          plugins[pidx]->stopEditor();
          sendAck("STOP_PLUGIN_GUI", true);
        } else
          sendAck("STOP_PLUGIN_GUI", false);
      } else
        sendAck("STOP_PLUGIN_GUI", false);
      break;
    }
    default:
      break;
  }
}

void handleAutomationCmd(const pb::commands::AutomationCmd& cmd,
                         ProjectState& state, HistoryManager& history) {
  int tidx = cmd.target().track_index();
  switch (cmd.action()) {
    case pb::commands::AutomationCmd::ACTION_ADD_LANE: {
      int pidx = cmd.target().plugin_index();
      uint32_t param_id = cmd.param_id();
      std::lock_guard<std::mutex> lock(state.tracks_mutex);
      history.pushState(CaptureProjectState(state));
      auto track = GetOrCreateTrack(state, tidx);
      track->AddAutomationLane(pidx, param_id);
      sendAutomationLanesData(tidx, track->automation_lanes, track->plugins);
      sendAck("ADD_AUTOMATION_LANE", true);
      break;
    }
    case pb::commands::AutomationCmd::ACTION_REMOVE_LANE: {
      int lidx = cmd.target().lane_index();
      std::lock_guard<std::mutex> lock(state.tracks_mutex);
      history.pushState(CaptureProjectState(state));
      auto track = GetOrCreateTrack(state, tidx);
      track->RemoveAutomationLane(lidx);
      sendAutomationLanesData(tidx, track->automation_lanes, track->plugins);
      sendAck("REMOVE_AUTOMATION_LANE", true);
      break;
    }
    case pb::commands::AutomationCmd::ACTION_UPDATE_POINTS: {
      int lidx = cmd.target().lane_index();
      std::lock_guard<std::mutex> lock(state.tracks_mutex);
      history.pushState(CaptureProjectState(state));
      if (state.tracks.count(tidx)) {
        auto& track = state.tracks[tidx];
        if (lidx >= 0 && lidx < (int)track->automation_lanes.size()) {
          auto& lane = track->automation_lanes[lidx];
          int clip_idx = cmd.clip_index();
          if (clip_idx >= 0 && clip_idx < (int)lane.clips.size() &&
              lane.clips[clip_idx] && lane.clips[clip_idx]->clip) {
            auto& points_dest = lane.clips[clip_idx]->clip->automation_points;
            points_dest.clear();
            for (const auto& pt : cmd.points()) {
              pb::core::AutomationPoint p;
              p.set_time_beats(pt.time_beats());
              p.set_value(std::max(0.0f, std::min(1.0f, pt.value())));
              p.set_tension(std::max(-1.0f, std::min(1.0f, pt.tension())));
              points_dest.push_back(p);
            }
            std::sort(points_dest.begin(), points_dest.end(),
                      [](const pb::core::AutomationPoint& a,
                         const pb::core::AutomationPoint& b) {
                        return a.time_beats() < b.time_beats();
                      });
            sendAutomationLanesData(tidx, track->automation_lanes,
                                    track->plugins);
            sendAck("UPDATE_AUTOMATION_LANE", true);
          } else {
            sendAck("UPDATE_AUTOMATION_LANE", false);
          }
        } else {
          sendAck("UPDATE_AUTOMATION_LANE", false);
        }
      } else {
        sendAck("UPDATE_AUTOMATION_LANE", false);
      }
      break;
    }
    case pb::commands::AutomationCmd::ACTION_ADD_CLIP: {
      int lidx = cmd.target().lane_index();
      std::lock_guard<std::mutex> lock(state.tracks_mutex);
      history.pushState(CaptureProjectState(state));
      if (state.tracks.count(tidx)) {
        auto& track = state.tracks[tidx];
        if (lidx >= 0 && lidx < (int)track->automation_lanes.size()) {
          auto& lane = track->automation_lanes[lidx];
          auto tc = std::make_unique<TimelineClip>();
          tc->clip = std::make_unique<Clip>();
          tc->clip->type = Clip::Type::AUTOMATION;
          tc->start_time_sec = cmd.start_time_sec();
          tc->duration_beats = cmd.duration_beats();
          lane.clips.push_back(std::move(tc));
          sendAutomationLanesData(tidx, track->automation_lanes,
                                  track->plugins);
          sendAck("ADD_AUTOMATION_CLIP", true);
        } else {
          sendAck("ADD_AUTOMATION_CLIP", false);
        }
      } else {
        sendAck("ADD_AUTOMATION_CLIP", false);
      }
      break;
    }
    case pb::commands::AutomationCmd::ACTION_REMOVE_CLIP: {
      int lidx = cmd.target().lane_index();
      int clip_idx = cmd.clip_index();
      std::lock_guard<std::mutex> lock(state.tracks_mutex);
      history.pushState(CaptureProjectState(state));
      if (state.tracks.count(tidx)) {
        auto& track = state.tracks[tidx];
        if (lidx >= 0 && lidx < (int)track->automation_lanes.size()) {
          auto& lane = track->automation_lanes[lidx];
          if (clip_idx >= 0 && clip_idx < (int)lane.clips.size()) {
            lane.clips.erase(lane.clips.begin() + clip_idx);
            sendAutomationLanesData(tidx, track->automation_lanes,
                                    track->plugins);
            sendAck("REMOVE_AUTOMATION_CLIP", true);
          } else {
            sendAck("REMOVE_AUTOMATION_CLIP", false);
          }
        } else {
          sendAck("REMOVE_AUTOMATION_CLIP", false);
        }
      } else {
        sendAck("REMOVE_AUTOMATION_CLIP", false);
      }
      break;
    }
    case pb::commands::AutomationCmd::ACTION_MOVE_CLIP: {
      int lidx = cmd.target().lane_index();
      int clip_idx = cmd.clip_index();
      std::lock_guard<std::mutex> lock(state.tracks_mutex);
      history.pushState(CaptureProjectState(state));
      if (state.tracks.count(tidx)) {
        auto& track = state.tracks[tidx];
        if (lidx >= 0 && lidx < (int)track->automation_lanes.size()) {
          auto& lane = track->automation_lanes[lidx];
          if (clip_idx >= 0 && clip_idx < (int)lane.clips.size()) {
            lane.clips[clip_idx]->start_time_sec = cmd.start_time_sec();
            sendAutomationLanesData(tidx, track->automation_lanes,
                                    track->plugins);
            sendAck("MOVE_AUTOMATION_CLIP", true);
          } else {
            sendAck("MOVE_AUTOMATION_CLIP", false);
          }
        } else {
          sendAck("MOVE_AUTOMATION_CLIP", false);
        }
      } else {
        sendAck("MOVE_AUTOMATION_CLIP", false);
      }
      break;
    }
    case pb::commands::AutomationCmd::ACTION_RESIZE_CLIP: {
      int lidx = cmd.target().lane_index();
      int clip_idx = cmd.clip_index();
      std::lock_guard<std::mutex> lock(state.tracks_mutex);
      history.pushState(CaptureProjectState(state));
      if (state.tracks.count(tidx)) {
        auto& track = state.tracks[tidx];
        if (lidx >= 0 && lidx < (int)track->automation_lanes.size()) {
          auto& lane = track->automation_lanes[lidx];
          if (clip_idx >= 0 && clip_idx < (int)lane.clips.size()) {
            lane.clips[clip_idx]->duration_beats = cmd.duration_beats();
            if (lane.clips[clip_idx]->clip) {
              lane.clips[clip_idx]->clip->duration_beats = cmd.duration_beats();
            }
            sendAutomationLanesData(tidx, track->automation_lanes,
                                    track->plugins);
            sendAck("RESIZE_AUTOMATION_CLIP", true);
          } else {
            sendAck("RESIZE_AUTOMATION_CLIP", false);
          }
        } else {
          sendAck("RESIZE_AUTOMATION_CLIP", false);
        }
      } else {
        sendAck("RESIZE_AUTOMATION_CLIP", false);
      }
      break;
    }
    case pb::commands::AutomationCmd::ACTION_RENAME_CLIP: {
      int lidx = cmd.target().lane_index();
      int clip_idx = cmd.clip_index();
      std::lock_guard<std::mutex> lock(state.tracks_mutex);
      history.pushState(CaptureProjectState(state));
      if (state.tracks.count(tidx)) {
        auto& track = state.tracks[tidx];
        if (lidx >= 0 && lidx < (int)track->automation_lanes.size()) {
          auto& lane = track->automation_lanes[lidx];
          if (clip_idx >= 0 && clip_idx < (int)lane.clips.size()) {
            if (lane.clips[clip_idx]->clip) {
              lane.clips[clip_idx]->clip->name = cmd.clip_name();
            }
            sendAutomationLanesData(tidx, track->automation_lanes,
                                    track->plugins);
            sendAck("RENAME_AUTOMATION_CLIP", true);
          } else {
            sendAck("RENAME_AUTOMATION_CLIP", false);
          }
        } else {
          sendAck("RENAME_AUTOMATION_CLIP", false);
        }
      } else {
        sendAck("RENAME_AUTOMATION_CLIP", false);
      }
      break;
    }
    case pb::commands::AutomationCmd::ACTION_GET_LANES: {
      std::lock_guard<std::mutex> lock(state.tracks_mutex);
      if (state.tracks.count(tidx)) {
        auto& track = state.tracks[tidx];
        sendAutomationLanesData(tidx, track->automation_lanes, track->plugins);
        sendAck("GET_AUTOMATION_LANES", true);
      } else
        sendAck("GET_AUTOMATION_LANES", false);
      break;
    }
    default:
      break;
  }
}

void handleMidiCmd(const pb::commands::MidiCmd& cmd, ProjectState& state,
                   HistoryManager& history) {
  int tidx = cmd.target().track_index();
  int sidx = cmd.target().session_slot();
  int cidx = cmd.target().timeline_clip();
  switch (cmd.action()) {
    case pb::commands::MidiCmd::ACTION_GET: {
      std::lock_guard<std::mutex> lock(state.tracks_mutex);
      Clip* clip = nullptr;
      if (state.tracks.count(tidx)) {
        auto& track = state.tracks[tidx];
        if (sidx >= 0 && track->clips.count(sidx) && track->clips[sidx]) {
          clip = track->clips[sidx].get();
        } else if (cidx >= 0 && cidx < (int)track->timeline_clips.size() &&
                   track->timeline_clips[cidx]) {
          clip = track->timeline_clips[cidx]->clip.get();
        }
      }
      if (clip && clip->type == Clip::Type::MIDI) {
        int ppq = 480;
        std::vector<pb::core::MidiEvent> notes;
        std::map<int, std::pair<long, int>> active_notes;
        for (const auto& ev : clip->midi_events) {
          long tick = (long)(ev.beats * ppq);
          if (isNoteOn(ev)) {
            active_notes[ev.note] = {tick, ev.velocity};
          } else if (isNoteOff(ev)) {
            if (active_notes.count(ev.note)) {
              auto [start_tick, vel] = active_notes[ev.note];
              pb::core::MidiEvent me;
              me.set_tick(start_tick);
              me.set_pitch(ev.note);
              me.set_duration_ticks(tick - start_tick);
              me.set_velocity(vel);
              notes.push_back(me);
              active_notes.erase(ev.note);
            }
          }
        }
        sendClipMidiData(tidx, sidx, cidx, ppq, notes);
        sendAck("GET_CLIP_MIDI", true);
      } else
        sendAck("GET_CLIP_MIDI", false);
      break;
    }
    case pb::commands::MidiCmd::ACTION_UPDATE: {
      int ppq = cmd.resolution();
      if (ppq <= 0) ppq = 480;
      std::lock_guard<std::mutex> lock(state.tracks_mutex);
      history.pushState(CaptureProjectState(state));
      Clip* clip = nullptr;
      if (state.tracks.count(tidx)) {
        auto& track = state.tracks[tidx];
        if (sidx >= 0 && track->clips.count(sidx) && track->clips[sidx]) {
          clip = track->clips[sidx].get();
        } else if (cidx >= 0 && cidx < (int)track->timeline_clips.size() &&
                   track->timeline_clips[cidx]) {
          clip = track->timeline_clips[cidx]->clip.get();
        }
      }
      if (clip && clip->type == Clip::Type::MIDI) {
        clip->midi_events.clear();
        for (const auto& ev : cmd.events()) {
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
        if (cidx >= 0 && state.tracks.count(tidx)) {
          auto& track = state.tracks[tidx];
          if (cidx < (int)track->timeline_clips.size() &&
              track->timeline_clips[cidx]) {
            auto& tc = track->timeline_clips[cidx];
            if (!clip->midi_events.empty()) {
              double note_end = clip->midi_events.back().beats + 0.1;
              clip->duration_beats = std::max(clip->duration_beats, note_end);
            }
            tc->duration_beats = clip->duration_beats;
            clip->waveform_summary.clear();
            double total_beats =
                clip->duration_beats > 0 ? clip->duration_beats : 1.0;
            for (size_t i = 0; i < clip->midi_events.size(); ++i) {
              auto& ev = clip->midi_events[i];
              if (isNoteOn(ev)) {
                double duration = 0.1;
                for (size_t j = i + 1; j < clip->midi_events.size(); ++j) {
                  auto& off_ev = clip->midi_events[j];
                  if (off_ev.note == ev.note && off_ev.channel == ev.channel &&
                      isNoteOff(off_ev)) {
                    duration = off_ev.beats - ev.beats;
                    break;
                  }
                }
                clip->waveform_summary.push_back(
                    (float)(ev.beats / total_beats));
                clip->waveform_summary.push_back((float)ev.note);
                clip->waveform_summary.push_back(
                    (float)(duration / total_beats));
              }
            }
            float duration_for_gui =
                (tc->duration_sec > 0)
                    ? (float)tc->duration_sec
                    : (float)(clip->duration_beats * 60.0 /
                              (state.bpm > 0 ? state.bpm : 120.0));
            std::string clipname = clip->path;
            size_t pos = clipname.find_last_of("/\\");
            if (pos != std::string::npos) clipname = clipname.substr(pos + 1);
            sendTimelineClipInfo(tidx, cidx, clipname, clip->path,
                                 (float)tc->start_time_sec, duration_for_gui,
                                 clip->waveform_summary);
          }
        }
        sendAck("UPDATE_CLIP_MIDI", true);
      } else
        sendAck("UPDATE_CLIP_MIDI", false);
      break;
    }
    default:
      break;
  }
}

void handleSetPluginHostMode(const pb::commands::SetPluginHostMode& cmd,
                             ProjectState& state) {
  switch (cmd.mode()) {
    case pb::commands::PLUGIN_HOST_LOCAL_SANDBOX:
      state.plugin_host_mode = PluginHostMode::LOCAL_SANDBOX;
      break;
    case pb::commands::PLUGIN_HOST_REMOTE:
      state.plugin_host_mode = PluginHostMode::REMOTE;
      state.remote_hosts.clear();
      for (const auto& host : cmd.remote_hosts()) {
        state.remote_hosts.push_back(host);
      }
      break;
    case pb::commands::PLUGIN_HOST_IN_PROCESS:
    default:
      state.plugin_host_mode = PluginHostMode::IN_PROCESS;
      break;
  }
  sendAck("SET_PLUGIN_HOST_MODE", true);
}

void handleSetAudioBufferSize(const pb::commands::SetAudioBufferSize& cmd,
                              ProjectState& state) {
  int ms = cmd.buffer_size_ms();
  if (ms < 10) ms = 10;
  if (ms > 2000) ms = 2000;
  state.buffer_latency_ms = ms;
  std::cerr << "Audio buffer size set to " << ms << " ms (restart to apply)\n";
  sendAck("SET_AUDIO_BUFFER_SIZE", true);
}

void handleScanRemotePlugins(const pb::commands::ScanRemotePlugins& cmd) {
  // Query each remote daemon for its plugin list in parallel.
  for (const auto& host_port : cmd.remote_hosts()) {
    std::string hp = host_port;
    std::thread([hp]() {
      std::string host = hp;
      int port = 9100;
      auto colon = hp.rfind(':');
      if (colon != std::string::npos) {
        host = hp.substr(0, colon);
        port = std::stoi(hp.substr(colon + 1));
      }

      tcp_init();

      socket_t fd = socket(AF_INET, SOCK_STREAM, 0);
      if (fd == INVALID_SOCK) {
        std::cerr << "ScanRemote: socket() failed for " << hp << "\n";
        return;
      }

      struct sockaddr_in addr;
      memset(&addr, 0, sizeof(addr));
      addr.sin_family = AF_INET;
      addr.sin_port = htons(port);

      // Resolve hostname
      struct hostent* he = gethostbyname(host.c_str());
      if (!he) {
        std::cerr << "ScanRemote: cannot resolve " << host << "\n";
        tcp_close(fd);
        return;
      }
      memcpy(&addr.sin_addr, he->h_addr_list[0], he->h_length);

      if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        std::cerr << "ScanRemote: connect failed for " << hp << "\n";
        tcp_close(fd);
        return;
      }

      // Helper: length-prefixed send/recv
      auto tcpSend = [&](const std::string& data) -> bool {
        uint32_t size = static_cast<uint32_t>(data.size());
        const uint8_t* p = reinterpret_cast<const uint8_t*>(&size);
        size_t rem = sizeof(size);
        while (rem > 0) {
          int n = tcp_send(fd, p, rem);
          if (n <= 0) return false;
          p += n;
          rem -= n;
        }
        p = reinterpret_cast<const uint8_t*>(data.data());
        rem = data.size();
        while (rem > 0) {
          int n = tcp_send(fd, p, rem);
          if (n <= 0) return false;
          p += n;
          rem -= n;
        }
        return true;
      };

      auto tcpRecv = [&](std::string& out) -> bool {
        uint32_t size = 0;
        uint8_t* p = reinterpret_cast<uint8_t*>(&size);
        size_t rem = sizeof(size);
        while (rem > 0) {
          int n = tcp_recv(fd, p, rem);
          if (n <= 0) return false;
          p += n;
          rem -= n;
        }
        if (size > 4 * 1024 * 1024) return false;
        out.resize(size);
        p = reinterpret_cast<uint8_t*>(out.data());
        rem = size;
        while (rem > 0) {
          int n = tcp_recv(fd, p, rem);
          if (n <= 0) return false;
          p += n;
          rem -= n;
        }
        return true;
      };

      // Send ListPlugins request
      pb::worker::WorkerRequest req;
      auto* lp = req.mutable_list_plugins();
      lp->set_search_path("/");  // daemon scans its local paths

      std::string req_data;
      req.SerializeToString(&req_data);
      if (!tcpSend(req_data)) {
        std::cerr << "ScanRemote: send failed for " << hp << "\n";
        tcp_close(fd);
        return;
      }

      std::string resp_data;
      if (!tcpRecv(resp_data)) {
        std::cerr << "ScanRemote: recv failed for " << hp << "\n";
        tcp_close(fd);
        return;
      }

      pb::worker::WorkerResponse resp;
      if (!resp.ParseFromString(resp_data) ||
          resp.result_case() !=
              pb::worker::WorkerResponse::kListPluginsResult) {
        std::cerr << "ScanRemote: bad response from " << hp << "\n";
        tcp_close(fd);
        return;
      }

      // Send shutdown to be polite
      pb::worker::WorkerRequest shutdown_req;
      shutdown_req.mutable_shutdown();
      std::string sd;
      shutdown_req.SerializeToString(&sd);
      tcpSend(sd);
      tcp_close(fd);

      // Convert to PluginListResponse notification
      pb::notifications::PluginListResponse plr;
      plr.set_path("/");
      plr.set_remote_host(hp);
      for (const auto& pi : resp.list_plugins_result().plugins()) {
        auto* pd = plr.add_plugins();
        pd->set_index(pi.plugin_index());
        pd->set_name(pi.name());
        pd->set_vendor("");  // worker proto doesn't have vendor
        pd->set_path(pi.path());  // actual .vst3 bundle path on remote
      }

      pb::notifications::Notification notif;
      *notif.mutable_plugin_list() = plr;
      std::string notif_data;
      notif.SerializeToString(&notif_data);
      sendNotification(reinterpret_cast<const uint8_t*>(notif_data.data()),
                       notif_data.size());

      std::cerr << "ScanRemote: found " << plr.plugins_size() << " plugins on "
                << hp << "\n";
    }).detach();
  }
  sendAck("SCAN_REMOTE_PLUGINS", true);
}

}  // namespace hibiki
