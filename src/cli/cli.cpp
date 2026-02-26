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

int RunPlanCommand(const std::vector<std::string>& args) {
  bool show_help = false;
  std::string config_path = "kubeforward.yaml";
  std::string env_filter;
  std::string format = "text";

  cxxopts::Options options(args.front(), "Render the normalized port-forward plan from kubeforward.yaml.");
  options.add_options()
      ("h,help", "Show help for plan command", cxxopts::value<bool>(show_help)->default_value("false"))
      ("config", "Path to kubeforward config file",
          cxxopts::value<std::string>(config_path)->default_value("kubeforward.yaml"))
      ("env", "Environment to display (defaults to all)", cxxopts::value<std::string>(env_filter))
      ("format", "Output format: text | json (defaults to text)",
          cxxopts::value<std::string>(format)->default_value("text"));

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

  if (format != "text") {
    std::cerr << "plan: unsupported format '" << format << "', only 'text' is available right now.\n";
    return 2;
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

  if (environments.empty()) {
    std::cout << "No environments defined in config.\n";
    return 0;
  }

  for (const auto& [name, env] : environments) {
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

  return 0;
}

}  // namespace

namespace kubeforward {

int run_cli(const std::vector<std::string>& args) {
  if (args.size() < 2) {
    PrintGeneralHelp();
    return 1;
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

  std::cerr << "Unknown command '" << command << "'.\n\n";
  PrintGeneralHelp();
  return 1;
}

}  // namespace kubeforward
