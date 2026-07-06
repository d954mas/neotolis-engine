---
name: reviewing-engine-code
description: Runs a multi-agent read-only review of the current branch against the Neotolis engine's own principles (explicit-over-implicit, no-heap-in-hot-path, NT_ASSERT crash-early, tiny size, engine/game boundary), the docs/spec/ chapters, plus correctness, performance, and tests. Spawns parallel focus-lens reviewers, then adversarially verifies each finding (CONFIRMED/PLAUSIBLE/REFUTED) and reports P0-P2 with trigger scenarios. Use whenever reviewing a branch, PR, diff, or commit in this engine, before merging, or when asked to check code against the engine's philosophy, spec, hot-path rules, or AGENTS.md — even if the user only says "review this" or "look at my changes".
---

# Reviewing Engine Code

An executable playbook for a multi-agent, read-only review of the current branch before
merge. The session that loads this skill becomes the **orchestrator** and runs the three
stages below. Works with any runtime that can spawn read-only subagents (Claude Code,
Codex Desktop). Read `AGENTS.md` and `references/principle-catalog.md` before starting —
they define the engine principles, the NT_ASSERT crash-early policy, hot-path rules, and
the pre-commit checks a review must not duplicate. The review is READ-ONLY end to end:
no edits, no commits, no branch changes.

The engine-principle axis (lens 1) is the point of this skill — it is what generic
bug-finding reviewers miss. Correctness, perf, and test lenses run alongside it, but the
principle check is never optional.

## Stage 1 — Orchestrate

1. Compute the merge base and review scope:
   - `git merge-base origin/master HEAD` → `<merge-base>`
   - `git diff <merge-base>...HEAD` → the full diff under review
   - `git diff --name-status <merge-base>...HEAD` → the changed-file list
2. Classify the diff by touched directories and pick **2-4 focus lenses** from the Focus
   Menu below. ALWAYS include lens 1 (Architecture & spec & principles) regardless of what
   changed. Pick the remaining lenses by where the diff lands (e.g. `engine/*/web/` or
   EM_JS → lens 3; `CMakeLists.txt` → lens 4; renderer/frame-loop code → lens 5).
3. Spawn **one subagent per lens**, all in parallel, each with a fresh context (no fork,
   no shared conversation — independence is the point). Every subagent is READ-ONLY: it
   must not edit files, must not commit, must not run builds that modify the tree.
4. Each subagent's prompt is the Shared Reviewer Template below, verbatim, with
   `<branch>`, `<merge-base>`, and `<FOCUS LINE>` substituted. For lens 1, also instruct
   the subagent to load `references/principle-catalog.md` and check each applicable
   principle against the diff, citing the catalog rule it violates.
5. While the subagents run, perform your OWN independent review pass over the full diff.
   Do not wait idle; your pass is a peer of theirs and feeds Stage 2 like any other report.
6. If a subagent fails or errors out, respawn it once with the same prompt. A cheaper
   model is acceptable for the retry. If the retry also fails, note the lens as
   "not covered" in the final report instead of silently dropping it.

## Focus Menu (pick by changed dirs)

1. **Architecture & spec & principles** (MANDATORY every review) — the engine principles
   in `references/principle-catalog.md` (each has a measurable rule + cite target + a
   compliant/violating example); the `docs/spec/` chapters relevant to the diff; module
   boundaries; module-composition and interface/impl link rules; explicit-over-implicit;
   runtime simplicity; game responsibility not leaking into the engine.
2. **Feature behavior & edge cases** — the feature's state machine; input edge cases;
   buffer ownership; lifecycle (init/reset/teardown); whether test coverage actually
   exercises the new behavior.
3. **Web/WASM bridges** — EM_JS/JS interop; exported functions; Closure-compiler
   compatibility; async lifetime (callbacks firing after teardown); memory ownership
   across the JS boundary (who mallocs, who frees).
