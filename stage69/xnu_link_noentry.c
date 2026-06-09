#include <stdint.h>

/*
 * Inert link anchor for the Stage69 controlled host link proof. The linked
 * public-XNU subset is never entered from the bootable Stage69 payload.
 */
void stage69_xnu_link_noentry(void) __attribute__((section(".stage69_xnu_link_noentry")));
void stage69_xnu_link_noentry(void)
{
    __asm__ volatile ("nop" ::: "memory");
}
