/*
 * `g_crypto_funcs`, supplied by this image because the thing that supplies it in a real kernel -
 * `com.apple.kec.corecrypto` - is a kext that is not in the tarball (experiment 437).
 *
 * Why this file exists
 * --------------------
 * 436 linked the whole kernel and the run stopped on something that is not a symbol at all:
 *
 *     exception: data abort
 *     xnu_entry_abort_first_pc    = 0x80409DB8     aes_encrypt_key128 + 0x18
 *     xnu_entry_abort_first_insn  = 0xE590603C     ldr r6, [r0, #0x3c]
 *     xnu_entry_abort_first_dfar  = 0x0000003C     r0 was 0
 *     xnu_entry_stub_caller_v     = 0x00000000     no stub was reached at all
 *
 * `r0` came from `0x8053381C`, which is `g_crypto_funcs`: a real `.bss` global, defined `B` by
 * `libkern/crypto/register_crypto.o`, whose only writer is `register_crypto_functions()` and whose
 * only caller in the whole source tree is Apple's `com.apple.kec.corecrypto` kext. **A definition
 * with the wrong value, and nothing to report it.** The call is unconditional:
 *
 *     tcp_init  ->  tcp_tfo_init()                     bsd/netinet/tcp_subr.c:449, inlined
 *               ->  read_frandom(key, 16)              tcp_init + 0xF4
 *               ->  aes_encrypt_key128(key, &tfo_ctx)  tcp_init + 0x104, no branch between
 *
 * so on the real device the table is non-NULL before `bsd_init` runs. Supplying it is reproducing
 * the kernel's own configuration, not working around it.
 *
 * What is real and what is not
 * ----------------------------
 * **`ccaes_cbc_encrypt` and `ccaes_cbc_decrypt` are real AES-128**, from `stage90_aes.c`, which is
 * FIPS-197 and is checked against FIPS-197's own vectors by the build before this object is linked.
 * That is what `aes_encrypt_key128` needs - it reads `cbc->size` and calls `cbc->init(cbc, ctx, 16,
 * key)` and nothing else - and it is real rather than a stand-in for a reason worth writing down,
 * because everywhere else in this image a stand-in is the right answer: **a stand-in earns its keep
 * by stopping and naming itself, and this call site cannot stop.** It is unconditional inside
 * `tcp_init`, so stopping there is stopping `domaininit` and therefore `bsd_init`. A stand-in that
 * returned without writing would leave `tfo_ctx` uninitialized - 436's defect class exactly, a wrong
 * value with nothing to report it. Real AES is also the only one of the three options that can be
 * *measured*, and this file makes that measurement twice: once on the host against FIPS-197, and
 * once in the image, in the constructor, against FIPS-197's Appendix C.1 block.
 *
 * **Everything else in the table is a stand-in that stops and names the field it was called
 * through.** `struct crypto_functions` has 45 members; the eleven objects that are linked and
 * reference `g_crypto_funcs` read 28 of them (`libkern/crypto/corecrypto_{aes,aesxts,des,md5,rand,
 * rsa,sha1,sha2,chacha20poly1305}.o`, `bsd/netinet/flow_divert.o`, `osfmk/kern/btlog.o`), and
 * whatever reads one gets `stub_hit=g_crypto_funcs.<field>` with its caller key rather than a fault
 * on a NULL. **Descriptor-valued fields get a stand-in descriptor rather than NULL, and that is the
 * whole point of the exercise**: a NULL descriptor is 436's stop, and a stand-in descriptor is this
 * instrument's.
 *
 * The digests are the one place where the stand-in is knowingly imperfect, and it is named here
 * rather than left to be found: `ccdigest_di_decl(di, ctx)` and `cchmac_di_decl(di, ctx)` size a
 * stack buffer from `di->state_size` *before* any method is called, so a stand-in `ccdigest_info`
 * with `state_size = 0` gives that declaration a zero-length context and the stop arrives one
 * statement later, at the stand-in method, with the right name and key. Nothing on the boot path
 * reaches a digest - `flow_divert_packet_compute_hmac` is the only user and it is a data-path
 * function behind a `g_crypto_funcs == NULL || group->token_key == NULL` guard - so this is
 * recorded, not fixed. A real SHA-1 is the step that reaches it.
 *
 * Why the table is Apple's struct and not a copy of it
 * ---------------------------------------------------
 * `crypto_functions_t` and every descriptor type come from the tree's own headers -
 * `<libkern/crypto/register_crypto.h>`, `<libkern/crypto/aes.h>`, `<corecrypto/ccmode.h>` - and this
 * file is compiled by `tools/build_xnu_arm_kernel.sh`'s platform block under the same include roots
 * and the same defines as the objects it supplies values to. **A hand-written mirror of any of these
 * layouts would be a second definition of a struct, which is the defect class this project has a
 * memory about (`mi4-one-value-two-definitions`)** - and it would not fail loudly: it would call a
 * method through the wrong offset.
 */

#include <libkern/crypto/register_crypto.h>
#include <libkern/crypto/crypto_internal.h>
#include <libkern/crypto/aes.h>

#include "stage90_aes.h"

