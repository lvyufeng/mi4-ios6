/* The parts of mach/message.h MIG's own code uses. Constant values copied verbatim from
 * osfmk/mach/message.h in the 4570 tree (lines 233-271, 661-692). */
#ifndef _MACH_MESSAGE_H_
#define _MACH_MESSAGE_H_
#include <stdint.h>
#include <mach/boolean.h>
#include <mach/kern_return.h>
#include <mach/port.h>
#include <mach/machine/vm_types.h>

typedef unsigned int mach_msg_size_t;
typedef natural_t    mach_msg_type_number_t;
typedef natural_t    mach_msg_timeout_t;
typedef int          mach_msg_option_t;
typedef unsigned int mach_msg_type_name_t;
typedef unsigned int mach_msg_bits_t;
typedef unsigned int mach_msg_id_t;
typedef unsigned int mach_msg_priority_t;
typedef unsigned int mach_msg_descriptor_type_t;

typedef struct {
    mach_msg_bits_t    msgh_bits;
    mach_msg_size_t    msgh_size;
    mach_port_t        msgh_remote_port;
    mach_port_t        msgh_local_port;
    mach_port_name_t   msgh_voucher_port;
    mach_msg_id_t      msgh_id;
} mach_msg_header_t;

#define MACH_MSG_TYPE_MOVE_RECEIVE      16
#define MACH_MSG_TYPE_MOVE_SEND         17
#define MACH_MSG_TYPE_MOVE_SEND_ONCE    18
#define MACH_MSG_TYPE_COPY_SEND         19
#define MACH_MSG_TYPE_MAKE_SEND         20
#define MACH_MSG_TYPE_MAKE_SEND_ONCE    21
#define MACH_MSG_TYPE_COPY_RECEIVE      22
#define MACH_MSG_TYPE_DISPOSE_RECEIVE   24
#define MACH_MSG_TYPE_DISPOSE_SEND      25
#define MACH_MSG_TYPE_DISPOSE_SEND_ONCE 26
#define MACH_MSG_TYPE_PORT_NONE         0
#define MACH_MSG_TYPE_PORT_NAME         15
#define MACH_MSG_TYPE_PORT_RECEIVE      MACH_MSG_TYPE_MOVE_RECEIVE
#define MACH_MSG_TYPE_PORT_SEND         MACH_MSG_TYPE_MOVE_SEND
#define MACH_MSG_TYPE_PORT_SEND_ONCE    MACH_MSG_TYPE_MOVE_SEND_ONCE
#define MACH_MSG_TYPE_LAST              22
#define MACH_MSG_TYPE_POLYMORPHIC       ((mach_msg_type_name_t) -1)
#define MACH_MSG_TYPE_PORT_ANY(x) \
        (((x) >= MACH_MSG_TYPE_MOVE_RECEIVE) && ((x) <= MACH_MSG_TYPE_MAKE_SEND_ONCE))
#define MACH_MSG_TYPE_PORT_ANY_SEND(x) \
        (((x) >= MACH_MSG_TYPE_MOVE_SEND) && ((x) <= MACH_MSG_TYPE_MAKE_SEND_ONCE))
#define MACH_MSG_TYPE_PORT_ANY_RIGHT(x) \
        (((x) >= MACH_MSG_TYPE_MOVE_RECEIVE) && ((x) <= MACH_MSG_TYPE_COPY_SEND))
#define MACH_MSG_TYPE_UNSTRUCTURED      0
#define MACH_MSG_TYPE_BIT               0
#define MACH_MSG_TYPE_BOOLEAN           0
#define MACH_MSG_TYPE_INTEGER_16        1
#define MACH_MSG_TYPE_INTEGER_32        2
#define MACH_MSG_TYPE_CHAR              8
#define MACH_MSG_TYPE_BYTE              9
#define MACH_MSG_TYPE_INTEGER_8         9
#define MACH_MSG_TYPE_REAL              10
#define MACH_MSG_TYPE_STRING            12
#define MACH_MSG_TYPE_STRING_C          12
#define MACH_MSG_TYPE_MOVE_SEND_ONCE    18

#define MACH_MSG_PORT_DESCRIPTOR          0
#define MACH_MSG_OOL_DESCRIPTOR           1
#define MACH_MSG_OOL_PORTS_DESCRIPTOR     2
#define MACH_MSG_OOL_VOLATILE_DESCRIPTOR  3

#define MACH_MSG_SUCCESS 0
#define MACH_MSGH_BITS_ZERO 0x00000000

typedef struct {
    mach_msg_size_t msgh_descriptor_count;
} mach_msg_body_t;

typedef struct {
    unsigned int  msgt_name : 8,
                  msgt_size : 8,
                  msgt_number : 12,
                  msgt_inline : 1,
                  msgt_longform : 1,
                  msgt_deallocate : 1,
                  msgt_unused : 1;
} mach_msg_type_t;
#endif
