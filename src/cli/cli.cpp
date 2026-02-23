#include "kubeforward/cli.h"

#include <iostream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include <cxxopts.hpp>

#include "kubeforward/config/loader.h"

namespace {

std::vector<const char*> ToCArgs(const std::vector<std::string>& args) {
  std::vector<const char*> result;
  result.reserve(args.size());
  for (const auto& arg : args) {
    result.push_back(arg.c_str());
  }
  return result;
}

std::vector<std::string> BuildSubcommandArgs(const std::vector<std::string>& args, size_t start_index,
                                             const std::string& command_name) {
  std::vector<std::string> sub_args;
  sub_args.reserve(args.size() - start_index + 1);
  sub_args.emplace_back(args.front() + " " + command_name);
  for (size_t i = start_index; i < args.size(); ++i) {
    sub_args.push_back(args[i]);
  }
  return sub_args;
}

void PrintGeneralHelp() {
  std::cout << "kubeforward CLI\n"
            << "\n"
            << "Usage:\n"
            << "  kubeforward <command> [options]\n"
            << "\n"
            << "Commands:\n"
            << "  plan    Render the normalized port-forward plan.\n"
            << "  help    Show this message.\n";
}

std::string OptionalValueOr(const std::optional<std::string>& value, const std::string& fallback = "<unset>") {
  return value.has_value() ? *value : fallback;
}

const char* ResourceKindToString(kubeforward::config::ResourceKind kind) {
  switch (kind) {
    case kubeforward::config::ResourceKind::kPod:
      return "pod";
    case kubeforward::config::ResourceKind::kDeployment:
      return "deployment";
    case kubeforward::config::ResourceKind::kService:
      return "service";
    case kubeforward::config::ResourceKind::kStatefulSet:
      return "statefulset";
  }
  return "unknown";
}

const char* PortProtocolToString(kubeforward::config::PortProtocol protocol) {
  switch (protocol) {
    case kubeforward::config::PortProtocol::kTcp:
      return "tcp";
    case kubeforward::config::PortProtocol::kUdp:
      return "udp";
  }
  return "unknown";
}

const char* RestartPolicyToString(kubeforward::config::RestartPolicy policy) {
  switch (policy) {
    case kubeforward::config::RestartPolicy::kFailFast:
      return "fail-fast";
    case kubeforward::config::RestartPolicy::kReplace:
      return "replace";
  }
  return "unknown";
}

void PrintStringMap(const std::map<std::string, std::string>& values, const std::string& indent) {
  if (values.empty()) {
    std::cout << indent << "<none>\n";
    return;
  }
  for (const auto& [key, value] : values) {
    std::cout << indent << key << "=" << value << "\n";
  }
}

void PrintPlanSummary(const std::string& name, const kubeforward::config::EnvironmentDefinition& env) {
  std::cout << "Environment: " << name << "\n";
  if (env.description.has_value()) {
    std::cout << "  Description: " << *env.description << "\n";
  }
  std::cout << "  Forwards (" << env.forwards.size() << ")\n";
  for (const auto& forward : env.forwards) {
    std::cout << "    - " << forward.name << " [" << forward.ports.size() << " port(s)]\n";
  }
  std::cout << "\n";
}

void PrintVerboseHeader(const kubeforward::config::Config& config, const std::string& config_path) {
  std::cout << "Config file: " << config_path << "\n";
  std::cout << "Version: " << config.version << "\n";
  std::cout << "Metadata:\n";
  std::cout << "  project: " << config.metadata.project << "\n";
  std::cout << "  owner: " << OptionalValueOr(config.metadata.owner) << "\n";
  std::cout << "Defaults:\n";
  std::cout << "  kubeconfig: " << OptionalValueOr(config.defaults.kubeconfig) << "\n";
  std::cout << "  context: " << OptionalValueOr(config.defaults.context) << "\n";
  std::cout << "  namespace: " << OptionalValueOr(config.defaults.namespace_name) << "\n";
  std::cout << "  bindAddress: " << OptionalValueOr(config.defaults.bind_address) << "\n";
  std::cout << "  labels:\n";
  PrintStringMap(config.defaults.labels, "    ");
  std::cout << "\n";
}

void PrintPlanVerbose(const std::string& name, const kubeforward::config::EnvironmentDefinition& env) {
  std::cout << "Environment: " << name << "\n";
  std::cout << "  extends: " << OptionalValueOr(env.extends) << "\n";
  std::cout << "  description: " << OptionalValueOr(env.description) << "\n";
  std::cout << "  settings:\n";
  std::cout << "    kubeconfig: " << OptionalValueOr(env.settings.kubeconfig) << "\n";
  std::cout << "    context: " << OptionalValueOr(env.settings.context) << "\n";
  std::cout << "    namespace: " << OptionalValueOr(env.settings.namespace_name) << "\n";
  std::cout << "    bindAddress: " << OptionalValueOr(env.settings.bind_address) << "\n";
  std::cout << "    labels:\n";
  PrintStringMap(env.settings.labels, "      ");
  std::cout << "  guards:\n";
  std::cout << "    allowProduction: " << (env.guards.allow_production ? "true" : "false") << "\n";
  std::cout << "  forwards:\n";
  if (env.forwards.empty()) {
    std::cout << "    <none>\n\n";
    return;
  }
  for (const auto& forward : env.forwards) {
    std::cout << "    - name: " << forward.name << "\n";
    std::cout << "      container: " << OptionalValueOr(forward.container) << "\n";
    std::cout << "      resource:\n";
    std::cout << "        kind: " << ResourceKindToString(forward.resource.kind) << "\n";
    std::cout << "        name: " << OptionalValueOr(forward.resource.name) << "\n";
    std::cout << "        namespace: " << OptionalValueOr(forward.resource.namespace_override) << "\n";
    std::cout << "        selector:\n";
    PrintStringMap(forward.resource.selector, "          ");
    std::cout << "      annotations:\n";
    std::cout << "        detach: " << (forward.detach ? "true" : "false") << "\n";
    std::cout << "        restartPolicy: " << RestartPolicyToString(forward.restart_policy) << "\n";
    std::cout << "        passthrough:\n";
    PrintStringMap(forward.annotations, "          ");
    std::cout << "      healthCheck:\n";
    if (!forward.health_check.has_value()) {
      std::cout << "        <none>\n";
    } else {
      std::cout << "        timeoutMs: "
                << (forward.health_check->timeout_ms.has_value() ? std::to_string(*forward.health_check->timeout_ms)
                                                                 : "<unset>")
                << "\n";
      std::cout << "        exec:\n";
      if (forward.health_check->exec.empty()) {
        std::cout << "          <none>\n";
      } else {
        for (const auto& command_part : forward.health_check->exec) {
          std::cout << "          - " << command_part << "\n";
        }
      }
    }
    std::cout << "      env:\n";
    PrintStringMap(forward.env, "        ");
    std::cout << "      ports:\n";
    if (forward.ports.empty()) {
      std::cout << "        <none>\n";
    } else {
      for (const auto& port : forward.ports) {
        std::cout << "        - " << port.local_port << " -> " << port.remote_port
                  << " (" << PortProtocolToString(port.protocol) << ")\n";
        std::cout << "          bindAddress: " << OptionalValueOr(port.bind_address) << "\n";
      }
    }
  }
  std::cout << "\n";
}

int RunPlanCommand(const std::vector<std::string>& args) {
  bool show_help = false;
  bool verbose = false;
  std::string config_path = "kubeforward.yaml";
  std::string env_filter;

  cxxopts::Options options(args.front(), "Render the normalized port-forward plan from kubeforward.yaml.");
  options.add_options()
      ("h,help", "Show help for plan command", cxxopts::value<bool>(show_help)->default_value("false"))
      ("f,file", "Path to config file (defaults to kubeforward.yaml in current directory)",
          cxxopts::value<std::string>(config_path)->default_value("kubeforward.yaml"))
      ("e,env", "Environment to display", cxxopts::value<std::string>(env_filter))
      ("v,verbose", "Show detailed plan output", cxxopts::value<bool>(verbose)->default_value("false"));

  const auto c_args = ToCArgs(args);
  const int argc = static_cast<int>(c_args.size());
  char** argv = const_cast<char**>(c_args.data());

  try {
    options.parse_positional({});
    options.parse(argc, argv);
  } catch (const cxxopts::exceptions::exception& e) {
    std::cerr << "plan: " << e.what() << "\n";
    return 1;
  }

  if (show_help) {
    std::cout << options.help() << "\n";
    return 0;
  }

  auto config_result = kubeforward::config::LoadConfigFromFile(config_path);
  if (!config_result.config) {
    std::cerr << "plan: failed to load config '" << config_path << "'.\n";
    for (const auto& error : config_result.errors) {
      std::cerr << "  - " << error.context << ": " << error.message << "\n";
    }
    return 2;
  }

  const auto& config = *config_result.config;
  std::vector<std::pair<std::string, kubeforward::config::EnvironmentDefinition>> environments;
  environments.reserve(config.environments.size());

  if (config.environments.empty()) {
    std::cout << "No environments defined in config.\n";
    return 0;
  }

  if (!env_filter.empty()) {
    auto it = config.environments.find(env_filter);
    if (it == config.environments.end()) {
      std::cerr << "plan: unknown environment '" << env_filter << "'.\n";
      return 2;
    }
    environments.emplace_back(*it);
  } else {
    for (const auto& entry : config.environments) {
      environments.emplace_back(entry);
    }
  }

  if (verbose) {
    PrintVerboseHeader(config, config_path);
  }

  for (const auto& [name, env] : environments) {
    if (verbose) {
      PrintPlanVerbose(name, env);
    } else {
      PrintPlanSummary(name, env);
    }
  }

  return 0;
}

}  // namespace

namespace kubeforward {

int run_cli(const std::vector<std::string>& args) {
  if (args.size() < 2) {
    auto sub_args = BuildSubcommandArgs(args, 1, "plan");
    return RunPlanCommand(sub_args);
  }

  const std::string command = args[1];
  if (command == "help" || command == "--help" || command == "-h") {
    PrintGeneralHelp();
    return 0;
  }

  if (command == "plan") {
    auto sub_args = BuildSubcommandArgs(args, 2, "plan");
    return RunPlanCommand(sub_args);
  }

  if (!command.empty() && command[0] == '-') {
    auto sub_args = BuildSubcommandArgs(args, 1, "plan");
    return RunPlanCommand(sub_args);
  }

  std::cerr << "Unknown command '" << command << "'.\n\n";
  PrintGeneralHelp();
  return 1;
}

}  // namespace kubeforward
