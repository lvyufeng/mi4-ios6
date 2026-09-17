#ifndef MI4IOS6_SHIM_SYSTEM_MACH_clock_types_H
#define MI4IOS6_SHIM_SYSTEM_MACH_clock_types_H
/*
 * Path shim. The XNU sources include <System/mach/clock_types.h>, which in a real build is the SDK's
 * System framework; the header itself is the ordinary osfmk/mach/clock_types.h. Same shape as the
 * mach_debug.h redirect next door - one definition, reached by the second name.
 */
#include <mach/clock_types.h>
#endif
