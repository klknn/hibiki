#pragma once

#include <cstddef>
#include <string>

namespace hibiki {

// Abstract channel for host ↔ worker communication.
// Implementations provide both a command channel (send/recv serialized protos)
// and shared memory audio buffers (zero-copy).
class WorkerChannel {
 public:
  virtual ~WorkerChannel() = default;

  // Send/receive length-prefixed messages on the command channel.
  virtual bool send(const void* data, size_t len) = 0;
  virtual bool recv(void* buf, size_t len) = 0;

  // Receive a length-prefixed message, allocating the buffer.
  // Returns the number of bytes received, or -1 on error.
  virtual int recvMessage(std::string& out) = 0;

  // Send a length-prefixed message (uint32 size + data).
  virtual bool sendMessage(const void* data, size_t len) = 0;

  // Access shared memory audio buffers.
  virtual float* inputBuffer(int channel) = 0;
  virtual float* outputBuffer(int channel) = 0;
};

}  // namespace hibiki
