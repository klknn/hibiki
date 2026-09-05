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

echo "🔨 Building Native C++ Audio Engine (libhibiki_jni.so)..."
cd "$REPO_ROOT"
bazel build //engine/android:libhibiki_jni.so -c opt --jobs=8
TARGET_ABI="$(adb shell getprop ro.product.cpu.abi 2>/dev/null || echo "x86_64")"
echo "📱 Target device architecture: $TARGET_ABI"

if [[ "$TARGET_ABI" == *"x86_64"* ]]; then
  echo "🔨 Packaging Native C++ Audio Engine for x86_64..."
  mkdir -p "$REPO_ROOT/android/app/src/main/jniLibs/x86_64"
  cp -f "$REPO_ROOT/bazel-bin/engine/android/libhibiki_jni.so" "$REPO_ROOT/android/app/src/main/jniLibs/x86_64/"
else
  # On ARM physical devices without cross-compiled ARM64 .so, avoid packaging x86_64 .so
  # so that Android package manager does not reject installation with NO_MATCHING_ABIS.
  rm -rf "$REPO_ROOT/android/app/src/main/jniLibs/x86_64"
  echo "ℹ️  Physical ARM device detected ($TARGET_ABI); running in simulated engine mode."
fi

echo "🔨 Building Hibiki Android APK via Bazel..."
cd "$REPO_ROOT"
bazel build //android/app:app -c opt --jobs=8

APK_PATH="$REPO_ROOT/bazel-bin/android/app/app.apk"
echo "📦 Installing APK to target device..."
adb install -r "$APK_PATH"

echo "🚀 Launching Hibiki DAW (hibiki.android/.MainActivity)..."
adb shell am start -n hibiki.android/.MainActivity

if [[ "${1:-}" != "--no-logs" ]]; then
  echo "📋 Streaming real-time audio and engine logs (Ctrl+C to stop)..."
  adb logcat -s HibikiEngine AAudio AudioTrack AndroidRuntime System.err
fi
