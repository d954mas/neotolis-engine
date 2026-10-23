# Video Recorder

`nt_recorder` is a dev-only module that records the presented frames and an audio track into an MP4
(H.264 + AAC, or Opus where the browser has no AAC encoder; 48 kHz stereo) at a constant frame rate.
It replaces an external screen recorder for debugging, iteration and gameplay clips. A game that does
not link it pays nothing; like `nt_crash` it has no option and no stub. The track holds only what the
game pushes: `nt_audio` output is not captured.

Related: [Logging, Errors, Debugging](logging-errors-debugging.md) (devapi, frame capture), [Frame Lifecycle](../runtime/frame-lifecycle.md)

## API and call placement

```c
bool nt_recorder_start(const char *name, uint32_t fps);
void nt_recorder_frame(double t, double dt);
void nt_recorder_push_audio(const float *stereo48k, uint32_t frames);
uint32_t nt_recorder_stop(void);
bool nt_recorder_active(void);
```

The game calls `nt_recorder_frame` once per presented frame, after `nt_gfx_end_frame` and before
`nt_window_swap_buffers`, and passes its clock: `t` is the time of this frame, `dt` the advance that
produced it; `t` never goes back within a recording (asserted). There is no hook and no hidden clock. With `g_nt_app.time` and MANUAL lockstep the
recording is offline and frame-exact; stepping that outruns the encoder ends it on web and waits for
it on Windows (see Backends). With RUN it records hand play in real time. The first `frame` call of a
recording sets its origin `t0`: the start of the step that produced that frame (`t - dt`), or, when
the mixer pushed samples before it, where those samples begin. The video deadlines count from `t0`
and audio sample 0 sits there, so `start` can come anywhere in a step: between steps, or inside one
before or after its push.

`start` fixes the size to the window framebuffer, rounded down to even (H.264 4:2:0). It asserts a
non-empty `name`, `fps` within 1..120 and no active recording. It returns false (logged) when either
side of that size is under 2 px or the backend refuses: no encoder, a size the encoder rejects, or
an output that cannot be opened (see Backends). `frame`, `push_audio` and `stop` are no-ops while idle.
`stop` returns the frames sent to the encoder, 0 when the final audio write failed.

A recording also ends on its own, with a WARN, in three cases. A change of the even-rounded
framebuffer size, and a `frame` call with more than two seconds (`2 · fps`) of due frames (render
off while time ran), end it with the audio padded to the video. An encoder failure ends it; web
writes no file, Windows keeps the fragments already on disk. After any of them `stop` returns 0.

## Frame selection

Output frame `i` has pts `i / fps` and is due at `t0 + (i + 1) / fps`, where `t0` is the origin:
the first frame shows the result of the first advance, and `N` lockstep steps of `1 / fps` give `N`
frames. A `frame` call takes every due deadline within a margin of `t`: half of `dt`, at most half
a period. With a steady game frame rate that picks the presented frame nearest each deadline and
absorbs float `step_dt` that rounds below `1 / fps` (`1/50`, `1/25`); when the game runs slower than
`fps` the margin caps at half a period, so a deadline goes to the first frame no earlier than half a
period before it. Several deadlines in one call (game slower than `fps`, a clamped hitch, a short
render-off gap) re-encode the same pixels. Frozen time (pause, MANUAL idle) writes nothing.
`step_dt = 1 / (fps · k)` records every k-th step.

## Audio

The game's mixer pushes its output as one stream of interleaved stereo float samples at 48 kHz from
the main thread, each step's samples before the `frame` call for that step. The stream carries no
timestamps and the recorder corrects nothing: samples pushed before the first `frame` call set the
origin (render off after `start`, several steps per frame); until the first push each `frame` call
writes silence up to its `t`; from then on sample `n` of the stream follows sample `n − 1`. So the mixer pushes
every step's samples once it starts, silence included. A lockstep mixer renders
`llround(t · 48000) − llround((t − dt) · 48000)` samples per step: from absolute times, so rounding
does not accumulate. A mixer whose first push is the step right after a `frame` call starts on
time. One whose first push comes later (a second step of the same frame, or render off) starts early
by the steps since that call; past 100 ms the drift WARN reports it. A game without sound pushes
nothing and gets a silent track.

