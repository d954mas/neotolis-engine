# Architecture and Engine rules

Permanent perspective, always routed. Runs as a normal fresh lens agent.

Check changed decisions against the relevant principle-catalog rules and spec:

- module and layer responsibility;
- engine/game and interface/implementation boundaries;
- explicit behavior instead of hidden policy;
- builder-heavy, simple runtime design;
- composable modules instead of speculative framework machinery;
- platform abstraction and scoped data-oriented design;
- hot-path and tiny-size rules at the architectural level;
- silent code/spec divergence.

Whole-diff aggregate obligation: some violations exist only in aggregate, not at any
single anchor. Census the diff as a set: enumerate every changed failure-policy site
(assert added/removed/rerouted, error channel changed, fallback introduced) and compare
the resulting set against the documented policy in spec/AGENTS; do the same for any
other changed-decision family that repeats across files (new public entry points,
new allocation sites, new platform calls). Report a divergence between the aggregate
set and the documented contract even when each individual site looks locally justified.

Architecture is the object being reviewed; Engine philosophy is the evaluation
criterion. Do not split principles into separate reviewers. Do not absorb detailed
feature edge cases, cache mechanics, build execution, or test-oracle analysis owned by
another selected perspective. Public layout/source compatibility belongs to behavior
or abi-layout unless the layout itself crosses an Engine boundary or violates a
catalog principle.

Return only failures caused by the branch with a reachable architectural consequence
and an exact violated rule/spec clause. Generic SOLID, clean-code, naming, and style
preferences are not evidence.
