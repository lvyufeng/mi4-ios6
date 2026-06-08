#include <stdint.h>

/*
 * Inert link anchor for the Stage62 controlled host link proof. The linked
 * public-XNU subset is never entered from the bootable Stage62 payload.
 */
void stage62_xnu_link_noentry(void) __attribute__((section(".stage62_xnu_link_noentry")));
void stage62_xnu_link_noentry(void)
{
    __asm__ volatile ("nop" ::: "memory");
}
