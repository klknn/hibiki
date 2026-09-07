#!/bin/bash
# Build, install, and run Hibiki DAW on connected Android emulator or device.
# Usage: ./tools/run_android_app.sh [--no-logs]

set -euo pipefail

if [[ -n "${BUILD_WORKING_DIRECTORY:-}" ]]; then
  REPO_ROOT="$BUILD_WORKING_DIRECTORY"
else
  REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd -P)"
fi
export ANDROID_HOME="${ANDROID_HOME:-$HOME/Android/Sdk}"
if [[ -f "$ANDROID_HOME/env.sh" ]]; then
  # shellcheck source=/dev/null
  source "$ANDROID_HOME/env.sh"
fi
export PATH="$ANDROID_HOME/cmdline-tools/latest/bin:$ANDROID_HOME/platform-tools:$ANDROID_HOME/emulator:$PATH"

# Ensure adb is present
if ! command -v adb &>/dev/null; then
  echo "❌ adb not found. Setting up Android SDK..."
  "$REPO_ROOT/tools/setup_android_sdk.sh"
fi

# Ensure device/emulator is connected
if [[ -z "$(adb devices | awk 'NR>1 && $2=="device" {print $1}')" ]]; then
  echo "⚠️  No active Android device detected. Launching emulator..."
  "$REPO_ROOT/tools/start_android_emulator.sh"
fi

TARGET_ABI="$(adb shell getprop ro.product.cpu.abi 2>/dev/null || echo "arm64-v8a")"
echo "📱 Target device architecture: $TARGET_ABI"

cd "$REPO_ROOT"
if [[ "$TARGET_ABI" == *"arm64"* || "$TARGET_ABI" == *"aarch64"* ]]; then
  echo "🔨 Building Native C++ Audio Engine for ARM64 (libhibiki_jni.so)..."
  bazel build //engine/android:libhibiki_jni.so --platforms=//:android_arm64 -c opt --jobs=8
  rm -rf "$REPO_ROOT/android/app/src/main/jniLibs"
  mkdir -p "$REPO_ROOT/android/app/src/main/jniLibs/arm64-v8a"
  cp -f "$REPO_ROOT/bazel-bin/engine/android/libhibiki_jni.so" "$REPO_ROOT/android/app/src/main/jniLibs/arm64-v8a/"
  BAZEL_PLATFORM_FLAG="--android_platforms=//:arm64-v8a"
elif [[ "$TARGET_ABI" == *"x86_64"* ]]; then
  echo "🔨 Building Native C++ Audio Engine for x86_64 (libhibiki_jni.so)..."
  bazel build //engine/android:libhibiki_jni.so -c opt --jobs=8
  rm -rf "$REPO_ROOT/android/app/src/main/jniLibs"
  mkdir -p "$REPO_ROOT/android/app/src/main/jniLibs/x86_64"
  cp -f "$REPO_ROOT/bazel-bin/engine/android/libhibiki_jni.so" "$REPO_ROOT/android/app/src/main/jniLibs/x86_64/"
  BAZEL_PLATFORM_FLAG=""
else
  echo "⚠️ Unknown ABI: $TARGET_ABI; building default..."
  BAZEL_PLATFORM_FLAG=""
fi

echo "🔨 Building Hibiki Android APK via Bazel..."
cd "$REPO_ROOT"
bazel build //android/app:app ${BAZEL_PLATFORM_FLAG} -c opt --jobs=8

APK_PATH="$REPO_ROOT/bazel-bin/android/app/app.apk"
echo "📦 Installing APK to target device..."
adb install -r "$APK_PATH"

echo "🚀 Launching Hibiki DAW (hibiki.android/.MainActivity)..."
adb shell am start -n hibiki.android/.MainActivity

if [[ "${1:-}" != "--no-logs" ]]; then
  echo "📋 Streaming real-time audio and engine logs (Ctrl+C to stop)..."
  adb logcat -s HibikiEngine AAudio AudioTrack AndroidRuntime System.err
fi
