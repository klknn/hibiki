#pragma once

#include <gtest/gtest.h>

#include <memory>
#include <utility>
#include <vector>

#include "engine/core/clip.hpp"
#include "engine/core/history.hpp"
#include "engine/core/midi.hpp"
#include "engine/core/project.hpp"
#include "engine/core/track.hpp"
#include "pb/core.pb.h"

namespace hibiki {
namespace test {

// Build a MIDI clip with note-on/off pairs from {pitch, beats} pairs.
// Each note gets velocity=100, duration=0.25 beats.
inline std::unique_ptr<Clip> MakeMidiClip(
    std::vector<std::pair<int, double>> notes, double note_duration = 0.25) {
  auto clip = std::make_unique<Clip>();
  clip->type = Clip::Type::MIDI;
  clip->path = "in_memory.mid";
  double max_beats = 0.0;
  for (const auto& [pitch, beats] : notes) {
    MidiEvent on;
    on.beats = beats;
    on.type = 0x90;
    on.channel = 0;
    on.note = static_cast<uint8_t>(pitch);
    on.velocity = 100;
    clip->midi_events.push_back(on);

    MidiEvent off;
    off.beats = beats + note_duration;
    off.type = 0x80;
    off.channel = 0;
    off.note = static_cast<uint8_t>(pitch);
    off.velocity = 0;
    clip->midi_events.push_back(off);

    max_beats = std::max(max_beats, beats + note_duration);
  }
  std::sort(
      clip->midi_events.begin(), clip->midi_events.end(),
      [](const MidiEvent& a, const MidiEvent& b) { return a.beats < b.beats; });
  clip->duration_beats = max_beats + 0.1;
  return clip;
}

// Build a stub audio clip with zero-filled data.
inline std::unique_ptr<Clip> MakeAudioClip(int samples = 1000, int channels = 2,
                                           double sr = 44100.0) {
  auto clip = std::make_unique<Clip>();
  clip->type = Clip::Type::AUDIO;
  clip->path = "in_memory.wav";
  clip->audio_data.resize(samples * channels, 0.0f);
  clip->num_channels = channels;
  clip->sample_rate = sr;
  clip->duration_sec = static_cast<double>(samples) / sr;
  return clip;
}

// Build an automation clip with breakpoints from {beats, value} pairs.
inline std::unique_ptr<Clip> MakeAutomationClip(
    std::vector<std::pair<float, float>> points) {
  auto clip = std::make_unique<Clip>();
  clip->type = Clip::Type::AUTOMATION;
  for (const auto& [beats, value] : points) {
    pb::core::AutomationPoint pt;
    pt.set_time_beats(beats);
    pt.set_value(value);
    clip->automation_points.push_back(pt);
  }
  if (!points.empty()) {
    clip->duration_beats = points.back().first + 0.1;
  }
  return clip;
}

// Add a track with a MIDI clip in session slot 0.
inline Track* AddMidiTrack(ProjectState& state, int tidx,
                           std::vector<std::pair<int, double>> notes) {
  auto* track = GetOrCreateTrack(state, tidx);
  track->clips[0] = MakeMidiClip(std::move(notes));
  return track;
}

// Add a track with a MIDI clip on the timeline at the given position.
inline Track* AddTimelineMidiTrack(ProjectState& state, int tidx,
                                   std::vector<std::pair<int, double>> notes,
                                   double start_sec = 0.0) {
  auto* track = GetOrCreateTrack(state, tidx);
  auto tc = std::make_unique<TimelineClip>();
  tc->clip = MakeMidiClip(std::move(notes));
  tc->start_time_sec = start_sec;
  tc->duration_beats = tc->clip->duration_beats;
  track->timeline_clips.push_back(std::move(tc));
  return track;
}

// Count note-on events in a clip's midi_events.
inline int CountNoteOns(const Clip& clip) {
  int count = 0;
  for (const auto& ev : clip.midi_events) {
    if (isNoteOn(ev)) ++count;
  }
  return count;
}

// Helper: undo one step and apply to state.
inline bool UndoOnce(HistoryManager& history, ProjectState& state) {
  auto current = CaptureProjectState(state);
  std::vector<uint8_t> prev;
  if (!history.undo(current, prev)) return false;
  return ApplyProjectState(state, prev).ok();
}

// Helper: redo one step and apply to state.
inline bool RedoOnce(HistoryManager& history, ProjectState& state) {
  auto current = CaptureProjectState(state);
  std::vector<uint8_t> next;
  if (!history.redo(current, next)) return false;
  return ApplyProjectState(state, next).ok();
}

}  // namespace test
}  // namespace hibiki
