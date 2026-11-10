# Video Recorder

`nt_recorder` is a dev-only module that records the presented frames and an audio track into an MP4
(H.264 + AAC, or Opus where the browser has no AAC encoder; 48 kHz stereo) at a constant frame rate.
It replaces an external screen recorder for debugging, iteration and gameplay clips. A game that does
not link it pays nothing; like `nt_crash` it has no option and no stub. The track holds only what the
game pushes: `nt_audio` output is not captured.

Related: [Logging, Errors, Debugging](logging-errors-debugging.md) (devapi, frame capture), [Frame Lifecycle](../runtime/frame-lifecycle.md)

## API and call placement

```c
typedef struct {
    uint32_t frames;  /* frames sent to the encoder */
    double max_drift; /* seconds; the signed audio-minus-clock gap of largest magnitude after the first push */
} nt_recorder_stats_t;

bool nt_recorder_ready(void);
bool nt_recorder_start(const char *name, uint32_t fps, uint32_t bitrate_bps);
void nt_recorder_frame(double t, double dt);
void nt_recorder_push_audio(const float *stereo48k, uint32_t frames);
nt_recorder_stats_t nt_recorder_stop(void);
bool nt_recorder_active(void);
```

`ready` tells whether a `start` can pick its codecs now: web is not ready until its load-time codec
probe settles (see Backends); Windows is always ready. Ready does not promise an encoder exists.

The game calls `nt_recorder_frame` once per presented frame, after `nt_gfx_end_frame` and before
`nt_window_swap_buffers`, and passes its clock: `t` is the time of this frame, `dt` the advance that
produced it; `t` never goes back within a recording (a `t` before the origin asserts). There is no hook and no hidden clock. With `g_nt_app.time` and MANUAL lockstep the
recording is offline and frame-exact; stepping that outruns the encoder ends it on web and waits for
it on Windows (see Backends). With RUN it records hand play in real time. The first `frame` call of a
recording sets its origin `t0`: the start of the step that produced that frame (`t - dt`), or, when
the mixer pushed samples before it, where those samples begin. The video deadlines count from `t0`
and audio sample 0 sits there, so `start` can come anywhere in a step: between steps, or inside one
before or after its push.

`start` fixes the size to the window framebuffer, rounded down to even (H.264 4:2:0). It asserts a
non-empty `name`, `fps` within 1..120, nonzero `bitrate_bps` and no active recording. It returns false (logged) when either
side of that size is under 2 px or the backend refuses: not ready, no encoder, a size the encoder
rejects, an unsupported bitrate, or an output that cannot be opened (see Backends). `frame`, `push_audio` and `stop` are no-ops while idle.
`stop` returns the frames sent to the encoder and `max_drift` (see Audio); the whole struct is zero
while idle and when a final write failed (audio, or on Windows the last frames, see Backends).

A recording also ends on its own, with a WARN, in three cases. A change of the even-rounded
framebuffer size, and a `frame` call with more than two seconds (`2 · fps`) of due frames (render
off while time ran), end it with the audio padded to the video. An encoder failure ends it; web
writes no file, Windows keeps the fragments already on disk. After any of them `stop` returns zero stats.

## Choosing the bitrate

`bitrate_bps` is the target **video bits per second**, fixed for that recording; audio is separate.
The game chooses it at each start. There is no default, preset or automatic quality adjustment.
Variable bitrate output can be above or below the target; an encoder can also reject a configuration.
To change the target, stop and start another recording.

A starting estimate is `round(width * height * fps * k)`, using the even-rounded framebuffer size
and floating-point or sufficiently wide arithmetic. These coefficients are guidance to test on the
game's content, not quality guarantees or engine modes:

| Content and purpose | k (bits per pixel per frame) | 1080p30 | 1080p60 |
|---|---:|---:|---:|
| Compact debug/agent clips | 0.1 | 6.22 Mbps | 12.44 Mbps |
| Higher-quality gameplay | 0.4 | 24.88 Mbps | 49.77 Mbps |
| Dense detail and trailer source | 0.8 | 49.77 Mbps | 99.53 Mbps |

For example, `nt_recorder_start("trailer.mp4", 60, 100000000)` requests 100 Mbps.
Thin outlines, small text, particles and rapid motion may need more bits than a smooth scene.
Inspect a representative clip at its final resolution before selecting a target. H.264 here uses
4:2:0 chroma subsampling; increasing bitrate does not make it lossless or preserve every colored edge.

