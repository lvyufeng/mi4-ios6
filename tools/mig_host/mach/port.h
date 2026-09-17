#ifndef _MACH_PORT_H_
#define _MACH_PORT_H_
#include <mach/boolean.h>
#include <mach/machine/vm_types.h>
#define MACH_PORT_NULL ((mach_port_t) 0)
#define MACH_PORT_DEAD ((mach_port_t) 0xffffffffu)
typedef unsigned int mach_port_right_t;
typedef unsigned int mach_port_type_t;
typedef unsigned int mach_port_urefs_t;
typedef unsigned int mach_port_msgcount_t;
typedef unsigned int mach_port_mscount_t;
typedef unsigned int mach_port_seqno_t;
typedef unsigned int mach_port_delta_t;
typedef unsigned int mach_port_context_t;
#endif
