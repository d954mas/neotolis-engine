# Measuring performance on phones

Rules for comparing two engine or game builds on a mobile device. They come from
the dynamic-upload study (#351, epic #589) on the reference phone, where a naive
A/B reported an 11% regression that was the GPU clock governor, not the engine.

Reference device: Huawei P40 ANA-NX9 (Kirin 990, Mali-G76 MP16, 600 MHz max,
Android 10), Chromium 156 (`org.chromium.chrome`), not rooted.

## Measure optimized builds

- Both arms are optimized builds (`-O2` or the release level) that differ only in
  the change. A game's instrumented web build can default to `-O0 -g`; its page
  thread then measures the debug build, and a 23 MB `-O0` wasm against a 9 MB
  `-O2` one shifts every CPU number. Record the build profile of each arm.

## Compare work per clock, not raw FPS

- **Record the GPU clock for every measured window** and compare **FPS per GPU
  MHz** next to FPS. A GPU-bound frame scales with the clock, and the clock is a
  governor decision.
- The Mali governor on this phone raises the clock when the client blocks on the
  GPU. A build that stalls by accident (a buffer rewrite waiting for draws) got
  7% more MHz and 6% more FPS than a stall-free build with identical FPS per MHz.
  Do not add waits to earn clock: it is vendor policy, paid in power and heat.
- **A frame locked to the vsync step hides the clock.** On a hot phone with vsync
  on, a frame that misses 60 Hz settles at two intervals (~30-32 FPS, p50 at
  33 ms); FPS then stays put while the governor moves the clock, so FPS per MHz
  reads the governor (#593: equal FPS, ~5% clock difference between windows).
  Compare GPU work at equal clock in vsync-off windows, and treat FPS per MHz only
  from windows that are GPU-bound and not locked.
- p95 under vsync is quantized to the refresh interval (16.6 / 33.2 ms here) and
  often cannot separate two arms; report p99 and the share of frames over 33 ms
  for pacing.
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

## Thread times and counters

- Chrome's GPU-process main thread (`CrGpuMain`: command-buffer decode and the
  driver calls) is a **CPU** thread on the phone's cores, not GPU time. Fewer GL
  calls shorten it and its long flushes, which line up with dropped frames; they
  change FPS only when that thread, not the GPU, limits the frame (#593: -24% on
  it with FPS per MHz unchanged on a fragment-bound frame).
- Measure thread times in separate traced windows: tracing perturbs the clean
  FPS and clock windows.
- Every GL call runs at `nt_gfx_end_frame`, so GL and upload counters only
  complete there; per-step deltas read zero. Attribute GL calls to steps through
  the capture's GPU segment debug groups (native GL only; web has none) and
  compare frame totals over enough consecutive frames to cover every phase of an
  alternating pass, such as a staggered shadow cascade.

## Micro-benchmarks

- A micro-benchmark with an idle GPU (clock at its 166 MHz floor) can invert a
  result: ring appends looked 3x slower than per-frame uploads there and faster
  under GPU load. Give the benchmark GPU work matching
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
| CPU clocks and load | `/sys/devices/system/cpu/cpu{0,4,6}/cpufreq/scaling_cur_freq` and `stats/time_in_state` per cluster, `/proc/stat` |
| Memory | `dumpsys meminfo <package>` and the same for the browser's GPU process (PSS varies 10-15% between windows) |
| Battery temperature | `dumpsys battery` |
| Temperatures | `dumpsys thermalservice`, section `Current temperatures from HAL` (the `Cached temperatures` list is stale) |
| Not available | sysfs thermal zones, battery current and voltage, GPU governor and `trans_stat`, WebGL GPU timer queries |

Energy is therefore not measured directly; equal clocks are the only proxy.

## Running on the device

- One driver at a time. Stopping a background task may leave its shell script
  running; check for leftover drivers before a new run, because two scripts
  restarting the browser corrupt each other's windows.
- Engine tools: `--frames N` on a real example prints frame time, draws,
  uploads, stream and frame storage peaks and a checksum on desktop.
  Game-level and phone probes live in the game repository.
