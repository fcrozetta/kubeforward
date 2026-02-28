#include "kubeforward/runtime/session_conflicts.h"

#include <cerrno>
#include <csignal>
#include <set>
#include <sstream>
#include <string>
#include <tuple>

namespace {

std::string ResolveBindAddress(const kubeforward::config::PortMapping& port) {
  if (port.bind_address.has_value() && !port.bind_address->empty()) {
    return *port.bind_address;
  }
  return "127.0.0.1";
}

std::string ResolveBindAddress(const kubeforward::runtime::ManagedForwardProcess& process) {
  if (!process.bind_address.empty()) {
    return process.bind_address;
  }
  return "127.0.0.1";
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

using PortClaimKey = std::tuple<std::string, int, kubeforward::config::PortProtocol>;

PortClaimKey PortClaimFor(const kubeforward::config::PortMapping& port) {
  return {ResolveBindAddress(port), port.local_port, port.protocol};
}

PortClaimKey PortClaimFor(const kubeforward::runtime::ManagedForwardProcess& process) {
  return {ResolveBindAddress(process), process.local_port, process.protocol};
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

}  // namespace

namespace kubeforward::runtime {

bool CheckRuntimeSessionPortConflicts(const RuntimeState& state, const std::string& normalized_config_path,
                                      const ResolvedEnvironment& target_env, std::string& error) {
  std::set<PortClaimKey> target_ports;
  for (const auto& forward : target_env.forwards) {
    for (const auto& port : forward.ports) {
      target_ports.insert(PortClaimFor(port));
    }
  }

  for (const auto& session : state.sessions) {
    if (session.config_path == normalized_config_path && session.environment == target_env.name) {
      continue;
    }
    for (const auto& process : session.forwards) {
      if (target_ports.count(PortClaimFor(process)) == 0) {
        continue;
      }
      if (!IsPidAlive(process.pid)) {
        continue;
      }
      std::ostringstream oss;
      oss << "local "
          << PortProtocolToString(process.protocol)
          << " port "
          << process.local_port
          << " on "
          << ResolveBindAddress(process)
          << " is already claimed by running session '"
          << session.id
          << "'";
      error = oss.str();
      return false;
    }
  }

  error.clear();
  return true;
}

}  // namespace kubeforward::runtime
