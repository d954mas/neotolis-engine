# Measuring performance on phones

Rules for comparing two engine or game builds on a mobile device. They come from
the dynamic-upload study (#351, epic #589) on the reference phone, where a naive
A/B reported an 11% regression that was the GPU clock governor, not the engine.

Reference device: Huawei P40 ANA-NX9 (Kirin 990, Mali-G76 MP16, 600 MHz max,
Android 10), Chromium 156 (`org.chromium.chrome`), not rooted.

## Compare work per clock, not raw FPS

- **Record the GPU clock for every measured window** and compare **FPS per GPU
  MHz** next to FPS. A GPU-bound frame scales with the clock, and the clock is a
  governor decision.
- The Mali governor on this phone raises the clock when the client blocks on the
  GPU. A build that stalls by accident (a buffer rewrite waiting for draws) got
  7% more MHz and 6% more FPS than a stall-free build with identical FPS per MHz.
  Do not add waits to earn clock: it is vendor policy, paid in power and heat.
- State which of FPS, frame-time p95, GPU MHz and FPS per MHz a claim rests on.

## Warm, sustained, interleaved

- **Pre-heat the phone** for about 5 minutes on the scene, then run the arms in
  **ABBA order, 5-10 minutes each**, sampling FPS, p95, GPU clock and
  temperatures every 10 seconds. Short windows on a cool phone measure the
  governor's boost, which fades once the shell warms up.
- Do not key cool-down on the GPU sensor: it drops within ~20 seconds after the
  page closes while the shell and battery stay warm. Pre-heat instead, and
  compare runs at similar shell temperature.
- Short ABBA windows (12 runs, 6 per arm) are still useful for GL calls,
  uploads and thread times; report them next to the sustained result.

## Micro-benchmarks

- A micro-benchmark with an idle GPU (clock at its 166 MHz floor) can invert a
  result: ring appends looked 3x slower than per-frame uploads there and faster
  under GPU load. Give the benchmark GPU work (`bench_stream` `load=`) matching
  the target scene, and **confirm every win in the real game** before designing
  on it (see also #587).
- Vsync caps the page at the display rate (60 Hz here), which hides any arm that
  stays above it. Chromium 156 reads
  `/data/local/tmp/chrome-command-line`; the line
  `_ --disable-gpu-vsync --disable-frame-rate-limit` uncaps it (force-stop the
  browser to apply, delete the file afterwards; Chrome Beta ignores it). Say
  which mode a table used.

## Sensors without root

| Signal | Source |
|---|---|
| GPU clock | `/sys/class/devfreq/gpufreq/cur_freq` (Hz) |
| Temperatures | `dumpsys thermalservice`, section `Current temperatures from HAL` (the `Cached temperatures` list is stale) |
| Not available | sysfs thermal zones, battery current and voltage, WebGL GPU timer queries |

Energy is therefore not measured directly; equal clocks are the only proxy.

## Running on the device

- One driver at a time. Stopping a background task may leave its shell script
  running; check for leftover drivers before a new run, because two scripts
  restarting the browser corrupt each other's windows.
- Engine tools: `examples/bench_stream` with `scripts/bench_stream.py serve --adb`
  (see [build](build.md)); it tags each window with GPU clock and HAL
  temperatures. Game-level probes live in the game repository.
