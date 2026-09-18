#ifndef MI4IOS6_SHIM_STRING_H
#define MI4IOS6_SHIM_STRING_H
/*
 * XNU's kernel sources include <string.h> for the memory- and string-function declarations,
 * which the kernel build provides from libkern. There is no such header for a bare-metal ARM
 * target on this host, and pointing at newlib's is worse than not having one: newlib's
 * sys/reent.h defines _mbstate_t and _off_t, which collide with the Darwin types XNU's own
 * headers bring in.
 *
 * Declarations only. This is the same approach shims/libkern/OSAtomic.h already takes for the
 * atomic layer, and for the same reason: the kernel supplies these, not libc.
 */
/*
 * `size_t` from the compiler rather than from <stddef.h>, and that is evidence rather than taste.
 * `libkern/zlib/zutil.h:193-194` is
 *
 *     #if KERNEL
 *         typedef long ptrdiff_t;
 *
 * in a file Apple compiles into the ARM kernel, so at that point in Apple's build `ptrdiff_t` is
 * NOT defined. `EXTERNAL_HEADERS/stddef.h:31` defines it (as `int` on this target), and including
 * <stddef.h> here is what dragged it in: eight files in libkern/zlib failed with
 * "typedef redefinition with different types ('long' vs ... 'int')". So Apple's kernel <string.h>
 * does not include stddef.h, and this one must not either.
 *
 * The compiler builtin gives the same type stddef.h would have - the type of a `sizeof` - without
 * defining anything else along the way.
 */
/* `_SIZE_T` is claimed for the same reason stddef.h and osfmk/libsa/types.h claim it: whichever
 * definition comes first wins, and this one is the compiler's own. libsa/types.h:52-54 says
 * `unsigned long size_t` - written for a Darwin target, where that is what __SIZE_TYPE__ is - and
 * on `--target=armv7-none-eabi` the compiler says `unsigned int`. Left to race, one of the two
 * loses and four files fail on the redefinition; claiming the guard makes ours the definition.
 * (On `--target=armv7-apple-darwin` the two agree and this is inert.) */
#ifndef _SIZE_T
#define _SIZE_T
typedef __SIZE_TYPE__ size_t;
#endif
/* stddef.h also carries NULL, and plenty of files were getting it from here by accident - removing
 * the include without this took the build from 381 of 419 to 0, on `return NULL` in
 * osfmk/kern/kcdata.h. Both are the compiler's own definitions, so this adds nothing new. */
#ifndef NULL
#ifdef __cplusplus
/* `((void *)0)` is not a null pointer constant in C++ - `void *` does not implicitly convert to
 * another pointer type - so the C spelling makes every `return NULL` in a `.cpp` an error. `0` is
 * what C++ wants and what the compiler's own `<stddef.h>` uses there. Found by attempting the
 * kernel's C++ files, which are the last unbuilt block: `osfmk/kern/kcdata.h:1119` is
 * `return NULL;` in a `char *` function. */
#define NULL 0
#else
#define NULL ((void *)0)
#endif
#endif

/* The declarations are C declarations and must say so. Without this wrapper a C++ translation unit
 * gives every one of them **C++ linkage**, so the reference it emits is the mangled name —
 * `bzero(void*, unsigned int)` — and the kernel's own `bzero` (a C symbol, from `osfmk/arm/bzero.s`)
 * no longer matches it. Measured at the link: the C++ objects added 14 undefined symbols whose names
 * are signatures, `bzero`, `bcopy`, `bcmp`, `memcpy`, `memmove`, `memset`, `memcmp`, `strlen`,
 * `strnlen`, `strcmp`, `strncmp`, `strncpy`, `strcpy`, `strchr`, `strlcpy`, `strlcat` — every
 * function below, and nothing else. Apple's own kernel `<string.h>` is reached through
 * `__BEGIN_DECLS` for the same reason; this is that, spelled out, since this header shim has no
 * `<sys/cdefs.h>` guarantee to lean on at this point in the include order. */
