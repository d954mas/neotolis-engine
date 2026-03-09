<purpose>
Fetch a GitHub issue and create a new GSD phase from it. Automatically extracts issue title as phase description, creates the phase directory, generates CONTEXT.md pre-filled with issue details, and updates roadmap structure.
</purpose>

<required_reading>
Read all files referenced by the invoking prompt's execution_context before starting.
</required_reading>

<process>

<step name="parse_arguments">
Parse the command arguments:
- Argument can be an issue number (e.g., `4`) or a full GitHub URL (e.g., `https://github.com/owner/repo/issues/4`)
- Extract the issue number from the argument

If no arguments provided:

```
ERROR: Issue number or URL required
Usage: /gsd:issue-to-phase <issue-number-or-url>
Example: /gsd:issue-to-phase 4
Example: /gsd:issue-to-phase https://github.com/user/repo/issues/4
```

Exit.
</step>

<step name="fetch_issue">
Fetch the issue details using GitHub CLI:

```bash
gh issue view <issue_number> --json title,body,labels,assignees,milestone,state,url
```

If the command fails, report the error and exit.

Extract from JSON:
- `title` — used as phase description
- `body` — used for CONTEXT.md details
- `labels` — included in context metadata
- `url` — linked in CONTEXT.md for traceability
- `state` — informational
- `assignees` — informational
- `milestone` — informational
</step>

<step name="init_context">
Load phase operation context:

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
</step>

<step name="add_phase">
**Delegate the phase addition to gsd-tools:**

Use the issue title as the phase description:

```bash
RESULT=$(node "./.claude/get-shit-done/bin/gsd-tools.cjs" phase add "${issue_title}")
```

The CLI handles:
- Finding the highest existing integer phase number
- Calculating next phase number (max + 1)
- Generating slug from description
- Creating the phase directory (`.planning/phases/{NN}-{slug}/`)
- Inserting the phase entry into ROADMAP.md with Goal, Depends on, and Plans sections

Extract from result: `phase_number`, `padded`, `name`, `slug`, `directory`.
</step>

<step name="create_context">
Create a CONTEXT.md file in the phase directory pre-filled with issue details:

Write to `.planning/phases/{NN}-{slug}/{padded}-CONTEXT.md`:

```markdown
---
phase: "{padded}"
name: "{issue_title}"
created: {today}
source: github-issue
issue_number: {issue_number}
issue_url: "{issue_url}"
---

# Phase {phase_number}: {issue_title} — Context

## Source Issue

- **Issue:** [{repo}#{issue_number}]({issue_url})
- **State:** {state}
- **Labels:** {labels as comma-separated list, or "none"}
- **Assignees:** {assignees as comma-separated list, or "none"}
- **Milestone:** {milestone or "none"}

## Issue Description

{issue_body — the full body of the issue, verbatim}

## Decisions

_Decisions will be captured during /gsd:discuss-phase {phase_number}_

## Discretion Areas

_Areas where the executor can use judgment_

## Deferred Ideas

_Ideas to consider later_
```
</step>

<step name="update_project_state">
Update STATE.md to reflect the new phase:

1. Read `.planning/STATE.md`
2. Under "## Accumulated Context" → "### Roadmap Evolution" add entry:
   ```
   - Phase {N} added from issue #{issue_number}: {issue_title}
   ```

If "Roadmap Evolution" section doesn't exist, create it.
</step>

<step name="completion">
Present completion summary:

```
Phase {N} created from GitHub issue #{issue_number}:
- Issue: {issue_url}
- Title: {issue_title}
- Directory: .planning/phases/{phase-num}-{slug}/
- Context: Pre-filled from issue body
- Status: Not planned yet

Roadmap updated: .planning/ROADMAP.md

---

## ▶ Next Up

**Phase {N}: {issue_title}**

`/gsd:plan-phase {N}`

<sub>`/clear` first → fresh context window</sub>

---

**Also available:**
- `/gsd:issue-to-phase <number>` — create phase from another issue
- `/gsd:add-phase <description>` — add phase manually
- Review roadmap

---
```
</step>

</process>

<success_criteria>
- [ ] Issue fetched successfully via `gh issue view`
- [ ] `gsd-tools phase add` executed successfully
- [ ] Phase directory created
- [ ] CONTEXT.md created with issue details
- [ ] Roadmap updated with new phase entry
- [ ] STATE.md updated with roadmap evolution note
- [ ] User informed of next steps
</success_criteria>
