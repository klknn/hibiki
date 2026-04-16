// POSIX implementation of platform-specific PluginProxy methods.
// See plugin_proxy_win32.cpp for the Windows equivalent.

#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cstring>
#include <iostream>

#include "engine/ipc/worker_channel_local.hpp"
#include "engine/plugin/plugin_proxy.hpp"

namespace hibiki {

// Defined in plugin_proxy.cpp — shared across platform files.
std::string generateUniqueId();

namespace {

std::string findWorkerBinary() {
  char exe_path[1024];
  ssize_t len = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
  if (len <= 0) return "hbk-plugin-worker";
  exe_path[len] = '\0';
  std::string path(exe_path);
  size_t slash = path.find_last_of('/');
  if (slash != std::string::npos) {
    path = path.substr(0, slash + 1) + "hbk-plugin-worker";
  }
  return path;
}

}  // namespace

bool PluginProxy::spawnLocalWorker() {
  std::string uid = generateUniqueId();
  socket_path_ = "/tmp/hbk-plugin-" + uid + ".sock";
  shm_name_ = "/hbk-plugin-" + uid;

  auto* ch = WorkerChannelLocal::createServer(socket_path_, shm_name_, 512, 2);
  if (!ch) return false;
  channel_.reset(ch);

  std::string worker_bin = findWorkerBinary();
  worker_pid_ = fork();
  if (worker_pid_ < 0) {
    std::cerr << "PluginProxy: fork() failed: " << strerror(errno) << "\n";
    channel_.reset();
    return false;
  }

  if (worker_pid_ == 0) {
    execl(worker_bin.c_str(), "hbk-plugin-worker", socket_path_.c_str(),
          shm_name_.c_str(), nullptr);
    std::cerr << "PluginProxy: execl() failed: " << strerror(errno) << "\n";
    _exit(1);
  }

  if (!static_cast<WorkerChannelLocal*>(channel_.get())->accept()) {
    std::cerr << "PluginProxy: accept() failed\n";
    kill(worker_pid_, SIGTERM);
    waitpid(worker_pid_, nullptr, 0);
    worker_pid_ = -1;
    channel_.reset();
    return false;
  }

  return true;
}

bool PluginProxy::isWorkerAlive() const {
  if (is_remote_) return channel_ != nullptr;
  if (worker_pid_ <= 0) return false;
  int status;
  int result = waitpid(worker_pid_, &status, WNOHANG);
  return result == 0;
}

void PluginProxy::killWorker() {
  if (!is_remote_ && worker_pid_ > 0) {
    int status;
    waitpid(worker_pid_, &status, WNOHANG);
    if (isWorkerAlive()) {
      kill(worker_pid_, SIGTERM);
      waitpid(worker_pid_, &status, 0);
    }
  }
}

}  // namespace hibiki
