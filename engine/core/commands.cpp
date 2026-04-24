#include "engine/core/commands.hpp"

#include <google/protobuf/text_format.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "absl/log/log.h"
#include "engine/audio/midi_input.hpp"
#include "engine/core/audio_file.hpp"
#include "engine/core/clip.hpp"
#include "engine/core/midi.hpp"
#include "engine/core/modulator.hpp"
#include "engine/core/track.hpp"
#include "engine/instruments/builtin_sampler.hpp"
#include "engine/ipc/ipc.hpp"
#include "engine/ipc/tcp.hpp"
#include "engine/plugin/plugin_scanner.hpp"
#include "pb/commands.pb.h"
#include "pb/core.pb.h"
#include "pb/notifications.pb.h"
#include "pb/plugin_worker.pb.h"

namespace hibiki {

void handleProjectCmd(const pb::commands::ProjectCmd& cmd, ProjectState& state,
                      HistoryManager& history) {
  switch (cmd.action()) {
    case pb::commands::ProjectCmd::ACTION_SAVE: {
      std::lock_guard<std::mutex> lock(state.tracks_mutex);
      // Copy /tmp/hibiki recordings to audio/ subdir next to project file
      std::string project_path = cmd.path();
      std::filesystem::path proj_dir =
          std::filesystem::path(project_path).parent_path();
      state.project_dir = proj_dir.string();
      std::filesystem::path audio_dir = proj_dir / "audio";
      std::filesystem::path tmp_dir = "/tmp/hibiki";
      if (std::filesystem::exists(tmp_dir)) {
        std::filesystem::create_directories(audio_dir);
        for (auto& [tidx, track] : state.tracks) {
          for (auto& tc : track->timeline_clips) {
            if (!tc || !tc->clip) continue;
            std::string cpath = tc->clip->path;
            if (cpath.find("/tmp/hibiki/") == 0) {
              std::filesystem::path src(cpath);
              std::filesystem::path dst = audio_dir / src.filename();
              std::error_code ec;
              std::filesystem::copy_file(
                  src, dst, std::filesystem::copy_options::overwrite_existing,
                  ec);
              if (!ec) {
                tc->clip->path = dst.string();
              }
            }
          }
        }
      }
      auto save_status = SaveProject(state, project_path);
      if (!save_status.ok()) {
        LOG(ERROR) << "Save failed: " << save_status.message();
      }
      sendAck("SAVE_PROJECT", save_status.ok());
      break;
    }
    case pb::commands::ProjectCmd::ACTION_LOAD: {
      std::lock_guard<std::mutex> lock(state.tracks_mutex);
      history.pushState(CaptureProjectState(state));
      auto load_status = LoadProject(state, cmd.path());
      if (!load_status.ok()) {
        LOG(ERROR) << "Load failed: " << load_status.message();
      }
      SyncProjectToGui(state);
      sendAck("LOAD_PROJECT", load_status.ok());
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
          float duration_for_gui = (float)(tc->duration_beats * 60.0 /
                                           (state.bpm > 0 ? state.bpm : 120.0));
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

void handleListAudioInputs() {
  auto devices = SoundDevice::listInputDevices();
  pb::notifications::Notification notif;
  auto* list = notif.mutable_audio_input_list();
  for (const auto& dev : devices) {
    auto* d = list->add_devices();
    d->set_id(dev.id);
    d->set_name(dev.name);
    d->set_channel_count(dev.channel_count);
  }
  std::string data;
  notif.SerializeToString(&data);
  sendNotification(reinterpret_cast<const uint8_t*>(data.data()), data.size());
}

void handleListMidiInputs() {
  auto devices = MidiInput::listDevices();
  pb::notifications::Notification notif;
  auto* list = notif.mutable_midi_input_list();
  for (const auto& dev : devices) {
    auto* d = list->add_devices();
    d->set_id(dev.id);
    d->set_name(dev.name);
    d->set_port_count(dev.port_count);
  }
  std::string data;
  notif.SerializeToString(&data);
  sendNotification(reinterpret_cast<const uint8_t*>(data.data()), data.size());
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

void handlePluginCmd(const pb::commands::PluginCmd& cmd, ProjectState& state,
                     HistoryManager& history) {
  int tidx = cmd.target().track_index();
  switch (cmd.action()) {
    case pb::commands::PluginCmd::ACTION_LOAD: {
      std::string vpath = cmd.path();
      int pidx = cmd.target().plugin_index();
      // Displaced plugin must be destroyed OUTSIDE tracks_mutex to avoid
      // blocking the audio thread during VST3 teardown (editor thread join,
      // COM release, etc.)
      std::unique_ptr<IPlugin> displaced;
      {
        std::lock_guard<std::mutex> lock(state.tracks_mutex);
        history.pushState(CaptureProjectState(state));
        auto track = GetOrCreateTrack(state, tidx);
        LOG(INFO) << "BACKEND: Loading plugin: " << vpath;
        sendLog("Loading plugin: " + vpath + " ...");
        auto result =
            track->LoadPlugin(vpath, pidx, state.sample_rate,
                              state.plugin_host_mode, cmd.remote_host());
        displaced = std::move(result.displaced);
        int target_idx = result.index;
        if (target_idx != -1) {
          std::vector<VstParamInfo> params;
          auto& plugin = track->plugins[target_idx];
          for (int i = 0; i < plugin->getParameterCount(); ++i) {
            VstParamInfo info;
            if (plugin->getParameterInfo(i, info)) params.push_back(info);
          }
          sendParamList(tidx, target_idx, plugin->getName(),
                        plugin->isInstrument(), params);
          // If instrument was inserted at front, re-send param lists for all
          // shifted effect plugins so Java panel indices stay in sync.
          if (plugin->isInstrument() && !displaced) {
            for (int i = 0; i < (int)track->plugins.size(); ++i) {
              if (i == target_idx) continue;
              auto& p = track->plugins[i];
              std::vector<VstParamInfo> ep;
              for (int j = 0; j < p->getParameterCount(); ++j) {
                VstParamInfo info;
                if (p->getParameterInfo(j, info)) ep.push_back(info);
              }
              sendParamList(tidx, i, p->getName(), p->isInstrument(), ep);
            }
          }
        } else {
          sendLog("Failed to load plugin: " + vpath);
        }
      }
      // `displaced` destroyed here, outside the mutex
      break;
    }
    case pb::commands::PluginCmd::ACTION_REMOVE: {
      int pidx = cmd.target().plugin_index();
      std::unique_ptr<IPlugin> removed;
      {
        std::lock_guard<std::mutex> lock(state.tracks_mutex);
        history.pushState(CaptureProjectState(state));
        auto track = GetOrCreateTrack(state, tidx);
        removed = track->RemovePlugin(pidx);
        sendAck("REMOVE_PLUGIN", removed != nullptr);
        if (removed) {
          // Notify Java: send empty param list for removed index
          sendParamList(tidx, pidx, "", false, {});
          // Re-send param lists for all remaining plugins at their new indices
          for (int i = 0; i < (int)track->plugins.size(); ++i) {
            auto& p = track->plugins[i];
            std::vector<VstParamInfo> params;
            for (int j = 0; j < p->getParameterCount(); ++j) {
              VstParamInfo info;
              if (p->getParameterInfo(j, info)) params.push_back(info);
            }
            sendParamList(tidx, i, p->getName(), p->isInstrument(), params);
          }
        }
      }
      // `removed` destroyed here, outside the mutex
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
      // Batch mode: scan multiple bundles in parallel
      if (cmd.paths_size() > 0) {
        std::vector<std::string> bundles;
        for (const auto& p : cmd.paths()) {
          bundles.push_back(p);
        }
        std::thread([bundles]() {
          scanBundlesParallel(
              bundles, Vst3Plugin::listPluginsIsolated,
              [](const std::string& path,
                 const std::vector<PluginDescription>& plugins) {
                sendPluginList(path, plugins);
              });
        }).detach();
      } else {
        // Single path fallback
        std::string path = cmd.path();
        std::thread([path]() {
          sendPluginList(path, Vst3Plugin::listPluginsIsolated(path));
        }).detach();
      }
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
    case pb::commands::PluginCmd::ACTION_LOAD_SAMPLE: {
      int pidx = cmd.target().plugin_index();
      std::string sample_path = cmd.sample_path();
      std::lock_guard<std::mutex> lock(state.tracks_mutex);
      if (state.tracks.count(tidx)) {
        auto& plugins = state.tracks[tidx]->plugins;
        if (pidx >= 0 && pidx < (int)plugins.size()) {
          auto* sampler = dynamic_cast<BuiltinSampler*>(plugins[pidx].get());
          if (sampler && sampler->loadSample(sample_path)) {
            pb::notifications::Notification notification;
            auto* sd = notification.mutable_plugin_sample_data();
            sd->set_track_index(tidx);
            sd->set_plugin_index(pidx);
            for (float v : sampler->getWaveformSummary()) {
              sd->add_waveform(v);
            }
            auto slash = sample_path.rfind('/');
            sd->set_sample_name(slash != std::string::npos
                                    ? sample_path.substr(slash + 1)
                                    : sample_path);
            std::string data;
            notification.SerializeToString(&data);
            sendNotification(reinterpret_cast<const uint8_t*>(data.data()),
                             data.size());
            sendAck("LOAD_SAMPLE", true);
          } else {
            sendAck("LOAD_SAMPLE", false);
          }
        } else {
          sendAck("LOAD_SAMPLE", false);
        }
      } else {
        sendAck("LOAD_SAMPLE", false);
      }
      break;
    }
    case pb::commands::PluginCmd::ACTION_SET_SIDECHAIN: {
      int pidx = cmd.target().plugin_index();
      int sc_tidx = cmd.sidechain_track_index();
      std::lock_guard<std::mutex> lock(state.tracks_mutex);
      if (state.tracks.count(tidx)) {
        auto& track = state.tracks[tidx];
        if (sc_tidx < 0) {
          track->plugin_sidechain.erase(pidx);
        } else {
          track->plugin_sidechain[pidx] = {sc_tidx};
        }
        sendAck("SET_SIDECHAIN", true);
      } else {
        sendAck("SET_SIDECHAIN", false);
      }
      break;
    }
    case pb::commands::PluginCmd::ACTION_SET_BYPASS: {
      int pidx = cmd.target().plugin_index();
      bool bypassed = !cmd.flag();  // flag=true means "on", so bypassed = !on
      std::lock_guard<std::mutex> lock(state.tracks_mutex);
      if (state.tracks.count(tidx)) {
        auto& track = state.tracks[tidx];
        if (bypassed) {
          track->plugin_bypass[pidx] = true;
        } else {
          track->plugin_bypass.erase(pidx);
        }
        LOG(INFO) << "Plugin " << pidx << " on track " << tidx
                  << (bypassed ? " bypassed" : " enabled");
      }
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

void handleSetPluginHostMode(const pb::commands::SetPluginHostMode& cmd,
                             ProjectState& state) {
  switch (cmd.mode()) {
    case pb::commands::PLUGIN_HOST_LOCAL_SANDBOX:
      state.plugin_host_mode = PluginHostMode::LOCAL_SANDBOX;
      break;
    case pb::commands::PLUGIN_HOST_IN_PROCESS:
    default:
      state.plugin_host_mode = PluginHostMode::IN_PROCESS;
      break;
  }
  // Always update remote hosts list (independent of local mode)
  state.remote_hosts.clear();
  for (const auto& host : cmd.remote_hosts()) {
    state.remote_hosts.push_back(host);
  }
  sendAck("SET_PLUGIN_HOST_MODE", true);
  saveConfig(state);
}

void handleSetAudioBufferSize(const pb::commands::SetAudioBufferSize& cmd,
                              ProjectState& state) {
  int ms = cmd.buffer_size_ms();
  if (ms < 10) ms = 10;
  if (ms > 2000) ms = 2000;
  state.buffer_latency_ms = ms;
  LOG(INFO) << "Audio buffer size set to " << ms << " ms (restart to apply)";
  sendAck("SET_AUDIO_BUFFER_SIZE", true);
  saveConfig(state);
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
        LOG(ERROR) << "ScanRemote: socket() failed for " << hp;
        return;
      }

      struct sockaddr_in addr;
      memset(&addr, 0, sizeof(addr));
      addr.sin_family = AF_INET;
      addr.sin_port = htons(port);

      // Resolve hostname
      struct hostent* he = gethostbyname(host.c_str());
      if (!he) {
        LOG(INFO) << "ScanRemote: cannot resolve " << host;
        tcp_close(fd);
        return;
      }
      memcpy(&addr.sin_addr, he->h_addr_list[0], he->h_length);

      if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        LOG(ERROR) << "ScanRemote: connect failed for " << hp;
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
        LOG(ERROR) << "ScanRemote: send failed for " << hp;
        tcp_close(fd);
        return;
      }

      // Read streamed plugin chunks until is_complete=true
      int total_plugins = 0;
      while (true) {
        std::string resp_data;
        if (!tcpRecv(resp_data)) {
          LOG(ERROR) << "ScanRemote: recv failed for " << hp;
          break;
        }

        pb::worker::WorkerResponse resp;
        if (!resp.ParseFromString(resp_data) ||
            resp.result_case() !=
                pb::worker::WorkerResponse::kListPluginsResult) {
          LOG(INFO) << "ScanRemote: bad response from " << hp;
          break;
        }

        const auto& chunk = resp.list_plugins_result();

        // Forward this chunk as a notification (even if empty)
        if (chunk.plugins_size() > 0) {
          pb::notifications::PluginListResponse plr;
          plr.set_path("/");
          plr.set_remote_host(hp);
          for (const auto& pi : chunk.plugins()) {
            auto* pd = plr.add_plugins();
            pd->set_index(pi.plugin_index());
            pd->set_name(pi.name());
            pd->set_vendor("");  // worker proto doesn't have vendor
            pd->set_path(pi.path());
          }
          total_plugins += chunk.plugins_size();

          pb::notifications::Notification notif;
          *notif.mutable_plugin_list() = plr;
          std::string notif_data;
          notif.SerializeToString(&notif_data);
          sendNotification(reinterpret_cast<const uint8_t*>(notif_data.data()),
                           notif_data.size());
        }

        if (chunk.is_complete()) break;
      }

      LOG(INFO) << "ScanRemote: found " << total_plugins << " plugins on "
                << hp;

      // Send shutdown to be polite, then close
      pb::worker::WorkerRequest shutdown_req;
      shutdown_req.mutable_shutdown();
      std::string sd;
      shutdown_req.SerializeToString(&sd);
      tcpSend(sd);
      tcp_close(fd);
    }).detach();
  }
  sendAck("SCAN_REMOTE_PLUGINS", true);
}

void loadConfig(ProjectState& state) {
  std::ifstream in(kConfigFile);
  if (!in.is_open()) {
    LOG(INFO) << "No config file found (" << kConfigFile << "), using defaults";
    return;
  }
  std::string content((std::istreambuf_iterator<char>(in)),
                      std::istreambuf_iterator<char>());
  pb::commands::HibikiConfig config;
  if (!google::protobuf::TextFormat::ParseFromString(content, &config)) {
    LOG(ERROR) << "Failed to parse " << kConfigFile << ", using defaults";
    return;
  }
  // Apply config to state
  state.plugin_host_mode =
      (config.plugin_host_mode() == pb::commands::PLUGIN_HOST_LOCAL_SANDBOX)
          ? PluginHostMode::LOCAL_SANDBOX
          : PluginHostMode::IN_PROCESS;
  state.remote_hosts.clear();
  for (const auto& host : config.remote_hosts()) {
    state.remote_hosts.push_back(host);
  }
  if (config.buffer_latency_ms() > 0) {
    state.buffer_latency_ms = config.buffer_latency_ms();
  }
  LOG(INFO) << "Loaded config from " << kConfigFile;
}

void saveConfig(const ProjectState& state) {
  pb::commands::HibikiConfig config;
  config.set_plugin_host_mode(
      (state.plugin_host_mode == PluginHostMode::LOCAL_SANDBOX)
          ? pb::commands::PLUGIN_HOST_LOCAL_SANDBOX
          : pb::commands::PLUGIN_HOST_IN_PROCESS);
  for (const auto& host : state.remote_hosts) {
    config.add_remote_hosts(host);
  }
  config.set_buffer_latency_ms(state.buffer_latency_ms);

  std::string text;
  google::protobuf::TextFormat::PrintToString(config, &text);
  std::ofstream out(kConfigFile);
  if (out.is_open()) {
    out << text;
    LOG(INFO) << "Saved config to " << kConfigFile;
  } else {
    LOG(ERROR) << "Failed to save config to " << kConfigFile;
  }
}

// Helper: send ModulationInfo notification for a given track+plugin's
// modulators
static void sendModulationInfo(Track* track, int plugin_idx) {
  pb::notifications::Notification notif;
  auto* info = notif.mutable_modulation_info();
  info->set_track_index(track->index);
  info->set_plugin_index(plugin_idx);

  auto it = track->modulations.find(plugin_idx);
  if (it != track->modulations.end()) {
    for (int s = 0; s < Modulator::kMaxSlots; ++s) {
      auto& mod = it->second.slots[s];
      auto* slot = info->add_slots();
      slot->set_slot_index(s);
      slot->set_waveform(static_cast<int>(mod.waveform));
      slot->set_rate_hz(mod.rate_hz);
      slot->set_depth(mod.depth);
      slot->set_target_param_id(mod.param_id);
      slot->set_target_param_name(mod.param_name);
      slot->set_assigned(mod.assigned);
      slot->set_sync_to_tempo(mod.sync_to_tempo);
    }
  }

  std::string data;
  notif.SerializeToString(&data);
  sendNotification(reinterpret_cast<const uint8_t*>(data.data()), data.size());
}

void handleModulationCmd(const pb::commands::ModulationCmd& cmd,
                         ProjectState& state) {
  int tidx = cmd.target().track_index();
  int pidx = cmd.target().plugin_index();
  int slot = cmd.slot_index();

  std::lock_guard<std::mutex> lock(state.tracks_mutex);
  if (!state.tracks.count(tidx)) return;
  auto& track = state.tracks[tidx];
  if (pidx < 0 || pidx >= (int)track->plugins.size()) return;
  if (slot < 0 || slot >= Modulator::kMaxSlots) return;

  auto& pmod = track->modulations[pidx];
  auto& mod = pmod.slots[slot];

  switch (cmd.action()) {
    case pb::commands::ModulationCmd::ACTION_ADD: {
      mod.waveform =
          static_cast<Modulator::Waveform>(std::clamp(cmd.waveform(), 0, 3));
      mod.rate_hz = cmd.rate_hz() > 0 ? cmd.rate_hz() : 1.0f;
      mod.depth = std::clamp(cmd.depth(), -1.0f, 1.0f);
      mod.sync_to_tempo = cmd.sync_to_tempo();
      mod.plugin_idx = pidx;
      mod.reset();
      break;
    }
    case pb::commands::ModulationCmd::ACTION_REMOVE: {
      mod = Modulator{};  // Reset to defaults
      break;
    }
    case pb::commands::ModulationCmd::ACTION_CONFIGURE: {
      mod.waveform =
          static_cast<Modulator::Waveform>(std::clamp(cmd.waveform(), 0, 3));
      mod.rate_hz = cmd.rate_hz() > 0 ? cmd.rate_hz() : mod.rate_hz;
      mod.depth = std::clamp(cmd.depth(), -1.0f, 1.0f);
      mod.sync_to_tempo = cmd.sync_to_tempo();
      break;
    }
    case pb::commands::ModulationCmd::ACTION_ASSIGN: {
      mod.plugin_idx = pidx;
      mod.param_id = cmd.target_param_id();
      mod.assigned = true;

      // Look up the param name for display
      VstParamInfo pinfo;
      auto* plugin = track->plugins[pidx].get();
      int param_count = plugin->getParameterCount();
      for (int i = 0; i < param_count; ++i) {
        if (plugin->getParameterInfo(i, pinfo) &&
            pinfo.id == cmd.target_param_id()) {
          mod.param_name = pinfo.name;
          break;
        }
      }
      break;
    }
    default:
      return;
  }

  sendModulationInfo(track.get(), pidx);
}

}  // namespace hibiki
