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
void nt_recorder_push_audio(const float *stereo48k, uint32_t frames);
uint32_t nt_recorder_stop(void);
bool nt_recorder_active(void);
```

The game calls `nt_recorder_frame` once per presented frame, after `nt_gfx_end_frame` and before
`nt_window_swap_buffers`, and passes its clock: `t` is the time of this frame, `dt` its advance.
There is no hook and no hidden clock. With `g_nt_app.time` and MANUAL lockstep the recording is
offline and frame-exact as long as the encoder keeps up with the stepping (see Backends); with RUN it
records hand play in real time. `start`'s `t` is where the audio stream begins: call it between
steps, so the first pushed samples are those of the next step (devapi does), or pass that step's
start time.

`start` fixes the size to the window framebuffer, rounded down to even. It returns false (logged)
when that leaves less than 2x2, the platform has no encoder, or the output cannot be opened; it
asserts while a recording is active. `frame`, `push_audio` and `stop` are no-ops while idle. `stop`
returns the frame count, 0 when the final audio write failed. A recording also ends on the spot,
with a WARN, on a change of the even-rounded framebuffer size or a `frame` call more than two
seconds of frames after the previous one (render off while time ran), both with the audio padded to
the video, and on an encoder failure, which writes no file; a later `stop` returns 0.

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

The game's mixer pushes its output as one stream of interleaved stereo float samples at 48 kHz from
the main thread, each step's samples before the `frame` call for that step. The stream carries no
timestamps and the recorder corrects nothing: until the first push each `frame` call writes silence
up to its `t`, and from then on sample `n` of the stream follows sample `n − 1`. So the mixer pushes
every step's samples once it starts, silence included: a lockstep mixer renders
`llround(t · 48000) − llround((t − dt) · 48000)` samples per step, which never drifts from the clock.
A mixer whose first push is the step right after a `frame` call starts on time; one whose first push
comes later (a second step of the same frame, or render off) starts early by the steps since that
call, which the drift WARN reports past 100 ms. A game without sound pushes nothing and gets a
silent track.

A sound the mixer starts at the beginning of a step plays at that step's time. With `step_dt =
1 / fps` it plays with the frame that first shows its cause; with more steps per frame it can come up
to one frame period after that frame's pts, as the frame shows the nearest step. A gap or a mixer on
another clock shifts the rest of the track; when the pushed stream differs from the time passed to
`frame` by more than 100 ms (one mixer block and a step fit well inside), a WARN says so once per
recording. A real-time mixer that renders on an audio thread is the game's to align: it pushes the
samples rendered for each step on the game clock. Sounds played through `nt_audio` are not captured.

Pushed samples go to the encoder at once, so the track may run ahead of the video while recording
(render off). At the end an INFO line gives the frames, the video length and the audio pushed
against it (positive up to a frame period is normal: the clock runs past the last deadline); then the
track is padded with silence to the video length, and audio pushed past the last frame stays.

## Backends

**Web.** `VideoFrame` from the canvas inside the rAF task, before the swap (the context is
`preserveDrawingBuffer:false`, so the drawing buffer still holds the frame), into `VideoEncoder`
and `AudioEncoder`. Codecs are probed once when the script loads: H.264 High, else Constrained
Baseline, at the lowest listed level (3.1 to 6.2) whose frame size and macroblock rate fit
(bitrate `0.1 · w · h · fps`); AAC-LC, else Opus (Firefox, Chrome on Linux; editors handle Opus in
MP4 poorly), both 192 kbps. `start` returns false while the probe runs (a console warning says so;
start again), past level 6.2 (a console warning), and in a browser without WebCodecs, an H.264
encoder or an AAC/Opus encoder. The probe uses 640x360 at 30 fps: an encoder that rejects the level
picked for the real size ends the recording at its first frame (`error` in the event). WebCodecs
needs a secure context: a page over plain `http` to a LAN address (a phone) has none, so recording
there needs `https` or `localhost`. The vendored Mediabunny muxer (`deps/mediabunny`) writes the
MP4. The JS lives in `web/nt_recorder_web.js` and loads with `--extern-pre-js`, outside Closure; a
page that links the recorder evaluates the 125 KB muxer at load. One module per page, main thread.

Encoded bytes stay in tab memory (~0.8 MB/s at 1080p30; finishing copies the file, so the peak is
two to three times its size): a dev-only exception to preallocated storage; long web recordings
are not a goal. Nothing paces the game to the encoder, so an encoder
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
ffmpeg: streams, frame count, 48 kHz stereo audio as long as the video, flash/beep offset within a
frame). A second test records in RUN at time scale 7 (steps over the 100 ms drift bound), fails on a
drift WARN, and checks `bad_params` and a cancelled `nt-recorder` event.
