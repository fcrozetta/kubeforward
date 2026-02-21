#include <catch2/catch_test_macros.hpp>

#include <optional>
#include <string>

#include "kubeforward/config/loader.h"

#ifndef KF_SOURCE_DIR
#error "KF_SOURCE_DIR must be defined"
#endif

namespace {

std::string Fixture(const std::string& name) {
  return std::string(KF_SOURCE_DIR) + "/tests/fixtures/" + name;
}

}  // namespace

TEST_CASE("config loads valid fixture", "[config]") {
  const auto result = kubeforward::config::LoadConfigFromFile(Fixture("basic.yaml"));
  REQUIRE(result.ok());
  REQUIRE(result.config);
  const auto& config = *result.config;
  REQUIRE(config.version == 1);
  REQUIRE(config.environments.count("dev") == 1);
  const auto& dev = config.environments.at("dev");
  REQUIRE(dev.forwards.size() == 1);
  REQUIRE(dev.forwards[0].ports[0].local_port == 7000);
}

TEST_CASE("config detects duplicate ports", "[config]") {
  const auto result = kubeforward::config::LoadConfigFromFile(Fixture("invalid_duplicate_ports.yaml"));
  REQUIRE_FALSE(result.ok());
  bool saw_duplicate_port = false;
  for (const auto& error : result.errors) {
    if (error.message.find("duplicate local port") != std::string::npos) {
      saw_duplicate_port = true;
      break;
    }
  }
  REQUIRE(saw_duplicate_port);
}

TEST_CASE("config errors on missing file", "[config]") {
  const auto result = kubeforward::config::LoadConfigFromFile(Fixture("does_not_exist.yaml"));
  REQUIRE_FALSE(result.ok());
  REQUIRE_FALSE(result.errors.empty());
}
