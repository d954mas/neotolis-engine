/* Windows backend: a Media Foundation sink writer streams H.264 + AAC into a fragmented MP4 at the
   name path, so a killed process keeps every fragment written before. Main thread; MF converts and
   encodes on its own threads. */

#define COBJMACROS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <string.h>

#include "core/nt_assert.h"
#include "graphics/nt_gfx.h"
#include "log/nt_log.h"
#include "recorder/nt_recorder_internal.h"
#include "time/nt_time.h"
#include "window/nt_window.h"

/* From codecapi.h, which compiles as C only after ks.h: CODECAPI_AVEncMPVDefaultBPictureCount,
   CODECAPI_AVEncMPVGOPSize, CODECAPI_AVEncCommonRateControlMode and two of their values. */
static const GUID k_bframes = {0x8d390aac, 0xdc5c, 0x4200, {0xb5, 0x7f, 0x81, 0x4d, 0x04, 0xba, 0xba, 0xb2}};
static const GUID k_gop = {0x95f31b26, 0x95a4, 0x41aa, {0x93, 0x03, 0x24, 0x6a, 0x7f, 0xc6, 0xee, 0xf1}};
static const GUID k_rate_control = {0x1c0608e9, 0x370c, 0x4710, {0x8a, 0x58, 0xcb, 0x61, 0x81, 0xc4, 0x24, 0x23}};
#define NT_RECORDER_H264_HIGH 100U          /* eAVEncH264VProfile_High */
#define NT_RECORDER_PEAK_CONSTRAINED_VBR 1U /* eAVEncCommonRateControlMode_PeakConstrainedVBR */

/* Frames in flight. MF keeps the last input sample until the next, so fewer than 2 deadlock; more than 4
   measured no faster. */
#define NT_RECORDER_POOL 4U

static struct {
    IMFSinkWriter *writer;
    IMFVideoSampleAllocatorEx *pool;
    DWORD video;
    DWORD audio;
    uint32_t width;
    uint32_t height;
    uint32_t fps;
    bool com; /* this recording holds a COM reference */
    wchar_t path[MAX_PATH];
} s_mf;

/* Calls that only fail on out-of-memory or a broken argument. */
static void must(HRESULT hr) { NT_ASSERT(SUCCEEDED(hr)); }

/* Calls MF may refuse at run time (a size the encoder rejects, an encoder failure). */
static bool ok(HRESULT hr, const char *call) {
    if (FAILED(hr)) {
        NT_LOG_WARN("%s failed 0x%08lx", call, (unsigned long)hr);
        return false;
    }
    return true;
}

/* 100 ns time of sample n at rate; durations are differences of these, so rounding never accumulates. */
static LONGLONG hns(uint64_t n, uint32_t rate) { return (LONGLONG)(n * 10000000ULL / rate); }

/* MFSetAttributeSize / MFSetAttributeRatio are C++-only. */
static void set_pair(IMFMediaType *type, const GUID *key, uint32_t hi, uint32_t lo) { must(IMFMediaType_SetUINT64(type, key, ((UINT64)hi << 32) | lo)); }

