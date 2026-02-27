#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "kubeforward/cli.h"

#ifndef KF_SOURCE_DIR
#error "KF_SOURCE_DIR must be defined"
#endif

namespace {

std::string Fixture(const std::string& name) {
  return std::string(KF_SOURCE_DIR) + "/tests/fixtures/" + name;
}

std::filesystem::path FixtureDir() { return std::string(KF_SOURCE_DIR) + "/tests/fixtures"; }

struct CliResult {
  int exit_code = 0;
  std::string out;
  std::string err;
};

class ScopedStreamCapture {
 public:
  ScopedStreamCapture() : original_out_(std::cout.rdbuf(out_.rdbuf())), original_err_(std::cerr.rdbuf(err_.rdbuf())) {}

  ~ScopedStreamCapture() {
    std::cout.rdbuf(original_out_);
    std::cerr.rdbuf(original_err_);
  }

  std::string out() const { return out_.str(); }
  std::string err() const { return err_.str(); }

 private:
  std::ostringstream out_;
  std::ostringstream err_;
  std::streambuf* original_out_ = nullptr;
  std::streambuf* original_err_ = nullptr;
};

class ScopedCurrentPath {
 public:
  explicit ScopedCurrentPath(const std::filesystem::path& new_path) : original_(std::filesystem::current_path()) {
    std::filesystem::current_path(new_path);
  }

  ~ScopedCurrentPath() { std::filesystem::current_path(original_); }

 private:
  std::filesystem::path original_;
};

class ScopedEnvVar {
 public:
  ScopedEnvVar(const char* key, const char* value) : key_(key) {
    const char* existing = std::getenv(key_);
    if (existing != nullptr) {
      had_original_ = true;
      original_ = existing;
    }
    ::setenv(key_, value, 1);
  }

  ~ScopedEnvVar() {
    if (had_original_) {
      ::setenv(key_, original_.c_str(), 1);
      return;
    }
    ::unsetenv(key_);
  }

 private:
  const char* key_;
  bool had_original_ = false;
  std::string original_;
};

CliResult RunAndCapture(const std::vector<std::string>& args) {
  ScopedStreamCapture capture;
  const int exit_code = kubeforward::run_cli(args);
  return {.exit_code = exit_code, .out = capture.out(), .err = capture.err()};
}

}  // namespace

TEST_CASE("plan succeeds with valid config", "[cli]") {
  std::vector<std::string> args = {"kubeforward", "plan", "-f", Fixture("basic.yaml"), "-e", "dev"};
  REQUIRE(kubeforward::run_cli(args) == 0);
}

TEST_CASE("default command is plan when no subcommand is provided", "[cli]") {
  ScopedCurrentPath cwd(FixtureDir());

  std::vector<std::string> args = {"kubeforward"};
  const int exit_code = kubeforward::run_cli(args);

  REQUIRE(exit_code == 0);
}

TEST_CASE("empty argv is handled without crashing", "[cli]") {
  std::vector<std::string> args = {};
  REQUIRE(kubeforward::run_cli(args) == 1);
}

TEST_CASE("default plan command accepts plan flags without subcommand", "[cli]") {
  std::vector<std::string> args = {"kubeforward", "--file", Fixture("basic.yaml"), "-e", "dev"};
  REQUIRE(kubeforward::run_cli(args) == 0);
}

TEST_CASE("default plan command accepts equals syntax for long flags", "[cli]") {
  std::vector<std::string> args = {"kubeforward", "--file=" + Fixture("basic.yaml"), "--env=dev", "--verbose=true"};
  const auto result = RunAndCapture(args);

  REQUIRE(result.exit_code == 0);
  CHECK(result.out.find("Environment: dev") != std::string::npos);
  CHECK(result.out.find("Environment: prod") == std::string::npos);
}

TEST_CASE("default plan command rejects equals syntax for short flags", "[cli]") {
  std::vector<std::string> args = {"kubeforward", "-f=" + Fixture("basic.yaml"), "-e=dev", "-v=true"};
  const auto result = RunAndCapture(args);

  REQUIRE(result.exit_code == 1);
  CHECK(result.err.find("Option '=' does not exist") != std::string::npos);
}

