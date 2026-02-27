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
make build BUILD_TYPE=Release BUILD_TARGET=kubeforward CMAKE_FLAGS="-DKF_APP_VERSION=1.2.3"
```

### Using CMake directly

```bash
cmake -S . -B build/Debug \
  -DCMAKE_BUILD_TYPE=Debug \
  -DKF_APP_VERSION=0.0.0 \
  -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
cmake --build build/Debug
```

## Usage

```bash
./build/Debug/kubeforward help
./build/Debug/kubeforward --version
./build/Debug/kubeforward -e dev
./build/Debug/kubeforward -v
./build/Debug/kubeforward plan --env dev
./build/Debug/kubeforward up -f ./kubeforward.yaml -e dev
./build/Debug/kubeforward down -f ./kubeforward.yaml -d
./build/Debug/kubeforward plan -f ./configs/staging.yaml -e staging -v
```

- `help` prints the global command list.
- `--version` prints the app version embedded at build time (defaults to `0.0.0`).
- Running `kubeforward` without a subcommand defaults to `plan`.
- `plan`, `up`, and `down` are mutually exclusive subcommands.
- `plan` loads `kubeforward.yaml` from the current directory by default and lists all environments.
- `--env`/`-e` filters to a single environment.
- `--verbose`/`-v` prints full plan details (defaults + environment + forward fields).

## Configuration

See `docs/config-schema.md` for the canonical `kubeforward.yaml` format, environment overrides, and validation semantics.

## IDE Support

See `docs/vscode.md` for a step-by-step VS Code setup (extensions, CMake/vcpkg integration, and debugging via CodeLLDB).

## Tests

Kubeforward uses [Catch2](https://github.com/catchorg/Catch2) with CTest. After configuring/building, run:

```bash
make test
```

to execute the normal unit/runtime test suite.

For runtime smoke validation against a real Kubernetes API, run:

```bash
make test-e2e
```

This builds the CLI and runs the `kind` smoke flow in `tests/kind_smoke.sh`.

To run both the normal suite and the `kind` smoke flow in one command, run:

```bash
make test KIND_SMOKE=1
```

`make test-e2e` requires local Docker, `kind`, and `kubectl`.
GitHub Releases are gated by the same smoke flow on `ubuntu-latest` before the macOS packaging/signing jobs run.

## Commands

- `help`: prints usage and supported commands.
- `--version`: prints the embedded app version.
- `plan`: validates the config and prints a summary of each environment. Options:
  - `-f, --file <path>`: alternate path to config file (default `./kubeforward.yaml`).
  - `-e, --env <name>`: environment to display (optional filter).
  - `-v, --verbose`: print detailed fields instead of summary.
- `up`: starts forwards for one environment (defaults to first environment when `--env` is omitted). Options:
  - `-f, --file <path>`: alternate path to config file.
  - `-e, --env <name>`: environment to start.
  - `-d, --daemon`: daemon mode (logs hidden).
  - `-v, --verbose`: print detailed command output.
- `down`: stops forwards for one environment or all environments. Options:
  - `-f, --file <path>`: alternate path to config file.
  - `-e, --env <name>`: environment to stop; if omitted, all environments are targeted.
  - `-d, --daemon`: daemon mode (logs hidden).
  - `-v, --verbose`: print detailed command output.
