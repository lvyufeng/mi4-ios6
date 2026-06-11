#include <stdint.h>

/*
 * Inert link anchor for the Stage76 controlled host link proof. The linked
 * public-XNU subset is never entered from the bootable Stage76 payload.
 */
void stage76_xnu_link_noentry(void) __attribute__((section(".stage76_xnu_link_noentry")));
void stage76_xnu_link_noentry(void)
{
    __asm__ volatile ("nop" ::: "memory");
}
