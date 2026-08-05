# State, data, and lifecycle

Use this specialist for identity, ownership, representation, and meaning across time or
boundaries.

Trace each relevant chain end to end:

```text
producer -> transform/serialization -> stored/shared representation -> consumer
```

Check:

- cache keys and invalidation cover every consumer-visible field;
- aliases, handles, spans, offsets, and shared payload retain the correct identity;
- ownership, pinning, cleanup, reset, mount/unmount, and deferred callbacks agree;
- finite transforms, masks, ordering, coordinate conversions, and serialization preserve
  meaning for every relevant input class;
- a valid local representation satisfies downstream exact-pattern contracts;
- on every failure, cancel, or partial-completion path, name the fate of ALL side
  effects already committed by this run or a prior successful run — files on disk,
  caches, registries, handles, accumulators, rings. A failure path that leaves a
  previously produced artifact or populated state in place, where a consumer can
  later mistake it for the current run's output, is a finding even when the failing
  operation itself reports its error correctly.

When two risks require unrelated chains, request separate narrow packets using this same
reference. Do not report numeric API validation or general behavior unless the root
cause is state, representation, identity, or lifetime.
