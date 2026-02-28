#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace kubeforward::runtime {

//! Process start request abstraction used by runtime orchestration.
struct StartProcessRequest {
  std::vector<std::string> argv;
  std::filesystem::path cwd;
  bool daemon = false;
  std::filesystem::path log_path;
};

//! Process handle metadata returned after successful process start.
struct StartedProcess {
  int pid = 0;
};

//! Runtime process control interface.
class ProcessRunner {
 public:
  virtual ~ProcessRunner() = default;

  virtual std::optional<StartedProcess> Start(const StartProcessRequest& request, std::string& error) = 0;
  virtual bool Stop(int pid, std::string& error) = 0;
};

//! POSIX-backed runner that launches and terminates real child process groups.
class PosixProcessRunner final : public ProcessRunner {
 public:
  std::optional<StartedProcess> Start(const StartProcessRequest& request, std::string& error) override;
  bool Stop(int pid, std::string& error) override;
};

//! No-op runner used while kubectl invocation is not yet wired.
class NoopProcessRunner final : public ProcessRunner {
 public:
  std::optional<StartedProcess> Start(const StartProcessRequest& request, std::string& error) override;
  bool Stop(int pid, std::string& error) override;

 private:
  int next_pid_ = 12000;
};

}  // namespace kubeforward::runtime
