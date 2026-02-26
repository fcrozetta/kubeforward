---
name: github-format-layout
description: Apply one GitHub writing policy across releases, pull requests, and issues with user-facing highlights, sparse alert callouts, optional top TOC blocks, and mermaid diagrams when helpful.
---

# GitHub Format Layout

Use this skill when designing, drafting, or reviewing content for:
- GitHub Releases
- Pull Requests
- Issues

## Scope

- Target: body formatting and section layout.
- Output: markdown that renders cleanly in GitHub UI.
- Non-goal: CI, packaging, or signing implementation.

## Workflow

1. Pick surface
- `release`: user-facing shipped changes.
- `pr`: reviewer-focused change proposal.
- `issue`: problem/proposal tracking.

2. Pick template
- `assets/release-template.md`
- `assets/pr-template.md`
- `assets/issue-template.md`

3. Apply shared formatting policy
- Include a user-facing `Highlights` section for releases and PRs.
- Alerts are visual anchors; use sparingly and never as decoration.
- No emojis.
- For long bodies, place a TOC in a top `<details><summary>...</summary></details>` block.
- Use fenced code blocks with explicit language tags.
- Use mermaid diagrams only when they clarify behavior/concepts.

4. Validate before publish
- Check links and anchors.
- Keep headings stable and predictable.
- Verify alerts are not overused and do not interrupt dense tables/lists.

## Output Rules

- Prefer concise, deterministic headings.
- Use impact-based grouping over raw changelog dumps.
- Keep one canonical “how to use/activate” path in release notes.
- Avoid HTML-heavy formatting except the TOC `details/summary` block.

## Files

- Templates:
  - `assets/release-template.md`
  - `assets/pr-template.md`
  - `assets/issue-template.md`
- Capabilities reference: `references/github-rendering-capabilities.md`
