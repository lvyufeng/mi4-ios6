#include <stdint.h>

/*
 * Inert link anchor for the Stage70 controlled host link proof. The linked
 * public-XNU subset is never entered from the bootable Stage70 payload.
 */
void stage70_xnu_link_noentry(void) __attribute__((section(".stage70_xnu_link_noentry")));
void stage70_xnu_link_noentry(void)
{
    __asm__ volatile ("nop" ::: "memory");
}
