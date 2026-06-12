#include <stdint.h>

/*
 * Inert link anchor for the Stage79 controlled host link proof. The linked
 * public-XNU subset is never entered from the bootable Stage79 payload.
 */
void stage79_xnu_link_noentry(void) __attribute__((section(".stage79_xnu_link_noentry")));
void stage79_xnu_link_noentry(void)
{
    __asm__ volatile ("nop" ::: "memory");
}
