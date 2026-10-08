/* _caddr_t.h - stands in for the modern tree's `bsd/sys/_types/_caddr_t.h` (913).
 *
 * The kernel build force-includes `sys/_types/_caddr_t.h` (tools/build_xnu_arm_kernel.sh), which the
 * modern tree (4570) ships but the 2013-era Darwin-13 (iOS 7) tree does not - it predates the split
 * of `caddr_t` out of `sys/types.h` into a `_types/` fragment. The guard is Darwin's own `_CADDR_T`
 * and the body is the one-line typedef, so this is a *path* adapter, not a declaration of our own:
 * if `mi4ios6_build_config.h` (also force-included) has already defined `caddr_t` under the same
 * guard, this file expands to nothing, and on 4570 it is never reached at all.
 *
 * It matters off the entry path: `bsd/sys/types.h:99` reaches `caddr_t` through this header, and
 * several BSD files this configuration compiles use the type.
 */
#ifndef _CADDR_T
#define _CADDR_T
typedef	char *		caddr_t;
#endif /* _CADDR_T */