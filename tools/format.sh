#!/bin/bash
# Format all source files in the repository.
# Usage: tools/format.sh [--check]
#   --check   Dry-run mode: exit 1 if any files would change.

PATH=$JAVA_HOME/bin:$PATH

set -euo pipefail

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

# ─── Helper: ensure dfmt is available ────────────────────────────────
DFMT_VERSION="0.15.2"
DFMT_BIN="$TOOLS_DIR/dfmt"
ensure_dfmt() {
  if command -v dfmt &>/dev/null; then
    return 0
  fi
  if [[ -x "$DFMT_BIN" ]]; then
    return 0
  fi
  local dub_cmd=""
  local ldc_cmd=""
  if command -v dub &>/dev/null && command -v ldc2 &>/dev/null; then
    dub_cmd="$(command -v dub)"
    ldc_cmd="$(command -v ldc2)"
  else
    local ldc_bin_dir=""
    if [[ -L "$REPO_ROOT/bazel-out" ]]; then
      local real_out
      real_out="$(python3 -c 'import os,sys; print(os.path.realpath(sys.argv[1]))' "$REPO_ROOT/bazel-out")"
      local out_base="${real_out%%/execroot/*}"
      if [[ -x "$out_base/external/+ldc_repositories+ldc_toolchain/bin/dub" ]]; then
        ldc_bin_dir="$out_base/external/+ldc_repositories+ldc_toolchain/bin"
      fi
    fi
    if [[ -n "$ldc_bin_dir" ]]; then
      dub_cmd="$ldc_bin_dir/dub"
      ldc_cmd="$ldc_bin_dir/ldc2"
    fi
  fi
  if [[ -z "$dub_cmd" || -z "$ldc_cmd" ]]; then
    return 1
  fi
  echo "⬇️  Building dfmt v${DFMT_VERSION} into $DFMT_BIN..."
  local tmp_dir
  tmp_dir="$(mktemp -d)"
  (
    cd "$tmp_dir"
    if [[ "$(uname -s)" == "Darwin" && "$(uname -m)" == "arm64" ]]; then
      export DFLAGS="${DFLAGS:+$DFLAGS }-mtriple=arm64-apple-macos11.0"
      export MACOSX_DEPLOYMENT_TARGET="11.0"
    fi
    "$dub_cmd" fetch "dfmt@${DFMT_VERSION}" >/dev/null
    "$dub_cmd" build "dfmt@${DFMT_VERSION}" --compiler="$ldc_cmd" --build=release >/dev/null
    local pkg_dir
    pkg_dir="$("$dub_cmd" list | awk -v v="${DFMT_VERSION}" '{gsub(/:/, "", $2)} $1=="dfmt" && $2==v {print $3; exit}')"
    if [[ -n "$pkg_dir" && -x "$pkg_dir/bin/dfmt" ]]; then
      cp "$pkg_dir/bin/dfmt" "$DFMT_BIN"
      chmod +x "$DFMT_BIN"
    elif [[ -n "$pkg_dir" && -x "$pkg_dir/dfmt" ]]; then
      cp "$pkg_dir/dfmt" "$DFMT_BIN"
      chmod +x "$DFMT_BIN"
    fi
  )
  rm -rf "$tmp_dir"
  if [[ -x "$DFMT_BIN" ]]; then
    echo "✅ Built $DFMT_BIN"
    return 0
  fi
  return 1
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

# ─── D (dfmt) ────────────────────────────────────────────────────────
D_FILES=$(find "$REPO_ROOT" \
  -path "$REPO_ROOT/third_party" -prune -o \
  -path "$REPO_ROOT/bazel-*" -prune -o \
  -type f -name '*.d' -print 2>/dev/null || true)
if [[ -n "$D_FILES" ]]; then
  ensure_dfmt || true
  if command -v dfmt &>/dev/null; then
    DFMT_CMD="dfmt"
  elif [[ -x "$DFMT_BIN" ]]; then
    DFMT_CMD="$DFMT_BIN"
  else
    DFMT_CMD=""
  fi
  if [[ -n "$DFMT_CMD" ]]; then
    if $CHECK_MODE; then
      for f in $D_FILES; do
        if ! diff -q "$f" <("$DFMT_CMD" "$f") &>/dev/null; then
          echo "❌ D files need formatting: $f"
          CHANGED=1
        fi
      done
    else
      echo "$D_FILES" | xargs "$DFMT_CMD" -i
      echo "✅ Formatted $(echo "$D_FILES" | wc -l) D files"
    fi
  else
    echo "⚠️  dfmt not found and auto-build skipped (no dub/ldc2)"
  fi
fi

if $CHECK_MODE && [[ $CHANGED -ne 0 ]]; then
  exit 1
fi
