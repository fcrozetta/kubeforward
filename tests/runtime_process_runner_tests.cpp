#include <catch2/catch_test_macros.hpp>

#include <cerrno>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <signal.h>
#include <string>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

#include "kubeforward/runtime/process_runner.h"

namespace {

std::filesystem::path TempRunnerPath(const std::string& name) {
  const auto base = std::filesystem::temp_directory_path() / "kubeforward-process-runner-tests";
  std::filesystem::create_directories(base);
  return base / name;
}

std::string ReadFile(const std::filesystem::path& path) {
  std::ifstream input(path);
  return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

bool CleanupProcessGroup(int pid) {
  if (pid <= 0) {
    return true;
  }
  (void)::kill(-pid, SIGKILL);
  for (int waited_ms = 0; waited_ms < 1000; waited_ms += 50) {
    const pid_t wait_result = ::waitpid(pid, nullptr, WNOHANG);
    if (wait_result == pid || (wait_result < 0 && errno == ECHILD)) {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
  return false;
}

}  // namespace

TEST_CASE("noop process runner allocates stable synthetic pids", "[runtime]") {
  kubeforward::runtime::NoopProcessRunner runner;
  std::string error;

  kubeforward::runtime::StartProcessRequest request;
  request.argv = {"kubectl", "port-forward", "pod/api", "7000:7000"};
  auto started = runner.Start(request, error);

  REQUIRE(started.has_value());
  REQUIRE(started->pid > 0);
  REQUIRE(error.empty());
}

TEST_CASE("noop process runner rejects empty argv", "[runtime]") {
  kubeforward::runtime::NoopProcessRunner runner;
  std::string error;

  kubeforward::runtime::StartProcessRequest request;
  auto started = runner.Start(request, error);

  REQUIRE_FALSE(started.has_value());
  REQUIRE_FALSE(error.empty());
}

TEST_CASE("noop process runner validates pid on stop", "[runtime]") {
  kubeforward::runtime::NoopProcessRunner runner;
  std::string error;

  REQUIRE(runner.Stop(1234, error));
  REQUIRE(error.empty());

  REQUIRE_FALSE(runner.Stop(0, error));
  REQUIRE_FALSE(error.empty());
}

TEST_CASE("posix process runner starts and stops a process group", "[runtime]") {
  kubeforward::runtime::PosixProcessRunner runner;
  kubeforward::runtime::StartProcessRequest request;
  request.argv = {"/bin/sh", "-c", "sleep 30"};
  request.cwd = std::filesystem::current_path();
  std::string error;

  const auto started = runner.Start(request, error);
  REQUIRE(started.has_value());
  REQUIRE(started->pid > 0);
  REQUIRE(error.empty());

  REQUIRE(runner.Stop(started->pid, error));
  REQUIRE(error.empty());
}

TEST_CASE("posix process runner reports exec failures", "[runtime]") {
  kubeforward::runtime::PosixProcessRunner runner;
  kubeforward::runtime::StartProcessRequest request;
  request.argv = {"/path/that/does/not/exist"};
  request.cwd = std::filesystem::current_path();
  std::string error;

  const auto started = runner.Start(request, error);
  REQUIRE_FALSE(started.has_value());
  REQUIRE_FALSE(error.empty());
}

TEST_CASE("posix process runner treats missing pid as already stopped", "[runtime]") {
  kubeforward::runtime::PosixProcessRunner runner;
  std::string error;

  REQUIRE(runner.Stop(999999, error));
  REQUIRE(error.empty());
}

TEST_CASE("posix process runner writes daemon output to its log file", "[runtime]") {
  kubeforward::runtime::PosixProcessRunner runner;
  kubeforward::runtime::StartProcessRequest request;
  const auto log_path = TempRunnerPath("daemon.log");
  std::filesystem::remove(log_path);

  request.argv = {"/bin/sh", "-c", "echo daemon-output; sleep 30"};
  request.cwd = std::filesystem::current_path();
  request.daemon = true;
  request.log_path = log_path;

  std::string error;
  const auto started = runner.Start(request, error);
  REQUIRE(started.has_value());
  REQUIRE(error.empty());

  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  REQUIRE(std::filesystem::exists(log_path));
  CHECK(ReadFile(log_path).find("daemon-output") != std::string::npos);
  CHECK((runner.Stop(started->pid, error) || CleanupProcessGroup(started->pid)));
}

TEST_CASE("posix process runner keeps foreground launches on inherited stdio", "[runtime]") {
  kubeforward::runtime::PosixProcessRunner runner;
  kubeforward::runtime::StartProcessRequest request;
  const auto log_path = TempRunnerPath("foreground.log");
  std::filesystem::remove(log_path);

  request.argv = {"/bin/sh", "-c", "echo foreground-output; sleep 30"};
  request.cwd = std::filesystem::current_path();
  request.daemon = false;
  request.log_path = log_path;

  std::string error;
  const auto started = runner.Start(request, error);
  REQUIRE(started.has_value());
  REQUIRE(error.empty());

  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  CHECK_FALSE(std::filesystem::exists(log_path));
  CHECK((runner.Stop(started->pid, error) || CleanupProcessGroup(started->pid)));
}