Approximate file size is `(video_bps + 192000) * seconds / 8`, plus container overhead, using the
**measured** video rate when available. At the target rate, 100 Mbps video uses about 12.5 MB/s or
752 MB/minute including audio. A higher bitrate increases storage and potentially encoding cost;
it does not remove GPU readback or encoder waits. Windows is the primary path for long, high-quality
clips; web keeps the whole file in memory (see Backends).

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
recording. `stop` reports the signed difference of largest magnitude, sampled at each `frame` call after the
first push, as
`max_drift` (positive: audio ahead of the clock), so a check can bound it without reading the log. A real-time mixer that renders on an audio thread is the game's to align: it pushes the
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
when the script loads: H.264 High, else Constrained Baseline; AAC-LC, else Opus (Firefox,
Chrome on Linux; editors handle Opus in MP4 poorly), both 192 kbps. Each `start` selects the lowest
listed H.264 level (3.1 to 6.2) whose frame size, macroblock rate and requested bitrate fit. While the probe runs
`nt_recorder_ready` is false and `start` returns false (a console warning says so; start again). Once
it settles `ready` is true, and `start` still returns false past level 6.2 and in a browser without
WebCodecs, an H.264 encoder or an AAC/Opus encoder (a console warning names the cause). The video probe uses 640x360 at
30 fps and level 3.1: this is not a capability check for the requested size, rate and bitrate. A synchronous
configuration rejection makes `start` return false with a console warning; an encoder that rejects
the configuration asynchronously ends the recording on the next video/audio submission or `stop`
(`error` in the event). A paused `frame` call with no video or audio to submit does not observe that failure.
WebCodecs needs a secure context: a page over plain `http` to a LAN address (a phone) has none,
so recording there needs `https` or `localhost`.
The vendored Mediabunny muxer (`deps/mediabunny`) writes the MP4. The JS lives in
`web/nt_recorder_web.js` and loads with `--extern-pre-js`, outside Closure; a page that links the
recorder evaluates the muxer (about 125 KB) at load. One recording at a time per page, on the main
thread.

