#include "basisu/nt_basisu_transcoder.h"

#include "core/nt_assert.h"

/* Loud-fail stub: a BASIS texture reaching a stub build is a build-composition
   bug (basis content packed, transcoder not linked) — assert, don't mask with a
   placeholder. Under NT_ASSERT_MODE=OFF the failure returns route into
   nt_gfx's activate error path (log + FAILED asset). */
#define NT_BASISU_STUB_TRAP() NT_ASSERT(0 && "BASIS texture but nt_basisu_transcoder_stub linked -- link nt_basisu_transcoder")

void nt_basisu_transcoder_global_init(void) { NT_BASISU_STUB_TRAP(); }

bool nt_basisu_validate_header(const void *basis_data, uint32_t basis_size) {
    (void)basis_data;
    (void)basis_size;
    NT_BASISU_STUB_TRAP();
    return false;
}

uint32_t nt_basisu_get_level_count(const void *basis_data, uint32_t basis_size) {
    (void)basis_data;
    (void)basis_size;
    NT_BASISU_STUB_TRAP();
    return 0;
}

// NOLINTNEXTLINE(readability-non-const-parameter) — out param signature must match the real transcoder
bool nt_basisu_get_level_desc(const void *basis_data, uint32_t basis_size, uint32_t level_index, uint32_t *out_width, uint32_t *out_height, uint32_t *out_total_blocks) {
    (void)basis_data;
    (void)basis_size;
    (void)level_index;
    (void)out_width;
    (void)out_height;
    (void)out_total_blocks;
    NT_BASISU_STUB_TRAP();
    return false;
}

bool nt_basisu_start_transcoding(const void *basis_data, uint32_t basis_size) {
    (void)basis_data;
    (void)basis_size;
    NT_BASISU_STUB_TRAP();
    return false;
}

/* No-op, not a trap: cleanup must not crash. Unreachable anyway -- only called
   inside a successfully started transcode session. */
void nt_basisu_stop_transcoding(void) {}

bool nt_basisu_transcode_level(const void *basis_data, uint32_t basis_size, uint32_t level_index, void *output, uint32_t output_blocks, nt_basisu_format_t format) {
    (void)basis_data;
    (void)basis_size;
    (void)level_index;
    (void)output;
    (void)output_blocks;
    (void)format;
    NT_BASISU_STUB_TRAP();
    return false;
}

uint32_t nt_basisu_bytes_per_block(nt_basisu_format_t format) {
    (void)format;
    NT_BASISU_STUB_TRAP();
    return 0;
}

uint32_t nt_basisu_gl_internal_format(nt_basisu_format_t format) {
    (void)format;
    NT_BASISU_STUB_TRAP();
    return 0;
}
