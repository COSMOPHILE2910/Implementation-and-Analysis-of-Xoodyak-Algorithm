

#include "aead.h"
#include "cyclist.h"
#include <string.h>

int crypto_aead_encrypt(
    unsigned char *c,       unsigned long long *clen,
    const unsigned char *m, unsigned long long  mlen,
    const unsigned char *ad,unsigned long long  adlen,
    const unsigned char *nsec,
    const unsigned char *npub,
    const unsigned char *k)
{
    XoodyakInstance inst;
    (void)nsec;  /* Xoodyak does not use a secret nonce */

    /*
     * Sequence matches encrypt.c from the NIST submission and the spec:
     *
     *   Cyclist(K=k, ID=npub, counter=NULL)
     *   Absorb(ad)
     *   C[0..mlen-1] = Encrypt(m)
     *   C[mlen..mlen+15] = Squeeze(16)   ← 16-byte tag at the end
     */
    xoodyak_initialize(&inst, k, CRYPTO_KEYBYTES,
                               npub, CRYPTO_NPUBBYTES,
                               NULL, 0);

    xoodyak_absorb(&inst, ad, (size_t)adlen);
    xoodyak_encrypt(&inst, m, c, (size_t)mlen);
    xoodyak_squeeze(&inst, c + mlen, CRYPTO_ABYTES);

    *clen = mlen + CRYPTO_ABYTES;
    return 0;
}

int crypto_aead_decrypt(
    unsigned char *m,       unsigned long long *mlen,
    unsigned char *nsec,
    const unsigned char *c, unsigned long long  clen,
    const unsigned char *ad,unsigned long long  adlen,
    const unsigned char *npub,
    const unsigned char *k)
{
    XoodyakInstance inst;
    uint8_t tag[CRYPTO_ABYTES];
    unsigned long long mlen_;
    (void)nsec;

    *mlen = 0;

    /* Ciphertext must be at least as long as the tag */
    if (clen < CRYPTO_ABYTES)
        return -1;

    mlen_ = clen - CRYPTO_ABYTES;

    xoodyak_initialize(&inst, k, CRYPTO_KEYBYTES,
                               npub, CRYPTO_NPUBBYTES,
                               NULL, 0);

    xoodyak_absorb (&inst, ad, (size_t)adlen);
    xoodyak_decrypt(&inst, c, m, (size_t)mlen_);
    xoodyak_squeeze(&inst, tag, CRYPTO_ABYTES);

    /*
     * Constant-time tag comparison.
     * Using a byte-by-byte XOR accumulator prevents the compiler from
     * short-circuiting and avoids timing side channels.
     */
    {
        unsigned int i;
        uint8_t diff = 0;
        for (i = 0; i < CRYPTO_ABYTES; i++)
            diff |= tag[i] ^ c[mlen_ + i];

        if (diff != 0) {
            /* Zero the plaintext buffer — never release unverified plaintext */
            memset(m, 0, (size_t)mlen_);
            return -1;
        }
    }

    *mlen = mlen_;
    return 0;
}
