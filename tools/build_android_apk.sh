#!/bin/bash
# Build Hibiki Android APK with native JNI engine and Java frontend.
# Usage: ./tools/build_android_apk.sh

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

echo "🔨 [1/2] Building Native C++ Audio Engine (libhibiki_jni.so)..."
cd "$REPO_ROOT"
bazel build //engine/android:libhibiki_jni.so -c opt --jobs=2
mkdir -p "$REPO_ROOT/android/app/src/main/jniLibs/x86_64"
cp -f "$REPO_ROOT/bazel-bin/engine/android/libhibiki_jni.so" "$REPO_ROOT/android/app/src/main/jniLibs/x86_64/"

echo "🔨 [2/2] Assembling Android Debug APK via Gradle..."
cd "$REPO_ROOT/android"
./gradlew assembleDebug

APK_PATH="$REPO_ROOT/android/app/build/outputs/apk/debug/app-debug.apk"
if [[ -f "$APK_PATH" ]]; then
  echo "✅ APK successfully built: $APK_PATH"
else
  echo "❌ APK build failed"
  exit 1
fi
