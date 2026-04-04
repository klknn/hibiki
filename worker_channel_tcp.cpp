#include "worker_channel_tcp.hpp"

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstring>
#include <iostream>

WorkerChannelTcp* WorkerChannelTcp::createClient(const std::string& host,
                                                  int port, int block_size,
                                                  int num_channels) {
  auto* ch = new WorkerChannelTcp();
  ch->block_size_ = block_size;
  ch->num_channels_ = num_channels;

  // Allocate audio buffers
  ch->input_bufs_.resize(num_channels, std::vector<float>(block_size, 0.0f));
  ch->output_bufs_.resize(num_channels, std::vector<float>(block_size, 0.0f));

  // Resolve host
  struct addrinfo hints, *res;
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;

  std::string port_str = std::to_string(port);
  if (getaddrinfo(host.c_str(), port_str.c_str(), &hints, &res) != 0) {
    std::cerr << "WorkerChannelTcp: getaddrinfo failed for " << host << ":"
              << port << "\n";
    delete ch;
    return nullptr;
  }

  ch->conn_fd_ = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
  if (ch->conn_fd_ < 0) {
    std::cerr << "WorkerChannelTcp: socket() failed: " << strerror(errno)
              << "\n";
    freeaddrinfo(res);
    delete ch;
    return nullptr;
  }

  // Disable Nagle's algorithm for low-latency
  int flag = 1;
  setsockopt(ch->conn_fd_, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));

  if (connect(ch->conn_fd_, res->ai_addr, res->ai_addrlen) < 0) {
    std::cerr << "WorkerChannelTcp: connect() failed: " << strerror(errno)
              << "\n";
    freeaddrinfo(res);
    delete ch;
    return nullptr;
  }

  freeaddrinfo(res);
  return ch;
}

WorkerChannelTcp* WorkerChannelTcp::createServer(int listen_port) {
  auto* ch = new WorkerChannelTcp();
  ch->listen_port_ = listen_port;

  ch->listen_fd_ = socket(AF_INET, SOCK_STREAM, 0);
  if (ch->listen_fd_ < 0) {
    std::cerr << "WorkerChannelTcp: socket() failed: " << strerror(errno)
              << "\n";
    delete ch;
    return nullptr;
  }

  int opt = 1;
  setsockopt(ch->listen_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = INADDR_ANY;
  addr.sin_port = htons(listen_port);

  if (bind(ch->listen_fd_, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
    std::cerr << "WorkerChannelTcp: bind() failed: " << strerror(errno)
              << "\n";
    delete ch;
    return nullptr;
  }

  if (listen(ch->listen_fd_, 8) < 0) {
    std::cerr << "WorkerChannelTcp: listen() failed: " << strerror(errno)
              << "\n";
    delete ch;
    return nullptr;
  }

  return ch;
}

bool WorkerChannelTcp::accept() {
  if (listen_fd_ < 0) return false;
  conn_fd_ = ::accept(listen_fd_, nullptr, nullptr);
  if (conn_fd_ < 0) {
    std::cerr << "WorkerChannelTcp: accept() failed: " << strerror(errno)
              << "\n";
    return false;
  }

  // Disable Nagle's algorithm
  int flag = 1;
  setsockopt(conn_fd_, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));

  return true;
}

WorkerChannelTcp::~WorkerChannelTcp() {
  if (conn_fd_ >= 0) close(conn_fd_);
  if (listen_fd_ >= 0) close(listen_fd_);
}

bool WorkerChannelTcp::send(const void* data, size_t len) {
  if (conn_fd_ < 0) return false;
  const uint8_t* p = reinterpret_cast<const uint8_t*>(data);
  size_t remaining = len;
  while (remaining > 0) {
    ssize_t n = ::write(conn_fd_, p, remaining);
    if (n <= 0) return false;
    p += n;
    remaining -= n;
  }
  return true;
}

bool WorkerChannelTcp::recv(void* buf, size_t len) {
  if (conn_fd_ < 0) return false;
  uint8_t* p = reinterpret_cast<uint8_t*>(buf);
  size_t remaining = len;
  while (remaining > 0) {
    ssize_t n = ::read(conn_fd_, p, remaining);
    if (n <= 0) return false;
    p += n;
    remaining -= n;
  }
  return true;
}

bool WorkerChannelTcp::sendMessage(const void* data, size_t len) {
  uint32_t size = static_cast<uint32_t>(len);
  if (!send(&size, sizeof(size))) return false;
  return send(data, len);
}

int WorkerChannelTcp::recvMessage(std::string& out) {
  uint32_t size = 0;
  if (!recv(&size, sizeof(size))) return -1;
  if (size > 4 * 1024 * 1024) return -1;  // 4MB safety limit
  out.resize(size);
  if (!recv(out.data(), size)) return -1;
  return (int)size;
}

float* WorkerChannelTcp::inputBuffer(int channel) {
  if (channel < 0 || channel >= num_channels_) return nullptr;
  if (input_bufs_.empty()) {
    input_bufs_.resize(num_channels_,
                       std::vector<float>(block_size_, 0.0f));
  }
  return input_bufs_[channel].data();
}

float* WorkerChannelTcp::outputBuffer(int channel) {
  if (channel < 0 || channel >= num_channels_) return nullptr;
  if (output_bufs_.empty()) {
    output_bufs_.resize(num_channels_,
                        std::vector<float>(block_size_, 0.0f));
  }
  return output_bufs_[channel].data();
}
