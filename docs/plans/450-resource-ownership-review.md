# Resource ownership plan and implementation review

Date: 2026-09-10. Result: APPROVE, no unresolved implementation blockers.

Reviewed [plan](450-resource-ownership-plan.md) against branch `450-pack-registration`, HEAD `161b876899687205737bffd29671513ab5c0fc1f`. The first draft was revised after independent source-based critiques; all four reviewers approved revision 2. This is plan acceptance, not implementation or performance verification.

## Independent reviews

| Reviewer | Focus | Final verdict |
| --- | --- | --- |
| plan_format_research | Wire contract, builder dedup, constructors and readers | APPROVE |
| plan_lifecycle_research | Free slots, ownership, activation, BLOB and cleanup | APPROVE |
| plan_adversarial_review | Engine principles, minimal state, failure and reuse counterexamples | APPROVE |
| Claude Fable (`claude-fable-5-1`) | Independent source-based critique and focused re-review | APPROVE |

Subagents were read-only. Fable ran through Claude CLI with Read/Glob/Grep only, safe mode, no session persistence and no MCP configuration. The developer explicitly authorized transmission of the plan and related source to Anthropic. The failed initial network attempt produced no review; the subsequent two successful rounds produced the critique and approval.

## Findings incorporated into revision 2

| Finding | Disposition |
| --- | --- |
| A failed activation on an otherwise clean step does not dirty resolve | Require `needs_resolve` after every completed activation attempt; test delayed failure after the initial resolve. Source: nt_resource.c, activation around lines 748-754. |
| Zero-ID virtual unregister can match a tombstone and push a free index twice | Assert nonzero virtual IDs, match only live records, and test rejection before mutation. Wire zero IDs remain recoverable malformed input. |
| Same-index/same-handle reuse can retain stale AUX data and expose replacement bytes before resolve | Invalidate matching existing resolve/user-data indices before reuse, using the existing resource-ID slot lookup. Keep normal resolve responsible for cleanup, callbacks and publication; make get_blob follow the published named index. No generations or new state. |
| PIN_BLOB severing needs the old asset attribution | Finish synchronous severing before invalidating indices, clearing records or returning them to the stack. Retain copy-out data for normal resolve cleanup. |
| File-owner removal through virtual unregister would leave aliases dangling | Assert the existing virtual-only unregister contract. File assets leave through whole-pack unmount. |
| Read-through ownership requires more than changing activation | Enumerate resolve, effective handles, getters, diagnostics, invalidation and test setters. BLOB handles retain the named entry index, including zero; metadata and callbacks retain per-name identity. |
| Constructor/version migration was incomplete | Inventory direct fixtures, omitted-field initializers, encoder plumbing and test readers. Include test_resource_timing.c independent owner ordinals and the NT_TEST_ACCESS font fixture. Preserve payload versions and raw encoded caches. |
| Pack dump infers aliases from equal byte ranges | Migrate find_duplicate_original and duplicate statistics to validated owner_entry. Equal-range self-owned records remain independent. |
| Early dedup can reference a late alias | Flatten both writer paths to a canonical owner and assert that local invariant before copy/serialization. Require compatible types when sharing final encoded bytes. |
| Capacity and benchmark cases could exceed their actual configurations | Separate zero-entry packs from invalid zero capacity. Use independent capacity 1/odd/65535 smokes. Standard builder cases stop at 1024; 2048-entry cases are explicitly synthetic. |
| Parse-only timing hides stack initialization and owner read-through costs | Measure initialization, parse/CRC, activation, dirty resolve, clean step, persistent bytes and peak scratch separately. Compare equivalent logical packs across the format bump. |

The reviewers accepted the reserved free-stack suffix as the temporary manifest-to-registry mapping: complete validation precedes reservation, parsing has no callbacks, and no pointer survives parsing. They also accepted direct canonical links without refcounts, an object table or mirrored alias state. Alias-only requests remain supported by eager pack activation.

Fable's first-round suggestion that the corrected-load reference should name issue #458 instead of #459 was rejected after checking [issue #459](https://github.com/d954mas/neotolis-engine/issues/459). That issue was delivered by PR #458, the original baseline commit. The plan now names both; Fable accepted the clarification in round 2.

## Nonblocking implementation notes

- Set the activation dirty flag once after the success/failure branch.
- Look up the requested slot using the released record's resource_id before clearing that ID. Virtual unregister requires no PIN_BLOB sever step: blobless virtual packs cannot publish that provider policy.
- Fable suggested optionally folding PIN_BLOB severing into the per-record slot lookup to remove the existing full-slot scan. Revision 2 keeps the verified existing severing order; loop consolidation is not required for this migration and introduces no reason for another subsystem or state field.

## Evidence and limits

Local raw Fable results are in ignored `build/450-plan-fable-round1-retry.json` and `build/450-plan-fable-round2.json`; both identify the reviewing model as `claude-fable-5-1`. The round-2 response explicitly verifies the failed-activation dirty flag and release-time invalidation against resolve phases D.1-D.5, getters and unmount ordering.

The plan defines the behavioral regressions, format migration, benchmark cases and build gates needed during implementation. Engine source remains unchanged by this planning task. No new speedup, binary-size reduction or runtime fix is claimed as measured or delivered.

## Implementation review after authorization

The developer subsequently authorized implementation. Four fresh independent
read-only subagents reviewed the working-tree migration against `161b8768`,
followed by a source-based Claude Fable review. Final verdict: merge-ready;
no unresolved actionable findings. This verdict does not authorize a merge.

| Reviewer | Lens | Result |
| --- | --- | --- |
| owner_principles_review | Architecture, spec and no ceremony | No actionable findings |
| owner_lifecycle_review | Owner/alias lifecycle and reuse | File-only parse precondition added after adjudication |
| owner_format_review | Writer, format migration and tests | Fixture IDs and accidental text encoding corrected |
| owner_perf_review | Algorithms, storage, size and maximum capacity | No actionable findings |
| Claude Fable (`claude-fable-5-1`) | Full implementation, source and behavioral tests | APPROVE |

The parent verified the virtual-parse counterexample: allowing parse into a virtual
pack would let unregister remove an owner while leaving its aliases. Parsing now
asserts a file mount, with public/spec documentation and an assertion test. This
enforces the agreed file-pack ownership boundary without adding state or API.

Three atlas-bench entry fixtures omitted resource_id; their v3 constructors now
use nonzero IDs. Unicode in golden provenance files was restored from the baseline
while retaining the intentionally updated hashes and migration evidence.

Coverage gaps raised by reviewers were closed with independently authored tests
for self-owned equal ranges, alias-only AUX/PIN fallback and reload, and synchronous
PIN cleanup. A local real dump probe confirms that equal-range self-owners are not
reported as aliases, explicit aliases are reported, and invalid owner links fail.
The initial recovery test attempted to replace an already registered activator;
it was corrected to use one fake activator that fails once and then succeeds.

Fable's final response independently confirmed canonical preflight, suffix mapping,
owner-only activation/destruction, delayed failure publication, BLOB index zero,
per-name metadata, release invalidation, builder canonicalization and AUX/PIN paths.
Its raw response is local at `build/450-owner-fable.json`. Its unverified execution
claims are covered by the parent's [build, test and benchmark evidence](../resource-ownership-450.md).

Rejected expansion: no refcounts, object table, mirrored alias state, generations,
new test hooks, public ownership API or unrelated resolve rewrite. Existing
resolve allocation and unrelated BLOB validation/invalidation limitations remain
explicitly outside this migration.
