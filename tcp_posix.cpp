// POSIX implementation of TCP socket shim (tcp.hpp).

#include "tcp.hpp"

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>


namespace hibiki {

void tcp_init() {}  // no-op on POSIX

int tcp_close(socket_t s) { return ::close(s); }

int tcp_send(socket_t s, const void* buf, size_t len) {
  return (int)::write(s, buf, len);
}

int tcp_recv(socket_t s, void* buf, size_t len) {
  return (int)::read(s, buf, len);
}

void tcp_setsockopt(socket_t s, int level, int optname, int val) {
  ::setsockopt(s, level, optname, &val, sizeof(val));
}

const char* tcp_strerror() { return strerror(errno); }

}  // namespace hibiki
