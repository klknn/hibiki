#!/bin/bash
# End-to-end (E2E) testing on Android Simulator.
# Usage: ./tools/test_android_e2e.sh

set -euo pipefail

if [[ -n "${BUILD_WORKING_DIRECTORY:-}" ]]; then
  REPO_ROOT="$BUILD_WORKING_DIRECTORY"
elif [[ -d "/home/karita/repos/hibiki" ]]; then
  REPO_ROOT="/home/karita/repos/hibiki"
elif git rev-parse --show-toplevel &>/dev/null; then
  REPO_ROOT="$(git rev-parse --show-toplevel)"
else
  REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd -P)"
fi

if [[ -z "${ANDROID_HOME:-}" || ! -d "$ANDROID_HOME" ]]; then
  for cand in "${HOME:-}/Android/Sdk" "/home/karita/Android/Sdk"; do
    if [[ -d "$cand" ]]; then
      export ANDROID_HOME="$cand"
      break
    fi
  done
fi

if [[ -f "${ANDROID_HOME:-}/env.sh" ]]; then
  # shellcheck source=/dev/null
  source "$ANDROID_HOME/env.sh"
fi

if [[ -z "${ANDROID_AVD_HOME:-}" || ! -d "$ANDROID_AVD_HOME" ]]; then
  for cand in "${HOME:-}/.android/avd" "/home/karita/.android/avd"; do
    if [[ -d "$cand" ]]; then
      export ANDROID_AVD_HOME="$cand"
      break
    fi
  done
fi

export PATH="${PATH:-}:/home/karita/local/bin:/usr/bin:/bin:/usr/local/bin:${ANDROID_HOME:-}/cmdline-tools/latest/bin:${ANDROID_HOME:-}/platform-tools:${ANDROID_HOME:-}/emulator"

echo "📱 Step 1: Checking Android Simulator..."
if [[ -z "$(adb devices | awk 'NR>1 && $2=="device" {print $1}')" ]]; then
  echo "⚠️  No running emulator found. Launching simulator in background mode..."
  "$REPO_ROOT/tools/start_android_emulator.sh" --headless
else
  echo "✅ Active Android simulator/device detected:"
  adb devices
fi

echo "🔨 Step 2: Preparing native C++ JNI audio engine (libhibiki_jni.so)..."
SO_SRC=""
if [[ -f "$REPO_ROOT/bazel-bin/engine/android/libhibiki_jni.so" ]]; then
  SO_SRC="$REPO_ROOT/bazel-bin/engine/android/libhibiki_jni.so"
elif [[ -n "${TEST_SRCDIR:-}" && -f "${TEST_SRCDIR}/_main/engine/android/libhibiki_jni.so" ]]; then
  SO_SRC="${TEST_SRCDIR}/_main/engine/android/libhibiki_jni.so"
fi

if [[ -n "$SO_SRC" ]]; then
  echo "📦 Packaging libhibiki_jni.so from $SO_SRC..."
  mkdir -p "$REPO_ROOT/android/app/src/main/jniLibs/x86_64"
  cp -f "$SO_SRC" "$REPO_ROOT/android/app/src/main/jniLibs/x86_64/"
elif command -v bazel &>/dev/null; then
  cd "$REPO_ROOT"
  bazel build //engine/android:libhibiki_jni.so -c opt --jobs=2
  mkdir -p "$REPO_ROOT/android/app/src/main/jniLibs/x86_64"
  cp -f "$REPO_ROOT/bazel-bin/engine/android/libhibiki_jni.so" "$REPO_ROOT/android/app/src/main/jniLibs/x86_64/"
fi

echo "🧪 Step 3: Executing End-to-End Instrumented Tests on Simulator..."
cd "$REPO_ROOT/android"
./gradlew connectedDebugAndroidTest

echo "🎉 All Android E2E tests PASSED successfully on simulator!"
TEST_REPORT="$REPO_ROOT/android/app/build/reports/androidTests/connected/index.html"
if [[ -f "$TEST_REPORT" ]]; then
  echo "📊 Test report available at: $TEST_REPORT"
fi
