#pragma once

#include <string>

#include "worker_channel.hpp"

// Unix implementation of WorkerChannel using:
// - Unix domain sockets for command/response messages
// - POSIX shared memory (shm_open/mmap) for audio buffers
//
// Shared memory layout (per sandbox.md):
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

class WorkerChannelUnix : public WorkerChannel {
 public:
  // Create the server side (host): creates socket, binds, listens.
  // Call accept() after spawning the worker.
  static WorkerChannelUnix* createServer(const std::string& socket_path,
                                         const std::string& shm_name,
                                         int block_size, int num_channels);

  // Create the client side (worker): connects to existing socket and shm.
  static WorkerChannelUnix* createClient(const std::string& socket_path,
                                         const std::string& shm_name);

  ~WorkerChannelUnix() override;

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
  WorkerChannelUnix() = default;

  int listen_fd_ = -1;  // Server listen socket
  int conn_fd_ = -1;    // Connected socket (both sides)
  int shm_fd_ = -1;
  void* shm_ptr_ = nullptr;
  size_t shm_size_ = 0;
  SharedMemHeader* header_ = nullptr;
  int block_size_ = 512;
  int num_channels_ = 2;
  std::string socket_path_;
  std::string shm_name_;
  bool is_server_ = false;
};
