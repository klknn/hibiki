#!/bin/bash
# Build, install, and run Hibiki DAW on connected Android emulator or device.
# Usage: ./tools/run_android_app.sh [--no-logs]

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd -P)"
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
if ! adb devices | grep -E "(emulator-[0-9]+|[a-zA-Z0-9]+)\s+device" &>/dev/null; then
  echo "⚠️  No active Android device detected. Launching emulator..."
  "$REPO_ROOT/tools/start_android_emulator.sh"
fi

echo "🔨 Building Hibiki Android APK..."
cd "$REPO_ROOT/android"
./gradlew assembleDebug

echo "📦 Installing APK to target device..."
adb install -r "$REPO_ROOT/android/app/build/outputs/apk/debug/app-debug.apk"

echo "🚀 Launching Hibiki DAW (hibiki.android/.MainActivity)..."
adb shell am start -n hibiki.android/.MainActivity

if [[ "${1:-}" != "--no-logs" ]]; then
  echo "📋 Streaming real-time audio and engine logs (Ctrl+C to stop)..."
  adb logcat -s HibikiEngine AAudio AudioTrack
fi
