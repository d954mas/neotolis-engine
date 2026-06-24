#include "nt_fpng.h"

#include <cstring>
#include <vector>

#include "fpng.h"

extern "C" void nt_fpng_init(void) { fpng::fpng_init(); }

extern "C" uint32_t nt_fpng_encode_rgb(const void *rgb, uint32_t w, uint32_t h, uint8_t *out, uint32_t out_cap) {
    // std::vector stays inside this TU — only the raw bytes cross the C boundary.
    std::vector<uint8_t> buf;
    if (!fpng::fpng_encode_image_to_memory(rgb, w, h, 3u, buf, 0u)) {
        return 0u;
    }
    if (buf.size() > out_cap) {
        return 0u;
    }
    std::memcpy(out, buf.data(), buf.size());
    return (uint32_t)buf.size();
}
