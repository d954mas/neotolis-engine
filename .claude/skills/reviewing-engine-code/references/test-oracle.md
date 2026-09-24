# Test oracle

Use this specialist when tests are a material proof surface, not merely because a
feature includes tests.

Ask whether a plausible broken implementation can still pass:

- does the test assert the changed outcome rather than only execute the path;
- do asymmetric, boundary, negative, repeated, and alternate-order cases distinguish
  the intended contract;
- can stale artifacts, skipped assertions, mocks, fixtures, or setup/teardown asymmetry
  create a false green;
- does a regression test fail on the original defect mechanism;
- do golden/reference values come from an independent oracle rather than the same code.

Do not demand exhaustive testing. Report a missing or false oracle only when tied to a
specific plausible implementation defect or escaped failure class.
