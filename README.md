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
VCPKG_ROOT=/path/to/vcpkg make build
```

### Using CMake directly

```bash
cmake -S . -B build \
  -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
cmake --build build
```

## Usage

```bash
./build/kubeforward -h
./build/kubeforward --flag1 --flag2 demo
```

## Supported flags

- `-h` shows the help message.
- `--flag1` is a boolean flag and is set to `true` when provided.
- `--flag2 <value>` accepts a required value.

The CLI prints a summary of parsed flags after execution.
