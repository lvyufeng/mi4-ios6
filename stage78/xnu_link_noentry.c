#include <stdint.h>

/*
 * Inert link anchor for the Stage78 controlled host link proof. The linked
 * public-XNU subset is never entered from the bootable Stage78 payload.
 */
void stage78_xnu_link_noentry(void) __attribute__((section(".stage78_xnu_link_noentry")));
void stage78_xnu_link_noentry(void)
{
    __asm__ volatile ("nop" ::: "memory");
}
