#include <stdint.h>

/*
 * Inert link anchor for the Stage58 controlled host link proof. The linked
 * public-XNU subset is never entered from the bootable Stage58 payload.
 */
void stage58_xnu_link_noentry(void) __attribute__((section(".stage58_xnu_link_noentry")));
void stage58_xnu_link_noentry(void)
{
    __asm__ volatile ("nop" ::: "memory");
}
