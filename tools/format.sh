#!/bin/bash
# Format all source files in the repository.
# Usage: tools/format.sh [--check]
#   --check   Dry-run mode: exit 1 if any files would change.

set -euo pipefail

PATH=$JAVA_HOME/bin:$PATH
REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd -P)"
TOOLS_DIR="$REPO_ROOT/tools"
CHECK_MODE=false
[[ "${1:-}" == "--check" ]] && CHECK_MODE=true

CHANGED=0

# ─── Helper: detect OS and arch ──────────────────────────────────────
detect_platform() {
  local os arch
  case "$(uname -m)" in
    x86_64|amd64) arch=amd64 ;;
    arm64|aarch64) arch=arm64 ;;
    *)             arch=amd64 ;;
  esac
  case "$(uname -s)" in
    Linux*)  os=linux ;echo "${os}-${arch}";;
    Darwin*) os=darwin ; echo "${os}-${arch}";;
    *)       os=windows ; echo "${os}-${arch}.exe";;
  esac

}
PLATFORM="$(detect_platform)"

# ─── Helper: ensure google-java-format is available ──────────────────
GJF_VERSION="1.25.2"
GJF_JAR="$TOOLS_DIR/google-java-format-${GJF_VERSION}-all-deps.jar"
ensure_google_java_format() {
  if command -v google-java-format &>/dev/null; then
    return 0
  fi
  if [[ ! -f "$GJF_JAR" ]]; then
    echo "⬇️  Downloading google-java-format ${GJF_VERSION}..."
    curl -fsSL -o "$GJF_JAR" \
      "https://github.com/google/google-java-format/releases/download/v${GJF_VERSION}/google-java-format-${GJF_VERSION}-all-deps.jar"
    echo "✅ Downloaded to $GJF_JAR"
  fi
}

# ─── Helper: ensure buildifier is available ──────────────────────────
BUILDIFIER_VERSION="v8.5.1"
BUILDIFIER_BIN="$TOOLS_DIR/buildifier"
ensure_buildifier() {
  if command -v buildifier &>/dev/null; then
    return 0
  fi
  if [[ ! -x "$BUILDIFIER_BIN" ]]; then
    BUILDIFIER_URL="https://github.com/bazelbuild/buildtools/releases/download/${BUILDIFIER_VERSION}/buildifier-${PLATFORM}"
    echo "⬇️  Downloading buildifier $BUILDIFIER_URL"
    curl -fsSL -o "$BUILDIFIER_BIN" "$BUILDIFIER_URL"
    chmod +x "$BUILDIFIER_BIN"
    echo "✅ Downloaded to $BUILDIFIER_BIN"
  fi
}

# ─── C++ (clang-format, Google style) ────────────────────────────────
if command -v clang-format &>/dev/null; then
  CPP_FILES=$(find "$REPO_ROOT" \
    -path "$REPO_ROOT/third_party" -prune -o \
    -path "$REPO_ROOT/bazel-*" -prune -o \
    -type f \( -name '*.cpp' -o -name '*.hpp' -o -name '*.cc' -o -name '*.h' \) -print)
  if [[ -n "$CPP_FILES" ]]; then
    if $CHECK_MODE; then
      if echo "$CPP_FILES" | xargs clang-format -style=file --dry-run -Werror 2>&1 | grep -q 'error:'; then
        echo "❌ C++ files need formatting"
        CHANGED=1
      else
        echo "✅ C++ files are formatted"
      fi
    else
      echo "$CPP_FILES" | xargs clang-format -i -style=file
      echo "✅ Formatted $(echo "$CPP_FILES" | wc -l) C++ files"
    fi
  fi
else
  echo "⚠️  clang-format not found — skipping C++  (apt install clang-format)"
fi

# ─── Java (google-java-format) ───────────────────────────────────────
ensure_google_java_format
if command -v google-java-format &>/dev/null; then
  GJF_CMD="google-java-format"
elif [[ -f "$GJF_JAR" ]]; then
  GJF_CMD="java -jar $GJF_JAR"
else
  GJF_CMD=""
  echo "⚠️  google-java-format not found and download failed — skipping Java"
fi
if [[ -n "${GJF_CMD:-}" ]]; then
  JAVA_FILES=$(find "$REPO_ROOT/src" -type f -name '*.java' 2>/dev/null || true)
  if [[ -n "$JAVA_FILES" ]]; then
    if $CHECK_MODE; then
      if echo "$JAVA_FILES" | xargs $GJF_CMD --dry-run --set-exit-if-changed &>/dev/null; then
        echo "✅ Java files are formatted"
      else
        echo "❌ Java files need formatting"
        CHANGED=1
      fi
    else
      echo "$JAVA_FILES" | xargs $GJF_CMD -i
      echo "✅ Formatted $(echo "$JAVA_FILES" | wc -l) Java files"
    fi
  fi
fi

# ─── Starlark / BUILD files (buildifier) ─────────────────────────────
ensure_buildifier
if command -v buildifier &>/dev/null; then
  BUILDIFIER_CMD="buildifier"
elif [[ -x "$BUILDIFIER_BIN" ]]; then
  BUILDIFIER_CMD="$BUILDIFIER_BIN"
else
  BUILDIFIER_CMD=""
  echo "⚠️  buildifier not found and download failed — skipping BUILD files"
fi
if [[ -n "${BUILDIFIER_CMD:-}" ]]; then
  if $CHECK_MODE; then
    if $BUILDIFIER_CMD -mode=check -r "$REPO_ROOT" 2>/dev/null; then
      echo "✅ BUILD files are formatted"
    else
      echo "❌ BUILD files need formatting"
      CHANGED=1
    fi
  else
    $BUILDIFIER_CMD -r "$REPO_ROOT"
    echo "✅ Formatted BUILD files"
  fi
fi

# ─── Proto (clang-format, Google style) ──────────────────────────────
if command -v clang-format &>/dev/null; then
  PROTO_FILES=$(find "$REPO_ROOT/pb" -type f -name '*.proto' 2>/dev/null || true)
  if [[ -n "$PROTO_FILES" ]]; then
    if $CHECK_MODE; then
      if echo "$PROTO_FILES" | xargs clang-format -style=file --dry-run -Werror 2>&1 | grep -q 'error:'; then
        echo "❌ Proto files need formatting"
        CHANGED=1
      else
        echo "✅ Proto files are formatted"
      fi
    else
      echo "$PROTO_FILES" | xargs clang-format -i -style=file
      echo "✅ Formatted $(echo "$PROTO_FILES" | wc -l) proto files"
    fi
  fi
else
  echo "⚠️  clang-format not found — skipping proto files  (apt install clang-format)"
fi

if $CHECK_MODE && [[ $CHANGED -ne 0 ]]; then
  exit 1
fi
