#include <stdint.h>

/*
 * Inert link anchor for the Stage82 controlled host link proof. The linked
 * public-XNU subset is never entered from the bootable Stage82 payload.
 */
void stage82_xnu_link_noentry(void) __attribute__((section(".stage82_xnu_link_noentry")));
void stage82_xnu_link_noentry(void)
{
    __asm__ volatile ("nop" ::: "memory");
}
