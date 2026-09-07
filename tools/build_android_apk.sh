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

PLATFORM_ARG="${1:-arm64-v8a}"
cd "$REPO_ROOT"
if [[ "$PLATFORM_ARG" == "arm64-v8a" || "$PLATFORM_ARG" == "arm64" || "$PLATFORM_ARG" == "aarch64" ]]; then
  echo "🔨 Building Native C++ Audio Engine for ARM64 (libhibiki_jni.so)..."
  bazel build //engine/android:libhibiki_jni.so --platforms=//:android_arm64 -c opt --jobs=8
  rm -rf "$REPO_ROOT/android/app/src/main/jniLibs"
  mkdir -p "$REPO_ROOT/android/app/src/main/jniLibs/arm64-v8a"
  cp -f "$REPO_ROOT/bazel-bin/engine/android/libhibiki_jni.so" "$REPO_ROOT/android/app/src/main/jniLibs/arm64-v8a/"
  BAZEL_PLATFORM_FLAG="--android_platforms=//:arm64-v8a"
elif [[ "$PLATFORM_ARG" == "x86_64" ]]; then
  echo "🔨 Building Native C++ Audio Engine for x86_64 (libhibiki_jni.so)..."
  bazel build //engine/android:libhibiki_jni.so -c opt --jobs=8
  rm -rf "$REPO_ROOT/android/app/src/main/jniLibs"
  mkdir -p "$REPO_ROOT/android/app/src/main/jniLibs/x86_64"
  cp -f "$REPO_ROOT/bazel-bin/engine/android/libhibiki_jni.so" "$REPO_ROOT/android/app/src/main/jniLibs/x86_64/"
  BAZEL_PLATFORM_FLAG=""
else
  BAZEL_PLATFORM_FLAG=""
fi

echo "🔨 Building Hibiki Android APK via Bazel..."
bazel build //android/app:app ${BAZEL_PLATFORM_FLAG} -c opt --jobs=8

APK_PATH="$REPO_ROOT/bazel-bin/android/app/app.apk"
if [[ -f "$APK_PATH" ]]; then
  echo "✅ APK successfully built via Bazel: $APK_PATH"
else
  echo "❌ APK build failed"
  exit 1
fi
