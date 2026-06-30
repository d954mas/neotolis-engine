#ifndef NT_DEVAPI_CAPTURE_INTERNAL_H
#define NT_DEVAPI_CAPTURE_INTERNAL_H

#include <stdint.h>

/* Capture-group internals (NOT a host contract). Kept out of the public nt_devapi_capture.h and the core
   nt_devapi_internal.h so the capture unit test reaches the value-checkable helper without either. */

/* Fused alpha-strip + integer box-average (rgba8 src -> RGB dst; dst = w/factor x h/factor; factor==1 is
   a plain strip). Non-static so the capture unit test can value-check the box mean. */
void nt_devapi_capture_strip_and_box(const uint8_t *src, uint32_t w, uint32_t h, uint32_t factor, uint8_t *dst);

#endif /* NT_DEVAPI_CAPTURE_INTERNAL_H */
