#include <catch2/catch_test_macros.hpp>

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
