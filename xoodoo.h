/*
 * xoodoo.h
 * Xoodoo[12] permutation — header
 *
 * 384-bit (48-byte) permutation used as the core of Xoodyak.
 * State layout: 3 planes × 4 lanes × 32 bits = 12 uint32_t words.
 * Memory order: lane[y*4 + x], so y=0 is words 0-3, y=1 is 4-7, y=2 is 8-11.
 */

#ifndef XOODOO_H
#define XOODOO_H

#include <stdint.h>
#include <string.h>

/* ── State geometry ─────────────────────────────────────────────── */
#define XOODOO_ROWS     3       /* number of planes (y-index)        */
#define XOODOO_COLS     4       /* lanes per plane  (x-index)        */
#define XOODOO_LANES    12      /* total 32-bit words in state       */
#define XOODOO_BYTES    48      /* total bytes in state              */
#define XOODOO_ROUNDS   12      /* rounds used by Xoodyak            */

/*
 * Index macro: maps (x, y) coordinates into the flat 12-word array.
 * Both x and y are taken modulo their dimension so wrap-around in
 * the round function works transparently (e.g. x = -1 → x = 3).
 */
#define LANE(x, y) \
    ( (((unsigned int)(y) % XOODOO_ROWS) * XOODOO_COLS) \
    + (((unsigned int)(x) % XOODOO_COLS)) )

/* ── Portable 32-bit rotate left ────────────────────────────────── */
static inline uint32_t rotl32(uint32_t v, unsigned int n)
{
    n &= 31;
    return (n == 0) ? v : ((v << n) | (v >> (32 - n)));
}

/* ── Round constants (rc12 applied first, rc1 applied last) ──────── */
/*
 * Applied in order RC[0]=rc12 ... RC[11]=rc1 to A[0] (lane x=0,y=0).
 * Derived from two LFSRs; values taken directly from the spec.
 */
static const uint32_t XOODOO_RC[XOODOO_ROUNDS] = {
    0x00000058,  /* round 1  (index 0  → rc12) */
    0x00000038,  /* round 2  (index 1  → rc11) */
    0x000003C0,  /* round 3  (index 2  → rc10) */
    0x000000D0,  /* round 4  (index 3  → rc9 ) */
    0x00000120,  /* round 5  (index 4  → rc8 ) */
    0x00000014,  /* round 6  (index 5  → rc7 ) */
    0x00000060,  /* round 7  (index 6  → rc6 ) */
    0x0000002C,  /* round 8  (index 7  → rc5 ) */
    0x00000380,  /* round 9  (index 8  → rc4 ) */
    0x000000F0,  /* round 10 (index 9  → rc3 ) */
    0x000001A0,  /* round 11 (index 10 → rc2 ) */
    0x00000012,  /* round 12 (index 11 → rc1 ) */
};

/* ── State type ─────────────────────────────────────────────────── */
/*
 * The state is stored as a flat array of 12 uint32_t words.
 * When loading from/storing to bytes we use little-endian order,
 * matching the reference implementation.
 */
typedef uint32_t XoodooState[XOODOO_LANES];

/* ── Public API ─────────────────────────────────────────────────── */

/* Apply nr rounds of the Xoodoo permutation to state (nr ≤ 12). */
void xoodoo_permute(XoodooState state, unsigned int nr);

/* Convenience wrappers */
static inline void xoodoo_permute_12(XoodooState state)
{
    xoodoo_permute(state, 12);
}

/*
 * XOR 'length' bytes from 'data' into the byte-view of the state
 * starting at byte offset 'offset'.
 */
void xoodoo_add_bytes(XoodooState state,
                      const uint8_t *data,
                      unsigned int offset,
                      unsigned int length);

/* XOR a single byte into the state at byte offset 'offset'. */
void xoodoo_add_byte(XoodooState state,
                     uint8_t byte,
                     unsigned int offset);

/*
 * Extract 'length' bytes from the state starting at byte offset 'offset'
 * into 'out'.
 */
void xoodoo_extract_bytes(const XoodooState state,
                          uint8_t *out,
                          unsigned int offset,
                          unsigned int length);

/*
 * out[i] = in[i] XOR state_bytes[offset + i]  for i in [0, length).
 * Used during encryption/decryption to apply keystream.
 */
void xoodoo_extract_and_add_bytes(const XoodooState state,
                                  const uint8_t *in,
                                  uint8_t *out,
                                  unsigned int offset,
                                  unsigned int length);

/* Zero the entire state. */
static inline void xoodoo_init(XoodooState state)
{
    memset(state, 0, XOODOO_BYTES);
}

#endif /* XOODOO_H */
