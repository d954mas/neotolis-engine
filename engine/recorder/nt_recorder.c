#include "recorder/nt_recorder.h"

#include <math.h>
#include <string.h>

#include "core/nt_assert.h"
#include "log/nt_log.h"
#include "recorder/nt_recorder_internal.h"
#include "window/nt_window.h"

/* Audio placed on the track but past the written video: one period at 1 fps plus a clamped frame. */
#define NT_RECORDER_AUDIO_CAP (2ULL * NT_RECORDER_AUDIO_RATE)
/* A push that starts within one mixer block of where the previous one ended is joined to it. */
#define NT_RECORDER_AUDIO_SLACK 1024

static struct {
    bool active;
    uint32_t fps;
    double t0;
    uint32_t fb_width;
    uint32_t fb_height;
    uint32_t frames;
    uint64_t audio_written; /* track position written so far */
    uint64_t queue_pos;     /* track position of audio[0] */
    uint32_t queued;
    float audio[(size_t)NT_RECORDER_AUDIO_CAP * 2U];
} s_rec;

static void finish(void) {
    s_rec.active = false;
    NT_LOG_INFO("%u frames recorded", s_rec.frames);
    nt_recorder_platform_close(s_rec.frames);
}

static void fail(void) {
    NT_LOG_WARN("encoder failed, recording ended");
    finish();
}

static uint64_t frames_end(uint32_t frames) { return (uint64_t)llround((double)frames * NT_RECORDER_AUDIO_RATE / (double)s_rec.fps); }

static bool write_silence(uint64_t frames) {
    static const float k_zero[1024 * 2];
    while (frames > 0) {
        uint32_t n = frames < 1024U ? (uint32_t)frames : 1024U;
        if (!nt_recorder_platform_audio(k_zero, n)) {
            return false;
        }
        s_rec.audio_written += n;
        frames -= n;
    }
    return true;
}

/* Writes the track up to `limit`, the end of the written video: silence up to the queue, then the queue.
   With nothing queued, silence stops one block short of the limit unless `pad`, leaving room for a
   mixer that delivers late. */
static bool write_audio(uint64_t limit, bool pad) {
    uint64_t fill = limit;
    if (s_rec.queued > 0) {
        fill = s_rec.queue_pos < limit ? s_rec.queue_pos : limit;
    } else if (!pad && limit > NT_RECORDER_AUDIO_SLACK) {
        fill = limit - NT_RECORDER_AUDIO_SLACK;
    }
    if (s_rec.audio_written < fill && !write_silence(fill - s_rec.audio_written)) {
        return false;
    }
    if (s_rec.queued > 0 && s_rec.queue_pos == s_rec.audio_written && s_rec.audio_written < limit) {
        uint64_t room = limit - s_rec.audio_written;
        uint32_t n = room < s_rec.queued ? (uint32_t)room : s_rec.queued;
        if (!nt_recorder_platform_audio(s_rec.audio, n)) {
            return false;
        }
        s_rec.audio_written += n;
        s_rec.queued -= n;
        s_rec.queue_pos += n;
        memmove(s_rec.audio, s_rec.audio + ((size_t)n * 2U), (size_t)s_rec.queued * 2U * sizeof(float));
    }
    if (pad && s_rec.audio_written < limit) {
        return write_silence(limit - s_rec.audio_written);
    }
    return true;
}

/* Ends with the audio track padded to the video; audio past it is dropped. */
static uint32_t end_recording(void) {
    if (s_rec.frames == 0) {
        NT_LOG_WARN("stopped before the first frame, nothing written");
    }
    if (!write_audio(frames_end(s_rec.frames), true)) {
        fail();
        return 0;
    }
    uint32_t frames = s_rec.frames;
    finish();
    return frames;
}

