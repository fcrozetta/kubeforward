#include "kubeforward/cli.h"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <functional>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include <arpa/inet.h>
#include <cxxopts.hpp>
#include <netinet/in.h>
#include <signal.h>
#include <sys/socket.h>
#include <unistd.h>

#include "kubeforward/config/loader.h"
#include "kubeforward/runtime/process_runner.h"
#include "kubeforward/runtime/resolved_plan.h"
#include "kubeforward/runtime/state_store.h"

namespace {

//! Converts vector<string> argv representation into the char* form expected by cxxopts.
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
    std::cout << "      resource:\n";
    std::cout << "        kind: " << ResourceKindToString(forward.resource.kind) << "\n";
    std::cout << "        name: " << OptionalValueOr(forward.resource.name) << "\n";
    std::cout << "        namespace: " << OptionalValueOr(forward.resource.namespace_override) << "\n";
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

std::string ResourceKindTargetPrefix(kubeforward::config::ResourceKind kind) {
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
  return "pod";
}

const char* KubectlBinary() {
  if (const char* override_bin = std::getenv("KUBEFORWARD_KUBECTL_BIN")) {
    if (override_bin[0] != '\0') {
      return override_bin;
    }
  }
  return "kubectl";
}

bool UseNoopRunner() {
  if (const char* value = std::getenv("KUBEFORWARD_USE_NOOP_RUNNER")) {
    return std::string(value) == "1";
  }
  return false;
}

std::unique_ptr<kubeforward::runtime::ProcessRunner> MakeProcessRunner() {
  if (UseNoopRunner()) {
    return std::make_unique<kubeforward::runtime::NoopProcessRunner>();
  }
  return std::make_unique<kubeforward::runtime::PosixProcessRunner>();
}

std::string ResolveBindAddress(const kubeforward::config::PortMapping& port) {
  if (port.bind_address.has_value() && !port.bind_address->empty()) {
    return *port.bind_address;
  }
  return "127.0.0.1";
}

bool IsPidAlive(int pid) {
  if (pid <= 0) {
    return false;
  }
  if (::kill(pid, 0) == 0) {
    return true;
  }
  return errno == EPERM;
}

bool CheckPortAvailability(const kubeforward::config::PortMapping& port, std::string& error) {
  const int socket_type = port.protocol == kubeforward::config::PortProtocol::kUdp ? SOCK_DGRAM : SOCK_STREAM;
  const int fd = ::socket(AF_INET, socket_type, 0);
  if (fd < 0) {
    if (errno == EPERM || errno == EACCES) {
      // Restricted runtimes (tests/sandboxes) may forbid socket probes.
      error.clear();
      return true;
    }
    error = "failed to create socket for preflight";
    return false;
  }

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(static_cast<uint16_t>(port.local_port));
  const auto bind_address = ResolveBindAddress(port);
  if (::inet_pton(AF_INET, bind_address.c_str(), &addr.sin_addr) != 1) {
    ::close(fd);
    error = "invalid bind address '" + bind_address + "'";
    return false;
  }

  if (::bind(fd, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) != 0) {
    if (errno == EPERM || errno == EACCES) {
      // Restricted runtimes (tests/sandboxes) may forbid bind probes.
      ::close(fd);
      error.clear();
      return true;
    }
    std::ostringstream oss;
    if (errno == EADDRINUSE) {
      oss << "local port " << port.local_port << " is already in use on " << bind_address;
    } else {
      oss << "failed to preflight port " << port.local_port << " on " << bind_address << ": " << std::strerror(errno);
    }
    error = oss.str();
    ::close(fd);
    return false;
  }

  ::close(fd);
  error.clear();
  return true;
}

std::string SanitizeLogToken(const std::string& token) {
  std::string result;
  result.reserve(token.size());
  for (char ch : token) {
    const bool safe = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '-' ||
                      ch == '_' || ch == '.';
    result.push_back(safe ? ch : '_');
  }
  if (result.empty()) {
    return "forward";
  }
  return result;
}

std::filesystem::path DefaultLogsDirectoryForConfig(const std::string& normalized_config_path) {
  const size_t hash = std::hash<std::string>{}(normalized_config_path);
  const auto base_dir = std::filesystem::temp_directory_path() / "kubeforward";
  return base_dir / ("logs-" + std::to_string(hash));
}

std::filesystem::path BuildForwardLogPath(const std::string& normalized_config_path, const std::string& env_name,
                                          const std::string& forward_name, int local_port) {
  const auto logs_dir = DefaultLogsDirectoryForConfig(normalized_config_path);
  std::ostringstream filename;
  filename << SanitizeLogToken(env_name) << "-" << SanitizeLogToken(forward_name) << "-" << local_port << ".log";
  return logs_dir / filename.str();
}

bool BuildKubectlPortForwardArgv(const kubeforward::runtime::ResolvedEnvironment& env,
                                 const kubeforward::runtime::ResolvedForward& forward,
                                 const kubeforward::config::PortMapping& port, std::vector<std::string>& argv,
                                 std::string& error) {
  if (port.protocol != kubeforward::config::PortProtocol::kTcp) {
    error = "unsupported protocol for kubectl port-forward (only tcp is supported)";
    return false;
  }

  if (!forward.resource.name.has_value() || forward.resource.name->empty()) {
    error = "resource.name is required for kubectl port-forward";
    return false;
  }

  const std::string target = ResourceKindTargetPrefix(forward.resource.kind) + "/" + *forward.resource.name;
  argv = {KubectlBinary(), "port-forward", target, std::to_string(port.local_port) + ":" + std::to_string(port.remote_port)};
  argv.push_back("--namespace");
  argv.push_back(forward.namespace_name);
  if (env.settings.context.has_value() && !env.settings.context->empty()) {
    argv.push_back("--context");
    argv.push_back(*env.settings.context);
  }
  if (env.settings.kubeconfig.has_value() && !env.settings.kubeconfig->empty()) {
    argv.push_back("--kubeconfig");
    argv.push_back(*env.settings.kubeconfig);
  }
  if (port.bind_address.has_value() && !port.bind_address->empty()) {
    argv.push_back("--address");
    argv.push_back(*port.bind_address);
  }
  error.clear();
  return true;
}

bool CheckRuntimeSessionPortConflicts(const kubeforward::runtime::RuntimeState& state,
                                      const std::string& normalized_config_path,
                                      const kubeforward::runtime::ResolvedEnvironment& target_env, std::string& error) {
  std::set<int> target_ports;
  for (const auto& forward : target_env.forwards) {
    for (const auto& port : forward.ports) {
      target_ports.insert(port.local_port);
    }
  }

  for (const auto& session : state.sessions) {
    if (session.config_path == normalized_config_path && session.environment == target_env.name) {
      continue;
    }
    for (const auto& process : session.forwards) {
      if (target_ports.count(process.local_port) == 0) {
        continue;
      }
      if (!IsPidAlive(process.pid)) {
        continue;
      }
      std::ostringstream oss;
      oss << "local port " << process.local_port << " is already claimed by running session '" << session.id << "'";
      error = oss.str();
      return false;
    }
  }

  error.clear();
  return true;
}

bool CheckPlanPortsAvailable(const kubeforward::runtime::ResolvedEnvironment& target_env, std::string& error) {
  for (const auto& forward : target_env.forwards) {
    for (const auto& port : forward.ports) {
      if (!CheckPortAvailability(port, error)) {
        return false;
      }
    }
  }
  error.clear();
  return true;
}

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

void PrintForwardNames(const kubeforward::runtime::ResolvedEnvironment& env, const std::string& indent) {
  std::cout << indent << "forward names:\n";
  if (env.forwards.empty()) {
    std::cout << indent << "  <none>\n";
    return;
  }
  for (const auto& forward : env.forwards) {
    std::cout << indent << "  - " << forward.name << "\n";
  }
}

//! Shared parser for commands that support the common -f/-e/-d/-v option contract.
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

std::string UtcNowString() {
  const std::time_t now = std::time(nullptr);
  std::tm utc_tm{};
#if defined(_WIN32)
  gmtime_s(&utc_tm, &now);
#else
  gmtime_r(&now, &utc_tm);
#endif
  std::ostringstream oss;
  oss << std::put_time(&utc_tm, "%Y-%m-%dT%H:%M:%SZ");
  return oss.str();
}

std::string NormalizePath(const std::string& raw_path) {
  std::error_code ec;
  const auto absolute = std::filesystem::absolute(raw_path, ec);
  if (ec) {
    return raw_path;
  }
  return absolute.string();
}

int RunUpCommand(const std::vector<std::string>& args) {
  //! up always resolves to a single environment target.
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

  const auto plan_result =
      kubeforward::runtime::BuildResolvedPlan(*config, options.config_path, std::optional<std::string>{*env_name});
  if (!plan_result.ok()) {
    std::cerr << "up: failed to resolve execution plan.\n";
    for (const auto& error : plan_result.errors) {
      std::cerr << "  - " << error.context << ": " << error.message << "\n";
    }
    return 2;
  }
  const auto& resolved_env = plan_result.plan->environments.at(0);
  const auto normalized_config_path = NormalizePath(options.config_path);
  const auto state_path = kubeforward::runtime::DefaultStatePathForConfig(normalized_config_path);
  const auto state_load = kubeforward::runtime::LoadState(state_path);
  if (!state_load.ok()) {
    std::cerr << "up: failed to load runtime state '" << state_path.string() << "'.\n";
    for (const auto& error : state_load.errors) {
      std::cerr << "  - " << error << "\n";
    }
    return 2;
  }

  kubeforward::runtime::RuntimeState state = state_load.state;
  auto runner = MakeProcessRunner();
  bool replace_stop_failed = false;
  int replaced_processes = 0;
  state.sessions.erase(
      std::remove_if(state.sessions.begin(), state.sessions.end(),
                     [&](const kubeforward::runtime::ManagedSession& existing) {
                       if (existing.config_path != normalized_config_path || existing.environment != resolved_env.name) {
                         return false;
                       }
                       bool session_failed = false;
                       for (const auto& process : existing.forwards) {
                         std::string stop_error;
                         if (!runner->Stop(process.pid, stop_error)) {
                           std::cerr << "up: failed to stop replaced pid " << process.pid << ": " << stop_error << "\n";
                           replace_stop_failed = true;
                           session_failed = true;
                         } else {
                           ++replaced_processes;
                         }
                       }
                       return !session_failed;
                     }),
      state.sessions.end());

  if (replace_stop_failed) {
    return 2;
  }

  if (!UseNoopRunner()) {
    std::string preflight_error;
    if (!CheckRuntimeSessionPortConflicts(state, normalized_config_path, resolved_env, preflight_error)) {
      std::cerr << "up: preflight failed: " << preflight_error << "\n";
      return 2;
    }
    if (!CheckPlanPortsAvailable(resolved_env, preflight_error)) {
      std::cerr << "up: preflight failed: " << preflight_error << "\n";
      return 2;
    }
  }

  kubeforward::runtime::ManagedSession session;
  session.id = normalized_config_path + "::" + resolved_env.name + "::" + UtcNowString();
  session.config_path = normalized_config_path;
  session.environment = resolved_env.name;
  session.daemon = options.daemon;
  session.started_at_utc = UtcNowString();

  for (const auto& forward : resolved_env.forwards) {
    for (const auto& port : forward.ports) {
      std::vector<std::string> argv;
      std::string argv_error;
      if (!BuildKubectlPortForwardArgv(resolved_env, forward, port, argv, argv_error)) {
        std::cerr << "up: invalid forward '" << forward.name << "': " << argv_error << "\n";
        return 2;
      }

      kubeforward::runtime::StartProcessRequest request;
      request.cwd = std::filesystem::current_path();
      request.daemon = options.daemon;
      request.argv = argv;
      request.log_path = BuildForwardLogPath(normalized_config_path, resolved_env.name, forward.name, port.local_port);

      std::string start_error;
      const auto started = runner->Start(request, start_error);
      if (!started.has_value()) {
        std::cerr << "up: failed to start forward '" << forward.name << "': " << start_error << "\n";
        for (const auto& started_forward : session.forwards) {
          std::string rollback_error;
          (void)runner->Stop(started_forward.pid, rollback_error);
        }
        return 2;
      }

      session.forwards.push_back(kubeforward::runtime::ManagedForwardProcess{
          .environment = resolved_env.name,
          .forward_name = forward.name,
          .local_port = port.local_port,
          .remote_port = port.remote_port,
          .pid = started->pid,
      });
    }
  }
  state.sessions.push_back(session);

  std::string save_error;
  if (!kubeforward::runtime::SaveState(state_path, state, save_error)) {
    std::cerr << "up: failed to save runtime state '" << state_path.string() << "': " << save_error << "\n";
    return 2;
  }

  std::cout << "up: starting forwards\n";
  std::cout << "  file: " << options.config_path << "\n";
  std::cout << "  env: " << *env_name << "\n";
  std::cout << "  mode: " << RunMode(options.daemon) << "\n";
  std::cout << "  forwards: " << resolved_env.forwards.size() << "\n";
  if (options.verbose) {
    std::cout << "  state: " << state_path.string() << "\n";
    std::cout << "  kubectl: " << KubectlBinary() << "\n";
    std::cout << "  replaced: " << replaced_processes << "\n";
    std::cout << "  logs: " << DefaultLogsDirectoryForConfig(normalized_config_path).string() << "\n";
    PrintForwardNames(resolved_env, "  ");
  }
  return 0;
}

int RunDownCommand(const std::vector<std::string>& args) {
  //! down can target a single environment (--env) or all configured environments.
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

  const auto normalized_config_path = NormalizePath(options.config_path);
  const auto state_path = kubeforward::runtime::DefaultStatePathForConfig(normalized_config_path);
  const auto state_load = kubeforward::runtime::LoadState(state_path);
  if (!state_load.ok()) {
    std::cerr << "down: failed to load runtime state '" << state_path.string() << "'.\n";
    for (const auto& error : state_load.errors) {
      std::cerr << "  - " << error << "\n";
    }
    return 2;
  }
  kubeforward::runtime::RuntimeState state = state_load.state;
  auto runner = MakeProcessRunner();
  int stopped_processes = 0;
  bool stop_failed = false;

  auto stop_session = [&](const kubeforward::runtime::ManagedSession& session) -> bool {
    if (session.config_path != normalized_config_path) {
      return false;
    }
    if (!options.env_filter.empty() && session.environment != options.env_filter) {
      return false;
    }
    bool session_failed = false;
    for (const auto& process : session.forwards) {
      std::string stop_error;
      if (!runner->Stop(process.pid, stop_error)) {
        std::cerr << "down: failed to stop pid " << process.pid << ": " << stop_error << "\n";
        stop_failed = true;
        session_failed = true;
      } else {
        ++stopped_processes;
      }
    }
    return !session_failed;
  };

  state.sessions.erase(std::remove_if(state.sessions.begin(), state.sessions.end(), stop_session), state.sessions.end());

  std::string save_error;
  if (!kubeforward::runtime::SaveState(state_path, state, save_error)) {
    std::cerr << "down: failed to save runtime state '" << state_path.string() << "': " << save_error << "\n";
    return 2;
  }

  if (stop_failed) {
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
      std::cout << "  state: " << state_path.string() << "\n";
      std::cout << "  stopped: " << stopped_processes << "\n";
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
    std::cout << "  state: " << state_path.string() << "\n";
    std::cout << "  stopped: " << stopped_processes << "\n";
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
  //! Top-level dispatcher: commands are mutually exclusive and parsed by first token.
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
