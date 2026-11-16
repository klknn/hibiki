#include <algorithm>
#include <filesystem>
#include <mutex>
#include <string>

#include "absl/log/log.h"
#include "engine/audio/midi_input.hpp"
#include "engine/commands/commands.hpp"
#include "engine/core/audio_file.hpp"
#include "engine/core/clip.hpp"
#include "engine/core/midi.hpp"
#include "engine/core/track.hpp"
#include "engine/ipc/ipc.hpp"
#include "pb/commands.pb.h"
#include "pb/notifications.pb.h"

namespace hibiki {

void handleTransportCmd(const pb::commands::TransportCmd& cmd,
                        ProjectState& state) {
  switch (cmd.action()) {
    case pb::commands::TransportCmd::ACTION_PLAY:
      state.is_timeline_playing = true;
      sendAck("PLAY", true);
      break;
    case pb::commands::TransportCmd::ACTION_STOP: {
      std::lock_guard<std::mutex> lock(state.tracks_mutex);
      // Finalize recording if active
      if (state.is_recording) {
        state.is_recording = false;
        // Determine output directory
        std::string out_dir = "/tmp/hibiki";
        if (!state.project_dir.empty()) {
          out_dir =
              (std::filesystem::path(state.project_dir) / "audio").string();
        }
        std::filesystem::create_directories(out_dir);

        for (auto& pair : state.tracks) {
          Track* track = pair.second.get();
          if (!track->record_armed) continue;

          if (track->record_mode == Track::RecordMode::RECORD_MIDI) {
            // Finalize MIDI recording
            if (track->midi_record_buffer.empty()) continue;

            double beats_per_sec = state.bpm / 60.0;
            auto clip = std::make_unique<Clip>();
            clip->type = Clip::Type::MIDI;
            double max_beat = 0;
            for (const auto& tev : track->midi_record_buffer) {
              MidiEvent me;
              me.beats =
                  (tev.time_sec - state.record_start_sec) * beats_per_sec;
              me.channel = tev.event.channel;
              me.note = tev.event.pitch;
              me.velocity = (uint8_t)(tev.event.velocity * 127.0f);
              me.type = tev.event.isNoteOn ? 0x90 : 0x80;
              clip->midi_events.push_back(me);
              if (me.beats > max_beat) max_beat = me.beats;
            }
            // Snap duration up to next bar (4 beats)
            double dur_beats = std::ceil(max_beat / 4.0) * 4.0;
            if (dur_beats < 4.0) dur_beats = 4.0;
            clip->duration_beats = dur_beats;

            static int midi_rec_counter = 0;
            std::string filename = "midi_rec_track" +
                                   std::to_string(pair.first) + "_" +
                                   std::to_string(++midi_rec_counter);
            clip->name = filename;

            // Generate MIDI preview waveform (note bars encoded as triplets)
            std::vector<float> waveform;
            for (const auto& me : clip->midi_events) {
              if (me.type == 0x90 && me.velocity > 0) {
                // Find matching note-off
                double end_beat = dur_beats;
                for (const auto& off : clip->midi_events) {
                  if ((off.type == 0x80 ||
                       (off.type == 0x90 && off.velocity == 0)) &&
                      off.note == me.note && off.beats > me.beats) {
                    end_beat = off.beats;
                    break;
                  }
                }
                float startRatio = (float)(me.beats / dur_beats);
                float pitch = (float)me.note;
                float durRatio = (float)((end_beat - me.beats) / dur_beats);
                waveform.push_back(startRatio);
                waveform.push_back(pitch);
                waveform.push_back(durRatio);
              }
            }
            clip->waveform_summary = waveform;

            double duration_sec = dur_beats / beats_per_sec;
            auto tc = std::make_unique<TimelineClip>();
            tc->start_time_sec = state.record_start_sec;
            tc->duration_beats = dur_beats;
            tc->duration_sec = duration_sec;
            tc->clip = std::move(clip);
            track->timeline_clips.push_back(std::move(tc));
            int clip_idx = (int)track->timeline_clips.size() - 1;

            // Debug: log timing values for MIDI recording
            {
              auto& finalized = track->timeline_clips[clip_idx];
              double first_beat = finalized->clip->midi_events.empty()
                                      ? -1
                                      : finalized->clip->midi_events[0].beats;
              sendLog(
                  "MIDI_REC track=" + std::to_string(pair.first) +
                  " record_start=" +
                  std::to_string(state.record_start_sec.load()) +
                  " clip_start=" + std::to_string(finalized->start_time_sec) +
                  " dur_beats=" + std::to_string(dur_beats) +
                  " dur_sec=" + std::to_string(duration_sec) +
                  " first_event_beat=" + std::to_string(first_beat) +
                  " max_beat=" + std::to_string(max_beat) + " n_events=" +
                  std::to_string(finalized->clip->midi_events.size()));
            }

            sendTimelineClipInfo(
                pair.first, clip_idx, filename, "",
                (float)state.record_start_sec, (float)duration_sec,
                track->timeline_clips[clip_idx]->clip->waveform_summary);

            // If this is a Channel Rack generator track (>= 100), also append
            // recorded notes into the active SequencerPattern so Piano Roll
            // and PAT/SONG sequencer reflect the take immediately.
            if (pair.first >= 100) {
              const int ch_idx = pair.first - 100;
              SequencerPattern* active_pat = nullptr;
              for (auto& pat : state.sequencer_patterns) {
                if (pat.pattern_index == state.selected_pattern_index) {
                  active_pat = &pat;
                  break;
                }
              }
              if (active_pat) {
                auto& dst_notes = active_pat->channels[ch_idx].notes;
                constexpr double kPpq = 480.0;
                std::map<int, std::pair<long, int>> active_notes;
                for (const auto& me :
                     track->timeline_clips[clip_idx]->clip->midi_events) {
                  const long tick = std::max(
                      0L, static_cast<long>(std::llround(me.beats * kPpq)));
                  if (me.type == 0x90 && me.velocity > 0) {
                    active_notes[me.note] = {tick, me.velocity};
                  } else if (me.type == 0x80 ||
                             (me.type == 0x90 && me.velocity == 0)) {
                    auto it = active_notes.find(me.note);
                    if (it != active_notes.end()) {
                      pb::core::MidiEvent pe;
                      pe.set_tick(it->second.first);
                      pe.set_pitch(me.note);
                      pe.set_duration_ticks(
                          std::max(120L, tick - it->second.first));
                      pe.set_velocity(it->second.second);
                      dst_notes.push_back(pe);
                      active_notes.erase(it);
                    }
                  }
                }
                for (const auto& [note, info] : active_notes) {
                  pb::core::MidiEvent pe;
                  pe.set_tick(info.first);
                  pe.set_pitch(note);
                  pe.set_duration_ticks(240);
                  pe.set_velocity(info.second);
                  dst_notes.push_back(pe);
                }
                pb::notifications::SequencerSync sync;
                sync.set_playback_mode(state.playback_mode ==
                                               SequencerPlaybackMode::SONG
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
                  for (const auto& [cidx, data] : pat.channels) {
                    auto* cp = sp->add_channels();
                    cp->set_channel_index(cidx);
                    for (bool b : data.steps) cp->add_steps(b);
                    for (float v : data.step_velocities)
                      cp->add_step_velocities(v);
                    for (float p : data.step_pitches) cp->add_step_pitches(p);
                    for (float pa : data.step_pans) cp->add_step_pans(pa);
                    for (float sh : data.step_shifts) cp->add_step_shifts(sh);
                    for (const auto& ev : data.notes) *cp->add_notes() = ev;
                  }
                }
                sendSequencerSync(sync);
              }
            }

            track->midi_record_buffer.clear();
          } else {
            // Finalize audio recording
            if (track->record_buffer.empty()) continue;

            int rec_channels = track->input_stereo ? 2 : 1;
            int sample_rate = track->input_device
                                  ? track->input_device->get_sample_rate()
                                  : 44100;

            static int rec_counter = 0;
            std::string filename = "recording_track" +
                                   std::to_string(pair.first) + "_" +
                                   std::to_string(++rec_counter) + ".wav";
            std::string filepath =
                (std::filesystem::path(out_dir) / filename).string();

            auto wav_status = SaveWav(filepath, track->record_buffer,
                                      rec_channels, sample_rate);
            if (!wav_status.ok()) {
              LOG(ERROR) << "Record save failed: " << wav_status.message();
            }

            double duration_sec = (double)track->record_buffer.size() /
                                  (rec_channels * sample_rate);
            auto clip = std::make_unique<Clip>();
            clip->type = Clip::Type::AUDIO;
            clip->audio_data = std::move(track->record_buffer);
            clip->num_channels = rec_channels;
            clip->sample_rate = sample_rate;
            clip->duration_sec = duration_sec;
            clip->path = filepath;
            clip->name = filename;
            int summary_size = 200;
            clip->waveform_summary.resize(summary_size, 0.0f);
            int samples_per_bucket =
                (int)clip->audio_data.size() / (rec_channels * summary_size);
            if (samples_per_bucket < 1) samples_per_bucket = 1;
            for (int b = 0; b < summary_size; ++b) {
              float peak = 0.0f;
              for (int s = 0; s < samples_per_bucket; ++s) {
                int idx = (b * samples_per_bucket + s) * rec_channels;
                if (idx < (int)clip->audio_data.size()) {
                  peak = std::max(peak, std::abs(clip->audio_data[idx]));
                }
              }
              clip->waveform_summary[b] = peak;
            }

            auto tc = std::make_unique<TimelineClip>();
            tc->start_time_sec = state.record_start_sec;
            tc->duration_sec = duration_sec;
            tc->clip = std::move(clip);
            track->timeline_clips.push_back(std::move(tc));
            int clip_idx = (int)track->timeline_clips.size() - 1;

            sendTimelineClipInfo(
                pair.first, clip_idx, filename, filepath,
                (float)state.record_start_sec, (float)duration_sec,
                track->timeline_clips[clip_idx]->clip->waveform_summary);

            pb::notifications::Notification notif;
            auto* rf = notif.mutable_recording_finished();
            rf->set_track_index(pair.first);
            rf->set_path(filepath);
            rf->set_clip_index(clip_idx);
            std::string data;
            notif.SerializeToString(&data);
            sendNotification(reinterpret_cast<const uint8_t*>(data.data()),
                             data.size());

            track->input_device.reset();
          }
        }
      }

      state.is_timeline_playing = false;
      for (auto& pair : state.tracks) {
        pair.second->Stop();
      }
      sendAck("STOP", true);
      break;
    }
    case pb::commands::TransportCmd::ACTION_RECORD: {
      std::lock_guard<std::mutex> lock(state.tracks_mutex);
      state.record_start_sec.store(state.playhead_pos_sec.load());
      // Create input devices and pre-reserve buffers for armed tracks
      for (auto& pair : state.tracks) {
        Track* track = pair.second.get();
        if (track->record_armed) {
          if (track->record_mode == Track::RecordMode::RECORD_AUDIO &&
              !track->input_device) {
            int ch = track->input_stereo ? 2 : 1;
            track->input_device = SoundDevice::createInput(
                track->input_device_id, (int)state.sample_rate, ch,
                state.buffer_latency_ms);
            if (!track->input_device->is_ready()) {
              sendLog("Failed to open input device for track " +
                      std::to_string(pair.first));
              track->input_device.reset();
            }
          }
          track->record_buffer.clear();
          track->record_buffer.reserve(44100 * 16);
          track->midi_record_buffer.clear();
          track->midi_record_buffer.reserve(4096);
        }
      }
      state.is_recording = true;
      state.is_timeline_playing = true;
      sendAck("RECORD", true);
      break;
    }
    case pb::commands::TransportCmd::ACTION_SEEK:
      state.playhead_pos_sec = cmd.seek_pos();
      sendAck("SEEK", true);
      break;
    case pb::commands::TransportCmd::ACTION_SET_LOOP:
      state.loop_enabled = cmd.loop_enabled();
      state.loop_start_sec = cmd.loop_start();
      state.loop_end_sec = cmd.loop_end();
      LOG(INFO) << "Loop " << (state.loop_enabled ? "enabled" : "disabled")
                << " [" << state.loop_start_sec << "s, " << state.loop_end_sec
                << "s]";
      sendAck("SET_LOOP", true);
      break;
    case pb::commands::TransportCmd::ACTION_SET_METRONOME:
      state.metronome_enabled = cmd.enabled();
      sendAck("SET_METRONOME", true);
      break;
    case pb::commands::TransportCmd::ACTION_PANIC: {
      std::lock_guard<std::mutex> lock(state.tracks_mutex);
      for (auto& pair : state.tracks) {
        if (pair.second) {
          pair.second->Stop();
          pair.second->panic_requested_.store(true);
        }
      }
      sendAck("PANIC", true);
      break;
    }
    case pb::commands::TransportCmd::ACTION_SET_MASTER_VOLUME:
      state.master_volume = std::clamp(cmd.value(), 0.0f, 1.5f);
      sendAck("SET_MASTER_VOLUME", true);
      break;
    case pb::commands::TransportCmd::ACTION_SET_MASTER_PITCH:
      state.master_pitch = std::clamp(cmd.value(), -12.0f, 12.0f);
      sendAck("SET_MASTER_PITCH", true);
      break;
    default:
      break;
  }
}

}  // namespace hibiki
