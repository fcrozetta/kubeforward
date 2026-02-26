#pragma once

#include <optional>
#include <string>
#include <vector>

#include "kubeforward/config/types.h"

namespace kubeforward::config {

struct ConfigLoadError {
  std::string context;
  std::string message;
};

struct ConfigLoadResult {
  std::optional<Config> config;
  std::vector<ConfigLoadError> errors;

  bool ok() const { return config.has_value() && errors.empty(); }
};

ConfigLoadResult LoadConfigFromFile(const std::string& path);

}  // namespace kubeforward::config

