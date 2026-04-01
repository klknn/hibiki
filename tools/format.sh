#!/bin/bash
# Format all source files in the repository.
# Usage: tools/format.sh [--check]
#   --check   Dry-run mode: exit 1 if any files would change.

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CHECK_MODE=false
[[ "${1:-}" == "--check" ]] && CHECK_MODE=true

CHANGED=0

# ─── C++ (clang-format, Google style) ────────────────────────────────
if command -v clang-format &>/dev/null; then
  CPP_FILES=$(find "$REPO_ROOT" -maxdepth 1 -type f \( -name '*.cpp' -o -name '*.hpp' -o -name '*.cc' -o -name '*.h' \))
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
if command -v google-java-format &>/dev/null; then
  JAVA_FILES=$(find "$REPO_ROOT/src" -type f -name '*.java' 2>/dev/null || true)
  if [[ -n "$JAVA_FILES" ]]; then
    if $CHECK_MODE; then
      if echo "$JAVA_FILES" | xargs google-java-format --dry-run --set-exit-if-changed &>/dev/null; then
        echo "✅ Java files are formatted"
      else
        echo "❌ Java files need formatting"
        CHANGED=1
      fi
    else
      echo "$JAVA_FILES" | xargs google-java-format -i
      echo "✅ Formatted $(echo "$JAVA_FILES" | wc -l) Java files"
    fi
  fi
else
  echo "⚠️  google-java-format not found — skipping Java"
fi

# ─── Starlark / BUILD files (buildifier) ─────────────────────────────
if command -v buildifier &>/dev/null; then
  if $CHECK_MODE; then
    if buildifier -mode=check -r "$REPO_ROOT" 2>/dev/null; then
      echo "✅ BUILD files are formatted"
    else
      echo "❌ BUILD files need formatting"
      CHANGED=1
    fi
  else
    buildifier -r "$REPO_ROOT"
    echo "✅ Formatted BUILD files"
  fi
else
  echo "⚠️  buildifier not found — skipping BUILD files"
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
