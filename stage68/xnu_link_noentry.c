#include <stdint.h>

/*
 * Inert link anchor for the Stage68 controlled host link proof. The linked
 * public-XNU subset is never entered from the bootable Stage68 payload.
 */
void stage68_xnu_link_noentry(void) __attribute__((section(".stage68_xnu_link_noentry")));
void stage68_xnu_link_noentry(void)
{
    __asm__ volatile ("nop" ::: "memory");
}
