#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace kubeforward::runtime {

//! Runtime process metadata for one forwarded local port.
struct ManagedForwardProcess {
  std::string environment;
  std::string forward_name;
  int local_port = 0;
  int remote_port = 0;
  int pid = 0;
};

//! Runtime session persisted by `up` and consumed by `down`.
struct ManagedSession {
  std::string id;
  std::string config_path;
  std::string environment;
  bool daemon = false;
  std::string started_at_utc;
  std::vector<ManagedForwardProcess> forwards;
};

//! Persisted state file model.
struct RuntimeState {
  std::vector<ManagedSession> sessions;
};

//! Load operation result for runtime state.
struct StateLoadResult {
  RuntimeState state;
  std::vector<std::string> errors;

  bool ok() const { return errors.empty(); }
};

//! Returns the default state file path associated with a config path.
std::filesystem::path DefaultStatePathForConfig(const std::string& config_path);

//! Reads runtime state from disk. Missing file is treated as empty state.
StateLoadResult LoadState(const std::filesystem::path& path);

//! Writes runtime state to disk atomically (parent directory created when missing).
bool SaveState(const std::filesystem::path& path, const RuntimeState& state, std::string& error);

}  // namespace kubeforward::runtime
