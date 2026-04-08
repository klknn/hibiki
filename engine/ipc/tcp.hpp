#pragma once

// Platform-agnostic TCP socket shim.
// Implemented in tcp_posix.cpp (Linux/macOS) and tcp_win32.cpp (Windows).
// BUILD selects the right one.
//
// Defines socket_t, INVALID_SOCK, and cross-platform socket includes.

#include <cstddef>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
using socket_t = SOCKET;
constexpr socket_t INVALID_SOCK = INVALID_SOCKET;
#else
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
using socket_t = int;
constexpr socket_t INVALID_SOCK = -1;
#endif

namespace hibiki {

// Called once before any socket operations (no-op on POSIX, WSAStartup on
// Windows).
void tcp_init();

// Close a socket.
int tcp_close(socket_t s);

// Send raw bytes. Returns number of bytes sent, or <= 0 on error.
int tcp_send(socket_t s, const void* buf, size_t len);

// Receive raw bytes. Returns number of bytes received, or <= 0 on error.
int tcp_recv(socket_t s, void* buf, size_t len);

// Set an integer socket option.
void tcp_setsockopt(socket_t s, int level, int optname, int val);

// Return a human-readable error string for the last socket error.
const char* tcp_strerror();

}  // namespace hibiki
