// Windows (Winsock2) implementation of WorkerChannelTcp.
// Uses ws2_32.lib for socket operations.
// See worker_channel_tcp_posix.cpp for the POSIX equivalent.

#include "worker_channel_tcp.hpp"

#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")

#include <cstring>
#include <iostream>

// --- Platform shim (Winsock2) ---------------------------------------------

static void initWinsock() {
  static bool initialized = false;
  if (!initialized) {
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
    initialized = true;
  }
}

static int tcp_close(socket_t s) { return closesocket(s); }

static int tcp_send(socket_t s, const void* buf, size_t len) {
  return ::send(s, reinterpret_cast<const char*>(buf), (int)len, 0);
}

static int tcp_recv(socket_t s, void* buf, size_t len) {
  return ::recv(s, reinterpret_cast<char*>(buf), (int)len, 0);
}

static void tcp_setsockopt(socket_t s, int level, int optname, int val) {
  ::setsockopt(s, level, optname, reinterpret_cast<const char*>(&val),
               sizeof(val));
}

static const char* tcp_strerror() {
  static thread_local char buf[64];
  snprintf(buf, sizeof(buf), "WSA error %d", WSAGetLastError());
  return buf;
}

// --- Shared implementation ------------------------------------------------

WorkerChannelTcp* WorkerChannelTcp::createClient(const std::string& host,
                                                  int port, int block_size,
                                                  int num_channels) {
  initWinsock();
  auto* ch = new WorkerChannelTcp();
  ch->block_size_ = block_size;
  ch->num_channels_ = num_channels;

  ch->input_bufs_.resize(num_channels, std::vector<float>(block_size, 0.0f));
  ch->output_bufs_.resize(num_channels, std::vector<float>(block_size, 0.0f));

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
  if (ch->conn_fd_ == INVALID_SOCK) {
    std::cerr << "WorkerChannelTcp: socket() failed: " << tcp_strerror()
              << "\n";
    freeaddrinfo(res);
    delete ch;
    return nullptr;
  }

  tcp_setsockopt(ch->conn_fd_, IPPROTO_TCP, TCP_NODELAY, 1);

  if (connect(ch->conn_fd_, res->ai_addr, (int)res->ai_addrlen) != 0) {
    std::cerr << "WorkerChannelTcp: connect() failed: " << tcp_strerror()
              << "\n";
    freeaddrinfo(res);
    delete ch;
    return nullptr;
  }

  freeaddrinfo(res);
  return ch;
}

WorkerChannelTcp* WorkerChannelTcp::createServer(int listen_port) {
  initWinsock();
  auto* ch = new WorkerChannelTcp();
  ch->listen_port_ = listen_port;

  ch->listen_fd_ = socket(AF_INET, SOCK_STREAM, 0);
  if (ch->listen_fd_ == INVALID_SOCK) {
    std::cerr << "WorkerChannelTcp: socket() failed: " << tcp_strerror()
              << "\n";
    delete ch;
    return nullptr;
  }

  tcp_setsockopt(ch->listen_fd_, SOL_SOCKET, SO_REUSEADDR, 1);

  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = INADDR_ANY;
  addr.sin_port = htons(listen_port);

  if (bind(ch->listen_fd_, (struct sockaddr*)&addr, sizeof(addr)) != 0) {
    std::cerr << "WorkerChannelTcp: bind() failed: " << tcp_strerror() << "\n";
    delete ch;
    return nullptr;
  }

  if (listen(ch->listen_fd_, 8) != 0) {
    std::cerr << "WorkerChannelTcp: listen() failed: " << tcp_strerror()
              << "\n";
    delete ch;
    return nullptr;
  }

  return ch;
}

bool WorkerChannelTcp::accept() {
  if (listen_fd_ == INVALID_SOCK) return false;
  conn_fd_ = ::accept(listen_fd_, nullptr, nullptr);
  if (conn_fd_ == INVALID_SOCK) {
    std::cerr << "WorkerChannelTcp: accept() failed: " << tcp_strerror()
              << "\n";
    return false;
  }
  tcp_setsockopt(conn_fd_, IPPROTO_TCP, TCP_NODELAY, 1);
  return true;
}

WorkerChannelTcp::~WorkerChannelTcp() {
  if (conn_fd_ != INVALID_SOCK) tcp_close(conn_fd_);
  if (listen_fd_ != INVALID_SOCK) tcp_close(listen_fd_);
}

bool WorkerChannelTcp::send(const void* data, size_t len) {
  if (conn_fd_ == INVALID_SOCK) return false;
  const uint8_t* p = reinterpret_cast<const uint8_t*>(data);
  size_t remaining = len;
  while (remaining > 0) {
    int n = tcp_send(conn_fd_, p, remaining);
    if (n <= 0) return false;
    p += n;
    remaining -= n;
  }
  return true;
}

bool WorkerChannelTcp::recv(void* buf, size_t len) {
  if (conn_fd_ == INVALID_SOCK) return false;
  uint8_t* p = reinterpret_cast<uint8_t*>(buf);
  size_t remaining = len;
  while (remaining > 0) {
    int n = tcp_recv(conn_fd_, p, remaining);
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
  if (size > 4 * 1024 * 1024) return -1;
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
