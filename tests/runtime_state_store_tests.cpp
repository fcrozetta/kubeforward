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

class ScopedCurrentPath {
 public:
  explicit ScopedCurrentPath(const std::filesystem::path& path) : original_(std::filesystem::current_path()) {
    std::filesystem::current_path(path);
  }

  ~ScopedCurrentPath() { std::filesystem::current_path(original_); }

 private:
  std::filesystem::path original_;
};

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
      .environment = "dev",
      .forward_name = "api",
      .argv = {"kubectl", "port-forward", "deployment/api", "7000:7000"},
      .cwd = "/tmp/workdir",
      .log_path = "/tmp/kubeforward/api.log",
      .bind_address = "127.0.0.2",
      .local_port = 7000,
      .remote_port = 7000,
      .protocol = kubeforward::config::PortProtocol::kUdp,
      .pid = 12001});
  state.sessions.push_back(session);

  std::string error;
  REQUIRE(kubeforward::runtime::SaveState(path, state, error));
  REQUIRE(error.empty());

  const auto load = kubeforward::runtime::LoadState(path);
  REQUIRE(load.ok());
  REQUIRE(load.state.sessions.size() == 1);
  REQUIRE(load.state.sessions.at(0).id == "session-dev");
  REQUIRE(load.state.sessions.at(0).forwards.size() == 1);
  CHECK(load.state.sessions.at(0).forwards.at(0).argv.size() == 4);
  CHECK(load.state.sessions.at(0).forwards.at(0).cwd == "/tmp/workdir");
  CHECK(load.state.sessions.at(0).forwards.at(0).log_path == "/tmp/kubeforward/api.log");
  CHECK(load.state.sessions.at(0).forwards.at(0).bind_address == "127.0.0.2");
  CHECK(load.state.sessions.at(0).forwards.at(0).protocol == kubeforward::config::PortProtocol::kUdp);
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

TEST_CASE("default state path normalizes equivalent relative config paths", "[runtime]") {
  const auto base = std::filesystem::temp_directory_path() / "kubeforward-tests-state-path-normalize";
  std::filesystem::create_directories(base);
  ScopedCurrentPath cwd(base);

  const auto plain = kubeforward::runtime::DefaultStatePathForConfig("kubeforward.yaml");
  const auto dotted = kubeforward::runtime::DefaultStatePathForConfig("./kubeforward.yaml");
  const auto parent_ref = kubeforward::runtime::DefaultStatePathForConfig("folder/../kubeforward.yaml");

  CHECK(plain == dotted);
  CHECK(plain == parent_ref);
}

TEST_CASE("state store supports parentless relative paths", "[runtime]") {
  const auto base = std::filesystem::temp_directory_path() / "kubeforward-tests-relative-state";
  std::filesystem::create_directories(base);
  ScopedCurrentPath cwd(base);

  kubeforward::runtime::RuntimeState state;
  std::string error;
  const auto relative_path = std::filesystem::path("state.yaml");
  REQUIRE(kubeforward::runtime::SaveState(relative_path, state, error));
  REQUIRE(error.empty());
  CHECK(std::filesystem::exists(base / relative_path));

  std::filesystem::remove(base / relative_path);
}
