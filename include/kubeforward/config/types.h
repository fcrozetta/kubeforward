#pragma once

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace kubeforward::config {

enum class ResourceKind {
  kPod,
  kDeployment,
  kService,
  kStatefulSet,
};

enum class PortProtocol {
  kTcp,
  kUdp,
};

enum class RestartPolicy {
  kFailFast,
  kReplace,
};

struct Metadata {
  std::string project;
  std::optional<std::string> owner;
};

struct TargetDefaults {
  std::optional<std::string> kubeconfig;
  std::optional<std::string> context;
  std::optional<std::string> namespace_name;
  std::optional<std::string> bind_address;
  std::map<std::string, std::string> labels;
};

struct EnvironmentGuards {
  bool allow_production = false;
};

struct ResourceSelector {
  ResourceKind kind = ResourceKind::kPod;
  std::optional<std::string> name;
  std::map<std::string, std::string> selector;
  std::optional<std::string> namespace_override;
};

struct PortMapping {
  int local_port = 0;
  int remote_port = 0;
  std::optional<std::string> bind_address;
  PortProtocol protocol = PortProtocol::kTcp;
};

struct HealthCheck {
  std::vector<std::string> exec;
  std::optional<int> timeout_ms;
};

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

struct EnvironmentDefinition {
  std::string name;
  std::optional<std::string> extends;
  std::optional<std::string> description;
  TargetDefaults settings;
  EnvironmentGuards guards;
  std::vector<ForwardDefinition> forwards;
};

struct Config {
  int version = 0;
  Metadata metadata;
  TargetDefaults defaults;
  std::map<std::string, EnvironmentDefinition> environments;
};

}  // namespace kubeforward::config

