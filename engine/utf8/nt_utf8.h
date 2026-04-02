#ifndef NT_UTF8_H
#define NT_UTF8_H

/*
 * Hoehrmann DFA UTF-8 decoder (MIT license: http://bjoern.hoehrmann.de/utf-8/decoder/dfa/)
 *
 * Used by nt_font.c and nt_text_renderer.c.
 */

#include <stdint.h>

#define NT_UTF8_ACCEPT 0
#define NT_UTF8_REJECT 12

extern const uint8_t nt_utf8d[364];

uint32_t nt_utf8_decode(uint32_t *state, uint32_t *codep, uint32_t byte);

#endif /* NT_UTF8_H */
