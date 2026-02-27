#include "kubeforward/cli.h"

#include <iostream>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include <cxxopts.hpp>

#include "kubeforward/config/loader.h"

namespace {

// Converts vector<string> argv representation into the char* form expected by cxxopts.
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

const char* AppVersion() { return KF_APP_VERSION; }

void PrintGeneralHelp() {
  std::cout << "kubeforward CLI\n"
            << "\n"
            << "Usage:\n"
            << "  kubeforward <command> [options]\n"
            << "  kubeforward --version\n"
            << "\n"
            << "Commands:\n"
            << "  plan    Render the normalized port-forward plan.\n"
            << "  up      Start port-forwards for one environment.\n"
            << "  down    Stop port-forwards for one or all environments.\n"
            << "  help    Show this message.\n"
            << "\n"
            << "Global options:\n"
            << "  --version    Show kubeforward CLI version.\n";
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

struct CommandOptions {
  bool show_help = false;
  bool daemon = false;
  bool verbose = false;
  std::string config_path = "kubeforward.yaml";
  std::string env_filter;
};

const char* RunMode(bool daemon) { return daemon ? "daemon" : "foreground"; }

void PrintForwardNames(const kubeforward::config::EnvironmentDefinition& env, const std::string& indent) {
  std::cout << indent << "forward names:\n";
  if (env.forwards.empty()) {
    std::cout << indent << "  <none>\n";
    return;
  }
  for (const auto& forward : env.forwards) {
    std::cout << indent << "  - " << forward.name << "\n";
  }
}

// Shared parser for commands that support the common -f/-e/-d/-v option contract.
bool ParseCommandOptions(const std::vector<std::string>& args, const std::string& command_name,
                         const std::string& description, CommandOptions& parsed, int& exit_code) {
  cxxopts::Options options(args.front(), description);
  options.add_options()
      ("h,help", "Show help", cxxopts::value<bool>(parsed.show_help)->default_value("false"))
      ("d,daemon", "Run in daemon mode (logs hidden)", cxxopts::value<bool>(parsed.daemon)->default_value("false"))
      ("v,verbose", "Show detailed command output", cxxopts::value<bool>(parsed.verbose)->default_value("false"))
      ("f,file", "Path to config file (defaults to kubeforward.yaml in current directory)",
          cxxopts::value<std::string>(parsed.config_path)->default_value("kubeforward.yaml"))
      ("e,env", "Environment to target", cxxopts::value<std::string>(parsed.env_filter));

  const auto c_args = ToCArgs(args);
  const int argc = static_cast<int>(c_args.size());
  char** argv = const_cast<char**>(c_args.data());
  try {
    options.parse_positional({});
    options.parse(argc, argv);
  } catch (const cxxopts::exceptions::exception& e) {
    std::cerr << command_name << ": " << e.what() << "\n";
    exit_code = 1;
    return false;
  }

  if (parsed.show_help) {
    std::cout << options.help() << "\n";
    exit_code = 0;
    return false;
  }

  exit_code = 0;
  return true;
}

std::optional<kubeforward::config::Config> LoadConfigForCommand(const std::string& command_name,
                                                                const std::string& config_path) {
  auto config_result = kubeforward::config::LoadConfigFromFile(config_path);
  if (!config_result.config) {
    std::cerr << command_name << ": failed to load config '" << config_path << "'.\n";
    for (const auto& error : config_result.errors) {
      std::cerr << "  - " << error.context << ": " << error.message << "\n";
    }
    return std::nullopt;
  }
  return config_result.config;
}

std::optional<std::string> ResolveSingleEnvironment(const kubeforward::config::Config& config,
                                                    const std::string& env_filter) {
  if (!env_filter.empty()) {
    return env_filter;
  }
  if (config.environments.empty()) {
    return std::nullopt;
  }
  return config.environments.begin()->first;
}

int RunUpCommand(const std::vector<std::string>& args) {
  // up always resolves to a single environment target.
  CommandOptions options;
  int parse_exit_code = 0;
  if (!ParseCommandOptions(args, "up", "Start port-forwards for one environment.", options, parse_exit_code)) {
    return parse_exit_code;
  }

  const auto config = LoadConfigForCommand("up", options.config_path);
  if (!config.has_value()) {
    return 2;
  }

  const auto env_name = ResolveSingleEnvironment(*config, options.env_filter);
  if (!env_name.has_value()) {
    std::cerr << "up: no environments defined in config.\n";
    return 2;
  }
  auto env_it = config->environments.find(*env_name);
  if (env_it == config->environments.end()) {
    std::cerr << "up: unknown environment '" << *env_name << "'.\n";
    return 2;
  }

  std::cout << "up: starting forwards\n";
  std::cout << "  file: " << options.config_path << "\n";
  std::cout << "  env: " << *env_name << "\n";
  std::cout << "  mode: " << RunMode(options.daemon) << "\n";
  std::cout << "  forwards: " << env_it->second.forwards.size() << "\n";
  if (options.verbose) {
    PrintForwardNames(env_it->second, "  ");
  }
  return 0;
}

int RunDownCommand(const std::vector<std::string>& args) {
  // down can target a single environment (--env) or all configured environments.
  CommandOptions options;
  int parse_exit_code = 0;
  if (!ParseCommandOptions(args, "down", "Stop port-forwards for one or all environments.", options,
                           parse_exit_code)) {
    return parse_exit_code;
  }

  const auto config = LoadConfigForCommand("down", options.config_path);
  if (!config.has_value()) {
    return 2;
  }

  std::cout << "down: stopping forwards\n";
  std::cout << "  file: " << options.config_path << "\n";
  std::cout << "  mode: " << RunMode(options.daemon) << "\n";

  if (!options.env_filter.empty()) {
    auto env_it = config->environments.find(options.env_filter);
    if (env_it == config->environments.end()) {
      std::cerr << "down: unknown environment '" << options.env_filter << "'.\n";
      return 2;
    }
    std::cout << "  scope: environment\n";
    std::cout << "  env: " << options.env_filter << "\n";
    std::cout << "  forwards: " << env_it->second.forwards.size() << "\n";
    if (options.verbose) {
      PrintForwardNames(env_it->second, "  ");
    }
    return 0;
  }

  size_t total_forwards = 0;
  for (const auto& [_, env] : config->environments) {
    total_forwards += env.forwards.size();
  }
  std::cout << "  scope: all environments\n";
  std::cout << "  environments: " << config->environments.size() << "\n";
  std::cout << "  forwards: " << total_forwards << "\n";
  if (options.verbose) {
    std::cout << "  environment breakdown:\n";
    for (const auto& [name, env] : config->environments) {
      std::cout << "    - " << name << " (" << env.forwards.size() << " forward(s))\n";
      PrintForwardNames(env, "      ");
    }
  }
  return 0;
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
      ("v,verbose", "Show detailed plan output",
       cxxopts::value<bool>(verbose)->default_value("false"));

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
  // Top-level dispatcher: commands are mutually exclusive and parsed by first token.
  if (args.empty()) {
    PrintGeneralHelp();
    return 1;
  }

  if (args.size() < 2) {
    auto sub_args = BuildSubcommandArgs(args, 1, "plan");
    return RunPlanCommand(sub_args);
  }

  const std::string command = args[1];
  if (command == "help" || command == "--help" || command == "-h") {
    PrintGeneralHelp();
    return 0;
  }

  if (command == "--version") {
    std::cout << AppVersion() << "\n";
    return 0;
  }

  if (command == "plan") {
    auto sub_args = BuildSubcommandArgs(args, 2, "plan");
    return RunPlanCommand(sub_args);
  }

  if (command == "up") {
    auto sub_args = BuildSubcommandArgs(args, 2, "up");
    return RunUpCommand(sub_args);
  }

  if (command == "down") {
    auto sub_args = BuildSubcommandArgs(args, 2, "down");
    return RunDownCommand(sub_args);
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
