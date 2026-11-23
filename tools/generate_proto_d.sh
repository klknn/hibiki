#!/usr/bin/env bash
set -euo pipefail

# Build the hermetic protoc and protoc-gen-d plugin via Bazel
bazel build @protobuf//:protoc //tools/protoc-gen-d:protoc-gen-d -c opt

PROTOC_PATH=$(bazel cquery --output=files @protobuf//:protoc 2>/dev/null | head -n 1)
if [[ ! -x "${PROTOC_PATH:-}" ]]; then
  PROTOC_PATH="protoc"
fi

PLUGIN_PATH="bazel-bin/tools/protoc-gen-d/protoc-gen-d"

"${PROTOC_PATH}" \
  --plugin="protoc-gen-d=${PLUGIN_PATH}" \
  --d_out=. \
  -I. \
  pb/core.proto \
  pb/commands.proto \
  pb/notifications.proto \
  pb/plugin_worker.proto

echo "✅ Successfully generated D Protobuf definitions in pb/"
