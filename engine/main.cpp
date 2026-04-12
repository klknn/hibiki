#include <chrono>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

#include "engine/audio/sound.hpp"
#include "engine/core/audio_file.hpp"
#include "engine/core/clip.hpp"
#include "engine/core/commands.hpp"
#include "engine/core/history.hpp"
#include "engine/core/midi.hpp"
#include "engine/core/project.hpp"
#include "engine/core/track.hpp"
#include "engine/effects/builtin_compressor.hpp"
#include "engine/effects/builtin_eq.hpp"
#include "engine/ipc/ipc.hpp"
#include "engine/vst3/vst3_host.hpp"
#include "pb/commands.pb.h"
#include "pb/notifications.pb.h"

namespace hibiki {

void playback_thread(ProjectState& state) {
  auto audio = SoundDevice::create(44100, 2, state.buffer_latency_ms);
  float sample_rate = (float)audio->get_sample_rate();
  int actual_channels = audio->get_channels();
  state.sample_rate = (double)sample_rate;
  if (!audio->is_ready()) return;

  int block_size = 512;

  alignas(32) float bufferL[512];
  alignas(32) float bufferR[512];
  float* outChannels[] = {bufferL, bufferR};

  HostProcessContext context;
  context.sampleRate = sample_rate;
  context.timeSigNumerator = 4;
  context.timeSigDenominator = 4;
  context.continuousTimeSamples = 0;
  context.projectTimeMusic = 0;
  context.tempo = state.bpm;
  double time_per_block = block_size / (double)sample_rate;

  while (!state.quit) {
    std::vector<float> mixBufferL(block_size, 0.0f);
    std::vector<float> mixBufferR(block_size, 0.0f);

    context.tempo = state.bpm;

    {
      std::lock_guard<std::mutex> lock(state.tracks_mutex);

      // Check if any track is soloed (once, outside per-track loop)
      bool any_soloed = false;
      for (const auto& sp : state.tracks) {
        if (sp.second->soloed) {
          any_soloed = true;
          break;
        }
      }

      for (auto& pair : state.tracks) {
        Track* track = pair.second.get();
        std::fill(bufferL, bufferL + block_size, 0.0f);
        std::fill(bufferR, bufferR + block_size, 0.0f);

        // 1. Session clip playback
        std::vector<MidiNoteEvent> clipMidiEvents;
        if (track->playing_slot >= 0 &&
            track->clips.count(track->playing_slot) &&
            track->clips[track->playing_slot]) {
          auto& clip = track->clips[track->playing_slot];
          if (clip->type == Clip::Type::AUDIO) {
            int start_sample = (int)(track->current_time_sec * sample_rate);
            for (int i = 0; i < block_size; ++i) {
              int sample_pos = start_sample + i;
              if (clip->is_loop) {
                int total_samples =
                    clip->audio_data.size() / clip->num_channels;
                if (total_samples > 0) sample_pos = sample_pos % total_samples;
              }
              if (clip->num_channels == 2 &&
                  sample_pos * 2 + 1 < (int)clip->audio_data.size()) {
                bufferL[i] += clip->audio_data[sample_pos * 2];
                bufferR[i] += clip->audio_data[sample_pos * 2 + 1];
              } else if (clip->num_channels == 1 &&
                         sample_pos < (int)clip->audio_data.size()) {
                float s = clip->audio_data[sample_pos];
                bufferL[i] += s;
                bufferR[i] += s;
              }
            }
          } else if (clip->type == Clip::Type::MIDI) {
            // MIDI playback for session clips (looping)
            // Collect clip events — merged with live events below.
            double beats_per_sec = state.bpm / 60.0;
            double current_beats = track->current_time_sec * beats_per_sec;
            double block_end_beats =
                (track->current_time_sec + time_per_block) * beats_per_sec;
            // Handle looping
            double loop_beats =
                clip->duration_beats > 0 ? clip->duration_beats : 4.0;
            if (clip->is_loop && loop_beats > 0) {
              current_beats = std::fmod(current_beats, loop_beats);
              block_end_beats = current_beats + time_per_block * beats_per_sec;
            }
            for (const auto& me : clip->midi_events) {
              if (me.beats >= current_beats && me.beats < block_end_beats) {
                MidiNoteEvent e;
                double event_sec =
                    me.beats / beats_per_sec - track->current_time_sec;
                if (clip->is_loop && loop_beats > 0) {
                  event_sec = (me.beats - current_beats) / beats_per_sec;
                }
                e.sampleOffset = std::max(0, (int)(event_sec * sample_rate));
                if (e.sampleOffset >= block_size)
                  e.sampleOffset = block_size - 1;
                e.channel = me.channel;
                e.pitch = me.note;
                e.isNoteOn = isNoteOn(me);
                e.velocity = e.isNoteOn ? me.velocity / 127.0f : 0.0f;
                clipMidiEvents.push_back(e);
              }
            }
          }
          track->current_time_sec += time_per_block;
        }

        // 1b. Collect timeline MIDI events (merged with allEvents below)
        std::vector<MidiNoteEvent> timelineMidiEvents;
        if (state.is_timeline_playing) {
          for (const auto& tc : track->timeline_clips) {
            if (!tc->clip || tc->clip->type != Clip::Type::MIDI) continue;
            double clip_duration = (tc->duration_beats > 0)
                                       ? tc->duration_beats * 60.0 / state.bpm
                                       : tc->duration_sec;
            if (state.playhead_pos_sec + time_per_block > tc->start_time_sec &&
                state.playhead_pos_sec < tc->start_time_sec + clip_duration) {
              double clip_local_time =
                  state.playhead_pos_sec - tc->start_time_sec;
              double beats_per_sec = state.bpm / 60.0;
              double window_start_beats =
                  clip_local_time * beats_per_sec + tc->trim_start_beats;
              double window_end_beats =
                  (clip_local_time + time_per_block) * beats_per_sec +
                  tc->trim_start_beats;
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
                  timelineMidiEvents.push_back(e);
                }
              }
            }
          }
        }

        // 1c. Merge live MIDI + virtual MIDI + timeline MIDI, process
        // instrument once
        if (!track->plugins.empty() && track->plugins[0]->isInstrument()) {
          std::vector<MidiNoteEvent> allEvents(std::move(clipMidiEvents));
          // Add timeline MIDI events
          allEvents.insert(allEvents.end(), timelineMidiEvents.begin(),
                           timelineMidiEvents.end());

          // Lazily create MIDI input device (only if one is configured)
          if (!track->midi_input_device &&
              !track->midi_input_device_id.empty()) {
            track->midi_input_device = MidiInput::create();
            if (!track->midi_input_device->open(track->midi_input_device_id)) {
              track->midi_input_device.reset();
            }
          }
          if (track->midi_input_device) {
            auto hwEvents = track->midi_input_device->read();
            allEvents.insert(allEvents.end(), hwEvents.begin(), hwEvents.end());
          }

          // Drain virtual MIDI queue (from PC keyboard)
          {
            std::lock_guard<std::mutex> mlock(track->virtual_midi_mutex);
            allEvents.insert(allEvents.end(), track->virtual_midi_queue.begin(),
                             track->virtual_midi_queue.end());
            track->virtual_midi_queue.clear();
          }

          // Capture MIDI events for recording (MIDI mode)
          if (state.is_recording && track->record_armed &&
              track->record_mode == Track::RECORD_MIDI) {
            if (!allEvents.empty()) {
              static int midi_cap_log_counter = 0;
              if (midi_cap_log_counter++ % 1000 == 0) {
                fprintf(stderr,
                        "[MIDI_CAP] track=%d events=%d "
                        "buf_size=%d playhead=%.3f\n",
                        (int)pair.first, (int)allEvents.size(),
                        (int)track->midi_record_buffer.size(),
                        state.playhead_pos_sec);
              }
            }
            for (const auto& ev : allEvents) {
              Track::TimestampedMidiEvent tev;
              tev.time_sec =
                  state.playhead_pos_sec + ev.sampleOffset / state.sample_rate;
              tev.event = ev;
              track->midi_record_buffer.push_back(tev);
            }
          }

          // Always call process() — instruments must render every block
          // (sustained notes, envelopes, effects tails) even without new
          // events.
          track->plugins[0]->process(nullptr, outChannels, block_size, context,
                                     allEvents);
        }

        // 2. Timeline clip playback
        if (state.is_timeline_playing) {
          for (const auto& tc : track->timeline_clips) {
            if (!tc->clip) continue;
            // Get clip duration - use duration_beats for MIDI clips,
            // duration_sec for audio
            double clip_duration = (tc->duration_beats > 0)
                                       ? tc->duration_beats * 60.0 / state.bpm
                                       : tc->duration_sec;
            if (state.playhead_pos_sec + time_per_block > tc->start_time_sec &&
                state.playhead_pos_sec < tc->start_time_sec + clip_duration) {
              double clip_local_time =
                  state.playhead_pos_sec - tc->start_time_sec;

              if (tc->clip->type == Clip::Type::MIDI) {
                // MIDI events already merged above in step 1b
                // (no separate process() call needed)
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
        }

        // 2a. Recording input capture
        if (state.is_recording && track->record_armed && track->input_device &&
            track->input_device->is_ready()) {
          std::vector<float> input_block;
          int input_ch = track->input_device->get_channels();
          if (track->input_device->read(input_block, block_size)) {
            // Extract selected channels from device input
            int ch_start = track->input_channel_start;
            bool stereo = track->input_stereo;
            for (int i = 0; i < block_size; ++i) {
              if (stereo) {
                int idx_l = i * input_ch + ch_start;
                int idx_r = i * input_ch + ch_start + 1;
                float l = (idx_l < (int)input_block.size()) ? input_block[idx_l]
                                                            : 0.0f;
                float r = (idx_r < (int)input_block.size()) ? input_block[idx_r]
                                                            : 0.0f;
                track->record_buffer.push_back(l);
                track->record_buffer.push_back(r);
                // Live monitoring: mix input into track output
                bufferL[i] += l;
                bufferR[i] += r;
              } else {
                int idx = i * input_ch + ch_start;
                float s =
                    (idx < (int)input_block.size()) ? input_block[idx] : 0.0f;
                track->record_buffer.push_back(s);
                bufferL[i] += s;
                bufferR[i] += s;
              }
            }
          }
        }

        // 3. Apply Automation — set parameter values from curves
        if (state.is_timeline_playing) {
          double current_beats = state.playhead_pos_sec * (state.bpm / 60.0);
          for (const auto& lane : track->automation_lanes) {
            if (!lane.clips.empty() && lane.plugin_idx >= 0 &&
                lane.plugin_idx < (int)track->plugins.size()) {
              float val = GetAutomationValue(lane, current_beats, state.bpm);
              track->plugins[lane.plugin_idx]->setParameterValue(lane.param_id,
                                                                 val);
            }
          }
        }

        // 4. Process effects (skip instrument at slot 0 — already used above)
        for (size_t i = 0; i < track->plugins.size(); ++i) {
          if (i == 0 && track->plugins[i]->isInstrument()) continue;
          track->plugins[i]->process(outChannels, outChannels, block_size,
                                     context, {});
        }

        // 5. Apply volume and pan, mix to master, compute peak levels
        bool is_silenced = track->muted || (any_soloed && !track->soloed);
        float vol = is_silenced ? 0.0f : track->volume;
        // Constant-power pan: pan_angle maps [-1,1] to [0, pi/2]
        float pan_angle = (track->pan + 1.0f) * 0.25f *
                          3.14159265f;  // 0 = full left, pi/2 = full right
        float panL = std::cos(pan_angle);
        float panR = std::sin(pan_angle);
        float peakL = 0.0f, peakR = 0.0f;
        for (int i = 0; i < block_size; ++i) {
          float l = bufferL[i] * vol * panL;
          float r = bufferR[i] * vol * panR;
          mixBufferL[i] += l;
          mixBufferR[i] += r;
          peakL = std::max(peakL, std::abs(l));
          peakR = std::max(peakR, std::abs(r));
        }
        {
          std::lock_guard<std::mutex> llock(state.levels_mutex);
          state.track_levels[pair.first] = {peakL, peakR};
        }
      }
    }

    // Mix to output
    std::vector<float> interleaved(block_size * actual_channels, 0.0f);
    for (int i = 0; i < block_size; ++i) {
      interleaved[i * actual_channels] = mixBufferL[i];
      interleaved[i * actual_channels + 1] = mixBufferR[i];
    }
    audio->write(interleaved, block_size);

    if (state.is_timeline_playing) {
      state.playhead_pos_sec += time_per_block;
    }
    context.continuousTimeSamples += block_size;
    context.projectTimeMusic = state.playhead_pos_sec * (context.tempo / 60.0);
  }
}

// Separate thread for sending GUI notifications (playhead, levels).
// Runs at ~30Hz, completely independent of the audio thread.
void notification_thread(ProjectState& state) {
  while (!state.quit) {
    std::this_thread::sleep_for(std::chrono::milliseconds(33));  // ~30Hz

    hibiki::sendPlayheadInfo((float)state.playhead_pos_sec, (float)state.bpm,
                             state.is_timeline_playing);

    hibiki::pb::notifications::Notification notification;
    auto* tl = notification.mutable_track_levels();
    {
      std::lock_guard<std::mutex> llock(state.levels_mutex);
      for (auto& pair : state.track_levels) {
        auto* level = tl->add_levels();
        level->set_track_index(pair.first);
        level->set_peak_l(pair.second.first);
        level->set_peak_r(pair.second.second);
      }
    }
    std::string data;
    notification.SerializeToString(&data);
    sendNotification(reinterpret_cast<const uint8_t*>(data.data()),
                     data.size());

    // Send builtin plugin metering data (~30Hz)
    {
      std::lock_guard<std::mutex> lock(state.tracks_mutex);
      for (auto& pair : state.tracks) {
        auto& track = pair.second;
        int track_idx = pair.first;
        for (size_t p = 0; p < track->plugins.size(); ++p) {
          auto& plugin = track->plugins[p];
          if (!plugin) continue;
          if (auto* eq = dynamic_cast<BuiltinEq*>(plugin.get())) {
            auto spec = eq->getSpectrumData();
            sendPluginSpectrumData(track_idx, (int)p, spec.input_db,
                                   spec.output_db, BuiltinEq::kSpectrumBins);
          } else if (auto* comp =
                         dynamic_cast<BuiltinCompressor*>(plugin.get())) {
            sendPluginMeteringData(track_idx, (int)p, comp->getInputDb(),
                                   comp->getOutputDb(),
                                   comp->getGainReductionDb());
          }
        }
      }
    }

    // Live waveform updates during recording (~5Hz)
    static int wf_counter = 0;
    if (state.is_recording && ++wf_counter >= 6) {
      wf_counter = 0;
      std::lock_guard<std::mutex> lock(state.tracks_mutex);
      for (auto& pair : state.tracks) {
        auto& track = pair.second;
        if (!track->record_armed || track->record_buffer.empty()) continue;
        int rec_ch = track->input_stereo ? 2 : 1;
        int total_samples = (int)track->record_buffer.size();
        double duration_sec =
            (double)total_samples / (rec_ch * state.sample_rate);

        // Generate quick 200-point peak summary
        int summary_size = 200;
        std::vector<float> waveform(summary_size, 0.0f);
        int samples_per_bucket = total_samples / (rec_ch * summary_size);
        if (samples_per_bucket < 1) samples_per_bucket = 1;
        for (int b = 0; b < summary_size; ++b) {
          float peak = 0.0f;
          for (int s = 0; s < samples_per_bucket; ++s) {
            int idx = (b * samples_per_bucket + s) * rec_ch;
            if (idx < total_samples) {
              peak = std::max(peak, std::abs(track->record_buffer[idx]));
            }
          }
          waveform[b] = peak;
        }

        // Use next available clip index (matches final clip on stop)
        int live_clip_idx = (int)track->timeline_clips.size();
        sendTimelineClipInfo(pair.first, live_clip_idx, "Recording...", "",
                             (float)state.record_start_sec, (float)duration_sec,
                             waveform);
      }
    }
  }
}

void run_ipc_loop(ProjectState& state) {
  HistoryManager history;

  // Push current config to GUI on startup
  {
    pb::commands::HibikiConfig config;
    config.set_plugin_host_mode(
        (state.plugin_host_mode == PluginHostMode::LOCAL_SANDBOX)
            ? pb::commands::PLUGIN_HOST_LOCAL_SANDBOX
            : pb::commands::PLUGIN_HOST_IN_PROCESS);
    for (const auto& host : state.remote_hosts) {
      config.add_remote_hosts(host);
    }
    config.set_buffer_latency_ms(state.buffer_latency_ms);
    pb::notifications::Notification notif;
    *notif.mutable_config() = config;
    std::string data;
    notif.SerializeToString(&data);
    sendNotification(reinterpret_cast<const uint8_t*>(data.data()),
                     data.size());
  }

  while (true) {
    uint32_t msg_size = 0;
    std::cin.read(reinterpret_cast<char*>(&msg_size), sizeof(msg_size));
    if (std::cin.eof()) break;
    if (std::cin.fail()) {
      std::cerr << "BACKEND ERROR: Failed to read message size from stdin"
                << std::endl;
      break;
    }

    if (msg_size > 1024 * 1024) {  // 1MB limit for safety
      std::cerr << "BACKEND ERROR: Message size too large: " << msg_size
                << std::endl;
      break;
    }

    std::unique_ptr<uint8_t[]> buffer(new uint8_t[msg_size]);
    std::cin.read(reinterpret_cast<char*>(buffer.get()), msg_size);
    if (std::cin.fail()) {
      std::cerr << "BACKEND ERROR: Failed to read message payload from stdin"
                << std::endl;
      break;
    }

    hibiki::pb::commands::Request request;
    if (!request.ParseFromArray(buffer.get(), msg_size)) {
      std::cerr << "BACKEND ERROR: Failed to parse protobuf request"
                << std::endl;
      continue;
    }

    switch (request.command_case()) {
      case hibiki::pb::commands::Request::kProject:
        handleProjectCmd(request.project(), state, history);
        break;
      case hibiki::pb::commands::Request::kTransport:
        handleTransportCmd(request.transport(), state);
        break;
      case hibiki::pb::commands::Request::kTrack:
        handleTrackCmd(request.track(), state, history);
        break;
      case hibiki::pb::commands::Request::kPlugin:
        handlePluginCmd(request.plugin(), state, history);
        break;
      case hibiki::pb::commands::Request::kAutomation:
        handleAutomationCmd(request.automation(), state, history);
        break;
      case hibiki::pb::commands::Request::kMidi:
        handleMidiCmd(request.midi(), state, history);
        break;
      case hibiki::pb::commands::Request::kSetPluginHostMode:
        handleSetPluginHostMode(request.set_plugin_host_mode(), state);
        break;
      case hibiki::pb::commands::Request::kScanRemotePlugins:
        handleScanRemotePlugins(request.scan_remote_plugins());
        break;
      case hibiki::pb::commands::Request::kSetAudioBufferSize:
        handleSetAudioBufferSize(request.set_audio_buffer_size(), state);
        break;
      case hibiki::pb::commands::Request::kListAudioInputs:
        handleListAudioInputs();
        break;
      case hibiki::pb::commands::Request::kListMidiInputs:
        handleListMidiInputs();
        break;
      case hibiki::pb::commands::Request::kSendVirtualMidi:
        handleSendVirtualMidi(request.send_virtual_midi(), state);
        break;
    }
    if (state.quit) break;
  }
}

}  // namespace hibiki

int main(int argc, char** argv) {
  using namespace hibiki;
  if (argc >= 2 && std::string(argv[1]) == "--list") {
    auto plugins = Vst3Plugin::listPlugins(argv[2]);
    for (const auto& p : plugins) {
      std::cout << p.index << ":" << p.name << ":" << p.vendor << "\n";
    }
    return 0;
  }

#ifdef _WIN32
  // Ensure binary mode for IPC on Windows
  _setmode(_fileno(stdin), _O_BINARY);
  _setmode(_fileno(stdout), _O_BINARY);
#endif

  hibiki::ProjectState state;
  state.bpm = 140.0;            // Explicitly set default BPM
  state.sample_rate = 44100.0;  // Explicitly set default sample rate
  hibiki::loadConfig(
      state);  // Load persisted settings from .hibikirc.textproto
  std::thread audio_thread(hibiki::playback_thread, std::ref(state));
  std::thread notif_thread(hibiki::notification_thread, std::ref(state));

#if defined(__APPLE__)
  std::thread ipc_thread(hibiki::run_ipc_loop, std::ref(state));
  Vst3Plugin::runMainLoop();
  if (ipc_thread.joinable()) ipc_thread.join();
#else
  hibiki::run_ipc_loop(state);
#endif

  if (notif_thread.joinable()) notif_thread.join();
  if (audio_thread.joinable()) audio_thread.join();
  return 0;
}
