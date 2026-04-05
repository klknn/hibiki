// Windows (Winsock2) implementation of TCP socket shim (tcp.hpp).

#include "tcp.hpp"

#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")

#include <cstdio>

void tcp_init() {
  static bool initialized = false;
  if (!initialized) {
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
    initialized = true;
  }
}

int tcp_close(socket_t s) { return closesocket(s); }

int tcp_send(socket_t s, const void* buf, size_t len) {
  return ::send(s, reinterpret_cast<const char*>(buf), (int)len, 0);
}

int tcp_recv(socket_t s, void* buf, size_t len) {
  return ::recv(s, reinterpret_cast<char*>(buf), (int)len, 0);
}

void tcp_setsockopt(socket_t s, int level, int optname, int val) {
  ::setsockopt(s, level, optname, reinterpret_cast<const char*>(&val),
               sizeof(val));
}

const char* tcp_strerror() {
  static thread_local char buf[64];
  snprintf(buf, sizeof(buf), "WSA error %d", WSAGetLastError());
  return buf;
}