static IMFMediaType *video_type(const GUID *subtype) {
    IMFMediaType *type = NULL;
    must(MFCreateMediaType(&type));
    must(IMFMediaType_SetGUID(type, &MF_MT_MAJOR_TYPE, &MFMediaType_Video));
    must(IMFMediaType_SetGUID(type, &MF_MT_SUBTYPE, subtype));
    must(IMFMediaType_SetUINT32(type, &MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive));
    set_pair(type, &MF_MT_FRAME_SIZE, s_mf.width, s_mf.height);
    set_pair(type, &MF_MT_FRAME_RATE, s_mf.fps, 1);
    set_pair(type, &MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
    return type;
}

static IMFMediaType *audio_type(const GUID *subtype, uint32_t bits, uint32_t bytes_per_second) {
    IMFMediaType *type = NULL;
    must(MFCreateMediaType(&type));
    must(IMFMediaType_SetGUID(type, &MF_MT_MAJOR_TYPE, &MFMediaType_Audio));
    must(IMFMediaType_SetGUID(type, &MF_MT_SUBTYPE, subtype));
    must(IMFMediaType_SetUINT32(type, &MF_MT_AUDIO_BITS_PER_SAMPLE, bits));
    must(IMFMediaType_SetUINT32(type, &MF_MT_AUDIO_SAMPLES_PER_SECOND, NT_RECORDER_AUDIO_RATE));
    must(IMFMediaType_SetUINT32(type, &MF_MT_AUDIO_NUM_CHANNELS, 2));
    must(IMFMediaType_SetUINT32(type, &MF_MT_AUDIO_AVG_BYTES_PER_SECOND, bytes_per_second));
    return type;
}

/* Releases in dependency order; the file goes only when this recording's writer made it. */
static void release(bool remove_file) {
    if (s_mf.writer != NULL) {
        IMFSinkWriter_Release(s_mf.writer);
        s_mf.writer = NULL;
        if (remove_file) {
            (void)DeleteFileW(s_mf.path);
        }
    }
    if (s_mf.pool != NULL) {
        IMFVideoSampleAllocatorEx_Release(s_mf.pool);
        s_mf.pool = NULL;
    }
    (void)MFShutdown();
    if (s_mf.com) {
        CoUninitialize();
        s_mf.com = false;
    }
}

static bool create_writer(void) {
    IMFAttributes *attrs = NULL;
    must(MFCreateAttributes(&attrs, 3));
    must(IMFAttributes_SetGUID(attrs, &MF_TRANSCODE_CONTAINERTYPE, &MFTranscodeContainerType_FMPEG4));
    must(IMFAttributes_SetUINT32(attrs, &MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, TRUE));
    /* The sample pool bounds the frames in flight; throttling alone let a hardware encoder queue seconds
       of frames. */
    must(IMFAttributes_SetUINT32(attrs, &MF_SINK_WRITER_DISABLE_THROTTLING, TRUE));
    bool good = ok(MFCreateSinkWriterFromURL(s_mf.path, NULL, attrs, &s_mf.writer), "MFCreateSinkWriterFromURL");
    IMFAttributes_Release(attrs);
    if (!good) {
        return false;
    }

    // #region stream types
    IMFMediaType *vout = video_type(&MFVideoFormat_H264);
    must(IMFMediaType_SetUINT32(vout, &MF_MT_MPEG2_PROFILE, NT_RECORDER_H264_HIGH));
    must(IMFMediaType_SetUINT32(vout, &MF_MT_AVG_BITRATE, (UINT32)(0.1 * s_mf.width * s_mf.height * s_mf.fps)));
    /* Untagged output decodes with the wrong matrix in ffmpeg and players. */
    must(IMFMediaType_SetUINT32(vout, &MF_MT_YUV_MATRIX, MFVideoTransferMatrix_BT709));
    must(IMFMediaType_SetUINT32(vout, &MF_MT_VIDEO_PRIMARIES, MFVideoPrimaries_BT709));
    must(IMFMediaType_SetUINT32(vout, &MF_MT_TRANSFER_FUNCTION, MFVideoTransFunc_709));
    must(IMFMediaType_SetUINT32(vout, &MF_MT_VIDEO_NOMINAL_RANGE, MFNominalRange_16_235));
    IMFMediaType *vin = video_type(&MFVideoFormat_RGB32);
    must(IMFMediaType_SetUINT32(vin, &MF_MT_DEFAULT_STRIDE, s_mf.width * 4U)); /* positive: top-down rows */
    /* B-frames would delay the video start by a frame (the MP4 sink writes no edit list); the default
       hardware CBR pads a still screen to the full bitrate. */
    IMFAttributes *encoder = NULL;
    must(MFCreateAttributes(&encoder, 3));
    must(IMFAttributes_SetUINT32(encoder, &k_bframes, 0));
    must(IMFAttributes_SetUINT32(encoder, &k_gop, 2U * s_mf.fps));
    must(IMFAttributes_SetUINT32(encoder, &k_rate_control, NT_RECORDER_PEAK_CONSTRAINED_VBR));
    IMFMediaType *aout = audio_type(&MFAudioFormat_AAC, 16, 24000); /* 192 kbps */
    IMFMediaType *ain = audio_type(&MFAudioFormat_Float, 32, NT_RECORDER_AUDIO_RATE * 8U);
    must(IMFMediaType_SetUINT32(ain, &MF_MT_AUDIO_BLOCK_ALIGNMENT, 8));
    // #endregion

    good = ok(IMFSinkWriter_AddStream(s_mf.writer, vout, &s_mf.video), "AddStream video") && ok(IMFSinkWriter_SetInputMediaType(s_mf.writer, s_mf.video, vin, encoder), "SetInputMediaType video") &&
           ok(IMFSinkWriter_AddStream(s_mf.writer, aout, &s_mf.audio), "AddStream audio") && ok(IMFSinkWriter_SetInputMediaType(s_mf.writer, s_mf.audio, ain, NULL), "SetInputMediaType audio") &&
           ok(MFCreateVideoSampleAllocatorEx(&IID_IMFVideoSampleAllocatorEx, (void **)&s_mf.pool), "MFCreateVideoSampleAllocatorEx") &&
           ok(IMFVideoSampleAllocatorEx_InitializeSampleAllocatorEx(s_mf.pool, NT_RECORDER_POOL, NT_RECORDER_POOL, NULL, vin), "InitializeSampleAllocatorEx") &&
           ok(IMFSinkWriter_BeginWriting(s_mf.writer), "BeginWriting");
    IMFMediaType_Release(vout);
    IMFMediaType_Release(vin);
    IMFAttributes_Release(encoder);
    IMFMediaType_Release(aout);
    IMFMediaType_Release(ain);
    return good;
}

bool nt_recorder_platform_open(const char *name, uint32_t width, uint32_t height, uint32_t fps) {
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, name, -1, s_mf.path, MAX_PATH) == 0) {
        NT_LOG_WARN("%s: not a UTF-8 path under %d characters", name, MAX_PATH);
        return false;
    }
    s_mf.width = width;
    s_mf.height = height;
    s_mf.fps = fps;
    /* S_OK and S_FALSE add a reference this recording releases; RPC_E_CHANGED_MODE means the thread
       already has COM, which the free-threaded sink writer can use. */
    s_mf.com = SUCCEEDED(CoInitializeEx(NULL, COINIT_MULTITHREADED));
    must(MFStartup(MF_VERSION, MFSTARTUP_FULL)); /* the process could not start without Media Foundation */
    if (!create_writer()) {
        release(true);
        return false;
    }
    return true;
}

