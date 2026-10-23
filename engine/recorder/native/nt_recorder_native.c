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
#include "window/nt_window.h"

/* From codecapi.h, which compiles as C only after ks.h: CODECAPI_AVEncMPVDefaultBPictureCount,
   CODECAPI_AVEncMPVGOPSize, CODECAPI_AVEncCommonRateControlMode, CODECAPI_AVEncCommonMaxBitRate and
   two values. */
static const GUID k_bframes = {0x8d390aac, 0xdc5c, 0x4200, {0xb5, 0x7f, 0x81, 0x4d, 0x04, 0xba, 0xba, 0xb2}};
static const GUID k_gop = {0x95f31b26, 0x95a4, 0x41aa, {0x93, 0x03, 0x24, 0x6a, 0x7f, 0xc6, 0xee, 0xf1}};
static const GUID k_rate_control = {0x1c0608e9, 0x370c, 0x4710, {0x8a, 0x58, 0xcb, 0x61, 0x81, 0xc4, 0x24, 0x23}};
static const GUID k_max_bitrate = {0x9651eae4, 0x39b9, 0x4ebf, {0x85, 0xef, 0xd7, 0xf4, 0x44, 0xec, 0x74, 0x65}};
#define NT_RECORDER_H264_HIGH 100U          /* eAVEncH264VProfile_High */
#define NT_RECORDER_PEAK_CONSTRAINED_VBR 1U /* eAVEncCommonRateControlMode_PeakConstrainedVBR */

/* Frames in flight. MF keeps the last input sample until the next, so fewer than 2 deadlock; more than 4
   measured no faster. */
#define NT_RECORDER_POOL 4U
/* Window reads in flight: a read is mapped when the ring is full, three reads after it was issued,
   so the GPU has normally written it and the map does not wait. */
#define NT_RECORDER_READS 3U

