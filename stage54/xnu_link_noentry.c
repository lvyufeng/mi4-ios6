#include <stdint.h>

/*
 * Inert link anchor for the Stage54 controlled host link proof. The linked
 * public-XNU subset is never entered from the bootable Stage54 payload.
 */
void stage54_xnu_link_noentry(void) __attribute__((section(".stage54_xnu_link_noentry")));
void stage54_xnu_link_noentry(void)
{
    __asm__ volatile ("nop" ::: "memory");
}
