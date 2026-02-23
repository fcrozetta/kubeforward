# Kubeforward Config Schema (v1)

## Format & Location
- Default file name: `kubeforward.yaml` in the current working directory. Optional `.json` variant for automation, but YAML is the canonical authoring format.
- UTF-8 only, deterministic ordering to keep diffs clean.
- Single document per repo. Nested `includes` are forbidden to avoid non-deterministic evaluation.

## Top-Level Structure
```yaml
version: 1
metadata:
  project: string                # required, human label
  owner: string                  # optional pager/Slack hook
defaults:
  kubeconfig: string?            # path relative to repo
  context: string?               # kubectl context name
  namespace: string?             # cluster namespace
  bindAddress: string?           # default listen IPv4 (default 127.0.0.1)
  labels: map<string,string>?    # labels applied to every forward plan
environments:
  <name>:
    extends: string?             # optional parent env for DTAPS reuse
    description: string?
    kubeconfig/context/namespace/bindAddress/labels: overrides
    guards:
      allowProduction: bool?     # explicit opt-in for prod usage
    forwards:
      - name: string (required, unique per env)
        resource:
          kind: enum[pod, deployment, service, statefulset]
          name: string                      # mutually exclusive with selector
          selector: map<string,string>?     # mutually exclusive with name
          namespace: string?                # overrides env/default namespace
        container: string?                  # for pods w/ multiple containers
        ports:
          - local: int (required, 1-65535)
            remote: int (required, 1-65535)
            bindAddress: string?            # overrides env/default (IPv4 literal)
            protocol: enum[tcp, udp] default tcp
        annotations:
          detach: bool default false
          restartPolicy: enum[fail-fast, replace]
          healthCheck:
            exec: [string]?                 # command run locally post-bind
            timeoutMs: int?
        env: map<string,string>?            # interpolated into exec hooks
```

## Resolution & Overrides
1. `defaults` apply to every environment unless explicitly overridden.
2. `extends` performs a shallow merge (metadata excluded). Lists are replaced, not merged, to keep order deterministic.
3. Namespace resolution order: port-forward namespace → environment namespace → defaults namespace → error if unset.
4. Local port collisions are forbidden post-merge. When duplicates appear, validation fails with both forward names and env.

## Validation Rules
- Unknown keys anywhere cause failure; no silent drops.
- `version` must equal `1`. Future versions will be backward incompatible and gated via CLI flag.
- `name` uniqueness enforced within each environment and across entire file (global collisions block to prevent human confusion).
- `resource.name` and `resource.selector` are mutually exclusive; at least one must be present.
- `selector` maps must contain deterministic key ordering when serialized (CLI rewrites sorted order on `plan` output).
- `bindAddress` must be an IPv4 literal. Hostnames rejected to avoid implicit DNS dependencies.
- Production environments (`guards.allowProduction=true`) require every forward to specify `annotations.detach=true` to enforce detached supervision.
- `healthCheck.exec` commands are validated for absolute paths or repo-relative scripts; bare names rejected.

## Error Surfaces
- Missing file → exit 2 with guidance to add `kubeforward.yaml`.
- YAML syntax errors → exit 2 with line/col from parser.
- Semantic errors → exit 3 with deterministic list of violations, sorted by environment then forward.

## Compatibility Guarantees
- CLI accepts `-f` / `--file <path>` to override lookup but still requires schema v1.
- CLI accepts `-e` / `--env <name>` as an optional filter over loaded environments.
- CLI accepts `-v` / `--verbose` to render full field-level plan details.
- JSON variant is accepted via `-f` / `--file` but must conform to the same field names and casing.

## Future Evolution Hooks
- Reserved top-level key `extensions` (map<string,any>) for experimental modules; ignored unless `--enable-extension=<name>` flag provided.
- `annotations` bucket is extensible; unknown annotations are merely surfaced in plan output, never enforced.
