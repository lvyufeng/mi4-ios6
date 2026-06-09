#include <stdint.h>

/*
 * Inert link anchor for the Stage71 controlled host link proof. The linked
 * public-XNU subset is never entered from the bootable Stage71 payload.
 */
void stage71_xnu_link_noentry(void) __attribute__((section(".stage71_xnu_link_noentry")));
void stage71_xnu_link_noentry(void)
{
    __asm__ volatile ("nop" ::: "memory");
}
