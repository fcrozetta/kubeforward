#include "kubeforward/config/loader.h"

#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace kubeforward::config {
namespace {

void AddError(std::vector<ConfigLoadError>& errors, std::string context, std::string message) {
  errors.push_back(ConfigLoadError{std::move(context), std::move(message)});
}

bool NodeIsMap(const YAML::Node& node) { return node && node.IsMap(); }

bool NodeIsSequence(const YAML::Node& node) { return node && node.IsSequence(); }

bool LooksLikeIPv4Literal(const std::string& value) {
  int dots = 0;
  int segment_len = 0;
  int segments = 0;
  int current = 0;
  bool has_digit = false;
  for (char ch : value) {
    if (std::isdigit(static_cast<unsigned char>(ch))) {
      has_digit = true;
      current = current * 10 + (ch - '0');
      if (current > 255) {
        return false;
      }
      ++segment_len;
      if (segment_len > 3) {
        return false;
      }
    } else if (ch == '.') {
      if (!has_digit) {
        return false;
      }
      ++dots;
      ++segments;
      if (segments > 4) {
        return false;
      }
      has_digit = false;
      segment_len = 0;
      current = 0;
    } else {
      return false;
    }
  }
  if (!has_digit) {
    return false;
  }
  ++segments;
  return dots == 3 && segments == 4;
}

bool IsIpLiteral(const std::string& value) {
  // TODO(>=1.0.0): Re-introduce IPv6 using strict parsing rules.
  return LooksLikeIPv4Literal(value);
}

bool IsPortValid(int value) { return value >= 1 && value <= 65535; }

std::string ContextForForward(const std::string& env_name, size_t index) {
  return "environments." + env_name + ".forwards[" + std::to_string(index) + "]";
}

template <typename Range>
std::set<std::string> MakeSet(Range&& range) {
  return std::set<std::string>(range.begin(), range.end());
}

void EnsureAllowedKeys(const YAML::Node& node, const std::string& context,
                       std::set<std::string> allowed, std::vector<ConfigLoadError>& errors) {
  if (!NodeIsMap(node)) {
    return;
  }
  for (const auto& entry : node) {
    if (!entry.first.IsScalar()) {
      AddError(errors, context, "encountered non-string key");
      continue;
    }
    const auto key = entry.first.as<std::string>();
    if (allowed.count(key) == 0) {
      AddError(errors, context, "unknown key '" + key + "'");
    }
  }
}

std::optional<std::string> ReadOptionalString(const YAML::Node& node, const std::string& context,
                                              std::vector<ConfigLoadError>& errors) {
  if (!node) {
    return std::nullopt;
  }
  if (!node.IsScalar()) {
    AddError(errors, context, "expected string");
    return std::nullopt;
  }
  return node.as<std::string>();
}

std::optional<bool> ReadOptionalBool(const YAML::Node& node, const std::string& context,
                                     std::vector<ConfigLoadError>& errors) {
  if (!node) {
    return std::nullopt;
  }
  if (!node.IsScalar()) {
    AddError(errors, context, "expected boolean");
    return std::nullopt;
  }
  try {
    return node.as<bool>();
  } catch (const YAML::BadConversion&) {
    AddError(errors, context, "expected boolean");
    return std::nullopt;
  }
}

std::optional<int> ReadOptionalInt(const YAML::Node& node, const std::string& context,
                                   std::vector<ConfigLoadError>& errors) {
  if (!node) {
    return std::nullopt;
  }
  if (!node.IsScalar()) {
    AddError(errors, context, "expected integer");
    return std::nullopt;
  }
  try {
    return node.as<int>();
  } catch (const YAML::BadConversion&) {
    AddError(errors, context, "expected integer");
    return std::nullopt;
  }
}

std::map<std::string, std::string> ParseStringMap(const YAML::Node& node, const std::string& context,
                                                  std::vector<ConfigLoadError>& errors) {
  std::map<std::string, std::string> values;
  if (!node) {
    return values;
  }
  if (!node.IsMap()) {
    AddError(errors, context, "expected mapping");
    return values;
  }
  for (const auto& entry : node) {
    if (!entry.first.IsScalar() || !entry.second.IsScalar()) {
      AddError(errors, context, "expected string keys and values");
      continue;
    }
    const std::string key = entry.first.as<std::string>();
    const std::string value = entry.second.as<std::string>();
    if (values.count(key) != 0) {
      AddError(errors, context, "duplicate key '" + key + "'");
      continue;
    }
    values.emplace(key, value);
  }
  return values;
}

ResourceKind ParseResourceKind(const std::string& value, const std::string& context,
                               std::vector<ConfigLoadError>& errors) {
  if (value == "pod") {
    return ResourceKind::kPod;
  }
  if (value == "deployment") {
    return ResourceKind::kDeployment;
  }
  if (value == "service") {
    return ResourceKind::kService;
  }
  if (value == "statefulset") {
    return ResourceKind::kStatefulSet;
  }
  AddError(errors, context, "invalid resource.kind '" + value + "'");
  return ResourceKind::kPod;
}

PortProtocol ParseProtocol(const std::string& value, const std::string& context,
                           std::vector<ConfigLoadError>& errors) {
  if (value.empty() || value == "tcp") {
    return PortProtocol::kTcp;
  }
  if (value == "udp") {
    return PortProtocol::kUdp;
  }
  AddError(errors, context, "invalid protocol '" + value + "'");
  return PortProtocol::kTcp;
}

RestartPolicy ParseRestartPolicy(const std::string& value, const std::string& context,
                                 std::vector<ConfigLoadError>& errors) {
  if (value.empty() || value == "fail-fast") {
    return RestartPolicy::kFailFast;
  }
  if (value == "replace") {
    return RestartPolicy::kReplace;
  }
  AddError(errors, context, "invalid restartPolicy '" + value + "'");
  return RestartPolicy::kFailFast;
}

void ParseHealthCheck(const YAML::Node& node, const std::string& context, std::optional<HealthCheck>& out,
                      std::vector<ConfigLoadError>& errors) {
  if (!node) {
    out.reset();
    return;
  }
  if (!node.IsMap()) {
    AddError(errors, context, "expected mapping for healthCheck");
    out.reset();
    return;
  }

  EnsureAllowedKeys(node, context, MakeSet(std::vector<std::string>{"exec", "timeoutMs"}), errors);

  HealthCheck hc;
  const auto exec_node = node["exec"];
  if (!NodeIsSequence(exec_node) || exec_node.size() == 0) {
    AddError(errors, context + ".exec", "expected non-empty list");
  } else {
    for (size_t i = 0; i < exec_node.size(); ++i) {
      const auto& arg = exec_node[i];
      if (!arg.IsScalar()) {
        AddError(errors, context + ".exec[" + std::to_string(i) + "]", "expected string");
        continue;
      }
      const std::string value = arg.as<std::string>();
      if (value.empty()) {
        AddError(errors, context + ".exec[" + std::to_string(i) + "]", "command arguments cannot be empty");
      }
      hc.exec.push_back(value);
    }
  }
  if (hc.exec.empty()) {
    // Do not set output to preserve failure.
    return;
  }

  const std::string& entrypoint = hc.exec.front();
  if (entrypoint.find('/') == std::string::npos) {
    AddError(errors, context + ".exec[0]", "command must be absolute or repo-relative (contains '/')");
  }

  if (const auto timeout = ReadOptionalInt(node["timeoutMs"], context + ".timeoutMs", errors)) {
    if (*timeout <= 0) {
      AddError(errors, context + ".timeoutMs", "must be positive");
    } else {
      hc.timeout_ms = *timeout;
    }
  }

  out = hc;
}

TargetDefaults ParseTargetDefaults(const YAML::Node& node, const std::string& context, bool enforce_key_whitelist,
                                   std::vector<ConfigLoadError>& errors) {
  TargetDefaults defaults;
  if (!node) {
    return defaults;
  }
  if (!node.IsMap()) {
    AddError(errors, context, "expected mapping");
    return defaults;
  }

  if (enforce_key_whitelist) {
    EnsureAllowedKeys(
        node, context,
        MakeSet(std::vector<std::string>{"kubeconfig", "context", "namespace", "bindAddress", "labels"}), errors);
  }

  if (const auto kube = ReadOptionalString(node["kubeconfig"], context + ".kubeconfig", errors)) {
    defaults.kubeconfig = kube;
  }
  if (const auto ctx = ReadOptionalString(node["context"], context + ".context", errors)) {
    defaults.context = ctx;
  }
  if (const auto ns = ReadOptionalString(node["namespace"], context + ".namespace", errors)) {
    defaults.namespace_name = ns;
  }
  if (const auto bind = ReadOptionalString(node["bindAddress"], context + ".bindAddress", errors)) {
    if (!bind->empty() && !IsIpLiteral(*bind)) {
      AddError(errors, context + ".bindAddress", "must be an IPv4 literal");
    } else {
      defaults.bind_address = bind;
    }
  }
  defaults.labels = ParseStringMap(node["labels"], context + ".labels", errors);
  return defaults;
}

EnvironmentGuards ParseEnvironmentGuards(const YAML::Node& node, const std::string& context,
                                         std::vector<ConfigLoadError>& errors) {
  EnvironmentGuards guards;
  if (!node) {
    return guards;
  }
  if (!node.IsMap()) {
    AddError(errors, context, "expected mapping");
    return guards;
  }
  EnsureAllowedKeys(node, context, MakeSet(std::vector<std::string>{"allowProduction"}), errors);
  if (const auto allow = ReadOptionalBool(node["allowProduction"], context + ".allowProduction", errors)) {
    guards.allow_production = *allow;
  }
  return guards;
}

ResourceSelector ParseResourceSelector(const YAML::Node& node, const std::string& context,
                                       std::vector<ConfigLoadError>& errors) {
  ResourceSelector selector;
  if (!node) {
    AddError(errors, context, "resource block missing");
    return selector;
  }
  if (!node.IsMap()) {
    AddError(errors, context, "expected mapping for resource");
    return selector;
  }
  EnsureAllowedKeys(node, context,
                    MakeSet(std::vector<std::string>{"kind", "name", "selector", "namespace"}), errors);
  if (const auto kind_value = ReadOptionalString(node["kind"], context + ".kind", errors)) {
    selector.kind = ParseResourceKind(*kind_value, context + ".kind", errors);
  } else {
    AddError(errors, context + ".kind", "resource kind is required");
  }

  const auto name_value = ReadOptionalString(node["name"], context + ".name", errors);
  const auto selector_node = node["selector"];
  if (name_value && selector_node) {
    AddError(errors, context, "name and selector are mutually exclusive");
  }
  if (name_value) {
    selector.name = name_value;
  }
  if (selector_node) {
    selector.selector = ParseStringMap(selector_node, context + ".selector", errors);
    if (selector.selector.empty()) {
      AddError(errors, context + ".selector", "selector cannot be empty");
    }
  }
  if (!selector.name.has_value() && selector.selector.empty()) {
    AddError(errors, context, "resource requires name or selector");
  }
  if (const auto ns = ReadOptionalString(node["namespace"], context + ".namespace", errors)) {
    selector.namespace_override = ns;
  }
  return selector;
}

PortMapping ParsePortMapping(const YAML::Node& node, const std::string& context,
                             std::vector<ConfigLoadError>& errors) {
  PortMapping mapping;
  if (!node.IsMap()) {
    AddError(errors, context, "expected mapping");
    return mapping;
  }
  EnsureAllowedKeys(node, context, MakeSet(std::vector<std::string>{"local", "remote", "bindAddress", "protocol"}),
                    errors);
  if (const auto local = ReadOptionalInt(node["local"], context + ".local", errors)) {
    mapping.local_port = *local;
    if (!IsPortValid(mapping.local_port)) {
      AddError(errors, context + ".local", "port must be between 1 and 65535");
    }
  } else {
    AddError(errors, context + ".local", "local port is required");
  }
  if (const auto remote = ReadOptionalInt(node["remote"], context + ".remote", errors)) {
    mapping.remote_port = *remote;
    if (!IsPortValid(mapping.remote_port)) {
      AddError(errors, context + ".remote", "port must be between 1 and 65535");
    }
  } else {
    AddError(errors, context + ".remote", "remote port is required");
  }
  if (const auto bind = ReadOptionalString(node["bindAddress"], context + ".bindAddress", errors)) {
    if (!bind->empty() && !IsIpLiteral(*bind)) {
      AddError(errors, context + ".bindAddress", "must be an IPv4 literal");
    } else {
      mapping.bind_address = bind;
    }
  }
  if (const auto protocol = ReadOptionalString(node["protocol"], context + ".protocol", errors)) {
    mapping.protocol = ParseProtocol(*protocol, context + ".protocol", errors);
  }
  return mapping;
}

void ParseForwardAnnotations(const YAML::Node& node, const std::string& context, ForwardDefinition& forward,
                             std::vector<ConfigLoadError>& errors) {
  if (!node) {
    return;
  }
  if (!node.IsMap()) {
    AddError(errors, context, "expected mapping");
    return;
  }
  if (const auto detach = ReadOptionalBool(node["detach"], context + ".detach", errors)) {
    forward.detach = *detach;
  }
  if (const auto restart = ReadOptionalString(node["restartPolicy"], context + ".restartPolicy", errors)) {
    forward.restart_policy = ParseRestartPolicy(*restart, context + ".restartPolicy", errors);
  }
  ParseHealthCheck(node["healthCheck"], context + ".healthCheck", forward.health_check, errors);
}

ForwardDefinition ParseForward(const YAML::Node& node, const std::string& context,
                               std::vector<ConfigLoadError>& errors) {
  ForwardDefinition forward;
  if (!node.IsMap()) {
    AddError(errors, context, "expected mapping");
    return forward;
  }
  EnsureAllowedKeys(
      node, context,
      MakeSet(std::vector<std::string>{"name", "resource", "container", "ports", "annotations", "env"}), errors);

  if (const auto name = ReadOptionalString(node["name"], context + ".name", errors)) {
    forward.name = *name;
  } else {
    AddError(errors, context + ".name", "forward requires a name");
  }
  forward.resource = ParseResourceSelector(node["resource"], context + ".resource", errors);
  if (const auto container = ReadOptionalString(node["container"], context + ".container", errors)) {
    forward.container = container;
  }
  const auto ports_node = node["ports"];
  if (!NodeIsSequence(ports_node) || ports_node.size() == 0) {
    AddError(errors, context + ".ports", "expected non-empty list");
  } else {
    forward.ports.reserve(ports_node.size());
    for (size_t i = 0; i < ports_node.size(); ++i) {
      forward.ports.push_back(ParsePortMapping(ports_node[i], context + ".ports[" + std::to_string(i) + "]", errors));
    }
  }
  ParseForwardAnnotations(node["annotations"], context + ".annotations", forward, errors);
  forward.env = ParseStringMap(node["env"], context + ".env", errors);

  // Capture passthrough annotations for unknown consumers.
  if (const auto annotations = node["annotations"]; NodeIsMap(annotations)) {
    for (const auto& entry : annotations) {
      if (!entry.first.IsScalar()) {
        continue;
      }
      const std::string key = entry.first.as<std::string>();
      if (key == "detach" || key == "restartPolicy" || key == "healthCheck") {
        continue;
      }
      forward.annotations[key] = YAML::Dump(entry.second);
    }
  }

  return forward;
}

EnvironmentDefinition ParseEnvironment(const std::string& name, const YAML::Node& node,
                                       std::vector<ConfigLoadError>& errors) {
  EnvironmentDefinition env;
  env.name = name;
  const std::string context = "environments." + name;
  if (!node.IsMap()) {
    AddError(errors, context, "expected mapping");
    return env;
  }
  EnsureAllowedKeys(node, context,
                    MakeSet(std::vector<std::string>{"extends", "description", "kubeconfig", "context", "namespace",
                                                     "bindAddress", "labels", "guards", "forwards"}),
                    errors);
  if (const auto extends = ReadOptionalString(node["extends"], context + ".extends", errors)) {
    env.extends = extends;
  }
  if (const auto description = ReadOptionalString(node["description"], context + ".description", errors)) {
    env.description = description;
  }
  env.settings = ParseTargetDefaults(node, context, /*enforce_key_whitelist=*/false, errors);
  env.guards = ParseEnvironmentGuards(node["guards"], context + ".guards", errors);

  const auto forwards = node["forwards"];
  const bool has_parent = env.extends.has_value() && !env.extends->empty();
  if (!forwards) {
    if (!has_parent) {
      AddError(errors, context, "environment must define 'forwards'");
    }
  } else if (!NodeIsSequence(forwards)) {
    AddError(errors, context + ".forwards", "expected list");
  } else {
    env.forwards.reserve(forwards.size());
    for (size_t i = 0; i < forwards.size(); ++i) {
      env.forwards.push_back(ParseForward(forwards[i], ContextForForward(name, i), errors));
    }
  }
  return env;
}

void ValidateEnvironment(const EnvironmentDefinition& env, std::vector<ConfigLoadError>& errors) {
  std::unordered_set<std::string> forward_names;
  std::unordered_set<int> local_ports;
  for (size_t idx = 0; idx < env.forwards.size(); ++idx) {
    const auto& forward = env.forwards[idx];
    const auto context = ContextForForward(env.name, idx);
    if (forward.name.empty()) {
      AddError(errors, context + ".name", "forward name cannot be empty");
    } else if (!forward_names.insert(forward.name).second) {
      AddError(errors, context + ".name", "duplicate forward name within environment");
    }
    if (forward.ports.empty()) {
      AddError(errors, context + ".ports", "forward must define at least one port mapping");
    }
    for (size_t p = 0; p < forward.ports.size(); ++p) {
      const auto& mapping = forward.ports[p];
      const std::string port_context = context + ".ports[" + std::to_string(p) + "]";
      if (mapping.local_port == 0) {
        AddError(errors, port_context + ".local", "local port missing");
      } else if (!local_ports.insert(mapping.local_port).second) {
        AddError(errors, port_context + ".local", "duplicate local port within environment");
      }
      if (mapping.remote_port == 0) {
        AddError(errors, port_context + ".remote", "remote port missing");
      }
      if (mapping.bind_address && !mapping.bind_address->empty() && !IsIpLiteral(*mapping.bind_address)) {
        AddError(errors, port_context + ".bindAddress", "must be an IPv4 literal");
      }
    }
    if (env.guards.allow_production && !forward.detach) {
      AddError(errors, context + ".annotations.detach",
               "production environment requires detach=true for every forward");
    }
  }
}

void ValidateGlobalForwardNames(const Config& config, std::vector<ConfigLoadError>& errors) {
  std::map<std::string, std::vector<std::string>> occurrences;
  for (const auto& [env_name, env] : config.environments) {
    for (const auto& forward : env.forwards) {
      occurrences[forward.name].push_back(env_name);
    }
  }
  for (const auto& [forward_name, envs] : occurrences) {
    if (forward_name.empty()) {
      continue;
    }
    if (envs.size() <= 1) {
      continue;
    }
    std::ostringstream oss;
    oss << "forward name '" << forward_name << "' used in environments: ";
    for (size_t i = 0; i < envs.size(); ++i) {
      if (i > 0) {
        oss << ", ";
      }
      oss << envs[i];
    }
    AddError(errors, "environments", oss.str());
  }
}

void ValidateEnvironmentExtends(const Config& config, std::vector<ConfigLoadError>& errors) {
  const auto& envs = config.environments;
  for (const auto& [name, env] : envs) {
    if (!env.extends.has_value()) {
      continue;
    }
    if (env.extends == name) {
      AddError(errors, "environments." + name + ".extends", "environment cannot extend itself");
      continue;
    }
    if (envs.count(*env.extends) == 0) {
      AddError(errors, "environments." + name + ".extends",
               "references unknown environment '" + *env.extends + "'");
    }
  }

  enum class VisitState {
    kUnvisited,
    kVisiting,
    kVisited,
  };

  std::map<std::string, VisitState> state;
  std::vector<std::string> stack;

  std::function<void(const std::string&)> visit = [&](const std::string& name) {
    state[name] = VisitState::kVisiting;
    stack.push_back(name);

    const auto& env = envs.at(name);
    if (env.extends && env.extends != name && envs.count(*env.extends) == 1) {
      const std::string& parent = *env.extends;
      const auto parent_it = state.find(parent);
      const VisitState parent_state = parent_it == state.end() ? VisitState::kUnvisited : parent_it->second;

      if (parent_state == VisitState::kVisiting) {
        auto cycle_begin = std::find(stack.begin(), stack.end(), parent);
        std::ostringstream cycle;
        if (cycle_begin != stack.end()) {
          for (auto it = cycle_begin; it != stack.end(); ++it) {
            if (it != cycle_begin) {
              cycle << " -> ";
            }
            cycle << *it;
          }
          cycle << " -> " << parent;
        } else {
          cycle << name << " -> " << parent;
        }
        AddError(errors, "environments." + name + ".extends", "cyclic environment inheritance: " + cycle.str());
      } else if (parent_state == VisitState::kUnvisited) {
        visit(parent);
      }
    }

    stack.pop_back();
    state[name] = VisitState::kVisited;
  };

  for (const auto& [name, _] : envs) {
    const auto it = state.find(name);
    if (it == state.end() || it->second == VisitState::kUnvisited) {
      visit(name);
    }
  }
}

}  // namespace

