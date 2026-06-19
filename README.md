# Implementation-and-Analysis-of-Xoodyak-Algorithm
# Xoodyak Lightweight Cryptographic Algorithm

> Implementation and Analysis — Internship Report  
> **Anandita Mukherjee** | Indian Institute of Technology Bhilai

---

## Table of Contents

- [Introduction](#introduction)
- [System Architecture](#system-architecture)
- [State Structure of Xoodoo](#state-structure-of-xoodoo)
- [Basic Operations in Xoodoo Permutation](#basic-operations-in-xoodoo-permutation)
- [Cyclist Algorithm](#cyclist-algorithm)
  - [State in Cyclist Mode](#state-in-cyclist-mode)
  - [DOWN and UP Operations](#down-and-up-operations)
  - [Color Bytes](#color-bytes)
  - [Cyclist Functions](#cyclist-functions)
  - [Encryption Workflow](#encryption-workflow)
  - [Ratchet](#ratchet)
- [Trail Analysis](#trail-analysis)
  - [Differential Trail](#differential-trail)
  - [S-Box and DDT Table](#s-box-and-ddt-table)
  - [One-Round Trail Analysis](#one-round-trail-analysis)
- [References](#references)

---

## Introduction

The **Internet of Things (IoT)** ecosystem comprises millions of resource-constrained devices — smart sensors, medical implants, digital cards — with very limited power, memory, and processing capability.

Traditional cryptographic standards like AES and SHA-3 are too resource-heavy for such devices. This motivated the field of **Lightweight Cryptography**, which delivers strong security with minimal resource usage.

**Xoodyak** is a lightweight cryptographic algorithm designed by the Keccak team and selected as a **NIST Lightweight Cryptography finalist**. It provides:

- Hashing
- Encryption
- Authentication
- Secure stream generation

...all within a single, efficient framework.

---

## System Architecture

The implementation is written in **C** and organized into four modular components:

| Module | Description |
|--------|-------------|
| `xoodoo.c / xoodoo.h` | 384-bit Xoodoo permutation with five round operations: Theta (θ), Rho-west (ρ_west), Iota (ι), Chi (χ), Rho-east (ρ_east) |
| `xoodyak.c / xoodyak.h` | Cyclist mode — manages absorb, encrypt, decrypt, and squeeze |
| `aead.c / aead.h` | AEAD interface via `crypto_aead_encrypt()` and `crypto_aead_decrypt()` |
| `trail.c` | Differential cryptanalysis — S-Box and DDT computation |

### Initialization

1. All 48 bytes of internal state set to zero
2. Cyclist configured in **keyed mode**: absorption rate R_kin = 44 bytes, squeeze rate R_kout = 24 bytes
3. 128-bit key + 128-bit nonce packed and absorbed via the DOWN operation
4. State transitions to UP phase before further data is absorbed

### Correctness Verification

Verified against **1089 official NIST Known Answer Tests (KAT)**, confirming:
- Correct ciphertext generation
- Correct authentication tag generation
- Successful plaintext recovery during decryption
- Proper handling of empty plaintext and associated data

✅ All 1089 test vectors pass.

---

## State Structure of Xoodoo

Xoodoo operates on a **384-bit internal state** organized as:

```
3 planes × 4 lanes × 32 bits = 384 bits
```

| Unit | Size |
|------|------|
| Lane | 32 bits |
| Plane | 4 lanes = 128 bits |
| State | 3 planes = 384 bits |

The state can also be viewed in terms of **sheets** and **columns** depending on the operation being applied.

---

## Basic Operations in Xoodoo Permutation

The Xoodoo permutation applies **5 operations across 12 rounds** to transform the 384-bit state:

### 1. Theta (θ) — Column Parity Mixing
```
P  ← A0 ⊕ A1 ⊕ A2
E  ← P <<< (1,5) ⊕ P <<< (1,14)
Ay ← Ay ⊕ E,  ∀y ∈ {0, 1, 2}
```

### 2. Rho-west (ρ_west) — Plane Shift
```
A1 ← A1 <<< (1, 0)
A2 ← A2 <<< (0, 11)
```

### 3. Iota (ι) — Round Constant Injection
```
A0 ← A0 ⊕ Ci
```

Where `Ci` is the round constant for round `i`.

**Round Constants** (`-11 ≤ i ≤ 0`):

| i | c_i | i | c_i |
|---|-----|---|-----|
| -11 | `0x00000058` | -5 | `0x00000060` |
| -10 | `0x00000038` | -4 | `0x0000002C` |
| -9  | `0x000003C0` | -3 | `0x00000380` |
| -8  | `0x000000D0` | -2 | `0x000000F0` |
| -7  | `0x00000120` | -1 | `0x000001A0` |
| -6  | `0x00000014` |  0 | `0x00000012` |

### 4. Chi (χ) — Nonlinear Layer
```
B0 ← A1 · A2
B1 ← A2 · A0
B2 ← A0 · A1
Ay ← Ay ⊕ By,  ∀y ∈ {0, 1, 2}
```
> Chi is the **only nonlinear operation** in Xoodoo.

### 5. Rho-east (ρ_east) — Plane Shift
```
A1 ← A1 <<< (0, 1)
A2 ← A2 <<< (2, 8)
```

---

## Cyclist Algorithm

Xoodyak wraps Xoodoo in a framework called **Cyclist mode**, which controls how data flows in and out of the permutation.

### State in Cyclist Mode

The 384-bit state is split into two regions:

```
┌──────────────────────────────┐
│        RATE (Visible)        │  ← First 44 bytes (keyed mode)
│   Data absorbed/extracted    │
├──────────────────────────────┤
│     CAPACITY (Hidden)        │  ← Remaining bytes
│   Never directly accessed    │
└──────────────────────────────┘
```

| Parameter | Value |
|-----------|-------|
| R_hash    | 16 bytes |
| R_kin     | 44 bytes |
| R_kout    | 24 bytes |

The capacity region is never directly exposed to inputs or outputs — this is the foundation of **sponge security**.

---

### DOWN and UP Operations

Cyclist alternates between two fundamental operations:

#### DOWN — Absorbing Data into State
1. Take a block of input data
2. XOR it into the rate portion of the state
3. Append padding byte `0x01` immediately after the data
4. XOR color byte at byte 47 of the state

#### UP — Generating Output from State
1. XOR color byte at byte 47 of the state
2. Apply Xoodoo permutation (12 rounds)
3. Extract required output bytes from the rate region

---

### Color Bytes

Each operation uses a distinct color byte to provide **domain separation**:

| Operation | Color Name | Value |
|-----------|-----------|-------|
| Absorbing Key | `COLOR_ABSORB_KEY` | `0x02` |
| Absorbing Data | `COLOR_ABSORB` | `0x03` |
| Encrypting/Decrypting | `COLOR_CRYPT` | `0x80` |
| Squeezing Tag | `COLOR_SQUEEZE` | `0x40` |
| Continuation | `COLOR_ZERO` | `0x00` |

---

### Cyclist Functions

| Function | Description |
|----------|-------------|
| `AbsorbKey(K)` | Loads the secret key into the state; always first in keyed mode |
| `AbsorbAny(X, r, cd)` | General-purpose absorption for data of any length, split into blocks of size `r` |
| `Absorb(A)` | Specialized absorption for associated data; wraps `AbsorbAny()` |
| `Crypt()` | Encrypts/decrypts by XORing message blocks with keystream; absorbs plaintext back into state |
| `Squeeze(l)` | Extracts `l` bytes of output; used for authentication tags and hash outputs |
| `SqueezeAny(l, cu)` | General-purpose output extraction for arbitrary lengths; performs multiple permutation calls if needed |

---

### Encryption Workflow

Authenticated Encryption (AE) provides both **confidentiality** and **authenticity**.

```
Step 1 — INITIALIZATION
  State = 0x00...00 (48 bytes)
  Key + Nonce → AbsorbKey() → AbsorbAny() → DOWN()

Step 2 — ABSORB ASSOCIATED DATA
  Absorb(A) → AbsorbAny() → DOWN()

Step 3 — ENCRYPT PLAINTEXT
  for each block of P:
    UP()  → generate keystream
    C  ← keystream ⊕ P_block
    DOWN() ← absorb C back into state

Step 4 — TAG GENERATION
  Squeeze() → SqueezeAny() → UP(color=0x40)
  → final permutation → T (16-byte auth tag)

Output: C ∥ T
```

---

### Ratchet

**Ratchet** is a one-way, irreversible state update that provides **forward secrecy**.

```
Without Ratchet:  State(T) → State(T-1) → State(T-2) → ...
With Ratchet:     State(T) → ✗ (cannot go back)
```

#### How Ratchet Works

```
Current State S
    ↓
SqueezeAny(16, color=0x10)   → Xoodoo runs, extract 16 bytes R
    ↓
AbsorbAny(R, R_absorb, 0x00) → XOR R back into state + padding
    ↓
New State S' (no way back to S)
```

#### Two Placement Options

**Option 1 — Ratchet after Encryption, before Tag:**
```
Init(K, nonce) → Absorb(A) → C ← Encrypt(P) → Ratchet() → T ← Squeeze(t)
```
The ciphertext and tag are cryptographically separated — even recovering `T` cannot reach the state that produced `C`.

**Option 2 — Ratchet after Tag:**
```
Init(K, nonce) → Absorb(A) → C ← Encrypt(P) → T ← Squeeze(t) → Ratchet()
```
The next message encryption starts from the post-ratchet state — past messages stay safe even if the current state is compromised.

---

## Trail Analysis

Trail analysis tracks how an initial difference (e.g., a single flipped bit) propagates through each round of the cipher.

### Differential Trail

A **differential trail** records how an input difference `ΔIN` evolves through the Xoodoo permutation round by round:

```
θ → ρ_west → ι → χ → ρ_east
```

- **θ, ρ, ι** — linear operations, propagate differences with probability **1** (zero cost)
- **χ** — the only **nonlinear** operation; differences propagate probabilistically

The overall trail probability depends **entirely** on the Chi (χ) steps encountered.

#### CP-Kernel

To minimize trail weight, input differences are chosen from the **CP-kernel** (Column Parity Kernel) — states where the column parity is zero for every column. This ensures Theta (θ) adds no new active columns.

---

### S-Box and DDT Table

Since Chi (χ) is the only nonlinear component, it defines the **S-Box** (3-bit input → 3-bit output):

| Input (bin) | Output (bin) | Output (dec) |
|-------------|--------------|--------------|
| 000 | 000 | 0 |
| 001 | 011 | 3 |
| 010 | 110 | 6 |
| 011 | 001 | 1 |
| 100 | 101 | 5 |
| 101 | 100 | 4 |
| 110 | 010 | 2 |
| 111 | 111 | 7 |

The **Difference Distribution Table (DDT)** shows that for any non-zero input difference δ_in, exactly **4 output differences** are compatible, each with probability:

```
2/8 = 1/4 = 2^(-2)
```

Therefore, each active column contributes a **restriction weight of 2**:

```
Weight = 2 × (Number of Active Columns)
```

---

### One-Round Trail Analysis

#### Standard Sequence (θ → ρ_west → ι → χ → ρ_east)

Starting from a CP-kernel difference, after tracking through all operations:

- Active columns before χ: **2**
- Trail weight: **2 × 2 = 4**

#### After Rephasing (ρ_east → θ → ρ_west → ι → χ)

The linear operations are grouped as a single layer λ, so the round becomes:

```
λ → χ
```

Using the same input difference:

- Active columns before χ: **14**
- Trail weight: **2 × 14 = 28**

The higher weight after rephasing is due to the **dispersion effect** of ρ_east and ρ_west: these shift and redistribute bits across columns, spreading a localized difference throughout the state. This increases the differential trail weight, which is desirable for **cryptographic security**.

---

## References

1. J. Daemen, S. Hoffert, G. Van Assche, R. Van Keer — *Xoodyak Specification*, NIST Lightweight Cryptography Project.  
   https://csrc.nist.gov/projects/lightweight-cryptography

2. J. Daemen, S. Hoffert, G. Van Assche, R. Van Keer — *The Design of Xoodoo and Xoofff*, IACR Transactions on Symmetric Cryptology, vol. 2018, no. 4, pp. 1–38.

3. G. Bertoni, J. Daemen, M. Peeters, G. Van Assche — *Cryptographic Sponge Functions*, January 2011.  
   https://keccak.team/papers.html