A sound the mixer starts at the beginning of a step plays at that step's time. With `step_dt =
1 / fps` it plays with the frame that first shows its cause; with more steps per frame it can come up
to one frame period after that frame's pts, as the frame shows the nearest step. A gap or a mixer on
another clock shifts the rest of the track. When the track position differs from the time passed to
`frame` by more than 100 ms (a mixer block and a 60 Hz step fit inside), a WARN says so once per
recording. A real-time mixer that renders on an audio thread is the game's to align: it pushes the
samples rendered for each step on the game clock.

Pushed samples go to the encoder at once, so the track may run ahead of the video while recording
(render off). At the end an INFO line gives the frames, the video length and the audio written so
far against it (within a frame period either way is normal: the clock rarely stops on a deadline);
then the track is padded with silence to the video length, and audio pushed past the last frame
stays.

## Backends

**Web.** The context uses `preserveDrawingBuffer:false`, so the drawing buffer is valid only until
the task presents it: a `VideoFrame` from the canvas inside the rAF task, before the swap, reads the
finished frame. Frames and samples go to `VideoEncoder` and `AudioEncoder`. Codecs are probed once
when the script loads: H.264 High, else Constrained Baseline, at the lowest listed level (3.1 to 6.2)
whose frame size and macroblock rate fit (bitrate `0.1 · w · h · fps`); AAC-LC, else Opus (Firefox,
Chrome on Linux; editors handle Opus in MP4 poorly), both 192 kbps. `start` returns false while the
probe runs (a console warning says so; start again), past level 6.2 (a console warning), and in a
browser without WebCodecs, an H.264 encoder or an AAC/Opus encoder. The probe uses 640x360 at
30 fps: an encoder that rejects the level picked for the real size ends the recording at the first
frame call after it reports so (`error` in the event). WebCodecs needs a secure context: a page over
plain `http` to a LAN address (a phone) has none, so recording there needs `https` or `localhost`.
The vendored Mediabunny muxer (`deps/mediabunny`) writes the MP4. The JS lives in
`web/nt_recorder_web.js` and loads with `--extern-pre-js`, outside Closure; a page that links the
recorder evaluates the muxer (about 125 KB) at load. One recording at a time per page, on the main
thread.

Encoded bytes stay in tab memory (the target bitrate at 1080p30 is about 0.8 MB/s; finishing copies
the buffer into a `Blob`, so the peak is at least twice the file size): a dev-only exception to
preallocated storage; long web recordings are not a goal. Nothing paces the game to the encoder: an
encoder queue above four seconds of frames (two of backlog plus one call's catch-up) ends the
recording (`encoder backlog`), so lockstep that makes frames faster than the encoder drains them
ends once about two seconds of frames wait.

When a recording ends (`stop`, or on its own) and the encoders flush, the window event
`nt-recorder` (cancelable, `detail {name, blob, frames, error}`) is dispatched after the C call
returns. `blob` is null and `error` set when no file was written (`'no frames'` when no frame was
due). Unless a listener cancels the event, the browser downloads `blob` as `name` (Chrome asks
before a second automatic download from a page). A throw in a backend call (a lost context) ends the
recording in that call. An asynchronous codec, muxer or backlog failure ends it at the next call
into the backend, or, after the last one, shows only as `error` in the event: the event is the
authority for whether a file exists.

**Windows.** A Media Foundation sink writer writes a fragmented MP4 to the `name` path while
recording (UTF-8, relative to the working directory, overwritten; a name that does not convert to a
path under `MAX_PATH` makes `start` return false). A killed process leaves a file readable up to the
last fragment, which loses about one fragment plus the frames still in the encoder. Video is H.264
High without B-frames (the MP4 sink writes no edit list, so B-frames would start the video a frame
late), peak-constrained VBR at the web bitrate, BT.709 tags; audio is AAC-LC 192 kbps from the float
samples. Both backends put a keyframe every two seconds. Media Foundation takes a hardware encoder
when one accepts the stream, else its software one. `start` returns false where no encoder accepts
the size (very small, or past 4K) and blocks for encoder setup, up to about a second with a hardware
encoder, so in RUN the step after `start` advances by that wait and its `frame` call repeats a frame
for each deadline it covers.
Media Foundation must be installed: a game that links the recorder does not start on a Windows N
edition without the Media Feature Pack.

Each due frame reads the window with `nt_gfx_read_pixels` straight into one of four pooled encoder
buffers. The read takes the top rows, so an odd framebuffer loses its bottom row and right column, as
on web. When the encoder holds all four buffers, the `frame` call waits for one: lockstep runs at the
encoder's pace, RUN shows hitches, and an encoder that keeps all four for ten seconds counts as an
encoder failure. Each audio write, pushed samples or silence, allocates one Media Foundation buffer: a
dev-only exception to preallocated storage. An encoder failure ends the recording and keeps the
fragments on disk; so does a failed `Finalize` (WARN), after which `stop` still returns the frames. A
recording without a frame deletes its file.

**Other platforms** define no `nt_recorder` target.

## devapi `record.*`

`NT_DEVAPI_GROUP_RECORDER` (OFF) links `nt_recorder` and adds two commands; the host calls
`nt_recorder_frame` for every presented frame (a host that does not records nothing).

| Command | Params | Result | Errors |
|---|---|---|---|
| `record.start` | `{name, fps}` | `{}` | `bad_params` (name not a non-empty string, fps not an integer 1..120), `busy` (active), `failed` (`start` returned false) |
| `record.stop` | `{}` | `{frames}` | none; `frames` is 0 when nothing was active, the recording ended on its own, or the final audio write failed |

On Windows `name` is the file path. The host's first `frame` call after `record.start` sets the
origin. An offline recording is `time.set_mode{mode:"manual"}`,
`record.start`, `time.step{count}`, `record.stop`; the default `step_dt` of `1/60` at `fps` 30 records
every second step. The browser tests and the native check of this sequence are in
[build instructions](../../build.md).
