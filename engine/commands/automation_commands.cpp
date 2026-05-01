#include "engine/commands/commands.hpp"

#include <algorithm>
#include <mutex>
#include <string>

#include "engine/core/clip.hpp"
#include "engine/core/track.hpp"
#include "engine/ipc/ipc.hpp"
#include "pb/commands.pb.h"
#include "pb/core.pb.h"

namespace hibiki {

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

}  // namespace hibiki
