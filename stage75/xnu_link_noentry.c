#include <stdint.h>

/*
 * Inert link anchor for the Stage75 controlled host link proof. The linked
 * public-XNU subset is never entered from the bootable Stage75 payload.
 */
void stage75_xnu_link_noentry(void) __attribute__((section(".stage75_xnu_link_noentry")));
void stage75_xnu_link_noentry(void)
{
    __asm__ volatile ("nop" ::: "memory");
}