TEST_CASE("plan command rejects equals syntax for short flags", "[cli]") {
  std::vector<std::string> args = {"kubeforward", "plan", "-f=" + Fixture("basic.yaml"), "-e=dev", "-v=true"};
  const auto result = RunAndCapture(args);

  REQUIRE(result.exit_code == 1);
  CHECK(result.err.find("Option '=' does not exist") != std::string::npos);
}

TEST_CASE("version flag prints app version", "[cli]") {
  std::vector<std::string> args = {"kubeforward", "--version"};
  const auto result = RunAndCapture(args);

  REQUIRE(result.exit_code == 0);
  CHECK(result.out == std::string(KF_APP_VERSION) + "\n");
}

TEST_CASE("up defaults to the first environment when --env is omitted", "[cli]") {
  ScopedEnvVar noop_runner("KUBEFORWARD_USE_NOOP_RUNNER", "1");
  std::vector<std::string> args = {"kubeforward", "up", "--file", Fixture("basic.yaml")};
  const auto result = RunAndCapture(args);

  REQUIRE(result.exit_code == 0);
  CHECK(result.out.find("up: starting forwards") != std::string::npos);
  CHECK(result.out.find("env: dev") != std::string::npos);
}

TEST_CASE("up supports daemon mode and explicit environment", "[cli]") {
  ScopedEnvVar noop_runner("KUBEFORWARD_USE_NOOP_RUNNER", "1");
  std::vector<std::string> args = {"kubeforward", "up", "--file", Fixture("basic.yaml"), "--env", "dev", "--daemon"};
  const auto result = RunAndCapture(args);

  REQUIRE(result.exit_code == 0);
  CHECK(result.out.find("env: dev") != std::string::npos);
  CHECK(result.out.find("mode: daemon") != std::string::npos);
}

TEST_CASE("up supports verbose output", "[cli]") {
  ScopedEnvVar noop_runner("KUBEFORWARD_USE_NOOP_RUNNER", "1");
  std::vector<std::string> args = {"kubeforward", "up", "--file", Fixture("basic.yaml"), "--env", "dev", "--verbose"};
  const auto result = RunAndCapture(args);

  REQUIRE(result.exit_code == 0);
  CHECK(result.out.find("forward names:") != std::string::npos);
  CHECK(result.out.find("- api") != std::string::npos);
}

TEST_CASE("down defaults to all environments when --env is omitted", "[cli]") {
  ScopedEnvVar noop_runner("KUBEFORWARD_USE_NOOP_RUNNER", "1");
  std::vector<std::string> args = {"kubeforward", "down", "--file", Fixture("basic.yaml")};
  const auto result = RunAndCapture(args);

  REQUIRE(result.exit_code == 0);
  CHECK(result.out.find("scope: all environments") != std::string::npos);
  CHECK(result.out.find("environments: 2") != std::string::npos);
}

TEST_CASE("down supports explicit environment and daemon mode", "[cli]") {
  ScopedEnvVar noop_runner("KUBEFORWARD_USE_NOOP_RUNNER", "1");
  std::vector<std::string> args = {"kubeforward", "down", "--file", Fixture("basic.yaml"), "-e", "dev", "-d"};
  const auto result = RunAndCapture(args);

  REQUIRE(result.exit_code == 0);
  CHECK(result.out.find("scope: environment") != std::string::npos);
  CHECK(result.out.find("env: dev") != std::string::npos);
  CHECK(result.out.find("mode: daemon") != std::string::npos);
}

TEST_CASE("down supports verbose output", "[cli]") {
  ScopedEnvVar noop_runner("KUBEFORWARD_USE_NOOP_RUNNER", "1");
  std::vector<std::string> args = {"kubeforward", "down", "--file", Fixture("basic.yaml"), "--verbose"};
  const auto result = RunAndCapture(args);

  REQUIRE(result.exit_code == 0);
  CHECK(result.out.find("environment breakdown:") != std::string::npos);
  CHECK(result.out.find("- dev (1 forward(s))") != std::string::npos);
  CHECK(result.out.find("- prod (1 forward(s))") != std::string::npos);
}