4. **Build & linking** — CMake targets; interface/stub/impl composition; loud-link
   gates; transitive dependencies; submodule consumption; wasm vs native target skew.
5. **Performance & size** — hot-path allocations; per-frame work added; batching and
   GL state-change regressions; binary size risk from new code or data.
6. **Resource lifecycle** — pack mount/unmount ordering; blob pins; eviction; provider
   severing; use-after-free on handles that outlive their pack.
7. **UI state & retained data** — nt_ui state cells; id collisions (additive Clay child
   ids); retained-state cleanup on widget removal; layer/z/scissor ordering.
8. **Tests as spec** — do the new tests pin the new behavior or merely execute it;
   setup/teardown asymmetry; missing negative cases; tests that pass for the wrong reason.

Directory-to-lens hints (a diff usually spans several rows; pick the dominant ones):

| Changed paths                                             | Lenses to add |
| --------------------------------------------------------- | ------------- |
| `engine/render*`, `engine/renderers/`, frame loop, batching, SoA components | 5 |
| an `engine/` module with a state machine or public API change | 2         |
| `engine/*/web/`, `EM_JS`, `library_*.js`, exported symbols | 3            |
| `CMakeLists.txt`, `cmake/`, stubs/interface targets       | 4             |
| `engine/resource/`, atlas, pack format, mount/unmount, builder pack output | 6 |
| `engine/ui/`, widgets, `nt_ui_*`, Clay integration        | 7             |
| `tests/` dominating the diff, or a bugfix branch          | 8             |
| Anything (always)                                         | 1             |

When more than four rows match, prefer the lenses covering the riskiest integration
points (per AGENTS.md: coordinate/data transforms between systems, lifecycle, hot path)
over the ones covering the largest line counts.

## Shared Reviewer Template (verbatim block to give each subagent)

```
You are doing an independent READ-ONLY code review of branch <branch> in the
neotolis-engine repository. Do not edit any file, do not commit, do not switch branches.

Context: Neotolis Engine is a minimalist C17 game engine for Web/WASM (WebGL 2).
Explicit over implicit; the runtime stays simple; no heap allocation in hot paths
(frame loop, fixed update, render item generation, batching, per-frame resource
resolve, dense SoA iteration); compile-time limits and preallocated storages are
deliberate design. The NT_ASSERT crash-early policy is intentional: invariant
violations crash immediately rather than degrade silently — but note NT_ASSERT
compiles to ((void)0) in the OFF shipping config, so bounds checks and side effects
must never live inside it. Source of truth: AGENTS.md and docs/spec/ — read
docs/spec/index.md first, then only the chapters relevant to this diff.

Compare against merge-base <merge-base>. Scope: only code introduced or modified by
this branch (git diff <merge-base>...HEAD). Read the FULL enclosing function of every
hunk — bugs in unchanged lines of a touched function are in scope. For every line the
diff DELETES, name the invariant that line enforced and find where the new code
re-establishes it; if you cannot find where, that is a candidate finding.

Focus: <FOCUS LINE>

Surface EVERY candidate for which you can name a failure scenario — do not silently
drop half-believed candidates. Verification happens downstream; silent self-censorship
is the dominant cause of missed bugs. A candidate you are 40% sure of belongs in your
report with your uncertainty stated.

For each finding return:
- severity guess (P0/P1/P2)
- file:line
- issue in imperative mood, <= 80 chars
- body: the concrete trigger (inputs/state -> wrong outcome) as the FIRST sentence,
  then why it matters
- suggested fix, at most 3 lines of code

Also return two short sections: (a) good design decisions worth keeping, and
(b) any code-vs-spec divergences, flagged explicitly per AGENTS.md — never silently
normalized.
```

## Stage 2 — Verify (orchestrator, after collecting all reports)

1. Collect every report, including your own pass. Deduplicate findings by
   `(file, line, mechanism)` — the same line flagged for two different mechanisms is
   two findings; two lenses flagging the same mechanism is one.
