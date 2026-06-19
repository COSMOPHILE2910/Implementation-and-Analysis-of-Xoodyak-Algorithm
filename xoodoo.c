/*
 * xoodoo.c
 * Xoodoo[12] permutation — implementation
 *
 * Five-step round function applied 12 times:
 *   θ  (theta)   — column-parity mixer (linear diffusion)
 *   ρW (rho-west)— plane shifts before χ
 *   ι  (iota)    — round constant addition (breaks symmetry)
 *   χ  (chi)     — 3-bit S-box layer (the only nonlinear step)
 *   ρE (rho-east)— plane shifts after χ
 */

#include "xoodoo.h"
#include <assert.h>

/* ------------------------------------------------------------------ */
/* Byte ↔ Word conversion (little-endian)                             */
/* ------------------------------------------------------------------ */

/*
 * Load 48 bytes (little-endian) into 12 uint32_t words.
 * The reference implementation stores the state as raw bytes and
 * converts on every permutation call — we do the same so our byte-
 * level helper functions (add_byte, extract_bytes, etc.) remain simple.
 */
static void bytes_to_words(uint32_t words[12], const uint8_t bytes[48])
{
    unsigned int i;
    for (i = 0; i < 12; i++) {
        words[i] = (uint32_t)bytes[4*i + 0]
                 | (uint32_t)bytes[4*i + 1] <<  8
                 | (uint32_t)bytes[4*i + 2] << 16
                 | (uint32_t)bytes[4*i + 3] << 24;
    }
}

static void words_to_bytes(uint8_t bytes[48], const uint32_t words[12])
{
    unsigned int i;
    for (i = 0; i < 12; i++) {
        bytes[4*i + 0] = (uint8_t)(words[i]        & 0xFF);
        bytes[4*i + 1] = (uint8_t)((words[i] >>  8) & 0xFF);
        bytes[4*i + 2] = (uint8_t)((words[i] >> 16) & 0xFF);
        bytes[4*i + 3] = (uint8_t)((words[i] >> 24) & 0xFF);
    }
}

/* ------------------------------------------------------------------ */
/* One round of Xoodoo                                                 */
/* ------------------------------------------------------------------ */

/*
 * The state 'a[12]' is a flat array indexed by LANE(x, y).
 *   y=0: a[0..3]   (bottom plane)
 *   y=1: a[4..7]   (middle plane)
 *   y=2: a[8..11]  (top plane)
 *
 * 'rc' is the round constant for this round, added to lane (x=0, y=0).
 */
static void xoodoo_round(uint32_t a[12], uint32_t rc)
{
    uint32_t p[4];   /* column parities */
    uint32_t e[4];   /* theta effect    */
    uint32_t b[12];  /* temporary copy  */
    unsigned int x, y;

    /* ── θ (Theta): column-parity mixer ──────────────────────────
     *
     * For each column x (0..3):
     *   P[x] = A[x,0] XOR A[x,1] XOR A[x,2]          (column parity)
     *   E[x] = ROTL(P[x-1], 5) XOR ROTL(P[x-1], 14)  (effect)
     *
     * Then for every lane:
     *   A[x,y] ^= E[x]
     *
     * Note: E[x] uses column (x-1 mod 4), the "offset by 1" prevents
     * theta from being trivially invertible.
     */
    for (x = 0; x < 4; x++)
        p[x] = a[LANE(x,0)] ^ a[LANE(x,1)] ^ a[LANE(x,2)];

    for (x = 0; x < 4; x++) {
        /* (x-1) mod 4: in C, use (x+3)%4 to avoid negative modulo */
        uint32_t px1 = p[(x + 3) % 4];
        e[x] = rotl32(px1, 5) ^ rotl32(px1, 14);
    }

    for (x = 0; x < 4; x++)
        for (y = 0; y < 3; y++)
            a[LANE(x,y)] ^= e[x];

    /* ── ρW (Rho-west): plane shifts before chi ──────────────────
     *
     * Plane y=0: no change
     * Plane y=1: word-wise cyclic shift by 1 position (shift x-index by 1)
     *            i.e. b[x,1] = a[x-1, 1]
     * Plane y=2: bitwise rotation by 11 bits within each lane
     *            i.e. b[x,2] = ROTL(a[x,2], 11)
     */
    for (x = 0; x < 4; x++) {
        b[LANE(x, 0)] = a[LANE(x,     0)];           /* y=0: unchanged     */
        b[LANE(x, 1)] = a[LANE((x+3)%4, 1)];         /* y=1: word shift    */
        b[LANE(x, 2)] = rotl32(a[LANE(x, 2)], 11);   /* y=2: bit rotate 11 */
    }
    memcpy(a, b, 12 * sizeof(uint32_t));

    /* ── ι (Iota): round constant ────────────────────────────────
     *
     * XOR the round constant into lane (x=0, y=0) only.
     * Breaks the permutation's symmetry and prevents fixed-point attacks.
     */
    a[LANE(0, 0)] ^= rc;

    /* ── χ (Chi): 3-bit column-wise S-box — THE nonlinear step ──
     *
     * For each column (x, working in the y-direction):
     *   b[x,y] = a[x,y] XOR (NOT a[x, y+1] AND a[x, y+2])
     *
     * Indices y+1 and y+2 wrap modulo 3 via the LANE macro.
     *
     * This is the same structure as Keccak's chi and AES's MixColumns
     * in that it mixes three bits per column. It is the only place
     * where algebraic degree increases — making the cipher nonlinear.
     *
     * Degree of the full permutation grows as: 1 → 2 → 4 → ... → 2^r
     * after r rounds, bounded by 2^12 = 4096 (well above brute force).
     */
    for (x = 0; x < 4; x++)
        for (y = 0; y < 3; y++)
            b[LANE(x, y)] = a[LANE(x, y)]
                           ^ (~a[LANE(x, (y+1)%3)] & a[LANE(x, (y+2)%3)]);
    memcpy(a, b, 12 * sizeof(uint32_t));

    /* ── ρE (Rho-east): plane shifts after chi ───────────────────
     *
     * Plane y=0: no change
     * Plane y=1: bitwise rotation by 1 bit
     *            b[x,1] = ROTL(a[x,1], 1)
     * Plane y=2: word-wise shift by 2 + bitwise rotation by 8
     *            b[x,2] = ROTL(a[x+2, 2], 8)   [note: x+2, not x-2]
     *
     * Together with rho-west, rho-east creates asymmetric offsets that
     * destroy any structural symmetry that survives theta and chi.
     */
    for (x = 0; x < 4; x++) {
        b[LANE(x, 0)] = a[LANE(x,       0)];             /* y=0: unchanged       */
        b[LANE(x, 1)] = rotl32(a[LANE(x,     1)], 1);    /* y=1: bit rotate 1    */
        b[LANE(x, 2)] = rotl32(a[LANE((x+2)%4, 2)], 8); /* y=2: word+2, rot 8   */
    }
    memcpy(a, b, 12 * sizeof(uint32_t));
}

