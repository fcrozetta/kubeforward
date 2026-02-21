#pragma once

#include <string>
#include <vector>

namespace kubeforward {

// Entry point for the kubeforward CLI. Args are passed as UTF-8 strings.
int run_cli(const std::vector<std::string>& args);

}  // namespace kubeforward

