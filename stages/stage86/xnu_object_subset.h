#ifndef MI4IOS6_STAGE86_XNU_OBJECT_SUBSET_H
#define MI4IOS6_STAGE86_XNU_OBJECT_SUBSET_H

#include <stdint.h>

#define STAGE86_XNU_OBJECT_SUBSET_VERSION              2u

#define STAGE86_XNU_OBJ_DEVICE_TREE_SOURCE             0x00000001u
#define STAGE86_XNU_OBJ_BOOTARGS_SOURCE                0x00000002u
#define STAGE86_XNU_OBJ_PE_GEN_SOURCE                  0x00000004u
#define STAGE86_XNU_OBJ_SHIMS_READY                    0x00000008u
#define STAGE86_XNU_OBJ_GRAPH_ACCEPTED                 0x00000010u
#define STAGE86_XNU_OBJ_DEVICE_TREE_COMPILED           0x00000020u
#define STAGE86_XNU_OBJ_BOOTARGS_COMPILED              0x00000040u
#define STAGE86_XNU_OBJ_PE_GEN_COMPILED                0x00000080u
#define STAGE86_XNU_OBJ_OBJECTS_PRESENT                0x00000100u
#define STAGE86_XNU_OBJ_PUBLIC_2050_BASELINE           0x00000200u
#define STAGE86_XNU_OBJ_PUBLIC_ONLY                    0x00000400u
#define STAGE86_XNU_OBJ_NO_FULL_XNU_BUILD              0x00000800u
#define STAGE86_XNU_OBJ_NO_MACHO_LINK                  0x00001000u
#define STAGE86_XNU_OBJ_NO_PUBLIC_XNU_EXEC             0x00002000u
#define STAGE86_XNU_OBJ_NO_EXTERNAL_MUTATION           0x00004000u
#define STAGE86_XNU_OBJ_OUTPUTS_IGNORED                0x00008000u
#define STAGE86_XNU_OBJ_FAIL_CLOSED                    0x00010000u
#define STAGE86_XNU_OBJ_ARM_PE_BOOTARGS_SOURCE         0x00020000u
#define STAGE86_XNU_OBJ_ARM_PE_BOOTARGS_COMPILED       0x00040000u
#define STAGE86_XNU_OBJ_PUBLIC_ARM_PEXPERT_OBJECT      0x00080000u
#define STAGE86_XNU_OBJ_DUPLICATE_SYMBOLS_CLOSED       0x00100000u
#define STAGE86_XNU_OBJ_PE_STATE_ABI_SHIM_READY        0x00200000u
#define STAGE86_XNU_OBJ_NO_PLATFORM_RUNTIME_EXEC       0x00400000u
#define STAGE86_XNU_OBJ_ARM_PE_CONSISTENT_DEBUG_SOURCE 0x00800000u
#define STAGE86_XNU_OBJ_ARM_PE_CONSISTENT_DEBUG_COMPILED 0x01000000u
#define STAGE86_XNU_OBJ_PUBLIC_ARM_CONSISTENT_DEBUG_OBJECT 0x02000000u
#define STAGE86_XNU_OBJ_CONSISTENT_DEBUG_ABI_SHIM_READY 0x04000000u
#define STAGE86_XNU_OBJ_REQUIRED_MASK                  0x07ffffffu

#define STAGE86_XNU_OBJ_SOURCE_MASK_DEVICE_TREE        0x00000001u
#define STAGE86_XNU_OBJ_SOURCE_MASK_BOOTARGS           0x00000002u
#define STAGE86_XNU_OBJ_SOURCE_MASK_PE_GEN             0x00000004u
#define STAGE86_XNU_OBJ_SOURCE_MASK_ARM_PE_BOOTARGS    0x00000008u
#define STAGE86_XNU_OBJ_SOURCE_MASK_ARM_PE_CONSISTENT_DEBUG 0x00000010u
#define STAGE86_XNU_OBJ_SOURCE_MASK_REQUIRED           0x0000001fu