/* read_pixels leaves tight (w * 4) top-down RGBA rows; where MF pads the pitch each row moves to its
   slot, bottom up so no row lands on one not yet moved, and turns into BGRA on the way. */
static void place_rows(uint8_t *scan0, size_t pitch) {
    size_t row = (size_t)s_mf.width * 4U;
    for (uint32_t y = s_mf.height; y-- > 0;) {
        uint8_t *dst = scan0 + (y * pitch);
        if (pitch != row) {
            memmove(dst, scan0 + (y * row), row);
        }
        uint32_t *px = (uint32_t *)dst;
        for (uint32_t x = 0; x < s_mf.width; x++) {
            px[x] = (px[x] & 0xFF00FF00U) | ((px[x] >> 16) & 0xFFU) | ((px[x] & 0xFFU) << 16);
        }
    }
}

bool nt_recorder_platform_video(uint32_t index) {
    /* EMPTY means the encoder still holds every sample and returns them on its own threads (here
       AllocateSample waits about a second per call; MF does not promise it). Ten seconds of that is an
       encoder that died. */
    IMFSample *sample = NULL;
    HRESULT hr = MF_E_SAMPLEALLOCATOR_EMPTY;
    for (double until = nt_time_now() + 10.0; hr == MF_E_SAMPLEALLOCATOR_EMPTY && nt_time_now() < until;) {
        hr = IMFVideoSampleAllocatorEx_AllocateSample(s_mf.pool, &sample);
    }
    if (!ok(hr, "AllocateSample")) {
        return false;
    }
    IMFMediaBuffer *buffer = NULL;
    IMF2DBuffer *buffer2d = NULL;
    BYTE *scan0 = NULL;
    LONG pitch = 0;
    must(IMFSample_GetBufferByIndex(sample, 0, &buffer));
    must(IMFMediaBuffer_QueryInterface(buffer, &IID_IMF2DBuffer, (void **)&buffer2d));
    must(IMF2DBuffer_Lock2D(buffer2d, &scan0, &pitch));
    NT_ASSERT(pitch >= (LONG)s_mf.width * 4 && "nt_recorder: the RGB32 pool buffer is not top-down");
    /* The top rows: an odd framebuffer loses its bottom row, as on web. */
    bool good = nt_gfx_read_pixels((nt_render_target_t){0}, 0, (int)(g_nt_window.fb_height - s_mf.height), (int)s_mf.width, (int)s_mf.height, scan0, (uint32_t)pitch * s_mf.height);
    if (good) {
        place_rows(scan0, (size_t)pitch);
    }
    must(IMF2DBuffer_Unlock2D(buffer2d));
    if (good) {
        must(IMFMediaBuffer_SetCurrentLength(buffer, (DWORD)pitch * s_mf.height));
        must(IMFSample_SetSampleTime(sample, hns(index, s_mf.fps)));
        must(IMFSample_SetSampleDuration(sample, hns(index + 1ULL, s_mf.fps) - hns(index, s_mf.fps)));
        good = ok(IMFSinkWriter_WriteSample(s_mf.writer, s_mf.video, sample), "WriteSample video");
    }
    IMF2DBuffer_Release(buffer2d);
    IMFMediaBuffer_Release(buffer);
    IMFSample_Release(sample); /* back to the pool once the encoder is done with it */
    return good;
}

