#ifndef NT_BASE64_H
#define NT_BASE64_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Caller-allocates, cap-checked: returns 0 (fail-early, no write past out_cap) when
 * out_cap < ((in_len+2)/3)*4; otherwise returns the byte count written (the encoded length,
 * not NUL-terminated). Standard alphabet + '=' padding. */
uint32_t nt_base64_encode(const uint8_t *in, uint32_t in_len, char *out, uint32_t out_cap);

#ifdef __cplusplus
}
#endif

#endif /* NT_BASE64_H */
