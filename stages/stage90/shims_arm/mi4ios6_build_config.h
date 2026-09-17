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

#endif