ConfigLoadResult LoadConfigFromFile(const std::string& path) {
  ConfigLoadResult result;
  std::ifstream input(path);
  if (!input.is_open()) {
    AddError(result.errors, path, "unable to open config file");
    return result;
  }
  std::stringstream buffer;
  buffer << input.rdbuf();
  const auto contents = buffer.str();
  YAML::Node root;
  try {
    root = YAML::Load(contents);
  } catch (const YAML::ParserException& ex) {
    AddError(result.errors, path, std::string("YAML parse error: ") + ex.what());
    return result;
  }

  if (!root || !root.IsMap()) {
    AddError(result.errors, path, "expected top-level mapping");
    return result;
  }

  EnsureAllowedKeys(root, "root",
                    MakeSet(std::vector<std::string>{"version", "metadata", "defaults", "environments"}), result.errors);

  Config config;
  if (const auto version = ReadOptionalInt(root["version"], "version", result.errors)) {
    config.version = *version;
    if (config.version != 1) {
      AddError(result.errors, "version", "only schema version 1 is supported");
    }
  } else {
    AddError(result.errors, "version", "schema version is required");
  }

  const auto metadata = root["metadata"];
  if (!NodeIsMap(metadata)) {
    AddError(result.errors, "metadata", "metadata block is required");
  } else {
    EnsureAllowedKeys(metadata, "metadata", MakeSet(std::vector<std::string>{"project", "owner"}), result.errors);
    const auto project = ReadOptionalString(metadata["project"], "metadata.project", result.errors);
    if (!project || project->empty()) {
      AddError(result.errors, "metadata.project", "project is required");
    } else {
      config.metadata.project = *project;
    }
    if (const auto owner = ReadOptionalString(metadata["owner"], "metadata.owner", result.errors)) {
      config.metadata.owner = owner;
    }
  }

  config.defaults = ParseTargetDefaults(root["defaults"], "defaults", /*enforce_key_whitelist=*/true, result.errors);

  const auto environments = root["environments"];
  if (!NodeIsMap(environments)) {
    AddError(result.errors, "environments", "environments block is required and must be a mapping");
  } else {
    for (const auto& entry : environments) {
      if (!entry.first.IsScalar()) {
        AddError(result.errors, "environments", "environment name must be a string");
        continue;
      }
      const std::string env_name = entry.first.as<std::string>();
      if (env_name.empty()) {
        AddError(result.errors, "environments", "environment name cannot be empty");
        continue;
      }
      auto env = ParseEnvironment(env_name, entry.second, result.errors);
      if (config.environments.count(env_name) != 0) {
        AddError(result.errors, "environments." + env_name, "duplicate environment definition");
      } else {
        config.environments.emplace(env_name, std::move(env));
      }
    }
  }

  for (const auto& [env_name, env] : config.environments) {
    ValidateEnvironment(env, result.errors);
  }
  ValidateEnvironmentExtends(config, result.errors);
  ValidateGlobalForwardNames(config, result.errors);

  if (result.errors.empty()) {
    result.config = config;
  }
  return result;
}

}  // namespace kubeforward::config
