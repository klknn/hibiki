#include "engine/android/hibiki_jni.hpp"

#include <cmath>
#include <cstring>
#include <memory>
#include <vector>

#include "absl/log/log.h"
#include "absl/status/status.h"
#include "engine/audio/sound.hpp"
#include "engine/builtin_registry.hpp"
#include "engine/commands/commands.hpp"
#include "engine/core/track.hpp"
#include "engine/ipc/ipc.hpp"

namespace hibiki {

namespace {
static std::unique_ptr<AndroidEngineContext> g_engine_instance = nullptr;
static std::mutex g_engine_mutex;
}  // namespace

AndroidEngineContext::AndroidEngineContext()
    : state_(std::make_unique<ProjectState>()),
      history_(std::make_unique<HistoryManager>()) {}

AndroidEngineContext::~AndroidEngineContext() { destroy(); }

absl::Status AndroidEngineContext::init(int sample_rate, int latency_ms) {
  if (running_) {
    return absl::AlreadyExistsError("Engine is already initialized");
  }

  state_->sample_rate = sample_rate > 0 ? sample_rate : 44100;
  state_->buffer_latency_ms = latency_ms > 0 ? latency_ms : 50;
  state_->bpm = 120.0;
  state_->is_playing = false;
  state_->quit = false;

  // Route IPC notifications into our in-memory queue
  setNotificationHandler(
      [this](const uint8_t* buf, size_t size) { onNotification(buf, size); });

  running_ = true;
  audio_thread_ = std::thread(&AndroidEngineContext::audioThreadLoop, this);

  LOG(INFO) << "Android Hibiki Engine initialized successfully";
  return absl::OkStatus();
}

void AndroidEngineContext::destroy() {
  if (!running_) return;

  state_->quit = true;
  running_ = false;

  if (audio_thread_.joinable()) {
    audio_thread_.join();
  }

  setNotificationHandler(nullptr);
  LOG(INFO) << "Android Hibiki Engine destroyed";
}

void AndroidEngineContext::onNotification(const uint8_t* buf, size_t size) {
  std::lock_guard<std::mutex> lock(notification_mutex_);
  notification_queue_.emplace(buf, buf + size);
}

std::vector<uint8_t> AndroidEngineContext::pollNotification() {
  std::lock_guard<std::mutex> lock(notification_mutex_);
  if (notification_queue_.empty()) {
    return {};
  }
  std::vector<uint8_t> notif = std::move(notification_queue_.front());
  notification_queue_.pop();
  return notif;
}

absl::Status AndroidEngineContext::sendRequest(const uint8_t* data,
                                               size_t size) {
  if (!running_ || !state_ || !history_) {
    return absl::FailedPreconditionError("Engine is not running");
  }

  pb::commands::Request request;
  if (!request.ParseFromArray(data, size)) {
    LOG(ERROR) << "Failed to parse protobuf Request in Android JNI bridge";
    return absl::InvalidArgumentError("Malformed protobuf Request payload");
  }

  switch (request.command_case()) {
    case pb::commands::Request::kProject:
      handleProjectCmd(request.project(), *state_, *history_);
      break;
    case pb::commands::Request::kTransport:
      handleTransportCmd(request.transport(), *state_);
      break;
    case pb::commands::Request::kTrack:
      handleTrackCmd(request.track(), *state_, *history_);
      break;
    case pb::commands::Request::kPlugin:
      handlePluginCmd(request.plugin(), *state_, *history_);
      break;
    case pb::commands::Request::kAutomation:
      handleAutomationCmd(request.automation(), *state_, *history_);
      break;
    case pb::commands::Request::kMidi:
      handleMidiCmd(request.midi(), *state_, *history_);
      break;
    case pb::commands::Request::kSetPluginHostMode:
      handleSetPluginHostMode(request.set_plugin_host_mode(), *state_);
      break;
    case pb::commands::Request::kScanRemotePlugins:
      handleScanRemotePlugins(request.scan_remote_plugins());
      break;
    case pb::commands::Request::kSetAudioBufferSize:
      handleSetAudioBufferSize(request.set_audio_buffer_size(), *state_);
      break;
    case pb::commands::Request::kListAudioInputs:
      handleListAudioInputs();
      break;
    case pb::commands::Request::kListMidiInputs:
      handleListMidiInputs();
      break;
    case pb::commands::Request::kSendVirtualMidi:
      handleSendVirtualMidi(request.send_virtual_midi(), *state_);
      break;
    case pb::commands::Request::kModulation:
      handleModulationCmd(request.modulation(), *state_);
      break;
    case pb::commands::Request::kSetProcessingPrecision:
      handleSetProcessingPrecision(request.set_processing_precision(), *state_);
      break;
    case pb::commands::Request::kAudioEditor:
      handleAudioEditorCmd(request.audio_editor(), *state_);
      break;
    case pb::commands::Request::kDrumPad:
      handleDrumPadCmd(request.drum_pad(), *state_);
      break;
    default:
      break;
  }

  return absl::OkStatus();
}

void AndroidEngineContext::setPlayback(bool play) {
  if (state_) {
    state_->is_playing = play;
    sendPlayheadInfo(state_->playhead_pos_sec, state_->bpm, state_->is_playing,
                     state_->loop_enabled, state_->loop_start_sec,
                     state_->loop_end_sec);
  }
}

bool AndroidEngineContext::isPlaying() const {
  return state_ ? state_->is_playing : false;
}

double AndroidEngineContext::getPlaybackPosition() const {
  return state_ ? state_->playhead_pos_sec : 0.0;
}

void AndroidEngineContext::setBpm(double bpm) {
  if (state_ && bpm > 20.0 && bpm < 999.0) {
    state_->bpm = bpm;
    sendPlayheadInfo(state_->playhead_pos_sec, state_->bpm, state_->is_playing,
                     state_->loop_enabled, state_->loop_start_sec,
                     state_->loop_end_sec);
  }
}

double AndroidEngineContext::getBpm() const {
  return state_ ? state_->bpm : 120.0;
}

void AndroidEngineContext::audioThreadLoop() {
  auto audio =
      SoundDevice::create(state_->sample_rate, 2, state_->buffer_latency_ms);
  if (!audio || !audio->is_ready()) {
    LOG(WARNING)
        << "Audio output device not ready; running in simulated audio clock";
  }

  int block_size = 512;
  std::vector<float> out_buffer(block_size * 2, 0.0f);
  double sample_rate = state_->sample_rate > 0 ? state_->sample_rate : 44100.0;
  double dt = static_cast<double>(block_size) / sample_rate;
  std::vector<MidiNoteEvent> empty_events;

  while (!state_->quit) {
    std::fill(out_buffer.begin(), out_buffer.end(), 0.0f);

    if (state_->is_playing) {
      std::lock_guard<std::mutex> lock(state_->tracks_mutex);

      // Mix active tracks into stereo output buffer
      for (auto& [track_idx, track] : state_->tracks) {
        if (!track || track->muted) continue;

        // Process audio rendering for track if clips are active
        // Track processing and mixdown
        float vol = track->volume;
        float pan = track->pan;
        float left_gain = vol * (pan <= 0.0f ? 1.0f : (1.0f - pan));
        float right_gain = vol * (pan >= 0.0f ? 1.0f : (1.0f + pan));

        // Generate audio from active instruments/plugins
        for (auto& plugin : track->plugins) {
          if (!plugin) continue;
          float plugBufL[512] = {0.0f};
          float plugBufR[512] = {0.0f};
          float* plugOut[] = {plugBufL, plugBufR};

          HostProcessContext ctx;
          ctx.sampleRate = sample_rate;
          ctx.tempo = state_->bpm;
          ctx.projectTimeMusic =
              state_->playhead_pos_sec * (state_->bpm / 60.0);

          plugin->process(nullptr, plugOut, block_size, ctx, empty_events,
                          nullptr);

          for (int i = 0; i < block_size; ++i) {
            out_buffer[i * 2] += plugBufL[i] * left_gain;
            out_buffer[i * 2 + 1] += plugBufR[i] * right_gain;
          }
        }
      }

      state_->playhead_pos_sec += dt;
      if (state_->loop_enabled &&
          state_->playhead_pos_sec >= state_->loop_end_sec) {
        state_->playhead_pos_sec = state_->loop_start_sec;
      }
    }

    if (audio && audio->is_ready()) {
      audio->write(out_buffer, block_size);
    } else {
      std::this_thread::sleep_for(
          std::chrono::microseconds(static_cast<int64_t>(dt * 1000000)));
    }
  }
}

}  // namespace hibiki

