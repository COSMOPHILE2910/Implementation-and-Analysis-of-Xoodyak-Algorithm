#include "cyclist.h"
#include "aead.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/* --- Hex Utility Functions --- */
static int hex_char(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

static int hex_to_bytes(const char *hex, uint8_t *out, size_t out_size) {
    size_t hex_len = strlen(hex);
    if (hex_len % 2 != 0) return -1;
    size_t byte_len = hex_len / 2;
    if (byte_len > out_size) return -1;
    for (size_t i = 0; i < byte_len; i++) {
        int hi = hex_char(hex[2 * i]);
        int lo = hex_char(hex[2 * i + 1]);
        if (hi < 0 || lo < 0) return -1;
        out[i] = (uint8_t)((hi << 4) | lo);
    }
    return (int)byte_len;
}

static void bytes_to_hex(const uint8_t *buf, size_t len, char *out) {
    for (size_t i = 0; i < len; i++)
        sprintf(out + 2 * i, "%02X", buf[i]);
    out[2 * len] = '\0';
}

static void print_hex_or_empty(const char *label, const uint8_t *buf, size_t len) {
    if (len == 0) {
        printf("  %-14s  (empty)\n", label);
    } else {
        char hex[1024]; // Large enough for maximum KAT message sizes
        bytes_to_hex(buf, len, hex);
        printf("  %-14s  %s\n", label, hex);
    }
}

/* --- KAT Vector Structure and Parser --- */
typedef struct {
    int     count;
    uint8_t key[CRYPTO_KEYBYTES];
    uint8_t nonce[CRYPTO_NPUBBYTES];
    uint8_t pt[1024];  int ptlen;
    uint8_t ad[1024];  int adlen;
    uint8_t ct[1024 + CRYPTO_ABYTES]; int ctlen;
} kat_vector_t;

static int parse_next_kat(FILE *f, kat_vector_t *v) {
    char line[2048];
    int fields_read = 0;
    memset(v, 0, sizeof(*v));

    while (fgets(line, sizeof(line), f)) {
        size_t len = strlen(line);
        while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r'))
            line[--len] = '\0';

        if (len == 0) {
            if (fields_read == 6) return 1;
            continue;
        }

        char *sep = strstr(line, " = ");
        if (!sep) continue;
        *sep = '\0';
        char *field = line;
        char *value = sep + 3;

        if      (strcmp(field, "Count") == 0) { v->count = atoi(value); fields_read++; }
        else if (strcmp(field, "Key")   == 0) { hex_to_bytes(value, v->key, CRYPTO_KEYBYTES); fields_read++; }
        else if (strcmp(field, "Nonce") == 0) { hex_to_bytes(value, v->nonce, CRYPTO_NPUBBYTES); fields_read++; }
        else if (strcmp(field, "PT")    == 0) { v->ptlen = (strlen(value) > 0) ? hex_to_bytes(value, v->pt, 1024) : 0; fields_read++; }
        else if (strcmp(field, "AD")    == 0) { v->adlen = (strlen(value) > 0) ? hex_to_bytes(value, v->ad, 1024) : 0; fields_read++; }
        else if (strcmp(field, "CT")    == 0) { v->ctlen = hex_to_bytes(value, v->ct, 1024 + CRYPTO_ABYTES); fields_read++; }
    }
    return (fields_read == 6) ? 1 : 0;
}

/* --- Main Runner --- */
int main(int argc, char *argv[]) {
    const char *kat_file = (argc > 1) ? argv[1] : "/home/lenovo/xoodyak_project_1/LWC_AEAD_KAT_128_128.txt";
    FILE *f = fopen(kat_file, "r");
    if (!f) {
        fprintf(stderr, "[ERROR] Cannot open KAT file: %s\n", kat_file);
        return 1;
    }

    printf("=================================================================\n");
    printf(" Xoodyak — NIST KAT Test Runner (Verbose)\n");
    printf(" KAT file : %s\n", kat_file);
    printf("=================================================================\n\n");

    int total = 0, enc_pass = 0, dec_pass = 0, tamper_pass = 0;
    kat_vector_t v;

    while (parse_next_kat(f, &v) == 1) {
        total++;
        printf("-----------------------------------------------------------------\n");
        printf("  Count = %d  (PT=%d, AD=%d)\n", v.count, v.ptlen, v.adlen);
        printf("-----------------------------------------------------------------\n");

        print_hex_or_empty("Key:", v.key, CRYPTO_KEYBYTES);
        print_hex_or_empty("Nonce:", v.nonce, CRYPTO_NPUBBYTES);
        print_hex_or_empty("PT:", v.pt, v.ptlen);
        print_hex_or_empty("AD:", v.ad, v.adlen);

        /* TEST 1: Encryption */
        uint8_t ct_out[1024 + CRYPTO_ABYTES];
        unsigned long long ct_out_len = 0;
        crypto_aead_encrypt(ct_out, &ct_out_len, v.pt, v.ptlen, v.ad, v.adlen, NULL, v.nonce, v.key);

        char exp_ct[2100], got_ct[2100];
        bytes_to_hex(v.ct, v.ctlen, exp_ct);
        bytes_to_hex(ct_out, (size_t)ct_out_len, got_ct);
        printf("  Expected CT:   %s\n", exp_ct);
        printf("  Computed CT:   %s\n", got_ct);

        if (ct_out_len == (unsigned long long)v.ctlen && memcmp(ct_out, v.ct, v.ctlen) == 0) {
            enc_pass++; printf("  Encrypt:        PASS\n");
        } else {
            printf("  Encrypt:        *** FAIL ***\n");
        }

        /* TEST 2: Decryption */
        uint8_t pt_out[1024];
        unsigned long long pt_out_len = 0;
        int ret = crypto_aead_decrypt(pt_out, &pt_out_len, NULL, v.ct, v.ctlen, v.ad, v.adlen, v.nonce, v.key);

        if (ret == 0 && (int)pt_out_len == v.ptlen && memcmp(pt_out, v.pt, v.ptlen) == 0) {
            dec_pass++; printf("  Decrypt:        PASS\n");
        } else {
            printf("  Decrypt:        *** FAIL *** (ret=%d)\n", ret);
        }

        /* TEST 3: Tamper Rejection */
        uint8_t tampered[1024 + CRYPTO_ABYTES];
        memcpy(tampered, v.ct, v.ctlen);
        tampered[v.ctlen - 1] ^= 0x01; // Flip one bit in the tag
        int tamper_ret = crypto_aead_decrypt(pt_out, &pt_out_len, NULL, tampered, v.ctlen, v.ad, v.adlen, v.nonce, v.key);

        if (tamper_ret == -1) {
            tamper_pass++; printf("  Tamper reject:  PASS\n");
        } else {
            printf("  Tamper reject:  *** FAIL *** (ret=%d)\n", tamper_ret);
        }
    }

    fclose(f);
    printf("\n=================================================================\n");
    printf(" FINAL RESULTS\n");
    printf("=================================================================\n");
    printf(" Total vectors: %d\n", total);
    printf(" Encrypt (CT Match):    %d/%d\n", enc_pass, total);
    printf(" Decrypt (PT Recovery): %d/%d\n", dec_pass, total);
    printf(" Tamper Rejection:      %d/%d\n", tamper_pass, total);
    
    if (enc_pass == total && dec_pass == total && tamper_pass == total)
        printf("\n RESULT: ALL %d TESTS PASSED\n", total * 3);
    else
        printf("\n RESULT: SOME TESTS FAILED\n");
    printf("=================================================================\n");

    return (enc_pass == total && dec_pass == total && tamper_pass == total) ? 0 : 1;
}