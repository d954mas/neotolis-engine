# Issues

How to file, label, link and close GitHub issues for the engine. Agents and people follow the same rules.

## One issue, one problem

- One issue describes one problem or one deliverable. Split unrelated findings into separate issues; group a multi-step plan under an epic.
- Before filing, search open and closed issues for the same problem. Extend an existing issue instead of opening a near-duplicate.
- Write the body for a reader who has not seen the conversation that produced it. Point at code by path and symbol (`nt_gfx.c` `basis_target_format`), not by line number alone: line numbers go stale.
- No process framing in titles or bodies: no Phase/GSD numbers, no session or agent names.

## Title

`<module>[/<platform>]: <what is wrong or what to build>`, lowercase module, imperative or descriptive summary.

- `builder: write a depfile of every input read into a pack`
- `gfx/web: preserve browser status when WebGL context creation fails`

The module is the engine directory or subsystem the change lands in (`gfx`, `render`, `ui`, `font`, `resource`, `builder`, `skeletal`, `input`, `http`, `ci`, ...).

## Body

The GitHub templates in `.github/ISSUE_TEMPLATE/` carry these sections and apply the type label. With `gh issue create`, write the same sections and pass `--label`.

Bug:

- **Problem**: observed behavior, with the master commit it was seen on.
- **Repro**: smallest steps or test that shows it; say "traced in source only" when it was not reproduced.
- **Expected**: the contract it breaks (spec chapter and section title).
- **Who hits it**: the game or example, when known.

Feature:

- **Goal** and **who needs it**.
- **Scope** and **Non-goals**.
- **Acceptance**: checkable items, including tests and measured size/performance where relevant.

Research:

- **Question** to answer.
- **Method**: what to measure, on which targets (see [measuring performance on phones](perf-measurement.md)).
- **Decision rule**: which result leads to which action. "No change" is a valid outcome.

Keep the body current. When a decision changes the plan, edit the body; comments hold the history, the body holds the current plan.

## Labels

Every issue gets labels from this set and no other.

**Type**: at most one. Leave it off when none fits (cleanup, docs, perf without a question, tooling).

| Label | Use when |
|---|---|
| `bug` | Behavior breaks a documented contract or is plainly wrong. |
| `feature` | A new capability or API. |
| `research` | The output is a decision or numbers, not necessarily code. |

**Area**: one, at most two when the work truly spans both.

| Label | Covers |
|---|---|
| `render` | gfx backend (GL/WebGL), renderers, materials, passes, postfx, shapes, sprites, meshes |
| `ui` | nt_ui, Clay, widgets, fonts, text rendering |
| `assets` | resource, packs, loading, and the builder |
| `skeletal` | skeletal animation |
| `io` | window, input, clipboard, crash, fs, http, network |
| `infra` | CMake, CI, emscripten, tests, DevAPI, metrics, profiling, agent tooling |
| `core` | assert, log, memory, math, entity/component storage, app and time |

**Platform**: only when the issue is specific to a platform. No platform label means cross-platform.

| Label | Use when |
|---|---|
| `web` | Browser or WASM only. |
| `mobile` | Mobile only (mobile browsers included). |
| `desktop` | Native builds in general. |
| `windows` / `macos` / `linux` | One desktop OS only. |

**`game`**: a real game or the studio needs this. Name the game and the use in the body.

## Epics

An epic is an issue whose body lists its children as a task list (`- [ ] #N short title`). Each child names its parent in the first line (`Parent epic: #N`). Tick the box when the child closes and keep the list complete.

## Closing

- A PR that delivers the issue says `Closes #N`.
- Closing without a PR always gets a comment with the reason and evidence: the commit or PR that did the work, or why the premise no longer holds.
- Duplicate: close as duplicate with `Duplicate of #N`. Copy anything unique (numbers, repro, use case) into the surviving issue first.
- Merge: same as duplicate when one issue absorbs another's scope; the comment names what moved.
- Rejected ideas: close as not planned with the reason, so the same idea is not filed again.
