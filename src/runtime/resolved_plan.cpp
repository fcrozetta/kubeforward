#include "kubeforward/runtime/resolved_plan.h"

#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace kubeforward::runtime {
namespace {

config::TargetDefaults MergeTargetDefaults(const config::TargetDefaults& base, const config::TargetDefaults& override) {
  config::TargetDefaults merged = base;
  if (override.kubeconfig.has_value()) {
    merged.kubeconfig = override.kubeconfig;
  }
  if (override.context.has_value()) {
    merged.context = override.context;
  }
  if (override.namespace_name.has_value()) {
    merged.namespace_name = override.namespace_name;
  }
  if (override.bind_address.has_value()) {
    merged.bind_address = override.bind_address;
  }
  for (const auto& [key, value] : override.labels) {
    merged.labels[key] = value;
  }
  return merged;
}

config::EnvironmentGuards MergeEnvironmentGuards(const config::EnvironmentGuards& base,
                                                 const config::EnvironmentGuards& override) {
  config::EnvironmentGuards merged = base;
  merged.allow_production = base.allow_production || override.allow_production;
  return merged;
}

void AddError(std::vector<PlanBuildError>& errors, const std::string& context, const std::string& message) {
  errors.push_back(PlanBuildError{context, message});
}

std::vector<ResolvedForward> ResolveForwards(const std::string& env_name, const config::EnvironmentDefinition& env,
                                             const config::TargetDefaults& settings,
                                             const std::vector<ResolvedForward>& inherited_forwards,
                                             std::vector<PlanBuildError>& errors) {
  std::vector<ResolvedForward> forwards;
  if (env.forwards.empty()) {
    forwards = inherited_forwards;
    for (auto& forward : forwards) {
      forward.environment = env_name;
    }
    return forwards;
  }

  forwards.reserve(env.forwards.size());
  for (size_t i = 0; i < env.forwards.size(); ++i) {
    const auto& source = env.forwards[i];
    const std::string context = "environments." + env_name + ".forwards[" + std::to_string(i) + "]";

    ResolvedForward forward;
    forward.environment = env_name;
    forward.name = source.name;
    forward.resource = source.resource;
    forward.detach = source.detach;
    forward.restart_policy = source.restart_policy;
    forward.health_check = source.health_check;
    forward.env = source.env;
    forward.annotations = source.annotations;

    const std::string namespace_name = source.resource.namespace_override.has_value()
                                           ? *source.resource.namespace_override
                                           : settings.namespace_name.value_or("");
    if (namespace_name.empty()) {
      AddError(errors, context + ".resource.namespace",
               "resolved namespace is empty (set resource.namespace, environment namespace, or defaults.namespace)");
    } else {
      forward.namespace_name = namespace_name;
    }

    forward.ports.reserve(source.ports.size());
    for (const auto& port : source.ports) {
      auto resolved_port = port;
      if (!resolved_port.bind_address.has_value() && settings.bind_address.has_value()) {
        resolved_port.bind_address = settings.bind_address;
      }
      forward.ports.push_back(resolved_port);
    }

    forwards.push_back(std::move(forward));
  }
  return forwards;
}

std::optional<ResolvedEnvironment> ResolveEnvironmentRecursive(
    const std::string& env_name, const config::Config& config, std::map<std::string, ResolvedEnvironment>& cache,
    std::set<std::string>& visiting, std::vector<PlanBuildError>& errors) {
  const auto cached = cache.find(env_name);
  if (cached != cache.end()) {
    return cached->second;
  }

  if (visiting.count(env_name) != 0) {
    AddError(errors, "environments." + env_name + ".extends", "cyclic dependency detected during plan resolution");
    return std::nullopt;
  }

  const auto env_it = config.environments.find(env_name);
  if (env_it == config.environments.end()) {
    AddError(errors, "environments." + env_name, "unknown environment");
    return std::nullopt;
  }
  const auto& env = env_it->second;

  visiting.insert(env_name);
  config::TargetDefaults effective_settings = config.defaults;
  config::EnvironmentGuards effective_guards = {};
  std::vector<ResolvedForward> inherited_forwards;

  if (env.extends.has_value()) {
    const auto parent = ResolveEnvironmentRecursive(*env.extends, config, cache, visiting, errors);
    if (parent.has_value()) {
      effective_settings = parent->settings;
      effective_guards = parent->guards;
      inherited_forwards = parent->forwards;
    }
  }

  effective_settings = MergeTargetDefaults(effective_settings, env.settings);
  effective_guards = MergeEnvironmentGuards(effective_guards, env.guards);

  ResolvedEnvironment resolved;
  resolved.name = env_name;
  resolved.settings = effective_settings;
  resolved.guards = effective_guards;
  resolved.forwards = ResolveForwards(env_name, env, effective_settings, inherited_forwards, errors);

  visiting.erase(env_name);
  cache.emplace(env_name, resolved);
  return resolved;
}

}  // namespace

PlanBuildResult BuildResolvedPlan(const config::Config& config, const std::string& config_path,
                                  const std::optional<std::string>& env_filter) {
  PlanBuildResult result;
  ResolvedPlan plan;
  plan.config_path = config_path;

  std::vector<std::string> targets;
  if (env_filter.has_value()) {
    if (config.environments.count(*env_filter) == 0) {
      AddError(result.errors, "environments." + *env_filter, "unknown environment");
      return result;
    }
    targets.push_back(*env_filter);
  } else {
    targets.reserve(config.environments.size());
    for (const auto& [name, _] : config.environments) {
      targets.push_back(name);
    }
  }

  std::map<std::string, ResolvedEnvironment> cache;
  std::set<std::string> visiting;
  for (const auto& target : targets) {
    const auto resolved = ResolveEnvironmentRecursive(target, config, cache, visiting, result.errors);
    if (!resolved.has_value()) {
      continue;
    }
    plan.environments.push_back(*resolved);
  }

  if (result.errors.empty()) {
    result.plan = plan;
  }
  return result;
}

}  // namespace kubeforward::runtime
