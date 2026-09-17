#ifndef MI4IOS6_SHIM_MACH_KDP_H
#define MI4IOS6_SHIM_MACH_KDP_H
/*
 * mach_kdp.h is absent from the xnu-4570.1.46 tarball. osfmk/arm/start.s and locore.s include it
 * for the KDP (kernel debugging protocol) entry points they do not call on the paths this project
 * assembles. Empty on purpose: a name that resolves to nothing is better than a guess at a
 * signature nobody here calls. If a future path does call one, the compiler says so.
 */
#endif
