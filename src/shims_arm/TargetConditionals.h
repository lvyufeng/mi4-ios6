#ifndef MI4IOS6_SHIM_TARGET_CONDITIONALS_H
#define MI4IOS6_SHIM_TARGET_CONDITIONALS_H
/*
 * TargetConditionals.h is an SDK header (part of the macOS/iOS SDK) and is absent from the kernel
 * tarball. osfmk/kern/debug.h includes it; it is reached here only because MACH_KERNEL_PRIVATE
 * pulls misc_protos.h in, which the real build does too.
 *
 * All macros 0: this is not an Apple platform, and every branch guarded by one of these describes
 * an SDK-targeted build rather than a kernel build.
 */
#define TARGET_OS_MAC 0
#define TARGET_OS_OSX 0
#define TARGET_OS_IPHONE 0
#define TARGET_OS_SIMULATOR 0
#define TARGET_OS_EMBEDDED 0
#define TARGET_CPU_ARM 1
#endif