/* One small MF buffer per push: a dev-only allocation on the step path. */
bool nt_recorder_platform_audio(uint64_t at, const float *stereo, uint32_t frames) {
    NT_ASSERT(frames <= UINT32_MAX / 8U);
    DWORD bytes = frames * 8U;
    IMFMediaBuffer *buffer = NULL;
    IMFSample *sample = NULL;
    BYTE *data = NULL;
    must(MFCreateMemoryBuffer(bytes, &buffer));
    must(IMFMediaBuffer_Lock(buffer, &data, NULL, NULL));
    memcpy(data, stereo, bytes);
    must(IMFMediaBuffer_Unlock(buffer));
    must(IMFMediaBuffer_SetCurrentLength(buffer, bytes));
    must(MFCreateSample(&sample));
    must(IMFSample_AddBuffer(sample, buffer));
    must(IMFSample_SetSampleTime(sample, hns(at, NT_RECORDER_AUDIO_RATE)));
    must(IMFSample_SetSampleDuration(sample, hns(at + frames, NT_RECORDER_AUDIO_RATE) - hns(at, NT_RECORDER_AUDIO_RATE)));
    bool good = ok(IMFSinkWriter_WriteSample(s_mf.writer, s_mf.audio, sample), "WriteSample audio");
    IMFSample_Release(sample);
    IMFMediaBuffer_Release(buffer);
    return good;
}

/* Without a frame there is nothing to keep: Finalize refuses an empty video track. A failed Finalize
   leaves the fragments written before. */
void nt_recorder_platform_close(uint32_t frames) {
    if (frames > 0) {
        (void)ok(IMFSinkWriter_Finalize(s_mf.writer), "Finalize");
    }
    release(frames == 0);
}
