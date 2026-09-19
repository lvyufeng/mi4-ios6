/*
 * AES-128: FIPS-197 key expansion, the forward and inverse ciphers, and SP 800-38A CBC.
 *
 * The whole file is plain C with two `stdint.h` types and no library calls, so the *same* source is
 * what the entry image links and what `build_entry.sh` compiles natively to run FIPS-197's own
 * vectors against. That is the point: the 437 step adds a **value** to the image - a real key
 * schedule where 436's run found a NULL - and a value is the one kind of thing this ledger has
 * repeatedly got wrong silently (memory: `mi4-stand-in-size-is-not-value`). A known-answer test that
 * stops the build is the remedy, and it costs one `if` in the build script.
 *
 * The formulation is the standard byte-oriented one from FIPS-197 section 3: the 16-byte state is
 * `s[r + 4*c]` for row `r`, column `c`, which is the order the standard prints it in and the order
 * `ShiftRows` and `MixColumns` are stated over.
 */

#include "stage90_aes.h"

/* FIPS-197 figure 7. */
static const uint8_t sbox[256] = {
    0x63, 0x7c, 0x77, 0x7b, 0xf2, 0x6b, 0x6f, 0xc5, 0x30, 0x01, 0x67, 0x2b, 0xfe, 0xd7, 0xab, 0x76,
    0xca, 0x82, 0xc9, 0x7d, 0xfa, 0x59, 0x47, 0xf0, 0xad, 0xd4, 0xa2, 0xaf, 0x9c, 0xa4, 0x72, 0xc0,
    0xb7, 0xfd, 0x93, 0x26, 0x36, 0x3f, 0xf7, 0xcc, 0x34, 0xa5, 0xe5, 0xf1, 0x71, 0xd8, 0x31, 0x15,
    0x04, 0xc7, 0x23, 0xc3, 0x18, 0x96, 0x05, 0x9a, 0x07, 0x12, 0x80, 0xe2, 0xeb, 0x27, 0xb2, 0x75,
    0x09, 0x83, 0x2c, 0x1a, 0x1b, 0x6e, 0x5a, 0xa0, 0x52, 0x3b, 0xd6, 0xb3, 0x29, 0xe3, 0x2f, 0x84,
    0x53, 0xd1, 0x00, 0xed, 0x20, 0xfc, 0xb1, 0x5b, 0x6a, 0xcb, 0xbe, 0x39, 0x4a, 0x4c, 0x58, 0xcf,
    0xd0, 0xef, 0xaa, 0xfb, 0x43, 0x4d, 0x33, 0x85, 0x45, 0xf9, 0x02, 0x7f, 0x50, 0x3c, 0x9f, 0xa8,
    0x51, 0xa3, 0x40, 0x8f, 0x92, 0x9d, 0x38, 0xf5, 0xbc, 0xb6, 0xda, 0x21, 0x10, 0xff, 0xf3, 0xd2,
    0xcd, 0x0c, 0x13, 0xec, 0x5f, 0x97, 0x44, 0x17, 0xc4, 0xa7, 0x7e, 0x3d, 0x64, 0x5d, 0x19, 0x73,
    0x60, 0x81, 0x4f, 0xdc, 0x22, 0x2a, 0x90, 0x88, 0x46, 0xee, 0xb8, 0x14, 0xde, 0x5e, 0x0b, 0xdb,
    0xe0, 0x32, 0x3a, 0x0a, 0x49, 0x06, 0x24, 0x5c, 0xc2, 0xd3, 0xac, 0x62, 0x91, 0x95, 0xe4, 0x79,
    0xe7, 0xc8, 0x37, 0x6d, 0x8d, 0xd5, 0x4e, 0xa9, 0x6c, 0x56, 0xf4, 0xea, 0x65, 0x7a, 0xae, 0x08,
    0xba, 0x78, 0x25, 0x2e, 0x1c, 0xa6, 0xb4, 0xc6, 0xe8, 0xdd, 0x74, 0x1f, 0x4b, 0xbd, 0x8b, 0x8a,
    0x70, 0x3e, 0xb5, 0x66, 0x48, 0x03, 0xf6, 0x0e, 0x61, 0x35, 0x57, 0xb9, 0x86, 0xc1, 0x1d, 0x9e,
    0xe1, 0xf8, 0x98, 0x11, 0x69, 0xd9, 0x8e, 0x94, 0x9b, 0x1e, 0x87, 0xe9, 0xce, 0x55, 0x28, 0xdf,
    0x8c, 0xa1, 0x89, 0x0d, 0xbf, 0xe6, 0x42, 0x68, 0x41, 0x99, 0x2d, 0x0f, 0xb0, 0x54, 0xbb, 0x16,
};

