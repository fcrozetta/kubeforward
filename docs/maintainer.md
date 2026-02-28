# Maintainer Guide

This document is maintainer-facing. User-facing usage docs stay in [`README.md`](../README.md).

## Documentation Split

- `README.md`: user-facing entrypoint (what the tool does, commands, config usage).
- `docs/maintainer.md` and other files in `docs/`: maintainer/contributor workflows.
- If a change affects both audiences, update both in the same PR.

## Scope

Use this guide for:
- local build/test workflows
- contributor expectations for code/docs changes
- release/packaging guardrails

## Toolchain Requirements

- CMake 3.16+
- C++20-compatible compiler
- vcpkg with `VCPKG_ROOT` exported

Dependency resolution is pinned via `vcpkg.json` `builtin-baseline`. Do not introduce floating dependency sources.

## Build

Preferred path:

```bash
export VCPKG_ROOT=/path/to/vcpkg
make build
make build BUILD_TYPE=Release
make build BUILD_TYPE=Release BUILD_TARGET=kubeforward
```

Direct CMake path:

```bash
cmake -S . -B build/Debug \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
cmake --build build/Debug
```

## Test

```bash
ctest --test-dir build/Debug
```

Catch2 test sources live in `tests/`.

## Change Rules

- Keep PRs small and single-purpose.
- Add/update tests when behavior changes.
- Update docs when flags, config, or output changes.
- CLI flags/output are interface; breaking changes require explicit callout in PR/release notes.
- Do not add new Go code; ongoing implementation direction is C++.

## Release and Packaging

- GitHub Actions is source of truth for CI/CD.
- macOS builds/artifacts are first priority.
- Public releases should eventually use Developer ID signing + notarization.
- Do not add signing/notarization steps before packaging format and secrets handling are finalized.

## Related Maintainer Docs

- VS Code workflow: [`docs/vscode.md`](vscode.md)
- Issue taxonomy/triage: [`docs/issue-triage.md`](issue-triage.md)
