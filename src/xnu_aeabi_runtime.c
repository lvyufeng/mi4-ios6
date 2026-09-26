/*
 * The EABI runtime Apple's kernel never needs, because Apple's ABI is not EABI and this project's
 * target is.
 *
 * `armv7-apple-ios` and `armv7-apple-darwin` are Darwin targets: clang lowers an aggregate copy or
 * a zero-fill to a call to `memcpy` or `bzero`, which the kernel already defines in `libkern`. Every
 * ELF armv7 triple — this project's `armv7-unknown-netbsd-eabi` since experiment-161, and
 * `armv7-none-eabi` before it — is EABI, and EABI requires the *same* lowering to call
 * `__aeabi_memcpy4`, `__aeabi_memclr8` and friends instead. Nothing in the tarball defines them:
 * `grep -rn "__aeabi" external/xnu-4570.1.46` over the whole tree is empty, and it is empty because
 * Apple never has to. So they are a cost of the ELF path, and this is where it is paid.
 *
 * These four are on the boot path — `stub_reach.py --from arm_init` puts `__aeabi_memcpy4` at
 * **distance 1**, the nearest missing symbol in the image, because `arm_init`'s first aggregate copy
 * is one — so this is not a tidy-up.
 *
 * Each is a tail call. `-O2` turns every body into a single `b memcpy` / `b memset` / `b memmove`
 * (measured: the twelve functions together are 108 bytes of `.text` and reference nothing but the
 * three libkern routines), which is the point: the runtime must not become a second implementation
 * of anything.
 *
 * The argument order of `__aeabi_memset` is `(dst, n, c)` — size before character — and it is the
 * one that differs from `memset`. It is written here as the EABI specifies it, not as it looks like
 * it should be, and the compiler checks that by the call it generates.
 *
 * The five arithmetic helpers the image also needs — `__aeabi_uldivmod`, `__aeabi_ldivmod`,
 * `__aeabi_d2ulz`, `__aeabi_l2d`, `__aeabi_ul2d` — are not here: they are a compiler runtime, and
 * the compiler runtime the project links is `libgcc.a` from the host's `arm-none-eabi-gcc`. See
 * `tools/build_xnu_arm_kernel.sh` and experiment-163 for why that is the ARM-state multilib and
 * what it costs.
 */

#include <stddef.h>

extern void *memcpy(void *, const void *, size_t);
extern void *memmove(void *, const void *, size_t);
extern void *memset(void *, int, size_t);

void *
__aeabi_memcpy(void *dst, const void *src, size_t n)
{
	return memcpy(dst, src, n);
}

void *
__aeabi_memcpy4(void *dst, const void *src, size_t n)
{
	return memcpy(dst, src, n);
}

void *
__aeabi_memcpy8(void *dst, const void *src, size_t n)
{
	return memcpy(dst, src, n);
}

void *
__aeabi_memmove(void *dst, const void *src, size_t n)
{
	return memmove(dst, src, n);
}

void *
__aeabi_memmove4(void *dst, const void *src, size_t n)
{
	return memmove(dst, src, n);
}

void *
__aeabi_memmove8(void *dst, const void *src, size_t n)
{
	return memmove(dst, src, n);
}

/* `(dst, n, c)` - the EABI order, and the only signature here that does not match the routine it
 * forwards to. */
void *
__aeabi_memset(void *dst, size_t n, int c)
{
	return memset(dst, c, n);
}

void *
__aeabi_memset4(void *dst, size_t n, int c)
{
	return memset(dst, c, n);
}

void *
__aeabi_memset8(void *dst, size_t n, int c)
{
	return memset(dst, c, n);
}

void *
__aeabi_memclr(void *dst, size_t n)
{
	return memset(dst, 0, n);
}

void *
__aeabi_memclr4(void *dst, size_t n)
{
	return memset(dst, 0, n);
}

void *
__aeabi_memclr8(void *dst, size_t n)
{
	return memset(dst, 0, n);
}