/* FIPS-197 figure 14. */
static const uint8_t rsbox[256] = {
    0x52, 0x09, 0x6a, 0xd5, 0x30, 0x36, 0xa5, 0x38, 0xbf, 0x40, 0xa3, 0x9e, 0x81, 0xf3, 0xd7, 0xfb,
    0x7c, 0xe3, 0x39, 0x82, 0x9b, 0x2f, 0xff, 0x87, 0x34, 0x8e, 0x43, 0x44, 0xc4, 0xde, 0xe9, 0xcb,
    0x54, 0x7b, 0x94, 0x32, 0xa6, 0xc2, 0x23, 0x3d, 0xee, 0x4c, 0x95, 0x0b, 0x42, 0xfa, 0xc3, 0x4e,
    0x08, 0x2e, 0xa1, 0x66, 0x28, 0xd9, 0x24, 0xb2, 0x76, 0x5b, 0xa2, 0x49, 0x6d, 0x8b, 0xd1, 0x25,
    0x72, 0xf8, 0xf6, 0x64, 0x86, 0x68, 0x98, 0x16, 0xd4, 0xa4, 0x5c, 0xcc, 0x5d, 0x65, 0xb6, 0x92,
    0x6c, 0x70, 0x48, 0x50, 0xfd, 0xed, 0xb9, 0xda, 0x5e, 0x15, 0x46, 0x57, 0xa7, 0x8d, 0x9d, 0x84,
    0x90, 0xd8, 0xab, 0x00, 0x8c, 0xbc, 0xd3, 0x0a, 0xf7, 0xe4, 0x58, 0x05, 0xb8, 0xb3, 0x45, 0x06,
    0xd0, 0x2c, 0x1e, 0x8f, 0xca, 0x3f, 0x0f, 0x02, 0xc1, 0xaf, 0xbd, 0x03, 0x01, 0x13, 0x8a, 0x6b,
    0x3a, 0x91, 0x11, 0x41, 0x4f, 0x67, 0xdc, 0xea, 0x97, 0xf2, 0xcf, 0xce, 0xf0, 0xb4, 0xe6, 0x73,
    0x96, 0xac, 0x74, 0x22, 0xe7, 0xad, 0x35, 0x85, 0xe2, 0xf9, 0x37, 0xe8, 0x1c, 0x75, 0xdf, 0x6e,
    0x47, 0xf1, 0x1a, 0x71, 0x1d, 0x29, 0xc5, 0x89, 0x6f, 0xb7, 0x62, 0x0e, 0xaa, 0x18, 0xbe, 0x1b,
    0xfc, 0x56, 0x3e, 0x4b, 0xc6, 0xd2, 0x79, 0x20, 0x9a, 0xdb, 0xc0, 0xfe, 0x78, 0xcd, 0x5a, 0xf4,
    0x1f, 0xdd, 0xa8, 0x33, 0x88, 0x07, 0xc7, 0x31, 0xb1, 0x12, 0x10, 0x59, 0x27, 0x80, 0xec, 0x5f,
    0x60, 0x51, 0x7f, 0xa9, 0x19, 0xb5, 0x4a, 0x0d, 0x2d, 0xe5, 0x7a, 0x9f, 0x93, 0xc9, 0x9c, 0xef,
    0xa0, 0xe0, 0x3b, 0x4d, 0xae, 0x2a, 0xf5, 0xb0, 0xc8, 0xeb, 0xbb, 0x3c, 0x83, 0x53, 0x99, 0x61,
    0x17, 0x2b, 0x04, 0x7e, 0xba, 0x77, 0xd6, 0x26, 0xe1, 0x69, 0x14, 0x63, 0x55, 0x21, 0x0c, 0x7d,
};