/*
 * Reporting, exactly as the generated stand-ins in `build_entry.sh` do it and exactly as
 * `stage90_pthread_functions.c` does it. Both symbols are defined by this image's own
 * `entry_stubs.c`, so pass 1 of `build_entry.sh` resolves them and no stub is generated for either.
 */
extern void entry_stub_hit(const char *name, uint32_t caller);
extern void entry_kv(const char *key, uint32_t value);

/* ------------------------------------------------------------------------------------------------
 * The real part: AES-128 CBC, in the shape `struct ccmode_cbc` asks for.
 * ---------------------------------------------------------------------------------------------- */

/*
 * The key schedule `ccaes_cbc_encrypt->init` writes into the caller's context.
 *
 * 16-byte aligned because `cccbc_ctx` is (`ccmode_impl.h`: `cc_aligned_struct(16) cccbc_ctx`), and
 * sized so that the check `aes_encrypt_key` makes passes - it panics if `cbc->size` exceeds
 * `sizeof(aes_encrypt_ctx)`. The marker is not decoration: `.size` is what a *caller* uses to decide
 * whether the context fits, so this struct being larger than the schedule it holds is the honest
 * number, and the static assertion below makes the panic unreachable at build time rather than at
 * run time.
 */
struct stage90_aes_cbc_ctx {
    uint8_t rk[STAGE90_AES128_RK_BYTES];
    uint32_t rounds;      /* CAPTURED: 10, so the schedule self-describes rather than the caller knowing */
    uint32_t marker;      /* CAPTURED: 0x41455331, "AES1" - written last, so a partial init is detectable */
} __attribute__((aligned(16)));

#define STAGE90_AES_CTX_MARKER 0x41455331u

_Static_assert(sizeof(struct stage90_aes_cbc_ctx) <= sizeof(aes_encrypt_ctx),
               "stage90_aes_cbc_ctx does not fit aes_encrypt_ctx, which aes_encrypt_key panics on");

/*
 * Which of `aes.h`'s two `AES_CBC_CTX_MAX_SIZE` branches this build takes, asserted rather than
 * predicted - 437 predicted 288 and the device measured 1984, and the difference is a `#if` the
 * prediction did not read. The arithmetic, from the tree's own macros:
 *
 *   `cc_config.h:190`   `__arm__` (not `__arm64__`) -> `CCN_UNIT_SIZE 4`, so `sizeof(cc_unit) == 4`
 *   `ccn.h:104`         `ccn_sizeof_size(n) = sizeof(cc_unit) * ceil(n / CCN_UNIT_SIZE)`
 *   `aes.h:44`          the `__ARM_NEON__` branch adds `(14-1)*128 + 32` = 1696, the bit-sliced
 *                       schedule: 13 x 128 bytes of expanded keys, 32 bytes of state
 *   `ccmode_impl.h:68`  `cc_aligned_struct(16) cccbc_ctx` -> `sizeof(cccbc_ctx) == 16`
 *   `cc.h:37`           `cc_ctx_n(T, n) = ceil(n / sizeof(T))`, and `aes.h:50` declares the context
 *                       as `cc_ctx_decl(cccbc_ctx, AES_CBC_CTX_MAX_SIZE, ctx)` -> an array rounded up
 *
 *   NEON:     4 + 16 + 256 + 1696 = 1972  -> ceil(1972/16) = 124  -> 124 * 16 = 1984 (0x7C0)
 *   no NEON:  4 + 16 + 256        =  276  -> ceil( 276/16) =  18  ->  18 * 16 =  288 (0x120)
 *
 * 0x120 is exactly what 437's prediction said, so the miss is identified as the branch and not as
 * the arithmetic. This assertion is the check on that reading: it is compiled by the same build, with
 * the same flags, as the code that reports the number - so if a flag changes the branch, the build
 * stops instead of the record quietly moving.
 */
#if defined(__ARM_NEON__) && !defined(__arm64__)
_Static_assert(sizeof(aes_encrypt_ctx) == 1984,
               "AES_CBC_CTX_MAX_SIZE took the bit-sliced branch but does not come to 0x7C0");
_Static_assert(AES_CBC_CTX_MAX_SIZE == 1972,
               "the bit-sliced AES_CBC_CTX_MAX_SIZE is not 1972, so the derivation above is stale");
#else
_Static_assert(sizeof(aes_encrypt_ctx) == 288,
               "AES_CBC_CTX_MAX_SIZE took the scalar branch but does not come to 0x120");
_Static_assert(AES_CBC_CTX_MAX_SIZE == 276,
               "the scalar AES_CBC_CTX_MAX_SIZE is not 276, so the derivation above is stale");
#endif

