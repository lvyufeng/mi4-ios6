#include <stdint.h>

/*
 * Inert link anchor for the Stage66 controlled host link proof. The linked
 * public-XNU subset is never entered from the bootable Stage66 payload.
 */
void stage66_xnu_link_noentry(void) __attribute__((section(".stage66_xnu_link_noentry")));
void stage66_xnu_link_noentry(void)
{
    __asm__ volatile ("nop" ::: "memory");
}