/* `x * 2` in GF(2^8) with FIPS-197's reducing polynomial x^8 + x^4 + x^3 + x + 1 (0x11B). */
static uint8_t xtime(uint8_t x)
{
    return (uint8_t)((x << 1) ^ (uint8_t)((x & 0x80u) ? 0x1bu : 0x00u));
}

static void add_round_key(uint8_t s[16], const uint8_t *rk)
{
    for (unsigned i = 0; i < 16; i++) {
        s[i] ^= rk[i];
    }
}

static void sub_bytes(uint8_t s[16]) { for (unsigned i = 0; i < 16; i++) s[i] = sbox[s[i]]; }
static void inv_sub_bytes(uint8_t s[16]) { for (unsigned i = 0; i < 16; i++) s[i] = rsbox[s[i]]; }

/* FIPS-197 5.1.2: row `r` rotates left by `r`. */
static void shift_rows(uint8_t s[16])
{
    uint8_t t[16];
    for (unsigned i = 0; i < 16; i++) t[i] = s[i];
    for (unsigned r = 0; r < 4; r++) {
        for (unsigned c = 0; c < 4; c++) {
            s[r + 4 * c] = t[r + 4 * ((c + r) & 3u)];
        }
    }
}

/* FIPS-197 5.3.1: row `r` rotates right by `r`. */
static void inv_shift_rows(uint8_t s[16])
{
    uint8_t t[16];
    for (unsigned i = 0; i < 16; i++) t[i] = s[i];
    for (unsigned r = 0; r < 4; r++) {
        for (unsigned c = 0; c < 4; c++) {
            s[r + 4 * c] = t[r + 4 * ((c + 4 - r) & 3u)];
        }
    }
}

/* FIPS-197 5.1.3. */
static void mix_columns(uint8_t s[16])
{
    for (unsigned c = 0; c < 4; c++) {
        uint8_t *p = s + 4 * c;
        uint8_t a0 = p[0], a1 = p[1], a2 = p[2], a3 = p[3];
        p[0] = (uint8_t)(xtime(a0) ^ (xtime(a1) ^ a1) ^ a2 ^ a3);
        p[1] = (uint8_t)(a0 ^ xtime(a1) ^ (xtime(a2) ^ a2) ^ a3);
        p[2] = (uint8_t)(a0 ^ a1 ^ xtime(a2) ^ (xtime(a3) ^ a3));
        p[3] = (uint8_t)((xtime(a0) ^ a0) ^ a1 ^ a2 ^ xtime(a3));
    }
}

/* `a * b` in GF(2^8) with the same polynomial, by the usual shift-and-add. */
static uint8_t gmul(uint8_t a, uint8_t b)
{
    uint8_t r = 0;
    while (b) {
        if (b & 1u) {
            r ^= a;
        }
        a = xtime(a);
        b >>= 1;
    }
    return r;
}

