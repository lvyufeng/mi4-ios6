#ifndef MI4IOS6_STAGE75_SHIM_LIBKERN_OSATOMIC_H
#define MI4IOS6_STAGE75_SHIM_LIBKERN_OSATOMIC_H

#include <stdint.h>
#include <mach/boolean.h>

typedef uint64_t UInt64;
typedef boolean_t Boolean;

Boolean OSCompareAndSwap64(UInt64 oldValue, UInt64 newValue, volatile UInt64 *address);

#endif
