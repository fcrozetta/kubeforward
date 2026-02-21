# kubeforward

Kubeforward is a C++ CLI project built with CMake and dependencies managed through vcpkg (manifest mode).

## Requirements

- CMake 3.16+
- A C++20-compatible compiler
- vcpkg (set `VCPKG_ROOT`)

## Dependency management (vcpkg)

This repository uses `vcpkg.json` with a pinned `builtin-baseline` to keep dependency resolution reproducible.

## Build

### Using Make (recommended)

```bash
export VCPKG_ROOT=/path/to/vcpkg
make build
make build BUILD_TYPE=Release
make build BUILD_TYPE=Release BUILD_TARGET=kubeforward
```

### Using CMake directly

```bash
cmake -S . -B build/Debug \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
cmake --build build/Debug
```

## Usage

```bash
./build/Debug/kubeforward help
./build/Debug/kubeforward plan --config kubeforward.yaml --env dev
```

- `help` prints the global command list.
- `plan` loads `kubeforward.yaml`, validates it, and lists the forwards for the selected environment (or all environments when `--env` is omitted).

## Configuration

See `docs/config-schema.md` for the canonical `kubeforward.yaml` format, environment overrides, and validation semantics.

## IDE Support

See `docs/vscode.md` for a step-by-step VS Code setup (extensions, CMake/vcpkg integration, and debugging via CodeLLDB).

## Tests

Kubeforward uses [Catch2](https://github.com/catchorg/Catch2) with CTest. After configuring/building, run:

```bash
ctest --test-dir build/Debug
```

to execute all suites (config loader + CLI plan). Add new `TEST_CASE`s under `tests/`.

## Commands

- `help`: prints usage and supported commands.
- `plan`: validates the config and prints a summary of each environment. Options:
  - `--config <path>`: alternate path to `kubeforward.yaml`.
  - `--env <name>`: limit output to a single environment.