static int stage90_crypto_aes_cbc_init(const struct ccmode_cbc *cbc, cccbc_ctx *ctx,
                                       size_t key_len, const void *key)
{
    struct stage90_aes_cbc_ctx *c = (struct stage90_aes_cbc_ctx *)(void *)ctx;

    /*
     * `aes_encrypt_key128` and `aes_decrypt_key128` pass 16. `aes_encrypt_key256` passes 32 and
     * there is no AES-256 here, so it stops and names itself - which is the honest answer and the
     * one that keeps this file's promise that an unimplemented key size is a *report* and not a
     * truncated schedule.
     */
    if (key_len != STAGE90_AES_BLOCK_BYTES) {
        entry_stub_hit("g_crypto_funcs.ccaes_cbc.key_len", (uint32_t)(uintptr_t)__builtin_return_address(0));
        return -1;
    }

    stage90_aes128_expand_key((const uint8_t *)key, c->rk);
    c->rounds = STAGE90_AES128_ROUNDS;
    c->marker = STAGE90_AES_CTX_MARKER;
    (void)cbc;
    return 0;
}

static int stage90_crypto_aes_cbc_encrypt_blocks(const cccbc_ctx *ctx, cccbc_iv *iv,
                                                 size_t nblocks, const void *in, void *out)
{
    const struct stage90_aes_cbc_ctx *c = (const struct stage90_aes_cbc_ctx *)(const void *)ctx;

    if (c->marker != STAGE90_AES_CTX_MARKER || c->rounds != STAGE90_AES128_ROUNDS) {
        entry_stub_hit("g_crypto_funcs.ccaes_cbc_encrypt.uninitialized",
                       (uint32_t)(uintptr_t)__builtin_return_address(0));
        return -1;
    }
    stage90_aes128_cbc_encrypt(c->rk, (uint8_t *)(void *)iv, nblocks,
                              (const uint8_t *)in, (uint8_t *)out);
    return 0;
}

static int stage90_crypto_aes_cbc_decrypt_blocks(const cccbc_ctx *ctx, cccbc_iv *iv,
                                                 size_t nblocks, const void *in, void *out)
{
    const struct stage90_aes_cbc_ctx *c = (const struct stage90_aes_cbc_ctx *)(const void *)ctx;

    if (c->marker != STAGE90_AES_CTX_MARKER || c->rounds != STAGE90_AES128_ROUNDS) {
        entry_stub_hit("g_crypto_funcs.ccaes_cbc_decrypt.uninitialized",
                       (uint32_t)(uintptr_t)__builtin_return_address(0));
        return -1;
    }
    stage90_aes128_cbc_decrypt(c->rk, (uint8_t *)(void *)iv, nblocks,
                              (const uint8_t *)in, (uint8_t *)out);
    return 0;
}

/*
 * `block_size` is 16 and it is not cosmetic: `cccbc_set_iv` copies `mode->block_size` bytes into the
 * caller's IV, so a wrong value here is an over- or under-copy rather than a wrong answer.
 */
static const struct ccmode_cbc stage90_ccaes_cbc_encrypt = {
    .size = sizeof(struct stage90_aes_cbc_ctx),
    .block_size = STAGE90_AES_BLOCK_BYTES,
    .init = stage90_crypto_aes_cbc_init,
    .cbc = stage90_crypto_aes_cbc_encrypt_blocks,
    .custom = NULL,
};

static const struct ccmode_cbc stage90_ccaes_cbc_decrypt = {
    .size = sizeof(struct stage90_aes_cbc_ctx),
    .block_size = STAGE90_AES_BLOCK_BYTES,
    .init = stage90_crypto_aes_cbc_init,
    .cbc = stage90_crypto_aes_cbc_decrypt_blocks,
    .custom = NULL,
};

/* ------------------------------------------------------------------------------------------------
 * The stand-ins: one self-naming body per shape, and one stand-in descriptor per mode.
 *
 * `STAGE90_CRYPTO_STUB(field, RET, PARAMS, RETVAL)` - the name in the report is the `struct
 * crypto_functions` member, so a stop says which *slot* was called, not merely that crypto was
 * missing. `PARAMS` is passed parenthesised, which is what lets it contain commas.
 * ---------------------------------------------------------------------------------------------- */

#define STAGE90_CRYPTO_STUB(field, RET, PARAMS, RETVAL)                          \
    static RET stage90_crypto_##field PARAMS                                     \
    {                                                                            \
        entry_stub_hit("g_crypto_funcs." #field,                                 \
                       (uint32_t)(uintptr_t)__builtin_return_address(0));        \
        return RETVAL;                                                           \
    }

/* digests: the four common functions */
STAGE90_CRYPTO_STUB(ccdigest_init_fn, void,
                    (const struct ccdigest_info *di, ccdigest_ctx_t ctx), (void)0)
STAGE90_CRYPTO_STUB(ccdigest_update_fn, void,
                    (const struct ccdigest_info *di, ccdigest_ctx_t ctx,
                     unsigned long len, const void *data), (void)0)
STAGE90_CRYPTO_STUB(ccdigest_final_fn, void,
                    (const struct ccdigest_info *di, ccdigest_ctx_t ctx, void *digest), (void)0)
STAGE90_CRYPTO_STUB(ccdigest_fn, void,
                    (const struct ccdigest_info *di, unsigned long len,
                     const void *data, void *digest), (void)0)

/* hmac: the three common functions, and the one-shot */
STAGE90_CRYPTO_STUB(cchmac_init_fn, void,
                    (const struct ccdigest_info *di, cchmac_ctx_t ctx,
                     unsigned long key_len, const void *key), (void)0)