/* FIPS-197 5.3.3, using the 0e/0b/0d/09 coefficients. */
static void inv_mix_columns(uint8_t s[16])
{
    for (unsigned c = 0; c < 4; c++) {
        uint8_t *p = s + 4 * c;
        uint8_t a0 = p[0], a1 = p[1], a2 = p[2], a3 = p[3];
        p[0] = (uint8_t)(gmul(a0, 0x0e) ^ gmul(a1, 0x0b) ^ gmul(a2, 0x0d) ^ gmul(a3, 0x09));
        p[1] = (uint8_t)(gmul(a0, 0x09) ^ gmul(a1, 0x0e) ^ gmul(a2, 0x0b) ^ gmul(a3, 0x0d));
        p[2] = (uint8_t)(gmul(a0, 0x0d) ^ gmul(a1, 0x09) ^ gmul(a2, 0x0e) ^ gmul(a3, 0x0b));
        p[3] = (uint8_t)(gmul(a0, 0x0b) ^ gmul(a1, 0x0d) ^ gmul(a2, 0x09) ^ gmul(a3, 0x0e));
    }
}

/*
 * FIPS-197 5.2. The round constants are `Rcon[1..10]` with a leading dummy at index 0, which is what
 * makes `Rcon[i / 4]` the right index at `i % 4 == 0` without an off-by-one term.
 */
static const uint8_t Rcon[11] = {
    0x8d, 0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80, 0x1b, 0x36,
};

int stage90_aes128_expand_key(const uint8_t key[STAGE90_AES_BLOCK_BYTES],
                              uint8_t rk[STAGE90_AES128_RK_BYTES])
{
    for (unsigned i = 0; i < 16; i++) {
        rk[i] = key[i];
    }
    for (unsigned i = 4; i < 44; i++) {
        uint8_t t[4];
        for (unsigned j = 0; j < 4; j++) {
            t[j] = rk[4 * (i - 1) + j];
        }
        if (i % 4u == 0u) {
            /* RotWord, then SubWord, then the round constant on the first byte only. */
            uint8_t a = t[0];
            t[0] = (uint8_t)(sbox[t[1]] ^ Rcon[i / 4u]);
            t[1] = sbox[t[2]];
            t[2] = sbox[t[3]];
            t[3] = sbox[a];
        }
        for (unsigned j = 0; j < 4; j++) {
            rk[4 * i + j] = (uint8_t)(rk[4 * (i - 4) + j] ^ t[j]);
        }
    }
    return 0;
}

void stage90_aes128_encrypt_block(const uint8_t rk[STAGE90_AES128_RK_BYTES],
                                  const uint8_t in[STAGE90_AES_BLOCK_BYTES],
                                  uint8_t out[STAGE90_AES_BLOCK_BYTES])
{
    uint8_t s[16];
    for (unsigned i = 0; i < 16; i++) {
        s[i] = in[i];
    }
    add_round_key(s, rk);
    for (unsigned round = 1; round < STAGE90_AES128_ROUNDS; round++) {
        sub_bytes(s);
        shift_rows(s);
        mix_columns(s);
        add_round_key(s, rk + 16 * round);
    }
    sub_bytes(s);
    shift_rows(s);
    add_round_key(s, rk + 16 * STAGE90_AES128_ROUNDS);
    for (unsigned i = 0; i < 16; i++) {
        out[i] = s[i];
    }
}

void stage90_aes128_decrypt_block(const uint8_t rk[STAGE90_AES128_RK_BYTES],
                                  const uint8_t in[STAGE90_AES_BLOCK_BYTES],
                                  uint8_t out[STAGE90_AES_BLOCK_BYTES])
{
    uint8_t s[16];
    for (unsigned i = 0; i < 16; i++) {
        s[i] = in[i];
    }
    add_round_key(s, rk + 16 * STAGE90_AES128_ROUNDS);
    for (unsigned round = STAGE90_AES128_ROUNDS; round-- > 1u;) {
        inv_shift_rows(s);
        inv_sub_bytes(s);
        add_round_key(s, rk + 16 * round);
        inv_mix_columns(s);
    }
    inv_shift_rows(s);
    inv_sub_bytes(s);
    add_round_key(s, rk);
    for (unsigned i = 0; i < 16; i++) {
        out[i] = s[i];
    }
}

