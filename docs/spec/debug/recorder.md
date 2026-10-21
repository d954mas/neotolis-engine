# Video Recorder

`nt_recorder` is a dev-only module that records the presented frames and an audio track into an MP4
(H.264 + AAC, or Opus where the browser has no AAC encoder; 48 kHz stereo) at a constant frame rate.
It replaces an external screen recorder for debugging, iteration and gameplay clips. A game that does
not link it pays nothing; like `nt_crash` it has no option and no stub. The engine has no audio
mixer of its own: the track holds only what the game pushes.

Related: [Logging, Errors, Debugging](logging-errors-debugging.md) (devapi, frame capture), [Frame Lifecycle](../runtime/frame-lifecycle.md)

## API and call placement

```c
bool nt_recorder_start(const char *name, uint32_t fps, double t);
void nt_recorder_frame(double t, double dt);
void nt_recorder_push_audio(const float *stereo48k, uint32_t frames, double t_start);
uint32_t nt_recorder_stop(void);
bool nt_recorder_active(void);
```

The game calls `nt_recorder_frame` once per presented frame, after `nt_gfx_end_frame` and before
`nt_window_swap_buffers`, and passes its clock: `t` is the time of this frame, `dt` its advance.
There is no hook and no hidden clock. With `g_nt_app.time` and MANUAL lockstep the recording is
offline and frame-exact as long as the encoder keeps up with the stepping (see Backends); with RUN it
records hand play in real time.

`start` fixes the size to the window framebuffer, rounded down to even. It returns false (logged)
when that leaves less than 2x2, the platform has no encoder, or the output cannot be opened; it
asserts while a recording is active. `frame`, `push_audio` and `stop` are no-ops while idle. `stop`
returns the frame count, 0 when the final audio write failed. A recording also ends on the spot,
with a WARN, on a framebuffer size change or a `frame` call more than two seconds of frames after
the previous one (render off while time ran), both with the audio padded to the video, and on an
encoder failure; a later `stop` returns 0.

## Frame selection

Output frame `i` has pts `i / fps` and is due at `t0 + (i + 1) / fps`, where `t0` is the start time:
the first frame shows the result of the first advance, and `N` lockstep steps of `1 / fps` give `N`
frames. A `frame` call takes every due deadline within a margin of `t`: half of `dt`, at most half
a period. With an even frame rate that picks the presented frame nearest each deadline and absorbs
float `step_dt` that rounds below `1 / fps` (`1/50`, `1/25`); when the game runs slower than `fps`
the margin caps at half a period, so a deadline goes to the first frame within it. Several
deadlines in one call (game slower than `fps`, a clamped hitch, a short render-off gap) re-encode
the same pixels. Frozen time (pause, MANUAL idle) writes nothing. `step_dt = 1 / (fps · k)` records
every k-th step.

## Audio

The owner of the mix pushes interleaved stereo float samples at 48 kHz from the main thread, each
block with its start time `t_start` on the same clock; sample `s` of a block plays at file time
`t_start - t0 + s / 48000`. A game mixer that starts a sound at the beginning of the block for the
step that first shows its cause (`t_start = t - dt`) plays it with that frame. A lockstep mixer
renders `step_dt · 48000` samples per step without a sound device; a real-time mixer copies its
device output to the main thread. Sounds played through `nt_audio` are not captured.

Blocks are placed by their time. A block within one mixer block (1024 samples) of where the
previous push ended continues it, queued or already written, so rounding and drift never split a
mixer's stream. Once silence has been written past the previous push (the mixer paused), a block
keeps its exact time. An overlap with placed audio, and anything before `t0`, is dropped, and a
gap becomes silence. Audio is written only up to the end of the written video, so the track never
runs ahead of the picture; the rest waits in a queue of 3 s plus two blocks (the catch-up bound
plus a period at 1 fps), and a push that does not fit ends the recording (WARN), as the next
`frame` call would. With nothing queued, silence is written up to 250 ms short of the video, which
leaves room for a real-time mixer whose blocks reach the main thread late. A track is always
written, silent when nothing is pushed; at the end it is padded to the video length.

## Backends

**Web.** `VideoFrame` from the canvas inside the rAF task, before the swap (the context is
`preserveDrawingBuffer:false`, so the drawing buffer still holds the frame), into `VideoEncoder`
and `AudioEncoder`. Codecs are probed once when the script loads: H.264 High, else Constrained
Baseline, at the lowest level that fits size × fps (bitrate `0.1 · w · h · fps`); AAC-LC 192 kbps,
else Opus (Firefox, Chrome on Linux; editors handle Opus in MP4 poorly). `start` returns false
while the probe runs (a console warning says so; start again), and in a browser without WebCodecs,
an H.264 encoder or an AAC/Opus encoder. The vendored Mediabunny muxer (`deps/mediabunny`) writes the MP4. The
JS lives in `web/nt_recorder_web.js` and loads with `--extern-pre-js`, outside Closure; a page that
links the recorder evaluates the 125 KB muxer at load. One module per page, main thread.

Encoded bytes stay in tab memory (~1 MB/s at 1080p30): a dev-only exception to preallocated
storage; long web recordings are not a goal. Nothing paces the game to the encoder, so an encoder
queue above four seconds of frames (two of backlog plus one call's catch-up) ends the recording
(`encoder backlog`); lockstep must not step much faster than the encoder runs. After `stop`, when
the encoders flush, the window event `nt-recorder` (cancelable, `detail {name, blob, frames,
error}`) is dispatched after the C call returns; unless a listener cancels it, the browser
downloads `blob` as `name` (Chrome asks before a second automatic download from a page). A codec,
muxer or backlog failure, or a throw such as a lost context, ends the recording at the next call
into the backend, or, after the last one, shows only as `error` in the event; the event is the
authority for whether a file exists.

**Other platforms** define no `nt_recorder` target; a Windows Media Foundation backend is planned.

## devapi `record.*`

`NT_DEVAPI_GROUP_RECORDER` (OFF) links `nt_recorder` and adds two commands; the host calls
`nt_recorder_frame` for every presented frame (a host that does not records nothing).

| Command | Params | Result | Errors |
|---|---|---|---|
| `record.start` | `{name, fps}` | `{}` | `bad_params` (empty name, fps outside 1..120), `busy` (active), `failed` (`start` returned false) |
| `record.stop` | `{}` | `{frames}` | none; `frames` is 0 when nothing was active |

`record.start` uses `g_nt_app.time`. An offline recording is `time.set_mode{mode:"manual"}`,
`record.start`, `time.step{count}`, `record.stop`; the default `step_dt` of `1/60` at `fps` 30 records
every second step. `tests/browser/record.spec.ts` runs exactly that on `devapi_host` (built with the
group) and checks the downloaded file with `tools/devapi/check_recording.py` (needs python and
ffmpeg: streams, frame count, flash/beep offset within a frame).