STAGE90_CRYPTO_STUB(cchmac_update_fn, void,
                    (const struct ccdigest_info *di, cchmac_ctx_t ctx,
                     unsigned long data_len, const void *data), (void)0)
STAGE90_CRYPTO_STUB(cchmac_final_fn, void,
                    (const struct ccdigest_info *di, cchmac_ctx_t ctx, unsigned char *mac), (void)0)
STAGE90_CRYPTO_STUB(cchmac_fn, void,
                    (const struct ccdigest_info *di, unsigned long key_len, const void *key,
                     unsigned long data_len, const void *data, unsigned char *mac), (void)0)

/* gcm's two free functions */
STAGE90_CRYPTO_STUB(ccgcm_init_with_iv_fn, int,
                    (const struct ccmode_gcm *mode, ccgcm_ctx *ctx, size_t key_nbytes,
                     const void *key, const void *iv), -1)
STAGE90_CRYPTO_STUB(ccgcm_inc_iv_fn, int,
                    (const struct ccmode_gcm *mode, ccgcm_ctx *ctx, void *iv), -1)

/* the des key helpers */
STAGE90_CRYPTO_STUB(ccdes_key_is_weak_fn, int, (void *key, unsigned long length), 1)
STAGE90_CRYPTO_STUB(ccdes_key_set_odd_parity_fn, void, (void *key, unsigned long length), (void)0)

/* the two padding helpers */
STAGE90_CRYPTO_STUB(ccpad_xts_encrypt_fn, void,
                    (const struct ccmode_xts *xts, ccxts_ctx *ctx, unsigned long nbytes,
                     const void *in, void *out), (void)0)
STAGE90_CRYPTO_STUB(ccpad_xts_decrypt_fn, void,
                    (const struct ccmode_xts *xts, ccxts_ctx *ctx, unsigned long nbytes,
                     const void *in, void *out), (void)0)
STAGE90_CRYPTO_STUB(ccpad_cts3_crypt_fn, size_t,
                    (const struct ccmode_cbc *cbc, cccbc_ctx *cbc_key, cccbc_iv *iv,
                     size_t nbytes, const void *in, void *out), 0)

/* rng and rsa */
STAGE90_CRYPTO_STUB(ccrng_fn, struct ccrng_state *, (int *error), NULL)
STAGE90_CRYPTO_STUB(ccrsa_make_pub_fn, int,
                    (ccrsa_pub_ctx_t pubk, size_t exp_nbytes, const uint8_t *exp,
                     size_t mod_nbytes, const uint8_t *mod), -1)
STAGE90_CRYPTO_STUB(ccrsa_verify_pkcs1v15_fn, int,
                    (ccrsa_pub_ctx_t key, const uint8_t *oid, size_t digest_len,
                     const uint8_t *digest, size_t sig_len, const uint8_t *sig, bool *valid), -1)

/* the ecb method, shared by every stand-in descriptor of ecb/ctr/... shape */
STAGE90_CRYPTO_STUB(mode_ecb_init, int,
                    (const struct ccmode_ecb *ecb, ccecb_ctx *ctx,
                     size_t key_nbytes, const void *key), -1)
STAGE90_CRYPTO_STUB(mode_ecb_crypt, int,
                    (const ccecb_ctx *ctx, size_t nblocks, const void *in, void *out), -1)
STAGE90_CRYPTO_STUB(mode_cbc_init, int,
                    (const struct ccmode_cbc *cbc, cccbc_ctx *ctx,
                     size_t key_len, const void *key), -1)
STAGE90_CRYPTO_STUB(mode_cbc_crypt, int,
                    (const cccbc_ctx *ctx, cccbc_iv *iv, size_t nblocks,
                     const void *in, void *out), -1)
STAGE90_CRYPTO_STUB(mode_ctr_init, int,
                    (const struct ccmode_ctr *mode, ccctr_ctx *ctx, size_t key_len,
                     const void *key, const void *iv), -1)
STAGE90_CRYPTO_STUB(mode_ctr_setctr, int,
                    (const struct ccmode_ctr *mode, ccctr_ctx *ctx, const void *ctr), -1)
STAGE90_CRYPTO_STUB(mode_ctr_crypt, int,
                    (ccctr_ctx *ctx, size_t nbytes, const void *in, void *out), -1)
STAGE90_CRYPTO_STUB(mode_xts_init, int,
                    (const struct ccmode_xts *xts, ccxts_ctx *ctx, size_t key_nbytes,
                     const void *data_key, const void *tweak_key), -1)
STAGE90_CRYPTO_STUB(mode_xts_key_sched, void,
                    (const struct ccmode_xts *xts, ccxts_ctx *ctx, size_t key_nbytes,
                     const void *data_key, const void *tweak_key), (void)0)
STAGE90_CRYPTO_STUB(mode_xts_set_tweak, int,
                    (const ccxts_ctx *ctx, ccxts_tweak *tweak, const void *iv), -1)
STAGE90_CRYPTO_STUB(mode_xts_crypt, void *,
                    (const ccxts_ctx *ctx, ccxts_tweak *tweak, size_t nblocks,
                     const void *in, void *out), NULL)
