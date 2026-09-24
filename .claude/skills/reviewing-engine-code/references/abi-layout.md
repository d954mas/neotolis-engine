# Public ABI, layout, and persisted representation

Use this specialist when the branch changes a public struct, enum, status/error code
set, packed binary format, or any value that outlives one compilation: persisted to a
pack/file, embedded in a generated header, or consumed by code compiled against BASE.

Check:

- changed public struct field order, size, alignment, and default construction against
  every consumer that initializes, copies, or serializes it — compile the old usage
  mentally against HEAD;
- enum/status/error values that are persisted, exposed in a public header, or matched
  numerically anywhere: renumbering, insertion before existing values, and collisions
  with user-overridable or reserved ranges;
- capacity/limit macros that participate in a public contract: a consumer-visible
  change of an existing limit is a compatibility decision, not a tuning knob;
- packed/serialized format version bumps: every layout-affecting change must be
  reflected in the version/magic check that gates readers;
- static asserts or builder validation that pinned the old layout: removed or now
  vacuously true.

The wrongness is often the CHANGE, not the HEAD state — read the BASE side of the diff
for every touched public declaration before judging. Leave runtime size/footprint cost
to performance-size and general behavior to behavior-contracts.
