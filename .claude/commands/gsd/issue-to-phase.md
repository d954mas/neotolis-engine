---
name: gsd:issue-to-phase
description: Create a new GSD phase from a GitHub issue
argument-hint: <issue-number-or-url>
allowed-tools:
  - Read
  - Write
  - Bash
---

<objective>
Fetch a GitHub issue and create a new GSD phase from it.

Routes to the issue-to-phase workflow which handles:
- Fetching issue details via `gh issue view`
- Phase creation via `gsd-tools phase add`
- CONTEXT.md generation with issue body, labels, and link
- STATE.md roadmap evolution tracking
</objective>

<execution_context>
@./.claude/get-shit-done/workflows/issue-to-phase.md
</execution_context>

<context>
Arguments: $ARGUMENTS (GitHub issue number or URL)

Roadmap and state are resolved in-workflow via `init phase-op` and targeted tool calls.
</context>

<process>
**Follow the issue-to-phase workflow** from `@./.claude/get-shit-done/workflows/issue-to-phase.md`.

The workflow handles all logic including:
1. Argument parsing (issue number or URL)
2. Fetching issue via `gh issue view`
3. Extracting title, body, labels, assignees
4. Creating phase via `gsd-tools phase add`
5. Generating CONTEXT.md with issue details
6. STATE.md updates
</process>
