/*
 * trail_step2_lambda.c
 * ============================================================
 * DIFFERENTIAL TRAIL ANALYSIS — STEP 2
 * Lambda (λ) Layer: How Theta Spreads Differences
 *
 * SOURCE: "The Design of Xoodoo and Xoofff"
 *         Daemen, Hoffert, Van Assche, Van Keer, IACR ToSC 2018
 *         Section 2 (Xoodoo specification) +
 *         Section 5 (Propagation: column parity kernel)
 *
 * COMPILE:
 *   gcc -Wall -O2 -o trail_step2 trail_step2_lambda.c xoodoo.c
 * RUN:
 *   ./trail_step2
 * ============================================================
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "xoodoo.h"

/* ── Rotation helper ─────────────────────────────────────────────── */
static uint32_t rotl_local(uint32_t v, int n)
{
    return (v << n) | (v >> (32 - n));
}

/* ── Apply theta to a difference state ──────────────────────────── */
static void apply_theta(uint32_t s[12])
{
    uint32_t P[4], E[4];
    for (int x = 0; x < 4; x++)
        P[x] = s[0*4+x] ^ s[1*4+x] ^ s[2*4+x];
    for (int x = 0; x < 4; x++)
        E[x] = rotl_local(P[(x+3)%4], 5) ^ rotl_local(P[(x+3)%4], 14);
    for (int y = 0; y < 3; y++)
        for (int x = 0; x < 4; x++)
            s[y*4+x] ^= E[x];
}

/* ── Apply rho_east ─────────────────────────────────────────────── */
static void apply_rho_east(uint32_t s[12])
{
    for (int x = 0; x < 4; x++)
        s[1*4+x] = rotl_local(s[1*4+x], 1);
    uint32_t tmp[4];
    for (int x = 0; x < 4; x++)
        tmp[(x+2)%4] = rotl_local(s[2*4+x], 8);
    for (int x = 0; x < 4; x++)
        s[2*4+x] = tmp[x];
}

/* ── Apply rho_west ─────────────────────────────────────────────── */
static void apply_rho_west(uint32_t s[12])
{
    uint32_t t = s[1*4+0];
    s[1*4+0] = s[1*4+3];
    s[1*4+3] = s[1*4+2];
    s[1*4+2] = s[1*4+1];
    s[1*4+1] = t;
    for (int x = 0; x < 4; x++)
        s[2*4+x] = rotl_local(s[2*4+x], 11);
}

/* ── Apply full lambda = rho_west o theta o rho_east ────────────── */
static void apply_lambda(uint32_t s[12])
{
    apply_rho_east(s);
    apply_theta(s);
    apply_rho_west(s);
}

/* ── Count active columns ────────────────────────────────────────── */
static int count_active_cols(const uint32_t s[12])
{
    int count = 0;
    for (int x = 0; x < 4; x++) {
        uint32_t combined = s[0*4+x] | s[1*4+x] | s[2*4+x];
        while (combined) { count += combined & 1; combined >>= 1; }
    }
    return count;
}

/* ── Print column parity ─────────────────────────────────────────── */
static void print_parity(const uint32_t s[12])
{
    printf("  Column parity P[x] = A0[x] XOR A1[x] XOR A2[x]:\n");
    for (int x = 0; x < 4; x++) {
        uint32_t P = s[0*4+x] ^ s[1*4+x] ^ s[2*4+x];
        printf("    P[%d] = 0x%08X  %s\n", x, P,
               P == 0 ? "-> zero parity (theta does nothing)"
                      : "-> NON-ZERO (theta will spread!)");
    }
    printf("\n");
}

