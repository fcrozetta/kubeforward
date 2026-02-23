#include <catch2/catch_test_macros.hpp>

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

CliResult RunAndCapture(const std::vector<std::string>& args) {
  std::ostringstream out;
  std::ostringstream err;
  auto* original_out = std::cout.rdbuf(out.rdbuf());
  auto* original_err = std::cerr.rdbuf(err.rdbuf());
  const int exit_code = kubeforward::run_cli(args);
  std::cout.rdbuf(original_out);
  std::cerr.rdbuf(original_err);
  return {.exit_code = exit_code, .out = out.str(), .err = err.str()};
}

}  // namespace

TEST_CASE("plan succeeds with valid config", "[cli]") {
  std::vector<std::string> args = {"kubeforward", "plan", "-f", Fixture("basic.yaml"), "-e", "dev"};
  REQUIRE(kubeforward::run_cli(args) == 0);
}

TEST_CASE("default command is plan when no subcommand is provided", "[cli]") {
  const auto original = std::filesystem::current_path();
  std::filesystem::current_path(FixtureDir());

  std::vector<std::string> args = {"kubeforward"};
  const int exit_code = kubeforward::run_cli(args);

  std::filesystem::current_path(original);
  REQUIRE(exit_code == 0);
}

TEST_CASE("default plan command accepts plan flags without subcommand", "[cli]") {
  std::vector<std::string> args = {"kubeforward", "--file", Fixture("basic.yaml"), "-e", "dev"};
  REQUIRE(kubeforward::run_cli(args) == 0);
}

TEST_CASE("plan fails when config missing", "[cli]") {
  std::vector<std::string> args = {"kubeforward", "plan", "--file", Fixture("missing.yaml")};
  REQUIRE(kubeforward::run_cli(args) != 0);
}

TEST_CASE("plan defaults to kubeforward.yaml in current directory", "[cli]") {
  const auto original = std::filesystem::current_path();
  std::filesystem::current_path(FixtureDir());

  std::vector<std::string> args = {"kubeforward", "plan", "--env", "dev"};
  const int exit_code = kubeforward::run_cli(args);

  std::filesystem::current_path(original);
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