/* ------------------------------------------------------------------ */
/* Public: permute nr rounds                                           */
/* ------------------------------------------------------------------ */

void xoodoo_permute(XoodooState state, unsigned int nr)
{
    uint32_t a[12];
    unsigned int i;

    assert(nr <= XOODOO_ROUNDS);

    /*
     * The state is stored as a byte array (XoodooState = uint32_t[12]).
     * We reinterpret it as words for the round function.
     * The cast is safe because XoodooState is already uint32_t[12].
     */
    bytes_to_words(a, (const uint8_t *)state);

    /*
     * Apply rounds starting from index (MAXROUNDS - nr) so that the
     * first round always uses RC[0] = rc12 regardless of nr.
     * For nr=12: i runs 0..11, using RC[0..11].
     * For nr=6:  i runs 6..11, using RC[6..11].
     */
    for (i = XOODOO_ROUNDS - nr; i < XOODOO_ROUNDS; i++)
        xoodoo_round(a, XOODOO_RC[i]);

    words_to_bytes((uint8_t *)state, a);
}

/* ------------------------------------------------------------------ */
/* Byte-level state manipulation helpers                               */
/* ------------------------------------------------------------------ */

void xoodoo_add_bytes(XoodooState state,
                      const uint8_t *data,
                      unsigned int offset,
                      unsigned int length)
{
    unsigned int i;
    uint8_t *s = (uint8_t *)state;
    assert(offset + length <= XOODOO_BYTES);
    for (i = 0; i < length; i++)
        s[offset + i] ^= data[i];
}

void xoodoo_add_byte(XoodooState state, uint8_t byte, unsigned int offset)
{
    uint8_t *s = (uint8_t *)state;
    assert(offset < XOODOO_BYTES);
    s[offset] ^= byte;
}

void xoodoo_extract_bytes(const XoodooState state,
                          uint8_t *out,
                          unsigned int offset,
                          unsigned int length)
{
    const uint8_t *s = (const uint8_t *)state;
    assert(offset + length <= XOODOO_BYTES);
    memcpy(out, s + offset, length);
}

void xoodoo_extract_and_add_bytes(const XoodooState state,
                                  const uint8_t *in,
                                  uint8_t *out,
                                  unsigned int offset,
                                  unsigned int length)
{
    const uint8_t *s = (const uint8_t *)state;
    unsigned int i;
    assert(offset + length <= XOODOO_BYTES);
    for (i = 0; i < length; i++)
        out[i] = in[i] ^ s[offset + i];
}
