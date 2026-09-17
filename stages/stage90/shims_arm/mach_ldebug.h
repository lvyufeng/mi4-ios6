#ifndef MI4IOS6_SHIM_MACH_LDEBUG_H
#define MI4IOS6_SHIM_MACH_LDEBUG_H
/*
 * mach_ldebug.h is absent from the xnu-4570.1.46 tarball - nothing matching *ldebug* exists in it
 * - and 20 of the 32 .c files in osfmk/arm reach it, mostly through osfmk/kern/thread.h:104.
 *
 * It exists to declare the lock-debugging hooks (pal_mlock/pal_munlock-style) that the kernel's
 * LOCK_DEBUG build turns on. All no-ops here, which is the released-kernel configuration and
 * exercises less code rather than more - the same reasoning as MACH_ASSERT 0 in mach_assert.h and
 * CONFIG_DTRACE 0 in config_dtrace.h.
 */
#define MACH_LDEBUG_H 1
#define ldebug(x) do { } while (0)
#define ldebug_mlock(x) do { } while (0)
#define ldebug_munlock(x) do { } while (0)
#define ldebug_lock_panic(x) do { } while (0)
#endif
