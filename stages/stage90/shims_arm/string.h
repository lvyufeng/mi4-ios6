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

#endif