bool nt_recorder_start(const char *name, uint32_t fps, double t) {
    NT_ASSERT(!s_rec.active && "nt_recorder_start: already recording");
    NT_ASSERT(name != NULL && name[0] != '\0');
    NT_ASSERT(fps >= 1 && fps <= 120);
    uint32_t w = g_nt_window.fb_width & ~1U;
    uint32_t h = g_nt_window.fb_height & ~1U;
    if (w == 0 || h == 0) {
        NT_LOG_WARN("%s: framebuffer is %ux%u, not recording", name, g_nt_window.fb_width, g_nt_window.fb_height);
        return false;
    }
    if (!nt_recorder_platform_open(name, w, h, fps)) {
        NT_LOG_WARN("%s: no encoder, or the output cannot be opened", name);
        return false;
    }
    s_rec.active = true;
    s_rec.fps = fps;
    s_rec.t0 = t;
    s_rec.fb_width = g_nt_window.fb_width;
    s_rec.fb_height = g_nt_window.fb_height;
    s_rec.frames = 0;
    s_rec.audio_written = 0;
    s_rec.queue_pos = 0;
    s_rec.queued = 0;
    NT_LOG_INFO("%s: recording %ux%u at %u fps", name, w, h, fps);
    return true;
}

void nt_recorder_frame(double t, double dt) {
    if (!s_rec.active) {
        return;
    }
    if (g_nt_window.fb_width != s_rec.fb_width || g_nt_window.fb_height != s_rec.fb_height) {
        NT_LOG_WARN("framebuffer resized, recording ended");
        (void)end_recording();
        return;
    }
    /* Nearest sample: a deadline belongs to this frame once it is closer to t than to the neighbouring
       frames. The half-frame margin also absorbs float step_dt that rounds below 1 / fps; capping it at
       half a period keeps a long hitch from claiming deadlines the next frame is nearer to. */
    double period = 1.0 / (double)s_rec.fps;
    double margin = 0.5 * (dt < period ? dt : period);
    uint32_t frames = s_rec.frames;
    while (t + margin >= s_rec.t0 + ((double)(frames + 1U) * period)) {
        frames++;
    }
    /* Catching up more than two seconds (render off while time ran) would flood the encoder. */
    if (frames - s_rec.frames > 2U * s_rec.fps) {
        NT_LOG_WARN("%.1f s without frames, recording ended", (double)(frames - s_rec.frames) * period);
        (void)end_recording();
        return;
    }
    /* Audio first, and only up to the end of the video being written. */
    if (!write_audio(frames_end(frames), false)) {
        fail();
        return;
    }
    for (; s_rec.frames < frames; s_rec.frames++) {
        if (!nt_recorder_platform_video(s_rec.frames)) {
            fail();
            return;
        }
    }
}

void nt_recorder_push_audio(const float *stereo48k, uint32_t frames, double t_start) {
    if (!s_rec.active || frames == 0) {
        return;
    }
    NT_ASSERT(stereo48k != NULL);
    int64_t pos = (int64_t)llround((t_start - s_rec.t0) * NT_RECORDER_AUDIO_RATE);
    /* After written audio (silence ends one block early) the block keeps its exact time; next to queued
       audio, drift within a block joins it, so a mixer's blocks stay one run. */
    int64_t end = (int64_t)(s_rec.queued > 0 ? s_rec.queue_pos + s_rec.queued : s_rec.audio_written);
    int64_t join = s_rec.queued > 0 ? NT_RECORDER_AUDIO_SLACK : 0;
    if (pos < end - join || pos < 0) {
        /* Overlaps placed audio or precedes t0: keep only the part after it. */
        int64_t skip = (end > 0 ? end : 0) - pos;
        if (skip >= (int64_t)frames) {
            return;
        }
        stereo48k += (size_t)skip * 2U;
        frames -= (uint32_t)skip;
        pos += skip;
    }
    if (pos <= end + join) {
        pos = end;
    }
    if (s_rec.queued == 0) {
        s_rec.queue_pos = (uint64_t)pos; /* a gap before it is written as silence */
        end = pos;
    }
    uint64_t gap = (uint64_t)(pos - end);
    /* No room only while video lags the audio by over a second; the block is lost and plays as silence. */
    if (s_rec.queued + gap + frames > NT_RECORDER_AUDIO_CAP) {
        return;
    }
    float *dst = s_rec.audio + ((size_t)s_rec.queued * 2U);
    memset(dst, 0, (size_t)gap * 2U * sizeof(float));
    memcpy(dst + ((size_t)gap * 2U), stereo48k, (size_t)frames * 2U * sizeof(float));
    s_rec.queued += (uint32_t)gap + frames;
}

uint32_t nt_recorder_stop(void) { return s_rec.active ? end_recording() : 0; }

bool nt_recorder_active(void) { return s_rec.active; }