STAGE90_CRYPTO_STUB(mode_gcm_init, int,
                    (const struct ccmode_gcm *gcm, ccgcm_ctx *ctx,
                     size_t key_nbytes, const void *key), -1)
STAGE90_CRYPTO_STUB(mode_gcm_set_iv, int,
                    (ccgcm_ctx *ctx, size_t iv_nbytes, const void *iv), -1)
STAGE90_CRYPTO_STUB(mode_gcm_gmac, int,
                    (ccgcm_ctx *ctx, size_t nbytes, const void *in), -1)
STAGE90_CRYPTO_STUB(mode_gcm_gcm, int,
                    (ccgcm_ctx *ctx, size_t nbytes, const void *in, void *out), -1)
STAGE90_CRYPTO_STUB(mode_gcm_finalize, int,
                    (ccgcm_ctx *ctx, size_t tag_nbytes, void *tag), -1)
STAGE90_CRYPTO_STUB(mode_gcm_reset, int, (ccgcm_ctx *ctx), -1)
STAGE90_CRYPTO_STUB(mode_rc4_init, void,
                    (ccrc4_ctx *ctx, size_t key_len, const void *key), (void)0)
STAGE90_CRYPTO_STUB(mode_rc4_crypt, void,
                    (ccrc4_ctx *ctx, size_t nbytes, const void *in, void *out), (void)0)
STAGE90_CRYPTO_STUB(mode_digest_compress, void,
                    (ccdigest_state_t state, size_t nblocks, const void *data), (void)0)
STAGE90_CRYPTO_STUB(mode_digest_final, void,
                    (const struct ccdigest_info *di, ccdigest_ctx_t ctx,
                     unsigned char *digest), (void)0)
STAGE90_CRYPTO_STUB(mode_chacha_info, const struct ccchacha20poly1305_info *, (void), NULL)
STAGE90_CRYPTO_STUB(mode_chacha_init, int,
                    (const struct ccchacha20poly1305_info *info, ccchacha20poly1305_ctx *ctx,
                     const uint8_t *key), -1)
STAGE90_CRYPTO_STUB(mode_chacha_reset, int,
                    (const struct ccchacha20poly1305_info *info, ccchacha20poly1305_ctx *ctx), -1)
STAGE90_CRYPTO_STUB(mode_chacha_setnonce, int,
                    (const struct ccchacha20poly1305_info *info, ccchacha20poly1305_ctx *ctx,
                     const uint8_t *nonce), -1)
STAGE90_CRYPTO_STUB(mode_chacha_incnonce, int,
                    (const struct ccchacha20poly1305_info *info, ccchacha20poly1305_ctx *ctx,
                     uint8_t *nonce), -1)
STAGE90_CRYPTO_STUB(mode_chacha_aad, int,
                    (const struct ccchacha20poly1305_info *info, ccchacha20poly1305_ctx *ctx,
                     size_t nbytes, const void *aad), -1)
STAGE90_CRYPTO_STUB(mode_chacha_encrypt, int,
                    (const struct ccchacha20poly1305_info *info, ccchacha20poly1305_ctx *ctx,
                     size_t nbytes, const void *ptext, void *ctext), -1)
STAGE90_CRYPTO_STUB(mode_chacha_finalize, int,
                    (const struct ccchacha20poly1305_info *info, ccchacha20poly1305_ctx *ctx,
                     uint8_t *tag), -1)
STAGE90_CRYPTO_STUB(mode_chacha_decrypt, int,
                    (const struct ccchacha20poly1305_info *info, ccchacha20poly1305_ctx *ctx,
                     size_t nbytes, const void *ctext, void *ptext), -1)
STAGE90_CRYPTO_STUB(mode_chacha_verify, int,
                    (const struct ccchacha20poly1305_info *info, ccchacha20poly1305_ctx *ctx,
                     const uint8_t *tag), -1)

/*
 * The stand-in descriptors. Their scalar fields are the sizes of a context this image does not
 * implement, and they are not zero for a reason: a caller that sizes storage from `->size` and then
 * calls `->init` allocates nothing and reaches the stop, while a caller that sizes storage from 0
 * might do neither.
 */
#define STAGE90_CRYPTO_STANDIN_ECB  { .size = 0, .block_size = 0, \
    .init = stage90_crypto_mode_ecb_init, .ecb = stage90_crypto_mode_ecb_crypt }
#define STAGE90_CRYPTO_STANDIN_CTR  { .size = 0, .block_size = 1, .ecb_block_size = 0, \
    .init = stage90_crypto_mode_ctr_init, .setctr = stage90_crypto_mode_ctr_setctr, \
    .ctr = stage90_crypto_mode_ctr_crypt, .custom = NULL }
#define STAGE90_CRYPTO_STANDIN_XTS  { .size = 0, .tweak_size = 0, .block_size = 0, \
    .init = stage90_crypto_mode_xts_init, .key_sched = stage90_crypto_mode_xts_key_sched, \
    .set_tweak = stage90_crypto_mode_xts_set_tweak, .xts = stage90_crypto_mode_xts_crypt, \
    .custom = NULL, .custom1 = NULL }
