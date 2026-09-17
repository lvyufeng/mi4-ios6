#ifndef MI4IOS6_SHIM_MACH_DEBUG_H
#define MI4IOS6_SHIM_MACH_DEBUG_H
/*
 * Path shim. The real header is at osfmk/mach_debug/mach_debug.h - it exists in the tarball, but
 * XNU's sources include it as <mach_debug.h>, which only resolves because the kernel build adds
 * osfmk/mach_debug to its include path. That path is one of the things the build configuration
 * supplies and the tarball does not.
 *
 * A redirect rather than a copy: one definition, which is the rule this project has had to learn
 * four separate times.
 */
#include <mach_debug/mach_debug.h>
#endif
