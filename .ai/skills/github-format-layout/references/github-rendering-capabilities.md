# GitHub Rendering Capabilities

This file captures what we can reliably use across Releases, PRs, and Issues.

## Confirmed from GitHub docs

- GitHub supports markdown formatting in comments/issues/PRs and related surfaces.
- Releases support markdown descriptions, `@mentions`, and links to binary files.
- GitHub supports Markdown alert syntax:
  - `> [!NOTE]`
  - `> [!TIP]`
  - `> [!IMPORTANT]`
  - `> [!WARNING]`
  - `> [!CAUTION]`
- Alerts cannot be nested inside other elements.
- Collapsed sections are supported with `<details><summary>...</summary>...</details>`.
- Fenced code blocks support language-specific syntax highlighting.
- Mermaid diagrams are supported.

## Practical guidance

- Safe defaults:
  - headings, lists, links, code blocks, tables, checklists
  - short alert callouts for high-signal warnings/notes
- Alerts are a visual aid. Use sparingly and place near section boundaries when possible.
- Do not use emojis in release/PR/issue bodies.
- For long content, use a top TOC block in `<details>` and avoid heading noise above it.
- Use Mermaid only when it reduces ambiguity for flows/concepts.

## Primary sources

- https://docs.github.com/get-started/writing-on-github/working-with-advanced-formatting
- https://docs.github.com/en/repositories/releasing-projects-on-github/managing-releases-in-a-repository
- https://docs.github.com/en/get-started/writing-on-github/working-with-advanced-formatting/organizing-information-with-collapsed-sections
- https://docs.github.com/en/get-started/writing-on-github/working-with-advanced-formatting/creating-a-table
- https://docs.github.com/en/get-started/writing-on-github/working-with-advanced-formatting/creating-and-highlighting-code-blocks
- https://docs.github.com/en/get-started/writing-on-github/working-with-advanced-formatting/creating-a-task-list
- https://docs.github.com/en/get-started/writing-on-github/working-with-advanced-formatting/creating-an-alert
- https://docs.github.com/en/enterprise-cloud@latest/get-started/writing-on-github/working-with-advanced-formatting/creating-diagrams
