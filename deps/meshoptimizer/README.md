# meshoptimizer (index codec, TEST-ONLY reference)

Vendored subset of https://github.com/zeux/meshoptimizer

- Upstream commit: `b5b2c4391cb62d434d44e7f6ffb96194605fae2f` (v1.2, MESHOPTIMIZER_VERSION 1020)
- Files: `meshoptimizer.h`, `indexcodec.cpp`, `LICENSE.md` (MIT)

The engine ships a C port of the triangle index codec (`engine/meshwire`,
stream format v1). This upstream C++ copy is compiled ONLY into
`test_meshwire_diff` (see `tests/CMakeLists.txt`), which pins byte-parity:
our encoder must produce byte-identical streams and both decoders must agree
on both encoders' output. A gate in `scripts/check_crt_pins.sh` keeps it out
of shipping targets.

To update: replace the three files from upstream `src/` + `LICENSE.md`, update
the commit hash above, and re-run `test_meshwire_diff` — if upstream changes
the v1 stream format (it is frozen by design), parity failures show exactly
where.
