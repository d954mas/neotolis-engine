## Project

Neotolis Engine is a minimalist **C17** game engine for **Web/WASM (WebGL 2)**. The game controls the loop; the builder does heavy work offline; runtime stays simple.

## Read before changing

- Start at [docs/spec/index.md](docs/spec/index.md); read the whole chapter for each module you change. Public API changes also read [API contracts](docs/spec/core/api-contracts.md).
- Before configuring, building, writing tests or debugging build/CI failures, read [docs/build.md](docs/build.md). It owns setup, flags, check coverage and troubleshooting.
- Code/spec divergence must be stated explicitly. Mark any necessary temporary deviation in the change explanation and final report; never guess a replacement contract.
- Keep module behavior in its spec and build instructions in `docs/build.md`. This file contains shared working rules, not a catalogue of flags or past failures.

## Workflow

- Start with a GitHub issue and feature branch before the first commit; never stack commits on local master. Branch names: `{issue_number}-{slug}`. Continue the current task's branch/PR when requested.
- Navigate C with clangd LSP first. After adding a `.c` file, rebuild `native-debug` to refresh `compile_commands.json`; confirm new-file and platform-`#if` diagnostics against a real build.
- Execute the requested scope. Report unrelated improvements separately.
- Before changing a validator contract, find every constructor of that data shape, including spec examples and parameterized test helpers.

## Engine principles

- **Explicit, code-first, composable:** game owns gameplay, system order, render passes and content organization. Do not hide policy in the engine or replace modules with a monolith. See [principles](docs/spec/core/principles.md).
- **Keep it simple and small:** avoid speculative abstractions and dependencies. Every byte counts; use only the modules needed.
- **Prebuilt assets:** source formats and heavy validation belong in the builder. Runtime loads binary packs and enforces its documented safety checks and recoverable contracts.
- **Platform abstraction:** browser/OS calls go through the owning engine wrapper.
- **Data-oriented where useful:** dense storage, typed handles and predictable access for renderers/components; input, window and app remain simple structs.
- **No heap in hot paths:** use fixed limits, preallocated storage or frame scratch; no hidden reallocations, unnecessary copies or heavy layers. Hot paths include frame/fixed loops, render-item generation, batching, per-frame resource resolve and dense ECS/SoA iteration.

## Asserts and errors

- Prefer `NT_ASSERT` for invariants and unexpected runtime states; use `NT_BUILD_ASSERT` for builder programmer invariants, unexpected states, OOM, missing/unreadable files and single-asset decode failures.
- Error returns are valid for documented recoverable API outcomes, never to silently swallow bugs or broken data.
- Release defaults to TRAP. OFF is an unsupported size escape hatch: violated asserted preconditions cause undefined behavior; no fallback is required solely for OFF.
- Assert expressions must be side-effect-free because OFF does not evaluate them. Hard guards belong at untrusted/runtime-input boundaries and where the API promises recoverable rejection.
- ATLAS builder content failures use `nt_builder_get_errors`; keep the exact exception list in [builder error policy](docs/spec/builder/builder.md#asserts-vs-graceful-content-errors).

## No ceremony

Code that serves other new code rather than the game or the engine contract is
ceremony: a return value every caller must branch on, a test hook in production
code, a mirror of state that already exists, an API or backend hook added only to
check another new piece, a guard for a scenario no engine path produces. Before
adding any of these, check whether the need is real: can existing state, an
existing assert, or an existing skip path already cover it, and is the branch
that needs it reachable at all. If the work still seems to require new public
API, new state, a new backend hook, or a new obligation on every caller: stop and
ask the developer with one paragraph — what breaks without it, the smallest
alternative, what each catches. The developer decides.

## Code and design

- Comments explain a non-obvious WHY, preferably one line, at most 2–3. No history, commit/issue references, Phase/REVIEW/CHUNK tags, test-name pins, user quotes or experimental boilerplate in source; explanations of changes belong in commits/PRs.
- Use `// #region name` / `// #endregion` in long functions, with no blank line just inside either marker. Preserve existing short inline comments.
- Organize large files with regions rather than extra translation units; cross-TU calls inhibit inlining without LTO.
- Before adding a subsystem: diagram data/coordinate transforms, compare parallel APIs, check mobile-WASM types/ranges, prototype the riskiest integration, and test asymmetric data.
- New widget demos belong in `examples/ui_showcase`, never a new example directory. Follow the [UI ID rules](docs/spec/ui/nt-ui.md#widget-ids).
- Cache identity must be exact or hashed as a whole, never a linear fold of handles/enums. Include consecutive-handle × one-field-change tests; see [render architecture](docs/spec/render/architecture.md).

## Required checks

- After each edit burst and before each commit: `bash scripts/format_and_check.sh`.
- Before push: `bash scripts/check.sh --push`. `bash scripts/format_and_check.sh --push` combines both requirements; `bash scripts/check.sh --full` runs the full format/tidy sweep.
- Fix failures before committing. A fresh full gate is authoritative; targeted tests alone can use stale binaries.
- Never run two `check.sh`/`ctest` processes in one tree at once, including subagents. Remove `build/.check.lock` only after confirming its owner is dead.
- Before pushing new C test/tool files, run `clang-tidy -p build/_cmake/tidy-ci <file>` directly; see [check details](docs/build.md#checks).
- Commit deterministic regenerated `examples/*/generated/*.h` when pack builds reveal stale committed copies.

## Evidence and review

- State the changed claim and prove it with the narrowest check that exercises the expected behavior. Build success alone is not runtime proof; inspect errors even when exit status is zero.
- Before running an example/benchmark by hand, rebuild its target. An existing executable is not freshness evidence.
- Missing infrastructure or untested behavior is `unverified`, with the next concrete command; never imply success. Record rejected approaches in the issue so later work does not repeat them.
- For full branch review, use [.claude/skills/reviewing-engine-code/SKILL.md](.claude/skills/reviewing-engine-code/SKILL.md): independent read-only reviewers, engine-principle lens and adversarial verification.

## Communication

- Lead with the result in 1–2 lines; then only the delta. Keep routine commit/check/push reports to one line. Expand for concepts, trade-offs or an explicit request; tables compare at least three items.
- Present material design/library choices with trade-offs; do not choose for the developer. Respect library choices and include size, benchmarks and other engines' practice when proposing dependencies.
- Verify the developer's debugging hypothesis first and confirm/refute with evidence. Without a hypothesis, diagnose and fix independently. Do not repeat the user's words.
