---
name: session-retro
description: Evidence-based session retrospective — what was slow, what broke, what to change durably. Use when the user asks for a retro/reflection on the work session.
---

# Session retrospective

Reconstruct from EVIDENCE, in this order — the conversation is read LAST so it
cannot overwrite what the artifacts say:

1. Git: `git log --oneline` for the session's branches; diff stats; commit times.
2. Task/issue state: what was opened, closed, re-opened; CI outcomes (`gh run list`).
3. Artifacts: files created/deleted, gate outputs, benchmark numbers recorded.
4. Only then: the conversation, for intent and decisions not visible in artifacts.

## Output shape

- **What shipped** — outcomes with numbers, not activity.
- **Problems** — for each: `symptom / root cause / faster path next time`. Root
  cause must cite evidence (a command, an output, a diff), not memory.
- **Cost hotspots** — where wall-time/tokens went: repeated commands, re-runs,
  dead ends, CI round-trips.
- **Decisions made** — accepted AND rejected (rejected ones stop re-proposals).
- **Durable changes** — recommend a concrete destination for lessons worth
  preserving (AGENTS.md, a gotcha, a script, a skill, an issue). Keep a
  report-only retrospective read-only; apply changes only when the user asks.

## Non-negotiables

- Do not invent concrete examples; if evidence for a claim is missing, mark it
  `likely`/`unknown` instead of asserting it.
- Do not infer time spent from memory — use timestamps (commits, CI runs) or say
  `unknown`.
- Do not mark goals complete from inside a retrospective — that is the work's
  job, with its own evidence.
