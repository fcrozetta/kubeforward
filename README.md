# kubeforward

Kubeforward is a macOS-first CLI for config-driven Kubernetes port-forward workflows.

Current implementation status: config loading/validation and `plan` output are implemented; forward execution is not implemented yet.

## Quick Start

```bash
kubeforward help
kubeforward plan --config kubeforward.yaml
kubeforward plan --config kubeforward.yaml --env dev
```

- `help` shows global command usage.
- `plan` validates config and renders a normalized environment/forward summary.

## Minimal Config

Create `kubeforward.yaml` at repo root:

```yaml
version: 1
metadata:
  project: demo-project
defaults:
  namespace: default
  bindAddress: 127.0.0.1
environments:
  dev:
    forwards:
      - name: api
        resource:
          kind: deployment
          name: api
        ports:
          - local: 7000
            remote: 7000
```

Then run:

```bash
kubeforward plan --env dev
```

## Command Reference

- `kubeforward help`
- `kubeforward plan [--config <path>] [--env <name>] [--format text]`

Notes:
- Unknown environments fail fast.
- Schema errors are reported with contextual paths.
- Duplicate local ports within an environment are rejected.

## Config Reference

Full schema and validation details: [`docs/config-schema.md`](docs/config-schema.md)

## Maintainer Docs

Build/test/release/maintenance workflows live in [`docs/maintainer.md`](docs/maintainer.md).
