#include "project.hpp"

#include <fstream>
#include <iostream>

#include "audio_file.hpp"
#include "ipc.hpp"
#include "pb/commands.pb.h"
#include "pb/core.pb.h"
#include "pb/notifications.pb.h"

namespace hibiki {

Track* GetOrCreateTrack(ProjectState& state, int track_index) {
  if (state.tracks.find(track_index) == state.tracks.end()) {
    state.tracks[track_index] = std::make_unique<Track>(track_index);
  }
  return state.tracks[track_index].get();
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

    for (const auto& plugin : track->plugins) {
      auto* ps = ts->add_plugins();
      ps->set_path(plugin->getPath());
      ps->set_plugin_index(plugin->getPluginIndex());
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
    }

    for (const auto& [slot, clip] : track->clips) {
      auto* ss = ts->add_session_slots();
      ss->set_slot_index(slot);
      auto* cs = ss->mutable_clip();
      cs->set_path(clip->path);
      cs->set_is_loop(clip->is_loop);
      cs->set_type(clip->type == Clip::Type::MIDI
                       ? hibiki::pb::core::CLIP_TYPE_MIDI
                       : hibiki::pb::core::CLIP_TYPE_AUDIO);
    }

    for (const auto& tc : track->timeline_clips) {
      if (!tc->clip) continue;
      auto* tcs = ts->add_timeline_clips();
      auto* clip = tcs->mutable_clip();
      clip->set_path(tc->clip->path);
      tcs->set_start_time_sec(tc->start_time_sec);
      clip->set_duration_sec(tc->duration_sec);
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
      sendTrackInfo(track_data.track_index(), track->name);
    }

    for (const auto& plugin_data : track_data.plugins()) {
      if (plugin_data.path().empty()) continue;
      int pidx = track->LoadPlugin(
          plugin_data.path(), plugin_data.plugin_index(), state.sample_rate);
      if (pidx >= 0) {
        for (const auto& param_data : plugin_data.params()) {
          track->plugins[pidx]->setParameterValue(param_data.id(),
                                                  param_data.current_value());
        }
      }
    }

    for (const auto& slot_data : track_data.session_slots()) {
      if (!slot_data.has_clip() || slot_data.clip().path().empty()) continue;
      track->LoadClip(slot_data.slot_index(), slot_data.clip().path(),
                      slot_data.clip().is_loop());
    }

    for (const auto& tc_data : track_data.timeline_clips()) {
      if (!tc_data.has_clip() || tc_data.clip().path().empty()) continue;
      auto tc = std::make_unique<TimelineClip>();
      tc->clip = hibiki::LoadClip(tc_data.clip().path());
      tc->start_time_sec = tc_data.start_time_sec();
      tc->duration_sec =
          tc->clip ? tc->clip->duration_sec : tc_data.clip().duration_sec();
      tc->duration_beats = tc->clip ? tc->clip->duration_beats : 0.0;
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
}

bool SaveProject(const ProjectState& state, const std::string& path) {
  hibiki::pb::core::Project project = BuildProjectProto(state);

  std::ofstream out(path, std::ios::binary);
  if (!out) {
    std::cerr << "Failed to open project file for writing: " << path << "\n";
    return false;
  }
  return project.SerializeToOstream(&out);
}

bool LoadProject(ProjectState& state, const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    std::cerr << "Failed to open project file for reading: " << path << "\n";
    return false;
  }

  hibiki::pb::core::Project project;
  if (!project.ParseFromIstream(&in)) {
    std::cerr << "Failed to parse project file: " << path << "\n";
    return false;
  }

  LoadTracksFromProto(state, project);
  return true;
}

std::vector<uint8_t> CaptureProjectState(const ProjectState& state) {
  hibiki::pb::core::Project project = BuildProjectProto(state);
  std::string data;
  project.SerializeToString(&data);
  return std::vector<uint8_t>(data.begin(), data.end());
}

bool ApplyProjectState(ProjectState& state, const std::vector<uint8_t>& data) {
  if (data.empty()) return false;
  hibiki::pb::core::Project project;
  if (!project.ParseFromArray(data.data(), data.size())) return false;
  LoadTracksFromProto(state, project);
  return true;
}

void SyncProjectToGui(const ProjectState& state) {
  hibiki::sendClearProject();
  for (const auto& [tidx, track] : state.tracks) {
    // Sync Session Clips
    for (const auto& [sidx, clip] : track->clips) {
      std::string cname = clip->path;
      size_t last_slash = cname.find_last_of("/\\");
      if (last_slash != std::string::npos) {
        cname = cname.substr(last_slash + 1);
      }
      hibiki::sendClipInfo(tidx, sidx, cname, clip->path);
    }
    // Sync Plugins
    for (int pidx = 0; pidx < (int)track->plugins.size(); ++pidx) {
      auto& plugin = track->plugins[pidx];
      std::vector<VstParamInfo> params;
      for (int i = 0; i < plugin->getParameterCount(); ++i) {
        VstParamInfo info;
        if (plugin->getParameterInfo(i, info)) {
          params.push_back(info);
        }
      }
      hibiki::sendParamList(tidx, pidx, plugin->getName(),
                            plugin->isInstrument(), params);
    }
    // Sync Timeline Clips
    for (int tc_idx = 0; tc_idx < (int)track->timeline_clips.size(); ++tc_idx) {
      const auto& tc = track->timeline_clips[tc_idx];
      if (!tc->clip) continue;
      std::string cname = tc->clip->path;
      size_t last_slash = cname.find_last_of("/\\");
      if (last_slash != std::string::npos) {
        cname = cname.substr(last_slash + 1);
      }
      // For MIDI clips, convert duration_beats to seconds using project BPM
      float duration_for_gui =
          (tc->duration_beats > 0)
              ? (float)(tc->duration_beats * 60.0 / state.bpm)
              : (float)tc->duration_sec;
      hibiki::sendTimelineClipInfo(tidx, tc_idx, cname, tc->clip->path,
                                   tc->start_time_sec, duration_for_gui,
                                   tc->clip->waveform_summary);
    }
    // Sync Automation Lanes
    if (!track->automation_lanes.empty()) {
      hibiki::sendAutomationLanesData(tidx, track->automation_lanes,
                                      track->plugins);
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
  return max_duration > 0.0 ? max_duration + 2.0 : 0.0;
}

void BounceProject(ProjectState& live_state, const std::string& path) {
  std::vector<uint8_t> snapshot;
  double duration = 0.0;
  {
    std::lock_guard<std::mutex> lock(live_state.tracks_mutex);
    snapshot = CaptureProjectState(live_state);
    duration = GetProjectDuration(live_state);
  }

  if (duration <= 0.0) {
    hibiki::sendBounceFinished(path, false);
    return;
  }

  ProjectState state;
  state.sample_rate = live_state.sample_rate;
  ApplyProjectState(state, snapshot);
  state.is_timeline_playing = true;
  state.playhead_pos_sec = 0.0;

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
  float* outChannels[] = {bufferL, bufferR};

  double time_per_block = block_size / (double)sample_rate;

  while (state.playhead_pos_sec < duration) {
    std::vector<float> mixBufferL(block_size, 0.0f);
    std::vector<float> mixBufferR(block_size, 0.0f);

    for (auto& pair : state.tracks) {
      Track* track = pair.second.get();
      std::fill(bufferL, bufferL + block_size, 0.0f);
      std::fill(bufferR, bufferR + block_size, 0.0f);

      for (const auto& tc : track->timeline_clips) {
        // Get clip duration - use duration_beats for MIDI clips, duration_sec
        // for audio
        double clip_duration = (tc->duration_beats > 0)
                                   ? tc->duration_beats * 60.0 / state.bpm
                                   : tc->duration_sec;
        if (state.playhead_pos_sec + time_per_block > tc->start_time_sec &&
            state.playhead_pos_sec < tc->start_time_sec + clip_duration) {
          double clip_local_time = state.playhead_pos_sec - tc->start_time_sec;

          if (tc->clip->type == Clip::Type::MIDI) {
            std::vector<MidiNoteEvent> blockEvents;
            double beats_per_sec =
                state.bpm / 60.0;  // Convert beats to seconds
            // Convert clip_local_time to beats for comparison
            double window_start_beats = clip_local_time * beats_per_sec;
            double window_end_beats =
                (clip_local_time + time_per_block) * beats_per_sec;
            for (const auto& me : tc->clip->midi_events) {
              if (me.beats >= window_start_beats &&
                  me.beats < window_end_beats) {
                MidiNoteEvent e;
                double event_local_sec =
                    me.beats / beats_per_sec - clip_local_time;
                e.sampleOffset =
                    std::max(0, (int)(event_local_sec * sample_rate));
                if (e.sampleOffset >= block_size)
                  e.sampleOffset = block_size - 1;
                e.channel = me.channel;
                e.pitch = me.note;
                e.isNoteOn = hibiki::isNoteOn(me);
                e.velocity = e.isNoteOn ? me.velocity / 127.0f : 0.0f;
                blockEvents.push_back(e);
              }
            }
            if (!track->plugins.empty() && track->plugins[0]->isInstrument()) {
              track->plugins[0]->process(nullptr, outChannels, block_size,
                                         context, blockEvents);
            }
          } else {
            int start_sample = (int)(clip_local_time * sample_rate);
            for (int i = 0; i < block_size; ++i) {
              int sample_pos = start_sample + i;
              if (sample_pos < 0) continue;
              if (tc->clip->num_channels == 2 &&
                  sample_pos * 2 + 1 < (int)tc->clip->audio_data.size()) {
                bufferL[i] += tc->clip->audio_data[sample_pos * 2];
                bufferR[i] += tc->clip->audio_data[sample_pos * 2 + 1];
              } else if (tc->clip->num_channels == 1 &&
                         sample_pos < (int)tc->clip->audio_data.size()) {
                float s = tc->clip->audio_data[sample_pos];
                bufferL[i] += s;
                bufferR[i] += s;
              }
            }
          }
        }
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
        track->plugins[i]->process(outChannels, outChannels, block_size,
                                   context, {});
      }

      for (int i = 0; i < block_size; ++i) {
        mixBufferL[i] += bufferL[i];
        mixBufferR[i] += bufferR[i];
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

  bool success = SaveWav(path, output_buffer, actual_channels, sample_rate);
  hibiki::sendBounceFinished(path, success);
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
