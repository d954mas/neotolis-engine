---
name: issue-to-phase
description: Create a new GSD phase from a GitHub issue
argument-hint: <issue-number-or-url>
allowed-tools:
  - Read
  - Write
  - Edit
  - Bash
---

<objective>
Fetch a GitHub issue and create a new GSD phase from it. Extracts issue title as phase name, issue body as phase Goal in ROADMAP.md. Does NOT create CONTEXT.md — that is handled by /gsd:discuss-phase.
</objective>

<context>
Arguments: $ARGUMENTS (GitHub issue number or URL)
</context>

<process>

## 1. Parse arguments

- Argument can be an issue number (e.g., `4`) or a full GitHub URL (e.g., `https://github.com/owner/repo/issues/4`)
- Extract the issue number from the argument

If no arguments provided:

```
ERROR: Issue number or URL required
Usage: /issue-to-phase <issue-number-or-url>
Example: /issue-to-phase 4
Example: /issue-to-phase https://github.com/user/repo/issues/4
```

Exit.

## 2. Fetch issue

```bash
gh issue view <issue_number> --json title,body,labels,assignees,milestone,state,url
```

If the command fails, report the error and exit.

Extract from JSON:
- `title` — used as phase name
- `body` — used as phase Goal in ROADMAP.md
- `labels` — informational
- `url` — linked in Goal for traceability
- `state`, `assignees`, `milestone` — informational

## 3. Check roadmap exists

```bash
INIT=$(node "./.claude/get-shit-done/bin/gsd-tools.cjs" init phase-op "0")
if [[ "$INIT" == @file:* ]]; then INIT=$(cat "${INIT#@file:}"); fi
```

Check `roadmap_exists` from init JSON. If false:
```
ERROR: No roadmap found (.planning/ROADMAP.md)
Run /gsd:new-project to initialize.
```
Exit.

## 4. Add phase

```bash
RESULT=$(node "./.claude/get-shit-done/bin/gsd-tools.cjs" phase add "${issue_title}")
```

Extract from result: `phase_number`, `padded`, `name`, `slug`, `directory`.

## 5. Update roadmap Goal

The `gsd-tools phase add` creates a placeholder `**Goal:** [To be planned]`. Replace it with a Goal derived from the issue body.

Read ROADMAP.md, find the Phase {N} entry, and replace:

```
**Goal:** [To be planned]
```

With:

```
**Goal:** {Concise 1-2 sentence summary of the issue body}
**Source:** [{repo}#{issue_number}]({issue_url})
```

If the issue has labels, add: `**Labels:** {comma-separated labels}`

## 6. Update STATE.md

Read `.planning/STATE.md`. Under "## Accumulated Context" → "### Roadmap Evolution" add:
```
- Phase {N} added from issue #{issue_number}: {issue_title}
```

If "Roadmap Evolution" section doesn't exist, create it.

## 7. Completion

```
Phase {N} created from GitHub issue #{issue_number}:
- Issue: {issue_url}
- Title: {issue_title}
- Directory: .planning/phases/{phase-num}-{slug}/
- Goal: Updated in ROADMAP.md from issue body
- Status: Not planned yet

Roadmap updated: .planning/ROADMAP.md

---

## ▶ Next Up

**Phase {N}: {issue_title}**

`/gsd:discuss-phase {N}` — discuss gray areas before planning

<sub>`/clear` first → fresh context window</sub>

---

**Also available:**
- `/gsd:plan-phase {N}` — skip discussion, go straight to planning
- `/issue-to-phase <number>` — create phase from another issue
- `/gsd:add-phase <description>` — add phase manually
- Review roadmap

---
```

</process>

<success_criteria>
- [ ] Issue fetched successfully via `gh issue view`
- [ ] `gsd-tools phase add` executed successfully
- [ ] Phase directory created
- [ ] Roadmap Goal updated with issue summary and source link
- [ ] STATE.md updated with roadmap evolution note
- [ ] User informed of next steps
- [ ] NO CONTEXT.md created (discuss-phase handles that)
</success_criteria>