#define STAGE90_CRYPTO_STANDIN_GCM(encdec_)  { .size = 0, .encdec = (encdec_), .block_size = 0, \
    .init = stage90_crypto_mode_gcm_init, .set_iv = stage90_crypto_mode_gcm_set_iv, \
    .gmac = stage90_crypto_mode_gcm_gmac, .gcm = stage90_crypto_mode_gcm_gcm, \
    .finalize = stage90_crypto_mode_gcm_finalize, .reset = stage90_crypto_mode_gcm_reset, \
    .custom = NULL }
#define STAGE90_CRYPTO_STANDIN_CBC  { .size = 0, .block_size = 0, \
    .init = stage90_crypto_mode_cbc_init, .cbc = stage90_crypto_mode_cbc_crypt, .custom = NULL }
#define STAGE90_CRYPTO_STANDIN_RC4  { .size = 0, \
    .init = stage90_crypto_mode_rc4_init, .crypt = stage90_crypto_mode_rc4_crypt }
#define STAGE90_CRYPTO_STANDIN_DIGEST(name_)  { \
    .output_size = 0, .state_size = 0, .block_size = 0, .oid_size = 0, \
    .oid = NULL, .initial_state = NULL, \
    .compress = stage90_crypto_mode_digest_compress, .final = stage90_crypto_mode_digest_final }
#define STAGE90_CRYPTO_STANDIN_CHACHA  { \
    .info = stage90_crypto_mode_chacha_info, .init = stage90_crypto_mode_chacha_init, \
    .reset = stage90_crypto_mode_chacha_reset, .setnonce = stage90_crypto_mode_chacha_setnonce, \
    .incnonce = stage90_crypto_mode_chacha_incnonce, .aad = stage90_crypto_mode_chacha_aad, \
    .encrypt = stage90_crypto_mode_chacha_encrypt, \
    .finalize = stage90_crypto_mode_chacha_finalize, \
    .decrypt = stage90_crypto_mode_chacha_decrypt, .verify = stage90_crypto_mode_chacha_verify }

static const struct ccmode_ecb stage90_ccaes_ecb_encrypt = STAGE90_CRYPTO_STANDIN_ECB;
static const struct ccmode_ecb stage90_ccaes_ecb_decrypt = STAGE90_CRYPTO_STANDIN_ECB;
static const struct ccmode_ctr stage90_ccaes_ctr_crypt = STAGE90_CRYPTO_STANDIN_CTR;
static const struct ccmode_xts stage90_ccaes_xts_encrypt = STAGE90_CRYPTO_STANDIN_XTS;
static const struct ccmode_xts stage90_ccaes_xts_decrypt = STAGE90_CRYPTO_STANDIN_XTS;
static const struct ccmode_gcm stage90_ccaes_gcm_encrypt = STAGE90_CRYPTO_STANDIN_GCM(0);
static const struct ccmode_gcm stage90_ccaes_gcm_decrypt = STAGE90_CRYPTO_STANDIN_GCM(1);
static const struct ccmode_ecb stage90_ccdes_ecb_encrypt = STAGE90_CRYPTO_STANDIN_ECB;
static const struct ccmode_ecb stage90_ccdes_ecb_decrypt = STAGE90_CRYPTO_STANDIN_ECB;
static const struct ccmode_cbc stage90_ccdes_cbc_encrypt = STAGE90_CRYPTO_STANDIN_CBC;
static const struct ccmode_cbc stage90_ccdes_cbc_decrypt = STAGE90_CRYPTO_STANDIN_CBC;
static const struct ccmode_ecb stage90_cctdes_ecb_encrypt = STAGE90_CRYPTO_STANDIN_ECB;
static const struct ccmode_ecb stage90_cctdes_ecb_decrypt = STAGE90_CRYPTO_STANDIN_ECB;
static const struct ccmode_cbc stage90_cctdes_cbc_encrypt = STAGE90_CRYPTO_STANDIN_CBC;
static const struct ccmode_cbc stage90_cctdes_cbc_decrypt = STAGE90_CRYPTO_STANDIN_CBC;
static const struct ccrc4_info stage90_ccrc4_info = STAGE90_CRYPTO_STANDIN_RC4;
static const struct ccmode_ecb stage90_ccblowfish_ecb_encrypt = STAGE90_CRYPTO_STANDIN_ECB;
static const struct ccmode_ecb stage90_ccblowfish_ecb_decrypt = STAGE90_CRYPTO_STANDIN_ECB;
static const struct ccmode_ecb stage90_cccast_ecb_encrypt = STAGE90_CRYPTO_STANDIN_ECB;
static const struct ccmode_ecb stage90_cccast_ecb_decrypt = STAGE90_CRYPTO_STANDIN_ECB;
static const struct ccdigest_info stage90_ccmd5_di = STAGE90_CRYPTO_STANDIN_DIGEST(ccmd5_di);
static const struct ccdigest_info stage90_ccsha1_di = STAGE90_CRYPTO_STANDIN_DIGEST(ccsha1_di);
static const struct ccdigest_info stage90_ccsha256_di = STAGE90_CRYPTO_STANDIN_DIGEST(ccsha256_di);
static const struct ccdigest_info stage90_ccsha384_di = STAGE90_CRYPTO_STANDIN_DIGEST(ccsha384_di);
static const struct ccdigest_info stage90_ccsha512_di = STAGE90_CRYPTO_STANDIN_DIGEST(ccsha512_di);
static const struct ccchacha20poly1305_fns stage90_ccchacha20poly1305_fns = STAGE90_CRYPTO_STANDIN_CHACHA;

