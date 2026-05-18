cmake_minimum_required(VERSION 3.16)

project(kubeforward VERSION 0.0.16 LANGUAGES CXX)

enable_testing()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT DEFINED KF_APP_VERSION OR KF_APP_VERSION STREQUAL "0.0.0")
  set(KF_APP_VERSION "${PROJECT_VERSION}" CACHE STRING "kubeforward CLI version string" FORCE)
else()
  set(KF_APP_VERSION "${KF_APP_VERSION}" CACHE STRING "kubeforward CLI version string")
endif()

find_package(cxxopts CONFIG REQUIRED)
find_package(yaml-cpp CONFIG REQUIRED)
find_package(Catch2 3 CONFIG REQUIRED)

add_library(kubeforward_lib
  src/cli/cli.cpp
  src/config/loader.cpp
  src/runtime/process_runner.cpp
  src/runtime/resolved_plan.cpp
  src/runtime/session_conflicts.cpp
  src/runtime/state_store.cpp
)
target_include_directories(kubeforward_lib PUBLIC include)
target_compile_definitions(kubeforward_lib PUBLIC KF_APP_VERSION=\"${KF_APP_VERSION}\")
target_link_libraries(kubeforward_lib PUBLIC cxxopts::cxxopts yaml-cpp::yaml-cpp)

add_executable(kubeforward
  cmd/kubeforward/main.cpp
)
target_link_libraries(kubeforward PRIVATE kubeforward_lib)

add_executable(kubeforward_tests
  tests/cli_plan_tests.cpp
  tests/config_loader_tests.cpp
  tests/runtime_process_runner_tests.cpp
  tests/runtime_resolved_plan_tests.cpp
  tests/runtime_session_conflicts_tests.cpp
  tests/runtime_state_store_tests.cpp
)
target_link_libraries(kubeforward_tests PRIVATE kubeforward_lib Catch2::Catch2WithMain)
target_compile_definitions(kubeforward_tests PRIVATE KF_SOURCE_DIR=\"${CMAKE_SOURCE_DIR}\")

include(Catch)
catch_discover_tests(kubeforward_tests)
