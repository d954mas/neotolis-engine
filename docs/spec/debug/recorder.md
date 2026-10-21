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
offline and frame-exact (the game advances one fixed step per frame however slow the encoder is);
with RUN it records hand play in real time.

`start` copies `name` and fixes the size to the window framebuffer, rounded down to even. It returns
false (logged) when the framebuffer is empty, the platform has no encoder, or the output cannot be
opened; it asserts while a recording is active. `frame`, `push_audio` and `stop` are no-ops while idle.
A framebuffer size change or an encoder failure ends the recording on the spot with a WARN; a later
`stop` returns 0. `stop` returns the frame count and logs the frames and the audio samples padded
and trimmed.

## Frame selection

Output frame `i` has pts `i / fps` and is due at `t0 + (i + 1) / fps`, where `t0` is the start time:
the first frame shows the result of the first advance, and `N` lockstep steps of `1 / fps` give `N`
frames. A `frame` call takes every due deadline within half a frame of `t` (half of `dt`, at most
half a period). That picks the presented frame nearest each deadline and absorbs float `step_dt` that
rounds below `1 / fps` (`1/50`, `1/25`). Several deadlines in one call (game slower than `fps`, a
clamped hitch) re-encode the same pixels; frozen time (pause, MANUAL idle) and skipped frames
(render off) write nothing. `step_dt = 1 / (fps · k)` records every k-th step.

## Audio

The owner of the mix (today a game audio module) pushes interleaved stereo float samples at 48 kHz
from the main thread, for the time since the previous `frame` call. File time 0 is `t0 + 1 / fps`
for both tracks, so a sound and the frame showing its cause share a timestamp. At each `frame` call
the queued samples are trimmed or padded with silence so the track reaches the caller's time; at
`stop` the track is padded to the video length. A track is always written, silent when nothing is
pushed. A lockstep mixer renders `step_dt · 48000` samples per step without a sound device; a
real-time mixer copies its device output to the main thread. Pushes beyond one second between two
`frame` calls are trimmed.

## Backends

**Web.** `VideoFrame` from the canvas inside the rAF task, before the swap (the context is
`preserveDrawingBuffer:false`, so the drawing buffer still holds the frame), into `VideoEncoder`
(H.264 High, the lowest level that fits size × fps, bitrate `0.1 · w · h · fps`) and
`AudioEncoder` (AAC-LC 192 kbps). The vendored Mediabunny muxer (`deps/mediabunny`) writes the MP4
and compensates the AAC priming delay. The JS lives in `web/nt_recorder_web.js` and loads with
`--extern-pre-js`, outside Closure. Encoded bytes stay in tab memory (~1 MB/s at 1080p30): a
dev-only exception to preallocated storage; long web recordings are not a goal. After `stop`, when
the encoders flush, the window event `nt-recorder` (cancelable, `detail {name, blob, frames, error}`)
is dispatched; unless a listener cancels it, the browser downloads `blob` as `name`. A browser
without WebCodecs makes `start` return false; a codec the browser rejects ends the recording and
the event carries `error`.

**Other platforms** define no `nt_recorder` target; a Windows Media Foundation backend is planned.

## devapi `record.*`

`NT_DEVAPI_GROUP_RECORDER` (OFF) links `nt_recorder` and adds two commands; the host calls
`nt_recorder_frame` every frame (a host that does not records nothing).

| Command | Params | Result | Errors |
|---|---|---|---|
| `record.start` | `{name, fps}` | `{}` | `bad_params` (empty or >255-byte name, fps outside 1..120), `busy` (active), `failed` (no encoder or output) |
| `record.stop` | `{}` | `{frames}` | none; `frames` is 0 when nothing was active |

`record.start` uses `g_nt_app.time`. An offline recording is `time.set_mode{mode:"manual"}`,
`record.start`, `time.step{count}`, `record.stop`; the default `step_dt` of `1/60` at `fps` 30 records
every second step.
