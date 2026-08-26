# meshoptimizer (index codec only)

Vendored subset of https://github.com/zeux/meshoptimizer

- Upstream commit: `b5b2c4391cb62d434d44e7f6ffb96194605fae2f` (v1.2, MESHOPTIMIZER_VERSION 1020)
- Files: `meshoptimizer.h`, `indexcodec.cpp`, `LICENSE.md` (MIT)

Only the triangle index codec is used (`meshopt_encodeIndexBuffer` /
`meshopt_decodeIndexBuffer`). The engine decodes; the builder encodes and
canonicalizes. Compiled solely inside `engine/meshwire` (duplicate-TU gate in
`scripts/check_crt_pins.sh`).

To update: replace the three files from upstream `src/` + `LICENSE.md`, update
the commit hash above, and re-verify that the builder's pinned
`meshopt_encodeIndexVersion(1)` still matches what the runtime decoder accepts.
