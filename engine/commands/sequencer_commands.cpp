#include <algorithm>
#include <mutex>

#include "engine/commands/commands.hpp"
#include "engine/ipc/ipc.hpp"

namespace hibiki {

namespace {

SequencerChannel* findOrCreateChannel(ProjectState& state, int ch_idx) {
  for (auto& ch : state.sequencer_channels) {
    if (ch.channel_index == ch_idx) return &ch;
  }
  SequencerChannel ch;
  ch.channel_index = ch_idx;
  state.sequencer_channels.push_back(ch);
  return &state.sequencer_channels.back();
}

SequencerPattern* findOrCreatePattern(ProjectState& state, int pat_idx) {
  for (auto& pat : state.sequencer_patterns) {
    if (pat.pattern_index == pat_idx) return &pat;
  }
  SequencerPattern pat;
  pat.pattern_index = pat_idx;
  pat.name = "Pattern " + std::to_string(pat_idx + 1);
  state.sequencer_patterns.push_back(pat);
  return &state.sequencer_patterns.back();
}

Track* syncChannelGeneratorTrack(ProjectState& state,
                                 const SequencerChannel& ch) {
  const int trk_idx = 100 + ch.channel_index;
  Track* trk = GetOrCreateTrack(state, trk_idx);
  trk->track_type = Track::TrackType::NORMAL;
  if (!ch.name.empty()) trk->name = ch.name;
  trk->output_track_index = ch.mixer_insert;
  trk->volume = ch.volume;
  trk->pan = ch.pan;
  trk->muted = ch.muted;
  trk->soloed = ch.soloed;
  return trk;
}

}  // namespace

void handleSequencerCmd(const pb::commands::SequencerCmd& cmd,
                        ProjectState& state, HistoryManager& /*history*/) {
  std::lock_guard<std::mutex> lock(state.tracks_mutex);
  const int pat_idx = static_cast<int>(cmd.pattern_index());
  const int ch_idx = static_cast<int>(cmd.channel_index());

  switch (cmd.action()) {
    case pb::commands::SequencerCmd::ACTION_SET_PLAYBACK_MODE: {
      state.playback_mode =
          (cmd.playback_mode() == pb::core::PLAYBACK_MODE_SONG)
              ? SequencerPlaybackMode::SONG
              : SequencerPlaybackMode::PAT;
      sendAck("SEQ_SET_PLAYBACK_MODE", true);
      break;
    }
    case pb::commands::SequencerCmd::ACTION_SET_SWING: {
      state.swing_amount = std::clamp(cmd.value(), 0.0f, 1.0f);
      sendAck("SEQ_SET_SWING", true);
      break;
    }
    case pb::commands::SequencerCmd::ACTION_SELECT_PATTERN: {
      state.selected_pattern_index = std::max(0, pat_idx);
      findOrCreatePattern(state, state.selected_pattern_index);
      sendAck("SEQ_SELECT_PATTERN", true);
      break;
    }
    case pb::commands::SequencerCmd::ACTION_ADD_PATTERN: {
      SequencerPattern* pat = findOrCreatePattern(state, pat_idx);
      if (!cmd.name().empty()) pat->name = cmd.name();
      if (cmd.color() != 0) pat->color = cmd.color();
      if (cmd.length_steps() > 0) pat->length_steps = cmd.length_steps();
      sendAck("SEQ_ADD_PATTERN", true);
      break;
    }
    case pb::commands::SequencerCmd::ACTION_ADD_CHANNEL: {
      SequencerChannel* ch = findOrCreateChannel(state, ch_idx);
      if (!cmd.name().empty()) ch->name = cmd.name();
      if (!cmd.uri().empty()) ch->uri = cmd.uri();
      if (!cmd.category().empty()) ch->category = cmd.category();
      if (cmd.color() != 0) ch->color = cmd.color();
      ch->mixer_insert = cmd.mixer_insert();
      Track* trk = syncChannelGeneratorTrack(state, *ch);
      if (!ch->uri.empty() &&
          (trk->plugins.empty() || trk->plugins[0]->getPath() != ch->uri)) {
        trk->LoadPlugin(ch->uri, 0, state.sample_rate, state.plugin_host_mode);
      }
      sendAck("SEQ_ADD_CHANNEL", true);
      break;
    }
    case pb::commands::SequencerCmd::ACTION_SET_CHANNEL_ROUTE: {
      SequencerChannel* ch = findOrCreateChannel(state, ch_idx);
      ch->mixer_insert = cmd.mixer_insert();
      syncChannelGeneratorTrack(state, *ch);
      sendAck("SEQ_SET_CHANNEL_ROUTE", true);
      break;
    }
    case pb::commands::SequencerCmd::ACTION_SET_CHANNEL_MUTE: {
      SequencerChannel* ch = findOrCreateChannel(state, ch_idx);
      ch->muted = cmd.enabled();
      syncChannelGeneratorTrack(state, *ch);
      sendAck("SEQ_SET_CHANNEL_MUTE", true);
      break;
    }
    case pb::commands::SequencerCmd::ACTION_SET_CHANNEL_SOLO: {
      SequencerChannel* ch = findOrCreateChannel(state, ch_idx);
      ch->soloed = cmd.enabled();
      syncChannelGeneratorTrack(state, *ch);
      sendAck("SEQ_SET_CHANNEL_SOLO", true);
      break;
    }
    case pb::commands::SequencerCmd::ACTION_SET_CHANNEL_VOLUME: {
      SequencerChannel* ch = findOrCreateChannel(state, ch_idx);
      ch->volume = std::clamp(cmd.value(), 0.0f, 1.5f);
      syncChannelGeneratorTrack(state, *ch);
      sendAck("SEQ_SET_CHANNEL_VOLUME", true);
      break;
    }
    case pb::commands::SequencerCmd::ACTION_SET_CHANNEL_PAN: {
      SequencerChannel* ch = findOrCreateChannel(state, ch_idx);
      ch->pan = std::clamp(cmd.value(), -1.0f, 1.0f);
      syncChannelGeneratorTrack(state, *ch);
      sendAck("SEQ_SET_CHANNEL_PAN", true);
      break;
    }
    case pb::commands::SequencerCmd::ACTION_SET_STEP: {
      SequencerPattern* pat = findOrCreatePattern(state, pat_idx);
      auto& data = pat->channels[ch_idx];
      const int sidx = static_cast<int>(cmd.step_index());
      if (sidx >= 0 && sidx < 16) {
        data.steps[sidx] = cmd.enabled();
      }
      sendAck("SEQ_SET_STEP", true);
      break;
    }
    case pb::commands::SequencerCmd::ACTION_SET_STEP_GRAPH: {
      SequencerPattern* pat = findOrCreatePattern(state, pat_idx);
      auto& data = pat->channels[ch_idx];
      const int sidx = static_cast<int>(cmd.step_index());
      if (sidx >= 0 && sidx < 16) {
        switch (cmd.graph_param()) {
          case 0:
            data.step_velocities[sidx] = std::clamp(cmd.value(), 0.0f, 1.0f);
            break;
          case 1:
            data.step_pitches[sidx] = std::clamp(cmd.value(), -1.0f, 1.0f);
            break;
          case 2:
            data.step_pans[sidx] = std::clamp(cmd.value(), -1.0f, 1.0f);
            break;
          case 3:
            data.step_shifts[sidx] = std::clamp(cmd.value(), 0.0f, 1.0f);
            break;
          default:
            break;
        }
      }
      sendAck("SEQ_SET_STEP_GRAPH", true);
      break;
    }
    case pb::commands::SequencerCmd::ACTION_SET_CHANNEL_NOTES: {
      SequencerPattern* pat = findOrCreatePattern(state, pat_idx);
      auto& data = pat->channels[ch_idx];
      data.notes.assign(cmd.notes().begin(), cmd.notes().end());
      sendAck("SEQ_SET_CHANNEL_NOTES", true);
      break;
    }
    default:
      break;
  }
}

}  // namespace hibiki
