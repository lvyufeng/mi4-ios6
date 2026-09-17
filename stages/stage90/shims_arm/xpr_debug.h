#ifndef MI4IOS6_SHIM_XPR_DEBUG_H
#define MI4IOS6_SHIM_XPR_DEBUG_H
/*
 * `xpr_debug.h` is build-generated and absent. Unlike the other generated headers here it cannot
 * be avoided: osfmk/kern/xpr.h:83 is `#ifdef MACH_KERNEL / #include <xpr_debug.h> / #else /
 * #include <sys/features.h>`, so with MACH_KERNEL defined — which the rest of this configuration
 * requires — the include is unconditional, and the alternative branch wants a userland header that
 * is equally absent.
 *
 * Empty, and checked rather than assumed: everything xpr.h needs from it, it declares itself. The
 * `XPR_*` flag names are defined at xpr.h:106-118, immediately after this include, and `xprflags`
 * at :100 — so this header contributes the generated *data* list, not any type the compiler needs.
 * pmap.c and locks_arm.c are the two files in this layer that reach it, via `#include
 * <kern/xpr.h>`.
 */
#endif
