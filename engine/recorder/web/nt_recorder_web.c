/* Web backend: thin calls into NtRecorder (nt_recorder_web.js), which holds the WebCodecs and
   Mediabunny code outside Closure. */

#include <emscripten.h>

#include "recorder/nt_recorder_internal.h"

/* clang-format off */
EM_JS_DEPS(nt_recorder_web, "$UTF8ToString")

EM_JS(int, nt_recorder_web_open, (const char *name, uint32_t w, uint32_t h, uint32_t fps), {
    return globalThis['NtRecorder']['open'](UTF8ToString(name), Module['canvas'], w, h, fps) ? 1 : 0;
})

EM_JS(int, nt_recorder_web_video, (uint32_t index), {
    return globalThis['NtRecorder']['video'](index) ? 1 : 0;
})

EM_JS(int, nt_recorder_web_audio, (const float *stereo, uint32_t frames), {
    return globalThis['NtRecorder']['audio'](HEAPF32.subarray(stereo >>> 2, (stereo >>> 2) + frames * 2), frames) ? 1 : 0;
})

EM_JS(void, nt_recorder_web_close, (uint32_t frames), {
    globalThis['NtRecorder']['close'](frames);
})
/* clang-format on */

bool nt_recorder_platform_open(const char *name, uint32_t width, uint32_t height, uint32_t fps) { return nt_recorder_web_open(name, width, height, fps) != 0; }

bool nt_recorder_platform_video(uint32_t index) { return nt_recorder_web_video(index) != 0; }

bool nt_recorder_platform_audio(const float *stereo, uint32_t frames) { return nt_recorder_web_audio(stereo, frames) != 0; }

void nt_recorder_platform_close(uint32_t frames) { nt_recorder_web_close(frames); }
