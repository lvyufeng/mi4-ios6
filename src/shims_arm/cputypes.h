#ifndef _MI4_COMPAT_CPUTYPES_H_
#define _MI4_COMPAT_CPUTYPES_H_

/*
 * WHY THIS SHIM EXISTS (913).  Apple's config tool writes this file and `platforms.h` is a SYMLINK
 * to it (`osfmk/conf/Makefile:45-50`); `build_cputypes` (`SETUP/config/mkmakefile.c:823-828`) emits
 * `#define <cpu> 1` for each `-cpu` the configuration declares.  For an ARM configuration that is
 * `#define arm 1`.  Two distinct includers reach it directly — `kern/thread.h:103` says
 * `<cputypes.h>` and `kern/ast.h:67` says `<platforms.h>` — so the shim set needs both names, the
 * way Apple's build has both names.  `platforms.h` includes this file so the two cannot drift.
 *
 * The modern tree (xnu-4570) does not need this file: nothing there includes either name (see
 * `platforms.h`).  It is inert for 4570 and load-bearing for Darwin-13.
 */
#define arm 1

#endif /* _MI4_COMPAT_CPUTYPES_H_ */