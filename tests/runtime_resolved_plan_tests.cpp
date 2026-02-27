#include <catch2/catch_test_macros.hpp>

#include <optional>
#include <string>

#include "kubeforward/config/loader.h"
#include "kubeforward/runtime/resolved_plan.h"

#ifndef KF_SOURCE_DIR
#error "KF_SOURCE_DIR must be defined"
#endif

namespace {

std::string Fixture(const std::string& name) {
  return std::string(KF_SOURCE_DIR) + "/tests/fixtures/" + name;
}

}  // namespace

TEST_CASE("resolved plan applies default bindAddress to ports", "[runtime]") {
  const auto load_result = kubeforward::config::LoadConfigFromFile(Fixture("basic.yaml"));
  REQUIRE(load_result.ok());
  REQUIRE(load_result.config.has_value());

  const auto plan_result =
      kubeforward::runtime::BuildResolvedPlan(*load_result.config, Fixture("basic.yaml"), std::optional<std::string>{"dev"});
  REQUIRE(plan_result.ok());
  REQUIRE(plan_result.plan.has_value());
  REQUIRE(plan_result.plan->environments.size() == 1);

  const auto& env = plan_result.plan->environments.at(0);
  REQUIRE(env.forwards.size() == 1);
  REQUIRE(env.forwards.at(0).ports.size() == 1);
  REQUIRE(env.forwards.at(0).ports.at(0).bind_address.has_value());
  CHECK(env.forwards.at(0).ports.at(0).bind_address.value() == "127.0.0.1");
}

TEST_CASE("resolved plan inherits forwards for extends environments without local forwards", "[runtime]") {
  const auto load_result = kubeforward::config::LoadConfigFromFile(Fixture("extends_with_defaults.yaml"));
  REQUIRE(load_result.ok());
  REQUIRE(load_result.config.has_value());

  const auto plan_result = kubeforward::runtime::BuildResolvedPlan(*load_result.config, Fixture("extends_with_defaults.yaml"),
                                                                   std::optional<std::string>{"child"});
  REQUIRE(plan_result.ok());
  REQUIRE(plan_result.plan.has_value());
  REQUIRE(plan_result.plan->environments.size() == 1);

  const auto& env = plan_result.plan->environments.at(0);
  REQUIRE(env.name == "child");
  REQUIRE(env.forwards.size() == 1);
  CHECK(env.forwards.at(0).name == "base-api");
}

TEST_CASE("resolved plan rejects unknown environment filter", "[runtime]") {
  const auto load_result = kubeforward::config::LoadConfigFromFile(Fixture("basic.yaml"));
  REQUIRE(load_result.ok());
  REQUIRE(load_result.config.has_value());

  const auto plan_result =
      kubeforward::runtime::BuildResolvedPlan(*load_result.config, Fixture("basic.yaml"), std::optional<std::string>{"missing"});
  REQUIRE_FALSE(plan_result.ok());
  REQUIRE_FALSE(plan_result.errors.empty());
}
