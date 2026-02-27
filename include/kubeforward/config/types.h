#pragma once

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace kubeforward::config {

/// Kubernetes resource kinds supported as forward targets.
enum class ResourceKind {
  kPod,
  kDeployment,
  kService,
  kStatefulSet,
};

/// Network protocol used by a local->remote mapping.
enum class PortProtocol {
  kTcp,
  kUdp,
};

/// Behavior when a detached forward process exits unexpectedly.
enum class RestartPolicy {
  kFailFast,
  kReplace,
};

/// File-level metadata for ownership and project labeling.
struct Metadata {
  std::string project;
  std::optional<std::string> owner;
};

/// Default target settings inherited by environments and forwards.
struct TargetDefaults {
  std::optional<std::string> kubeconfig;
  std::optional<std::string> context;
  std::optional<std::string> namespace_name;
  std::optional<std::string> bind_address;
  std::map<std::string, std::string> labels;
};

/// Safety switches for environment-specific runtime behavior.
struct EnvironmentGuards {
  bool allow_production = false;
};

/// How to select a Kubernetes target resource for a forward.
struct ResourceSelector {
  ResourceKind kind = ResourceKind::kPod;
  std::optional<std::string> name;
  std::map<std::string, std::string> selector;
  std::optional<std::string> namespace_override;
};

/// One local->remote port mapping definition.
struct PortMapping {
  int local_port = 0;
  int remote_port = 0;
  std::optional<std::string> bind_address;
  PortProtocol protocol = PortProtocol::kTcp;
};

/// Optional command used to verify forward readiness.
struct HealthCheck {
  std::vector<std::string> exec;
  std::optional<int> timeout_ms;
};

/// Full runtime definition for one named forward entry.
struct ForwardDefinition {
  std::string name;
  ResourceSelector resource;
  std::optional<std::string> container;
  std::vector<PortMapping> ports;
  bool detach = false;
  RestartPolicy restart_policy = RestartPolicy::kFailFast;
  std::optional<HealthCheck> health_check;
  std::map<std::string, std::string> env;
  std::map<std::string, std::string> annotations;
};

/// Environment-level forward set plus inherited overrides.
struct EnvironmentDefinition {
  std::string name;
  std::optional<std::string> extends;
  std::optional<std::string> description;
  TargetDefaults settings;
  EnvironmentGuards guards;
  std::vector<ForwardDefinition> forwards;
};

/// Canonical in-memory model of a kubeforward config file.
struct Config {
  int version = 0;
  Metadata metadata;
  TargetDefaults defaults;
  std::map<std::string, EnvironmentDefinition> environments;
};

}  // namespace kubeforward::config