/* ------------------------------------------------------------------------------------------------
 * The table, and the one check on it that matters.
 * ---------------------------------------------------------------------------------------------- */

static const struct crypto_functions stage90_crypto_functions = {
    /* digests common functions */
    .ccdigest_init_fn = stage90_crypto_ccdigest_init_fn,
    .ccdigest_update_fn = stage90_crypto_ccdigest_update_fn,
    .ccdigest_final_fn = stage90_crypto_ccdigest_final_fn,
    .ccdigest_fn = stage90_crypto_ccdigest_fn,
    /* digest implementations */
    .ccmd5_di = &stage90_ccmd5_di,
    .ccsha1_di = &stage90_ccsha1_di,
    .ccsha256_di = &stage90_ccsha256_di,
    .ccsha384_di = &stage90_ccsha384_di,
    .ccsha512_di = &stage90_ccsha512_di,
    /* hmac common functions */
    .cchmac_init_fn = stage90_crypto_cchmac_init_fn,
    .cchmac_update_fn = stage90_crypto_cchmac_update_fn,
    .cchmac_final_fn = stage90_crypto_cchmac_final_fn,
    .cchmac_fn = stage90_crypto_cchmac_fn,
    /* AES: the two real entries, and the stand-ins around them */
    .ccaes_ecb_encrypt = &stage90_ccaes_ecb_encrypt,
    .ccaes_ecb_decrypt = &stage90_ccaes_ecb_decrypt,
    .ccaes_cbc_encrypt = &stage90_ccaes_cbc_encrypt,
    .ccaes_cbc_decrypt = &stage90_ccaes_cbc_decrypt,
    .ccaes_ctr_crypt = &stage90_ccaes_ctr_crypt,
    .ccaes_xts_encrypt = &stage90_ccaes_xts_encrypt,
    .ccaes_xts_decrypt = &stage90_ccaes_xts_decrypt,
    .ccaes_gcm_encrypt = &stage90_ccaes_gcm_encrypt,
    .ccaes_gcm_decrypt = &stage90_ccaes_gcm_decrypt,
    .ccgcm_init_with_iv_fn = stage90_crypto_ccgcm_init_with_iv_fn,
    .ccgcm_inc_iv_fn = stage90_crypto_ccgcm_inc_iv_fn,
    .ccchacha20poly1305_fns = &stage90_ccchacha20poly1305_fns,
    /* DES and triple DES */
    .ccdes_ecb_encrypt = &stage90_ccdes_ecb_encrypt,
    .ccdes_ecb_decrypt = &stage90_ccdes_ecb_decrypt,
    .ccdes_cbc_encrypt = &stage90_ccdes_cbc_encrypt,
    .ccdes_cbc_decrypt = &stage90_ccdes_cbc_decrypt,
    .cctdes_ecb_encrypt = &stage90_cctdes_ecb_encrypt,
    .cctdes_ecb_decrypt = &stage90_cctdes_ecb_decrypt,
    .cctdes_cbc_encrypt = &stage90_cctdes_cbc_encrypt,
    .cctdes_cbc_decrypt = &stage90_cctdes_cbc_decrypt,
    /* RC4, Blowfish, CAST */
    .ccrc4_info = &stage90_ccrc4_info,
    .ccblowfish_ecb_encrypt = &stage90_ccblowfish_ecb_encrypt,
    .ccblowfish_ecb_decrypt = &stage90_ccblowfish_ecb_decrypt,
    .cccast_ecb_encrypt = &stage90_cccast_ecb_encrypt,
    .cccast_ecb_decrypt = &stage90_cccast_ecb_decrypt,
    /* DES key helpers */
    .ccdes_key_is_weak_fn = stage90_crypto_ccdes_key_is_weak_fn,
    .ccdes_key_set_odd_parity_fn = stage90_crypto_ccdes_key_set_odd_parity_fn,
    /* XTS and CTS3 padding */
    .ccpad_xts_encrypt_fn = stage90_crypto_ccpad_xts_encrypt_fn,
    .ccpad_xts_decrypt_fn = stage90_crypto_ccpad_xts_decrypt_fn,
    .ccpad_cts3_encrypt_fn = stage90_crypto_ccpad_cts3_crypt_fn,
    .ccpad_cts3_decrypt_fn = stage90_crypto_ccpad_cts3_crypt_fn,
    /* rng and rsa */
    .ccrng_fn = stage90_crypto_ccrng_fn,
    .ccrsa_make_pub_fn = stage90_crypto_ccrsa_make_pub_fn,
    .ccrsa_verify_pkcs1v15_fn = stage90_crypto_ccrsa_verify_pkcs1v15_fn,
};