The level table follows [H.264 Annex A limits as used by Chromium](https://github.com/chromium/chromium/blob/main/media/parsers/h264_level_limits.cc):
MaxFS, MaxMBPS and baseline MaxBR; High allows 1.25 times the baseline bitrate. Each side in macroblocks
also must not exceed `sqrt(8 * MaxFS)`. The table includes
3.2, 4.1 and 6.1, so raising the bitrate can select the next sufficient level without skipping one.
No level fits above 1,000,000,000 bps for High or 800,000,000 bps for Constrained Baseline; the browser
may have lower limits. A selected level does not guarantee that its encoder accepts the request.

Encoded bytes stay in tab memory (100 Mbps is about 12.5 MB/s; finishing copies
the buffer into a `Blob`, so the peak is at least twice the file size): a dev-only exception to
preallocated storage; long web recordings are not a goal. Nothing paces the game to the encoder.
Before submitting each video frame, `encodeQueueSize > 4 * fps` ends the recording (`encoder backlog`),
including during catch-up. Lockstep that outpaces the encoder can therefore accumulate just over
four seconds of pending encode requests before the next submission ends it.

On a hybrid-GPU laptop the browser renders and encodes on the integrated GPU (see
[GPU selection](../render/architecture.md#gpu-selection)); there 1080p at 60 fps can end in
`encoder backlog` under game load, while 30 fps keeps up. To record on the discrete GPU, set Chrome to
High performance in Windows Settings > Display > Graphics (persistent; restart Chrome), or launch
it once with `--force_high_performance_gpu`.

When a recording ends (`stop`, or on its own) and the encoders flush, the window event
`nt-recorder` (cancelable, `detail {name, blob, frames, error}`) is dispatched after the C call
returns. `blob` is null and `error` set when no file was written (`'no frames'` when no frame was
due). Unless a listener cancels the event, the browser downloads `blob` as `name` (Chrome asks
before a second automatic download from a page). A throw in a backend call (a lost context) ends the
recording in that call. An asynchronous codec or muxer failure ends it on the next video/audio
submission. If no further submission occurs, `stop` reports the failure only through `error` in the
event: the event is the authority for whether a file exists.

**Windows.** A Media Foundation sink writer writes a fragmented MP4 to the `name` path while
recording (UTF-8, relative to the working directory, overwritten; a name that does not convert to a
path under `MAX_PATH` makes `start` return false). A killed process leaves a file readable up to the
last fragment, which loses about one fragment plus the frames still in the encoder. Video is H.264
High without B-frames (the MP4 sink writes no edit list, so B-frames would start the video a frame
late), peak-constrained VBR at `bitrate_bps`, with peak `2 * bitrate_bps`, BT.709 tags; audio is AAC-LC 192 kbps from the float
samples. Both backends put a keyframe every two seconds. Media Foundation takes a hardware encoder
when one accepts the stream, else its software one. `start` returns false where no encoder accepts
the size or bitrate and blocks for encoder setup. Any repeated frames depend on the game's next
`t` and `dt`: in `nt_app` RUN, elapsed wall time is clamped to `max_dt` before applying `scale`,
so setup time does not necessarily advance the recording clock by the full wait.
Targets above `UINT32_MAX / 2` return false before opening the output, because the peak must fit
Media Foundation's UINT32 attribute. The target is passed unchanged to `MF_MT_AVG_BITRATE`; there
is no clamp or quality-mode fallback. Media Foundation chooses the H.264 level.
Media Foundation must be installed: a game that links the recorder does not start on a Windows N
edition without the Media Feature Pack. The executable links `nt_gfx`, not its stub: the recorder
reads the window through it. A recording takes five `nt_gfx` readback buffers of `w · h · 4` bytes
(about 41 MB at 1080p), so `nt_gfx_desc_t.max_buffers` needs five spare slots. The sink writer gets
a hardware D3D11 device with video support, so Media Foundation can convert the frames to NV12 on the
GPU; where no such device can be made, `start` WARNs and the conversion runs on the CPU.

A `frame` call with due frames queues one read of the window into a readback buffer
(`nt_gfx_read_pixels_to_buffer`, see [Rendering Architecture](../render/architecture.md#readback)).
When three reads are pending, the next read submission first encodes the oldest one. The delay counts
queued reads, not all `frame` calls: a call with no due frames queues nothing. Deferring the readback
gives the GPU time to finish, but the call can still block on the GPU or encoder (see pacing below).
Media Foundation reads its pixels in place, bottom-up; the recorder copies nothing: each output frame the read covers
(several when one call takes several deadlines) is one sample over the same buffer. The frames keep their pts, so
the delay does not move the video against the audio, and the end of a recording, unless a write
failed, encodes the reads still in flight before it finishes the file. The read takes the top rows,
so an odd framebuffer loses its bottom row and right column, as on web.

Pacing waits instead of dropping frames. When the GPU has not written the oldest read yet, the
encode waits for it. A buffer takes a new read only after Media Foundation has released every sample
over it. The two buffers beyond the reads in flight let it hold the samples of the last two encoded
reads; when it holds more, the call waits. So lockstep runs at the GPU's and the encoder's pace, RUN
shows a hitch instead of a missing frame, and ten seconds without Media Foundation releasing a sample
of the awaited buffer counts as an encoder failure. A catch-up call queues all its repeated frames
(up to two seconds of them) at once over one buffer, so the pixels in flight stay at five buffers.
`stop` also returns zero stats when a write fails while it encodes the last reads, as after any encoder
failure. Each audio write, pushed samples or silence, allocates one Media Foundation buffer, and each
video frame one sample, beside the per-sample allocations of the Media Foundation pipeline itself: a
dev-only exception to preallocated storage. The readback buffers are destroyed after `MFShutdown`;
a sample still over one then asserts. An encoder failure ends the recording without `Finalize`,
which could wait on the dead encoder, and keeps the fragments on disk; so does a failed `Finalize` (WARN), after which
`stop` still returns the stats. A recording without a frame deletes its file (WARN when it cannot).

**Other platforms** define no `nt_recorder` target.

## devapi `record.*`

`NT_DEVAPI_GROUP_RECORDER` (OFF) links `nt_recorder` and adds two commands; the host calls
`nt_recorder_frame` for every presented frame (a host that does not records nothing).

| Command | Params | Result | Errors |
|---|---|---|---|
| `record.start` | `{name, fps, bitrate_bps}` | `{}` | `bad_params` (name not a non-empty string, fps not an integer 1..120, bitrate_bps missing or not an integer 1..4294967295), `recording` (active), `not_ready` (`nt_recorder_ready` is false; start again), `failed` (`start` returned false) |
| `record.stop` | `{}` | `{frames, max_drift_ms}` | none of its own; both are 0 when nothing was active, the recording ended on its own, or a final write failed |

Both commands also answer the core `busy` while a deferred reply is pending ([Logging, Errors, Debugging](logging-errors-debugging.md), Developer API).

On Windows `name` is the file path. The host's first `frame` call after `record.start` sets the
origin. An offline recording is `time.set_mode{mode:"manual"}`,
`record.start`, `time.step{count}`, `record.stop`; the default `step_dt` of `1/60` at `fps` 30 records
every second step. The `record_demo` scenario runs this sequence and a real-time one on native and
web; see [build instructions](../../build.md).
