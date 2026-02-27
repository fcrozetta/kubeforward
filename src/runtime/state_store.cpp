#include "kubeforward/runtime/state_store.h"

#include <yaml-cpp/yaml.h>

#include <cerrno>
#include <cstring>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <sstream>

#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>

namespace kubeforward::runtime {
namespace {

std::string NormalizeConfigPath(const std::string& config_path) {
  std::error_code ec;
  const auto absolute_path = std::filesystem::absolute(config_path, ec);
  if (ec) {
    return config_path;
  }
  return absolute_path.string();
}

YAML::Node SerializeState(const RuntimeState& state) {
  YAML::Node root;
  YAML::Node sessions(YAML::NodeType::Sequence);
  for (const auto& session : state.sessions) {
    YAML::Node session_node;
    session_node["id"] = session.id;
    session_node["configPath"] = session.config_path;
    session_node["environment"] = session.environment;
    session_node["daemon"] = session.daemon;
    session_node["startedAtUtc"] = session.started_at_utc;

    YAML::Node forwards(YAML::NodeType::Sequence);
    for (const auto& forward : session.forwards) {
      YAML::Node forward_node;
      forward_node["environment"] = forward.environment;
      forward_node["name"] = forward.forward_name;
      forward_node["localPort"] = forward.local_port;
      forward_node["remotePort"] = forward.remote_port;
      forward_node["pid"] = forward.pid;
      forwards.push_back(forward_node);
    }
    session_node["forwards"] = forwards;
    sessions.push_back(session_node);
  }
  root["sessions"] = sessions;
  return root;
}

void AddStateError(std::vector<std::string>& errors, const std::string& context, const std::string& message) {
  errors.push_back(context + ": " + message);
}

RuntimeState ParseStateNode(const YAML::Node& root, std::vector<std::string>& errors) {
  RuntimeState state;
  if (!root) {
    return state;
  }
  if (!root.IsMap()) {
    AddStateError(errors, "root", "expected mapping");
    return state;
  }

  const auto sessions = root["sessions"];
  if (!sessions) {
    return state;
  }
  if (!sessions.IsSequence()) {
    AddStateError(errors, "sessions", "expected list");
    return state;
  }

  state.sessions.reserve(sessions.size());
  for (size_t i = 0; i < sessions.size(); ++i) {
    const auto node = sessions[i];
    const std::string context = "sessions[" + std::to_string(i) + "]";
    if (!node.IsMap()) {
      AddStateError(errors, context, "expected mapping");
      continue;
    }

    ManagedSession session;
    try {
      session.id = node["id"] ? node["id"].as<std::string>() : "";
      session.config_path = node["configPath"] ? node["configPath"].as<std::string>() : "";
      session.environment = node["environment"] ? node["environment"].as<std::string>() : "";
      session.daemon = node["daemon"] ? node["daemon"].as<bool>() : false;
      session.started_at_utc = node["startedAtUtc"] ? node["startedAtUtc"].as<std::string>() : "";
    } catch (const YAML::BadConversion&) {
      AddStateError(errors, context, "invalid scalar type");
      continue;
    }

    const auto forwards = node["forwards"];
    if (forwards && !forwards.IsSequence()) {
      AddStateError(errors, context + ".forwards", "expected list");
      continue;
    }
    if (forwards) {
      session.forwards.reserve(forwards.size());
      for (size_t j = 0; j < forwards.size(); ++j) {
        const auto forward_node = forwards[j];
        const std::string forward_context = context + ".forwards[" + std::to_string(j) + "]";
        if (!forward_node.IsMap()) {
          AddStateError(errors, forward_context, "expected mapping");
          continue;
        }

        ManagedForwardProcess forward;
        try {
          forward.environment = forward_node["environment"] ? forward_node["environment"].as<std::string>() : "";
          forward.forward_name = forward_node["name"] ? forward_node["name"].as<std::string>() : "";
          forward.local_port = forward_node["localPort"] ? forward_node["localPort"].as<int>() : 0;
          forward.remote_port = forward_node["remotePort"] ? forward_node["remotePort"].as<int>() : 0;
          forward.pid = forward_node["pid"] ? forward_node["pid"].as<int>() : 0;
        } catch (const YAML::BadConversion&) {
          AddStateError(errors, forward_context, "invalid scalar type");
          continue;
        }
        session.forwards.push_back(forward);
      }
    }

    state.sessions.push_back(session);
  }
  return state;
}

std::filesystem::path StateLockPath(const std::filesystem::path& path) { return path.string() + ".lock"; }

int OpenAndLockStateFile(const std::filesystem::path& path, int lock_mode, std::string& error) {
  const auto lock_path = StateLockPath(path);
  const int lock_fd = ::open(lock_path.c_str(), O_CREAT | O_RDWR, 0644);
  if (lock_fd < 0) {
    error = "failed to open state lock file: " + std::string(std::strerror(errno));
    return -1;
  }
  if (::flock(lock_fd, lock_mode) != 0) {
    error = "failed to lock state file: " + std::string(std::strerror(errno));
    ::close(lock_fd);
    return -1;
  }
  error.clear();
  return lock_fd;
}

std::filesystem::path BuildTemporaryStatePath(const std::filesystem::path& path) {
  std::ostringstream suffix;
  suffix << ".tmp." << ::getpid();
  return path.string() + suffix.str();
}

}  // namespace

std::filesystem::path DefaultStatePathForConfig(const std::string& config_path) {
  if (const char* override_path = std::getenv("KUBEFORWARD_STATE_FILE")) {
    if (override_path[0] != '\0') {
      return std::filesystem::path(override_path);
    }
  }

  const std::string normalized = NormalizeConfigPath(config_path);
  const size_t hash = std::hash<std::string>{}(normalized);
  const auto base_dir = std::filesystem::temp_directory_path() / "kubeforward";
  return base_dir / ("state-" + std::to_string(hash) + ".yaml");
}

StateLoadResult LoadState(const std::filesystem::path& path) {
  StateLoadResult result;
  const auto parent = path.parent_path();
  if (!parent.empty()) {
    std::error_code parent_ec;
    const bool parent_exists = std::filesystem::exists(parent, parent_ec);
    if (parent_ec || !parent_exists) {
      return result;
    }
  }

  std::string lock_error;
  const int lock_fd = OpenAndLockStateFile(path, LOCK_SH, lock_error);
  if (lock_fd < 0) {
    result.errors.push_back(lock_error);
    return result;
  }

  std::ifstream input(path);
  if (!input.is_open()) {
    ::close(lock_fd);
    return result;
  }

  std::stringstream buffer;
  buffer << input.rdbuf();
  try {
    const YAML::Node root = YAML::Load(buffer.str());
    result.state = ParseStateNode(root, result.errors);
  } catch (const YAML::ParserException& ex) {
    result.errors.push_back(std::string("state parse error: ") + ex.what());
  }
  ::close(lock_fd);
  return result;
}

bool SaveState(const std::filesystem::path& path, const RuntimeState& state, std::string& error) {
  std::error_code ec;
  std::filesystem::create_directories(path.parent_path(), ec);
  if (ec) {
    error = "failed to create state directory: " + ec.message();
    return false;
  }

  const int lock_fd = OpenAndLockStateFile(path, LOCK_EX, error);
  if (lock_fd < 0) {
    return false;
  }

  const auto tmp_path = BuildTemporaryStatePath(path);
  std::ofstream out(tmp_path, std::ios::trunc);
  if (!out.is_open()) {
    error = "failed to open temporary state file for writing";
    ::close(lock_fd);
    return false;
  }
  out << SerializeState(state);
  out.flush();
  if (!out.good()) {
    error = "failed to flush temporary state file";
    out.close();
    std::error_code remove_ec;
    std::filesystem::remove(tmp_path, remove_ec);
    ::close(lock_fd);
    return false;
  }
  out.close();

  std::error_code rename_ec;
  std::filesystem::rename(tmp_path, path, rename_ec);
  if (rename_ec) {
    error = "failed to replace state file atomically: " + rename_ec.message();
    std::error_code remove_ec;
    std::filesystem::remove(tmp_path, remove_ec);
    ::close(lock_fd);
    return false;
  }

  ::close(lock_fd);
  error.clear();
  return true;
}

}  // namespace kubeforward::runtime
