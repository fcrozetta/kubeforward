#pragma once

#include <string>
#include <vector>

namespace kubeforward {

/// Runs the kubeforward CLI dispatcher.
///
/// `args` must follow argv conventions where `args[0]` is the executable name.
/// Returns a process-style exit code (0 for success, non-zero for failure).
int run_cli(const std::vector<std::string>& args);

}  // namespace kubeforward
