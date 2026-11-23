#!/usr/bin/env bash
# Generate dub.json at the repository root from Bazel DInfo providers for serve-d / DCD IDE support.
# Usage: ./tools/gen_dub_json.sh
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd -P)"
cd "$REPO_ROOT"

bazel run //:gen_dub_json -c opt "$@"
