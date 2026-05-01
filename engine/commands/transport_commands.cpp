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
                  " record_start=" + std::to_string(state.record_start_sec) +
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
      state.record_start_sec = state.playhead_pos_sec;
      // Create input devices for armed tracks
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
          track->midi_record_buffer.clear();
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
    default:
      break;
  }
}

}  // namespace hibiki
