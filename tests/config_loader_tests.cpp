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

TEST_CASE("config reports scalar conversion errors instead of crashing", "[config]") {
  const auto result = kubeforward::config::LoadConfigFromFile(Fixture("invalid_scalar_int.yaml"));
  REQUIRE_FALSE(result.ok());

  bool saw_expected_integer = false;
  for (const auto& error : result.errors) {
    if (error.context == "environments.dev.forwards[0].ports[0].local" && error.message == "expected integer") {
      saw_expected_integer = true;
      break;
    }
  }
  REQUIRE(saw_expected_integer);
}

TEST_CASE("config preserves unknown forward annotations", "[config]") {
  const auto result = kubeforward::config::LoadConfigFromFile(Fixture("annotations_passthrough.yaml"));
  REQUIRE(result.ok());
  REQUIRE(result.config);

  const auto& forward = result.config->environments.at("dev").forwards.at(0);
  REQUIRE(forward.annotations.count("customPolicy") == 1);
  REQUIRE(forward.annotations.count("owner") == 1);
}

TEST_CASE("config allows extends environments without local forwards", "[config]") {
  const auto result = kubeforward::config::LoadConfigFromFile(Fixture("extends_without_forwards.yaml"));
  REQUIRE(result.ok());
  REQUIRE(result.config);

  const auto& child = result.config->environments.at("child");
  REQUIRE(child.extends.has_value());
  REQUIRE(child.extends.value() == "base");
  REQUIRE(child.forwards.empty());
}

TEST_CASE("config rejects cyclic environment extends", "[config]") {
  const auto result = kubeforward::config::LoadConfigFromFile(Fixture("invalid_extends_cycle.yaml"));
  REQUIRE_FALSE(result.ok());

  bool saw_cycle_error = false;
  for (const auto& error : result.errors) {
    if (error.message.find("cyclic environment inheritance") != std::string::npos) {
      saw_cycle_error = true;
      break;
    }
  }
  REQUIRE(saw_cycle_error);
}

TEST_CASE("config rejects non-ipv4 bindAddress literals", "[config]") {
  const auto result = kubeforward::config::LoadConfigFromFile(Fixture("invalid_bind_ipv6.yaml"));
  REQUIRE_FALSE(result.ok());

  bool saw_bind_error = false;
  for (const auto& error : result.errors) {
    if (error.context == "defaults.bindAddress" && error.message == "must be an IPv4 literal") {
      saw_bind_error = true;
      break;
    }
  }
  REQUIRE(saw_bind_error);
}
