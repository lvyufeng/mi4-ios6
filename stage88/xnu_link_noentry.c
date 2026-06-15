#include <stdint.h>

/*
 * Inert link anchor for the Stage84 controlled host link proof. The linked
 * public-XNU subset is never entered from the bootable Stage84 payload.
 */
void stage88_xnu_link_noentry(void) __attribute__((section(".stage88_xnu_link_noentry")));
void stage88_xnu_link_noentry(void)
{
    __asm__ volatile ("nop" ::: "memory");
}
