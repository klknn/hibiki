#!/bin/bash
# Start Android Emulator and wait for device to boot.
# Usage: ./tools/start_android_emulator.sh [avd_name]

set -euo pipefail

export ANDROID_HOME="${ANDROID_HOME:-$HOME/Android/Sdk}"
if [[ -f "$ANDROID_HOME/env.sh" ]]; then
  # shellcheck source=/dev/null
  source "$ANDROID_HOME/env.sh"
fi

export PATH="$ANDROID_HOME/cmdline-tools/latest/bin:$ANDROID_HOME/platform-tools:$ANDROID_HOME/emulator:$PATH"

AVD_NAME="${1:-hibiki_pixel}"

if ! command -v emulator &>/dev/null; then
  echo "❌ Android emulator binary not found. Running setup script first..."
  "$(dirname "$0")/setup_android_sdk.sh"
fi

# Check if an emulator or device is already running
if adb devices | grep -E "emulator-[0-9]+" | grep -q "device"; then
  echo "✅ An Android emulator is already running and ready."
  adb devices
  exit 0
fi

echo "🚀 Launching Android Emulator with AVD: $AVD_NAME..."

# Determine window mode based on DISPLAY environment variable
EMULATOR_OPTS=("-avd" "$AVD_NAME" "-no-boot-anim")
if [[ -z "${DISPLAY:-}" ]]; then
  echo "ℹ️  No DISPLAY detected; running emulator in headless mode (-no-window)..."
  EMULATOR_OPTS+=("-no-window" "-gpu" "swiftshader_indirect")
else
  echo "ℹ️  DISPLAY detected ($DISPLAY); running emulator in windowed GUI mode..."
  EMULATOR_OPTS+=("-gpu" "auto")
fi

emulator "${EMULATOR_OPTS[@]}" &
EMU_PID=$!
echo "📱 Emulator process launched (PID: $EMU_PID)"

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
