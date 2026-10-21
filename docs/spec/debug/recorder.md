# Video Recorder

`nt_recorder` is a dev-only module that records the presented frames and an audio track into an MP4
(H.264 + AAC 48 kHz stereo) at a constant frame rate. It replaces an external screen recorder for
debugging, iteration and gameplay clips. A game that does not link it pays nothing; like `nt_crash`
it has no option and no stub.

Related: [Logging, Errors, Debugging](logging-errors-debugging.md) (devapi, frame capture), [Frame Lifecycle](../runtime/frame-lifecycle.md)

## API and call placement

```c
bool nt_recorder_start(const char *name, uint32_t fps, double t);
void nt_recorder_frame(double t, double dt);
void nt_recorder_push_audio(const float *stereo48k, uint32_t frames);
uint32_t nt_recorder_stop(void);
bool nt_recorder_active(void);
```

The game calls `nt_recorder_frame` once per presented frame, after `nt_gfx_end_frame` and before
`nt_window_swap_buffers`, and passes its clock: `t` is the time of this frame, `dt` its advance.
There is no hook and no hidden clock. With `g_nt_app.time` and MANUAL lockstep the recording is
offline and frame-exact; with RUN it records hand play in real time.

`start` fixes the size to the window framebuffer, rounded down to even. It returns false (logged)
when that leaves less than 2x2, the platform has no encoder, or the output cannot be opened; it
asserts while a recording is active. `frame`, `push_audio` and `stop` are no-ops while idle. A
framebuffer size change or an encoder failure ends the recording on the spot with a WARN; a later
`stop` returns 0. `stop` returns the frame count (0 when the final audio write failed) and logs the
frames and the audio samples padded and dropped.

## Frame selection

Output frame `i` has pts `i / fps` and is due at `t0 + (i + 1) / fps`, where `t0` is the start time:
the first frame shows the result of the first advance, and `N` lockstep steps of `1 / fps` give `N`
frames. A `frame` call takes every due deadline within a margin of `t`: half of `dt`, at most half
a period. With an even frame rate that picks the presented frame nearest each deadline and absorbs
float `step_dt` that rounds below `1 / fps` (`1/50`, `1/25`); when the game runs slower than `fps`
the margin caps at half a period, so a deadline goes to the first frame within it. Several
deadlines in one call (game slower than `fps`, a clamped hitch, the first call after render off
while time ran) re-encode the same pixels. Frozen time (pause, MANUAL idle) writes nothing.
`step_dt = 1 / (fps · k)` records every k-th step.

## Audio

The owner of the mix (today a game audio module) pushes interleaved stereo float samples at 48 kHz
from the main thread, before the `frame` call, for that frame's advance. The track starts at `t0`: a
block mixed for the advance from `t - dt` to `t` plays from file time `t - dt - t0`, so a sound a
game mixer starts in the step that first shows its cause plays with that frame. Samples pushed at a
call whose time is at or before `t0` are dropped. Mixers push in blocks that rarely match a frame,
so a lag or surplus up to one frame carries over; beyond that the track is padded with silence or
the oldest surplus dropped, which keeps it on the caller's time. At `stop` the track is cut or
padded to the video length. A track is always written, silent when nothing is pushed. A lockstep
mixer renders `step_dt · 48000` samples per step without a sound device; a real-time mixer copies
its device output to the main thread. Pushes beyond one second between two `frame` calls are
dropped.

## Backends

**Web.** `VideoFrame` from the canvas inside the rAF task, before the swap (the context is
`preserveDrawingBuffer:false`, so the drawing buffer still holds the frame), into `VideoEncoder`
and `AudioEncoder`. Codecs are probed once when the script loads: H.264 High, or Constrained
Baseline where the browser has no High encoder, at the lowest of levels 3.1, 4.0, 4.2, 5.1, 5.2,
6.0, 6.2 that fits size × fps (bitrate `0.1 · w · h · fps`); AAC-LC 192 kbps, or Opus where the
browser has no AAC encoder (Firefox, Chrome on Linux; editors handle Opus in MP4 poorly). The
vendored Mediabunny muxer (`deps/mediabunny`) writes the MP4. The JS lives in
`web/nt_recorder_web.js` and loads with `--extern-pre-js`, outside Closure; a page that links the
recorder evaluates the 125 KB muxer at load. One module per page, main thread.

Encoded bytes stay in tab memory (~1 MB/s at 1080p30): a dev-only exception to preallocated
storage; long web recordings are not a goal. Nothing paces the game to the encoder, so an encoder
queue above two seconds of frames ends the recording (`encoder backlog`). After `stop`, when the
encoders flush, the window event `nt-recorder` (cancelable, `detail {name, blob, frames, error}`)
is dispatched; unless a listener cancels it, the browser downloads `blob` as `name`. A browser
without WebCodecs makes `start` return false. A codec, muxer or backlog failure ends the recording
at the next `frame` call, or, after the last one, shows only as `error` in the event; the event is
the authority for whether a file exists.

**Other platforms** define no `nt_recorder` target; a Windows Media Foundation backend is planned.

## devapi `record.*`

`NT_DEVAPI_GROUP_RECORDER` (OFF) links `nt_recorder` and adds two commands; the host calls
`nt_recorder_frame` every frame (a host that does not records nothing).

| Command | Params | Result | Errors |
|---|---|---|---|
| `record.start` | `{name, fps}` | `{}` | `bad_params` (empty name, fps outside 1..120), `busy` (active), `failed` (no encoder or output) |
| `record.stop` | `{}` | `{frames}` | none; `frames` is 0 when nothing was active |

`record.start` uses `g_nt_app.time`. An offline recording is `time.set_mode{mode:"manual"}`,
`record.start`, `time.step{count}`, `record.stop`; the default `step_dt` of `1/60` at `fps` 30 records
every second step. `tests/browser/record.spec.ts` runs exactly that on `devapi_host` and checks the
file with `tools/devapi/check_recording.py` (streams, frame count, flash/beep offset within a frame).
