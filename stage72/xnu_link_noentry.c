#include <stdint.h>

/*
 * Inert link anchor for the Stage72 controlled host link proof. The linked
 * public-XNU subset is never entered from the bootable Stage72 payload.
 */
void stage72_xnu_link_noentry(void) __attribute__((section(".stage72_xnu_link_noentry")));
void stage72_xnu_link_noentry(void)
{
    __asm__ volatile ("nop" ::: "memory");
}
