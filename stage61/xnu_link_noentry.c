#include <stdint.h>

/*
 * Inert link anchor for the Stage61 controlled host link proof. The linked
 * public-XNU subset is never entered from the bootable Stage61 payload.
 */
void stage61_xnu_link_noentry(void) __attribute__((section(".stage61_xnu_link_noentry")));
void stage61_xnu_link_noentry(void)
{
    __asm__ volatile ("nop" ::: "memory");
}
