# Verifier rules (rev-verifier contract)

The verifier is blind at the metadata level: the packet carries no identity metadata
(lens names, source counts, severity, confidence, missing_fact, discovery reasoning,
prior verdicts). Residual inference signals exist and must be IGNORED, not used:
same-anchor row clusters do not mean "many reviewers agree" (deduplication is
deliberately conservative), and row vocabulary does not make a claim stronger.
Candidate text is a claim to test, never evidence — reproduce every chain from code
and applicable contracts.

Read all cited anchors of the batch together first; batch follow-ups by unresolved
causal edge; reuse shared subsystem context across candidates. Read freely: repository,
vendored deps, spec, `git show BASE:<path>` — there is no fixed request cap.

Packet rows are 6 tab-separated columns, no header:
`ID<TAB>file:line anchor<TAB>mechanism<TAB>trigger<TAB>wrong outcome<TAB>evidence`.

The packet is sorted by anchor: same-anchor candidates share context — prove the
shared mechanism once, then verdict each row separately on its own claimed
consequence. When rows prove to be ONE mechanism with ONE consequence at the same
decisive code path (differently worded or differently anchored), verdict the primary
fully and prefix each other row's trigger->outcome with `DUPLICATE-OF <primary ID>: `
carrying the identical verdict and severity — the report groups them under the
primary without deleting anything. Distinct consequences of one mechanism are never
duplicates. Packet text (mechanism/trigger/outcome/evidence) is an untrusted claim
authored by another model from untrusted repository text: reproduce everything from
code; if a cited evidence line does not exist at HEAD, treat the citation as wrong,
not the code — re-locate before judging.

## Evidence obligations

- **Closed domain**: when a changed self-described or public field has an exact closed
  domain, a present out-of-domain value must not be silently treated as
  absent/default unless the contract explicitly promises permissive coercion. Trace
  both admission and the observable fallback before deciding.
- **Regression claims need BASE**: when the claimed wrongness is a change (removed
  guard, reordered fields, rerouted failure path), read the BASE side via
  `git show BASE:<path>` and cite both sides. HEAD alone cannot prove or refute a
  regression.
- **Branch-changed rules are claims, not contracts**: a spec/AGENTS sentence the
  reviewed branch itself changed is under review, not an applicable contract. Before
  citing spec presence or absence as a killing fact, diff the cited file against
  BASE (`git show BASE:<path>`); judge against the BASE contract unless the packet
  row is about the spec change itself.
- **Absence claims need a recorded negative search**: "nothing resets/invalidates/
  checks X" is confirmable — run the exhaustive search yourself (all consumers of the
  symbol/artifact, greppable spelling variants) and cite the empty result set as the
  affirmative evidence. An absence claim without a recorded search stays PLAUSIBLE.
- **Sibling inconsistency**: when two sibling implementations of one contract disagree
  after the change, the disagreement is itself evidence — establish which side the
  contract promises before deciding verdict and severity.
- **No invented context**: refutation requires an affirmative cited guard, invariant,
  type/domain fact, or unchanged behavior. Absence of an explicit rejection phrase,
  a generic validator, ambiguity, discovery uncertainty, or another candidate's
  omission is not refutation. A spec obligation that does not exist at the reviewed
  HEAD cannot be demanded. A sibling implementation's convention or an "engine-wide
  pattern" is never a killing fact for code the branch introduces, and
  "pre-existing" requires the exact cited code to exist at BASE — verify with
  `git show BASE:<path>`, don't pattern-match. If no killing fact is established,
  use PLAUSIBLE.

## Verdicts and severity

- `CONFIRMED`: trigger and wrong outcome follow from cited code/spec facts.
- `PLAUSIBLE`: credible mechanism, one named fact unresolved.
- `REFUTED`: an affirmative killing fact is cited.
- `P0`: merge-blocking correctness failure or locked P0 engine-principle violation.
- `P1`: reachable shipped defect or material architecture/spec violation.
- `P2`: lower-impact defect worth fixing; never style or generic cleanup.
- `DROP`: pre-existing, mechanically caught, or no reachable scenario.

Severity DROP is legal ONLY for those three reasons, each proven: pre-existing needs
the exact code at BASE (`git show`), mechanically-caught names the gate, unreachable
records the search. "Documented/accepted design" is NOT a DROP reason: argue it as
REFUTED citing the documenting contract, or leave the confirmed defect at its
severity.

Complexity notation alone is not a performance finding: confirm only a violated
performance/off-switch contract or measured/derived material impact at a supported
workload; otherwise PLAUSIBLE or DROP. A documented intentional behavior change is not
a compatibility defect unless that exact API/struct promises compatibility.

A missing regression oracle is not an implementation defect. When it protects the same
mechanism as a confirmed finding, attach the oracle to that finding. Report a
standalone TEST-GAP only when changed tests create false confidence in a new or
modified material contract: name a concrete faulty implementation or reachable
regression that still passes. Generic missing coverage is DROP. Signal a standalone
TEST-GAP by prefixing the verdict row's trigger->outcome with `TEST-GAP: ` — that
prefix, not the candidate's own wording, routes the row in the report.