TEST_CASE("commands are mutually exclusive by subcommand position", "[cli]") {
  std::vector<std::string> args = {"kubeforward", "up", "plan"};
  const auto result = RunAndCapture(args);

  REQUIRE(result.exit_code != 0);
  CHECK(result.err.find("up:") != std::string::npos);
}

TEST_CASE("unknown top-level flag is routed to plan and rejected", "[cli]") {
  std::vector<std::string> args = {"kubeforward", "--unknown"};
  const auto result = RunAndCapture(args);

  REQUIRE(result.exit_code == 1);
  CHECK(result.err.find("plan: ") != std::string::npos);
  CHECK(result.err.find("does not exist") != std::string::npos);
}

TEST_CASE("unknown top-level equals flag is routed to plan and rejected", "[cli]") {
  std::vector<std::string> args = {"kubeforward", "--version=1"};
  const auto result = RunAndCapture(args);

  REQUIRE(result.exit_code == 1);
  CHECK(result.err.find("plan: ") != std::string::npos);
  CHECK(result.err.find("does not exist") != std::string::npos);
}

TEST_CASE("plan fails when config missing", "[cli]") {
  std::vector<std::string> args = {"kubeforward", "plan", "--file", Fixture("missing.yaml")};
  REQUIRE(kubeforward::run_cli(args) != 0);
}

TEST_CASE("plan defaults to kubeforward.yaml in current directory", "[cli]") {
  ScopedCurrentPath cwd(FixtureDir());

  std::vector<std::string> args = {"kubeforward", "plan", "--env", "dev"};
  const int exit_code = kubeforward::run_cli(args);

  REQUIRE(exit_code == 0);
}

TEST_CASE("plan without env shows all environments", "[cli]") {
  std::vector<std::string> args = {"kubeforward", "plan", "--file", Fixture("basic.yaml")};
  const auto result = RunAndCapture(args);

  REQUIRE(result.exit_code == 0);
  CHECK(result.out.find("Environment: dev") != std::string::npos);
  CHECK(result.out.find("Environment: prod") != std::string::npos);
}

TEST_CASE("plan verbose shows detailed fields", "[cli]") {
  std::vector<std::string> args = {"kubeforward", "plan", "--file", Fixture("basic.yaml"), "--verbose"};
  const auto result = RunAndCapture(args);

  REQUIRE(result.exit_code == 0);
  CHECK(result.out.find("Config file: ") != std::string::npos);
  CHECK(result.out.find("Metadata:") != std::string::npos);
  CHECK(result.out.find("Defaults:") != std::string::npos);
  CHECK(result.out.find("settings:") != std::string::npos);
  CHECK(result.out.find("ports:") != std::string::npos);
}

TEST_CASE("plan env verbose filters to selected environment", "[cli]") {
  std::vector<std::string> args = {"kubeforward", "plan", "--file", Fixture("basic.yaml"), "-e", "dev", "-v"};
  const auto result = RunAndCapture(args);

  REQUIRE(result.exit_code == 0);
  CHECK(result.out.find("Environment: dev") != std::string::npos);
  CHECK(result.out.find("Environment: prod") == std::string::npos);
}

TEST_CASE("default plan with verbose shows all environments", "[cli]") {
  std::vector<std::string> args = {"kubeforward", "-v", "--file", Fixture("basic.yaml")};
  const auto result = RunAndCapture(args);

  REQUIRE(result.exit_code == 0);
  CHECK(result.out.find("Environment: dev") != std::string::npos);
  CHECK(result.out.find("Environment: prod") != std::string::npos);
}

TEST_CASE("plan accepts json config through --file", "[cli]") {
  std::vector<std::string> args = {"kubeforward", "plan", "--file", Fixture("basic.json"), "--env", "dev"};
  const auto result = RunAndCapture(args);

  REQUIRE(result.exit_code == 0);
  CHECK(result.out.find("Environment: dev") != std::string::npos);
}
