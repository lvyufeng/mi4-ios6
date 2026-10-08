#ifndef MI4IOS6_BUILD_CONFIG_H
#define MI4IOS6_BUILD_CONFIG_H
/*
 * The declarations that XNU's build configuration supplies and the tarball does not, collected in
 * one place so the force-include list stays short and each one carries its reason.
 *
 * This is NOT a shim standing in for a missing header. It is the header the build would have made.
 */

/* osfmk/libsa/types.h defines this; a kernel build gets it from `sys/types.h`'s Darwin variant,
 * which is not the one the entry path can include (it collides with kern_types.h over clock_t).
 * osfmk/arm/status.c:730 casts a pid_t through it. */
typedef unsigned int uint_t;

/* bsd/sys/types.h:87-90 defines this with a _U_LONG guard; the BSD type fragments the entry path
 * includes (sys/_types/_u_int.h and friends) do not carry u_long, and the whole of sys/types.h
 * collides with kern_types.h. Copied verbatim, guard and all. */
#ifndef _U_LONG
typedef unsigned long u_long;
#define _U_LONG
#endif

/* `char *caddr_t`. bsd/sys/types.h:99 gets it from sys/_types/_caddr_t.h (guard `_CADDR_T`), and
 * osfmk/sys/types.h:131 and libsa/types.h:71 define it unguarded - but none of those three is on
 * the entry path, and Darwin-13's osfmk/arm/cpu_data.h:103 uses caddr_t (`cpu_onfault`). The modern
 * tree's cpu_data.h does not use it, so this is that tree's own gap and is inert for 4570. Guarded
 * with Darwin's own `_CADDR_T` so a later `#include <sys/_types/_caddr_t.h>` is a no-op, not a
 * redefinition. */
#ifndef _CADDR_T
typedef char *caddr_t;
#define _CADDR_T
#endif

/* The BSD-legacy integer spellings. bsd/sys/types.h and osfmk/sys/types.h both define these, but
 * neither is on the entry path, and Darwin-13's osfmk/arm/cpu.c:479 (`u_char type`) and
 * loose_ends.c:136 (`arc4_addrandom(u_char *dat, int)`) use them. 4570's ARM layer does not, so
 * this is Darwin-13's own gap and is inert for 4570. Guarded with the names Apple's `sys/types.h`
 * uses so a later real definition is not a redefinition. */
#ifndef _U_CHAR
typedef unsigned char u_char;
#define _U_CHAR
#endif
#ifndef _U_SHORT
typedef unsigned short u_short;
#define _U_SHORT
#endif

#endif
