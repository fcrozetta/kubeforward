#pragma once

#include <string>

#include "kubeforward/runtime/resolved_plan.h"
#include "kubeforward/runtime/state_store.h"

namespace kubeforward::runtime {

//! Verifies that no live runtime session already claims the same local socket tuple for another environment/session.
bool CheckRuntimeSessionPortConflicts(const RuntimeState& state, const std::string& normalized_config_path,
                                      const ResolvedEnvironment& target_env, std::string& error);

}  // namespace kubeforward::runtime
