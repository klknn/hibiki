#include "worker_channel_local.hpp"

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <cstring>
#include <iostream>


namespace hibiki {

// Platform-specific state for POSIX.
struct WorkerChannelLocal::Impl {
  int listen_fd = -1;
  int conn_fd = -1;
  int shm_fd = -1;
};

static size_t computeShmSize(int block_size, int num_channels) {
  return sizeof(SharedMemHeader) +
         (size_t)block_size * sizeof(float) * num_channels * 2;
}

WorkerChannelLocal* WorkerChannelLocal::createServer(
    const std::string& socket_path, const std::string& shm_name,
    int block_size, int num_channels) {
  auto* ch = new WorkerChannelLocal();
  ch->impl_ = std::make_unique<Impl>();
  ch->path_or_name_ = socket_path;
  ch->shm_name_ = shm_name;
  ch->block_size_ = block_size;
  ch->num_channels_ = num_channels;
  ch->is_server_ = true;
  ch->shm_size_ = computeShmSize(block_size, num_channels);

  // Create Unix domain socket
  ch->impl_->listen_fd = socket(AF_UNIX, SOCK_STREAM, 0);
  if (ch->impl_->listen_fd < 0) {
    std::cerr << "WorkerChannel: socket() failed: " << strerror(errno) << "\n";
    delete ch;
    return nullptr;
  }

  // Bind
  unlink(socket_path.c_str());
  struct sockaddr_un addr;
  memset(&addr, 0, sizeof(addr));
  addr.sun_family = AF_UNIX;
  strncpy(addr.sun_path, socket_path.c_str(), sizeof(addr.sun_path) - 1);

  if (bind(ch->impl_->listen_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
    std::cerr << "WorkerChannel: bind() failed: " << strerror(errno) << "\n";
    delete ch;
    return nullptr;
  }

  if (listen(ch->impl_->listen_fd, 1) < 0) {
    std::cerr << "WorkerChannel: listen() failed: " << strerror(errno) << "\n";
    delete ch;
    return nullptr;
  }

  // Create shared memory
  shm_unlink(shm_name.c_str());
  ch->impl_->shm_fd = shm_open(shm_name.c_str(), O_CREAT | O_RDWR, 0600);
  if (ch->impl_->shm_fd < 0) {
    std::cerr << "WorkerChannel: shm_open() failed: " << strerror(errno)
              << "\n";
    delete ch;
    return nullptr;
  }

  if (ftruncate(ch->impl_->shm_fd, ch->shm_size_) < 0) {
    std::cerr << "WorkerChannel: ftruncate() failed: " << strerror(errno)
              << "\n";
    delete ch;
    return nullptr;
  }

  ch->shm_ptr_ = mmap(nullptr, ch->shm_size_, PROT_READ | PROT_WRITE,
                      MAP_SHARED, ch->impl_->shm_fd, 0);
  if (ch->shm_ptr_ == MAP_FAILED) {
    std::cerr << "WorkerChannel: mmap() failed: " << strerror(errno) << "\n";
    ch->shm_ptr_ = nullptr;
    delete ch;
    return nullptr;
  }

  ch->header_ = reinterpret_cast<SharedMemHeader*>(ch->shm_ptr_);
  ch->header_->block_size = block_size;
  ch->header_->num_channels = num_channels;
  ch->header_->flags = SHM_FLAG_IDLE;

  return ch;
}

WorkerChannelLocal* WorkerChannelLocal::createClient(
    const std::string& socket_path, const std::string& shm_name) {
  auto* ch = new WorkerChannelLocal();
  ch->impl_ = std::make_unique<Impl>();
  ch->path_or_name_ = socket_path;
  ch->shm_name_ = shm_name;
  ch->is_server_ = false;

  // Connect to Unix domain socket
  ch->impl_->conn_fd = socket(AF_UNIX, SOCK_STREAM, 0);
  if (ch->impl_->conn_fd < 0) {
    std::cerr << "WorkerChannel: socket() failed: " << strerror(errno) << "\n";
    delete ch;
    return nullptr;
  }

  struct sockaddr_un addr;
  memset(&addr, 0, sizeof(addr));
  addr.sun_family = AF_UNIX;
  strncpy(addr.sun_path, socket_path.c_str(), sizeof(addr.sun_path) - 1);

  if (connect(ch->impl_->conn_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
    std::cerr << "WorkerChannel: connect() failed: " << strerror(errno)
              << "\n";
    delete ch;
    return nullptr;
  }

  // Open existing shared memory
  ch->impl_->shm_fd = shm_open(shm_name.c_str(), O_RDWR, 0600);
  if (ch->impl_->shm_fd < 0) {
    std::cerr << "WorkerChannel: shm_open() failed: " << strerror(errno)
              << "\n";
    delete ch;
    return nullptr;
  }

  off_t size = lseek(ch->impl_->shm_fd, 0, SEEK_END);
  lseek(ch->impl_->shm_fd, 0, SEEK_SET);
  ch->shm_size_ = (size_t)size;

  ch->shm_ptr_ = mmap(nullptr, ch->shm_size_, PROT_READ | PROT_WRITE,
                      MAP_SHARED, ch->impl_->shm_fd, 0);
  if (ch->shm_ptr_ == MAP_FAILED) {
    std::cerr << "WorkerChannel: mmap() failed: " << strerror(errno) << "\n";
    ch->shm_ptr_ = nullptr;
    delete ch;
    return nullptr;
  }

  ch->header_ = reinterpret_cast<SharedMemHeader*>(ch->shm_ptr_);
  ch->block_size_ = ch->header_->block_size;
  ch->num_channels_ = ch->header_->num_channels;

  return ch;
}

bool WorkerChannelLocal::accept() {
  if (impl_->listen_fd < 0) return false;
  impl_->conn_fd = ::accept(impl_->listen_fd, nullptr, nullptr);
  if (impl_->conn_fd < 0) {
    std::cerr << "WorkerChannel: accept() failed: " << strerror(errno) << "\n";
    return false;
  }
  return true;
}

WorkerChannelLocal::~WorkerChannelLocal() {
  if (shm_ptr_ && shm_ptr_ != MAP_FAILED) {
    munmap(shm_ptr_, shm_size_);
  }
  if (impl_) {
    if (impl_->shm_fd >= 0) close(impl_->shm_fd);
    if (impl_->conn_fd >= 0) close(impl_->conn_fd);
    if (impl_->listen_fd >= 0) close(impl_->listen_fd);
  }

  if (is_server_) {
    if (!path_or_name_.empty()) unlink(path_or_name_.c_str());
    if (!shm_name_.empty()) shm_unlink(shm_name_.c_str());
  }
}

bool WorkerChannelLocal::send(const void* data, size_t len) {
  if (impl_->conn_fd < 0) return false;
  const uint8_t* p = reinterpret_cast<const uint8_t*>(data);
  size_t remaining = len;
  while (remaining > 0) {
    ssize_t n = ::write(impl_->conn_fd, p, remaining);
    if (n <= 0) return false;
    p += n;
    remaining -= n;
  }
  return true;
}

bool WorkerChannelLocal::recv(void* buf, size_t len) {
  if (impl_->conn_fd < 0) return false;
  uint8_t* p = reinterpret_cast<uint8_t*>(buf);
  size_t remaining = len;
  while (remaining > 0) {
    ssize_t n = ::read(impl_->conn_fd, p, remaining);
    if (n <= 0) return false;
    p += n;
    remaining -= n;
  }
  return true;
}

bool WorkerChannelLocal::sendMessage(const void* data, size_t len) {
  uint32_t size = static_cast<uint32_t>(len);
  if (!send(&size, sizeof(size))) return false;
  return send(data, len);
}

int WorkerChannelLocal::recvMessage(std::string& out) {
  uint32_t size = 0;
  if (!recv(&size, sizeof(size))) return -1;
  if (size > 1024 * 1024) return -1;
  out.resize(size);
  if (!recv(out.data(), size)) return -1;
  return (int)size;
}

float* WorkerChannelLocal::inputBuffer(int channel) {
  if (!shm_ptr_ || channel < 0 || channel >= num_channels_) return nullptr;
  auto* base = reinterpret_cast<uint8_t*>(shm_ptr_);
  return reinterpret_cast<float*>(base + sizeof(SharedMemHeader) +
                                  (size_t)channel * block_size_ * sizeof(float));
}

float* WorkerChannelLocal::outputBuffer(int channel) {
  if (!shm_ptr_ || channel < 0 || channel >= num_channels_) return nullptr;
  auto* base = reinterpret_cast<uint8_t*>(shm_ptr_);
  return reinterpret_cast<float*>(
      base + sizeof(SharedMemHeader) +
      (size_t)(num_channels_ + channel) * block_size_ * sizeof(float));
}

}  // namespace hibiki
