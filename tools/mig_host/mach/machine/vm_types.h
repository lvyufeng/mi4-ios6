#ifndef _MACH_MACHINE_VM_TYPES_H_
#define _MACH_MACHINE_VM_TYPES_H_
#include <stdint.h>
typedef unsigned int natural_t;
typedef int          integer_t;
typedef unsigned int vm_offset_t;
typedef unsigned int vm_size_t;
typedef unsigned int vm_address_t;
typedef uint64_t     mach_vm_address_t;
typedef uint64_t     mach_vm_size_t;
typedef uint64_t     mach_vm_offset_t;
typedef unsigned int mach_port_t;
typedef unsigned int mach_port_name_t;
#define VM_MIN_ADDRESS ((vm_address_t) 0x00000000u)
#endif
