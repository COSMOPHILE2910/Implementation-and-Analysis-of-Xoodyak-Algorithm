/*
 * xoodyak.c
 * Xoodyak — Cyclist mode implementation
 *
 * All functions implement exactly the algorithms in the spec (§2.2, §2.3).
 * Comments cite the spec page and algorithm number where relevant.
 */

#include "cyclist.h"
#include <assert.h>
#include <string.h>

/* ── Utility ──────────────────────────────────────────────────────── */
static inline unsigned int min_uint(unsigned int a, unsigned int b)
{
    return (a < b) ? a : b;
}

/* ── Down() : absorb one block into the state ─────────────────────── */
/*
 * Spec Algorithm 3, "Down(Xi, cD)":
 *
 *   state ^= Xi || 0x01 || 0x00* || (cD & 0x01 if hash else cD)
 *
 * Concretely:
 *   1. XOR Xi (xi_len bytes) into state starting at byte 0.
 *   2. XOR 0x01 into state[xi_len]  — the block delimiter / padding marker.
 *   3. XOR the domain color into state[47] (last byte).
 *      In hash mode the color is masked to 1 bit (cd & 0x01) so it cannot
 *      distinguish among keyed-mode domains even if the capacity is exposed.
 *   4. Set phase = down.
 */
void cyclist_down(XoodyakInstance *inst,
                  const uint8_t *xi, unsigned int xi_len,
                  uint8_t cd)
{
    /* Step 1: absorb the block */
    if (xi != NULL && xi_len > 0)
        xoodoo_add_bytes(inst->state, xi, 0, xi_len);

    /* Step 2: block delimiter */
    xoodoo_add_byte(inst->state, 0x01, xi_len);

    /* Step 3: domain color at last byte of state */
    if (inst->mode == CYCLIST_MODE_HASH)
        xoodoo_add_byte(inst->state, cd & 0x01, XOODYAK_b_PRIME - 1);
    else
        xoodoo_add_byte(inst->state, cd, XOODYAK_b_PRIME - 1);

    /* Step 4 */
    inst->phase = CYCLIST_PHASE_DOWN;
}

/* ── Up() : permute and extract output ────────────────────────────── */
/*
 * Spec Algorithm 3, "Up(|Yi|, cU)":
 *
 *   if mode = keyed: state ^= 0x00* || cU   (XOR color into last byte)
 *   state = f(state)                          (apply Xoodoo[12])
 *   Yi = state[0 .. |Yi|-1]                  (extract first |Yi| bytes)
 *   phase = up
 *
 * yi may be NULL (yi_len=0) when we only want to advance the state
 * without producing output (used in Crypt).
 */
void cyclist_up(XoodyakInstance *inst,
                uint8_t *yi, unsigned int yi_len,
                uint8_t cu)
{
    /* Only add color in keyed mode */
    if (inst->mode != CYCLIST_MODE_HASH)
        xoodoo_add_byte(inst->state, cu, XOODYAK_b_PRIME - 1);

    /* Apply the permutation */
    xoodoo_permute_12(inst->state);

    inst->phase = CYCLIST_PHASE_UP;

    /* Extract output bytes */
    if (yi != NULL && yi_len > 0)
        xoodoo_extract_bytes(inst->state, yi, 0, yi_len);
}

/* ── AbsorbAny() : absorb arbitrary-length input ─────────────────── */
/*
 * Spec Algorithm 3, "AbsorbAny(X, r, cD)":
 *
 *   Split X into blocks of size r (last block may be shorter).
 *   For each block Xi:
 *     if phase != up: Up(0, 0x00)   (ensure we are in "up" phase)
 *     Down(Xi, cD if first block else 0x00)
 *
 * The 'cD' color is only applied to the first block; subsequent blocks
 * use 0x00 (COLOR_ZERO) to avoid ambiguity.
 *
 * Note: even an empty X produces exactly one Down() call (with Xi = ε)
 * because the spec says Split(X, r) returns [ε] when X is empty.
 */
static void absorb_any(XoodyakInstance *inst,
                       const uint8_t *X, size_t X_len,
                       unsigned int r, uint8_t cd)
{
    int first = 1;

    do {
        /* Ensure up phase before each Down */
        if (inst->phase != CYCLIST_PHASE_UP)
            cyclist_up(inst, NULL, 0, COLOR_ZERO);

        /* Take one block of up to r bytes */
        unsigned int split = (X_len <= r) ? (unsigned int)X_len : r;
        cyclist_down(inst, (X_len > 0) ? X : NULL,
                     split,
                     first ? cd : COLOR_ZERO);
        first = 0;

        if (X_len > split) {
            X     += split;
            X_len -= split;
        } else {
            X_len  = 0;
        }

    } while (X_len > 0);
}

