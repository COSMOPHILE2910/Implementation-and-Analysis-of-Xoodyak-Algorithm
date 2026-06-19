/*
 * xoodyak.h
 * Xoodyak — Cyclist mode on top of Xoodoo[12]
 *
 * Xoodyak = Cyclist[Xoodoo[12], Rhash=16, Rkin=44, Rkout=24, lRatchet=16]
 *
 * Provides:
 *   • Hash mode  : Absorb / Squeeze
 *   • Keyed mode : Absorb / Encrypt / Decrypt / Squeeze / SqueezeKey / Ratchet
 *
 * NIST AEAD interface (crypto_aead_encrypt / crypto_aead_decrypt) is in
 * xoodyak_aead.h / xoodyak_aead.c.
 */

#ifndef XOODYAK_H
#define XOODYAK_H

#include <stdint.h>
#include <stddef.h>
#include "xoodoo.h"

/* ── Xoodyak parameters (from spec §2.3) ───────────────────────── */

/* Permutation width in bytes */
#define XOODYAK_b_PRIME     48

/* Rate in hash mode: 16 bytes outer = 128-bit rate, 32 bytes capacity */
#define XOODYAK_RHASH       16

/* Rate for key absorption in keyed mode (larger because key is secret) */
#define XOODYAK_RKIN        44

/* Rate for output (squeeze / encryption) in keyed mode */
#define XOODYAK_RKOUT       24

/* Ratchet output size (for forward secrecy) */
#define XOODYAK_LRATCHET    16

/* NIST submission fixed sizes */
#define XOODYAK_KEY_BYTES   16
#define XOODYAK_NONCE_BYTES 16
#define XOODYAK_TAG_BYTES   16
#define XOODYAK_HASH_BYTES  32

/* ── Domain-separation color bytes ──────────────────────────────── */
/*
 * These constants are XOR-ed into the last byte of the state (byte 47)
 * by Cyclist_Up() in keyed mode to separate output domains.
 * In hash mode the constant is XOR-ed into the first byte after the
 * absorbed data (the "frame bit" / padding marker 0x01 position).
 *
 * The value 0x01 is always added at position XiLen (end of absorbed
 * block) by Down() — that is the block delimiter, not one of these.
 */
#define COLOR_ZERO          0x00  /* continuation blocks           */
#define COLOR_ABSORB_KEY    0x02  /* first block of AbsorbKey      */
#define COLOR_ABSORB        0x03  /* first block of Absorb (keyed) */
#define COLOR_RATCHET       0x10  /* Ratchet squeeze               */
#define COLOR_SQUEEZE_KEY   0x20  /* SqueezeKey                    */
#define COLOR_SQUEEZE       0x40  /* Squeeze                       */
#define COLOR_CRYPT         0x80  /* first block of Encrypt/Decrypt*/

/* ── Phase and mode flags ────────────────────────────────────────── */
#define CYCLIST_PHASE_UP    1
#define CYCLIST_PHASE_DOWN  2
#define CYCLIST_MODE_HASH   1
#define CYCLIST_MODE_KEYED  2

/* ── Instance structure ─────────────────────────────────────────── */
/*
 * The complete state of one Xoodyak object.
 * Only 48 bytes of actual cryptographic state plus 4 small control fields.
 * This is the entire context — no dynamic allocation required.
 */
typedef struct {
    XoodooState state;      /* 48-byte Xoodoo state (uint32_t[12])   */
    unsigned int phase;     /* CYCLIST_PHASE_UP or CYCLIST_PHASE_DOWN */
    unsigned int mode;      /* CYCLIST_MODE_HASH or CYCLIST_MODE_KEYED*/
    unsigned int rabsorb;   /* current absorb rate (16 or 44 bytes)  */
    unsigned int rsqueeze;  /* current squeeze rate (16 or 24 bytes) */
} XoodyakInstance;

/* ── Cyclist primitive operations ───────────────────────────────── */
/*
 * These are the two lowest-level operations in the Cyclist construction.
 * Everything else is built on top of them.
 *
 * Down(Xi, Cd):
 *   XOR Xi into state[0..XiLen-1]
 *   XOR 0x01 into state[XiLen]           (block delimiter / padding)
 *   XOR Cd  into state[47]               (domain color, masked to 1 bit in hash mode)
 *   phase → down
 *
 * Up(Yi, Cu):
 *   if keyed mode: XOR Cu into state[47] (domain color before permutation)
 *   apply Xoodoo[12]
 *   read first YiLen bytes → Yi
 *   phase → up
 */
void cyclist_down(XoodyakInstance *inst,
                  const uint8_t *xi, unsigned int xi_len,
                  uint8_t cd);

void cyclist_up(XoodyakInstance *inst,
                uint8_t *yi, unsigned int yi_len,
                uint8_t cu);

/* ── Public Xoodyak API ─────────────────────────────────────────── */

/*
 * Initialize Xoodyak.
 *   K=NULL / KLen=0  → hash mode
 *   K!=NULL          → keyed mode; absorbs K || id || enc8(|id|) then counter
 *
 * For the NIST AEAD submission: K=key(16B), ID=nonce(16B), counter=NULL.
 */
void xoodyak_initialize(XoodyakInstance *inst,
                        const uint8_t *K,   size_t K_len,
                        const uint8_t *ID,  size_t ID_len,
                        const uint8_t *ctr, size_t ctr_len);

/*
 * Absorb an arbitrary-length byte string X.
 * In keyed mode:  splits into Rkin=44-byte blocks, color=0x03 first block.
 * In hash mode:   splits into Rhash=16-byte blocks, color=0x01 first block
 *                 (but the color is masked: only bit 0 is kept → effectively 0x01).
 *
 * Note: COLOR_ABSORB (0x03) has bit0=1, so in hash mode it collapses to 0x01.
 * That is intentional — hash mode uses a restricted color to hide mode info.
 */
void xoodyak_absorb(XoodyakInstance *inst,
                    const uint8_t *X, size_t X_len);

/*
 * Encrypt plaintext P → ciphertext C (same length).
 * Also absorbs the plaintext into the state (providing nonce-misuse resistance).
 * Must only be called in keyed mode.
 */
void xoodyak_encrypt(XoodyakInstance *inst,
                     const uint8_t *P, uint8_t *C, size_t len);

/*
 * Decrypt ciphertext C → plaintext P (same length).
 * Absorbs the recovered plaintext (not the ciphertext) into the state —
 * this keeps decrypt synchronized with encrypt.
 * Must only be called in keyed mode.
 */
void xoodyak_decrypt(XoodyakInstance *inst,
                     const uint8_t *C, uint8_t *P, size_t len);

/*
 * Squeeze 'len' bytes of output (hash digest or authentication tag).
 * First Rsqueeze bytes come from one Up() call; subsequent blocks each
 * require an additional Down(ε)+Up() pair.
 */
void xoodyak_squeeze(XoodyakInstance *inst, uint8_t *Y, size_t len);

/*
 * SqueezeKey: like Squeeze but uses a different color (0x20 vs 0x40).
 * Used to derive a sub-key without confusion with normal squeeze output.
 * Must only be called in keyed mode.
 */
void xoodyak_squeeze_key(XoodyakInstance *inst, uint8_t *K, size_t len);

/*
 * Ratchet: provides forward secrecy.
 * Squeezes 16 bytes with color 0x10, then re-absorbs them.
 * The old state becomes unrecoverable even if the new state is leaked.
 * Must only be called in keyed mode.
 */
void xoodyak_ratchet(XoodyakInstance *inst);

#endif /* XOODYAK_H */
