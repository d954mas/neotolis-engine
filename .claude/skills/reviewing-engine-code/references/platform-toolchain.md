# Platform and toolchain integration

Use this specialist when the branch changes how shipped code is selected, linked,
bridged, checked, or launched.

Trace configuration to the code that actually runs:

- CMake interface/stub/implementation composition and target/source selection;
- native, Web/WASM, debug, release, consumer, and CI configuration skew;
- link order, transitive dependencies, exported symbols, and feature defines;
- EM_JS/JS-C memory, Closure, helper dependencies, and callback lifetime;
- changed-file selection, formatting/tidy universe, test registration, exit status,
  cleanup, cache, and failure propagation in scripts/CI.

Build/CI is not an always-on lens. Report only a reachable shipped or gate outcome, not
style in configuration files. If a complex Web lifetime problem and an unrelated CI
routing problem coexist, use two narrow packets.
