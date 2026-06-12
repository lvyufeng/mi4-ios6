#ifndef MI4IOS6_STAGE79_XNU_COMPILE_GRAPH_H
#define MI4IOS6_STAGE79_XNU_COMPILE_GRAPH_H

#include <stdint.h>

#define STAGE79_XNU_COMPILE_GRAPH_VERSION             4u

#define STAGE79_XNU_GRAPH_SOURCE_ROOT_PRESENT         0x00000001u
#define STAGE79_XNU_GRAPH_CANDIDATES_PRESENT          0x00000002u
#define STAGE79_XNU_GRAPH_DEVICE_TREE_CLASSIFIED      0x00000004u
#define STAGE79_XNU_GRAPH_BOOTARGS_CLASSIFIED         0x00000008u
#define STAGE79_XNU_GRAPH_PE_GEN_CLASSIFIED           0x00000010u
#define STAGE79_XNU_GRAPH_FORBIDDEN_EXCLUDED          0x00000020u
#define STAGE79_XNU_GRAPH_INCLUDE_DEPS_RECORDED       0x00000040u
#define STAGE79_XNU_GRAPH_SYMBOL_DEPS_RECORDED        0x00000080u
#define STAGE79_XNU_GRAPH_SHIM_NEEDS_RECORDED         0x00000100u
#define STAGE79_XNU_GRAPH_RISK_CLASSES_RECORDED       0x00000200u
#define STAGE79_XNU_GRAPH_PUBLIC_2050_BASELINE        0x00000400u
#define STAGE79_XNU_GRAPH_4570_REFERENCE_ONLY         0x00000800u
#define STAGE79_XNU_GRAPH_NO_FULL_XNU_BUILD           0x00001000u
#define STAGE79_XNU_GRAPH_NO_PUBLIC_XNU_EXEC          0x00002000u
#define STAGE79_XNU_GRAPH_NO_MACHO_EXEC               0x00004000u
#define STAGE79_XNU_GRAPH_NO_EXTERNAL_MUTATION        0x00008000u
#define STAGE79_XNU_GRAPH_OUTPUTS_IGNORED             0x00010000u
#define STAGE79_XNU_GRAPH_FAIL_CLOSED                 0x00020000u
#define STAGE79_XNU_GRAPH_ARM_BOOTARGS_CLASSIFIED     0x00040000u
#define STAGE79_XNU_GRAPH_ARM_BOOTARGS_ALLOWED        0x00080000u
#define STAGE79_XNU_GRAPH_PLATFORM_REFS_RECORDED      0x00100000u
#define STAGE79_XNU_GRAPH_BLOCKED_RUNTIME_RECORDED    0x00200000u
#define STAGE79_XNU_GRAPH_ARM_BOOT_LAYOUT_RECORDED    0x00400000u
#define STAGE79_XNU_GRAPH_NO_PLATFORM_RUNTIME_EXEC    0x00800000u
#define STAGE79_XNU_GRAPH_ARM_CONSISTENT_DEBUG_CLASSIFIED 0x01000000u
#define STAGE79_XNU_GRAPH_ARM_CONSISTENT_DEBUG_ALLOWED 0x02000000u
#define STAGE79_XNU_GRAPH_CONSISTENT_DEBUG_LAYOUT_RECORDED 0x04000000u
#define STAGE79_XNU_GRAPH_ARM_PEXPERT_RUNTIME_BLOCKED 0x08000000u
#define STAGE79_XNU_GRAPH_BOOTSTRAP_CONTRACT_SELECTED 0x10000000u
#define STAGE79_XNU_GRAPH_IOKIT_REFS_RECORDED         0x20000000u
#define STAGE79_XNU_GRAPH_IOKIT_RUNTIME_BLOCKED       0x40000000u
#define STAGE79_XNU_GRAPH_REQUIRED_MASK               0x7fffffffu

