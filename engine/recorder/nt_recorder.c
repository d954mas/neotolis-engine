#include "recorder/nt_recorder.h"

#include <math.h>
#include <string.h>

#include "core/nt_assert.h"
#include "log/nt_log.h"
#include "recorder/nt_recorder_internal.h"
#include "window/nt_window.h"

/* Pushed audio not yet written: one period at 1 fps plus a clamped frame. */
#define NT_RECORDER_AUDIO_CAP (2U * NT_RECORDER_AUDIO_RATE)
/* Mixers push in blocks that rarely match a frame; drift up to one block carries over. */
#define NT_RECORDER_AUDIO_SLACK 1024

static struct {
    bool active;
    uint32_t fps;
    double t0;
    uint32_t fb_width;
    uint32_t fb_height;
    uint32_t frames;
    uint32_t audio_queued;
    uint64_t audio_written;
    uint64_t audio_padded;
    uint64_t audio_trimmed;
    float audio[(size_t)NT_RECORDER_AUDIO_CAP * 2U];
} s_rec;

static void finish(void) {
    s_rec.active = false; /* first: the backend may hand control to the host before it returns */
    NT_LOG_INFO("%u frames, audio padded %llu trimmed %llu samples", s_rec.frames, (unsigned long long)s_rec.audio_padded, (unsigned long long)s_rec.audio_trimmed);
    nt_recorder_platform_close(s_rec.frames);
}

static void fail(const char *why) {
    NT_LOG_WARN("%s, recording ended", why);
    finish();
}

static uint64_t samples_at(double seconds) { return seconds > 0.0 ? (uint64_t)llround(seconds * NT_RECORDER_AUDIO_RATE) : 0U; }

static void consume(uint32_t n) {
    s_rec.audio_queued -= n;
    memmove(s_rec.audio, s_rec.audio + ((size_t)n * 2U), (size_t)s_rec.audio_queued * 2U * sizeof(float));
}

static bool write_silence(uint64_t frames) {
    static const float k_zero[1024 * 2];
    s_rec.audio_padded += frames;
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

/* The queue holds the samples pushed for the advance that ends at track position `end`. Places it on the
   track first (a gap before it becomes silence, an overlap with written audio or with time before t0 is
   dropped), then writes up to `limit`, the end of the recorded video; the rest waits for later frames. */
static bool write_audio(uint64_t end, uint64_t limit) {
    int64_t start = (int64_t)end - (int64_t)s_rec.audio_queued;
    int64_t gap = start - (int64_t)s_rec.audio_written;
    if (start < 0 || gap < -NT_RECORDER_AUDIO_SLACK) {
        int64_t over = -gap; /* gap <= start: covers time before t0 as well */
        uint32_t drop = over < (int64_t)s_rec.audio_queued ? (uint32_t)over : s_rec.audio_queued;
        s_rec.audio_trimmed += drop;
        consume(drop);
    } else if (gap > NT_RECORDER_AUDIO_SLACK && s_rec.audio_written < limit) {
        uint64_t room = limit - s_rec.audio_written;
        if (!write_silence((uint64_t)gap < room ? (uint64_t)gap : room)) {
            return false;
        }
    }
    uint64_t room = limit > s_rec.audio_written ? limit - s_rec.audio_written : 0U;
    uint32_t n = room < s_rec.audio_queued ? (uint32_t)room : s_rec.audio_queued;
    if (n > 0) {
        if (!nt_recorder_platform_audio(s_rec.audio, n)) {
            return false;
        }
        s_rec.audio_written += n;
        consume(n);
    }
    return true;
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
    s_rec.audio_queued = 0;
    s_rec.audio_written = 0;
    s_rec.audio_padded = 0;
    s_rec.audio_trimmed = 0;
    NT_LOG_INFO("%s: recording %ux%u at %u fps", name, w, h, fps);
    return true;
}

void nt_recorder_frame(double t, double dt) {
    if (!s_rec.active) {
        return;
    }
    if (g_nt_window.fb_width != s_rec.fb_width || g_nt_window.fb_height != s_rec.fb_height) {
        fail("framebuffer resized");
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
    /* Audio first, and only up to the end of the video being written. */
    if (!write_audio(samples_at(t - s_rec.t0), samples_at((double)frames * period))) {
        fail("audio encoder failed");
        return;
    }
    for (; s_rec.frames < frames; s_rec.frames++) {
        if (!nt_recorder_platform_video(s_rec.frames)) {
            fail("video encoder failed");
            return;
        }
    }
}

void nt_recorder_push_audio(const float *stereo48k, uint32_t frames) {
    if (!s_rec.active || frames == 0) {
        return;
    }
    NT_ASSERT(stereo48k != NULL);
    uint32_t room = NT_RECORDER_AUDIO_CAP - s_rec.audio_queued;
    uint32_t n = frames < room ? frames : room;
    memcpy(s_rec.audio + ((size_t)s_rec.audio_queued * 2U), stereo48k, (size_t)n * 2U * sizeof(float));
    s_rec.audio_queued += n;
    s_rec.audio_trimmed += frames - n;
}

uint32_t nt_recorder_stop(void) {
    if (!s_rec.active) {
        return 0;
    }
    if (s_rec.frames == 0) {
        NT_LOG_WARN("stopped before the first frame, nothing written");
    }
    /* Samples pushed after the last frame call are past the recorded time. */
    s_rec.audio_trimmed += s_rec.audio_queued;
    consume(s_rec.audio_queued);
    uint64_t end = samples_at((double)s_rec.frames / (double)s_rec.fps);
    if (s_rec.audio_written < end && !write_silence(end - s_rec.audio_written)) {
        fail("audio encoder failed");
        return 0;
    }
    uint32_t frames = s_rec.frames;
    finish();
    return frames;
}

bool nt_recorder_active(void) { return s_rec.active; }
