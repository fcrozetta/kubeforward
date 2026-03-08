#pragma once

#include <optional>
#include <string>
#include <vector>

#include "kubeforward/config/types.h"

namespace kubeforward::config {

/// A single validation or loading error found while parsing config input.
struct ConfigLoadError {
  /// Dot-separated path that identifies where the error occurred.
  std::string context;
  /// Human-readable description of the failure.
  std::string message;
};

/// Result for config loading with partial diagnostics.
///
/// `config` is set only when parsing/validation produced a usable config model.
/// `errors` may contain warnings/failures gathered during parsing.
struct ConfigLoadResult {
  std::optional<Config> config;
  std::vector<ConfigLoadError> errors;

  /// True when a usable config exists and no errors were recorded.
  bool ok() const { return config.has_value() && errors.empty(); }
};

/// Loads and validates a kubeforward config file from disk.
///
/// Supports YAML and JSON input with the same schema contract.
/// On failure, `errors` contains deterministic validation details.
ConfigLoadResult LoadConfigFromFile(const std::string& path);

}  // namespace kubeforward::config
