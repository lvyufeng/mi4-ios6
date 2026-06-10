#include <stdint.h>

/*
 * Inert link anchor for the Stage74 controlled host link proof. The linked
 * public-XNU subset is never entered from the bootable Stage74 payload.
 */
void stage74_xnu_link_noentry(void) __attribute__((section(".stage74_xnu_link_noentry")));
void stage74_xnu_link_noentry(void)
{
    __asm__ volatile ("nop" ::: "memory");
}
