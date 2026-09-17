/* Host build of MIG.
 *
 * type.h does `#ifdef linux / #include <linux/types.h> / #else / <sys/types.h>`. On a Linux host
 * that first branch is taken, and glibc's linux/types.h defines __u32 and friends but not the BSD
 * u_int that type.h goes on to use. So this shim supplies both: the host's sys/types.h aliases via
 * the shim next door, and the real linux/types.h by absolute path.
 *
 * Recorded because the failure was misleading - u_int unknown immediately after an include that
 * looks like it should have provided it, on a branch that only exists on this host. */
#include <sys/types.h>
#include "/usr/include/linux/types.h"
