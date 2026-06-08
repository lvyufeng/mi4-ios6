#include <stdint.h>

/*
 * Inert link anchor for the Stage59 controlled host link proof. The linked
 * public-XNU subset is never entered from the bootable Stage59 payload.
 */
void stage59_xnu_link_noentry(void) __attribute__((section(".stage59_xnu_link_noentry")));
void stage59_xnu_link_noentry(void)
{
    __asm__ volatile ("nop" ::: "memory");
}
