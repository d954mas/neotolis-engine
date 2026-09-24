# Pose Layout Experiment Results

**Date:** 2026-09-15
**Benchmark source:** `tools/research/skeletal_layout/main.c`
**Machine:** Windows 11 x86_64, Intel Core i9-14900HX
**Compiler / preset:** clang 19.1.7, `native-release` (`-O3 -DNDEBUG`, asserts in TRAP mode)
**Method:** synthetic `sample → mix → FK` over three pose storages with shared
arithmetic; per (J, C, T, layout) a warm-up frame, a batch grown until it clears
1 ms, then a measured batch of about 16.7 ms per stage; median of 5 repetitions
with the repetition loop outermost. Metric: **ns per skeleton joint per frame**
(normalized by C × J for every stage, so `sample` and `mix` scale visibly with T
while `FK` does not).

## Candidates

| Layout | Storage | FK |
|--------|---------|----|
| AoS 40 B | `nt_skeletal_trs_t[]` — the shipped ABI | `nt_skeletal_fk` |
| AoS 48 B | `{ _Alignas(16) float t[4]; float q[4]; float s[4]; }[]` | local, same expressions |
| SoA 10ch | ten `float[]` planes (tx ty tz qx qy qz qw sx sy sz) | local, same expressions |

Startup cross-check: the three FK outputs agree within 1e-5 on one character at
J = 100, T = 4; the tool aborts and prints the mismatch otherwise.

## Results

