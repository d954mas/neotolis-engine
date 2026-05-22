#ifndef NT_ALIGN_H
#define NT_ALIGN_H

/* Round x up to the next multiple of N. N MUST be a power of two
 * (e.g. 8, 16, 64). Standard bit-trick: (x + N-1) masked to drop
 * the low log2(N) bits. */
#define NT_ALIGN_UP(x, N) (((x) + (N) - 1U) & ~((N) - 1U))

#endif /* NT_ALIGN_H */
