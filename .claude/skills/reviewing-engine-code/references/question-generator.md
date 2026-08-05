# Question mapper and lens routing (rev-mapper duties)

One fresh mapper receives the frozen scope artifacts (scope.json, diff.patch,
inventory.json, routing-hint.tsv, symbols.json) plus the frozen BASE rulebook
`agents-base.md` when present — and nothing else. No prior review, candidate, later
commit, worker reasoning, or expected result. If `agents-base.md` is absent, note
`rules=absent` in STATUS and route from changed contracts alone. Both outputs are
validated mechanically (`check-mapper.mjs`) before any lens spawns; malformed output
means one respawn, then abort.

## Private hypothesis distribution

Before writing anything, privately construct a probability distribution over
materially different reachable failure hypotheses. Include both typical mechanisms and
lower-probability mechanisms whose trigger is still supported by the changed contract.
Use the distribution to prevent the most obvious hypothesis from crowding out distinct
failure classes. The distribution is private sampling work: never expose hypotheses,
probabilities, likely answers, causal claims, confidence, or severity in any output.

## Output 1 — neutral question map

Questions are supplemental evidence probes, not findings, answers, severity hints,
expected counts, or routing authority:

- organize by changed operation or contract, not by file;
- for compound operations, ask for the exact primitive sequence, admission/rollback at
  boundaries, state transitions, and final observable result;
- for identity/ownership/state changes, ask for the complete producer -> stored
  identity/view -> replacement or teardown -> consumer path;
- for any shared mutable state introduced or touched, ALWAYS include the standing
  probes: behavior under reentrant use, under calls before init / after teardown, and
  the fate of state committed by earlier or failed operations — these are the probes
  most often lost to sampling variance;
- for untrusted boundaries, ask which exact values are accepted, represented,
  narrowed, rejected, and reported;
- for tests, ask which oracle distinguishes the promised behavior from a concrete
  faulty implementation — the faulty implementation must be HYPOTHETICAL AND GENERIC
  ("an implementation that reverses the order"), never a description of the actual
  code under review;
- neutrality is form, not just intent: no yes/no question about a single suspected
  property ("does the loop terminate?" is a hypothesis with a question mark — ask for
  the capacity sequence instead); the column-3 label names an operation or contract,
  never a failure class ("response buffer growth", not "request_id amplification");
  never assert likely defects, answers, causal locations, confidence, or severity;
- one probe per row: a row that packs unrelated sub-questions is invalid — split it.

One row per question, exactly 4 fields, then the STATUS row:

```text
QID<TAB>relevant lens ids (comma)<TAB>changed operation/contract<TAB>neutral evidence question
STATUS<TAB>mapper<TAB><assignment_hash><TAB>questions=<row count>
```

## Output 2 — final lens routing

The routing-hint from freeze-scope is a hint, never a decision. The mapper decides
from changed contracts:

- lens ids come from the fixed vocabulary (bare names, exactly as in the menu minus
  `.md`): a second narrow packet of the same reference is `<id>-2` — permitted when
  one branch contains two independently complex risks;
- `architecture` and `behavior-contracts` are permanent — never droppable;
- rows marked `force=` in the hint (mechanical triggers) may not be declined;
- the mapper may ADD any lens freely, BUT every added conditional lens's contract
  statement must (a) name a failure class PAIRWISE-UNIQUE among all routed rows —
  two rows claiming the same class is one packet too many — and (b) state in one
  clause what the overlapping permanent lens will NOT cover and why;
- the mapper may DROP only a hinted conditional lens, with a named reason of the form
  "no changed path or symbol in the frozen inventory exercises <failure class>";
- routing floor: at least 5 LENS rows;
- a lens row carries: lens id, its question subset (QIDs), and a one-line contract
  statement (changed contract + unique failure class + relevant paths).

Row formats, exactly 4 fields each, then the STATUS row:

```text
LENS<TAB><lens-id><TAB>qids(comma)<TAB>contract statement
DROP<TAB><lens-id><TAB>-<TAB>named reason
STATUS<TAB>mapper<TAB><assignment_hash><TAB>lenses=<LENS row count><TAB>drops=<n><TAB>rules=present|absent
```
