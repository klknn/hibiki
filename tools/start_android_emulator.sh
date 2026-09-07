#!/bin/bash
# Start Android Emulator and wait for device to boot.
# Usage: ./tools/start_android_emulator.sh [avd_name]

set -euo pipefail

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

export PATH="${PATH:-}:/usr/bin:/bin:/usr/local/bin:${ANDROID_HOME:-}/cmdline-tools/latest/bin:${ANDROID_HOME:-}/platform-tools:${ANDROID_HOME:-}/emulator"

HEADLESS="${HEADLESS:-false}"
AVD_NAME="hibiki_pixel"

for arg in "$@"; do
  if [[ "$arg" == "--headless" ]]; then
    HEADLESS="true"
  elif [[ "$arg" != -* ]]; then
    AVD_NAME="$arg"
  fi
done

if ! command -v emulator &>/dev/null; then
  echo "❌ Android emulator binary not found. Running setup script first..."
  "$(dirname "$0")/setup_android_sdk.sh"
fi

# Check if an emulator or device is already running
if [[ -n "$(adb devices | awk 'NR>1 && $2=="device" {print $1}')" ]]; then
  echo "✅ An Android emulator is already running and ready."
  adb devices
  exit 0
fi

echo "🚀 Launching Android Emulator with AVD: $AVD_NAME..."

# Determine window mode based on DISPLAY environment variable or --headless
EMULATOR_OPTS=("-avd" "$AVD_NAME" "-no-boot-anim")
if [[ "$HEADLESS" == "true" || -z "${DISPLAY:-}" ]]; then
  echo "ℹ️  Running emulator in headless mode (-no-window)..."
  EMULATOR_OPTS+=("-no-window" "-gpu" "swiftshader_indirect")
else
  echo "ℹ️  DISPLAY detected ($DISPLAY); running emulator in windowed GUI mode..."
  EMULATOR_OPTS+=("-gpu" "auto")
fi

emulator "${EMULATOR_OPTS[@]}" &
EMU_PID=$!
echo "📱 Emulator process launched (PID: $EMU_PID)"
sleep 1
if ! kill -0 "$EMU_PID" 2>/dev/null; then
  echo "❌ Emulator process terminated unexpectedly. Check AVD and emulator configuration."
  exit 1
fi

echo "⏳ Waiting for emulator to connect to adb..."
adb wait-for-device

echo "⏳ Waiting for Android system to finish booting..."
while true; do
  BOOT_COMPLETED=$(adb shell getprop sys.boot_completed 2>/dev/null | tr -d '\r' || true)
  if [[ "$BOOT_COMPLETED" == "1" ]]; then
    break
  fi
  sleep 2
done

echo "🎉 Android Emulator is fully booted and ready!"
adb devices
