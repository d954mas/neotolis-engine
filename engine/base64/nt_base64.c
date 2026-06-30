/* Pure base64 encoder (standard alphabet + '=' padding): no heap, caller-allocates, fail-early
 * cap-check. */

#include "nt_base64.h"

static const char B64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

uint32_t nt_base64_encode(const uint8_t *in, uint32_t in_len, char *out, uint32_t out_cap) {
    uint32_t need = ((in_len + 2U) / 3U) * 4U;
    if (need > out_cap) {
        return 0U; /* fail-early: never write past out_cap. */
    }
    uint32_t o = 0U;
    for (uint32_t i = 0U; i < in_len; i += 3U) {
        uint32_t b0 = in[i];
        uint32_t b1 = (i + 1U < in_len) ? in[i + 1U] : 0U;
        uint32_t b2 = (i + 2U < in_len) ? in[i + 2U] : 0U;
        uint32_t triple = (b0 << 16) | (b1 << 8) | b2;
        out[o++] = B64[(triple >> 18) & 0x3FU];
        out[o++] = B64[(triple >> 12) & 0x3FU];
        out[o++] = (char)((i + 1U < in_len) ? B64[(triple >> 6) & 0x3FU] : '=');
        out[o++] = (char)((i + 2U < in_len) ? B64[triple & 0x3FU] : '=');
    }
    return o;
}
