#include <stdint.h>

/*
 * Inert link anchor for the Stage60 controlled host link proof. The linked
 * public-XNU subset is never entered from the bootable Stage60 payload.
 */
void stage60_xnu_link_noentry(void) __attribute__((section(".stage60_xnu_link_noentry")));
void stage60_xnu_link_noentry(void)
{
    __asm__ volatile ("nop" ::: "memory");
}
