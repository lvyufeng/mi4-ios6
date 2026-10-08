/* _u_char.h - stands in for the modern tree's `bsd/sys/_types/_u_char.h` (913).
 *
 * The kernel build force-includes this (tools/build_xnu_arm_kernel.sh): `osfmk/kern/btlog.c:641`
 * casts to `u_char` and reaches no header that defines it, on either tree. The 2013-era Darwin-13
 * (iOS 7) tree predates the `_types/` split and ships no such file. The guard is Darwin's own
 * `_U_CHAR` and the body is the one-line typedef, so `mi4ios6_build_config.h` (also force-included)
 * defining `u_char` under the same guard makes this a no-op, and on 4570 it is never reached.
 */
#ifndef _U_CHAR
#define _U_CHAR
typedef	unsigned char 	u_char;
#endif /* _U_CHAR */