| J | C | T | Layout | sample | mix | FK | total |
|--:|--:|--:|:-------|-------:|----:|---:|------:|
| 30 | 1 | 1 | AoS 40 B | 4.00 | 4.60 | 6.64 | 15.25 |
| 30 | 1 | 1 | AoS 48 B | 4.00 | 3.64 | 6.69 | 14.33 |
| 30 | 1 | 1 | SoA 10ch | 6.36 | 6.22 | 6.94 | 19.52 |
| 30 | 100 | 1 | AoS 40 B | 3.88 | 4.64 | 6.59 | 15.11 |
| 30 | 100 | 1 | AoS 48 B | 3.87 | 3.66 | 6.68 | 14.21 |
| 30 | 100 | 1 | SoA 10ch | 6.39 | 6.37 | 6.89 | 19.65 |
| 30 | 1000 | 1 | AoS 40 B | 3.97 | 4.69 | 6.62 | 15.27 |
| 30 | 1000 | 1 | AoS 48 B | 3.89 | 3.65 | 6.59 | 14.13 |
| 30 | 1000 | 1 | SoA 10ch | 6.47 | 6.51 | 6.92 | 19.89 |
| 30 | 1 | 2 | AoS 40 B | 8.02 | 7.80 | 6.67 | 22.48 |
| 30 | 1 | 2 | AoS 48 B | 8.04 | 5.84 | 6.66 | 20.54 |
| 30 | 1 | 2 | SoA 10ch | 12.47 | 9.59 | 6.89 | 28.95 |
| 30 | 100 | 2 | AoS 40 B | 7.87 | 7.95 | 6.73 | 22.56 |
| 30 | 100 | 2 | AoS 48 B | 8.01 | 5.96 | 6.66 | 20.63 |
| 30 | 100 | 2 | SoA 10ch | 12.68 | 9.82 | 6.66 | 29.17 |
| 30 | 1000 | 2 | AoS 40 B | 7.89 | 8.10 | 6.52 | 22.50 |
| 30 | 1000 | 2 | AoS 48 B | 8.04 | 6.03 | 6.97 | 21.04 |
| 30 | 1000 | 2 | SoA 10ch | 12.55 | 9.86 | 6.77 | 29.18 |
| 30 | 1 | 4 | AoS 40 B | 16.39 | 15.36 | 6.70 | 38.45 |
| 30 | 1 | 4 | AoS 48 B | 16.04 | 12.63 | 6.81 | 35.48 |
| 30 | 1 | 4 | SoA 10ch | 26.18 | 17.49 | 7.15 | 50.83 |
| 30 | 100 | 4 | AoS 40 B | 15.65 | 15.79 | 6.77 | 38.21 |
| 30 | 100 | 4 | AoS 48 B | 15.92 | 13.03 | 6.65 | 35.60 |
| 30 | 100 | 4 | SoA 10ch | 25.65 | 17.47 | 6.89 | 50.01 |
| 30 | 1000 | 4 | AoS 40 B | 16.10 | 15.69 | 6.76 | 38.55 |
| 30 | 1000 | 4 | AoS 48 B | 16.01 | 13.24 | 6.58 | 35.83 |
| 30 | 1000 | 4 | SoA 10ch | 26.41 | 18.13 | 6.97 | 51.51 |
| 60 | 1 | 1 | AoS 40 B | 3.92 | 4.88 | 6.71 | 15.51 |
| 60 | 1 | 1 | AoS 48 B | 3.98 | 3.55 | 6.71 | 14.24 |
| 60 | 1 | 1 | SoA 10ch | 6.30 | 6.12 | 6.84 | 19.26 |
| 60 | 100 | 1 | AoS 40 B | 4.08 | 4.76 | 6.64 | 15.47 |
| 60 | 100 | 1 | AoS 48 B | 3.98 | 3.55 | 6.65 | 14.18 |
| 60 | 100 | 1 | SoA 10ch | 6.11 | 6.42 | 6.67 | 19.20 |
| 60 | 1000 | 1 | AoS 40 B | 3.87 | 4.76 | 6.95 | 15.59 |
| 60 | 1000 | 1 | AoS 48 B | 4.02 | 3.97 | 6.97 | 14.96 |
| 60 | 1000 | 1 | SoA 10ch | 6.51 | 6.62 | 6.90 | 20.03 |
| 60 | 1 | 2 | AoS 40 B | 7.73 | 7.72 | 6.67 | 22.12 |
| 60 | 1 | 2 | AoS 48 B | 7.86 | 5.92 | 6.67 | 20.46 |
| 60 | 1 | 2 | SoA 10ch | 12.83 | 9.76 | 6.80 | 29.39 |
| 60 | 100 | 2 | AoS 40 B | 7.77 | 7.96 | 6.88 | 22.61 |
| 60 | 100 | 2 | AoS 48 B | 7.91 | 5.92 | 6.69 | 20.53 |
| 60 | 100 | 2 | SoA 10ch | 12.21 | 9.83 | 7.11 | 29.15 |
| 60 | 1000 | 2 | AoS 40 B | 7.71 | 8.07 | 6.71 | 22.49 |
| 60 | 1000 | 2 | AoS 48 B | 7.75 | 6.07 | 6.77 | 20.60 |
| 60 | 1000 | 2 | SoA 10ch | 12.60 | 10.08 | 6.87 | 29.55 |
| 60 | 1 | 4 | AoS 40 B | 15.67 | 15.17 | 6.77 | 37.62 |
| 60 | 1 | 4 | AoS 48 B | 16.07 | 12.64 | 6.69 | 35.40 |
| 60 | 1 | 4 | SoA 10ch | 24.90 | 17.20 | 7.11 | 49.21 |
| 60 | 100 | 4 | AoS 40 B | 15.66 | 15.52 | 6.70 | 37.87 |
| 60 | 100 | 4 | AoS 48 B | 15.64 | 12.67 | 6.78 | 35.10 |
| 60 | 100 | 4 | SoA 10ch | 24.68 | 18.22 | 6.90 | 49.81 |
| 60 | 1000 | 4 | AoS 40 B | 15.41 | 16.05 | 6.68 | 38.14 |
| 60 | 1000 | 4 | AoS 48 B | 15.60 | 13.22 | 6.81 | 35.63 |
| 60 | 1000 | 4 | SoA 10ch | 25.31 | 18.27 | 6.99 | 50.57 |
| 100 | 1 | 1 | AoS 40 B | 3.93 | 4.58 | 7.08 | 15.59 |
| 100 | 1 | 1 | AoS 48 B | 4.01 | 3.53 | 6.58 | 14.12 |
| 100 | 1 | 1 | SoA 10ch | 6.44 | 6.30 | 7.12 | 19.86 |
| 100 | 100 | 1 | AoS 40 B | 3.94 | 4.82 | 6.70 | 15.46 |
| 100 | 100 | 1 | AoS 48 B | 3.98 | 3.66 | 6.61 | 14.25 |
| 100 | 100 | 1 | SoA 10ch | 6.33 | 6.42 | 6.92 | 19.67 |
| 100 | 1000 | 1 | AoS 40 B | 3.91 | 4.79 | 6.64 | 15.34 |
| 100 | 1000 | 1 | AoS 48 B | 4.00 | 3.71 | 6.83 | 14.54 |
| 100 | 1000 | 1 | SoA 10ch | 6.38 | 6.76 | 7.34 | 20.48 |
| 100 | 1 | 2 | AoS 40 B | 7.77 | 7.78 | 6.63 | 22.18 |
| 100 | 1 | 2 | AoS 48 B | 7.87 | 5.78 | 6.56 | 20.22 |
| 100 | 1 | 2 | SoA 10ch | 12.91 | 9.80 | 7.04 | 29.75 |
| 100 | 100 | 2 | AoS 40 B | 7.71 | 8.32 | 6.81 | 22.84 |
| 100 | 100 | 2 | AoS 48 B | 7.66 | 6.01 | 6.80 | 20.47 |
| 100 | 100 | 2 | SoA 10ch | 12.49 | 9.95 | 6.80 | 29.23 |
| 100 | 1000 | 2 | AoS 40 B | 7.90 | 8.32 | 6.79 | 23.01 |
| 100 | 1000 | 2 | AoS 48 B | 7.95 | 6.36 | 6.92 | 21.24 |
| 100 | 1000 | 2 | SoA 10ch | 12.51 | 10.01 | 7.03 | 29.55 |
| 100 | 1 | 4 | AoS 40 B | 15.62 | 16.44 | 7.00 | 39.05 |
| 100 | 1 | 4 | AoS 48 B | 15.50 | 12.87 | 6.71 | 35.09 |
| 100 | 1 | 4 | SoA 10ch | 24.47 | 17.18 | 7.06 | 48.71 |
| 100 | 100 | 4 | AoS 40 B | 15.47 | 15.99 | 6.66 | 38.11 |
| 100 | 100 | 4 | AoS 48 B | 15.30 | 12.71 | 6.81 | 34.82 |
| 100 | 100 | 4 | SoA 10ch | 24.55 | 18.29 | 6.80 | 49.64 |
| 100 | 1000 | 4 | AoS 40 B | 15.50 | 16.04 | 7.12 | 38.66 |
| 100 | 1000 | 4 | AoS 48 B | 15.58 | 13.31 | 6.99 | 35.88 |
| 100 | 1000 | 4 | SoA 10ch | 25.12 | 18.92 | 6.97 | 51.01 |

