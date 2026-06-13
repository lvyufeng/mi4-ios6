#include <stdint.h>

/*
 * Inert link anchor for the Stage80 controlled host link proof. The linked
 * public-XNU subset is never entered from the bootable Stage80 payload.
 */
void stage80_xnu_link_noentry(void) __attribute__((section(".stage80_xnu_link_noentry")));
void stage80_xnu_link_noentry(void)
{
    __asm__ volatile ("nop" ::: "memory");
}
