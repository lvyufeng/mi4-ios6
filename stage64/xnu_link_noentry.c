#include <stdint.h>

/*
 * Inert link anchor for the Stage64 controlled host link proof. The linked
 * public-XNU subset is never entered from the bootable Stage64 payload.
 */
void stage64_xnu_link_noentry(void) __attribute__((section(".stage64_xnu_link_noentry")));
void stage64_xnu_link_noentry(void)
{
    __asm__ volatile ("nop" ::: "memory");
}
