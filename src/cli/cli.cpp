#include "kubeforward/cli.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cstdio>
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
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

#include "kubeforward/config/loader.h"
#include "kubeforward/runtime/process_runner.h"
#include "kubeforward/runtime/resolved_plan.h"
#include "kubeforward/runtime/session_conflicts.h"
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

volatile sig_atomic_t g_foreground_signal = 0;

void HandleForegroundSignal(int signal_number) { g_foreground_signal = signal_number; }

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

void PrintPlanSummary(const kubeforward::config::EnvironmentDefinition& source_env,
                      const kubeforward::runtime::ResolvedEnvironment& env) {
  std::cout << "Environment: " << env.name << "\n";
  if (source_env.description.has_value()) {
    std::cout << "  Description: " << *source_env.description << "\n";
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

void PrintPlanVerbose(const kubeforward::config::EnvironmentDefinition& source_env,
                      const kubeforward::runtime::ResolvedEnvironment& env) {
  std::cout << "Environment: " << env.name << "\n";
  std::cout << "  extends: " << OptionalValueOr(source_env.extends) << "\n";
  std::cout << "  description: " << OptionalValueOr(source_env.description) << "\n";
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

std::optional<std::filesystem::path> ResolveExecutablePath(const std::string& executable) {
  if (executable.empty()) {
    return std::nullopt;
  }

  const std::filesystem::path executable_path(executable);
  const bool has_path_separator = executable.find('/') != std::string::npos;
  if (has_path_separator || executable_path.is_absolute()) {
    if (::access(executable.c_str(), X_OK) == 0) {
      return executable_path;
    }
    return std::nullopt;
  }

  const char* path_env = std::getenv("PATH");
  if (path_env == nullptr) {
    return std::nullopt;
  }

  std::stringstream path_stream(path_env);
  std::string entry;
  while (std::getline(path_stream, entry, ':')) {
    const auto candidate = std::filesystem::path(entry) / executable;
    if (::access(candidate.c_str(), X_OK) == 0) {
      return candidate;
    }
  }

  return std::nullopt;
}

bool ValidateKubectlExecutable(std::string& error) {
  if (ResolveExecutablePath(KubectlBinary()).has_value()) {
    error.clear();
    return true;
  }

  error = "kubectl executable is not available: " + std::string(KubectlBinary());
  return false;
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
  return absolute.lexically_normal().string();
}

size_t CountSessionForwards(const kubeforward::runtime::ManagedSession& session) { return session.forwards.size(); }

std::vector<const kubeforward::runtime::ManagedSession*> MatchingSessions(
    const kubeforward::runtime::RuntimeState& state, const std::string& normalized_config_path,
    const std::string& env_filter) {
  std::vector<const kubeforward::runtime::ManagedSession*> matches;
  for (const auto& session : state.sessions) {
    if (session.config_path != normalized_config_path) {
      continue;
    }
    if (!env_filter.empty() && session.environment != env_filter) {
      continue;
    }
    matches.push_back(&session);
  }
  return matches;
}

struct PreparedForwardLaunch {
  std::string forward_name;
  kubeforward::config::PortMapping port;
  kubeforward::runtime::StartProcessRequest request;
};

kubeforward::runtime::ManagedSession MakeManagedSession(const std::string& normalized_config_path,
                                                        const kubeforward::runtime::ResolvedEnvironment& resolved_env,
                                                        bool daemon) {
  kubeforward::runtime::ManagedSession session;
  session.id = normalized_config_path + "::" + resolved_env.name + "::" + UtcNowString();
  session.config_path = normalized_config_path;
  session.environment = resolved_env.name;
  session.daemon = daemon;
  session.started_at_utc = UtcNowString();
  return session;
}

bool BuildPreparedLaunches(const std::string& normalized_config_path,
                           const kubeforward::runtime::ResolvedEnvironment& resolved_env, bool daemon,
                           std::vector<PreparedForwardLaunch>& launches, std::string& error) {
  launches.clear();

  std::error_code cwd_error;
  const auto cwd = std::filesystem::current_path(cwd_error);
  if (cwd_error) {
    error = "failed to resolve current working directory: " + cwd_error.message();
    return false;
  }

  for (const auto& forward : resolved_env.forwards) {
    for (const auto& port : forward.ports) {
      std::vector<std::string> argv;
      if (!BuildKubectlPortForwardArgv(resolved_env, forward, port, argv, error)) {
        error = "invalid forward '" + forward.name + "': " + error;
        return false;
      }

      kubeforward::runtime::StartProcessRequest request;
      request.cwd = cwd;
      request.daemon = daemon;
      request.argv = std::move(argv);
      request.log_path = BuildForwardLogPath(normalized_config_path, resolved_env.name, forward.name, port.local_port);
      launches.push_back(PreparedForwardLaunch{.forward_name = forward.name, .port = port, .request = std::move(request)});
    }
  }

  error.clear();
  return true;
}

bool BuildPreparedLaunchesFromSession(const kubeforward::runtime::ManagedSession& session,
                                      std::vector<PreparedForwardLaunch>& launches, std::string& error) {
  launches.clear();
  launches.reserve(session.forwards.size());

  for (size_t i = 0; i < session.forwards.size(); ++i) {
    const auto& forward = session.forwards[i];
    if (forward.argv.empty()) {
      error = "session '" + session.id + "' cannot be restored because forward '" + forward.forward_name +
              "' has no stored argv";
      return false;
    }

    kubeforward::runtime::StartProcessRequest request;
    request.argv = forward.argv;
    request.cwd = forward.cwd;
    request.daemon = session.daemon;
    request.log_path = forward.log_path;

    kubeforward::config::PortMapping port;
    port.local_port = forward.local_port;
    port.remote_port = forward.remote_port;
    port.bind_address = forward.bind_address;
    port.protocol = forward.protocol;

    launches.push_back(PreparedForwardLaunch{
        .forward_name = forward.forward_name,
        .port = port,
        .request = std::move(request),
    });
  }

  error.clear();
  return true;
}

bool ValidateSessionSupportsRollback(const kubeforward::runtime::ManagedSession& session, std::string& error) {
  for (const auto& forward : session.forwards) {
    if (!forward.argv.empty()) {
      continue;
    }
    error = "existing session '" + session.id + "' cannot be replaced safely because forward '" + forward.forward_name +
            "' was created by an older runtime state format without restart metadata; run 'down' first";
    return false;
  }

  error.clear();
  return true;
}

bool ValidateSessionsSupportRollback(const std::vector<kubeforward::runtime::ManagedSession>& sessions,
                                     std::string& error) {
  for (const auto& session : sessions) {
    if (!ValidateSessionSupportsRollback(session, error)) {
      return false;
    }
  }

  error.clear();
  return true;
}

std::optional<std::string> ReadProcessCommandLine(int pid) {
  if (pid <= 0) {
    return std::nullopt;
  }

  std::array<char, 256> buffer {};
  std::string output;
  const std::string command = "ps -p " + std::to_string(pid) + " -o command=";
  FILE* pipe = ::popen(command.c_str(), "r");
  if (pipe == nullptr) {
    return std::nullopt;
  }

  while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
    output += buffer.data();
  }

  const int status = ::pclose(pipe);
  if (status != 0 || output.empty()) {
    return std::nullopt;
  }

  while (!output.empty() && (output.back() == '\n' || output.back() == '\r')) {
    output.pop_back();
  }
  if (output.empty()) {
    return std::nullopt;
  }

  return output;
}

bool ShouldSignalManagedProcess(const kubeforward::runtime::ManagedForwardProcess& process, std::string& reason) {
  if (UseNoopRunner()) {
    reason.clear();
    return true;
  }

  if (process.pid <= 0) {
    reason.clear();
    return true;
  }

  if (process.argv.empty()) {
    reason = "refusing to signal pid because runtime state is missing restart metadata (run 'down' after upgrading)";
    return false;
  }

  if (::kill(process.pid, 0) != 0 && errno == ESRCH) {
    reason.clear();
    return true;
  }

  const auto live_command = ReadProcessCommandLine(process.pid);
  if (!live_command.has_value()) {
    reason = "refusing to signal pid because process identity cannot be verified";
    return false;
  }

  const auto expected_binary = std::filesystem::path(process.argv.front()).filename().string();
  if (expected_binary.empty()) {
    reason.clear();
    return true;
  }

  const std::string expected_port_mapping = std::to_string(process.local_port) + ":" + std::to_string(process.remote_port);
  if (live_command->find(expected_binary) != std::string::npos &&
      live_command->find("port-forward") != std::string::npos &&
      live_command->find(expected_port_mapping) != std::string::npos) {
    reason.clear();
    return true;
  }

  reason = "refusing to signal pid because live command no longer matches managed forward signature";
  return false;
}

void StopSessionProcesses(const kubeforward::runtime::ManagedSession& session, kubeforward::runtime::ProcessRunner& runner,
                          const std::string& error_prefix, bool& stop_failed, int& stopped_processes) {
  for (const auto& process : session.forwards) {
    std::string identity_error;
    if (!ShouldSignalManagedProcess(process, identity_error)) {
      std::cerr << error_prefix << process.pid << ": " << identity_error << "\n";
      stop_failed = true;
      continue;
    }
    std::string stop_error;
    if (!runner.Stop(process.pid, stop_error)) {
      std::cerr << error_prefix << process.pid << ": " << stop_error << "\n";
      stop_failed = true;
      continue;
    }
    ++stopped_processes;
  }
}

void StopStartedSession(kubeforward::runtime::ManagedSession& session, kubeforward::runtime::ProcessRunner& runner) {
  bool stop_failed = false;
  int stopped_processes = 0;
  StopSessionProcesses(session, runner, "up: failed to stop pid ", stop_failed, stopped_processes);
  session.forwards.clear();
}

bool StartManagedSession(const std::string& normalized_config_path,
                         const kubeforward::runtime::ResolvedEnvironment& resolved_env, bool daemon,
                         const std::vector<PreparedForwardLaunch>& launches, kubeforward::runtime::ProcessRunner& runner,
                         kubeforward::runtime::ManagedSession& session, std::string& error) {
  session = MakeManagedSession(normalized_config_path, resolved_env, daemon);

  for (const auto& launch : launches) {
    std::string start_error;
    const auto started = runner.Start(launch.request, start_error);
    if (!started.has_value()) {
      error = "failed to start forward '" + launch.forward_name + "': " + start_error;
      StopStartedSession(session, runner);
      return false;
    }

    session.forwards.push_back(kubeforward::runtime::ManagedForwardProcess{
        .environment = resolved_env.name,
        .forward_name = launch.forward_name,
        .argv = launch.request.argv,
        .cwd = launch.request.cwd.string(),
        .log_path = launch.request.log_path.string(),
        .bind_address = ResolveBindAddress(launch.port),
        .local_port = launch.port.local_port,
        .remote_port = launch.port.remote_port,
        .protocol = launch.port.protocol,
        .pid = started->pid,
    });
  }

  error.clear();
  return true;
}

bool StartManagedSession(const kubeforward::runtime::ManagedSession& snapshot,
                         const std::vector<PreparedForwardLaunch>& launches, kubeforward::runtime::ProcessRunner& runner,
                         kubeforward::runtime::ManagedSession& session, std::string& error) {
  session = snapshot;
  session.forwards.clear();
  session.forwards.reserve(launches.size());

  for (size_t i = 0; i < launches.size(); ++i) {
    const auto& launch = launches[i];
    const auto& snapshot_forward = snapshot.forwards[i];

    std::string start_error;
    const auto started = runner.Start(launch.request, start_error);
    if (!started.has_value()) {
      error = "failed to restore forward '" + launch.forward_name + "': " + start_error;
      StopStartedSession(session, runner);
      return false;
    }

    auto restored_forward = snapshot_forward;
    restored_forward.pid = started->pid;
    restored_forward.argv = launch.request.argv;
    restored_forward.cwd = launch.request.cwd.string();
    restored_forward.log_path = launch.request.log_path.string();
    session.forwards.push_back(std::move(restored_forward));
  }

  error.clear();
  return true;
}

void RemoveMatchingSessions(kubeforward::runtime::RuntimeState& state, const std::string& normalized_config_path,
                            const std::string& environment) {
  state.sessions.erase(
      std::remove_if(state.sessions.begin(), state.sessions.end(),
                     [&](const kubeforward::runtime::ManagedSession& session) {
                       return session.config_path == normalized_config_path && session.environment == environment;
                     }),
      state.sessions.end());
}

bool RestoreReplacedSessions(const std::filesystem::path& state_path, const kubeforward::runtime::RuntimeState& original_state,
                             const std::vector<kubeforward::runtime::ManagedSession>& sessions_to_restore,
                             kubeforward::runtime::ProcessRunner& runner, std::string& error) {
  auto rollback_state = original_state;
  std::vector<kubeforward::runtime::ManagedSession> restored_sessions;
  for (const auto& snapshot : sessions_to_restore) {
    std::vector<PreparedForwardLaunch> launches;
    if (!BuildPreparedLaunchesFromSession(snapshot, launches, error)) {
      for (auto& restored : restored_sessions) {
        StopStartedSession(restored, runner);
      }
      return false;
    }

    kubeforward::runtime::ManagedSession restored_session;
    if (!StartManagedSession(snapshot, launches, runner, restored_session, error)) {
      for (auto& restored : restored_sessions) {
        StopStartedSession(restored, runner);
      }
      return false;
    }
    RemoveMatchingSessions(rollback_state, snapshot.config_path, snapshot.environment);
    rollback_state.sessions.push_back(restored_session);
    restored_sessions.push_back(std::move(restored_session));
  }

  std::string save_error;
  if (!kubeforward::runtime::SaveState(state_path, rollback_state, save_error)) {
    for (auto& restored : restored_sessions) {
      StopStartedSession(restored, runner);
    }
    error = "failed to restore previous session state: " + save_error;
    return false;
  }

  error.clear();
  return true;
}

struct ScopedSignalHandler {
  explicit ScopedSignalHandler(int signal_number) : signal_number_(signal_number) {
    struct sigaction action {};
    action.sa_handler = HandleForegroundSignal;
    sigemptyset(&action.sa_mask);
    action.sa_flags = 0;
    installed_ = (::sigaction(signal_number_, &action, &previous_) == 0);
  }

  ~ScopedSignalHandler() {
    if (installed_) {
      (void)::sigaction(signal_number_, &previous_, nullptr);
    }
  }

  int signal_number_;
  bool installed_ = false;
  struct sigaction previous_ {};
};

int ExitCodeFromSignal(int signal_number) {
  switch (signal_number) {
    case SIGINT:
      return 130;
    case SIGTERM:
      return 143;
    default:
      return 2;
  }
}

std::string DescribeWaitStatus(int status) {
  if (WIFEXITED(status)) {
    return "exit code " + std::to_string(WEXITSTATUS(status));
  }
  if (WIFSIGNALED(status)) {
    return "signal " + std::to_string(WTERMSIG(status));
  }
  return "unknown status";
}

struct ForegroundExitEvent {
  int pid = 0;
  int status = 0;
  std::string forward_name;
};

int RunForegroundSession(const std::filesystem::path& state_path, const kubeforward::runtime::RuntimeState& state_snapshot,
                         kubeforward::runtime::ManagedSession& session, kubeforward::runtime::ProcessRunner& runner) {
  g_foreground_signal = 0;
  ScopedSignalHandler sigint_handler(SIGINT);
  ScopedSignalHandler sigterm_handler(SIGTERM);

  const size_t total_forwards = session.forwards.size();
  const int poll_interval_ms = 100;
  std::vector<ForegroundExitEvent> exited_forwards;
  exited_forwards.reserve(total_forwards);
  std::set<int> exited_pids;
  while (g_foreground_signal == 0) {
    for (const auto& forward : session.forwards) {
      if (exited_pids.count(forward.pid) != 0) {
        continue;
      }
      int status = 0;
      const pid_t wait_result = ::waitpid(forward.pid, &status, WNOHANG);
      if (wait_result == forward.pid) {
        exited_pids.insert(forward.pid);
        exited_forwards.push_back(ForegroundExitEvent{
            .pid = forward.pid,
            .status = status,
            .forward_name = forward.forward_name,
        });
      }
    }
    if (!exited_forwards.empty()) {
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(poll_interval_ms));
  }

  StopStartedSession(session, runner);

  auto cleaned_state = state_snapshot;
  RemoveMatchingSessions(cleaned_state, session.config_path, session.environment);

  std::string save_error;
  if (!kubeforward::runtime::SaveState(state_path, cleaned_state, save_error)) {
    std::cerr << "up: failed to save runtime state '" << state_path.string() << "': " << save_error << "\n";
    return 2;
  }

  if (g_foreground_signal != 0) {
    return ExitCodeFromSignal(g_foreground_signal);
  }

  if (exited_forwards.size() == total_forwards) {
    bool all_succeeded = true;
    for (const auto& exited_forward : exited_forwards) {
      if (!WIFEXITED(exited_forward.status) || WEXITSTATUS(exited_forward.status) != 0) {
        all_succeeded = false;
        break;
      }
    }
    if (all_succeeded) {
      return 0;
    }
  }

  const auto& first_exited_forward = exited_forwards.front();
  if (exited_forwards.size() < total_forwards) {
    std::cerr << "up: foreground forward '" << first_exited_forward.forward_name
              << "' exited with " << DescribeWaitStatus(first_exited_forward.status)
              << " while other foreground forwards were still running\n";
    return 2;
  }

  if (WIFEXITED(first_exited_forward.status) && WEXITSTATUS(first_exited_forward.status) == 0) {
    return 0;
  }

  std::cerr << "up: foreground forward '" << first_exited_forward.forward_name
            << "' exited with " << DescribeWaitStatus(first_exited_forward.status) << "\n";
  return 2;
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
  int replaced_processes = 0;
  std::vector<kubeforward::runtime::ManagedSession> existing_sessions;
  for (const auto* session : MatchingSessions(state, normalized_config_path, resolved_env.name)) {
    existing_sessions.push_back(*session);
  }

  if (!existing_sessions.empty()) {
    std::string rollback_validation_error;
    if (!ValidateSessionsSupportRollback(existing_sessions, rollback_validation_error)) {
      std::cerr << "up: " << rollback_validation_error << "\n";
      return 2;
    }
  }

  std::vector<PreparedForwardLaunch> launches;
  std::string launch_error;
  if (!BuildPreparedLaunches(normalized_config_path, resolved_env, options.daemon, launches, launch_error)) {
    std::cerr << "up: " << launch_error << "\n";
    return 2;
  }

  if (!UseNoopRunner()) {
    std::string preflight_error;
    if (!kubeforward::runtime::CheckRuntimeSessionPortConflicts(state, normalized_config_path, resolved_env,
                                                                preflight_error)) {
      std::cerr << "up: preflight failed: " << preflight_error << "\n";
      return 2;
    }
    if (!ValidateKubectlExecutable(preflight_error)) {
      std::cerr << "up: preflight failed: " << preflight_error << "\n";
      return 2;
    }
    if (existing_sessions.empty() && !CheckPlanPortsAvailable(resolved_env, preflight_error)) {
      std::cerr << "up: preflight failed: " << preflight_error << "\n";
      return 2;
    }
  }

  if (!existing_sessions.empty()) {
    bool replace_stop_failed = false;
    for (const auto& existing_session : existing_sessions) {
      StopSessionProcesses(existing_session, *runner, "up: failed to stop replaced pid ", replace_stop_failed,
                           replaced_processes);
    }
    if (replace_stop_failed) {
      return 2;
    }

    if (!UseNoopRunner()) {
      std::string preflight_error;
      if (!CheckPlanPortsAvailable(resolved_env, preflight_error)) {
        std::string restore_error;
        if (!RestoreReplacedSessions(state_path, state, existing_sessions, *runner, restore_error)) {
          std::cerr << "up: preflight failed after stopping existing session: " << preflight_error << "\n";
          std::cerr << "up: rollback failed: " << restore_error << "\n";
          return 2;
        }
        std::cerr << "up: preflight failed after stopping existing session: " << preflight_error << "\n";
        std::cerr << "up: previous session was restored\n";
        return 2;
      }
    }
  }

  kubeforward::runtime::ManagedSession session;
  std::string start_error;
  if (!StartManagedSession(normalized_config_path, resolved_env, options.daemon, launches, *runner, session,
                           start_error)) {
    if (existing_sessions.empty()) {
      std::cerr << "up: " << start_error << "\n";
      return 2;
    }

    std::string restore_error;
    if (!RestoreReplacedSessions(state_path, state, existing_sessions, *runner, restore_error)) {
      std::cerr << "up: " << start_error << "\n";
      std::cerr << "up: rollback failed: " << restore_error << "\n";
      return 2;
    }

    std::cerr << "up: " << start_error << "\n";
    std::cerr << "up: previous session was restored\n";
    return 2;
  }

  auto next_state = state;
  RemoveMatchingSessions(next_state, normalized_config_path, resolved_env.name);
  next_state.sessions.push_back(session);

  std::string save_error;
  if (!kubeforward::runtime::SaveState(state_path, next_state, save_error)) {
    StopStartedSession(session, *runner);

    if (!existing_sessions.empty()) {
      std::string restore_error;
      if (!RestoreReplacedSessions(state_path, state, existing_sessions, *runner, restore_error)) {
        std::cerr << "up: failed to save runtime state '" << state_path.string() << "': " << save_error << "\n";
        std::cerr << "up: rollback failed: " << restore_error << "\n";
        return 2;
      }
      std::cerr << "up: failed to save runtime state '" << state_path.string() << "': " << save_error << "\n";
      std::cerr << "up: previous session was restored\n";
      return 2;
    }

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

  if (!options.daemon && !UseNoopRunner()) {
    return RunForegroundSession(state_path, next_state, session, *runner);
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
  const auto matched_sessions = MatchingSessions(state, normalized_config_path, options.env_filter);
  const size_t matched_session_count = matched_sessions.size();
  size_t matched_forwards = 0;
  std::set<std::string> matched_environments;
  std::map<std::string, size_t> matched_environment_forward_counts;
  for (const auto* session : matched_sessions) {
    const size_t forward_count = CountSessionForwards(*session);
    matched_forwards += forward_count;
    matched_environments.insert(session->environment);
    matched_environment_forward_counts[session->environment] += forward_count;
  }

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
      std::string identity_error;
      if (!ShouldSignalManagedProcess(process, identity_error)) {
        std::cerr << "down: skipped pid " << process.pid << ": " << identity_error << "\n";
        stop_failed = true;
        session_failed = true;
        continue;
      }
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
    std::cout << "  scope: environment\n";
    std::cout << "  env: " << options.env_filter << "\n";
    std::cout << "  forwards: " << matched_forwards << "\n";
    if (options.verbose) {
      std::cout << "  state: " << state_path.string() << "\n";
      std::cout << "  stopped: " << stopped_processes << "\n";
      std::cout << "  sessions: " << matched_session_count << "\n";
    }
    return 0;
  }

  std::cout << "  scope: all environments\n";
  std::cout << "  environments: " << matched_environments.size() << "\n";
  std::cout << "  forwards: " << matched_forwards << "\n";
  if (options.verbose) {
    std::cout << "  state: " << state_path.string() << "\n";
    std::cout << "  stopped: " << stopped_processes << "\n";
    std::cout << "  environment breakdown:\n";
    for (const auto& env_name : matched_environments) {
      const size_t env_forward_count = matched_environment_forward_counts[env_name];
      std::cout << "    - " << env_name << " (" << env_forward_count << " forward(s))\n";
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
  if (config.environments.empty()) {
    std::cout << "No environments defined in config.\n";
    return 0;
  }

  const auto plan_result =
      kubeforward::runtime::BuildResolvedPlan(config, config_path,
                                              env_filter.empty() ? std::optional<std::string>{}
                                                                 : std::optional<std::string>{env_filter});
  if (!plan_result.ok()) {
    std::cerr << "plan: failed to resolve execution plan.\n";
    for (const auto& error : plan_result.errors) {
      std::cerr << "  - " << error.context << ": " << error.message << "\n";
    }
    return 2;
  }

  if (verbose) {
    PrintVerboseHeader(config, config_path);
  }

  for (const auto& env : plan_result.plan->environments) {
    const auto& source_env = config.environments.at(env.name);
    if (verbose) {
      PrintPlanVerbose(source_env, env);
    } else {
      PrintPlanSummary(source_env, env);
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