#define STAGE79_XNU_GRAPH_FAIL_SOURCE_ROOT            0x00000001u
#define STAGE79_XNU_GRAPH_FAIL_CANDIDATES             0x00000002u
#define STAGE79_XNU_GRAPH_FAIL_BASELINE               0x00000004u
#define STAGE79_XNU_GRAPH_FAIL_FORBIDDEN              0x00000008u
#define STAGE79_XNU_GRAPH_FAIL_SHIMS                  0x00000010u
#define STAGE79_XNU_GRAPH_FAIL_ARM_BOOTARGS           0x00000020u
#define STAGE79_XNU_GRAPH_FAIL_PLATFORM_REFS          0x00000040u
#define STAGE79_XNU_GRAPH_FAIL_DUPLICATES             0x00000080u
#define STAGE79_XNU_GRAPH_FAIL_ARM_CONSISTENT_DEBUG   0x00000100u
#define STAGE79_XNU_GRAPH_FAIL_BOOTSTRAP_CONTRACT     0x00000200u
#define STAGE79_XNU_GRAPH_FAIL_PMAP_REFERENCE         0x00000400u
#define STAGE79_XNU_GRAPH_FAIL_SAFETY_BOUNDARY        0x80000000u

#define STAGE79_XNU_GRAPH_PMAP_REF_ARM_VM_INIT        0x00000001u
#define STAGE79_XNU_GRAPH_PMAP_REF_PMAP_C             0x00000002u
#define STAGE79_XNU_GRAPH_PMAP_REF_PMAP_H             0x00000004u
#define STAGE79_XNU_GRAPH_PMAP_REF_PROC_REG_H         0x00000008u
#define STAGE79_XNU_GRAPH_PMAP_REF_VM_PARAM_H         0x00000010u
#define STAGE79_XNU_GRAPH_PMAP_REF_REQUIRED           0x0000001fu
#define STAGE79_XNU_GRAPH_PMAP_RUNTIME_ARM_VM_INIT    0x00000001u
#define STAGE79_XNU_GRAPH_PMAP_RUNTIME_PMAP_C         0x00000002u
#define STAGE79_XNU_GRAPH_PMAP_RUNTIME_BLOCK_REQUIRED 0x00000003u

struct stage79_xnu_compile_graph {
    uint32_t version;
    uint32_t size;
    uint32_t status;
    uint32_t required_mask;
    uint32_t satisfied_mask;
    uint32_t failure_mask;
    uint32_t candidate_count;
    uint32_t allowed_compile_count;
    uint32_t allowed_link_count;
    uint32_t forbidden_count;
    uint32_t shim_required_count;
    uint32_t max_risk_class;
    uint32_t device_tree_classified;
    uint32_t bootargs_classified;
    uint32_t pe_gen_classified;
    uint32_t pe_gen_allowed;
    uint32_t arm_bootargs_classified;
    uint32_t arm_bootargs_allowed;
    uint32_t arm_consistent_debug_classified;
    uint32_t arm_consistent_debug_allowed;
    uint32_t consistent_debug_layout_recorded;
    uint32_t arm_pe_kprintf_blocked;
    uint32_t arm_pe_serial_blocked;
    uint32_t arm_pe_identify_machine_blocked;
    uint32_t arm_pe_init_blocked;
    uint32_t bootstrap_contract_selected;
    uint32_t pmap_reference_mask;
    uint32_t pmap_runtime_blocked_mask;
    uint32_t pmap_reference_count;
    uint32_t pmap_public_compile_count;
    uint32_t pmap_public_link_count;
    uint32_t pmap_reference_only;
    uint32_t iokit_reference_mask;
    uint32_t iokit_runtime_blocked_mask;
    uint32_t iokit_reference_count;
    uint32_t iokit_public_compile_count;
    uint32_t iokit_public_link_count;
    uint32_t iokit_reference_only;
    uint32_t platform_reference_count;
    uint32_t blocked_runtime_count;
    uint32_t duplicate_symbol_count;
    uint32_t pe_state_abi_recorded;
    uint32_t boot_args_arm_layout_recorded;
    uint32_t forbidden_excluded;
    uint32_t include_deps_recorded;
    uint32_t symbol_deps_recorded;
    uint32_t shim_needs_recorded;
    uint32_t risk_classes_recorded;
    uint32_t baseline_commit32;
    uint32_t baseline_master_version;
    uint32_t public_2050_baseline;
    uint32_t xnu_4570_bounded_reference_policy;
    uint32_t no_full_xnu_build;
    uint32_t no_public_xnu_exec;
    uint32_t no_macho_exec;
    uint32_t no_platform_runtime_exec;
    uint32_t no_external_mutation;
    uint32_t outputs_ignored;
    uint32_t fail_closed;
    uint32_t checksum;
};

int stage79_xnu_compile_graph_selftest(void);
const struct stage79_xnu_compile_graph *stage79_xnu_compile_graph_result(void);

#endif
