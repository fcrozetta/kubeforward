#include "kubeforward/runtime/process_runner.h"

#include <cerrno>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <sstream>
#include <thread>
#include <vector>

#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

namespace {

std::vector<char*> ToExecArgv(const std::vector<std::string>& args) {
  std::vector<char*> argv;
  argv.reserve(args.size() + 1);
  for (const auto& arg : args) {
    argv.push_back(const_cast<char*>(arg.c_str()));
  }
  argv.push_back(nullptr);
  return argv;
}

bool IsProcessGroupAlive(pid_t pgid) {
  if (pgid <= 0) {
    return false;
  }
  if (::kill(-pgid, 0) == 0) {
    return true;
  }
  return errno == EPERM;
}

bool WaitForProcessGroupExit(pid_t pgid, int timeout_ms) {
  const int step_ms = 100;
  int waited_ms = 0;
  while (waited_ms < timeout_ms) {
    const pid_t wait_result = ::waitpid(pgid, nullptr, WNOHANG);
    if (wait_result == pgid) {
      return true;
    }
    if (!IsProcessGroupAlive(pgid)) {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(step_ms));
    waited_ms += step_ms;
  }
  const pid_t wait_result = ::waitpid(pgid, nullptr, WNOHANG);
  if (wait_result == pgid) {
    return true;
  }
  return !IsProcessGroupAlive(pgid);
}

}  // namespace

namespace kubeforward::runtime {

std::optional<StartedProcess> PosixProcessRunner::Start(const StartProcessRequest& request, std::string& error) {
  if (request.argv.empty()) {
    error = "process argv cannot be empty";
    return std::nullopt;
  }

  if (!request.log_path.empty()) {
    std::error_code ec;
    std::filesystem::create_directories(request.log_path.parent_path(), ec);
    if (ec) {
      error = "failed to create log directory: " + ec.message();
      return std::nullopt;
    }
  }

  int exec_pipe[2] = {-1, -1};
  if (::pipe(exec_pipe) != 0) {
    error = "failed to create exec status pipe";
    return std::nullopt;
  }
  if (::fcntl(exec_pipe[1], F_SETFD, FD_CLOEXEC) != 0) {
    error = "failed to mark exec status pipe close-on-exec";
    ::close(exec_pipe[0]);
    ::close(exec_pipe[1]);
    return std::nullopt;
  }

  const pid_t pid = ::fork();
  if (pid < 0) {
    error = "failed to fork process";
    ::close(exec_pipe[0]);
    ::close(exec_pipe[1]);
    return std::nullopt;
  }

  if (pid == 0) {
    ::close(exec_pipe[0]);
    ::setpgid(0, 0);

    if (!request.cwd.empty() && ::chdir(request.cwd.c_str()) != 0) {
      const int child_errno = errno;
      (void)::write(exec_pipe[1], &child_errno, sizeof(child_errno));
      _exit(127);
    }

    const char* sink_path = "/dev/null";
    std::string sink_path_storage;
    if (!request.log_path.empty()) {
      sink_path_storage = request.log_path.string();
      sink_path = sink_path_storage.c_str();
    }

    const int in_fd = ::open("/dev/null", O_RDONLY);
    const int out_fd = ::open(sink_path, O_CREAT | O_WRONLY | O_APPEND, 0644);
    if (in_fd < 0 || out_fd < 0) {
      const int child_errno = errno;
      if (in_fd >= 0) {
        ::close(in_fd);
      }
      if (out_fd >= 0) {
        ::close(out_fd);
      }
      (void)::write(exec_pipe[1], &child_errno, sizeof(child_errno));
      _exit(127);
    }

    if (::dup2(in_fd, STDIN_FILENO) < 0 || ::dup2(out_fd, STDOUT_FILENO) < 0 || ::dup2(out_fd, STDERR_FILENO) < 0) {
      const int child_errno = errno;
      ::close(in_fd);
      ::close(out_fd);
      (void)::write(exec_pipe[1], &child_errno, sizeof(child_errno));
      _exit(127);
    }

    ::close(in_fd);
    ::close(out_fd);

    auto argv = ToExecArgv(request.argv);
    ::execvp(argv[0], argv.data());

    const int child_errno = errno;
    (void)::write(exec_pipe[1], &child_errno, sizeof(child_errno));
    _exit(127);
  }

  ::close(exec_pipe[1]);
  (void)::setpgid(pid, pid);

  int child_errno = 0;
  const ssize_t read_count = ::read(exec_pipe[0], &child_errno, sizeof(child_errno));
  ::close(exec_pipe[0]);

  if (read_count > 0) {
    std::ostringstream oss;
    oss << "failed to exec '" << request.argv.front() << "': " << std::strerror(child_errno);
    error = oss.str();
    (void)::waitpid(pid, nullptr, 0);
    return std::nullopt;
  }

  error.clear();
  return StartedProcess{static_cast<int>(pid)};
}

bool PosixProcessRunner::Stop(int pid, std::string& error) {
  if (pid <= 0) {
    std::ostringstream oss;
    oss << "invalid pid " << pid;
    error = oss.str();
    return false;
  }

  const pid_t pgid = static_cast<pid_t>(pid);
  if (::kill(-pgid, SIGTERM) != 0) {
    if (errno == ESRCH) {
      error.clear();
      return true;
    }
    std::ostringstream oss;
    oss << "failed to send SIGTERM to process group " << pgid << ": " << std::strerror(errno);
    error = oss.str();
    return false;
  }

  if (WaitForProcessGroupExit(pgid, 3000)) {
    error.clear();
    return true;
  }

  if (::kill(-pgid, SIGKILL) != 0 && errno != ESRCH) {
    std::ostringstream oss;
    oss << "failed to send SIGKILL to process group " << pgid << ": " << std::strerror(errno);
    error = oss.str();
    return false;
  }

  if (!WaitForProcessGroupExit(pgid, 1000)) {
    std::ostringstream oss;
    oss << "process group " << pgid << " did not exit after SIGKILL";
    error = oss.str();
    return false;
  }

  error.clear();
  return true;
}

std::optional<StartedProcess> NoopProcessRunner::Start(const StartProcessRequest& request, std::string& error) {
  if (request.argv.empty()) {
    error = "process argv cannot be empty";
    return std::nullopt;
  }
  error.clear();
  return StartedProcess{next_pid_++};
}

bool NoopProcessRunner::Stop(int pid, std::string& error) {
  if (pid <= 0) {
    std::ostringstream oss;
    oss << "invalid pid " << pid;
    error = oss.str();
    return false;
  }
  error.clear();
  return true;
}

}  // namespace kubeforward::runtime