// JNI bindings
extern "C" {

JNIEXPORT jboolean JNICALL Java_hibiki_android_engine_HibikiEngine_nativeInit(
    JNIEnv* env, jobject thiz, jint sample_rate, jint latency_ms) {
  std::lock_guard<std::mutex> lock(hibiki::g_engine_mutex);
  if (!hibiki::g_engine_instance) {
    hibiki::g_engine_instance =
        std::make_unique<hibiki::AndroidEngineContext>();
  }
  auto status = hibiki::g_engine_instance->init(sample_rate, latency_ms);
  return status.ok() ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL Java_hibiki_android_engine_HibikiEngine_nativeDestroy(
    JNIEnv* env, jobject thiz) {
  std::lock_guard<std::mutex> lock(hibiki::g_engine_mutex);
  if (hibiki::g_engine_instance) {
    hibiki::g_engine_instance->destroy();
    hibiki::g_engine_instance.reset();
  }
}

JNIEXPORT jboolean JNICALL
Java_hibiki_android_engine_HibikiEngine_nativeSendRequest(
    JNIEnv* env, jobject thiz, jbyteArray request_bytes) {
  std::lock_guard<std::mutex> lock(hibiki::g_engine_mutex);
  if (!hibiki::g_engine_instance || !request_bytes) {
    return JNI_FALSE;
  }

  jsize len = env->GetArrayLength(request_bytes);
  if (len <= 0) return JNI_TRUE;

  jbyte* data = env->GetByteArrayElements(request_bytes, nullptr);
  auto status = hibiki::g_engine_instance->sendRequest(
      reinterpret_cast<const uint8_t*>(data), static_cast<size_t>(len));
  env->ReleaseByteArrayElements(request_bytes, data, JNI_ABORT);

  return status.ok() ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jbyteArray JNICALL
Java_hibiki_android_engine_HibikiEngine_nativePollNotification(JNIEnv* env,
                                                               jobject thiz) {
  std::lock_guard<std::mutex> lock(hibiki::g_engine_mutex);
  if (!hibiki::g_engine_instance) {
    return nullptr;
  }

  auto notif = hibiki::g_engine_instance->pollNotification();
  if (notif.empty()) {
    return nullptr;
  }

  jbyteArray arr = env->NewByteArray(notif.size());
  env->SetByteArrayRegion(arr, 0, notif.size(),
                          reinterpret_cast<const jbyte*>(notif.data()));
  return arr;
}

JNIEXPORT void JNICALL
Java_hibiki_android_engine_HibikiEngine_nativeSetPlayback(JNIEnv* env,
                                                          jobject thiz,
                                                          jboolean play) {
  std::lock_guard<std::mutex> lock(hibiki::g_engine_mutex);
  if (hibiki::g_engine_instance) {
    hibiki::g_engine_instance->setPlayback(play == JNI_TRUE);
  }
}

JNIEXPORT jboolean JNICALL
Java_hibiki_android_engine_HibikiEngine_nativeIsPlaying(JNIEnv* env,
                                                        jobject thiz) {
  std::lock_guard<std::mutex> lock(hibiki::g_engine_mutex);
  if (hibiki::g_engine_instance) {
    return hibiki::g_engine_instance->isPlaying() ? JNI_TRUE : JNI_FALSE;
  }
  return JNI_FALSE;
}

JNIEXPORT jdouble JNICALL
Java_hibiki_android_engine_HibikiEngine_nativeGetPlaybackPosition(
    JNIEnv* env, jobject thiz) {
  std::lock_guard<std::mutex> lock(hibiki::g_engine_mutex);
  if (hibiki::g_engine_instance) {
    return hibiki::g_engine_instance->getPlaybackPosition();
  }
  return 0.0;
}

JNIEXPORT void JNICALL Java_hibiki_android_engine_HibikiEngine_nativeSetBpm(
    JNIEnv* env, jobject thiz, jdouble bpm) {
  std::lock_guard<std::mutex> lock(hibiki::g_engine_mutex);
  if (hibiki::g_engine_instance) {
    hibiki::g_engine_instance->setBpm(bpm);
  }
}

JNIEXPORT jdouble JNICALL Java_hibiki_android_engine_HibikiEngine_nativeGetBpm(
    JNIEnv* env, jobject thiz) {
  std::lock_guard<std::mutex> lock(hibiki::g_engine_mutex);
  if (hibiki::g_engine_instance) {
    return hibiki::g_engine_instance->getBpm();
  }
  return 120.0;
}

}  // extern "C"
