#pragma once

#include <cstdint>

// Shared memory header — used by both Unix and Win32 worker channel
// implementations to describe the layout of the audio buffer region.
//
// Layout (per sandbox.md):
//   Offset  Size        Content
//   0       64 B        Header (block_size, num_channels, flags)
//   64      N×4 B       Input L
//   64+N×4  N×4 B       Input R
//   64+N×8  N×4 B       Output L
//   64+N×12 N×4 B       Output R

struct SharedMemHeader {
  int32_t block_size;
  int32_t num_channels;
  int32_t flags;  // IDLE=0, READY=1, DONE=2
  int32_t reserved[13];  // Pad to 64 bytes
};

static_assert(sizeof(SharedMemHeader) == 64, "Header must be 64 bytes");

enum SharedMemFlags {
  SHM_FLAG_IDLE = 0,
  SHM_FLAG_READY = 1,
  SHM_FLAG_DONE = 2,
};

// Helper: compute total shared memory size for given block_size and channels.
inline size_t computeShmSize(int block_size, int num_channels) {
  return sizeof(SharedMemHeader) +
         (size_t)block_size * sizeof(float) * num_channels * 2;
}
