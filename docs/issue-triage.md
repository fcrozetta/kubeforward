# Issue Triage Conventions

This document is maintainer-facing and intentionally not part of the user-facing README flow.

## Label Taxonomy (Normalized)

Use prefixed labels consistently:

- `type:*` for issue class
- `area:*` for subsystem ownership
- `priority:*` for urgency

Current baseline:

- `type:bug`
- `type:feature`
- `type:support`
- `area:cli`
- `area:config`
- `area:port-forward`
- `priority:p1`
- `priority:p2`
- `priority:p3`

## Why Prefixes

- Avoids collisions with GitHub defaults (`bug`, `enhancement`, `question`).
- Keeps Projects filters and automation deterministic.
- Prevents taxonomy drift when new labels are added.

## Template Coupling

Issue forms in `.github/ISSUE_TEMPLATE/` must apply `type:*` labels only:

- `bug_report.yml` -> `type:bug`
- `feature_request.yml` -> `type:feature`
- `support_request.yml` -> `type:support`

If label names change, update templates in the same PR.

## Projects Integration Note

When Projects is configured, views and automation should use:

- grouping/filtering by `type:*`
- prioritization by `priority:*`
- optional swimlanes/filters by `area:*`

Do not mix prefixed and unprefixed type labels.
