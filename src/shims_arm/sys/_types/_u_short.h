/* _u_short.h - stands in for the modern tree's `bsd/sys/_types/_u_short.h` (913).
 *
 * Same class as `_u_char.h`: the 2013-era Darwin-13 (iOS 7) tree predates the `_types/` split and
 * ships no such file. Guard is Darwin's own `_U_SHORT`, body is the one-line typedef, so on 4570
 * (which has its own copy, reached through the BSD `<sys/types.h>` chain) this is never used and on
 * Darwin-13 it resolves the `u_short` uses off the entry path. `mi4ios6_build_config.h` defines the
 * same typedef under the same guard.
 */
#ifndef _U_SHORT
#define _U_SHORT
typedef	unsigned short 	u_short;
#endif /* _U_SHORT */