#define STAGE86_XNU_OBJ_SHIM_PEXPERT_BOOT              0x00000001u
#define STAGE86_XNU_OBJ_SHIM_PEXPERT_PROTOS            0x00000002u
#define STAGE86_XNU_OBJ_SHIM_PEXPERT_PEXPERT           0x00000004u
#define STAGE86_XNU_OBJ_SHIM_KALLOC                    0x00000008u
#define STAGE86_XNU_OBJ_SHIM_MACH_TYPES                0x00000010u
#define STAGE86_XNU_OBJ_SHIM_KERN_DEBUG                0x00000020u
#define STAGE86_XNU_OBJ_SHIM_PE_STATE_ABI              0x00000040u
#define STAGE86_XNU_OBJ_SHIM_CONSISTENT_DEBUG_H        0x00000080u
#define STAGE86_XNU_OBJ_SHIM_OSATOMIC                  0x00000100u
#define STAGE86_XNU_OBJ_SHIM_MACHINE_ROUTINES          0x00000200u
#define STAGE86_XNU_OBJ_SHIM_REQUIRED                  0x000003ffu

#define STAGE86_XNU_OBJ_FAIL_DEVICE_TREE_SOURCE        0x00000001u
#define STAGE86_XNU_OBJ_FAIL_BOOTARGS_SOURCE           0x00000002u
#define STAGE86_XNU_OBJ_FAIL_PE_GEN_SOURCE             0x00000004u
#define STAGE86_XNU_OBJ_FAIL_SHIMS                     0x00000008u
#define STAGE86_XNU_OBJ_FAIL_GRAPH                     0x00000010u
#define STAGE86_XNU_OBJ_FAIL_DEVICE_TREE_COMPILE       0x00000020u
#define STAGE86_XNU_OBJ_FAIL_BOOTARGS_COMPILE          0x00000040u
#define STAGE86_XNU_OBJ_FAIL_PE_GEN_COMPILE            0x00000080u
#define STAGE86_XNU_OBJ_FAIL_OBJECTS                   0x00000100u
#define STAGE86_XNU_OBJ_FAIL_PUBLIC_BASELINE           0x00000200u
#define STAGE86_XNU_OBJ_FAIL_ARM_PE_BOOTARGS_SOURCE    0x00000400u
#define STAGE86_XNU_OBJ_FAIL_ARM_PE_BOOTARGS_COMPILE   0x00000800u
#define STAGE86_XNU_OBJ_FAIL_DUPLICATE_SYMBOLS         0x00001000u
#define STAGE86_XNU_OBJ_FAIL_PE_STATE_ABI              0x00002000u
#define STAGE86_XNU_OBJ_FAIL_ARM_PE_CONSISTENT_DEBUG_SOURCE 0x00004000u
#define STAGE86_XNU_OBJ_FAIL_ARM_PE_CONSISTENT_DEBUG_COMPILE 0x00008000u
#define STAGE86_XNU_OBJ_FAIL_CONSISTENT_DEBUG_ABI      0x00010000u
#define STAGE86_XNU_OBJ_FAIL_SAFETY_BOUNDARY           0x80000000u

struct stage86_xnu_object_subset {
    uint32_t version;
    uint32_t size;
    uint32_t status;
    uint32_t required_mask;
    uint32_t satisfied_mask;
    uint32_t failure_mask;
    uint32_t source_mask;
    uint32_t shim_mask;
    uint32_t object_count;
    uint32_t public_2050_object_count;
    uint32_t public_arm_pexpert_object_count;
    uint32_t stage_owned_shim_object_count;
    uint32_t duplicate_symbol_count;
    uint32_t pe_state_abi_shim_ready;
    uint32_t consistent_debug_abi_shim_ready;
    uint32_t baseline_commit32;
    uint32_t baseline_master_version;
    uint32_t device_tree_object_bytes;
    uint32_t bootargs_object_bytes;
    uint32_t pe_gen_object_bytes;
    uint32_t arm_pe_bootargs_object_bytes;
    uint32_t arm_pe_consistent_debug_object_bytes;
    uint32_t device_tree_object_sha32;
    uint32_t bootargs_object_sha32;
    uint32_t pe_gen_object_sha32;
    uint32_t arm_pe_bootargs_object_sha32;
    uint32_t arm_pe_consistent_debug_object_sha32;
    uint32_t public_2050_baseline;
    uint32_t public_only;
    uint32_t no_full_xnu_build;
    uint32_t no_macho_link;
    uint32_t no_public_xnu_exec;
    uint32_t no_platform_runtime_exec;
    uint32_t no_external_mutation;
    uint32_t outputs_ignored;
    uint32_t fail_closed;
    uint32_t checksum;
};

int stage86_xnu_object_subset_selftest(void);
const struct stage86_xnu_object_subset *stage86_xnu_object_subset_result(void);

#endif
