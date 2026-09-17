/* Host build of MIG.
 *
 * MIG is a build tool that runs on the host, so it needs glibc's <sys/cdefs.h> for <stdlib.h>,
 * while the mach headers it reads need Darwin's cdefs macros. Including glibc's by absolute path
 * and then supplying the Darwin macros on top is what lets one translation unit have both; the
 * guard names differ (glibc's _SYS_CDEFS_H, Darwin's _SYS_CDEFS_H_), so Darwin's can still be
 * reached and is neutralised by the defines below rather than fought with.
 */
#include "/usr/include/x86_64-linux-gnu/sys/cdefs.h"
#ifndef __DARWIN_ALIAS
#define __DARWIN_ALIAS(sym) sym
#endif
#ifndef __DARWIN_ALIAS_C
#define __DARWIN_ALIAS_C(sym) sym
#endif
#ifndef __DARWIN_ALIAS_I
#define __DARWIN_ALIAS_I(sym) sym
#endif
#ifndef __DARWIN_EXTSN
#define __DARWIN_EXTSN(sym) sym
#endif
#ifndef __DARWIN_EXTSN_C
#define __DARWIN_EXTSN_C(sym) sym
#endif
#ifndef __DARWIN_LDBL_COMPAT
#define __DARWIN_LDBL_COMPAT(x)
#endif
#ifndef __DARWIN_ALIAS_STARTING
#define __DARWIN_ALIAS_STARTING(_mac,_iphone,x) x
#endif
#ifndef __DARWIN_ALIAS_STARTING_MAC_10_0
#define __DARWIN_ALIAS_STARTING_MAC_10_0(x) x
#endif
