#include "basisu/nt_basisu_transcoder.h"

#include "core/nt_assert.h"

/* Loud-fail stub: a BASIS texture reaching a stub build is a build-composition
   bug (basis content packed, transcoder not linked) — assert, don't mask with a
   placeholder. */
#define NT_BASISU_STUB_TRAP() NT_ASSERT(0 && "BASIS texture but nt_basisu_transcoder_stub linked -- link nt_basisu_transcoder")

void nt_basisu_transcoder_global_init(void) { NT_BASISU_STUB_TRAP(); }

nt_basisu_info_t nt_basisu_info(const void *basis_data, uint32_t basis_size) {
    (void)basis_data;
    (void)basis_size;
    NT_BASISU_STUB_TRAP();
}

void nt_basisu_transcode_chain(const void *basis_data, uint32_t basis_size, const nt_basisu_info_t *info, nt_texture_format_t format, void *output) {
    (void)basis_data;
    (void)basis_size;
    (void)info;
    (void)format;
    (void)output;
    NT_BASISU_STUB_TRAP();
}
