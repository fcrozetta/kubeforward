#include "kubeforward/runtime/process_runner.h"

#include <sstream>

namespace kubeforward::runtime {

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
