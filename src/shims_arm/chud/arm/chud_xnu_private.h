#ifndef MI4IOS6_SHIM_CHUD_ARM_CHUD_XNU_PRIVATE_H
#define MI4IOS6_SHIM_CHUD_ARM_CHUD_XNU_PRIVATE_H
/*
 * `chud/arm/chud_xnu_private.h` does not exist in the tarball; `chud/i386/` does, and
 * osfmk/chud/chud_xnu_private.h picks between them on `__arm__`. So the *arm* half of a component
 * that is otherwise present is missing.
 *
 * Written from the i386 file rather than guessed: same declarations, minus the x86-specific state.
 * What the ARM entry path actually needs from it is checked rather than assumed —
 * osfmk/arm/cpu_common.c:411 is the only use in this layer, and it needs
 * `chudxnu_cpu_signal_handler` and nothing else. The `chudcpu_data_t` struct is deliberately NOT
 * declared here: ARM's cpu_data has a single `void *cpu_chud` (cpu_data_internal.h:185) where
 * i386's has the full struct inline, so copying it would be inventing a layout.
 */
#include <stdint.h>
#include <mach/boolean.h>
#include <kern/thread_call.h>

extern void chudxnu_cpu_signal_handler(void);

#endif
