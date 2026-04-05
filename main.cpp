#include <atomic>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "history.hpp"

#if defined(__APPLE__)
#include "coreaudio_out.hpp"
#elif !defined(_WIN32)
#include "alsa_out.hpp"
#else
#include <fcntl.h>
#include <io.h>

#include "win32_out.hpp"
#endif
#include "audio_file.hpp"
#include "clip.hpp"
#include "commands.hpp"
#include "ipc.hpp"
#include "midi.hpp"
#include "pb/commands.pb.h"
#include "pb/core.pb.h"
#include "pb/notifications.pb.h"
#include "project.hpp"
#include "track.hpp"
#include "vst3_host.hpp"

namespace hibiki {

void playback_thread(ProjectState& state) {
#if defined(__APPLE__)
  CoreAudioPlayback alsa(44100, 2);
  float sample_rate = 44100.0f;
  int actual_channels = 2;
#elif !defined(_WIN32)
  AlsaPlayback alsa(44100, 2);
  float sample_rate = 44100.0f;
  int actual_channels = 2;
#else
  Win32Playback alsa(44100, 2);
  float sample_rate = (float)alsa.get_sample_rate();
  int actual_channels = alsa.get_channels();
#endif
  state.sample_rate = (double)sample_rate;
  if (!alsa.is_ready()) return;

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
      for (auto& pair : state.tracks) {
        Track* track = pair.second.get();
        std::fill(bufferL, bufferL + block_size, 0.0f);
        std::fill(bufferR, bufferR + block_size, 0.0f);

        // 1. Session clip playback
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
            std::vector<MidiNoteEvent> blockEvents;
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
                blockEvents.push_back(e);
              }
            }
            if (!track->plugins.empty() && track->plugins[0]->isInstrument()) {
              track->plugins[0]->process(nullptr, outChannels, block_size,
                                         context, blockEvents);
            }
          }
          track->current_time_sec += time_per_block;
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
                std::vector<MidiNoteEvent> blockEvents;
                double beats_per_sec = state.bpm / 60.0;
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
                if (!track->plugins.empty() &&
                    track->plugins[0]->isInstrument()) {
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

        // 5. Track peak levels
        float peakL = 0.0f, peakR = 0.0f;
        for (int i = 0; i < block_size; ++i) {
          mixBufferL[i] += bufferL[i];
          mixBufferR[i] += bufferR[i];
          peakL = std::max(peakL, std::abs(bufferL[i]));
          peakR = std::max(peakR, std::abs(bufferR[i]));
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
    alsa.write(interleaved, block_size);

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
  }
}

void run_ipc_loop(ProjectState& state) {
  HistoryManager history;
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
