#include <stdint.h>

/*
 * Inert link anchor for the Stage53 controlled host link proof. The linked
 * public-XNU subset is never entered from the bootable Stage53 payload.
 */
void stage53_xnu_link_noentry(void) __attribute__((section(".stage53_xnu_link_noentry")));
void stage53_xnu_link_noentry(void)
{
    __asm__ volatile ("nop" ::: "memory");
}
