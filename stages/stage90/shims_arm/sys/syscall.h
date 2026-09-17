#ifndef MI4IOS6_SHIM_SYS_SYSCALL_H
#define MI4IOS6_SHIM_SYS_SYSCALL_H
/*
 * `sys/syscall.h` does not exist anywhere in the xnu-4570.1.46 tarball. osfmk/arm/bsd_arm.c
 * includes it at line 52; on x86 that header carries the `unix_syscall`/`mach_syscall` prototypes
 * the BSD syscall path needs.
 *
 * Declared rather than left out, because bsd_arm.c's `thread_setsinglestep` and its neighbours sit
 * on the path a syscall takes, and a missing prototype there would be a silent ABI assumption.
 * The signatures are the ones osfmk/kern/syscall_sw.h and the i386 tree agree on.
 */
#include <mach/mach_types.h>
#include <mach/kern_return.h>

extern int unix_syscall(void *regs);
extern int mach_syscall(void *regs);
extern void unix_syscall_return(int retval);

#endif