2. Judge each candidate **adversarially**, reading the actual code at the cited
   location plus its callers where the trigger depends on them:
   - **CONFIRMED** — you can name concrete inputs or state that trigger it and the
     resulting wrong output or crash. Quote the guilty line in the finding.
   - **PLAUSIBLE** — the mechanism is real but the trigger is uncertain (timing,
     config, platform). State exactly what observation or test would confirm it.
   - **REFUTED** — only with quoted proof: the code factually does not say what the
     finding claims, or a guard exists (cite the guard's file:line), or the scenario
     is provably impossible (type range, compile-time constant, established invariant).
3. "Feels speculative" is NOT grounds for refutation when the state is realistic —
   error paths, cold caches, boundary values, falsy zero, first/last iteration, and
   unmount-during-use all count as realistic. Refute with evidence or keep the finding.
4. When two subagents disagree about the same location (one flags it, one implicitly
   treats it as fine), the flag wins the right to be judged — absence of a finding in
   another report is not evidence of absence. Judge it on the code alone.
5. Re-anchor each surviving finding's severity yourself using the Stage 3 anchors;
   subagent severity is a guess, not a vote.
6. Drop REFUTED findings entirely. Carry CONFIRMED and PLAUSIBLE into the report,
   ordered most severe first, CONFIRMED before PLAUSIBLE within a severity.

## Stage 3 — Report format

Produce one report with these sections, in order:

1. **Verdict line** — exactly one of: `merge-ready`, `merge with fixes`,
   `do not merge`. One sentence of justification. Default mapping: any CONFIRMED P0
   → `do not merge`; CONFIRMED P1, or P0 only PLAUSIBLE → `merge with fixes`;
   otherwise `merge-ready`. Override the mapping only with stated reasoning.
2. **Findings table** — columns: Severity | file:line | Issue | Trigger | Suggested fix.
   Severity anchors:
   - **P0** — breaks unconditionally, or violates a locked spec rule or a catalog
     principle marked P0 (heap in hot path, side effect inside NT_ASSERT). Drop everything.
   - **P1** — real bug with a narrow but reachable trigger, or a principle divergence
     from the catalog / spec that ships wrong behavior or the wrong architecture.
   - **P2** — should fix before or immediately after merge.
   - **P3** exists as a judgment level but is NOT reported. Do not include it.
3. **Good decisions** — short bullets naming design choices in the branch worth keeping
   (so a fix pass does not accidentally revert them).
4. **Pre-existing issues noticed** — problems on lines this branch did not touch,
   strictly separated from branch findings, no severities assigned. This section may
   be empty; never mix its content into the findings table.

An empty findings list is a valid, successful outcome — do not pad the report to look
thorough. Tone: matter-of-fact; no praise filler, no accusations. Code excerpts in the
report are at most 3 lines each.

## Do NOT report (false-positive classes)

- Anything on lines this branch did not touch — at most it goes to the
  Pre-existing section.
- Anything clang-format, clang-tidy, the compiler warnings, or the CI gates already
  catch — the pre-commit checks in AGENTS.md run these; a review that repeats them
  adds noise, not safety.
- Style, naming, comment volume, missing docstrings, or documentation quantity.
- "Add a NULL check / return an error code" on paths where NT_ASSERT crash-early is
  the documented policy. The exception is real: assert-only guards on untrusted or
  runtime input, or side effects inside NT_ASSERT, ARE findings (NT_ASSERT vanishes
  in the OFF shipping config).
- Compile-time limits (`#define` caps) and preallocated storages questioned as
  "inflexible" or "should be dynamic" — they are the design. Flag a genuine overflow
  or off-by-one against a cap instead.
- Demands for rigor absent from the rest of the codebase (extra abstraction layers,
  defensive copies, speculative generality).
- Any finding for which no one can name a trigger scenario. "This looks fragile"
  without inputs/state that break it is not a finding.
