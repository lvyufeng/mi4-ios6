#include <stdint.h>

/*
 * Inert link anchor for the Stage81 controlled host link proof. The linked
 * public-XNU subset is never entered from the bootable Stage81 payload.
 */
void stage81_xnu_link_noentry(void) __attribute__((section(".stage81_xnu_link_noentry")));
void stage81_xnu_link_noentry(void)
{
    __asm__ volatile ("nop" ::: "memory");
}
