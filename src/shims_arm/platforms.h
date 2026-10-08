#ifndef _MI4_COMPAT_PLATFORMS_H_
#define _MI4_COMPAT_PLATFORMS_H_

/*
 * WHY THIS SHIM EXISTS (913).  Apple's config tool generates two headers this project never had to
 * before, and they are the same header under two names:
 *
 *   `SETUP/config/mkmakefile.c:257`  ->  `do_build("cputypes.h", build_cputypes);`
 *   `build_cputypes` (:823-828)          walks the `-cpu` list and writes `#define <name> 1` for each
 *   `osfmk/conf/Makefile:45-50`          `platforms.h:  ${LN} cputypes.h $@   (a SYMLINK)
 *
 * so `platforms.h` is `#define arm 1` for an ARM configuration (`doconf -cpu arm`).  `doconf` is a
 * 2013-era csh/awk toolchain this harness does not run, and nothing else in the tree ships the file
 * — `kern/ast.h:67` includes `<platforms.h>` unconditionally, and the include is reached through the
 * force-included `kern/ast.h`, so 40 of 40 in the Darwin-13 ARM layer fail on it without this.
 *
 * The modern tree (xnu-4570) does not need it: no file there includes `<platforms.h>` (the name
 * survives only in a `bsd/man` page and `kern/timer.h`'s comments), and the one place the bare `arm`
 * macro would matter is spelled `defined(__arm__)` instead.  So this header is inert for 4570 and
 * load-bearing for Darwin-13 — a path adapter, not a re-declaration.
 *
 * `arm` is safe as a macro name here — a bare `arm` token never appears as an identifier or in a
 * non-preprocessor context anywhere under either `osfmk/arm/` (checked), only as `arm` in `#if`.
 *
 * The value lives in `cputypes.h`, which is the file this one is a symlink to in Apple's build; the
 * include keeps the two spellings from drifting.
 */
#include <cputypes.h>

#endif /* _MI4_COMPAT_PLATFORMS_H_ */