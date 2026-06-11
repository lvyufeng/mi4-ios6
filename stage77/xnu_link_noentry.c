#include <stdint.h>

/*
 * Inert link anchor for the Stage77 controlled host link proof. The linked
 * public-XNU subset is never entered from the bootable Stage77 payload.
 */
void stage77_xnu_link_noentry(void) __attribute__((section(".stage77_xnu_link_noentry")));
void stage77_xnu_link_noentry(void)
{
    __asm__ volatile ("nop" ::: "memory");
}