static struct {
    IMFSinkWriter *writer;
    IMFVideoSampleAllocatorEx *pool;
    DWORD video;
    DWORD audio;
    uint32_t width;
    uint32_t height;
    uint32_t fps;
    nt_buffer_t reads[NT_RECORDER_READS]; /* a FIFO ring: oldest at head - pending */
    uint32_t read_first[NT_RECORDER_READS];
    uint32_t read_count[NT_RECORDER_READS]; /* the output frames that show this read */
    uint32_t head;
    uint32_t pending;
    bool wrote;      /* a video sample reached the encoder: the file has something to keep */
    bool com;        /* this recording holds a COM reference */
    bool broken;     /* a write failed: the encoder may never drain */
    HANDLE released; /* set when the pool gets a sample back; lives with the process, as does s_notify */
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

/* The pool calls NotifyRelease on an MF thread when the encoder returns a sample; static, so it counts
   no references. */
static HRESULT STDMETHODCALLTYPE notify_query(IMFVideoSampleAllocatorNotify *self, REFIID riid, void **out) {
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IMFVideoSampleAllocatorNotify)) {
        *out = self;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG STDMETHODCALLTYPE notify_ref(IMFVideoSampleAllocatorNotify *self) {
    (void)self;
    return 1;
}
static HRESULT STDMETHODCALLTYPE notify_release(IMFVideoSampleAllocatorNotify *self) {
    (void)self;
    (void)SetEvent(s_mf.released);
    return S_OK;
}
static IMFVideoSampleAllocatorNotifyVtbl s_notify_vtbl = {notify_query, notify_ref, notify_ref, notify_release};
static IMFVideoSampleAllocatorNotify s_notify = {&s_notify_vtbl};

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

/* Releases in dependency order; the file goes only when this recording's writer made it, and after
   MFShutdown, since the sink may close it on an MF thread. */
static void release(bool remove_file) {
    for (uint32_t i = 0; i < NT_RECORDER_READS; i++) {
        if (s_mf.reads[i].id != 0) {
            nt_gfx_destroy_buffer(s_mf.reads[i]);
            s_mf.reads[i] = (nt_buffer_t){0};
        }
    }
    bool made = s_mf.writer != NULL;
    if (made) {
        IMFSinkWriter_Release(s_mf.writer);
        s_mf.writer = NULL;
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
    if (made && remove_file && !DeleteFileW(s_mf.path)) {
        NT_LOG_WARN("the empty recording was not deleted (error %lu)", GetLastError());
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
    UINT32 bitrate = (UINT32)(0.1 * s_mf.width * s_mf.height * s_mf.fps);
    must(IMFMediaType_SetUINT32(vout, &MF_MT_AVG_BITRATE, bitrate));
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
    must(MFCreateAttributes(&encoder, 4));
    must(IMFAttributes_SetUINT32(encoder, &k_bframes, 0));
    must(IMFAttributes_SetUINT32(encoder, &k_gop, 2U * s_mf.fps));
    must(IMFAttributes_SetUINT32(encoder, &k_rate_control, NT_RECORDER_PEAK_CONSTRAINED_VBR));
    must(IMFAttributes_SetUINT32(encoder, &k_max_bitrate, 2U * bitrate));
    IMFMediaType *aout = audio_type(&MFAudioFormat_AAC, 16, 24000); /* 192 kbps */
    IMFMediaType *ain = audio_type(&MFAudioFormat_Float, 32, NT_RECORDER_AUDIO_RATE * 8U);
    must(IMFMediaType_SetUINT32(ain, &MF_MT_AUDIO_BLOCK_ALIGNMENT, 8));
    // #endregion

    good = ok(IMFSinkWriter_AddStream(s_mf.writer, vout, &s_mf.video), "AddStream video") && ok(IMFSinkWriter_SetInputMediaType(s_mf.writer, s_mf.video, vin, encoder), "SetInputMediaType video") &&
           ok(IMFSinkWriter_AddStream(s_mf.writer, aout, &s_mf.audio), "AddStream audio") && ok(IMFSinkWriter_SetInputMediaType(s_mf.writer, s_mf.audio, ain, NULL), "SetInputMediaType audio") &&
           ok(MFCreateVideoSampleAllocatorEx(&IID_IMFVideoSampleAllocatorEx, (void **)&s_mf.pool), "MFCreateVideoSampleAllocatorEx") &&
           ok(IMFVideoSampleAllocatorEx_InitializeSampleAllocatorEx(s_mf.pool, NT_RECORDER_POOL, NT_RECORDER_POOL, NULL, vin), "InitializeSampleAllocatorEx") &&
           ok(IMFSinkWriter_BeginWriting(s_mf.writer), "BeginWriting");
    if (good) {
        IMFVideoSampleAllocatorCallback *callback = NULL;
        must(IMFVideoSampleAllocatorEx_QueryInterface(s_mf.pool, &IID_IMFVideoSampleAllocatorCallback, (void **)&callback));
        must(IMFVideoSampleAllocatorCallback_SetCallback(callback, &s_notify));
        IMFVideoSampleAllocatorCallback_Release(callback);
    }
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
    s_mf.broken = false;
    s_mf.head = 0;
    s_mf.pending = 0;
    s_mf.wrote = false;
    if (s_mf.released == NULL) {
        s_mf.released = CreateEventW(NULL, FALSE, FALSE, NULL);
        NT_ASSERT(s_mf.released != NULL);
    }
    /* S_OK and S_FALSE add a reference this recording releases; RPC_E_CHANGED_MODE means the thread
       already has COM, which the free-threaded sink writer can use. */
    s_mf.com = SUCCEEDED(CoInitializeEx(NULL, COINIT_MULTITHREADED));
    must(MFStartup(MF_VERSION, MFSTARTUP_FULL)); /* the process could not start without Media Foundation */
    if (!create_writer()) {
        release(true);
        return false;
    }
    for (uint32_t i = 0; i < NT_RECORDER_READS; i++) {
        s_mf.reads[i] = nt_gfx_make_buffer(&(nt_buffer_desc_t){.type = NT_BUFFER_READBACK, .size = width * height * 4U, .label = "recorder read"});
    }
    return true;
}

/* The read holds BGRA rows bottom-up; MF wants them top-down at its pitch. */
static void copy_rows(uint8_t *scan0, size_t pitch, const uint8_t *bytes) {
    size_t row = (size_t)s_mf.width * 4U;
    for (uint32_t y = 0; y < s_mf.height; y++) {
        memcpy(scan0 + (y * pitch), bytes + ((size_t)(s_mf.height - 1U - y) * row), row);
    }
}

/* One pooled sample per output frame, filled from the mapped read. */
static void write_frame(uint32_t index, const uint8_t *bytes) {
    /* EMPTY means the encoder still holds every sample; it returns them on its own threads and
       s_notify wakes this wait. Ten seconds of that is an encoder that died. */
    IMFSample *sample = NULL;
    HRESULT hr = MF_E_SAMPLEALLOCATOR_EMPTY;
    for (ULONGLONG until = GetTickCount64() + 10000U; (hr = IMFVideoSampleAllocatorEx_AllocateSample(s_mf.pool, &sample)) == MF_E_SAMPLEALLOCATOR_EMPTY;) {
        ULONGLONG now = GetTickCount64();
        if (now >= until || WaitForSingleObject(s_mf.released, (DWORD)(until - now)) != WAIT_OBJECT_0) {
            break;
        }
    }
    if (!ok(hr, "AllocateSample")) {
        s_mf.broken = true;
        return;
    }
    IMFMediaBuffer *buffer = NULL;
    IMF2DBuffer *buffer2d = NULL;
    BYTE *scan0 = NULL;
    LONG pitch = 0;
    must(IMFSample_GetBufferByIndex(sample, 0, &buffer));
    must(IMFMediaBuffer_QueryInterface(buffer, &IID_IMF2DBuffer, (void **)&buffer2d));
    must(IMF2DBuffer_Lock2D(buffer2d, &scan0, &pitch));
    NT_ASSERT(pitch >= (LONG)s_mf.width * 4 && "nt_recorder: the RGB32 pool buffer is not top-down");
    copy_rows(scan0, (size_t)pitch, bytes);
    must(IMF2DBuffer_Unlock2D(buffer2d));
    must(IMFMediaBuffer_SetCurrentLength(buffer, (DWORD)pitch * s_mf.height));
    must(IMFSample_SetSampleTime(sample, hns(index, s_mf.fps)));
    must(IMFSample_SetSampleDuration(sample, hns(index + 1ULL, s_mf.fps) - hns(index, s_mf.fps)));
    s_mf.broken = !ok(IMFSinkWriter_WriteSample(s_mf.writer, s_mf.video, sample), "WriteSample video");
    s_mf.wrote |= !s_mf.broken;
    IMF2DBuffer_Release(buffer2d);
    IMFMediaBuffer_Release(buffer);
    IMFSample_Release(sample); /* back to the pool once the encoder is done with it */
}

/* Encodes the oldest read; false once a write failed (broken). Reads leave the ring in issue order, so
   the encoder gets pts in order. */
static bool encode_oldest(void) {
    uint32_t slot = (s_mf.head + NT_RECORDER_READS - s_mf.pending) % NT_RECORDER_READS;
    const uint8_t *bytes = nt_gfx_map_readback(s_mf.reads[slot]);
    s_mf.pending--;
    for (uint32_t i = 0; !s_mf.broken && i < s_mf.read_count[slot]; i++) {
        write_frame(s_mf.read_first[slot] + i, bytes);
    }
    nt_gfx_unmap_readback(s_mf.reads[slot]);
    return !s_mf.broken;
}

bool nt_recorder_platform_video(uint32_t first, uint32_t count) {
    if (s_mf.pending == NT_RECORDER_READS && !encode_oldest()) {
        return false;
    }
    /* The top rows: an odd framebuffer loses its bottom row and right column, as on web. */
    nt_gfx_read_pixels_to_buffer((nt_render_target_t){0}, 0, (int)(g_nt_window.fb_height - s_mf.height), (int)s_mf.width, (int)s_mf.height, s_mf.reads[s_mf.head]);
    s_mf.read_first[s_mf.head] = first;
    s_mf.read_count[s_mf.head] = count;
    s_mf.head = (s_mf.head + 1U) % NT_RECORDER_READS;
    s_mf.pending++;
    return true;
}

/* One MF buffer per push, beside the per-sample allocations inside the MF pipeline itself. */
bool nt_recorder_platform_audio(uint64_t at, const float *stereo, uint32_t frames) {
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
    s_mf.broken = !good;
    IMFSample_Release(sample);
    IMFMediaBuffer_Release(buffer);
    return good;
}

/* The reads still in the ring go to the encoder first. Without a frame there is nothing to keep:
   Finalize refuses an empty video track. After a failed write Finalize could wait forever on the
   encoder, so it is skipped; either way the fragments on disk stay. */
bool nt_recorder_platform_close(void) {
    while (!s_mf.broken && s_mf.pending > 0 && encode_oldest()) {
    }
    if (s_mf.wrote && !s_mf.broken) {
        (void)ok(IMFSinkWriter_Finalize(s_mf.writer), "Finalize");
    }
    release(!s_mf.wrote);
    return !s_mf.broken;
}