/*
 * **The check that would have caught 436 before the device did.** `aes_encrypt_key` panics if
 * `ccaes_cbc_encrypt->size > sizeof(aes_encrypt_ctx)`, so the descriptor's own size field is a
 * contract with a caller in another object; the static assertion above is the build-time half and
 * this is the run-time half, measured in the image rather than argued from the source. It is a
 * second statement because the two can disagree in one direction only - the assertion says this
 * file's struct fits, this says the descriptor really points at it after the link.
 */
static void stage90_crypto_functions_register(void) __attribute__ ((constructor));
static void stage90_crypto_functions_register(void)
{
    const struct ccmode_cbc *cbc;
    const void *const *word = (const void *const *)&stage90_crypto_functions;
    size_t words = sizeof(stage90_crypto_functions) / sizeof(void *);
    uint8_t kat_key[16], kat_pt[16], kat_want[16], kat_got[16];
    uint8_t rk[STAGE90_AES128_RK_BYTES];
    size_t i;
    uint32_t nulls = 0;
    int ok = 1;

    cbc = stage90_crypto_functions.ccaes_cbc_encrypt;

    /*
     * **The completeness scan, which is 432's idiom and answers the question this file could get
     * wrong invisibly.** Every member of the table is pointer-sized, so the whole struct is an array
     * of words: a member this file forgot to set is a NULL in that array, C zeroes it silently, and
     * a NULL in this table is exactly what 436's run died on. The count is derived from `sizeof` and
     * not written down, so adding a member to Apple's struct (which would move this file's
     * initializer out of step with it) shows up here as a NULL rather than as a call to address 0.
     */
    for (i = 0; i < words; i++) {
        if (word[i] == NULL) {
            nulls++;
        }
    }

    entry_kv("xnu_entry_stage90_crypto_funcs_ptr", (uint32_t)(uintptr_t)g_crypto_funcs);
    entry_kv("xnu_entry_stage90_crypto_table_addr", (uint32_t)(uintptr_t)&stage90_crypto_functions);
    entry_kv("xnu_entry_stage90_crypto_words", (uint32_t)words);
    entry_kv("xnu_entry_stage90_crypto_nulls", nulls);
    entry_kv("xnu_entry_stage90_crypto_cbc_desc", (uint32_t)(uintptr_t)cbc);
    entry_kv("xnu_entry_stage90_crypto_cbc_size", (uint32_t)cbc->size);
    entry_kv("xnu_entry_stage90_crypto_aes_ctx_size", (uint32_t)sizeof(aes_encrypt_ctx));

    /*
     * FIPS-197 Appendix C.1, run by the image's own compiled AES: key 000102...0f, plaintext
     * 00112233...ff, ciphertext 69c4e0d8...c55a. The host build runs the same vector against the
     * same source (`stage90_aes.c`'s `STAGE90_AES_SELFTEST`, which `build_entry.sh` compiles and
     * runs before this object is linked), so the two together say the algorithm is right *and* that
     * the ARM compilation of it is - which is the only claim that matters to the device.
     */
    {
        static const uint8_t key_be[16] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
                                           0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f};
        static const uint8_t pt_be[16]  = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
                                           0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff};
        static const uint8_t ct_be[16]  = {0x69, 0xc4, 0xe0, 0xd8, 0x6a, 0x7b, 0x04, 0x30,
                                           0xd8, 0xcd, 0xb7, 0x80, 0x70, 0xb4, 0xc5, 0x5a};
        unsigned i;

        for (i = 0; i < 16; i++) {
            kat_key[i] = key_be[i];
            kat_pt[i] = pt_be[i];
            kat_want[i] = ct_be[i];
        }
        stage90_aes128_expand_key(kat_key, rk);
        stage90_aes128_encrypt_block(rk, kat_pt, kat_got);
        for (i = 0; i < 16; i++) {
            if (kat_got[i] != kat_want[i]) {
                ok = 0;
            }
        }
        entry_kv("xnu_entry_stage90_crypto_kat", (uint32_t)ok);
        entry_kv("xnu_entry_stage90_crypto_kat_w0", (uint32_t)(
            ((uint32_t)kat_got[0] << 24) | ((uint32_t)kat_got[1] << 16) |
            ((uint32_t)kat_got[2] << 8) | (uint32_t)kat_got[3]));
    }

    if (!ok) {
        /* Never reached unless the compiled AES disagrees with FIPS-197; the record above says which. */
        entry_stub_hit("stage90_crypto_functions.kat_failed", 0u);
    }
    if (nulls != 0) {
        entry_stub_hit("stage90_crypto_functions.null_word", nulls);
    }
    if (cbc->size > sizeof(aes_encrypt_ctx)) {
        entry_stub_hit("stage90_crypto_functions.cbc_size_too_large", 0u);
    }

    if (register_crypto_functions(&stage90_crypto_functions) != 0) {
        entry_stub_hit("stage90_crypto_functions.not_registered", 0u);
    }
    if (g_crypto_funcs != &stage90_crypto_functions) {
        entry_stub_hit("stage90_crypto_functions.wrong_table", 0u);
    }
}