/* ── AbsorbKey() : load key + ID into state, switch to keyed mode ─── */
/*
 * Spec Algorithm 3, "AbsorbKey(K, id, counter)":
 *
 *   mode = keyed,  Rabsorb = Rkin,  Rsqueeze = Rkout
 *   AbsorbAny(K || id || enc8(|id|), Rkin, COLOR_ABSORB_KEY)
 *   if counter != ε: AbsorbAny(counter, 1, COLOR_ZERO)
 *
 * enc8(n) is a single byte holding the value n (little-endian, trivially).
 * The concatenation K || id || enc8(|id|) must fit in Rkin-1 = 43 bytes.
 */
static void absorb_key(XoodyakInstance *inst,
                       const uint8_t *K,   size_t K_len,
                       const uint8_t *ID,  size_t ID_len,
                       const uint8_t *ctr, size_t ctr_len)
{
    uint8_t kid[XOODYAK_RKIN];

    assert(inst->mode == CYCLIST_MODE_HASH);
    assert((K_len + ID_len) <= (XOODYAK_RKIN - 1));

    /* Switch to keyed mode */
    inst->mode     = CYCLIST_MODE_KEYED;
    inst->rabsorb  = XOODYAK_RKIN;
    inst->rsqueeze = XOODYAK_RKOUT;

    if (K_len > 0) {
        /* Build K || ID || enc8(|ID|) */
        memcpy(kid,           K,  K_len);
        memcpy(kid + K_len,   ID, ID_len);
        kid[K_len + ID_len] = (uint8_t)ID_len;

        absorb_any(inst, kid, K_len + ID_len + 1,
                   inst->rabsorb, COLOR_ABSORB_KEY);

        /* Absorb counter byte-by-byte (rate = 1) */
        if (ctr_len > 0)
            absorb_any(inst, ctr, ctr_len, 1, COLOR_ZERO);
    }
}

/* ── SqueezeAny() : produce arbitrary-length output ─────────────── */
/*
 * Spec Algorithm 3, "SqueezeAny(ℓ, cU)":
 *
 *   Y = Up(min(ℓ, Rsqueeze), cU)
 *   while |Y| < ℓ:
 *     Down(ε, 0x00)
 *     Y ||= Up(min(ℓ - |Y|, Rsqueeze), 0x00)
 */
static void squeeze_any(XoodyakInstance *inst,
                        uint8_t *Y, size_t len,
                        uint8_t cu)
{
    unsigned int take;

    /* First block */
    take = min_uint((unsigned int)len, inst->rsqueeze);
    cyclist_up(inst, Y, take, cu);
    Y   += take;
    len -= take;

    /* Subsequent blocks */
    while (len > 0) {
        cyclist_down(inst, NULL, 0, COLOR_ZERO);
        take = min_uint((unsigned int)len, inst->rsqueeze);
        cyclist_up(inst, Y, take, COLOR_ZERO);
        Y   += take;
        len -= take;
    }
}

/* ── Crypt() : encrypt or decrypt ────────────────────────────────── */
/*
 * Spec Algorithm 3, "Crypt(I, decrypt)":
 *
 * Process in Rkout=24-byte blocks:
 *   For each block Ii:
 *     Oi = Ii XOR Up(|Ii|, Cu)           (apply keystream)
 *     Pi = (decrypt) ? Oi : Ii           (plaintext is Ii if encrypting)
 *     Down(Pi, 0x00)                     (absorb plaintext)
 *     Cu = 0x00 for subsequent blocks
 *
 * IMPORTANT asymmetry:
 *   - During encryption:  plaintext  P is absorbed (not ciphertext C)
 *   - During decryption:  plaintext  P (= Oi) is absorbed
 * Both directions absorb the same data (plaintext), so the states stay
 * synchronized and the scheme is nonce-misuse resistant: if two messages
 * share the same nonce, leakage is limited to the first differing block.
 *
 * Implementation detail: Up() is called with yi=NULL, yi_len=0 first
 * to advance the state and apply the color, then ExtractAndAdd reads
 * the keystream directly from the state without a second call.
 */
