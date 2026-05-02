#include <algorithm>
#include <map>
#include <mutex>
#include <string>

#include "absl/log/check.h"
#include "engine/commands/commands.hpp"
#include "engine/core/clip.hpp"
#include "engine/core/midi.hpp"
#include "engine/core/track.hpp"
#include "engine/ipc/ipc.hpp"
#include "pb/commands.pb.h"
#include "pb/core.pb.h"

namespace hibiki {

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
            CHECK_GT(state.bpm, 0);
            float duration_for_gui =
                (tc->duration_sec > 0)
                    ? (float)tc->duration_sec
                    : (float)beatsToSec(clip->duration_beats, state.bpm);
            std::string clipname = pathBasename(clip->path);
            float li_sec_u =
                (tc->loop_interval_beats > 0)
                    ? (float)beatsToSec(tc->loop_interval_beats, state.bpm)
                    : 0.0f;
            sendTimelineClipInfo(tidx, cidx, clipname, clip->path,
                                 (float)tc->start_time_sec, duration_for_gui,
                                 clip->waveform_summary, clip->is_loop,
                                 tc->alias_source, li_sec_u);
            // Propagate MIDI edits to alias clips
            for (int ai = 0; ai < (int)track->timeline_clips.size(); ++ai) {
              if (ai == cidx) continue;
              auto& atc = track->timeline_clips[ai];
              if (atc && atc->alias_source == cidx && atc->clip) {
                atc->clip->midi_events = clip->midi_events;
                atc->clip->duration_beats = clip->duration_beats;
                atc->clip->waveform_summary = clip->waveform_summary;
                float alias_dur =
                    (atc->duration_sec > 0)
                        ? (float)atc->duration_sec
                        : (float)beatsToSec(atc->clip->duration_beats,
                                            state.bpm);
                std::string aname = pathBasename(clip->path);
                float ali_sec =
                    (atc->loop_interval_beats > 0)
                        ? (float)beatsToSec(atc->loop_interval_beats, state.bpm)
                        : 0.0f;
                sendTimelineClipInfo(tidx, ai, aname, atc->clip->path,
                                     (float)atc->start_time_sec, alias_dur,
                                     atc->clip->waveform_summary,
                                     atc->clip->is_loop, atc->alias_source,
                                     ali_sec);
              }
            }
          }
        }
        sendAck("UPDATE_CLIP_MIDI", true);
      } else
        sendAck("UPDATE_CLIP_MIDI", false);
      break;
    }
    case pb::commands::MidiCmd::ACTION_PANIC: {
      std::lock_guard<std::mutex> lock(state.tracks_mutex);
      for (auto& pair : state.tracks) {
        pair.second->Panic();
      }
      sendAck("MIDI_PANIC", true);
      break;
    }
    default:
      break;
  }
}

void handleSendVirtualMidi(const pb::commands::SendVirtualMidi& cmd,
                           ProjectState& state) {
  int tidx = cmd.track_index();
  std::lock_guard<std::mutex> lock(state.tracks_mutex);
  if (!state.tracks.count(tidx)) return;
  auto& track = state.tracks[tidx];
  MidiNoteEvent ev;
  ev.sampleOffset = 0;
  ev.channel = 0;
  ev.pitch = static_cast<uint8_t>(cmd.note());
  ev.velocity = cmd.velocity() / 127.0f;
  ev.isNoteOn = cmd.note_on();
  {
    std::lock_guard<std::mutex> mlock(track->virtual_midi_mutex);
    track->virtual_midi_queue.push_back(ev);
  }
}

}  // namespace hibiki
