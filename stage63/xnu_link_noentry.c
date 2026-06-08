#include <stdint.h>

/*
 * Inert link anchor for the Stage63 controlled host link proof. The linked
 * public-XNU subset is never entered from the bootable Stage63 payload.
 */
void stage63_xnu_link_noentry(void) __attribute__((section(".stage63_xnu_link_noentry")));
void stage63_xnu_link_noentry(void)
{
    __asm__ volatile ("nop" ::: "memory");
}
