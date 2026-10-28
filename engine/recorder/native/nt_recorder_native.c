/* Windows backend: a Media Foundation sink writer streams H.264 + AAC into a fragmented MP4 at the
   name path, so a killed process keeps every fragment written before. Main thread; MF reads the
   window's pixels in place from the readback buffers, converts them (on a D3D11 device when one can
   be made) and encodes on its own threads. */

#define COBJMACROS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <d3d10_1.h> /* ID3D10Multithread */
#include <d3d11.h>
#include <limits.h>
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

/* Window reads in flight: a read is encoded three reads after it was issued, so the GPU has normally
   written it and the fence wait returns at once. */
#define NT_RECORDER_READS 3U
/* Readback buffers: the reads in flight plus the encoded ones MF still holds. MF keeps the last input
   sample until the next, so a ring of only the reads would wait on the frame just sent. */
#define NT_RECORDER_SLOTS 5U

/* One readback buffer and the MF buffer over its bytes: MF reads the pixels in place and holds a
   reference per sample, so the slot takes no new read until MF has released them all. */
typedef struct {
    IMFMediaBuffer mf; /* first: MF calls back with this pointer */
    volatile LONG refs;
    nt_buffer_t read;
    BYTE *bytes; /* read-only mapping: only MF's Lock signature drops the const */
    uint32_t first;
    uint32_t count; /* the output frames that show this read */
} nt_recorder_slot_t;

