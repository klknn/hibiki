#pragma once

#include <string>
#include <vector>

#include "worker_channel.hpp"

// Platform socket type abstraction
#ifdef _WIN32
#include <winsock2.h>
using socket_t = SOCKET;
constexpr socket_t INVALID_SOCK = INVALID_SOCKET;
#else
using socket_t = int;
constexpr socket_t INVALID_SOCK = -1;
#endif

// TCP implementation of WorkerChannel for remote workers.
// Uses TCP sockets for both commands AND audio data (no shared memory).
// Audio buffers are heap-allocated locally and serialized into protobuf
// messages during process().
//
// Cross-platform: uses Winsock2 on Windows, BSD sockets on POSIX.
class WorkerChannelTcp : public WorkerChannel {
 public:
  // Connect to a remote worker daemon (client mode).
  static WorkerChannelTcp* createClient(const std::string& host, int port,
                                        int block_size, int num_channels);

  // Accept a connection on a listening socket (server mode, for daemon).
  static WorkerChannelTcp* createServer(int listen_port);

  ~WorkerChannelTcp() override;

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
  int numChannels() const { return num_channels_; }
  int listenPort() const { return listen_port_; }

 private:
  WorkerChannelTcp() = default;

  socket_t listen_fd_ = INVALID_SOCK;
  socket_t conn_fd_ = INVALID_SOCK;
  int block_size_ = 512;
  int num_channels_ = 2;
  int listen_port_ = 0;

  // Heap-allocated audio buffers (no shared memory for TCP).
  std::vector<std::vector<float>> input_bufs_;
  std::vector<std::vector<float>> output_bufs_;
};
