#include <stdint.h>

/*
 * Inert link anchor for the Stage57 controlled host link proof. The linked
 * public-XNU subset is never entered from the bootable Stage57 payload.
 */
void stage57_xnu_link_noentry(void) __attribute__((section(".stage57_xnu_link_noentry")));
void stage57_xnu_link_noentry(void)
{
    __asm__ volatile ("nop" ::: "memory");
}
