#include <stdint.h>

/*
 * Inert link anchor for the Stage55 controlled host link proof. The linked
 * public-XNU subset is never entered from the bootable Stage55 payload.
 */
void stage55_xnu_link_noentry(void) __attribute__((section(".stage55_xnu_link_noentry")));
void stage55_xnu_link_noentry(void)
{
    __asm__ volatile ("nop" ::: "memory");
}