static void crypt(XoodyakInstance *inst,
                  const uint8_t *I, uint8_t *O,
                  size_t len, int decrypt)
{
    uint8_t  P_block[XOODYAK_RKOUT]; /* plaintext block (for absorb) */
    uint8_t  cu = COLOR_CRYPT;       /* 0x80 for first block         */

    assert(inst->mode == CYCLIST_MODE_KEYED);

    /*
     * MUST be do-while, not while.
     *
     * When len=0 (empty plaintext), the loop body still executes once:
     *   Up(NULL, 0, 0x80)   — advances state with CRYPT color
     *   ExtractAndAdd(0 bytes) — no-op
     *   Down(NULL, 0, 0x00) — absorbs empty block, adds delimiter
     *
     * This single cycle changes the state before Squeeze(tag), which is
     * what the NIST KAT file expects. Skipping it produces the wrong tag.
     * Verified against the reference Cyclist.inc which uses do-while.
     */
    do {
        unsigned int split = min_uint((unsigned int)len, XOODYAK_RKOUT);

        /* Advance state with color but don't extract yet */
        cyclist_up(inst, NULL, 0, cu);

        if (decrypt) {
            /* O[i] = I[i] XOR state[i]  → O is plaintext */
            if (split > 0)
                xoodoo_extract_and_add_bytes(inst->state, I, O, 0, split);
            /* Absorb recovered plaintext */
            cyclist_down(inst, (split > 0) ? O : NULL, split, COLOR_ZERO);
        } else {
            /* Save plaintext before overwriting */
            if (split > 0) memcpy(P_block, I, split);
            /* O[i] = I[i] XOR state[i]  → O is ciphertext */
            if (split > 0)
                xoodoo_extract_and_add_bytes(inst->state, I, O, 0, split);
            /* Absorb original plaintext (not ciphertext!) */
            cyclist_down(inst, (split > 0) ? P_block : NULL, split, COLOR_ZERO);
        }

        cu    = COLOR_ZERO;
        if (len > 0) {
            I    += split;
            O    += split;
            len  -= split;
        } else {
            break; /* executed the mandatory single empty-PT iteration */
        }

    } while (len > 0);
}

/* ── Public API ─────────────────────────────────────────────────── */

void xoodyak_initialize(XoodyakInstance *inst,
                        const uint8_t *K,   size_t K_len,
                        const uint8_t *ID,  size_t ID_len,
                        const uint8_t *ctr, size_t ctr_len)
{
    /* Zero the state and start in hash mode / up phase */
    xoodoo_init(inst->state);
    inst->phase    = CYCLIST_PHASE_UP;
    inst->mode     = CYCLIST_MODE_HASH;
    inst->rabsorb  = XOODYAK_RHASH;
    inst->rsqueeze = XOODYAK_RHASH;

    /* If a key is provided, switch to keyed mode */
    if (K_len > 0)
        absorb_key(inst, K, K_len, ID, ID_len, ctr, ctr_len);
}

void xoodyak_absorb(XoodyakInstance *inst,
                    const uint8_t *X, size_t X_len)
{
    absorb_any(inst, X, X_len, inst->rabsorb, COLOR_ABSORB);
}

void xoodyak_encrypt(XoodyakInstance *inst,
                     const uint8_t *P, uint8_t *C, size_t len)
{
    crypt(inst, P, C, len, 0);
}

void xoodyak_decrypt(XoodyakInstance *inst,
                     const uint8_t *C, uint8_t *P, size_t len)
{
    crypt(inst, C, P, len, 1);
}

void xoodyak_squeeze(XoodyakInstance *inst, uint8_t *Y, size_t len)
{
    squeeze_any(inst, Y, len, COLOR_SQUEEZE);
}

void xoodyak_squeeze_key(XoodyakInstance *inst, uint8_t *K, size_t len)
{
    assert(inst->mode == CYCLIST_MODE_KEYED);
    squeeze_any(inst, K, len, COLOR_SQUEEZE_KEY);
}

void xoodyak_ratchet(XoodyakInstance *inst)
{
    uint8_t buf[XOODYAK_LRATCHET];

    assert(inst->mode == CYCLIST_MODE_KEYED);

    /*
     * Squeeze lRatchet bytes with color 0x10 → destroys old state info.
     * Then absorb those bytes back in → new state cannot be reversed
     * to reveal the pre-ratchet state.
     */
    squeeze_any(inst, buf, XOODYAK_LRATCHET, COLOR_RATCHET);
    absorb_any (inst, buf, XOODYAK_LRATCHET, inst->rabsorb, COLOR_ZERO);
}
