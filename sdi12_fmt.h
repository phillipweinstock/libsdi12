/**
 * @file sdi12_fmt.h
 * @brief Internal byte-builder the library uses instead of snprintf.
 *
 * Keeps printf out of firmware images and makes wire formatting
 * independent of libc and locale. Internal: included only by the
 * library's .c files, not installed.
 *
 * Like snprintf, b.len counts every byte appended, even past b.cap, and
 * the buffer stays NUL-terminated while b.cap > 0. So b.len >= b.cap
 * means the output was truncated.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef SDI12_FMT_H
#define SDI12_FMT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>

/** Incremental byte-builder over a caller-owned buffer. */
typedef struct {
    char  *buf;    /**< Destination buffer (NUL-terminated while cap > 0). */
    size_t cap;    /**< Total capacity of buf. */
    size_t len;    /**< Would-be length (snprintf-style), not capped by cap. */
} sdi12_sbuf_t;

/** Start building into buf (cap bytes). Writes buf[0] = '\0' when cap > 0. */
static inline void sdi12_sbuf_init(sdi12_sbuf_t *b, char *buf, size_t cap)
{
    b->buf = buf;
    b->cap = cap;
    b->len = 0;
    if (cap > 0) buf[0] = '\0';
}

/** Append one char. */
static inline void sdi12_sbuf_putc(sdi12_sbuf_t *b, char c)
{
    if (b->len + 1 < b->cap) {
        b->buf[b->len] = c;
        b->buf[b->len + 1] = '\0';
    }
    b->len++;
}

/** Append up to n chars of s, stopping early at a NUL. */
static inline void sdi12_sbuf_puts(sdi12_sbuf_t *b, const char *s, size_t n)
{
    for (size_t i = 0; i < n && s[i] != '\0'; i++) {
        sdi12_sbuf_putc(b, s[i]);
    }
}

/** Append v in decimal, left-padded with `pad` to at least `width` chars. */
static inline void sdi12_sbuf_u32(sdi12_sbuf_t *b, uint32_t v,
                                  unsigned width, char pad)
{
    char digits[10];
    unsigned nd = 0;
    do {
        digits[nd++] = (char)('0' + (v % 10));
        v /= 10;
    } while (v != 0);

    for (unsigned i = nd; i < width; i++) {
        sdi12_sbuf_putc(b, pad);
    }
    while (nd > 0) {
        sdi12_sbuf_putc(b, digits[--nd]);
    }
}

/** Append s cut or space-padded to exactly `width` chars. Reads at most
 *  `width` bytes of s, so an unterminated char array is safe. */
static inline void sdi12_sbuf_field(sdi12_sbuf_t *b, const char *s,
                                    size_t width)
{
    size_t i = 0;
    for (; i < width && s[i] != '\0'; i++) {
        sdi12_sbuf_putc(b, s[i]);
    }
    for (; i < width; i++) {
        sdi12_sbuf_putc(b, ' ');
    }
}

#ifdef __cplusplus
}
#endif

#endif /* SDI12_FMT_H */