void stage90_aes128_cbc_encrypt(const uint8_t rk[STAGE90_AES128_RK_BYTES], uint8_t iv[STAGE90_AES_BLOCK_BYTES],
                                size_t nblocks, const uint8_t *in, uint8_t *out)
{
    for (size_t b = 0; b < nblocks; b++) {
        uint8_t blk[16];
        for (unsigned i = 0; i < 16; i++) {
            blk[i] = (uint8_t)(in[16 * b + i] ^ iv[i]);
        }
        stage90_aes128_encrypt_block(rk, blk, out + 16 * b);
        for (unsigned i = 0; i < 16; i++) {
            iv[i] = out[16 * b + i];
        }
    }
}

void stage90_aes128_cbc_decrypt(const uint8_t rk[STAGE90_AES128_RK_BYTES], uint8_t iv[STAGE90_AES_BLOCK_BYTES],
                                size_t nblocks, const uint8_t *in, uint8_t *out)
{
    for (size_t b = 0; b < nblocks; b++) {
        uint8_t blk[16];
        stage90_aes128_decrypt_block(rk, in + 16 * b, blk);
        for (unsigned i = 0; i < 16; i++) {
            out[16 * b + i] = (uint8_t)(blk[i] ^ iv[i]);
            iv[i] = in[16 * b + i];
        }
    }
}

/* ------------------------------------------------------------------------------------------------
 * The known-answer tests, compiled only into the host build `build_entry.sh` runs.
 *
 * Every vector below is printed in FIPS-197 or SP 800-38A. The first is the standard's own worked
 * example (Appendix B), the second is its AES-128 example (Appendix C.1), the third is its AES-128
 * key expansion (Appendix A.1) - included because the key schedule is the *only* thing the boot
 * path actually calls, so a wrong `Rcon` or a wrong `RotWord` would be invisible in a block test
 * and is not invisible here - and the rest are SP 800-38A's CBC vectors (F.2.1 encrypt, F.2.2
 * decrypt), which is the mode `struct ccmode_cbc` is.
 * ---------------------------------------------------------------------------------------------- */

#ifdef STAGE90_AES_SELFTEST

#include <stdio.h>
#include <string.h>

static int failures;

static void hex2bin(const char *hex, uint8_t *out, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        unsigned v;
        sscanf(hex + 2 * i, "%2x", &v);
        out[i] = (uint8_t)v;
    }
}

static void check(const char *what, const uint8_t *got, const uint8_t *want, size_t n)
{
    if (memcmp(got, want, n) != 0) {
        failures++;
        printf("FAIL %s\n  got  ", what);
        for (size_t i = 0; i < n; i++) printf("%02x", got[i]);
        printf("\n  want ");
        for (size_t i = 0; i < n; i++) printf("%02x", want[i]);
        printf("\n");
    } else {
        printf("ok   %s\n", what);
    }
}

