#pragma once

#include <jni.h>

#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <vector>

#include "absl/status/status.h"
#include "engine/commands/commands.hpp"
#include "engine/core/history.hpp"
#include "engine/core/project.hpp"
#include "pb/commands.pb.h"
#include "pb/notifications.pb.h"

namespace hibiki {

/**
 * AndroidEngineContext manages an in-process Hibiki audio engine instance
 * for Android. It runs the playback loop, dispatches protobuf commands,
 * and collects outgoing notifications for the Android UI.
 */
class AndroidEngineContext {
 public:
  AndroidEngineContext();
  ~AndroidEngineContext();

  /**
   * Initializes audio engine and starts real-time playback thread.
   */
  absl::Status init(int sample_rate = 44100, int latency_ms = 50);

  /**
   * Stops playback thread and frees engine resources.
   */
  void destroy();

  /**
   * Dispatches a serialized protobuf command Request to the engine.
   */
  absl::Status sendRequest(const uint8_t* data, size_t size);

  /**
   * Retrieves the next available serialized Notification, or empty vector if
   * none.
   */
  std::vector<uint8_t> pollNotification();

  /**
   * Controls playback state (play/stop).
   */
  void setPlayback(bool play);

  /**
   * Returns whether audio engine is currently playing.
   */
  bool isPlaying() const;

  /**
   * Returns current playhead position in seconds.
   */
  double getPlaybackPosition() const;

  /**
   * Sets project tempo in BPM.
   */
  void setBpm(double bpm);

  /**
   * Returns project tempo in BPM.
   */
  double getBpm() const;

  /**
   * Access underlying project state.
   */
  ProjectState* getState() { return state_.get(); }

 private:
  void audioThreadLoop();
  void onNotification(const uint8_t* buf, size_t size);

  std::unique_ptr<ProjectState> state_;
  std::unique_ptr<HistoryManager> history_;
  std::thread audio_thread_;
  bool running_ = false;

  mutable std::mutex notification_mutex_;
  std::queue<std::vector<uint8_t>> notification_queue_;
};

}  // namespace hibiki

#ifdef __cplusplus
extern "C" {
#endif

JNIEXPORT jboolean JNICALL Java_hibiki_android_engine_HibikiEngine_nativeInit(
    JNIEnv* env, jobject thiz, jint sample_rate, jint latency_ms);

JNIEXPORT void JNICALL Java_hibiki_android_engine_HibikiEngine_nativeDestroy(
    JNIEnv* env, jobject thiz);

JNIEXPORT jboolean JNICALL
Java_hibiki_android_engine_HibikiEngine_nativeSendRequest(
    JNIEnv* env, jobject thiz, jbyteArray request_bytes);

JNIEXPORT jbyteArray JNICALL
Java_hibiki_android_engine_HibikiEngine_nativePollNotification(JNIEnv* env,
                                                               jobject thiz);

JNIEXPORT void JNICALL
Java_hibiki_android_engine_HibikiEngine_nativeSetPlayback(JNIEnv* env,
                                                          jobject thiz,
                                                          jboolean play);

JNIEXPORT jboolean JNICALL
Java_hibiki_android_engine_HibikiEngine_nativeIsPlaying(JNIEnv* env,
                                                        jobject thiz);

JNIEXPORT jdouble JNICALL
Java_hibiki_android_engine_HibikiEngine_nativeGetPlaybackPosition(JNIEnv* env,
                                                                  jobject thiz);

JNIEXPORT void JNICALL Java_hibiki_android_engine_HibikiEngine_nativeSetBpm(
    JNIEnv* env, jobject thiz, jdouble bpm);

JNIEXPORT jdouble JNICALL
Java_hibiki_android_engine_HibikiEngine_nativeGetBpm(JNIEnv* env, jobject thiz);

#ifdef __cplusplus
}
#endif
