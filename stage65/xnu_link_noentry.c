#include <stdint.h>

/*
 * Inert link anchor for the Stage65 controlled host link proof. The linked
 * public-XNU subset is never entered from the bootable Stage65 payload.
 */
void stage65_xnu_link_noentry(void) __attribute__((section(".stage65_xnu_link_noentry")));
void stage65_xnu_link_noentry(void)
{
    __asm__ volatile ("nop" ::: "memory");
}
