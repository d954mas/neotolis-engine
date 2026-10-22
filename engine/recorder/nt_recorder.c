#include "recorder/nt_recorder.h"

#include <math.h>

#include "core/nt_assert.h"
#include "log/nt_log.h"
#include "recorder/nt_recorder_internal.h"
#include "window/nt_window.h"

/* Seconds the pushed stream may differ from the caller's clock: one mixer block and a frame fit; past
   this it has a gap or a late start. */
#define NT_RECORDER_AUDIO_DRIFT 0.1

static struct {
    bool active;
    bool pushed; /* the game's mixer has pushed; until then the track is silence beside the video */
    bool drift_warned;
    uint32_t fps;
    double t0;
    uint32_t width;
    uint32_t height;
    uint32_t frames;
    uint64_t audio; /* samples written: the track position */
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

/* Pads the track with silence to the end of the written video. */
static bool pad_to_video(void) {
    static const float k_zero[1024 * 2];
    uint64_t end = (uint64_t)llround((double)s_rec.frames * NT_RECORDER_AUDIO_RATE / (double)s_rec.fps);
    while (s_rec.audio < end) {
        uint32_t n = end - s_rec.audio < 1024U ? (uint32_t)(end - s_rec.audio) : 1024U;
        if (!nt_recorder_platform_audio(k_zero, n)) {
            return false;
        }
        s_rec.audio += n;
    }
    return true;
}

/* Ends with the audio track padded to the video; audio pushed past it stays. */
static uint32_t end_recording(void) {
    if (s_rec.frames == 0) {
        NT_LOG_WARN("ended before the first frame, nothing written");
    }
    if (!pad_to_video()) {
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
    s_rec.pushed = false;
    s_rec.drift_warned = false;
    s_rec.fps = fps;
    s_rec.t0 = t;
    s_rec.width = w;
    s_rec.height = h;
    s_rec.frames = 0;
    s_rec.audio = 0;
    NT_LOG_INFO("%s: recording %ux%u at %u fps", name, w, h, fps);
    return true;
}

void nt_recorder_frame(double t, double dt) {
    if (!s_rec.active) {
        return;
    }
    NT_ASSERT(isfinite(t - s_rec.t0) && isfinite(dt) && dt >= 0.0);
    if ((g_nt_window.fb_width & ~1U) != s_rec.width || (g_nt_window.fb_height & ~1U) != s_rec.height) {
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
    while (frames - s_rec.frames <= 2U * s_rec.fps && t + margin >= s_rec.t0 + ((double)(frames + 1U) * period)) {
        frames++;
    }
    /* Catching up more than two seconds (render off while time ran) would flood the encoder. */
    if (frames - s_rec.frames > 2U * s_rec.fps) {
        NT_LOG_WARN("%.1f s without frames, recording ended", t - (s_rec.t0 + ((double)s_rec.frames * period)));
        (void)end_recording();
        return;
    }
    for (; s_rec.frames < frames; s_rec.frames++) {
        if (!nt_recorder_platform_video(s_rec.frames)) {
            fail();
            return;
        }
    }
    /* Silence keeps pace with the video until the first push, so a long silent clip never pads at once
       and a mixer that starts later continues from the video end. */
    if (!s_rec.pushed) {
        if (!pad_to_video()) {
            fail();
        }
        return;
    }
    /* Audio is a stream without timestamps: a mixer that skips shifts the rest of the track, so say so
       once. */
    double drift = ((double)s_rec.audio / NT_RECORDER_AUDIO_RATE) - (t - s_rec.t0);
    if (!s_rec.drift_warned && fabs(drift) > NT_RECORDER_AUDIO_DRIFT) {
        s_rec.drift_warned = true;
        NT_LOG_WARN("audio %.0f ms %s the clock; push every step's samples from the start, silence included", fabs(drift) * 1000.0, drift < 0.0 ? "behind" : "ahead of");
    }
}

void nt_recorder_push_audio(const float *stereo48k, uint32_t frames) {
    if (!s_rec.active || frames == 0) {
        return;
    }
    NT_ASSERT(stereo48k != NULL);
    s_rec.pushed = true;
    if (!nt_recorder_platform_audio(stereo48k, frames)) {
        fail();
        return;
    }
    s_rec.audio += frames;
}

uint32_t nt_recorder_stop(void) { return s_rec.active ? end_recording() : 0; }

bool nt_recorder_active(void) { return s_rec.active; }
