#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <string>

#include "kubeforward/runtime/process_runner.h"

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
