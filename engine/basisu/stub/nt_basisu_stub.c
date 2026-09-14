#include "basisu/nt_basisu_transcoder.h"

#include "core/nt_assert.h"

/* Loud-fail stub: a BASIS texture reaching a stub build is a build-composition
   bug (basis content packed, transcoder not linked) — assert, don't mask with a
   placeholder. Under NT_ASSERT_MODE=OFF the failure returns route into
   nt_gfx's activate error path (log + FAILED asset). */
#define NT_BASISU_STUB_TRAP() NT_ASSERT(0 && "BASIS texture but nt_basisu_transcoder_stub linked -- link nt_basisu_transcoder")

void nt_basisu_transcoder_global_init(void) { NT_BASISU_STUB_TRAP(); }

bool nt_basisu_info(const void *basis_data, uint32_t basis_size, nt_basisu_info_t *out_info) {
    (void)basis_data;
    (void)basis_size;
    (void)out_info;
    NT_BASISU_STUB_TRAP();
    return false;
}

bool nt_basisu_transcode_chain(const void *basis_data, uint32_t basis_size, const nt_basisu_info_t *info, nt_texture_format_t format, void *output, uint32_t capacity_bytes) {
    (void)basis_data;
    (void)basis_size;
    (void)info;
    (void)format;
    (void)output;
    (void)capacity_bytes;
    NT_BASISU_STUB_TRAP();
    return false;
}