#ifdef __cplusplus
extern "C" {
#endif

void *memcpy(void *dst, const void *src, size_t n);
void *memmove(void *dst, const void *src, size_t n);
void *memset(void *dst, int c, size_t n);
int memcmp(const void *a, const void *b, size_t n);

size_t strlen(const char *s);
size_t strnlen(const char *s, size_t n);
int strcmp(const char *a, const char *b);
int strncmp(const char *a, const char *b, size_t n);
char *strncpy(char *dst, const char *src, size_t n);
char *strcpy(char *dst, const char *src);
char *strchr(const char *s, int c);
/* `strncat` is here because `san/memintrinsics.h`'s `__nosan_strncat` inline body calls it and this
 * header is where that header's other twelve names come from - it was the one name missing from
 * this list, which shows up as "incompatible integer to pointer conversion" inside the body of an
 * inline function nobody calls. Apple's own declaration, `osfmk/libsa/string.h:77`. */
char *strncat(char *dst, const char *src, size_t n);

/*
 * The five BSD-legacy names, and why this header is where they are missing rather than somewhere
 * else. `libkern/c++/*.cpp` writes `bcopy`, `bzero`, `bcmp`, `strlcpy` and `strlcat`
 * (OSSymbol.cpp:123, OSString.cpp:78, OSUnserializeXML.y:964 and their neighbours) and declares
 * three of them by including `<string.h>` - which in this build resolves to *this file*, because
 * `EXTERNAL_HEADERS/` ships stdarg, stdatomic, stdbool, stddef and stdint and **no string.h at
 * all**. So this shim is the kernel's `<string.h>` here, and it was four functions short.
 *
 * The declarations are Apple's own, transcribed from `osfmk/libsa/string.h:72,73,93,94,95` - the
 * one place in the tree that declares them for a kernel context. That directory is deliberately
 * NOT on the include path (it holds a `string.h`, a `stdlib.h` and a `sys/` that shadow the real
 * ones - four files, experiment-117), so these three lines are the exports that cost the least.
 *
 * What they were worth, measured with `build_xnu_arm_kernel.sh` on the libkern component alone:
 * 22 `.cpp` attempted, **7 compiled before this and 19 after**, with `bzero` (33 errors), `bcopy`
 * (22), `strlcpy` (8), `strlcat` (5) and `bcmp` (1) gone from the blocker list. `strchr` came with
 * them for the same reason and the same place.
 */
size_t strlcpy(char *dst, const char *src, size_t n);
size_t strlcat(char *dst, const char *src, size_t n);
int bcmp(const void *a, const void *b, size_t n);
void bcopy(const void *src, void *dst, size_t n);
void bzero(void *dst, size_t n);

#ifdef __cplusplus
}
#endif

/*
 * The block Apple's kernel `<string.h>` carries and this one did not, and it was missing the same
 * way the five BSD-legacy names above were: `osfmk/libsa/string.h:97-99` is
 *
 *     #ifdef PRIVATE
 *     #include <san/memintrinsics.h>
 *     #endif
 *
 * right after its own `bzero`/`bcopy`/`bcmp` declarations — which is where `__nosan_bzero`,
 * `__nosan_strncpy` and their eleven neighbours are declared, as `static inline`. Nothing else in
 * `osfmk`, `bsd`, `libkern`, `iokit`, `pexpert` or `security` includes that header, so in Apple's
 * build **this file is where those names come from**.
 *
 * What it cost to be without it: `osfmk/kern/zalloc.c:561` calls `__nosan_bzero` and `:4030` calls
 * `__nosan_strncpy`, and with no declaration visible clang emitted an implicit-declaration reference
 * to an external symbol that nothing defines — the tree's only definition is the `static inline`
 * in the header. The symbol then turns up in the link as `__nosan_bzero`, 7 call-graph edges from
 * `arm_init` (`get_zone_page_metadata <- zcram <- vm_map_init <- vm_mem_bootstrap <-
 * kernel_bootstrap`), and `stub_blockers.py` filed it as "a file not in the manifest" — which is
 * the wrong diagnosis of the right symptom, since the file is in the tarball and the manifest is
 * not what was missing (experiment-165 measured 0 occurrences of `san/memintrinsics` in the
 * preprocessed translation unit).
 *
 * **The `#ifdef PRIVATE` guard is deliberately dropped.** It exists in Apple's file to keep the
 * header's contents out of a non-private export, and here `PRIVATE=1` is one of the build's global
 * defines so the two would behave identically today — but "is this symbol declared" is not a
 * question a per-component define should be able to answer differently for two files that link
 * against each other, and that is the whole reason this project has a fixed define table per
 * component. The include is unconditional, and every name in the header is `static inline`, so a
 * translation unit that does not use them emits nothing.
 */
#include <san/memintrinsics.h>

#endif
