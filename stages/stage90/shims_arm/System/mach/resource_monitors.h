#ifndef MI4IOS6_SHIM_SYSTEM_MACH_resource_monitors_H
#define MI4IOS6_SHIM_SYSTEM_MACH_resource_monitors_H
/*
 * Path shim. The XNU sources include <System/mach/resource_monitors.h>, which in a real build is the SDK's
 * System framework; the header itself is the ordinary osfmk/mach/resource_monitors.h. Same shape as the
 * mach_debug.h redirect next door - one definition, reached by the second name.
 */
#include <mach/resource_monitors.h>
#endif
