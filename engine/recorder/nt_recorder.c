#include "recorder/nt_recorder.h"

#include <math.h>
#include <string.h>

#include "core/nt_assert.h"
#include "log/nt_log.h"
#include "recorder/nt_recorder_internal.h"
#include "window/nt_window.h"

/* Audio pushed but not yet due; one second covers any clamped frame. */
#define NT_RECORDER_AUDIO_CAP NT_RECORDER_AUDIO_RATE

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
    nt_recorder_platform_close(s_rec.frames);
    NT_LOG_INFO("%u frames, audio padded %llu trimmed %llu samples", s_rec.frames, (unsigned long long)s_rec.audio_padded, (unsigned long long)s_rec.audio_trimmed);
    s_rec.active = false;
}

static void fail(const char *why) {
    NT_LOG_WARN("%s, recording ended", why);
    finish();
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

/* Writes queued audio up to `target` samples in total. Mixers push in blocks that rarely match a frame,
   so a lag or surplus within `slack` carries over; beyond it the track is padded or the oldest surplus
   dropped, which keeps the track on the caller's time. */
static bool write_audio_to(uint64_t target, uint64_t slack) {
    uint64_t due = target > s_rec.audio_written ? target - s_rec.audio_written : 0U;
    uint32_t n = due < s_rec.audio_queued ? (uint32_t)due : s_rec.audio_queued;
    if (n > 0 && !nt_recorder_platform_audio(s_rec.audio, n)) {
        return false;
    }
    s_rec.audio_written += n;
    s_rec.audio_queued -= n;
    uint32_t drop = s_rec.audio_queued > slack ? s_rec.audio_queued - (uint32_t)slack : 0U;
    s_rec.audio_trimmed += drop;
    s_rec.audio_queued -= drop;
    memmove(s_rec.audio, s_rec.audio + ((size_t)(n + drop) * 2U), (size_t)s_rec.audio_queued * 2U * sizeof(float));
    if (s_rec.audio_written + slack < target) {
        return write_silence(target - s_rec.audio_written);
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
    double audio_t = t - s_rec.t0;
    uint64_t target = audio_t > 0.0 ? (uint64_t)llround(audio_t * NT_RECORDER_AUDIO_RATE) : 0U;
    if (target == 0) {
        /* Pushed for an advance that ended at or before the start time. */
        s_rec.audio_trimmed += s_rec.audio_queued;
        s_rec.audio_queued = 0;
    }
    if (!write_audio_to(target, NT_RECORDER_AUDIO_RATE / s_rec.fps)) {
        fail("audio encoder failed");
        return;
    }
    /* Nearest sample: a deadline belongs to this frame once it is closer to t than to the neighbouring
       frames. The half-frame margin also absorbs float step_dt that rounds below 1 / fps; capping it at
       half a period keeps a long hitch from claiming deadlines the next frame is nearer to. */
    double period = 1.0 / (double)s_rec.fps;
    double margin = 0.5 * (dt < period ? dt : period);
    while (t + margin >= s_rec.t0 + ((double)(s_rec.frames + 1U) * period)) {
        if (!nt_recorder_platform_video(s_rec.frames)) {
            fail("video encoder failed");
            return;
        }
        s_rec.frames++;
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
    uint64_t target = ((uint64_t)s_rec.frames * NT_RECORDER_AUDIO_RATE + (s_rec.fps / 2U)) / s_rec.fps;
    if (!write_audio_to(target, 0)) {
        fail("audio encoder failed");
        return 0;
    }
    uint32_t frames = s_rec.frames;
    finish();
    return frames;
}

bool nt_recorder_active(void) { return s_rec.active; }
