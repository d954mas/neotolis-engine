# Untrusted input

Use this rare specialist only when changed code consumes adversarial or corrupt external
data.

Trace bytes/values from boundary through validation, allocation/index arithmetic,
representation changes, and final use. Check truncation, overflow, type confusion,
embedded NUL, trailing data, decompression/size claims, path handling, and partial
failure cleanup. Respect the Engine's assert policy: programmer invariants may assert;
untrusted/runtime input needs the documented recoverable or hard guard.

Report only a reachable malformed input and concrete outcome. Do not perform a generic
security audit of unrelated code.
