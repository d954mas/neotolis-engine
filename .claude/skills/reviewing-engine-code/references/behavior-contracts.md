# Behavior and contracts

This is the permanent fresh-reviewer perspective.

Compare stated intent and relevant spec with observable behavior. Trace changed public
and internal contracts through callers and consumers:

- success, error, empty, boundary, feature-off, and teardown paths;
- state transitions and repeated/alternate operation order;
- public field domains, counts, capacities, masks, and enum coverage;
- assertion policy, recoverable errors, and untrusted-boundary guards;
- compatibility with unchanged callers and data;
- tests as evidence for the claimed behavior.

Prioritize these completion checks before lower-impact edge cases:

1. For every changed public struct, identify the exact promised initializer/compatibility
   contract, compare BASE field order/default construction, and compile the old usage
   mentally against HEAD. Do not infer a promise from a different struct.
2. For changed enum/mask/domain fields, enumerate zero, every declared value, boundaries,
   and unchanged callers through validation, storage, and consumers.
3. For changed error collection, match logical failing instances to error identity,
   payload, ordering, truncation, and public observability.
4. For feature-off controls, prove both behavior and promised expensive work are bypassed.
5. Place recoverable bounds/size guards before the expensive allocation, decode, scan,
   narrowing, or serialization they promise to protect against.

Look at tests, but do not conduct a full test-oracle audit unless that specialist is
selected. Exclude architecture taste, deep hot-path cost, and build/platform mechanics
owned by selected specialists.
