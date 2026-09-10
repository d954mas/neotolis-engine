# Performance and size

Use this specialist only when performance, memory/resource cost, or binary size is a
material changed contract.

Trace work on the actual execution path. Check:

- heap allocation, hidden growth, copies, indirection, and unpredictable access in hot
  paths;
- new per-frame/per-item work, batching fragmentation, GL state churn, and dense-loop
  regressions;
- lifetime and peak size of preallocated, scratch, GPU, and pack resources;
- runtime dependencies, large static data, parsers, formatting, or debug machinery that
  increase shipped WASM size;
- an optimization switch actually removes the promised work.

Do not flag compile-time caps, preallocation, or long inlinable functions merely because
generic advice prefers dynamic containers or smaller translation units. Name the
reachable workload and violated Engine budget/rule.
