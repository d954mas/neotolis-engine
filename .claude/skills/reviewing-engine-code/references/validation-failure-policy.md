# Validation and failure policy

Use this specialist when the branch changes preconditions, guards, assertions, status
returns, error collection, or partial-failure paths.

Distinguish the four Neotolis failure domains and hold each changed site to its own
contract: programmer invariants (assert, fail early), documented recoverable API
errors (error return, no assert), untrusted/runtime boundaries (hard guard), and
builder content errors (graceful error channel per the documented list).

Check:

- a changed site moved between domains (assert -> status, status -> silent fallback)
  with the move justified against the documented policy, not local convenience;
- error collection paths: every logical failing instance produces an error with the
  right identity, payload, ordering, and public observability — collection that stops
  early, overwrites, or drops instances is a finding;
- partial failure: after N of M items fail, the reported state, the returned status,
  and the produced outputs agree with each other;
- no new silent fallback: recovery only where the contract promises recovery;
- assert expressions stay side-effect-free (NT_ASSERT_MODE=OFF does not evaluate them).

Report a concrete accepted or rejected state and its wrong outcome. Leave
malformed-data arithmetic to untrusted-input and aggregate policy census to
architecture.
