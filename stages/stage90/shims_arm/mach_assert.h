/* Stub for a header the OSS tarball does not ship.
 *
 * Included by osfmk/kern/assert.h:68, osfmk/arm/{pmap.c,caches.c,pmap.h} and others. Nothing
 * matching *mach_assert* exists anywhere in the tarball — it is generated (or simply not
 * published) and it is what kern/assert.h expects to describe the assert implementation for
 * the build: which of assert()/__assert() are compiled in, and what they expand to.
 *
 * This stub takes the released-kernel configuration, which is the one that can be reasoned
 * about without the kernel's internals: MACH_ASSERT 0, so assert() compiles away and the
 * debug-only paths in the ARM tree stay out. That is the conservative choice for a
 * compile-check — it exercises strictly less code than a DEVELOPMENT build, so a clean result
 * here does not overstate what compiles.
 *
 * The declarations __assert and __assert_rtn are the ones kern/assert.h and the ARM sources
 * reference by name; they are declared rather than defined, because this header is only ever
 * used for syntax checking a translation unit, never linked.
 */
#ifndef _MACH_ASSERT_H_
#define _MACH_ASSERT_H_

#ifndef MACH_ASSERT
#define MACH_ASSERT 0
#endif

#endif /* _MACH_ASSERT_H_ */