int main(void)
{
    uint8_t key[16], pt[16], ct[16], rk[STAGE90_AES128_RK_BYTES], got[16];

    /* FIPS-197 Appendix B: 128-bit key, one block each way. */
    hex2bin("2b7e151628aed2a6abf7158809cf4f3c", key, 16);
    hex2bin("3243f6a8885a308d313198a2e0370734", pt, 16);
    hex2bin("3925841d02dc09fbdc118597196a0b32", ct, 16);
    stage90_aes128_expand_key(key, rk);
    stage90_aes128_encrypt_block(rk, pt, got);
    check("FIPS-197 B ciphertext", got, ct, 16);
    stage90_aes128_decrypt_block(rk, ct, got);
    check("FIPS-197 B plaintext", got, pt, 16);

    /*
     * FIPS-197 Appendix A.1: the key expansion of the same key. Checked in full, not just its last
     * round - a single wrong byte anywhere in `w[4..43]` would still encrypt one block correctly
     * only if it were cancelled, which it cannot be, but checking every word localises a defect to
     * the word that is wrong instead of leaving it to a block-level mismatch.
     */
    {
        static const char *want[11] = {
            "2b7e151628aed2a6abf7158809cf4f3c",
            "a0fafe1788542cb123a339392a6c7605",
            "f2c295f27a96b9435935807a7359f67f",
            "3d80477d4716fe3e1e237e446d7a883b",
            "ef44a541a8525b7fb671253bdb0bad00",
            "d4d1c6f87c839d87caf2b8bc11f915bc",
            "6d88a37a110b3efddbf98641ca0093fd",
            "4e54f70e5f5fc9f384a64fb24ea6dc4f",
            "ead27321b58dbad2312bf5607f8d292f",
            "ac7766f319fadc2128d12941575c006e",
            "d014f9a8c9ee2589e13f0cc8b6630ca6",
        };
        for (unsigned r = 0; r <= STAGE90_AES128_ROUNDS; r++) {
            uint8_t w[16];
            char label[48];
            hex2bin(want[r], w, 16);
            snprintf(label, sizeof label, "FIPS-197 A.1 round key %u", r);
            check(label, rk + 16 * r, w, 16);
        }
    }

    /* FIPS-197 Appendix C.1: the plaintext 00112233... and key 000102... . */
    hex2bin("000102030405060708090a0b0c0d0e0f", key, 16);
    hex2bin("00112233445566778899aabbccddeeff", pt, 16);
    hex2bin("69c4e0d86a7b0430d8cdb78070b4c55a", ct, 16);
    stage90_aes128_expand_key(key, rk);
    stage90_aes128_encrypt_block(rk, pt, got);
    check("FIPS-197 C.1 ciphertext", got, ct, 16);
    stage90_aes128_decrypt_block(rk, ct, got);
    check("FIPS-197 C.1 plaintext", got, pt, 16);

    /* SP 800-38A F.2.1: four blocks of CBC, and F.2.2 is the same data backwards. */
    {
        uint8_t iv[16], ivc[16];
        uint8_t cbcpt[64], cbcwant[64], cbcgot[64];
        hex2bin("2b7e151628aed2a6abf7158809cf4f3c", key, 16);
        hex2bin("000102030405060708090a0b0c0d0e0f", iv, 16);
        hex2bin("6bc1bee22e409f96e93d7e117393172a"
                "ae2d8a571e03ac9c9eb76fac45af8e51"
                "30c81c46a35ce411e5fbc1191a0a52ef"
                "f69f2445df4f9b17ad2b417be66c3710", cbcpt, 64);
        hex2bin("7649abac8119b246cee98e9b12e9197d"
                "5086cb9b507219ee95db113a917678b2"
                "73bed6b8e3c1743b7116e69e22229516"
                "3ff1caa1681fac09120eca307586e1a7", cbcwant, 64);
        stage90_aes128_expand_key(key, rk);
        for (unsigned i = 0; i < 16; i++) ivc[i] = iv[i];
        stage90_aes128_cbc_encrypt(rk, ivc, 4, cbcpt, cbcgot);
        check("SP 800-38A F.2.1 CBC ciphertext", cbcgot, cbcwant, 64);
        check("SP 800-38A F.2.1 final IV", ivc, cbcwant + 48, 16);

        for (unsigned i = 0; i < 16; i++) ivc[i] = iv[i];
        stage90_aes128_cbc_decrypt(rk, ivc, 4, cbcwant, cbcgot);
        check("SP 800-38A F.2.2 CBC plaintext", cbcgot, cbcpt, 64);
        check("SP 800-38A F.2.2 final IV", ivc, cbcwant + 48, 16);
    }

    if (failures) {
        printf("\n%d known-answer test(s) FAILED - build refused\n", failures);
        return 1;
    }
    printf("\nall known-answer tests passed\n");
    return 0;
}

#endif /* STAGE90_AES_SELFTEST */