static struct {
    IMFSinkWriter *writer;
    ID3D11Device *device;
    IMFDXGIDeviceManager *devices;
    DWORD video;
    DWORD audio;
    uint32_t width;
    uint32_t height;
    uint32_t fps;
    nt_recorder_slot_t slots[NT_RECORDER_SLOTS]; /* a FIFO ring: oldest read at head - pending */
    uint32_t head;
    uint32_t pending;
    bool wrote;      /* a video sample reached the encoder: the file has something to keep */
    bool com;        /* this recording holds a COM reference */
    bool broken;     /* a write failed: the encoder may never drain */
    HANDLE released; /* set when MF drops a slot reference; lives with the process */
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

// #region slot buffer
/* MF calls these on its own threads. The slots are static: the count only tells when MF is done. */
static ULONG STDMETHODCALLTYPE slot_add_ref(IMFMediaBuffer *self) { return (ULONG)InterlockedIncrement(&((nt_recorder_slot_t *)self)->refs); }
static ULONG STDMETHODCALLTYPE slot_release(IMFMediaBuffer *self) {
    LONG refs = InterlockedDecrement(&((nt_recorder_slot_t *)self)->refs);
    (void)SetEvent(s_mf.released);
    return (ULONG)refs;
}
static HRESULT STDMETHODCALLTYPE slot_query(IMFMediaBuffer *self, REFIID riid, void **out) {
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IMFMediaBuffer)) {
        (void)slot_add_ref(self);
        *out = self;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static DWORD slot_size(void) { return s_mf.width * s_mf.height * 4U; }
static HRESULT STDMETHODCALLTYPE slot_lock(IMFMediaBuffer *self, BYTE **bytes, DWORD *max, DWORD *current) {
    *bytes = ((nt_recorder_slot_t *)self)->bytes;
    if (max != NULL) {
        *max = slot_size();
    }
    if (current != NULL) {
        *current = slot_size();
    }
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE slot_unlock(IMFMediaBuffer *self) {
    (void)self;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE slot_length(IMFMediaBuffer *self, DWORD *length) {
    (void)self;
    *length = slot_size();
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE slot_set_length(IMFMediaBuffer *self, DWORD length) {
    (void)self;
    return length == slot_size() ? S_OK : E_INVALIDARG;
}
static IMFMediaBufferVtbl s_slot_vtbl = {slot_query, slot_add_ref, slot_release, slot_lock, slot_unlock, slot_length, slot_set_length, slot_length};

/* MF drops a slot's references on its own threads as its pipeline consumes each frame. A catch-up
   slot carries up to 2 * fps frames, so the limit is ten seconds without one released, not for all. */
static bool wait_released(nt_recorder_slot_t *slot) {
    LONG last = LONG_MAX;
    ULONGLONG until = 0;
    for (LONG refs; (refs = InterlockedCompareExchange(&slot->refs, 0, 0)) > 0;) {
        ULONGLONG now = GetTickCount64();
        if (refs < last) {
            last = refs;
            until = now + 10000U;
        } else if (now >= until) {
            return false;
        }
        (void)WaitForSingleObject(s_mf.released, (DWORD)(until - now));
    }
    return true;
}
// #endregion

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
   MFShutdown, since the sink may close it on an MF thread. The readback buffers go last: MF samples
   point into them until the pipeline is gone. */
static void release(bool remove_file) {
    bool made = s_mf.writer != NULL;
    if (made) {
        IMFSinkWriter_Release(s_mf.writer);
        s_mf.writer = NULL;
    }
    if (s_mf.devices != NULL) {
        IMFDXGIDeviceManager_Release(s_mf.devices);
        s_mf.devices = NULL;
    }
    if (s_mf.device != NULL) {
        ID3D11Device_Release(s_mf.device);
        s_mf.device = NULL;
    }
    (void)MFShutdown();
    if (s_mf.com) {
        CoUninitialize();
        s_mf.com = false;
    }
    for (uint32_t i = 0; i < NT_RECORDER_SLOTS; i++) {
        nt_recorder_slot_t *slot = &s_mf.slots[i];
        if (slot->read.id != 0) {
            bool idle = wait_released(slot);
            NT_ASSERT(idle && "nt_recorder: Media Foundation still holds a frame after MFShutdown");
            nt_gfx_destroy_buffer(slot->read);
            slot->read = (nt_buffer_t){0};
        }
    }
    if (made && remove_file && !DeleteFileW(s_mf.path)) {
        NT_LOG_WARN("the empty recording was not deleted (error %lu)", GetLastError());
    }
}

/* With this device MF can convert RGB32 to NV12 on the GPU; its CPU converter took about a third of
   a core at 1080p30. Without a hardware device with video support it converts on the CPU. */
static void attach_device(IMFAttributes *attrs) {
    HRESULT hr = D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, D3D11_CREATE_DEVICE_VIDEO_SUPPORT, NULL, 0, D3D11_SDK_VERSION, &s_mf.device, NULL, NULL);
    if (FAILED(hr)) {
        NT_LOG_WARN("no D3D11 video device (0x%08lx): colour conversion on the CPU", (unsigned long)hr);
        return;
    }
    /* MF uses the device from its own threads. */
    ID3D10Multithread *multithread = NULL;
    must(ID3D11Device_QueryInterface(s_mf.device, &IID_ID3D10Multithread, (void **)&multithread));
    (void)ID3D10Multithread_SetMultithreadProtected(multithread, TRUE);
    ID3D10Multithread_Release(multithread);
    UINT token = 0;
    must(MFCreateDXGIDeviceManager(&token, &s_mf.devices));
    must(IMFDXGIDeviceManager_ResetDevice(s_mf.devices, (IUnknown *)s_mf.device, token));
    must(IMFAttributes_SetUnknown(attrs, &MF_SINK_WRITER_D3D_MANAGER, (IUnknown *)s_mf.devices));
}

static bool create_writer(void) {
    IMFAttributes *attrs = NULL;
    must(MFCreateAttributes(&attrs, 4));
    attach_device(attrs);
    must(IMFAttributes_SetGUID(attrs, &MF_TRANSCODE_CONTAINERTYPE, &MFTranscodeContainerType_FMPEG4));
    must(IMFAttributes_SetUINT32(attrs, &MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, TRUE));
    /* The readback slots bound the pixels in flight; throttling alone let a hardware encoder queue
       seconds of frames. */
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
    /* Negative: bottom-up rows, as GL reads them, so nothing flips the frame. */
    must(IMFMediaType_SetUINT32(vin, &MF_MT_DEFAULT_STRIDE, (UINT32) - (INT32)(s_mf.width * 4U)));
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
           ok(IMFSinkWriter_BeginWriting(s_mf.writer), "BeginWriting");
    IMFMediaType_Release(vout);
    IMFMediaType_Release(vin);
    IMFAttributes_Release(encoder);
    IMFMediaType_Release(aout);
    IMFMediaType_Release(ain);
    return good;
}

bool nt_recorder_platform_ready(void) { return true; }

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
    for (uint32_t i = 0; i < NT_RECORDER_SLOTS; i++) {
        s_mf.slots[i].mf.lpVtbl = &s_slot_vtbl;
        s_mf.slots[i].read = nt_gfx_make_buffer(&(nt_buffer_desc_t){.type = NT_BUFFER_READBACK, .size = width * height * 4U, .label = "recorder read"});
    }
    return true;
}

/* One sample per output frame over the slot's pixels; repeated frames share the slot with their own pts. */
static void write_frame(nt_recorder_slot_t *slot, uint32_t index) {
    IMFSample *sample = NULL;
    must(MFCreateSample(&sample));
    must(IMFSample_AddBuffer(sample, &slot->mf));
    must(IMFSample_SetSampleTime(sample, hns(index, s_mf.fps)));
    must(IMFSample_SetSampleDuration(sample, hns(index + 1ULL, s_mf.fps) - hns(index, s_mf.fps)));
    s_mf.broken = !ok(IMFSinkWriter_WriteSample(s_mf.writer, s_mf.video, sample), "WriteSample video");
    s_mf.wrote |= !s_mf.broken;
    IMFSample_Release(sample);
}

/* Encodes the oldest read; false once a write failed (broken). Reads leave the ring in issue order, so
   the encoder gets pts in order. */
static bool encode_oldest(void) {
    nt_recorder_slot_t *slot = &s_mf.slots[(s_mf.head + NT_RECORDER_SLOTS - s_mf.pending) % NT_RECORDER_SLOTS];
    slot->bytes = (BYTE *)nt_gfx_wait_readback(slot->read);
    s_mf.pending--;
    for (uint32_t i = 0; !s_mf.broken && i < slot->count; i++) {
        write_frame(slot, slot->first + i);
    }
    return !s_mf.broken;
}

bool nt_recorder_platform_video(uint32_t first, uint32_t count) {
    if (s_mf.pending == NT_RECORDER_READS && !encode_oldest()) {
        return false;
    }
    nt_recorder_slot_t *slot = &s_mf.slots[s_mf.head];
    if (!wait_released(slot)) {
        NT_LOG_WARN("the encoder kept a frame for 10 s");
        s_mf.broken = true;
        return false;
    }
    /* The top rows: an odd framebuffer loses its bottom row and right column, as on web. */
    nt_gfx_read_pixels_to_buffer((nt_render_target_t){0}, 0, (int)(g_nt_window.fb_height - s_mf.height), (int)s_mf.width, (int)s_mf.height, slot->read);
    slot->first = first;
    slot->count = count;
    s_mf.head = (s_mf.head + 1U) % NT_RECORDER_SLOTS;
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
