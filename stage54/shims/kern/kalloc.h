#ifndef MI4IOS6_STAGE54_SHIM_KERN_KALLOC_H
#define MI4IOS6_STAGE54_SHIM_KERN_KALLOC_H

#include <mach/machine/vm_types.h>

void *kalloc(vm_size_t size);
void kfree(void *data, vm_size_t size);

#endif
