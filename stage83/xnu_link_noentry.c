#include <stdint.h>

/*
 * Inert link anchor for the Stage83 controlled host link proof. The linked
 * public-XNU subset is never entered from the bootable Stage83 payload.
 */
void stage83_xnu_link_noentry(void) __attribute__((section(".stage83_xnu_link_noentry")));
void stage83_xnu_link_noentry(void)
{
    __asm__ volatile ("nop" ::: "memory");
}
