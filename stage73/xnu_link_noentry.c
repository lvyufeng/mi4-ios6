#include <stdint.h>

/*
 * Inert link anchor for the Stage73 controlled host link proof. The linked
 * public-XNU subset is never entered from the bootable Stage73 payload.
 */
void stage73_xnu_link_noentry(void) __attribute__((section(".stage73_xnu_link_noentry")));
void stage73_xnu_link_noentry(void)
{
    __asm__ volatile ("nop" ::: "memory");
}
