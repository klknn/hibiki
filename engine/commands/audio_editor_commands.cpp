#include <filesystem>

#include "absl/log/log.h"
#include "engine/commands/commands.hpp"
#include "engine/core/audio_editor.hpp"
#include "engine/ipc/ipc.hpp"
#include "pb/notifications.pb.h"

namespace hibiki {

namespace {
// Singleton audio editor session (one per engine process).
AudioEditor g_audio_editor;

void sendAudioEditorData(const AudioEditor& editor) {
  auto waveform = editor.ComputeWaveform(2048);
  int spec_w = 0, spec_h = 0;
  auto spectrogram = editor.ComputeSpectrogram(spec_w, spec_h);

  pb::notifications::Notification notif;
  auto* data = notif.mutable_audio_editor_data();
  for (float v : waveform) data->add_waveform(v);
  for (float v : spectrogram) data->add_spectrogram(v);
  data->set_spectrogram_width(spec_w);
  data->set_spectrogram_height(spec_h);
  data->set_duration_sec((float)editor.duration_sec());
  data->set_sample_rate(editor.sample_rate());
  data->set_num_channels(editor.num_channels());
  data->set_file_name(editor.file_name());

  std::string buf;
  notif.SerializeToString(&buf);
  sendNotification(reinterpret_cast<const uint8_t*>(buf.data()), buf.size());
}
}  // namespace

void handleAudioEditorCmd(const pb::commands::AudioEditorCmd& cmd,
                          ProjectState& state) {
  namespace AE = pb::commands;

  switch (cmd.action()) {
    case AE::AudioEditorCmd::AE_ACTION_LOAD: {
      auto status = g_audio_editor.Load(cmd.path(), state.sample_rate);
      if (!status.ok()) {
        LOG(ERROR) << "AudioEditor: load failed: " << status.message();
        return;
      }
      sendAudioEditorData(g_audio_editor);
      break;
    }
    case AE::AudioEditorCmd::AE_ACTION_NORMALIZE:
      g_audio_editor.Normalize();
      sendAudioEditorData(g_audio_editor);
      break;
    case AE::AudioEditorCmd::AE_ACTION_REVERSE:
      g_audio_editor.Reverse(cmd.selection_start(), cmd.selection_end());
      sendAudioEditorData(g_audio_editor);
      break;
    case AE::AudioEditorCmd::AE_ACTION_FADE_IN:
      g_audio_editor.FadeIn(cmd.selection_start(), cmd.selection_end());
      sendAudioEditorData(g_audio_editor);
      break;
    case AE::AudioEditorCmd::AE_ACTION_FADE_OUT:
      g_audio_editor.FadeOut(cmd.selection_start(), cmd.selection_end());
      sendAudioEditorData(g_audio_editor);
      break;
    case AE::AudioEditorCmd::AE_ACTION_TRIM:
      g_audio_editor.Trim(cmd.selection_start(), cmd.selection_end());
      sendAudioEditorData(g_audio_editor);
      break;
    case AE::AudioEditorCmd::AE_ACTION_SAVE: {
      auto status = g_audio_editor.Save(cmd.path());
      if (!status.ok()) {
        LOG(ERROR) << "AudioEditor: save failed: " << status.message();
      }
      break;
    }
    case AE::AudioEditorCmd::AE_ACTION_GAIN:
      g_audio_editor.ApplyGain(cmd.selection_start(), cmd.selection_end(),
                               cmd.value());
      sendAudioEditorData(g_audio_editor);
      break;
    case AE::AudioEditorCmd::AE_ACTION_CONVOLVE: {
      auto status = g_audio_editor.Convolve(cmd.impulse_path(), cmd.conv_dry(),
                                            cmd.conv_wet(), cmd.conv_add_tail(),
                                            state.sample_rate);
      if (!status.ok()) {
        LOG(ERROR) << "AudioEditor: convolve failed: " << status.message();
        return;
      }
      sendAudioEditorData(g_audio_editor);
      break;
    }
    case AE::AudioEditorCmd::AE_ACTION_BLUR:
      g_audio_editor.Blur(cmd.blur_amount(), cmd.blur_envelope());
      sendAudioEditorData(g_audio_editor);
      break;
    case AE::AudioEditorCmd::AE_ACTION_APPLY_TO_CLIP: {
      // Save to a unique temp file, then load it as a clip on the track
      auto temp_dir = std::filesystem::temp_directory_path();
      auto temp_path =
          temp_dir / ("hibiki_editor_" + std::to_string(cmd.track_index()) +
                      "_" + std::to_string(cmd.clip_index()) + ".wav");
      auto status = g_audio_editor.Save(temp_path.string());
      if (!status.ok()) {
        LOG(ERROR) << "AudioEditor: apply-to-clip save failed: "
                   << status.message();
        return;
      }
      // Load the temp file as clip via the existing track pipeline
      int track_idx = cmd.track_index();
      int clip_idx = cmd.clip_index();
      auto it = state.tracks.find(track_idx);
      if (it != state.tracks.end() && it->second) {
        auto& track = it->second;
        // Try session clip first (LoadClip reloads the slot)
        if (track->clips.count(clip_idx)) {
          track->LoadClip(clip_idx, temp_path.string(), false,
                          state.sample_rate);
        }
        // Also try timeline clip (update path and reload)
        if (clip_idx >= 0 && clip_idx < (int)track->timeline_clips.size() &&
            track->timeline_clips[clip_idx]) {
          auto& tc = track->timeline_clips[clip_idx];
          if (tc->clip) {
            tc->clip->path = temp_path.string();
          }
          track->AddTimelineClip(temp_path.string(), tc->start_time_sec,
                                 state.bpm, tc->duration_beats,
                                 state.sample_rate);
          track->RemoveTimelineClip(clip_idx);
        }
      }
      LOG(INFO) << "AudioEditor: applied to clip track=" << track_idx
                << " clip=" << clip_idx << " path=" << temp_path.string();
      break;
    }
    case AE::AudioEditorCmd::AE_ACTION_PREVIEW_PLAY:
      g_audio_editor.PreviewPlay();
      LOG(INFO) << "AudioEditor: preview play started";
      break;
    case AE::AudioEditorCmd::AE_ACTION_PREVIEW_STOP:
      g_audio_editor.PreviewStop();
      LOG(INFO) << "AudioEditor: preview stop";
      break;
    default:
      LOG(WARNING) << "AudioEditor: unknown action " << (int)cmd.action();
      break;
  }
}

}  // namespace hibiki
