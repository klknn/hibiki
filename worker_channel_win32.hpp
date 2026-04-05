#pragma once

#ifdef _WIN32

#include <windows.h>

#include <string>

#include "worker_channel.hpp"

// Windows implementation of WorkerChannel using:
// - Named Pipes for command/response messages
// - File Mapping (shared memory) for audio buffers
//
// This is the Windows equivalent of WorkerChannelUnix.
// See docs/ipc.md for the POSIX → Windows API mapping.

struct SharedMemHeader {
  int32_t block_size;
  int32_t num_channels;
  int32_t flags;  // IDLE=0, READY=1, DONE=2
  int32_t reserved[13];  // Pad to 64 bytes
};

static_assert(sizeof(SharedMemHeader) == 64, "Header must be 64 bytes");

class WorkerChannelWin32 : public WorkerChannel {
 public:
  // Create the server side (host): creates named pipe + shared memory.
  // Call accept() after spawning the worker.
  static WorkerChannelWin32* createServer(const std::string& pipe_name,
                                          const std::string& shm_name,
                                          int block_size, int num_channels);

  // Create the client side (worker): connects to existing pipe + shm.
  static WorkerChannelWin32* createClient(const std::string& pipe_name,
                                          const std::string& shm_name);

  ~WorkerChannelWin32() override;

  // Accept a client connection (server side only).
  bool accept();

  // WorkerChannel interface
  bool send(const void* data, size_t len) override;
  bool recv(void* buf, size_t len) override;
  int recvMessage(std::string& out) override;
  bool sendMessage(const void* data, size_t len) override;
  float* inputBuffer(int channel) override;
  float* outputBuffer(int channel) override;

  int blockSize() const { return block_size_; }
  SharedMemHeader* header() const { return header_; }

 private:
  WorkerChannelWin32() = default;

  HANDLE pipe_handle_ = INVALID_HANDLE_VALUE;
  HANDLE shm_handle_ = NULL;
  void* shm_ptr_ = nullptr;
  size_t shm_size_ = 0;
  SharedMemHeader* header_ = nullptr;
  int block_size_ = 512;
  int num_channels_ = 2;
  std::string pipe_name_;
  std::string shm_name_;
  bool is_server_ = false;
};

#endif  // _WIN32