## Reading

1. **Sample and FK show no consistent AoS winner.** Across the published matrix,
   AoS48 sample time differs from AoS40 by −2.5 % to +3.9 %. FK spans
   6.52–7.34 ns across all three layouts, with no consistent ranking.
2. **The padded 48 B layout consistently wins in mix**, by 15.6–27.8 %: 3.66 vs
   4.82 ns at T=1, 6.01 vs 8.32 at T=2, 12.71 vs 15.99 at T=4 (J=100, C=100). Both AoS
   layouts use compiler-generated SIMD in the native Release build, including
   vector quaternion arithmetic at the 40-byte stride. These timings do not
   isolate the cause of the gap; alignment alone is not an established explanation.
3. **AoS48 reduces the total column by 4.0–10.4 %**, primarily through mix,
   with 20 % more pose memory in every local buffer, snapshot and scratch the
   game owns. Total is the sum of stage medians, not a measured full-frame time.
4. **SoA is slower in sample, mix and summed total.** Total is 24.1–34.1 %
   above AoS40; FK does not establish a consistent winner. This layout accesses
   ten separate planes without explicit SIMD across joints. Native compiler
   SIMD within a joint is already present; this is not a baseline-WASM measurement.
5. **Cost varies with joint and character count.** At J=60, T=1, changing
   C=1 to C=1000 raises AoS48 mix from 3.55 to 3.97 ns (+11.8 %). This timing
   matrix alone does not identify whether computation or memory bandwidth limits
   performance.
