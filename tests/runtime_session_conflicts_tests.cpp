#include <catch2/catch_test_macros.hpp>

#include <string>

#include <unistd.h>

#include "kubeforward/config/types.h"
#include "kubeforward/runtime/resolved_plan.h"
#include "kubeforward/runtime/session_conflicts.h"
#include "kubeforward/runtime/state_store.h"

namespace {

kubeforward::runtime::ResolvedEnvironment MakeTargetEnvironment(const std::string& name, const std::string& bind_address,
                                                                kubeforward::config::PortProtocol protocol) {
  kubeforward::runtime::ResolvedEnvironment env;
  env.name = name;
  kubeforward::runtime::ResolvedForward forward;
  forward.environment = name;
  forward.name = "api";
  forward.namespace_name = "default";
  forward.ports.push_back(kubeforward::config::PortMapping{
      .local_port = 18080,
      .remote_port = 80,
      .bind_address = bind_address,
      .protocol = protocol,
  });
  env.forwards.push_back(forward);
  return env;
}

kubeforward::runtime::RuntimeState MakeRunningState(const std::string& bind_address,
                                                    kubeforward::config::PortProtocol protocol) {
  kubeforward::runtime::RuntimeState state;
  kubeforward::runtime::ManagedSession session;
  session.id = "session-left";
  session.config_path = "/tmp/kubeforward.yaml";
  session.environment = "left";
  session.forwards.push_back(kubeforward::runtime::ManagedForwardProcess{
      .environment = "left",
      .forward_name = "api-left",
      .bind_address = bind_address,
      .local_port = 18080,
      .remote_port = 80,
      .protocol = protocol,
      .pid = static_cast<int>(::getpid()),
  });
  state.sessions.push_back(session);
  return state;
}

}  // namespace

TEST_CASE("runtime conflict check allows same port on different bind addresses", "[runtime]") {
  const auto state = MakeRunningState("127.0.0.1", kubeforward::config::PortProtocol::kTcp);
  const auto target_env = MakeTargetEnvironment("right", "127.0.0.2", kubeforward::config::PortProtocol::kTcp);

  std::string error;
  CHECK(kubeforward::runtime::CheckRuntimeSessionPortConflicts(state, "/tmp/kubeforward.yaml", target_env, error));
  CHECK(error.empty());
}

TEST_CASE("runtime conflict check allows same port on different protocols", "[runtime]") {
  const auto state = MakeRunningState("127.0.0.1", kubeforward::config::PortProtocol::kTcp);
  const auto target_env = MakeTargetEnvironment("right", "127.0.0.1", kubeforward::config::PortProtocol::kUdp);

  std::string error;
  CHECK(kubeforward::runtime::CheckRuntimeSessionPortConflicts(state, "/tmp/kubeforward.yaml", target_env, error));
  CHECK(error.empty());
}

TEST_CASE("runtime conflict check rejects identical socket tuples from another session", "[runtime]") {
  const auto state = MakeRunningState("127.0.0.1", kubeforward::config::PortProtocol::kTcp);
  const auto target_env = MakeTargetEnvironment("right", "127.0.0.1", kubeforward::config::PortProtocol::kTcp);

  std::string error;
  CHECK_FALSE(kubeforward::runtime::CheckRuntimeSessionPortConflicts(state, "/tmp/kubeforward.yaml", target_env, error));
  CHECK(error.find("127.0.0.1") != std::string::npos);
  CHECK(error.find("18080") != std::string::npos);
}
