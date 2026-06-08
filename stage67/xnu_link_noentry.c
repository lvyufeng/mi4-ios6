#include <stdint.h>

/*
 * Inert link anchor for the Stage67 controlled host link proof. The linked
 * public-XNU subset is never entered from the bootable Stage67 payload.
 */
void stage67_xnu_link_noentry(void) __attribute__((section(".stage67_xnu_link_noentry")));
void stage67_xnu_link_noentry(void)
{
    __asm__ volatile ("nop" ::: "memory");
}
