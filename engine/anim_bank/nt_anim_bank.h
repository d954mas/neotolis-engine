#ifndef NT_ANIM_BANK_H
#define NT_ANIM_BANK_H

#include <stdint.h>

/*
 * nt_anim_bank — baked playback banks (spec 10). Init, bake and lookup land
 * with #478; this header pins the lookup result type.
 */

/* Result of nt_anim_bank_lookup: the texel origins of the two frames of an
 * interpolated pair and the blend factor between them. The two frames may sit
 * in different texture rows, hence two independent origins. A pure value that
 * owns nothing and refers to no storage. */
typedef struct {
    uint16_t x0, y0, x1, y1;
    float alpha;
} nt_anim_bank_lookup_t;

_Static_assert(sizeof(nt_anim_bank_lookup_t) == 12, "bank lookup ABI: 12 bytes");

#endif /* NT_ANIM_BANK_H */
