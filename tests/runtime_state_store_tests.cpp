#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <string>

#include "kubeforward/runtime/state_store.h"

namespace {

std::filesystem::path TempStatePath() {
  const auto base = std::filesystem::temp_directory_path() / "kubeforward-tests";
  std::filesystem::create_directories(base);
  return base / "state-store-test.yaml";
}

}  // namespace

TEST_CASE("state store round-trips sessions", "[runtime]") {
  const auto path = TempStatePath();
  std::filesystem::remove(path);

  kubeforward::runtime::RuntimeState state;
  kubeforward::runtime::ManagedSession session;
  session.id = "session-dev";
  session.config_path = "/tmp/kubeforward.yaml";
  session.environment = "dev";
  session.daemon = true;
  session.started_at_utc = "2026-02-27T00:00:00Z";
  session.forwards.push_back(kubeforward::runtime::ManagedForwardProcess{
      .environment = "dev", .forward_name = "api", .local_port = 7000, .remote_port = 7000, .pid = 12001});
  state.sessions.push_back(session);

  std::string error;
  REQUIRE(kubeforward::runtime::SaveState(path, state, error));
  REQUIRE(error.empty());

  const auto load = kubeforward::runtime::LoadState(path);
  REQUIRE(load.ok());
  REQUIRE(load.state.sessions.size() == 1);
  REQUIRE(load.state.sessions.at(0).id == "session-dev");
  REQUIRE(load.state.sessions.at(0).forwards.size() == 1);
  CHECK(load.state.sessions.at(0).forwards.at(0).pid == 12001);
}

TEST_CASE("state store returns empty state for missing files", "[runtime]") {
  const auto path = TempStatePath();
  std::filesystem::remove(path);

  const auto load = kubeforward::runtime::LoadState(path);
  REQUIRE(load.ok());
  REQUIRE(load.state.sessions.empty());
}

TEST_CASE("default state path is deterministic for config path", "[runtime]") {
  const auto first = kubeforward::runtime::DefaultStatePathForConfig("/tmp/example.yaml");
  const auto second = kubeforward::runtime::DefaultStatePathForConfig("/tmp/example.yaml");
  const auto other = kubeforward::runtime::DefaultStatePathForConfig("/tmp/other.yaml");

  CHECK(first == second);
  CHECK(first != other);
}
