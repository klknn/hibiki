#!/bin/bash
# Setup Android SDK, command-line tools, emulator, and default AVD.
# Usage: ./tools/setup_android_sdk.sh

set -euo pipefail

export ANDROID_HOME="${ANDROID_HOME:-$HOME/Android/Sdk}"
export ANDROID_SDK_ROOT="$ANDROID_HOME"

echo "📱 Setting up Android SDK at: $ANDROID_HOME"
mkdir -p "$ANDROID_HOME"

CMDLINE_TOOLS_URL="https://dl.google.com/android/repository/commandlinetools-linux-11076708_latest.zip"
TMP_DIR="$(mktemp -d)"

cleanup() {
  rm -rf "$TMP_DIR"
}
trap cleanup EXIT

# ─── 1. Download & Extract Command-line Tools ───────────────────────────
if [[ ! -x "$ANDROID_HOME/cmdline-tools/latest/bin/sdkmanager" ]]; then
  echo "⬇️  Downloading Android Command-Line Tools..."
  curl -fsSL -o "$TMP_DIR/cmdline-tools.zip" "$CMDLINE_TOOLS_URL"
  
  echo "📦 Extracting commandlinetools..."
  mkdir -p "$TMP_DIR/extracted"
  unzip -q "$TMP_DIR/cmdline-tools.zip" -d "$TMP_DIR/extracted"
  
  mkdir -p "$ANDROID_HOME/cmdline-tools"
  rm -rf "$ANDROID_HOME/cmdline-tools/latest"
  mv "$TMP_DIR/extracted/cmdline-tools" "$ANDROID_HOME/cmdline-tools/latest"
  echo "✅ Command-line tools installed to $ANDROID_HOME/cmdline-tools/latest"
else
  echo "✅ Command-line tools already installed."
fi

SDKMANAGER="$ANDROID_HOME/cmdline-tools/latest/bin/sdkmanager"
AVDMANAGER="$ANDROID_HOME/cmdline-tools/latest/bin/avdmanager"

# ─── 2. Accept Licenses & Install Required Packages ────────────────────
echo "📜 Accepting Android SDK licenses..."
yes | "$SDKMANAGER" --licenses >/dev/null 2>&1 || true

echo "⬇️  Installing SDK components (platform-tools, platforms;android-34, platforms;android-35, build-tools;34.0.0, emulator, system-image)..."
"$SDKMANAGER" \
  "platform-tools" \
  "platforms;android-34" \
  "platforms;android-35" \
  "build-tools;34.0.0" \
  "emulator" \
  "system-images;android-34;google_apis;x86_64"

# ─── 3. Create Default AVD ─────────────────────────────────────────────
AVD_NAME="hibiki_pixel"
if "$ANDROID_HOME/cmdline-tools/latest/bin/avdmanager" list avd | grep -q "$AVD_NAME"; then
  echo "✅ AVD '$AVD_NAME' already exists."
else
  echo "🤖 Creating Android Virtual Device '$AVD_NAME'..."
  echo "no" | "$AVDMANAGER" create avd \
    -n "$AVD_NAME" \
    -k "system-images;android-34;google_apis;x86_64" \
    --device "pixel_7" \
    --force
  echo "✅ AVD '$AVD_NAME' created."
fi

# ─── 4. Create Environment Helper Script ───────────────────────────────
ENV_FILE="$ANDROID_HOME/env.sh"
cat <<EOF > "$ENV_FILE"
# Source this file to load Android SDK environment
export ANDROID_HOME="$ANDROID_HOME"
export ANDROID_SDK_ROOT="$ANDROID_HOME"
export PATH="\$ANDROID_HOME/cmdline-tools/latest/bin:\$ANDROID_HOME/platform-tools:\$ANDROID_HOME/emulator:\$PATH"
EOF

chmod +x "$ENV_FILE"
echo "✅ Android SDK setup completed successfully!"
echo "💡 To load paths into your current shell, run: source $ENV_FILE"
