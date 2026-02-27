#pragma once

#include <map>
#include <optional>
#include <string>
#include <vector>

#include "kubeforward/config/types.h"

namespace kubeforward::runtime {

//! Deterministic runtime-ready projection of one forward entry.
struct ResolvedForward {
  std::string environment;
  std::string name;
  config::ResourceSelector resource;
  std::optional<std::string> container;
  std::vector<config::PortMapping> ports;
  std::string namespace_name;
  bool detach = false;
  config::RestartPolicy restart_policy = config::RestartPolicy::kFailFast;
  std::optional<config::HealthCheck> health_check;
  std::map<std::string, std::string> env;
  std::map<std::string, std::string> annotations;
};

//! Effective environment after applying defaults + extends resolution.
struct ResolvedEnvironment {
  std::string name;
  config::TargetDefaults settings;
  config::EnvironmentGuards guards;
  std::vector<ResolvedForward> forwards;
};

//! Full runtime plan resolved from config input.
struct ResolvedPlan {
  std::string config_path;
  std::vector<ResolvedEnvironment> environments;
};

//! Plan-building error used before any process execution starts.
struct PlanBuildError {
  std::string context;
  std::string message;
};

//! Result wrapper for resolved plan construction.
struct PlanBuildResult {
  std::optional<ResolvedPlan> plan;
  std::vector<PlanBuildError> errors;

  bool ok() const { return plan.has_value() && errors.empty(); }
};

//! Builds a runtime plan with resolved inheritance and target environment filtering.
PlanBuildResult BuildResolvedPlan(const config::Config& config, const std::string& config_path,
                                  const std::optional<std::string>& env_filter);

}  // namespace kubeforward::runtime
