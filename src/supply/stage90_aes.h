/*
 * AES-128, and nothing else: key expansion, one block each way, and CBC.
 *
 * **Why this file exists at all.** Experiment 436 linked the whole kernel, and the run stopped on
 * `aes_encrypt_key128` dereferencing `g_crypto_funcs` - a real `.bss` global that nothing in the
 * tree writes, because its only writer `register_crypto_functions()` has exactly one caller: Apple's
 * `com.apple.kec.corecrypto` kext, which the tarball does not contain. The call is unconditional:
 *
 *     tcp_init  ->  tcp_tfo_init()            (bsd/netinet/tcp_subr.c:449, inlined)
 *               ->  read_frandom(key, 16)     (tcp_init + 0xF4)
 *               ->  aes_encrypt_key128(key, &tfo_ctx)   (tcp_init + 0x104, no branch between)
 *
 * so on the real device the table is non-NULL before `bsd_init` runs, and supplying it is
 * *reproducing* the kernel's own configuration rather than working around it.
 *
 * `aes_encrypt_key128` needs exactly one thing from `g_crypto_funcs`: `ccaes_cbc_encrypt`, a
 * pointer to a `struct ccmode_cbc`, and from that descriptor exactly one method - `init`, which it
 * calls as `cccbc_init(cbc, ctx, 16, key)`. So the honest supply is a **real AES-128 key schedule**
 * and not a stand-in: it is about a hundred lines, it has no dependencies, and its correctness is
 * checkable off the device against FIPS-197's own vectors - which is what `stage90_aes.c`'s
 * `STAGE90_AES_SELFTEST` build is, and what `build_entry.sh` runs before linking it.
 *
 * **Stand-ins are the wrong choice here specifically, and that is worth saying** because the rest of
 * this image uses them everywhere. A stand-in earns its keep when it *stops and names itself*: that
 * is the instrument. `aes_encrypt_key128`'s call site cannot stop - it is unconditional inside
 * `tcp_init`, and stopping there is stopping `domaininit`, which is stopping `bsd_init`. A stand-in
 * that returns without writing would leave `tfo_ctx` uninitialized, which is 436's defect class
 * exactly: a wrong value with nothing to report it. The remaining possibility is a real
 * implementation, so it is one.
 *
 * Nothing here is optimized and nothing here needs to be. The boot path runs one key expansion.
 */

#ifndef STAGE90_AES_H
#define STAGE90_AES_H

#include <stddef.h>
#include <stdint.h>

#define STAGE90_AES_BLOCK_BYTES  16
#define STAGE90_AES128_ROUNDS    10
/* 11 round keys of 16 bytes; FIPS-197 figure 5's `w[0..43]` laid out little-endian by word. */
#define STAGE90_AES128_RK_BYTES  176

/*
 * FIPS-197 section 5.2, for a 16-byte key. Returns 0.
 *
 * `rk` is written in full (176 bytes) and is the only thing the cipher needs afterwards.
 */
int stage90_aes128_expand_key(const uint8_t key[STAGE90_AES_BLOCK_BYTES],
                              uint8_t rk[STAGE90_AES128_RK_BYTES]);

/* FIPS-197 section 5.1, one block. `in` and `out` may not overlap. */
void stage90_aes128_encrypt_block(const uint8_t rk[STAGE90_AES128_RK_BYTES],
                                  const uint8_t in[STAGE90_AES_BLOCK_BYTES],
                                  uint8_t out[STAGE90_AES_BLOCK_BYTES]);

/* FIPS-197 section 5.3 (the inverse cipher), one block. */
void stage90_aes128_decrypt_block(const uint8_t rk[STAGE90_AES128_RK_BYTES],
                                  const uint8_t in[STAGE90_AES_BLOCK_BYTES],
                                  uint8_t out[STAGE90_AES_BLOCK_BYTES]);

/*
 * SP 800-38A section 6.2, over `nblocks` blocks of 16 bytes. `iv` is read and updated in place, so
 * successive calls continue the same stream - which is the contract `struct ccmode_cbc`'s `cbc`
 * method has (`ccmode_impl.h`: "iv will be used and updated").
 */
void stage90_aes128_cbc_encrypt(const uint8_t rk[STAGE90_AES128_RK_BYTES], uint8_t iv[STAGE90_AES_BLOCK_BYTES],
                                size_t nblocks, const uint8_t *in, uint8_t *out);
void stage90_aes128_cbc_decrypt(const uint8_t rk[STAGE90_AES128_RK_BYTES], uint8_t iv[STAGE90_AES_BLOCK_BYTES],
                                size_t nblocks, const uint8_t *in, uint8_t *out);

#endif /* STAGE90_AES_H */
