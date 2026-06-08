#ifndef MI4IOS6_STAGE64_SHIM_MACHINE_MACHINE_ROUTINES_H
#define MI4IOS6_STAGE64_SHIM_MACHINE_MACHINE_ROUTINES_H

#include <mach/vm_types.h>

typedef vm_address_t vm_map_address_t;

vm_map_address_t ml_map_high_window(vm_offset_t phys_addr, vm_size_t len);

#endif
