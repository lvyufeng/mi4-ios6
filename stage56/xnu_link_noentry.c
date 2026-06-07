#include <stdint.h>

/*
 * Inert link anchor for the Stage56 controlled host link proof. The linked
 * public-XNU subset is never entered from the bootable Stage56 payload.
 */
void stage56_xnu_link_noentry(void) __attribute__((section(".stage56_xnu_link_noentry")));
void stage56_xnu_link_noentry(void)
{
    __asm__ volatile ("nop" ::: "memory");
}