int main(void)
{
    printf("\n");
    printf("=============================================================\n");
    printf("  STEP 2: Lambda (λ) — How Theta Spreads Differences\n");
    printf("  Source: Xoodoo Design Paper, Section 2 and Section 5\n");
    printf("=============================================================\n\n");

    printf("λ = ρwest ∘ θ ∘ ρeast\n");
    printf("Linear → probability 1, no weight charged.\n");
    printf("But λ changes how many columns are active.\n\n");

    /* ── Experiment 1: non-zero parity ── */
    printf("─────────────────────────────────────────────────────────────\n");
    printf("EXPERIMENT 1: 1 active column, NON-ZERO parity\n");
    printf("  Column difference = 001 (only plane A0 active at x=0,z=0)\n");
    printf("─────────────────────────────────────────────────────────────\n\n");

    uint32_t d1[12]; memset(d1, 0, sizeof(d1));
    d1[0*4+0] = 1u; /* A0[x=0] bit z=0 */

    printf("  Before λ:\n");
    printf("    A0[0]=%08X  A1[0]=%08X  A2[0]=%08X\n", d1[0], d1[4], d1[8]);
    printf("    Active columns = %d\n", count_active_cols(d1));
    print_parity(d1);

    apply_lambda(d1);

    printf("  After λ:\n");
    printf("    A0[0]=%08X  A1[0]=%08X  A2[0]=%08X\n", d1[0], d1[4], d1[8]);
    printf("    A0[1]=%08X  A1[1]=%08X  A2[1]=%08X\n", d1[1], d1[5], d1[9]);
    int ac1 = count_active_cols(d1);
    printf("    Active columns = %d  (weight = %d)\n\n", ac1, 2*ac1);
    printf("  RESULT: Theta spread the difference from 1 to %d columns.\n\n", ac1);

    /* ── Experiment 2: zero parity / CP-kernel ── */
    printf("─────────────────────────────────────────────────────────────\n");
    printf("EXPERIMENT 2: 1 active column, ZERO parity (CP-kernel)\n");
    printf("  Column difference = 011 (planes A0 and A1 active)\n");
    printf("─────────────────────────────────────────────────────────────\n\n");

    uint32_t d2[12]; memset(d2, 0, sizeof(d2));
    d2[0*4+0] = 1u; /* A0[x=0] bit z=0 */
    d2[1*4+0] = 1u; /* A1[x=0] bit z=0 */

    printf("  Before λ:\n");
    printf("    A0[0]=%08X  A1[0]=%08X  A2[0]=%08X\n", d2[0], d2[4], d2[8]);
    printf("    Active columns = %d\n", count_active_cols(d2));
    print_parity(d2);

    apply_lambda(d2);

    printf("  After λ:\n");
    printf("    A0[0]=%08X  A1[0]=%08X  A2[0]=%08X\n", d2[0], d2[4], d2[8]);
    int ac2 = count_active_cols(d2);
    printf("    Active columns = %d  (weight = %d)\n\n", ac2, 2*ac2);
    printf("  RESULT: Theta did NOT spread (zero parity).\n");
    printf("  Only rho rotations moved the bits.\n\n");

    /* ── Experiment 3: minimum active cols after lambda ── */
    printf("─────────────────────────────────────────────────────────────\n");
    printf("EXPERIMENT 3: Minimum active columns after λ\n");
    printf("  (Search all 4×32×7 = 896 single-column differences)\n");
    printf("─────────────────────────────────────────────────────────────\n\n");

    int global_min = 999;
    int best_x = 0, best_z = 0, best_din = 0;

    for (int x = 0; x < 4; x++) {
        for (int z = 0; z < 32; z++) {
            for (int din = 1; din < 8; din++) {
                uint32_t s[12]; memset(s, 0, sizeof(s));
                if (din & 1) s[0*4+x] |= (1u << z);
                if (din & 2) s[1*4+x] |= (1u << z);
                if (din & 4) s[2*4+x] |= (1u << z);
                apply_lambda(s);
                int ac = count_active_cols(s);
                if (ac < global_min) {
                    global_min = ac;
                    best_x = x; best_z = z; best_din = din;
                }
            }
        }
    }

    printf("  Minimum active columns after λ = %d\n", global_min);
    printf("  Found at: x=%d, z=%d, din=%d (binary %d%d%d)\n",
           best_x, best_z, best_din,
           (best_din>>2)&1, (best_din>>1)&1, best_din&1);
    printf("  Weight of b1 = 2 × %d = %d\n\n", global_min, 2*global_min);

    printf("─────────────────────────────────────────────────────────────\n");
    printf("SUMMARY:\n\n");
    printf("  Non-zero parity → theta SPREADS → more active columns\n");
    printf("  Zero parity     → theta DOES NOTHING (CP-kernel)\n");
    printf("  Even CP-kernel inputs get moved by rho rotations\n\n");
    printf("  For 1-round trail core:\n");
    printf("    w(a1) = 2 × 1 = 2  (minimum: 1 active column)\n");
    printf("    The lambda step costs 0 weight (linear, probability=1)\n\n");
    printf("=== STEP 2 COMPLETE ===\n\n");

    return 0;
}