6. **Nothing here justifies moving away from AoS 40 B before the real kernels
   exist.** The single win is a synthetic stand-in for the §7.3 mix (no joint
   weights, no zero-total path, constant gains), it is confined to one stage, and
   SIMD mix kernels would reopen the alignment question for both AoS variants at
   once.
7. **Small differences need repeated measurements.** These medians do not
   establish confidence intervals. Repetitions interleave layouts in a fixed
   order, so timing drift can still favour one layout. AoS40 also pays the public
   `nt_skeletal_fk` call and its configured checks, unlike the local AoS48/SoA FKs.
   Checked builds add parent-index and TRS validation only to the AoS40 FK path;
   use the recorded production Release configuration for comparisons.
8. **Decision: keep AoS 40 B as the initial ABI.** Re-run this tool against the
   real kernels in #487 and revisit the layout in #492, where a SIMD mix is the
   deciding measurement rather than this one.

## Engine mix kernel (2026-09-24)

The AoS40 mix stage now calls the public `nt_skeletal_mix`; AoS48 and SoA keep
the local stand-in, so from here on the mix column compares kernels as well as
layouts, and conclusion 2 can no longer be re-checked with this tool as is.
Checked builds (`NT_SKELETAL_CHECKS=ON`) also validate every contributing pose
in the AoS40 mix, as conclusion 7 notes for FK; compare `native-release` only.
The table gains a `mix/input` column: mix / T, an average that includes the
per-joint fixed cost (seed sign, normalization, stores).

A separate A/B run put the stand-in and the kernel in one binary over the same
data (J=60, C=1000, `-O3`, TRAP, checks off), with two data sets: random unit
quaternions (inputs land in either hemisphere, the largest component varies)
and smooth ones (w dominant everywhere). ns/(joint·input), medians:

| data | T | stand-in | kernel, branch on dot sign | + `copysignf` | + seed sign of w (shipped) |
|:-----|--:|---------:|---------------------------:|--------------:|---------------------------:|
| random | 1 | 8.6 | 6.3 | 6.3 | 5.0 |
| random | 4 | 4.3 | 7.9 | 4.4 | 10–15 % below the previous column |
| smooth | 1 | 3.6 | 6.3 | 6.3 | 4.9 |
| smooth | 4 | 3.5 | not measured | 4.5 | 10–15 % below the previous column |

The shipped column comes from runs under background load, compared within
one binary; the A/B harness was a scratch tool and is not committed.

- The dot-sign branch mispredicts whenever inputs sit in both hemispheres;
  `copysignf` removes it, 1.8x at T=4 on random data. The kernel ships with it.
- The T=1 cost was the canonical sign of the first contributor: a kernel
  without it matched the stand-in. The stand-in's branchy search is faster
  only when its branches predict (smooth data) and slower when they do not
  (random data). Rewriting the search (fabsf, selects, an fmaxf tree) did not
  help; changing the seed rule to "w positive, largest component only for
  w == 0" did. Same binary, three runs under background load: T=1 random
  6.3 -> 5.0, smooth 6.3 -> 4.9 (stand-in 9.3 / 3.7); T=4 10-15 % lower in
  every run, absolute values too noisy to quote.
- An `NT_ASSERT_MODE=0` build measures the same as TRAP within noise, with and
  without joint weights, so the per-call checks and the per-element
  `weight >= 0` check stay below the noise floor (~0.2 ns).
- The earlier AoS40 figures in this file came from a different session; only
  same-binary comparisons are meaningful at this scale.
