// Windows implementation of platform-specific PluginProxy methods.
// See plugin_proxy_posix.cpp for the POSIX equivalent.

#include "engine/plugin/plugin_proxy.hpp"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstring>
#include <iostream>

#include "absl/log/log.h"
#include "engine/ipc/worker_channel_local.hpp"

namespace hibiki {

// Defined in plugin_proxy.cpp — shared across platform files.
std::string generateUniqueId();

namespace {

std::string findWorkerBinary() {
  char exe_path[MAX_PATH];
  DWORD len = GetModuleFileNameA(NULL, exe_path, MAX_PATH);
  if (len == 0) return "hbk-plugin-worker.exe";
  std::string path(exe_path, len);
  size_t slash = path.find_last_of("\\/");
  if (slash != std::string::npos) {
    path = path.substr(0, slash + 1) + "hbk-plugin-worker.exe";
  }
  return path;
}

}  // namespace

bool PluginProxy::spawnLocalWorker() {
  std::string uid = generateUniqueId();
  // Windows: use Named Pipe name + File Mapping name
  socket_path_ = "hbk-plugin-" + uid;
  shm_name_ = "hbk-plugin-" + uid;

  auto* ch = WorkerChannelLocal::createServer(socket_path_, shm_name_, 512, 2);
  if (!ch) return false;
  channel_.reset(ch);

  std::string worker_bin = findWorkerBinary();
  std::string cmd_line =
      "\"" + worker_bin + "\" " + socket_path_ + " " + shm_name_;

  // Create a Job Object so the worker dies when the host exits
  HANDLE hJob = CreateJobObjectA(NULL, NULL);
  if (hJob) {
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION jeli = {};
    jeli.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    SetInformationJobObject(hJob, JobObjectExtendedLimitInformation, &jeli,
                            sizeof(jeli));
  }

  STARTUPINFOA si = {};
  si.cb = sizeof(si);
  PROCESS_INFORMATION pi = {};

  if (!CreateProcessA(NULL, cmd_line.data(), NULL, NULL, FALSE,
                      CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
    LOG(ERROR) << "PluginProxy: CreateProcess failed: " << GetLastError();
    if (hJob) CloseHandle(hJob);
    channel_.reset();
    return false;
  }

  worker_handle_ = pi.hProcess;
  CloseHandle(pi.hThread);

  // Assign worker to job object (auto-kill on host exit)
  if (hJob) {
    AssignProcessToJobObject(hJob, pi.hProcess);
    // Keep hJob open — it auto-closes when host exits, killing children
  }

  if (!static_cast<WorkerChannelLocal*>(channel_.get())->accept()) {
    LOG(ERROR) << "PluginProxy: accept() failed";
    TerminateProcess(worker_handle_, 1);
    CloseHandle(worker_handle_);
    worker_handle_ = nullptr;
    if (hJob) CloseHandle(hJob);
    channel_.reset();
    return false;
  }

  return true;
}

bool PluginProxy::isWorkerAlive() const {
  if (is_remote_) return channel_ != nullptr;
  if (!worker_handle_) return false;
  DWORD exitCode;
  if (!GetExitCodeProcess(worker_handle_, &exitCode)) return false;
  return exitCode == STILL_ACTIVE;
}

void PluginProxy::killWorker() {
  if (!is_remote_ && worker_handle_) {
    if (isWorkerAlive()) {
      TerminateProcess(worker_handle_, 1);
      WaitForSingleObject(worker_handle_, 5000);
    }
    CloseHandle(worker_handle_);
    worker_handle_ = nullptr;
  }
}

}  // namespace hibiki
