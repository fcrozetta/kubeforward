#include <catch2/catch_test_macros.hpp>

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

}  // namespace

TEST_CASE("plan succeeds with valid config", "[cli]") {
  std::vector<std::string> args = {"kubeforward", "plan", "--config", Fixture("basic.yaml"), "--env", "dev"};
  REQUIRE(kubeforward::run_cli(args) == 0);
}

TEST_CASE("plan fails when config missing", "[cli]") {
  std::vector<std::string> args = {"kubeforward", "plan", "--config", Fixture("missing.yaml")};
  REQUIRE(kubeforward::run_cli(args) != 0);
}
