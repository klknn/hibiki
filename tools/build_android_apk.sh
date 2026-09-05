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

echo "🔨 Building Hibiki Android APK via Bazel..."
cd "$REPO_ROOT"
bazel build //android/app:app -c opt --jobs=8

APK_PATH="$REPO_ROOT/bazel-bin/android/app/app.apk"
if [[ -f "$APK_PATH" ]]; then
  echo "✅ APK successfully built via Bazel: $APK_PATH"
else
  echo "❌ APK build failed"
  exit 1
fi
