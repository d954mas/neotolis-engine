# Concurrency and reentrancy

Use this rare specialist only when the branch changes threads, jobs, callbacks,
cancellation, or reentrant execution.

Trace ownership and state transitions across scheduling boundaries. Check ordering,
publication, races, double completion, callback-after-teardown, cancellation, reentry,
and failure paths. Name an executable interleaving; do not report abstract
"thread-safety" concerns without one.
