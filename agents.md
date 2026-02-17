# Kubeforward — Agent & Contributor Guide

This repository is being transitioned from an initial Go prototype to a C++ implementation.

## Project intent

Kubeforward is a macOS-first CLI that manages Kubernetes port-forwards based on a repo-level config file
and provides safe handling for production contexts.

**Primary goals**
- Deterministic CLI behavior and clean failure modes (especially port collisions).
- Config-driven port-forward sets with explicit environment support (DTAPS).
- macOS-first packaging and distribution via GitHub Releases.

**Non-goals (for now)**
- No GUI app.
- No daemon-heavy architecture beyond what is required for detached operation.
- No “smart” retries on port collisions (fail fast with a clear message).

## Implementation direction

**Language:** C++ (modern standard, e.g., C++20 unless otherwise decided)  
**Package manager:** vcpkg  
**Build system:** CMake (preferred when using vcpkg), unless later replaced explicitly.

macOS is the first-class target. Linux/Windows may come later.

## Repository conventions

### Directory layout (target)
- `src/` — core implementation
- `include/` — public headers
- `apps/` or `cmd/` — CLI entrypoint(s)
- `tests/` — unit/integration tests
- `.github/workflows/` — CI

Do not add new Go code. Existing Go code remains until removed in a dedicated migration PR.

### Coding standards
- Prefer explicit error handling with typed results over exceptions crossing module boundaries.
- Keep modules small and dependency-light.
- Output should be stable and scriptable (avoid “cute” output unless gated behind a flag).

### CLI stability
- Flags and output formats are part of the interface; do not change them casually.
- Any breaking change must be called out in the PR description and release notes.

## CI/CD expectations

GitHub Actions is the source of truth for builds and releases.

**macOS builds first:**
- Build on the latest macOS runner.
- Produce a versioned artifact for Releases.

### Signing & notarization policy (macOS)
For public GitHub Releases intended for other users:
- Plan to implement Developer ID code-signing and notarization to avoid Gatekeeper friction.

For internal/dev artifacts:
- Unsigned builds are acceptable.

Do not introduce signing/notarization steps until the release packaging format is decided and secrets are
stored correctly (certificate, keychain handling, notarization credentials).

## vcpkg expectations
- Use a pinned vcpkg baseline (commit hash) to ensure reproducible builds.
- Prefer manifest mode (`vcpkg.json`) checked into the repo.
- Avoid unpinned or floating dependency sources.

## How agents should work in this repo

When making changes:
1. Keep PRs small and single-purpose.
2. Add/update tests when behavior changes.
3. Update docs when flags, config, or output changes.
4. Prefer clarity over cleverness.

If you are unsure about a behavior change, default to conservative, explicit behavior and document it.
