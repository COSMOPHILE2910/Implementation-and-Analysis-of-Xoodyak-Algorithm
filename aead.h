

#ifndef AEAD_H
#define AEAD_H

#include <stdint.h>

#define CRYPTO_KEYBYTES     16
#define CRYPTO_NPUBBYTES    16
#define CRYPTO_NSECBYTES    0
#define CRYPTO_ABYTES       16


int crypto_aead_encrypt(
    unsigned char *c,       unsigned long long *clen,
    const unsigned char *m, unsigned long long  mlen,
    const unsigned char *ad,unsigned long long  adlen,
    const unsigned char *nsec,
    const unsigned char *npub,
    const unsigned char *k);


int crypto_aead_decrypt(
    unsigned char *m,       unsigned long long *mlen,
    unsigned char *nsec,
    const unsigned char *c, unsigned long long  clen,
    const unsigned char *ad,unsigned long long  adlen,
    const unsigned char *npub,
    const unsigned char *k);

#endif 
