#include "recorder/nt_recorder.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "core/nt_assert.h"
#include "log/nt_log.h"
#include "recorder/nt_recorder_internal.h"
#include "window/nt_window.h"

#define NT_RECORDER_NAME_MAX 256
/* Audio pushed between two frame calls; one second covers any clamped frame. */
#define NT_RECORDER_AUDIO_CAP NT_RECORDER_AUDIO_RATE

static struct {
    bool active;
    char name[NT_RECORDER_NAME_MAX];
    uint32_t fps;
    double t0;
    uint32_t fb_width;
    uint32_t fb_height;
    uint32_t frames;
    float *audio;          /* NT_RECORDER_AUDIO_CAP stereo frames */
    uint32_t audio_queued; /* pushed since the last frame call */
    uint64_t audio_written;
    uint64_t audio_padded;
    uint64_t audio_trimmed;
} s_rec;

static void finish(void) {
    nt_recorder_platform_close(s_rec.frames);
    NT_LOG_INFO("%s: %u frames, audio padded %llu trimmed %llu samples", s_rec.name, s_rec.frames, (unsigned long long)s_rec.audio_padded, (unsigned long long)s_rec.audio_trimmed);
    free(s_rec.audio);
    s_rec.audio = NULL;
    s_rec.active = false;
}

static void fail(const char *why) {
    NT_LOG_WARN("%s: %s, recording ended", s_rec.name, why);
    finish();
}

/* Writes the queued audio, trimmed or padded with silence to `target` samples in total. */
static bool write_audio_to(uint64_t target) {
    uint32_t used = s_rec.audio_queued;
    if (s_rec.audio_written + used > target) {
        used = (target > s_rec.audio_written) ? (uint32_t)(target - s_rec.audio_written) : 0U;
    }
    s_rec.audio_trimmed += s_rec.audio_queued - used;
    s_rec.audio_queued = 0;
    if (used > 0 && !nt_recorder_platform_audio(s_rec.audio, used)) {
        return false;
    }
    s_rec.audio_written += used;
    if (s_rec.audio_written < target) {
        memset(s_rec.audio, 0, (size_t)NT_RECORDER_AUDIO_CAP * 2U * sizeof(float));
    }
    while (s_rec.audio_written < target) {
        uint64_t left = target - s_rec.audio_written;
        uint32_t n = left < NT_RECORDER_AUDIO_CAP ? (uint32_t)left : NT_RECORDER_AUDIO_CAP;
        if (!nt_recorder_platform_audio(s_rec.audio, n)) {
            return false;
        }
        s_rec.audio_written += n;
        s_rec.audio_padded += n;
    }
    return true;
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
bool nt_recorder_start(const char *name, uint32_t fps, double t) {
    NT_ASSERT(!s_rec.active && "nt_recorder_start: already recording");
    NT_ASSERT(name != NULL && name[0] != '\0' && strlen(name) < NT_RECORDER_NAME_MAX);
    NT_ASSERT(fps >= 1 && fps <= 120);
    uint32_t w = g_nt_window.fb_width & ~1U;
    uint32_t h = g_nt_window.fb_height & ~1U;
    if (w == 0 || h == 0) {
        NT_LOG_WARN("%s: framebuffer is %ux%u, not recording", name, g_nt_window.fb_width, g_nt_window.fb_height);
        return false;
    }
    if (!nt_recorder_platform_open(name, w, h, fps)) {
        NT_LOG_WARN("%s: cannot open the encoder or the output", name);
        return false;
    }
    memset(&s_rec, 0, sizeof(s_rec));
    s_rec.active = true;
    s_rec.fps = fps;
    s_rec.t0 = t;
    s_rec.fb_width = g_nt_window.fb_width;
    s_rec.fb_height = g_nt_window.fb_height;
    memcpy(s_rec.name, name, strlen(name) + 1);
    s_rec.audio = (float *)malloc((size_t)NT_RECORDER_AUDIO_CAP * 2U * sizeof(float));
    NT_ASSERT(s_rec.audio != NULL);
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
    /* File time 0 is the first frame's deadline t0 + 1 / fps for both tracks, so a sound and the frame
       showing its cause share a timestamp; audio from before it is trimmed. */
    double audio_t = t - s_rec.t0 - (1.0 / (double)s_rec.fps);
    if (!write_audio_to(audio_t > 0.0 ? (uint64_t)llround(audio_t * NT_RECORDER_AUDIO_RATE) : 0U)) {
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
    if (!s_rec.active) {
        return;
    }
    NT_ASSERT(stereo48k != NULL || frames == 0);
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
    uint32_t frames = s_rec.frames;
    if (frames == 0) {
        NT_LOG_WARN("%s: stopped before the first frame, nothing written", s_rec.name);
    }
    /* Samples queued after the last frame call are past the recorded time. */
    s_rec.audio_trimmed += s_rec.audio_queued;
    s_rec.audio_queued = 0;
    uint64_t target = ((uint64_t)frames * NT_RECORDER_AUDIO_RATE + (s_rec.fps / 2U)) / s_rec.fps;
    if (!write_audio_to(target)) {
        fail("audio encoder failed");
        return frames;
    }
    finish();
    return frames;
}

bool nt_recorder_active(void) { return s_rec.active; }
