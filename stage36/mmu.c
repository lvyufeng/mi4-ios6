#include "stage36.h"

#define L1_SECTION_COUNT       4096u
#define L1_SECTION_SIZE        0x00100000u
#define L1_SECTION_MASK        0xfff00000u

/* ARMv7 short-descriptor section: domain 0, AP full access, strongly ordered/shareable, executable. */
#define L1_DESC_SECTION_SO     0x00010c02u

#define SECTION_INDEX(addr)    (((uint32_t)(addr)) >> 20)
#define STAGE36_HIGH_ALIAS_BASE        0xc0000000u
#define STAGE36_RAM_CONSOLE_ALIAS_BASE 0xc0100000u
#define STAGE36_GIC_ALIAS_BASE         0xc0200000u

#define STAGE36_BOOTSTRAP_STATUS_OK    0x36000001u
#define STAGE36_BOOTSTRAP_STATUS_BASE  0x36000000u
#define STAGE36_FAIL_REV_VER           0x00000001u
#define STAGE36_FAIL_MACHINE           0x00000002u
#define STAGE36_FAIL_DT                0x00000004u
#define STAGE36_FAIL_MEMORY            0x00000008u
#define STAGE36_FAIL_CPU               0x00000010u
#define STAGE36_FAIL_GIC               0x00000020u
#define STAGE36_FAIL_TIMER             0x00000040u
#define STAGE36_FAIL_VECTOR            0x00000080u
#define STAGE36_FAIL_TIMEBASE_INIT     0x00000100u
#define STAGE36_FAIL_GIC_SUMMARY       0x00000200u
#define STAGE36_FAIL_INIT_STEPS        0x00000400u
#define STAGE36_FAIL_DT_SUMMARY        0x00000800u
#define STAGE36_FAIL_PLATFORM_RESULT   0x00001000u
#define STAGE36_FAIL_PHASE_TABLE       0x00002000u
#define STAGE36_FAIL_SERVICE_TABLE     0x00004000u
#define STAGE36_FAIL_PHASE_SERVICE_DEPS 0x00008000u
#define STAGE36_FAIL_PHASE_DISPATCHER  0x00010000u
#define STAGE36_FAIL_SERVICE_DISPATCHER 0x00020000u
#define STAGE36_FAIL_BOOTSTRAP_REGISTRY 0x00040000u
#define STAGE36_FAIL_BOOT_POLICY      0x00080000u
#define STAGE36_FAIL_BOOTSTRAP_MANIFEST 0x00100000u
#define STAGE36_FAIL_LAUNCH_CONTRACT 0x00200000u
#define STAGE36_FAIL_STARTUP_BOUNDARY 0x00400000u
#define STAGE36_FAIL_STARTUP_ROUTINE 0x00800000u
#define STAGE36_FAIL_STARTUP_ENTRY 0x01000000u
#define STAGE36_FAIL_KERNEL_CALLOUT 0x04000000u
#define STAGE36_FAIL_KERNEL_CONTEXT 0x08000000u
#define STAGE36_FAIL_BOOT_ALLOCATOR 0x10000000u
#define STAGE36_FAIL_VM_PLAN 0x40000000u
#define STAGE36_FAIL_VM_STATE 0x80000000u

#define STAGE36_INIT_STEP_VALIDATE     0x00000001u
#define STAGE36_INIT_STEP_TIMEBASE     0x00000002u
#define STAGE36_INIT_STEP_GIC_SUMMARY  0x00000004u
#define STAGE36_INIT_STEP_COMPLETE     0x00000008u

#define STAGE36_INIT_REQUIRED_STEPS    (STAGE36_INIT_STEP_VALIDATE | \
                                        STAGE36_INIT_STEP_TIMEBASE | \
                                        STAGE36_INIT_STEP_GIC_SUMMARY | \
                                        STAGE36_INIT_STEP_COMPLETE)

#define STAGE36_ROOT_STEP_ENTER        0x00000001u
#define STAGE36_ROOT_STEP_INIT         0x00000002u
#define STAGE36_ROOT_STEP_RESULT       0x00000004u
#define STAGE36_ROOT_STEP_RETURN       0x00000008u
#define STAGE36_ROOT_STEP_DT_SUMMARY      0x00000010u
#define STAGE36_ROOT_STEP_PLATFORM_RESULT 0x00000020u
#define STAGE36_ROOT_STEP_PHASE_TABLE     0x00000040u
#define STAGE36_ROOT_STEP_SERVICE_TABLE   0x00000080u
#define STAGE36_ROOT_STEP_PHASE_SERVICE_DEPS 0x00000100u
#define STAGE36_ROOT_STEP_PHASE_DISPATCHER   0x00000200u
#define STAGE36_ROOT_STEP_SERVICE_DISPATCHER 0x00000400u
#define STAGE36_ROOT_STEP_BOOTSTRAP_REGISTRY 0x00000800u
#define STAGE36_ROOT_STEP_BOOT_POLICY 0x00001000u
#define STAGE36_ROOT_STEP_BOOTSTRAP_MANIFEST 0x00002000u
#define STAGE36_ROOT_STEP_LAUNCH_CONTRACT 0x00004000u
#define STAGE36_ROOT_STEP_STARTUP_BOUNDARY 0x00008000u
#define STAGE36_ROOT_STEP_STARTUP_ROUTINE 0x00010000u
#define STAGE36_ROOT_STEP_STARTUP_ENTRY 0x00020000u
#define STAGE36_ROOT_STEP_KERNEL_CALLOUT 0x00040000u
#define STAGE36_ROOT_STEP_KERNEL_CONTEXT 0x00080000u
#define STAGE36_ROOT_STEP_VM_PLAN 0x00100000u
#define STAGE36_ROOT_STEP_VM_STATE 0x00200000u
#define STAGE36_ROOT_STEP_BOOT_ALLOCATOR 0x00400000u
#define STAGE36_ROOT_REQUIRED_STEPS       (STAGE36_ROOT_STEP_ENTER | \
                                           STAGE36_ROOT_STEP_INIT | \
                                           STAGE36_ROOT_STEP_RESULT | \
                                           STAGE36_ROOT_STEP_RETURN | \
                                           STAGE36_ROOT_STEP_DT_SUMMARY | \
                                           STAGE36_ROOT_STEP_PLATFORM_RESULT | \
                                           STAGE36_ROOT_STEP_PHASE_TABLE | \
                                           STAGE36_ROOT_STEP_SERVICE_TABLE | \
                                           STAGE36_ROOT_STEP_PHASE_SERVICE_DEPS | \
                                           STAGE36_ROOT_STEP_PHASE_DISPATCHER | \
                                           STAGE36_ROOT_STEP_SERVICE_DISPATCHER | \
                                           STAGE36_ROOT_STEP_BOOTSTRAP_REGISTRY | \
                                           STAGE36_ROOT_STEP_BOOT_POLICY | \
                                           STAGE36_ROOT_STEP_BOOTSTRAP_MANIFEST | \
                                           STAGE36_ROOT_STEP_LAUNCH_CONTRACT | \
                                           STAGE36_ROOT_STEP_STARTUP_BOUNDARY | \
                                           STAGE36_ROOT_STEP_STARTUP_ROUTINE | \
                                           STAGE36_ROOT_STEP_STARTUP_ENTRY | \
                                           STAGE36_ROOT_STEP_KERNEL_CALLOUT | \
                                           STAGE36_ROOT_STEP_KERNEL_CONTEXT | \
                                           STAGE36_ROOT_STEP_VM_PLAN | \
                                           STAGE36_ROOT_STEP_VM_STATE | \
                                           STAGE36_ROOT_STEP_BOOT_ALLOCATOR)

#define STAGE36_PLATFORM_RESULT_VERSION      1u
#define STAGE36_PLATFORM_CONSIST_MEMORY      0x00000001u
#define STAGE36_PLATFORM_CONSIST_CPU         0x00000002u
#define STAGE36_PLATFORM_CONSIST_GIC         0x00000004u
#define STAGE36_PLATFORM_CONSIST_TIMER       0x00000008u
#define STAGE36_PLATFORM_CONSIST_REQUIRED    (STAGE36_PLATFORM_CONSIST_MEMORY | \
                                              STAGE36_PLATFORM_CONSIST_CPU | \
                                              STAGE36_PLATFORM_CONSIST_GIC | \
                                              STAGE36_PLATFORM_CONSIST_TIMER)

#define STAGE36_PHASE_COUNT              4u
#define STAGE36_PHASE_VALIDATE          0u
#define STAGE36_PHASE_DT_SUMMARY        1u
#define STAGE36_PHASE_PLATFORM_RESULT   2u
#define STAGE36_PHASE_RETURN_READY      3u
#define STAGE36_PHASE_REQUIRED_MASK     0x0000000fu

#define STAGE36_SERVICE_COUNT           4u
#define STAGE36_SERVICE_LOGGING         0u
#define STAGE36_SERVICE_TIMEBASE        1u
#define STAGE36_SERVICE_PLATFORM        2u
#define STAGE36_SERVICE_INTERRUPTS      3u
#define STAGE36_SERVICE_REQUIRED_MASK   0x0000000fu

#define STAGE36_SERVICE_BIT_LOGGING     (1u << STAGE36_SERVICE_LOGGING)
#define STAGE36_SERVICE_BIT_TIMEBASE    (1u << STAGE36_SERVICE_TIMEBASE)
#define STAGE36_SERVICE_BIT_PLATFORM    (1u << STAGE36_SERVICE_PLATFORM)
#define STAGE36_SERVICE_BIT_INTERRUPTS  (1u << STAGE36_SERVICE_INTERRUPTS)

#define STAGE36_PHASE_VALIDATE_SERVICES        STAGE36_SERVICE_BIT_LOGGING
#define STAGE36_PHASE_DT_SUMMARY_SERVICES      (STAGE36_SERVICE_BIT_LOGGING | STAGE36_SERVICE_BIT_PLATFORM)
#define STAGE36_PHASE_PLATFORM_RESULT_SERVICES (STAGE36_SERVICE_BIT_LOGGING | STAGE36_SERVICE_BIT_PLATFORM | STAGE36_SERVICE_BIT_TIMEBASE)
#define STAGE36_PHASE_RETURN_READY_SERVICES    (STAGE36_SERVICE_BIT_LOGGING | STAGE36_SERVICE_BIT_TIMEBASE | STAGE36_SERVICE_BIT_PLATFORM | STAGE36_SERVICE_BIT_INTERRUPTS)
#define STAGE36_PHASE_SERVICE_REQUIRED_MASK    0x0000000fu
#define STAGE36_REGISTRY_VERSION               1u
#define STAGE36_REGISTRY_DISPATCH_COVERAGE_REQUIRED \
    (STAGE36_SERVICE_REQUIRED_MASK | (STAGE36_PHASE_REQUIRED_MASK << 16))

#define STAGE36_BOOT_POLICY_VERSION               1u
#define STAGE36_BOOT_POLICY_REQUIRED_ROOT_STEPS   (STAGE36_ROOT_STEP_ENTER | \
                                                   STAGE36_ROOT_STEP_INIT | \
                                                   STAGE36_ROOT_STEP_DT_SUMMARY | \
                                                   STAGE36_ROOT_STEP_PLATFORM_RESULT | \
                                                   STAGE36_ROOT_STEP_PHASE_TABLE | \
                                                   STAGE36_ROOT_STEP_SERVICE_TABLE | \
                                                   STAGE36_ROOT_STEP_PHASE_SERVICE_DEPS | \
                                                   STAGE36_ROOT_STEP_PHASE_DISPATCHER | \
                                                   STAGE36_ROOT_STEP_SERVICE_DISPATCHER | \
                                                   STAGE36_ROOT_STEP_BOOTSTRAP_REGISTRY)
#define STAGE36_BOOT_POLICY_SAT_ROOT_STEPS        0x00000001u
#define STAGE36_BOOT_POLICY_SAT_SERVICE_MASK      0x00000002u
#define STAGE36_BOOT_POLICY_SAT_PHASE_MASK        0x00000004u
#define STAGE36_BOOT_POLICY_SAT_DEPENDENCY_MASK   0x00000008u
#define STAGE36_BOOT_POLICY_SAT_DISPATCH_COVERAGE 0x00000010u
#define STAGE36_BOOT_POLICY_SAT_REGISTRY_STATUS   0x00000020u
#define STAGE36_BOOT_POLICY_SAT_REQUIRED          (STAGE36_BOOT_POLICY_SAT_ROOT_STEPS | \
                                                   STAGE36_BOOT_POLICY_SAT_SERVICE_MASK | \
                                                   STAGE36_BOOT_POLICY_SAT_PHASE_MASK | \
                                                   STAGE36_BOOT_POLICY_SAT_DEPENDENCY_MASK | \
                                                   STAGE36_BOOT_POLICY_SAT_DISPATCH_COVERAGE | \
                                                   STAGE36_BOOT_POLICY_SAT_REGISTRY_STATUS)

#define STAGE36_MANIFEST_VERSION                   1u
#define STAGE36_MANIFEST_RECORD_COUNT              6u
#define STAGE36_MANIFEST_RECORD_SERVICE            0u
#define STAGE36_MANIFEST_RECORD_PHASE              1u
#define STAGE36_MANIFEST_RECORD_DEPENDENCY         2u
#define STAGE36_MANIFEST_RECORD_DISPATCH           3u
#define STAGE36_MANIFEST_RECORD_POLICY             4u
#define STAGE36_MANIFEST_RECORD_STATUS             5u
#define STAGE36_MANIFEST_RECORD_REQUIRED_MASK      0x0000003fu
#define STAGE36_MANIFEST_REQUIRED_ROOT_STEPS       (STAGE36_BOOT_POLICY_REQUIRED_ROOT_STEPS | \
                                                   STAGE36_ROOT_STEP_BOOT_POLICY)

#define STAGE36_LAUNCH_CONTRACT_VERSION            1u
#define STAGE36_LAUNCH_REQUIRED_ROOT_STEPS         (STAGE36_MANIFEST_REQUIRED_ROOT_STEPS | \
                                                   STAGE36_ROOT_STEP_BOOTSTRAP_MANIFEST)
#define STAGE36_LAUNCH_MMU_ENABLED                 1u
#define STAGE36_LAUNCH_IRQ_READY_GIC_DIST          0x00000001u
#define STAGE36_LAUNCH_IRQ_READY_GIC_CPU           0x00000002u
#define STAGE36_LAUNCH_IRQ_READY_SERVICE           0x00000004u
#define STAGE36_LAUNCH_IRQ_READY_TIMEBASE          0x00000008u
#define STAGE36_LAUNCH_IRQ_READY_REQUIRED          (STAGE36_LAUNCH_IRQ_READY_GIC_DIST | \
                                                   STAGE36_LAUNCH_IRQ_READY_GIC_CPU | \
                                                   STAGE36_LAUNCH_IRQ_READY_SERVICE | \
                                                   STAGE36_LAUNCH_IRQ_READY_TIMEBASE)
#define STAGE36_LAUNCH_SAT_ROOT_STEPS              0x00000001u
#define STAGE36_LAUNCH_SAT_ROOT_STATUS             0x00000002u
#define STAGE36_LAUNCH_SAT_MANIFEST_STATUS         0x00000004u
#define STAGE36_LAUNCH_SAT_POLICY_STATUS           0x00000008u
#define STAGE36_LAUNCH_SAT_MMU_STATE               0x00000010u
#define STAGE36_LAUNCH_SAT_TIMEBASE                0x00000020u
#define STAGE36_LAUNCH_SAT_INTERRUPTS              0x00000040u
#define STAGE36_LAUNCH_SAT_REQUIRED                (STAGE36_LAUNCH_SAT_ROOT_STEPS | \
                                                   STAGE36_LAUNCH_SAT_ROOT_STATUS | \
                                                   STAGE36_LAUNCH_SAT_MANIFEST_STATUS | \
                                                   STAGE36_LAUNCH_SAT_POLICY_STATUS | \
                                                   STAGE36_LAUNCH_SAT_MMU_STATE | \
                                                   STAGE36_LAUNCH_SAT_TIMEBASE | \
                                                   STAGE36_LAUNCH_SAT_INTERRUPTS)

#define STAGE36_STARTUP_BOUNDARY_VERSION           1u
#define STAGE36_STARTUP_REQUIRED_ROOT_STEPS        (STAGE36_LAUNCH_REQUIRED_ROOT_STEPS | \
                                                   STAGE36_ROOT_STEP_LAUNCH_CONTRACT)
#define STAGE36_STARTUP_SAT_ROOT_STEPS             0x00000001u
#define STAGE36_STARTUP_SAT_LAUNCH_STATUS          0x00000002u
#define STAGE36_STARTUP_SAT_ROOT_STATUS            0x00000004u
#define STAGE36_STARTUP_SAT_MANIFEST_STATUS        0x00000008u
#define STAGE36_STARTUP_SAT_BOOT_ARGS              0x00000010u
#define STAGE36_STARTUP_SAT_DT                     0x00000020u
#define STAGE36_STARTUP_SAT_TIMEBASE               0x00000040u
#define STAGE36_STARTUP_SAT_INTERRUPTS             0x00000080u
#define STAGE36_STARTUP_SAT_REQUIRED               (STAGE36_STARTUP_SAT_ROOT_STEPS | \
                                                   STAGE36_STARTUP_SAT_LAUNCH_STATUS | \
                                                   STAGE36_STARTUP_SAT_ROOT_STATUS | \
                                                   STAGE36_STARTUP_SAT_MANIFEST_STATUS | \
                                                   STAGE36_STARTUP_SAT_BOOT_ARGS | \
                                                   STAGE36_STARTUP_SAT_DT | \
                                                   STAGE36_STARTUP_SAT_TIMEBASE | \
                                                   STAGE36_STARTUP_SAT_INTERRUPTS)

#define STAGE36_STARTUP_ROUTINE_VERSION            1u
#define STAGE36_ROUTINE_REQUIRED_ROOT_STEPS        (STAGE36_STARTUP_REQUIRED_ROOT_STEPS | \
                                                   STAGE36_ROOT_STEP_STARTUP_BOUNDARY)
#define STAGE36_ROUTINE_SAT_ROOT_STEPS             0x00000001u
#define STAGE36_ROUTINE_SAT_STARTUP_STATUS         0x00000002u
#define STAGE36_ROUTINE_SAT_LAUNCH_STATUS          0x00000004u
#define STAGE36_ROUTINE_SAT_ROOT_STATUS            0x00000008u
#define STAGE36_ROUTINE_SAT_BOOT_ARGS              0x00000010u
#define STAGE36_ROUTINE_SAT_DT                     0x00000020u
#define STAGE36_ROUTINE_SAT_TIMEBASE               0x00000040u
#define STAGE36_ROUTINE_SAT_INTERRUPTS             0x00000080u
#define STAGE36_ROUTINE_SAT_REQUIRED               (STAGE36_ROUTINE_SAT_ROOT_STEPS | \
                                                   STAGE36_ROUTINE_SAT_STARTUP_STATUS | \
                                                   STAGE36_ROUTINE_SAT_LAUNCH_STATUS | \
                                                   STAGE36_ROUTINE_SAT_ROOT_STATUS | \
                                                   STAGE36_ROUTINE_SAT_BOOT_ARGS | \
                                                   STAGE36_ROUTINE_SAT_DT | \
                                                   STAGE36_ROUTINE_SAT_TIMEBASE | \
                                                   STAGE36_ROUTINE_SAT_INTERRUPTS)

#define STAGE36_STARTUP_HANDOFF_VERSION            1u
#define STAGE36_STARTUP_ENTRY_VERSION              1u
#define STAGE36_ENTRY_REQUIRED_ROOT_STEPS          (STAGE36_ROUTINE_REQUIRED_ROOT_STEPS | \
                                                   STAGE36_ROOT_STEP_STARTUP_ROUTINE)
#define STAGE36_ENTRY_SAT_ROOT_STEPS               0x00000001u
#define STAGE36_ENTRY_SAT_ROUTINE_STATUS           0x00000002u
#define STAGE36_ENTRY_SAT_STARTUP_STATUS           0x00000004u
#define STAGE36_ENTRY_SAT_ROOT_STATUS              0x00000008u
#define STAGE36_ENTRY_SAT_BOOT_ARGS                0x00000010u
#define STAGE36_ENTRY_SAT_DT                       0x00000020u
#define STAGE36_ENTRY_SAT_TIMEBASE                 0x00000040u
#define STAGE36_ENTRY_SAT_INTERRUPTS               0x00000080u
#define STAGE36_ENTRY_SAT_REQUIRED                 (STAGE36_ENTRY_SAT_ROOT_STEPS | \
                                                   STAGE36_ENTRY_SAT_ROUTINE_STATUS | \
                                                   STAGE36_ENTRY_SAT_STARTUP_STATUS | \
                                                   STAGE36_ENTRY_SAT_ROOT_STATUS | \
                                                   STAGE36_ENTRY_SAT_BOOT_ARGS | \
                                                   STAGE36_ENTRY_SAT_DT | \
                                                   STAGE36_ENTRY_SAT_TIMEBASE | \
                                                   STAGE36_ENTRY_SAT_INTERRUPTS)

#define STAGE36_KERNEL_CALLOUT_VERSION             1u
#define STAGE36_KERNEL_CALLOUT_COUNT               4u
#define STAGE36_KERNEL_CALLOUT_BOOTSTRAP           0u
#define STAGE36_KERNEL_CALLOUT_PLATFORM            1u
#define STAGE36_KERNEL_CALLOUT_TIMEBASE            2u
#define STAGE36_KERNEL_CALLOUT_INTERRUPTS          3u
#define STAGE36_KERNEL_CALLOUT_REQUIRED_MASK       0x0000000fu
#define STAGE36_KERNEL_CALLOUT_REQUIRED_SERVICES   (STAGE36_SERVICE_BIT_LOGGING | \
                                                   STAGE36_SERVICE_BIT_PLATFORM | \
                                                   STAGE36_SERVICE_BIT_TIMEBASE | \
                                                   STAGE36_SERVICE_BIT_INTERRUPTS)
#define STAGE36_KERNEL_CONTEXT_VERSION             1u
#define STAGE36_KERNEL_CONTEXT_SAT_BOOT_ARGS       0x00000001u
#define STAGE36_KERNEL_CONTEXT_SAT_DT              0x00000002u
#define STAGE36_KERNEL_CONTEXT_SAT_PLATFORM        0x00000004u
#define STAGE36_KERNEL_CONTEXT_SAT_TIMEBASE        0x00000008u
#define STAGE36_KERNEL_CONTEXT_SAT_INTERRUPTS      0x00000010u
#define STAGE36_KERNEL_CONTEXT_SAT_CALLOUTS        0x00000020u
#define STAGE36_KERNEL_CONTEXT_SAT_REQUIRED        (STAGE36_KERNEL_CONTEXT_SAT_BOOT_ARGS | \
                                                   STAGE36_KERNEL_CONTEXT_SAT_DT | \
                                                   STAGE36_KERNEL_CONTEXT_SAT_PLATFORM | \
                                                   STAGE36_KERNEL_CONTEXT_SAT_TIMEBASE | \
                                                   STAGE36_KERNEL_CONTEXT_SAT_INTERRUPTS | \
                                                   STAGE36_KERNEL_CONTEXT_SAT_CALLOUTS)

#define STAGE36_VM_PLAN_VERSION                     1u
#define STAGE36_VM_PLAN_SAT_CONTEXT                 0x00000001u
#define STAGE36_VM_PLAN_SAT_MEMORY                  0x00000002u
#define STAGE36_VM_PLAN_SAT_L1_TABLE                0x00000004u
#define STAGE36_VM_PLAN_SAT_ALIASES                 0x00000008u
#define STAGE36_VM_PLAN_SAT_MMU                     0x00000010u
#define STAGE36_VM_PLAN_SAT_CACHES                  0x00000020u
#define STAGE36_VM_PLAN_SAT_SECTION_POLICY          0x00000040u
#define STAGE36_VM_PLAN_SAT_REQUIRED                (STAGE36_VM_PLAN_SAT_CONTEXT | \
                                                   STAGE36_VM_PLAN_SAT_MEMORY | \
                                                   STAGE36_VM_PLAN_SAT_L1_TABLE | \
                                                   STAGE36_VM_PLAN_SAT_ALIASES | \
                                                   STAGE36_VM_PLAN_SAT_MMU | \
                                                   STAGE36_VM_PLAN_SAT_CACHES | \
                                                   STAGE36_VM_PLAN_SAT_SECTION_POLICY)
#define STAGE36_VM_PLAN_IDENTITY_BASE               0x00000000u
#define STAGE36_VM_PLAN_MMU_ENABLED                 1u
#define STAGE36_VM_PLAN_CACHES_DISABLED             0u

#define STAGE36_VM_STATE_VERSION                    1u
#define STAGE36_VM_STATE_SAT_PLAN                   0x00000001u
#define STAGE36_VM_STATE_SAT_KERNEL_MAP             0x00000002u
#define STAGE36_VM_STATE_SAT_MEMORY_CURSOR          0x00000004u
#define STAGE36_VM_STATE_SAT_BOOT_ALLOC             0x00000008u
#define STAGE36_VM_STATE_SAT_PMAP_POLICY            0x00000010u
#define STAGE36_VM_STATE_SAT_MMU_CACHE_POLICY       0x00000020u
#define STAGE36_VM_STATE_SAT_REQUIRED               (STAGE36_VM_STATE_SAT_PLAN | \
                                                   STAGE36_VM_STATE_SAT_KERNEL_MAP | \
                                                   STAGE36_VM_STATE_SAT_MEMORY_CURSOR | \
                                                   STAGE36_VM_STATE_SAT_BOOT_ALLOC | \
                                                   STAGE36_VM_STATE_SAT_PMAP_POLICY | \
                                                   STAGE36_VM_STATE_SAT_MMU_CACHE_POLICY)
#define STAGE36_KERNEL_MAP_BASE                     STAGE36_HIGH_ALIAS_BASE
#define STAGE36_KERNEL_MAP_LIMIT                    (STAGE36_HIGH_ALIAS_BASE + L1_SECTION_SIZE)
#define STAGE36_BOOTSTRAP_ALLOC_SIZE                L1_SECTION_SIZE

#define STAGE36_BOOT_ALLOCATOR_VERSION              1u
#define STAGE36_BOOT_ALLOCATOR_SAT_VM_STATE         0x00000001u
#define STAGE36_BOOT_ALLOCATOR_SAT_SPAN             0x00000002u
#define STAGE36_BOOT_ALLOCATOR_SAT_CURSOR           0x00000004u
#define STAGE36_BOOT_ALLOCATOR_SAT_REMAINING        0x00000008u
#define STAGE36_BOOT_ALLOCATOR_SAT_FIRST_ALLOC      0x00000010u
#define STAGE36_BOOT_ALLOCATOR_SAT_POLICY           0x00000020u
#define STAGE36_BOOT_ALLOCATOR_SAT_REQUIRED         (STAGE36_BOOT_ALLOCATOR_SAT_VM_STATE | \
                                                   STAGE36_BOOT_ALLOCATOR_SAT_SPAN | \
                                                   STAGE36_BOOT_ALLOCATOR_SAT_CURSOR | \
                                                   STAGE36_BOOT_ALLOCATOR_SAT_REMAINING | \
                                                   STAGE36_BOOT_ALLOCATOR_SAT_FIRST_ALLOC | \
                                                   STAGE36_BOOT_ALLOCATOR_SAT_POLICY)
#define STAGE36_BOOT_ALLOCATOR_FIRST_TAG            0x414c4c43u
#define STAGE36_BOOT_ALLOCATOR_ALIGNMENT            0x00001000u

static uint32_t stage36_l1_table[L1_SECTION_COUNT] __attribute__((aligned(16384)));
static volatile uint32_t stage36_mmu_probe_word;
static volatile uint32_t stage36_mmu_alias_probe;

struct stage36_alias_state {
    uint32_t magic;
    uint32_t input;
    uint32_t result;
    uint32_t checksum;
};

static struct stage36_alias_state stage36_alias_state_block;

struct stage36_startup_handoff {
    uint32_t version;
    uint32_t size;
    uint32_t root_steps;
    uint32_t routine_status;
    uint32_t startup_status;
    uint32_t root_status;
    uint32_t boot_args_virt;
    uint32_t dt_virt;
    uint32_t timebase_freq;
    uint32_t interrupt_mask;
    uint32_t checksum;
    uint32_t status;
};

static struct stage36_startup_handoff stage36_startup_handoff_block;

struct stage36_kernel_context {
    uint32_t version;
    uint32_t size;
    uint32_t boot_args_virt;
    uint32_t dt_virt;
    uint32_t platform_status;
    uint32_t platform_consistency;
    uint32_t timebase_freq;
    uint32_t interrupt_mask;
    uint32_t callout_status;
    uint32_t callout_mask;
    uint32_t root_steps;
    uint32_t satisfied_mask;
    uint32_t checksum;
    uint32_t status;
};

static struct stage36_kernel_context stage36_kernel_context_block;

struct stage36_vm_bootstrap_plan {
    uint32_t version;
    uint32_t size;
    uint32_t context_status;
    uint32_t context_checksum;
    uint32_t low_identity_base;
    uint32_t high_alias_base;
    uint32_t ram_console_alias_base;
    uint32_t gic_alias_base;
    uint32_t l1_table_phys;
    uint32_t l1_table_virt;
    uint32_t memory_base;
    uint32_t memory_size;
    uint32_t section_size;
    uint32_t section_descriptor;
    uint32_t mmu_enabled;
    uint32_t cache_policy;
    uint32_t satisfied_mask;
    uint32_t checksum;
    uint32_t status;
};

static struct stage36_vm_bootstrap_plan stage36_vm_plan_block;

struct stage36_vm_bootstrap_state {
    uint32_t version;
    uint32_t size;
    uint32_t vm_plan_status;
    uint32_t vm_plan_checksum;
    uint32_t kernel_map_base;
    uint32_t kernel_map_limit;
    uint32_t available_memory_base;
    uint32_t available_memory_cursor;
    uint32_t bootstrap_alloc_base;
    uint32_t bootstrap_alloc_size;
    uint32_t bootstrap_alloc_end;
    uint32_t pmap_section_size;
    uint32_t pmap_section_descriptor;
    uint32_t pmap_l1_table_phys;
    uint32_t pmap_l1_table_virt;
    uint32_t mmu_enabled;
    uint32_t cache_policy;
    uint32_t satisfied_mask;
    uint32_t checksum;
    uint32_t status;
};

static struct stage36_vm_bootstrap_state stage36_vm_state_block;

struct stage36_bootstrap_allocator {
    uint32_t version;
    uint32_t size;
    uint32_t vm_state_status;
    uint32_t vm_state_checksum;
    uint32_t span_base;
    uint32_t span_size;
    uint32_t span_end;
    uint32_t initial_cursor;
    uint32_t current_cursor;
    uint32_t remaining_bytes;
    uint32_t first_alloc_base;
    uint32_t first_alloc_size;
    uint32_t first_alloc_end;
    uint32_t first_alloc_tag;
    uint32_t alignment;
    uint32_t satisfied_mask;
    uint32_t checksum;
    uint32_t status;
};

static struct stage36_bootstrap_allocator stage36_boot_allocator_block;

struct stage36_bootstrap_state {
    uint32_t magic;
    uint32_t boot_args_rev_ver;
    uint32_t machine_type;
    uint32_t device_tree_length;
    uint32_t pe_memory_base;
    uint32_t pe_memory_size;
    uint32_t pe_cpu_count;
    uint32_t pe_gic_dist_base;
    uint32_t pe_gic_cpu_base;
    uint32_t pe_timer_base;
    uint32_t pe_timer_frequency;
    uint32_t pe_vector_base;
    uint32_t boot_flags;
    uint32_t validation_mask;
    uint32_t root_steps;
    uint32_t root_status;
    uint32_t init_steps;
    uint32_t init_status;
    uint32_t init_timebase_freq;
    uint32_t init_timebase_delta_us;
    uint32_t init_gic_irq_count;
    uint32_t init_gic_cpu_count;
    uint32_t init_gic_dist_ctlr;
    uint32_t init_gic_cpu_ctlr;
    uint32_t root_boot_args_virt;
    uint32_t root_dt_virt;
    uint32_t root_dt_root_children;
    uint32_t root_dt_memory_base;
    uint32_t root_dt_memory_size;
    uint32_t root_dt_timer_frequency;
    uint32_t root_dt_summary_status;
    uint32_t platform_result_version;
    uint32_t platform_result_size;
    uint32_t platform_result_consistency;
    uint32_t platform_dt_cpu_count;
    uint32_t platform_pe_cpu_count;
    uint32_t platform_memory_base;
    uint32_t platform_memory_size;
    uint32_t platform_gic_dist_base;
    uint32_t platform_gic_cpu_base;
    uint32_t platform_timer_frequency;
    uint32_t platform_result_status;
    uint32_t phase_count;
    uint32_t phase_completed_mask;
    uint32_t phase_status_checksum;
    uint32_t phase0_status;
    uint32_t phase1_status;
    uint32_t phase2_status;
    uint32_t phase3_status;
    uint32_t phase0_required_services;
    uint32_t phase1_required_services;
    uint32_t phase2_required_services;
    uint32_t phase3_required_services;
    uint32_t phase_service_dependency_mask;
    uint32_t phase_service_satisfied_mask;
    uint32_t phase_service_status_checksum;
    uint32_t phase_service_dependency_status;
    uint32_t phase_dispatcher_count;
    uint32_t phase_dispatcher_order_mask;
    uint32_t phase_dispatcher_handler_mask;
    uint32_t phase_dispatcher_status_checksum;
    uint32_t phase0_descriptor_id;
    uint32_t phase1_descriptor_id;
    uint32_t phase2_descriptor_id;
    uint32_t phase3_descriptor_id;
    uint32_t phase0_handler_result;
    uint32_t phase1_handler_result;
    uint32_t phase2_handler_result;
    uint32_t phase3_handler_result;
    uint32_t phase_dispatcher_status;
    uint32_t service_count;
    uint32_t service_available_mask;
    uint32_t service_status_checksum;
    uint32_t service_logging_status;
    uint32_t service_timebase_status;
    uint32_t service_platform_status;
    uint32_t service_interrupts_status;
    uint32_t service_dispatcher_count;
    uint32_t service_dispatcher_order_mask;
    uint32_t service_dispatcher_handler_mask;
    uint32_t service_dispatcher_status_checksum;
    uint32_t service_logging_descriptor_id;
    uint32_t service_timebase_descriptor_id;
    uint32_t service_platform_descriptor_id;
    uint32_t service_interrupts_descriptor_id;
    uint32_t service_logging_handler_result;
    uint32_t service_timebase_handler_result;
    uint32_t service_platform_handler_result;
    uint32_t service_interrupts_handler_result;
    uint32_t service_dispatcher_status;
    uint32_t registry_version;
    uint32_t registry_size;
    uint32_t registry_service_descriptor_mask;
    uint32_t registry_phase_descriptor_mask;
    uint32_t registry_dependency_coverage_mask;
    uint32_t registry_dispatch_coverage_mask;
    uint32_t registry_status_checksum;
    uint32_t registry_status;
    uint32_t boot_policy_version;
    uint32_t boot_policy_size;
    uint32_t boot_policy_required_root_steps;
    uint32_t boot_policy_required_service_mask;
    uint32_t boot_policy_required_phase_mask;
    uint32_t boot_policy_required_dependency_mask;
    uint32_t boot_policy_required_dispatch_coverage_mask;
    uint32_t boot_policy_required_registry_status;
    uint32_t boot_policy_observed_root_steps;
    uint32_t boot_policy_observed_service_mask;
    uint32_t boot_policy_observed_phase_mask;
    uint32_t boot_policy_observed_dependency_mask;
    uint32_t boot_policy_observed_dispatch_coverage_mask;
    uint32_t boot_policy_observed_registry_status;
    uint32_t boot_policy_satisfied_mask;
    uint32_t boot_policy_status_checksum;
    uint32_t boot_policy_status;
    uint32_t manifest_version;
    uint32_t manifest_record_count;
    uint32_t manifest_required_record_mask;
    uint32_t manifest_order_mask;
    uint32_t manifest_satisfied_mask;
    uint32_t manifest_required_root_steps;
    uint32_t manifest_observed_root_steps;
    uint32_t manifest_service_required_mask;
    uint32_t manifest_service_observed_mask;
    uint32_t manifest_service_status;
    uint32_t manifest_phase_required_mask;
    uint32_t manifest_phase_observed_mask;
    uint32_t manifest_phase_status;
    uint32_t manifest_dependency_required_mask;
    uint32_t manifest_dependency_observed_mask;
    uint32_t manifest_dependency_status;
    uint32_t manifest_dispatch_required_mask;
    uint32_t manifest_dispatch_observed_mask;
    uint32_t manifest_dispatch_status;
    uint32_t manifest_policy_required_mask;
    uint32_t manifest_policy_observed_mask;
    uint32_t manifest_policy_status;
    uint32_t manifest_boot_required_status;
    uint32_t manifest_boot_observed_status;
    uint32_t manifest_boot_status_record_status;
    uint32_t manifest_status_checksum;
    uint32_t manifest_status;
    uint32_t launch_contract_version;
    uint32_t launch_contract_size;
    uint32_t launch_required_root_steps;
    uint32_t launch_observed_root_steps;
    uint32_t launch_required_root_status;
    uint32_t launch_observed_root_status;
    uint32_t launch_required_manifest_status;
    uint32_t launch_observed_manifest_status;
    uint32_t launch_required_policy_status;
    uint32_t launch_observed_policy_status;
    uint32_t launch_required_mmu_state;
    uint32_t launch_observed_mmu_state;
    uint32_t launch_required_timebase_freq;
    uint32_t launch_observed_timebase_freq;
    uint32_t launch_required_interrupt_mask;
    uint32_t launch_observed_interrupt_mask;
    uint32_t launch_satisfied_mask;
    uint32_t launch_status_checksum;
    uint32_t launch_status;
    uint32_t startup_boundary_version;
    uint32_t startup_boundary_size;
    uint32_t startup_required_root_steps;
    uint32_t startup_observed_root_steps;
    uint32_t startup_required_launch_status;
    uint32_t startup_observed_launch_status;
    uint32_t startup_required_root_status;
    uint32_t startup_observed_root_status;
    uint32_t startup_required_manifest_status;
    uint32_t startup_observed_manifest_status;
    uint32_t startup_required_boot_args_virt;
    uint32_t startup_observed_boot_args_virt;
    uint32_t startup_required_dt_virt;
    uint32_t startup_observed_dt_virt;
    uint32_t startup_required_timebase_freq;
    uint32_t startup_observed_timebase_freq;
    uint32_t startup_required_interrupt_mask;
    uint32_t startup_observed_interrupt_mask;
    uint32_t startup_satisfied_mask;
    uint32_t startup_status_checksum;
    uint32_t startup_status;
    uint32_t startup_routine_version;
    uint32_t startup_routine_size;
    uint32_t startup_routine_required_root_steps;
    uint32_t startup_routine_observed_root_steps;
    uint32_t startup_routine_required_startup_status;
    uint32_t startup_routine_observed_startup_status;
    uint32_t startup_routine_required_launch_status;
    uint32_t startup_routine_observed_launch_status;
    uint32_t startup_routine_required_root_status;
    uint32_t startup_routine_observed_root_status;
    uint32_t startup_routine_required_boot_args_virt;
    uint32_t startup_routine_observed_boot_args_virt;
    uint32_t startup_routine_required_dt_virt;
    uint32_t startup_routine_observed_dt_virt;
    uint32_t startup_routine_required_timebase_freq;
    uint32_t startup_routine_observed_timebase_freq;
    uint32_t startup_routine_required_interrupt_mask;
    uint32_t startup_routine_observed_interrupt_mask;
    uint32_t startup_routine_satisfied_mask;
    uint32_t startup_routine_status_checksum;
    uint32_t startup_routine_status;
    uint32_t startup_entry_version;
    uint32_t startup_entry_size;
    uint32_t startup_entry_required_root_steps;
    uint32_t startup_entry_observed_root_steps;
    uint32_t startup_entry_required_routine_status;
    uint32_t startup_entry_observed_routine_status;
    uint32_t startup_entry_required_startup_status;
    uint32_t startup_entry_observed_startup_status;
    uint32_t startup_entry_required_root_status;
    uint32_t startup_entry_observed_root_status;
    uint32_t startup_entry_required_boot_args_virt;
    uint32_t startup_entry_observed_boot_args_virt;
    uint32_t startup_entry_required_dt_virt;
    uint32_t startup_entry_observed_dt_virt;
    uint32_t startup_entry_required_timebase_freq;
    uint32_t startup_entry_observed_timebase_freq;
    uint32_t startup_entry_required_interrupt_mask;
    uint32_t startup_entry_observed_interrupt_mask;
    uint32_t startup_entry_satisfied_mask;
    uint32_t startup_entry_status_checksum;
    uint32_t startup_entry_status;
    uint32_t kernel_callout_version;
    uint32_t kernel_callout_count;
    uint32_t kernel_callout_required_mask;
    uint32_t kernel_callout_order_mask;
    uint32_t kernel_callout_handler_mask;
    uint32_t kernel_callout_required_service_mask;
    uint32_t kernel_callout_observed_service_mask;
    uint32_t kernel_callout_status_checksum;
    uint32_t kernel_callout0_descriptor_id;
    uint32_t kernel_callout1_descriptor_id;
    uint32_t kernel_callout2_descriptor_id;
    uint32_t kernel_callout3_descriptor_id;
    uint32_t kernel_callout0_required_services;
    uint32_t kernel_callout1_required_services;
    uint32_t kernel_callout2_required_services;
    uint32_t kernel_callout3_required_services;
    uint32_t kernel_callout0_status;
    uint32_t kernel_callout1_status;
    uint32_t kernel_callout2_status;
    uint32_t kernel_callout3_status;
    uint32_t kernel_callout_status;
    uint32_t kernel_context_version;
    uint32_t kernel_context_size;
    uint32_t kernel_context_required_mask;
    uint32_t kernel_context_satisfied_mask;
    uint32_t kernel_context_checksum;
    uint32_t kernel_context_status;
    uint32_t vm_plan_version;
    uint32_t vm_plan_size;
    uint32_t vm_plan_required_mask;
    uint32_t vm_plan_satisfied_mask;
    uint32_t vm_plan_checksum;
    uint32_t vm_plan_status;
    uint32_t vm_state_version;
    uint32_t vm_state_size;
    uint32_t vm_state_required_mask;
    uint32_t vm_state_satisfied_mask;
    uint32_t vm_state_checksum;
    uint32_t vm_state_status;
    uint32_t allocator_version;
    uint32_t allocator_size;
    uint32_t allocator_required_mask;
    uint32_t allocator_satisfied_mask;
    uint32_t allocator_checksum;
    uint32_t allocator_status;
    uint32_t status;
    uint32_t checksum;
};

static struct stage36_bootstrap_state stage36_bootstrap_state_block;

static uint32_t stage36_bootstrap_checksum(volatile const struct stage36_bootstrap_state *state)
{
    volatile const uint32_t *words = (volatile const uint32_t *)(uintptr_t)state;
    uint32_t count = (uint32_t)(offsetof(struct stage36_bootstrap_state, checksum) / sizeof(uint32_t));
    uint32_t checksum = 0;

    for (uint32_t i = 0; i < count; i++) {
        checksum ^= words[i];
    }

    return checksum;
}

static uint32_t stage36_kernel_context_checksum(volatile const struct stage36_kernel_context *context)
{
    volatile const uint32_t *words = (volatile const uint32_t *)(uintptr_t)context;
    uint32_t count = (uint32_t)(offsetof(struct stage36_kernel_context, checksum) / sizeof(uint32_t));
    uint32_t checksum = 0;

    for (uint32_t i = 0; i < count; i++) {
        checksum ^= words[i];
    }

    return checksum;
}

static uint32_t stage36_vm_plan_checksum(volatile const struct stage36_vm_bootstrap_plan *plan)
{
    volatile const uint32_t *words = (volatile const uint32_t *)(uintptr_t)plan;
    uint32_t count = (uint32_t)(offsetof(struct stage36_vm_bootstrap_plan, checksum) / sizeof(uint32_t));
    uint32_t checksum = 0;

    for (uint32_t i = 0; i < count; i++) {
        checksum ^= words[i];
    }

    return checksum;
}

static uint32_t stage36_vm_state_checksum(volatile const struct stage36_vm_bootstrap_state *vm_state)
{
    volatile const uint32_t *words = (volatile const uint32_t *)(uintptr_t)vm_state;
    uint32_t count = (uint32_t)(offsetof(struct stage36_vm_bootstrap_state, checksum) / sizeof(uint32_t));
    uint32_t checksum = 0;

    for (uint32_t i = 0; i < count; i++) {
        checksum ^= words[i];
    }

    return checksum;
}

static uint32_t stage36_boot_allocator_checksum(volatile const struct stage36_bootstrap_allocator *allocator)
{
    volatile const uint32_t *words = (volatile const uint32_t *)(uintptr_t)allocator;
    uint32_t count = (uint32_t)(offsetof(struct stage36_bootstrap_allocator, checksum) / sizeof(uint32_t));
    uint32_t checksum = 0;

    for (uint32_t i = 0; i < count; i++) {
        checksum ^= words[i];
    }

    return checksum;
}

struct stage36_phase_descriptor {
    uint32_t id;
    uint32_t required_services;
    uint32_t condition;
    volatile uint32_t *status_slot;
    volatile uint32_t *handler_result_slot;
};

struct stage36_service_descriptor {
    uint32_t id;
    uint32_t condition;
    volatile uint32_t *status_slot;
    volatile uint32_t *handler_result_slot;
};

struct stage36_manifest_record_descriptor {
    uint32_t id;
    uint32_t required;
    uint32_t observed;
    volatile uint32_t *status_slot;
};

struct stage36_kernel_callout_descriptor {
    uint32_t id;
    uint32_t required_services;
    uint32_t condition;
    volatile uint32_t *status_slot;
};

static inline uint32_t read_sctlr(void);

static uint32_t stage36_dispatch_service(const struct stage36_service_descriptor *desc,
                                         uint32_t *available_mask,
                                         uint32_t *order_mask)
{
    uint32_t service_bit = (1u << desc->id);
    uint32_t handler_result = desc->condition ? STAGE36_BOOTSTRAP_STATUS_OK :
        (STAGE36_BOOTSTRAP_STATUS_BASE | STAGE36_FAIL_SERVICE_DISPATCHER | service_bit);

    *desc->handler_result_slot = handler_result;
    *desc->status_slot = handler_result;
    *order_mask |= service_bit;

    if (handler_result == STAGE36_BOOTSTRAP_STATUS_OK) {
        *available_mask |= service_bit;
    }

    return handler_result;
}

static uint32_t stage36_dispatch_phase(volatile struct stage36_bootstrap_state *state,
                                       const struct stage36_phase_descriptor *desc,
                                       uint32_t *completed_mask,
                                       uint32_t *order_mask)
{
    uint32_t phase_bit = (1u << desc->id);
    uint32_t services_ok = ((state->service_available_mask & desc->required_services) == desc->required_services);
    uint32_t handler_result = (desc->condition && services_ok) ? STAGE36_BOOTSTRAP_STATUS_OK :
        (STAGE36_BOOTSTRAP_STATUS_BASE | STAGE36_FAIL_PHASE_DISPATCHER | phase_bit);

    *desc->handler_result_slot = handler_result;
    *desc->status_slot = handler_result;
    *order_mask |= phase_bit;

    if (handler_result == STAGE36_BOOTSTRAP_STATUS_OK) {
        *completed_mask |= phase_bit;
    }

    return handler_result;
}

static uint32_t stage36_dispatch_manifest_record(const struct stage36_manifest_record_descriptor *desc,
                                                  uint32_t *order_mask,
                                                  uint32_t *satisfied_mask)
{
    uint32_t record_bit = (1u << desc->id);
    uint32_t handler_result = (desc->observed == desc->required) ? STAGE36_BOOTSTRAP_STATUS_OK :
        (STAGE36_BOOTSTRAP_STATUS_BASE | STAGE36_FAIL_BOOTSTRAP_MANIFEST | record_bit);

    *desc->status_slot = handler_result;
    *order_mask |= record_bit;

    if (handler_result == STAGE36_BOOTSTRAP_STATUS_OK) {
        *satisfied_mask |= record_bit;
    }

    return handler_result;
}

static uint32_t stage36_dispatch_kernel_callout(const struct stage36_kernel_callout_descriptor *desc,
                                                uint32_t available_services,
                                                uint32_t *order_mask,
                                                uint32_t *handler_mask)
{
    uint32_t callout_bit = (1u << desc->id);
    uint32_t services_ok = ((available_services & desc->required_services) == desc->required_services);
    uint32_t handler_result = (desc->condition && services_ok) ? STAGE36_BOOTSTRAP_STATUS_OK :
        (STAGE36_BOOTSTRAP_STATUS_BASE | STAGE36_FAIL_KERNEL_CALLOUT | callout_bit);

    *desc->status_slot = handler_result;
    *order_mask |= callout_bit;

    if (handler_result == STAGE36_BOOTSTRAP_STATUS_OK) {
        *handler_mask |= callout_bit;
    }

    return handler_result;
}

static uint32_t stage36_high_alias_target(uint32_t input, volatile struct stage36_alias_state *state)
    __attribute__((noinline));

static uint32_t stage36_high_alias_target(uint32_t input, volatile struct stage36_alias_state *state)
{
    uint32_t result = (input ^ 0x12c0ffeeu) + 0x1234u;

    state->magic = 0x12001200u;
    state->input = input;
    state->result = result;
    state->checksum = state->magic ^ state->input ^ state->result;

    return result;
}

typedef uint32_t (*stage36_startup_entry_fn_t)(volatile struct stage36_startup_handoff *,
                                               volatile struct stage36_bootstrap_state *,
                                               volatile struct stage36_kernel_context *);

static uint32_t stage36_startup_entry(volatile struct stage36_startup_handoff *handoff,
                                      volatile struct stage36_bootstrap_state *state,
                                      volatile struct stage36_kernel_context *context)
    __attribute__((noinline));

static uint32_t stage36_startup_entry(volatile struct stage36_startup_handoff *handoff,
                                      volatile struct stage36_bootstrap_state *state,
                                      volatile struct stage36_kernel_context *context)
{
    uint32_t handoff_checksum = handoff->version ^ handoff->size ^ handoff->root_steps ^
        handoff->routine_status ^ handoff->startup_status ^ handoff->root_status ^
        handoff->boot_args_virt ^ handoff->dt_virt ^ handoff->timebase_freq ^
        handoff->interrupt_mask;
    uint32_t state_arg = (uint32_t)(uintptr_t)state;
    uint32_t handoff_arg = (uint32_t)(uintptr_t)handoff;
    uint32_t context_arg = (uint32_t)(uintptr_t)context;
    uint32_t handoff_ok = (handoff_arg >= STAGE36_HIGH_ALIAS_BASE &&
        state_arg >= STAGE36_HIGH_ALIAS_BASE &&
        context_arg >= STAGE36_HIGH_ALIAS_BASE &&
        handoff->version == STAGE36_STARTUP_HANDOFF_VERSION &&
        handoff->size == sizeof(*handoff) &&
        handoff->checksum == handoff_checksum &&
        handoff->status == STAGE36_BOOTSTRAP_STATUS_OK &&
        state->startup_routine_version == STAGE36_STARTUP_ROUTINE_VERSION &&
        state->startup_routine_status == STAGE36_BOOTSTRAP_STATUS_OK);

    xnu_log_puts("high virtual startup_entry entered\n");
    xnu_log_kv32("high_startup_entry_state_arg", state_arg);
    xnu_log_kv32("high_startup_entry_handoff_arg", handoff_arg);
    xnu_log_kv32("high_startup_entry_context_arg", context_arg);
    xnu_log_kv32("high_startup_entry_handoff_checksum_expected", handoff_checksum);

    state->startup_entry_observed_root_steps = handoff->root_steps &
        state->startup_entry_required_root_steps;
    state->startup_entry_observed_routine_status = handoff->routine_status;
    state->startup_entry_observed_startup_status = handoff->startup_status;
    state->startup_entry_observed_root_status = handoff->root_status;
    state->startup_entry_observed_boot_args_virt = handoff->boot_args_virt;
    state->startup_entry_observed_dt_virt = handoff->dt_virt;
    state->startup_entry_observed_timebase_freq = handoff->timebase_freq;
    state->startup_entry_observed_interrupt_mask = handoff->interrupt_mask;

    if (state->startup_entry_observed_root_steps == state->startup_entry_required_root_steps) {
        state->startup_entry_satisfied_mask |= STAGE36_ENTRY_SAT_ROOT_STEPS;
    }
    if (state->startup_entry_observed_routine_status == state->startup_entry_required_routine_status) {
        state->startup_entry_satisfied_mask |= STAGE36_ENTRY_SAT_ROUTINE_STATUS;
    }
    if (state->startup_entry_observed_startup_status == state->startup_entry_required_startup_status) {
        state->startup_entry_satisfied_mask |= STAGE36_ENTRY_SAT_STARTUP_STATUS;
    }
    if (state->startup_entry_observed_root_status == state->startup_entry_required_root_status) {
        state->startup_entry_satisfied_mask |= STAGE36_ENTRY_SAT_ROOT_STATUS;
    }
    if (state->startup_entry_observed_boot_args_virt == state->startup_entry_required_boot_args_virt) {
        state->startup_entry_satisfied_mask |= STAGE36_ENTRY_SAT_BOOT_ARGS;
    }
    if (state->startup_entry_observed_dt_virt == state->startup_entry_required_dt_virt) {
        state->startup_entry_satisfied_mask |= STAGE36_ENTRY_SAT_DT;
    }
    if (state->startup_entry_observed_timebase_freq == state->startup_entry_required_timebase_freq) {
        state->startup_entry_satisfied_mask |= STAGE36_ENTRY_SAT_TIMEBASE;
    }
    if (state->startup_entry_observed_interrupt_mask == state->startup_entry_required_interrupt_mask) {
        state->startup_entry_satisfied_mask |= STAGE36_ENTRY_SAT_INTERRUPTS;
    }

    state->startup_entry_status_checksum = state->startup_entry_version ^ state->startup_entry_size ^
        state->startup_entry_required_root_steps ^ state->startup_entry_observed_root_steps ^
        state->startup_entry_required_routine_status ^ state->startup_entry_observed_routine_status ^
        state->startup_entry_required_startup_status ^ state->startup_entry_observed_startup_status ^
        state->startup_entry_required_root_status ^ state->startup_entry_observed_root_status ^
        state->startup_entry_required_boot_args_virt ^ state->startup_entry_observed_boot_args_virt ^
        state->startup_entry_required_dt_virt ^ state->startup_entry_observed_dt_virt ^
        state->startup_entry_required_timebase_freq ^ state->startup_entry_observed_timebase_freq ^
        state->startup_entry_required_interrupt_mask ^ state->startup_entry_observed_interrupt_mask ^
        state->startup_entry_satisfied_mask;

    if (handoff_ok &&
        state->startup_entry_version == STAGE36_STARTUP_ENTRY_VERSION &&
        state->startup_entry_size == sizeof(*state) &&
        state->startup_entry_required_root_steps == STAGE36_ENTRY_REQUIRED_ROOT_STEPS &&
        state->startup_entry_observed_root_steps == STAGE36_ENTRY_REQUIRED_ROOT_STEPS &&
        state->startup_entry_required_routine_status == STAGE36_BOOTSTRAP_STATUS_OK &&
        state->startup_entry_observed_routine_status == STAGE36_BOOTSTRAP_STATUS_OK &&
        state->startup_entry_required_startup_status == STAGE36_BOOTSTRAP_STATUS_OK &&
        state->startup_entry_observed_startup_status == STAGE36_BOOTSTRAP_STATUS_OK &&
        state->startup_entry_required_root_status == STAGE36_BOOTSTRAP_STATUS_OK &&
        state->startup_entry_observed_root_status == STAGE36_BOOTSTRAP_STATUS_OK &&
        state->startup_entry_required_boot_args_virt == handoff->boot_args_virt &&
        state->startup_entry_observed_boot_args_virt == handoff->boot_args_virt &&
        state->startup_entry_required_dt_virt == handoff->dt_virt &&
        state->startup_entry_observed_dt_virt == handoff->dt_virt &&
        state->startup_entry_required_timebase_freq == 19200000u &&
        state->startup_entry_observed_timebase_freq == 19200000u &&
        state->startup_entry_required_interrupt_mask == STAGE36_LAUNCH_IRQ_READY_REQUIRED &&
        state->startup_entry_observed_interrupt_mask == STAGE36_LAUNCH_IRQ_READY_REQUIRED &&
        state->startup_entry_satisfied_mask == STAGE36_ENTRY_SAT_REQUIRED &&
        state->startup_entry_status_checksum == (STAGE36_STARTUP_ENTRY_VERSION ^ sizeof(*state) ^
            STAGE36_ENTRY_REQUIRED_ROOT_STEPS ^ STAGE36_ENTRY_REQUIRED_ROOT_STEPS ^
            STAGE36_BOOTSTRAP_STATUS_OK ^ STAGE36_BOOTSTRAP_STATUS_OK ^
            STAGE36_BOOTSTRAP_STATUS_OK ^ STAGE36_BOOTSTRAP_STATUS_OK ^
            STAGE36_BOOTSTRAP_STATUS_OK ^ STAGE36_BOOTSTRAP_STATUS_OK ^
            handoff->boot_args_virt ^ handoff->boot_args_virt ^ handoff->dt_virt ^
            handoff->dt_virt ^ 19200000u ^ 19200000u ^
            STAGE36_LAUNCH_IRQ_READY_REQUIRED ^ STAGE36_LAUNCH_IRQ_READY_REQUIRED ^
            STAGE36_ENTRY_SAT_REQUIRED)) {
        state->startup_entry_status = STAGE36_BOOTSTRAP_STATUS_OK;
        xnu_log_puts("high virtual startup_entry ok\n");
    } else {
        state->startup_entry_status = STAGE36_BOOTSTRAP_STATUS_BASE | STAGE36_FAIL_STARTUP_ENTRY;
        xnu_log_puts("high virtual startup_entry failed\n");
    }

    xnu_log_puts("high startup_entry kernel callout table begin\n");
    {
        uint32_t callout_order_mask = 0;
        uint32_t callout_handler_mask = 0;
        const struct stage36_kernel_callout_descriptor callout_desc[STAGE36_KERNEL_CALLOUT_COUNT] = {
            {
                STAGE36_KERNEL_CALLOUT_BOOTSTRAP,
                state->kernel_callout0_required_services,
                (state->startup_entry_status == STAGE36_BOOTSTRAP_STATUS_OK),
                &state->kernel_callout0_status,
            },
            {
                STAGE36_KERNEL_CALLOUT_PLATFORM,
                state->kernel_callout1_required_services,
                (state->platform_result_status == STAGE36_BOOTSTRAP_STATUS_OK),
                &state->kernel_callout1_status,
            },
            {
                STAGE36_KERNEL_CALLOUT_TIMEBASE,
                state->kernel_callout2_required_services,
                (state->init_timebase_freq == 19200000u),
                &state->kernel_callout2_status,
            },
            {
                STAGE36_KERNEL_CALLOUT_INTERRUPTS,
                state->kernel_callout3_required_services,
                (state->launch_observed_interrupt_mask == STAGE36_LAUNCH_IRQ_READY_REQUIRED),
                &state->kernel_callout3_status,
            },
        };

        state->kernel_callout_observed_service_mask = state->service_available_mask;
        for (uint32_t i = 0; i < STAGE36_KERNEL_CALLOUT_COUNT; i++) {
            (void)stage36_dispatch_kernel_callout(&callout_desc[i],
                state->kernel_callout_observed_service_mask,
                &callout_order_mask, &callout_handler_mask);
        }
        state->kernel_callout_order_mask = callout_order_mask;
        state->kernel_callout_handler_mask = callout_handler_mask;
    }

    state->kernel_callout_status_checksum = state->kernel_callout_version ^
        state->kernel_callout_count ^ state->kernel_callout_required_mask ^
        state->kernel_callout_order_mask ^ state->kernel_callout_handler_mask ^
        state->kernel_callout_required_service_mask ^ state->kernel_callout_observed_service_mask ^
        state->kernel_callout0_descriptor_id ^ state->kernel_callout1_descriptor_id ^
        state->kernel_callout2_descriptor_id ^ state->kernel_callout3_descriptor_id ^
        state->kernel_callout0_required_services ^ state->kernel_callout1_required_services ^
        state->kernel_callout2_required_services ^ state->kernel_callout3_required_services ^
        state->kernel_callout0_status ^ state->kernel_callout1_status ^
        state->kernel_callout2_status ^ state->kernel_callout3_status;

    if (state->startup_entry_status == STAGE36_BOOTSTRAP_STATUS_OK &&
        state->kernel_callout_version == STAGE36_KERNEL_CALLOUT_VERSION &&
        state->kernel_callout_count == STAGE36_KERNEL_CALLOUT_COUNT &&
        state->kernel_callout_required_mask == STAGE36_KERNEL_CALLOUT_REQUIRED_MASK &&
        state->kernel_callout_order_mask == STAGE36_KERNEL_CALLOUT_REQUIRED_MASK &&
        state->kernel_callout_handler_mask == STAGE36_KERNEL_CALLOUT_REQUIRED_MASK &&
        state->kernel_callout_required_service_mask == STAGE36_KERNEL_CALLOUT_REQUIRED_SERVICES &&
        state->kernel_callout_observed_service_mask == STAGE36_SERVICE_REQUIRED_MASK &&
        state->kernel_callout0_descriptor_id == STAGE36_KERNEL_CALLOUT_BOOTSTRAP &&
        state->kernel_callout1_descriptor_id == STAGE36_KERNEL_CALLOUT_PLATFORM &&
        state->kernel_callout2_descriptor_id == STAGE36_KERNEL_CALLOUT_TIMEBASE &&
        state->kernel_callout3_descriptor_id == STAGE36_KERNEL_CALLOUT_INTERRUPTS &&
        state->kernel_callout0_required_services == STAGE36_SERVICE_BIT_LOGGING &&
        state->kernel_callout1_required_services == STAGE36_SERVICE_BIT_PLATFORM &&
        state->kernel_callout2_required_services == STAGE36_SERVICE_BIT_TIMEBASE &&
        state->kernel_callout3_required_services == STAGE36_SERVICE_BIT_INTERRUPTS &&
        state->kernel_callout0_status == STAGE36_BOOTSTRAP_STATUS_OK &&
        state->kernel_callout1_status == STAGE36_BOOTSTRAP_STATUS_OK &&
        state->kernel_callout2_status == STAGE36_BOOTSTRAP_STATUS_OK &&
        state->kernel_callout3_status == STAGE36_BOOTSTRAP_STATUS_OK &&
        state->kernel_callout_status_checksum == (STAGE36_KERNEL_CALLOUT_VERSION ^
            STAGE36_KERNEL_CALLOUT_COUNT ^ STAGE36_KERNEL_CALLOUT_REQUIRED_MASK ^
            STAGE36_KERNEL_CALLOUT_REQUIRED_MASK ^ STAGE36_KERNEL_CALLOUT_REQUIRED_MASK ^
            STAGE36_KERNEL_CALLOUT_REQUIRED_SERVICES ^ STAGE36_SERVICE_REQUIRED_MASK ^
            STAGE36_KERNEL_CALLOUT_BOOTSTRAP ^ STAGE36_KERNEL_CALLOUT_PLATFORM ^
            STAGE36_KERNEL_CALLOUT_TIMEBASE ^ STAGE36_KERNEL_CALLOUT_INTERRUPTS ^
            STAGE36_SERVICE_BIT_LOGGING ^ STAGE36_SERVICE_BIT_PLATFORM ^
            STAGE36_SERVICE_BIT_TIMEBASE ^ STAGE36_SERVICE_BIT_INTERRUPTS ^
            STAGE36_BOOTSTRAP_STATUS_OK ^ STAGE36_BOOTSTRAP_STATUS_OK ^
            STAGE36_BOOTSTRAP_STATUS_OK ^ STAGE36_BOOTSTRAP_STATUS_OK)) {
        state->kernel_callout_status = STAGE36_BOOTSTRAP_STATUS_OK;
        xnu_log_puts("high startup_entry kernel callout table ok\n");
    } else {
        state->kernel_callout_status = STAGE36_BOOTSTRAP_STATUS_BASE | STAGE36_FAIL_KERNEL_CALLOUT;
        xnu_log_puts("high startup_entry kernel callout table failed\n");
    }

    xnu_log_puts("high startup_entry kernel context begin\n");
    memset((void *)(uintptr_t)context, 0, sizeof(*context));
    context->version = STAGE36_KERNEL_CONTEXT_VERSION;
    context->size = sizeof(*context);
    context->boot_args_virt = state->startup_entry_observed_boot_args_virt;
    context->dt_virt = state->startup_entry_observed_dt_virt;
    context->platform_status = state->platform_result_status;
    context->platform_consistency = state->platform_result_consistency;
    context->timebase_freq = state->startup_entry_observed_timebase_freq;
    context->interrupt_mask = state->startup_entry_observed_interrupt_mask;
    context->callout_status = state->kernel_callout_status;
    context->callout_mask = state->kernel_callout_handler_mask;
    context->root_steps = state->startup_entry_observed_root_steps;
    if (context->boot_args_virt == state->startup_entry_required_boot_args_virt) {
        context->satisfied_mask |= STAGE36_KERNEL_CONTEXT_SAT_BOOT_ARGS;
    }
    if (context->dt_virt == state->startup_entry_required_dt_virt) {
        context->satisfied_mask |= STAGE36_KERNEL_CONTEXT_SAT_DT;
    }
    if (context->platform_status == STAGE36_BOOTSTRAP_STATUS_OK &&
        context->platform_consistency == STAGE36_PLATFORM_CONSIST_REQUIRED) {
        context->satisfied_mask |= STAGE36_KERNEL_CONTEXT_SAT_PLATFORM;
    }
    if (context->timebase_freq == 19200000u) {
        context->satisfied_mask |= STAGE36_KERNEL_CONTEXT_SAT_TIMEBASE;
    }
    if (context->interrupt_mask == STAGE36_LAUNCH_IRQ_READY_REQUIRED) {
        context->satisfied_mask |= STAGE36_KERNEL_CONTEXT_SAT_INTERRUPTS;
    }
    if (context->callout_status == STAGE36_BOOTSTRAP_STATUS_OK &&
        context->callout_mask == STAGE36_KERNEL_CALLOUT_REQUIRED_MASK) {
        context->satisfied_mask |= STAGE36_KERNEL_CONTEXT_SAT_CALLOUTS;
    }
    context->checksum = stage36_kernel_context_checksum(context);
    if (context->version == STAGE36_KERNEL_CONTEXT_VERSION &&
        context->size == sizeof(*context) &&
        context->satisfied_mask == STAGE36_KERNEL_CONTEXT_SAT_REQUIRED &&
        context->checksum == (STAGE36_KERNEL_CONTEXT_VERSION ^ sizeof(*context) ^
            state->startup_entry_required_boot_args_virt ^ state->startup_entry_required_dt_virt ^
            STAGE36_BOOTSTRAP_STATUS_OK ^ STAGE36_PLATFORM_CONSIST_REQUIRED ^
            19200000u ^ STAGE36_LAUNCH_IRQ_READY_REQUIRED ^ STAGE36_BOOTSTRAP_STATUS_OK ^
            STAGE36_KERNEL_CALLOUT_REQUIRED_MASK ^ STAGE36_ENTRY_REQUIRED_ROOT_STEPS ^
            STAGE36_KERNEL_CONTEXT_SAT_REQUIRED)) {
        context->status = STAGE36_BOOTSTRAP_STATUS_OK;
        xnu_log_puts("high startup_entry kernel context ok\n");
    } else {
        context->status = STAGE36_BOOTSTRAP_STATUS_BASE | STAGE36_FAIL_KERNEL_CONTEXT;
        xnu_log_puts("high startup_entry kernel context failed\n");
    }

    state->kernel_context_version = context->version;
    state->kernel_context_size = context->size;
    state->kernel_context_required_mask = STAGE36_KERNEL_CONTEXT_SAT_REQUIRED;
    state->kernel_context_satisfied_mask = context->satisfied_mask;
    state->kernel_context_checksum = context->checksum;
    state->kernel_context_status = context->status;

    return context->status;
}

static uint32_t stage36_kernel_root(struct boot_args *args,
                                    volatile struct pe_platform_state *pe,
                                    volatile struct stage36_bootstrap_state *state)
    __attribute__((noinline));

static uint32_t stage36_kernel_root(struct boot_args *args,
                                    volatile struct pe_platform_state *pe,
                                    volatile struct stage36_bootstrap_state *state)
{
    volatile uint32_t *gicd_ctlr = (volatile uint32_t *)(uintptr_t)pe->gicDistributorBase;
    volatile uint32_t *gicd_typer = (volatile uint32_t *)(uintptr_t)(pe->gicDistributorBase + 0x004u);
    volatile uint32_t *gicc_ctlr = (volatile uint32_t *)(uintptr_t)pe->gicCpuBase;
    const void *dt_high = (const void *)(uintptr_t)(STAGE36_HIGH_ALIAS_BASE + (uint32_t)(uintptr_t)args->deviceTreeP);
    const void *memory_node;
    const void *timer_node;
    const void *cpus_node;
    const uint32_t *memory_reg;
    uint32_t memory_reg_len = 0;
    uint32_t rev_ver = ((uint32_t)args->Version << 16) | args->Revision;
    uint32_t failures = 0;
    uint32_t root_steps = 0;
    uint32_t steps = 0;
    uint32_t service_dispatcher_order_mask = 0;
    uint32_t service_dispatcher_available_mask = 0;
    uint32_t service_dispatcher_handler_mask = 0;
    uint32_t dispatcher_order_mask = 0;
    uint32_t dispatcher_completed_mask = 0;
    uint32_t dispatcher_handler_mask = 0;
    uint32_t manifest_order_mask = 0;
    uint32_t manifest_satisfied_mask = 0;
    uint32_t manifest_handler_mask = 0;
    uint32_t typer;
    uint64_t t0;
    uint64_t t1;
    uint32_t startup_entry_fn_phys = (uint32_t)(uintptr_t)stage36_startup_entry;
    uint32_t startup_handoff_phys = (uint32_t)(uintptr_t)&stage36_startup_handoff_block;
    uint32_t kernel_context_phys = (uint32_t)(uintptr_t)&stage36_kernel_context_block;
    uint32_t vm_plan_phys = (uint32_t)(uintptr_t)&stage36_vm_plan_block;
    uint32_t vm_state_phys = (uint32_t)(uintptr_t)&stage36_vm_state_block;
    uint32_t allocator_phys = (uint32_t)(uintptr_t)&stage36_boot_allocator_block;
    stage36_startup_entry_fn_t startup_entry_fn =
        (stage36_startup_entry_fn_t)(uintptr_t)(STAGE36_HIGH_ALIAS_BASE + startup_entry_fn_phys);
    volatile struct stage36_startup_handoff *startup_handoff =
        (volatile struct stage36_startup_handoff *)(uintptr_t)(STAGE36_HIGH_ALIAS_BASE + startup_handoff_phys);
    volatile struct stage36_kernel_context *kernel_context =
        (volatile struct stage36_kernel_context *)(uintptr_t)(STAGE36_HIGH_ALIAS_BASE + kernel_context_phys);
    volatile struct stage36_vm_bootstrap_plan *vm_plan =
        (volatile struct stage36_vm_bootstrap_plan *)(uintptr_t)(STAGE36_HIGH_ALIAS_BASE + vm_plan_phys);
    volatile struct stage36_vm_bootstrap_state *vm_state =
        (volatile struct stage36_vm_bootstrap_state *)(uintptr_t)(STAGE36_HIGH_ALIAS_BASE + vm_state_phys);
    volatile struct stage36_bootstrap_allocator *allocator =
        (volatile struct stage36_bootstrap_allocator *)(uintptr_t)(STAGE36_HIGH_ALIAS_BASE + allocator_phys);
    uint32_t startup_handoff_root_status = STAGE36_BOOTSTRAP_STATUS_BASE;
    uint32_t startup_handoff_checksum = 0;
    uint32_t startup_entry_result = STAGE36_BOOTSTRAP_STATUS_BASE;

    xnu_log_puts("high virtual kernel_root entered\n");
    xnu_log_kv32("high_startup_entry_fn_virt", (uint32_t)(uintptr_t)startup_entry_fn);
    xnu_log_kv32("high_startup_handoff_virt", (uint32_t)(uintptr_t)startup_handoff);
    xnu_log_kv32("high_kernel_context_virt", (uint32_t)(uintptr_t)kernel_context);
    xnu_log_kv32("high_vm_plan_virt", (uint32_t)(uintptr_t)vm_plan);
    xnu_log_kv32("high_vm_state_virt", (uint32_t)(uintptr_t)vm_state);
    xnu_log_kv32("high_allocator_virt", (uint32_t)(uintptr_t)allocator);
    root_steps |= STAGE36_ROOT_STEP_ENTER;
    xnu_log_kv32("high_root_steps", root_steps);
    xnu_log_puts("high root owns init sequence\n");
    xnu_log_puts("high init sequence begin\n");

    state->magic = 0x36003600u;
    state->boot_args_rev_ver = rev_ver;
    state->machine_type = args->machineType;
    state->device_tree_length = args->deviceTreeLength;
    state->pe_memory_base = pe->memoryBase;
    state->pe_memory_size = pe->memorySize;
    state->pe_cpu_count = pe->cpuCount;
    state->pe_gic_dist_base = pe->gicDistributorBase;
    state->pe_gic_cpu_base = pe->gicCpuBase;
    state->pe_timer_base = pe->timerBase;
    state->pe_timer_frequency = pe->timerFrequency;
    state->pe_vector_base = pe->vectorBase;
    state->boot_flags = args->bootFlags;
    state->validation_mask = 0;
    state->root_steps = root_steps;
    state->root_status = STAGE36_BOOTSTRAP_STATUS_BASE;
    state->init_steps = 0;
    state->init_status = STAGE36_BOOTSTRAP_STATUS_BASE;
    state->init_timebase_freq = 0;
    state->init_timebase_delta_us = 0;
    state->init_gic_irq_count = 0;
    state->init_gic_cpu_count = 0;
    state->init_gic_dist_ctlr = 0;
    state->init_gic_cpu_ctlr = 0;
    state->root_boot_args_virt = (uint32_t)(uintptr_t)args;
    state->root_dt_virt = (uint32_t)(uintptr_t)dt_high;
    state->root_dt_root_children = 0;
    state->root_dt_memory_base = 0;
    state->root_dt_memory_size = 0;
    state->root_dt_timer_frequency = 0;
    state->root_dt_summary_status = STAGE36_BOOTSTRAP_STATUS_BASE;
    state->platform_result_version = STAGE36_PLATFORM_RESULT_VERSION;
    state->platform_result_size = sizeof(*state);
    state->platform_result_consistency = 0;
    state->platform_dt_cpu_count = 0;
    state->platform_pe_cpu_count = 0;
    state->platform_memory_base = 0;
    state->platform_memory_size = 0;
    state->platform_gic_dist_base = 0;
    state->platform_gic_cpu_base = 0;
    state->platform_timer_frequency = 0;
    state->platform_result_status = STAGE36_BOOTSTRAP_STATUS_BASE;
    state->phase_count = STAGE36_PHASE_COUNT;
    state->phase_completed_mask = 0;
    state->phase_status_checksum = 0;
    state->phase0_status = STAGE36_BOOTSTRAP_STATUS_BASE;
    state->phase1_status = STAGE36_BOOTSTRAP_STATUS_BASE;
    state->phase2_status = STAGE36_BOOTSTRAP_STATUS_BASE;
    state->phase3_status = STAGE36_BOOTSTRAP_STATUS_BASE;
    state->phase0_required_services = STAGE36_PHASE_VALIDATE_SERVICES;
    state->phase1_required_services = STAGE36_PHASE_DT_SUMMARY_SERVICES;
    state->phase2_required_services = STAGE36_PHASE_PLATFORM_RESULT_SERVICES;
    state->phase3_required_services = STAGE36_PHASE_RETURN_READY_SERVICES;
    state->phase_service_dependency_mask = STAGE36_PHASE_SERVICE_REQUIRED_MASK;
    state->phase_service_satisfied_mask = 0;
    state->phase_service_status_checksum = 0;
    state->phase_service_dependency_status = STAGE36_BOOTSTRAP_STATUS_BASE;
    state->phase_dispatcher_count = STAGE36_PHASE_COUNT;
    state->phase_dispatcher_order_mask = 0;
    state->phase_dispatcher_handler_mask = 0;
    state->phase_dispatcher_status_checksum = 0;
    state->phase0_descriptor_id = STAGE36_PHASE_VALIDATE;
    state->phase1_descriptor_id = STAGE36_PHASE_DT_SUMMARY;
    state->phase2_descriptor_id = STAGE36_PHASE_PLATFORM_RESULT;
    state->phase3_descriptor_id = STAGE36_PHASE_RETURN_READY;
    state->phase0_handler_result = STAGE36_BOOTSTRAP_STATUS_BASE;
    state->phase1_handler_result = STAGE36_BOOTSTRAP_STATUS_BASE;
    state->phase2_handler_result = STAGE36_BOOTSTRAP_STATUS_BASE;
    state->phase3_handler_result = STAGE36_BOOTSTRAP_STATUS_BASE;
    state->phase_dispatcher_status = STAGE36_BOOTSTRAP_STATUS_BASE;
    state->service_count = STAGE36_SERVICE_COUNT;
    state->service_available_mask = 0;
    state->service_status_checksum = 0;
    state->service_logging_status = STAGE36_BOOTSTRAP_STATUS_BASE;
    state->service_timebase_status = STAGE36_BOOTSTRAP_STATUS_BASE;
    state->service_platform_status = STAGE36_BOOTSTRAP_STATUS_BASE;
    state->service_interrupts_status = STAGE36_BOOTSTRAP_STATUS_BASE;
    state->service_dispatcher_count = STAGE36_SERVICE_COUNT;
    state->service_dispatcher_order_mask = 0;
    state->service_dispatcher_handler_mask = 0;
    state->service_dispatcher_status_checksum = 0;
    state->service_logging_descriptor_id = STAGE36_SERVICE_LOGGING;
    state->service_timebase_descriptor_id = STAGE36_SERVICE_TIMEBASE;
    state->service_platform_descriptor_id = STAGE36_SERVICE_PLATFORM;
    state->service_interrupts_descriptor_id = STAGE36_SERVICE_INTERRUPTS;
    state->service_logging_handler_result = STAGE36_BOOTSTRAP_STATUS_BASE;
    state->service_timebase_handler_result = STAGE36_BOOTSTRAP_STATUS_BASE;
    state->service_platform_handler_result = STAGE36_BOOTSTRAP_STATUS_BASE;
    state->service_interrupts_handler_result = STAGE36_BOOTSTRAP_STATUS_BASE;
    state->service_dispatcher_status = STAGE36_BOOTSTRAP_STATUS_BASE;
    state->registry_version = STAGE36_REGISTRY_VERSION;
    state->registry_size = sizeof(*state);
    state->registry_service_descriptor_mask = 0;
    state->registry_phase_descriptor_mask = 0;
    state->registry_dependency_coverage_mask = 0;
    state->registry_dispatch_coverage_mask = 0;
    state->registry_status_checksum = 0;
    state->registry_status = STAGE36_BOOTSTRAP_STATUS_BASE;
    state->boot_policy_version = STAGE36_BOOT_POLICY_VERSION;
    state->boot_policy_size = sizeof(*state);
    state->boot_policy_required_root_steps = STAGE36_BOOT_POLICY_REQUIRED_ROOT_STEPS;
    state->boot_policy_required_service_mask = STAGE36_SERVICE_REQUIRED_MASK;
    state->boot_policy_required_phase_mask = STAGE36_PHASE_REQUIRED_MASK;
    state->boot_policy_required_dependency_mask = STAGE36_PHASE_SERVICE_REQUIRED_MASK;
    state->boot_policy_required_dispatch_coverage_mask = STAGE36_REGISTRY_DISPATCH_COVERAGE_REQUIRED;
    state->boot_policy_required_registry_status = STAGE36_BOOTSTRAP_STATUS_OK;
    state->boot_policy_observed_root_steps = 0;
    state->boot_policy_observed_service_mask = 0;
    state->boot_policy_observed_phase_mask = 0;
    state->boot_policy_observed_dependency_mask = 0;
    state->boot_policy_observed_dispatch_coverage_mask = 0;
    state->boot_policy_observed_registry_status = STAGE36_BOOTSTRAP_STATUS_BASE;
    state->boot_policy_satisfied_mask = 0;
    state->boot_policy_status_checksum = 0;
    state->boot_policy_status = STAGE36_BOOTSTRAP_STATUS_BASE;
    state->manifest_version = STAGE36_MANIFEST_VERSION;
    state->manifest_record_count = STAGE36_MANIFEST_RECORD_COUNT;
    state->manifest_required_record_mask = STAGE36_MANIFEST_RECORD_REQUIRED_MASK;
    state->manifest_order_mask = 0;
    state->manifest_satisfied_mask = 0;
    state->manifest_required_root_steps = STAGE36_MANIFEST_REQUIRED_ROOT_STEPS;
    state->manifest_observed_root_steps = 0;
    state->manifest_service_required_mask = STAGE36_SERVICE_REQUIRED_MASK;
    state->manifest_service_observed_mask = 0;
    state->manifest_service_status = STAGE36_BOOTSTRAP_STATUS_BASE;
    state->manifest_phase_required_mask = STAGE36_PHASE_REQUIRED_MASK;
    state->manifest_phase_observed_mask = 0;
    state->manifest_phase_status = STAGE36_BOOTSTRAP_STATUS_BASE;
    state->manifest_dependency_required_mask = STAGE36_PHASE_SERVICE_REQUIRED_MASK;
    state->manifest_dependency_observed_mask = 0;
    state->manifest_dependency_status = STAGE36_BOOTSTRAP_STATUS_BASE;
    state->manifest_dispatch_required_mask = STAGE36_REGISTRY_DISPATCH_COVERAGE_REQUIRED;
    state->manifest_dispatch_observed_mask = 0;
    state->manifest_dispatch_status = STAGE36_BOOTSTRAP_STATUS_BASE;
    state->manifest_policy_required_mask = STAGE36_BOOT_POLICY_SAT_REQUIRED;
    state->manifest_policy_observed_mask = 0;
    state->manifest_policy_status = STAGE36_BOOTSTRAP_STATUS_BASE;
    state->manifest_boot_required_status = STAGE36_BOOTSTRAP_STATUS_OK;
    state->manifest_boot_observed_status = STAGE36_BOOTSTRAP_STATUS_BASE;
    state->manifest_boot_status_record_status = STAGE36_BOOTSTRAP_STATUS_BASE;
    state->manifest_status_checksum = 0;
    state->manifest_status = STAGE36_BOOTSTRAP_STATUS_BASE;
    state->launch_contract_version = STAGE36_LAUNCH_CONTRACT_VERSION;
    state->launch_contract_size = sizeof(*state);
    state->launch_required_root_steps = STAGE36_LAUNCH_REQUIRED_ROOT_STEPS;
    state->launch_observed_root_steps = 0;
    state->launch_required_root_status = STAGE36_BOOTSTRAP_STATUS_OK;
    state->launch_observed_root_status = STAGE36_BOOTSTRAP_STATUS_BASE;
    state->launch_required_manifest_status = STAGE36_BOOTSTRAP_STATUS_OK;
    state->launch_observed_manifest_status = STAGE36_BOOTSTRAP_STATUS_BASE;
    state->launch_required_policy_status = STAGE36_BOOTSTRAP_STATUS_OK;
    state->launch_observed_policy_status = STAGE36_BOOTSTRAP_STATUS_BASE;
    state->launch_required_mmu_state = STAGE36_LAUNCH_MMU_ENABLED;
    state->launch_observed_mmu_state = 0;
    state->launch_required_timebase_freq = 19200000u;
    state->launch_observed_timebase_freq = 0;
    state->launch_required_interrupt_mask = STAGE36_LAUNCH_IRQ_READY_REQUIRED;
    state->launch_observed_interrupt_mask = 0;
    state->launch_satisfied_mask = 0;
    state->launch_status_checksum = 0;
    state->launch_status = STAGE36_BOOTSTRAP_STATUS_BASE;
    state->startup_boundary_version = STAGE36_STARTUP_BOUNDARY_VERSION;
    state->startup_boundary_size = sizeof(*state);
    state->startup_required_root_steps = STAGE36_STARTUP_REQUIRED_ROOT_STEPS;
    state->startup_observed_root_steps = 0;
    state->startup_required_launch_status = STAGE36_BOOTSTRAP_STATUS_OK;
    state->startup_observed_launch_status = STAGE36_BOOTSTRAP_STATUS_BASE;
    state->startup_required_root_status = STAGE36_BOOTSTRAP_STATUS_OK;
    state->startup_observed_root_status = STAGE36_BOOTSTRAP_STATUS_BASE;
    state->startup_required_manifest_status = STAGE36_BOOTSTRAP_STATUS_OK;
    state->startup_observed_manifest_status = STAGE36_BOOTSTRAP_STATUS_BASE;
    state->startup_required_boot_args_virt = (uint32_t)(uintptr_t)args;
    state->startup_observed_boot_args_virt = 0;
    state->startup_required_dt_virt = (uint32_t)(uintptr_t)dt_high;
    state->startup_observed_dt_virt = 0;
    state->startup_required_timebase_freq = 19200000u;
    state->startup_observed_timebase_freq = 0;
    state->startup_required_interrupt_mask = STAGE36_LAUNCH_IRQ_READY_REQUIRED;
    state->startup_observed_interrupt_mask = 0;
    state->startup_satisfied_mask = 0;
    state->startup_status_checksum = 0;
    state->startup_status = STAGE36_BOOTSTRAP_STATUS_BASE;
    state->startup_routine_version = STAGE36_STARTUP_ROUTINE_VERSION;
    state->startup_routine_size = sizeof(*state);
    state->startup_routine_required_root_steps = STAGE36_ROUTINE_REQUIRED_ROOT_STEPS;
    state->startup_routine_observed_root_steps = 0;
    state->startup_routine_required_startup_status = STAGE36_BOOTSTRAP_STATUS_OK;
    state->startup_routine_observed_startup_status = STAGE36_BOOTSTRAP_STATUS_BASE;
    state->startup_routine_required_launch_status = STAGE36_BOOTSTRAP_STATUS_OK;
    state->startup_routine_observed_launch_status = STAGE36_BOOTSTRAP_STATUS_BASE;
    state->startup_routine_required_root_status = STAGE36_BOOTSTRAP_STATUS_OK;
    state->startup_routine_observed_root_status = STAGE36_BOOTSTRAP_STATUS_BASE;
    state->startup_routine_required_boot_args_virt = (uint32_t)(uintptr_t)args;
    state->startup_routine_observed_boot_args_virt = 0;
    state->startup_routine_required_dt_virt = (uint32_t)(uintptr_t)dt_high;
    state->startup_routine_observed_dt_virt = 0;
    state->startup_routine_required_timebase_freq = 19200000u;
    state->startup_routine_observed_timebase_freq = 0;
    state->startup_routine_required_interrupt_mask = STAGE36_LAUNCH_IRQ_READY_REQUIRED;
    state->startup_routine_observed_interrupt_mask = 0;
    state->startup_routine_satisfied_mask = 0;
    state->startup_routine_status_checksum = 0;
    state->startup_routine_status = STAGE36_BOOTSTRAP_STATUS_BASE;
    state->startup_entry_version = STAGE36_STARTUP_ENTRY_VERSION;
    state->startup_entry_size = sizeof(*state);
    state->startup_entry_required_root_steps = STAGE36_ENTRY_REQUIRED_ROOT_STEPS;
    state->startup_entry_observed_root_steps = 0;
    state->startup_entry_required_routine_status = STAGE36_BOOTSTRAP_STATUS_OK;
    state->startup_entry_observed_routine_status = STAGE36_BOOTSTRAP_STATUS_BASE;
    state->startup_entry_required_startup_status = STAGE36_BOOTSTRAP_STATUS_OK;
    state->startup_entry_observed_startup_status = STAGE36_BOOTSTRAP_STATUS_BASE;
    state->startup_entry_required_root_status = STAGE36_BOOTSTRAP_STATUS_OK;
    state->startup_entry_observed_root_status = STAGE36_BOOTSTRAP_STATUS_BASE;
    state->startup_entry_required_boot_args_virt = (uint32_t)(uintptr_t)args;
    state->startup_entry_observed_boot_args_virt = 0;
    state->startup_entry_required_dt_virt = (uint32_t)(uintptr_t)dt_high;
    state->startup_entry_observed_dt_virt = 0;
    state->startup_entry_required_timebase_freq = 19200000u;
    state->startup_entry_observed_timebase_freq = 0;
    state->startup_entry_required_interrupt_mask = STAGE36_LAUNCH_IRQ_READY_REQUIRED;
    state->startup_entry_observed_interrupt_mask = 0;
    state->startup_entry_satisfied_mask = 0;
    state->startup_entry_status_checksum = 0;
    state->startup_entry_status = STAGE36_BOOTSTRAP_STATUS_BASE;
    state->kernel_callout_version = STAGE36_KERNEL_CALLOUT_VERSION;
    state->kernel_callout_count = STAGE36_KERNEL_CALLOUT_COUNT;
    state->kernel_callout_required_mask = STAGE36_KERNEL_CALLOUT_REQUIRED_MASK;
    state->kernel_callout_order_mask = 0;
    state->kernel_callout_handler_mask = 0;
    state->kernel_callout_required_service_mask = STAGE36_KERNEL_CALLOUT_REQUIRED_SERVICES;
    state->kernel_callout_observed_service_mask = 0;
    state->kernel_callout_status_checksum = 0;
    state->kernel_callout0_descriptor_id = STAGE36_KERNEL_CALLOUT_BOOTSTRAP;
    state->kernel_callout1_descriptor_id = STAGE36_KERNEL_CALLOUT_PLATFORM;
    state->kernel_callout2_descriptor_id = STAGE36_KERNEL_CALLOUT_TIMEBASE;
    state->kernel_callout3_descriptor_id = STAGE36_KERNEL_CALLOUT_INTERRUPTS;
    state->kernel_callout0_required_services = STAGE36_SERVICE_BIT_LOGGING;
    state->kernel_callout1_required_services = STAGE36_SERVICE_BIT_PLATFORM;
    state->kernel_callout2_required_services = STAGE36_SERVICE_BIT_TIMEBASE;
    state->kernel_callout3_required_services = STAGE36_SERVICE_BIT_INTERRUPTS;
    state->kernel_callout0_status = STAGE36_BOOTSTRAP_STATUS_BASE;
    state->kernel_callout1_status = STAGE36_BOOTSTRAP_STATUS_BASE;
    state->kernel_callout2_status = STAGE36_BOOTSTRAP_STATUS_BASE;
    state->kernel_callout3_status = STAGE36_BOOTSTRAP_STATUS_BASE;
    state->kernel_callout_status = STAGE36_BOOTSTRAP_STATUS_BASE;
    state->kernel_context_version = STAGE36_KERNEL_CONTEXT_VERSION;
    state->kernel_context_size = sizeof(struct stage36_kernel_context);
    state->kernel_context_required_mask = STAGE36_KERNEL_CONTEXT_SAT_REQUIRED;
    state->kernel_context_satisfied_mask = 0;
    state->kernel_context_checksum = 0;
    state->kernel_context_status = STAGE36_BOOTSTRAP_STATUS_BASE;
    state->vm_plan_version = STAGE36_VM_PLAN_VERSION;
    state->vm_plan_size = sizeof(struct stage36_vm_bootstrap_plan);
    state->vm_plan_required_mask = STAGE36_VM_PLAN_SAT_REQUIRED;
    state->vm_plan_satisfied_mask = 0;
    state->vm_plan_checksum = 0;
    state->vm_plan_status = STAGE36_BOOTSTRAP_STATUS_BASE;
    state->vm_state_version = STAGE36_VM_STATE_VERSION;
    state->vm_state_size = sizeof(struct stage36_vm_bootstrap_state);
    state->vm_state_required_mask = STAGE36_VM_STATE_SAT_REQUIRED;
    state->vm_state_satisfied_mask = 0;
    state->vm_state_checksum = 0;
    state->vm_state_status = STAGE36_BOOTSTRAP_STATUS_BASE;
    state->allocator_version = STAGE36_BOOT_ALLOCATOR_VERSION;
    state->allocator_size = sizeof(struct stage36_bootstrap_allocator);
    state->allocator_required_mask = STAGE36_BOOT_ALLOCATOR_SAT_REQUIRED;
    state->allocator_satisfied_mask = 0;
    state->allocator_checksum = 0;
    state->allocator_status = STAGE36_BOOTSTRAP_STATUS_BASE;
    state->status = STAGE36_BOOTSTRAP_STATUS_BASE;
    state->checksum = 0;

    xnu_log_puts("high init step validate begin\n");
    if (rev_ver != 0x00020002u) failures |= STAGE36_FAIL_REV_VER;
    if (args->machineType != MACHINE_TYPE_MSM8974) failures |= STAGE36_FAIL_MACHINE;
    if (!args->deviceTreeP || args->deviceTreeLength < 0x200u) failures |= STAGE36_FAIL_DT;
    if (pe->memoryBase != RAM_PHYS_BASE || pe->memorySize != (RAM_CONSOLE_BASE - RAM_PHYS_BASE)) failures |= STAGE36_FAIL_MEMORY;
    if (pe->cpuCount != 4u) failures |= STAGE36_FAIL_CPU;
    if (pe->gicDistributorBase != 0xf9000000u || pe->gicCpuBase != 0xf9002000u) failures |= STAGE36_FAIL_GIC;
    if (pe->timerBase != 0xf9020000u || pe->timerFrequency != 19200000u) failures |= STAGE36_FAIL_TIMER;
    if (pe->vectorBase != (uint32_t)(uintptr_t)stage36_vectors) failures |= STAGE36_FAIL_VECTOR;
    if ((failures & 0x000000ffu) == 0u) {
        steps |= STAGE36_INIT_STEP_VALIDATE;
        xnu_log_puts("high init step validate ok\n");
    } else {
        xnu_log_puts("high init step validate failed\n");
    }

    xnu_log_puts("high init step timebase begin\n");
    ml_init_timebase();
    t0 = ml_get_timebase();
    delay_us(1000);
    t1 = ml_get_timebase();
    state->init_timebase_freq = ml_get_timebase_frequency();
    state->init_timebase_delta_us = timebase_elapsed_us(t0, t1);
    if (state->init_timebase_freq == 19200000u && state->init_timebase_delta_us >= 900u) {
        steps |= STAGE36_INIT_STEP_TIMEBASE;
        xnu_log_puts("high init step timebase ok\n");
    } else {
        failures |= STAGE36_FAIL_TIMEBASE_INIT;
        xnu_log_puts("high init step timebase failed\n");
    }

    xnu_log_puts("high init step gic summary begin\n");
    typer = *gicd_typer;
    state->init_gic_dist_ctlr = *gicd_ctlr;
    state->init_gic_cpu_ctlr = *gicc_ctlr;
    state->init_gic_irq_count = ((typer & 0x1fu) + 1u) * 32u;
    state->init_gic_cpu_count = ((typer >> 5) & 0x7u) + 1u;
    if ((state->init_gic_dist_ctlr & 1u) != 0u &&
        (state->init_gic_cpu_ctlr & 1u) != 0u &&
        state->init_gic_irq_count >= 288u &&
        state->init_gic_cpu_count == 4u) {
        steps |= STAGE36_INIT_STEP_GIC_SUMMARY;
        xnu_log_puts("high init step gic summary ok\n");
    } else {
        failures |= STAGE36_FAIL_GIC_SUMMARY;
        xnu_log_puts("high init step gic summary failed\n");
    }

    xnu_log_puts("high root dt summary begin\n");
    state->root_dt_root_children = apple_dt_node_child_count(dt_high, args->deviceTreeLength, dt_high);
    memory_node = apple_dt_find_child(dt_high, args->deviceTreeLength, dt_high, "memory");
    timer_node = apple_dt_find_child(dt_high, args->deviceTreeLength, dt_high, "timer");
    cpus_node = apple_dt_find_child(dt_high, args->deviceTreeLength, dt_high, "cpus");
    memory_reg = (const uint32_t *)apple_dt_get_prop(dt_high, args->deviceTreeLength, memory_node, "reg", &memory_reg_len);
    if (memory_reg && memory_reg_len >= 8u) {
        state->root_dt_memory_base = memory_reg[0];
        state->root_dt_memory_size = memory_reg[1];
    }
    state->root_dt_timer_frequency = apple_dt_get_u32_prop(dt_high, args->deviceTreeLength, timer_node, "frequency", 0);
    if ((uint32_t)(uintptr_t)args >= STAGE36_HIGH_ALIAS_BASE &&
        state->root_dt_virt >= STAGE36_HIGH_ALIAS_BASE &&
        state->root_dt_root_children == 6u &&
        state->root_dt_memory_base == RAM_PHYS_BASE &&
        state->root_dt_memory_size == (RAM_CONSOLE_BASE - RAM_PHYS_BASE) &&
        state->root_dt_timer_frequency == 19200000u) {
        root_steps |= STAGE36_ROOT_STEP_DT_SUMMARY;
        state->root_dt_summary_status = STAGE36_BOOTSTRAP_STATUS_OK;
        xnu_log_puts("high root dt summary ok\n");
    } else {
        failures |= STAGE36_FAIL_DT_SUMMARY;
        state->root_dt_summary_status = STAGE36_BOOTSTRAP_STATUS_BASE | STAGE36_FAIL_DT_SUMMARY;
        xnu_log_puts("high root dt summary failed\n");
    }

    xnu_log_puts("high root platform result begin\n");
    state->platform_dt_cpu_count = apple_dt_node_child_count(dt_high, args->deviceTreeLength, cpus_node);
    state->platform_pe_cpu_count = pe->cpuCount;
    state->platform_memory_base = pe->memoryBase;
    state->platform_memory_size = pe->memorySize;
    state->platform_gic_dist_base = pe->gicDistributorBase;
    state->platform_gic_cpu_base = pe->gicCpuBase;
    state->platform_timer_frequency = pe->timerFrequency;
    if (state->platform_memory_base == state->root_dt_memory_base &&
        state->platform_memory_size == state->root_dt_memory_size) {
        state->platform_result_consistency |= STAGE36_PLATFORM_CONSIST_MEMORY;
    }
    if (state->platform_dt_cpu_count == 4u && state->platform_pe_cpu_count == 4u) {
        state->platform_result_consistency |= STAGE36_PLATFORM_CONSIST_CPU;
    }
    if (state->platform_gic_dist_base == 0xf9000000u && state->platform_gic_cpu_base == 0xf9002000u) {
        state->platform_result_consistency |= STAGE36_PLATFORM_CONSIST_GIC;
    }
    if (state->platform_timer_frequency == state->root_dt_timer_frequency && state->platform_timer_frequency == 19200000u) {
        state->platform_result_consistency |= STAGE36_PLATFORM_CONSIST_TIMER;
    }
    if (state->platform_result_version == STAGE36_PLATFORM_RESULT_VERSION &&
        state->platform_result_size == sizeof(*state) &&
        state->platform_result_consistency == STAGE36_PLATFORM_CONSIST_REQUIRED) {
        root_steps |= STAGE36_ROOT_STEP_PLATFORM_RESULT;
        state->platform_result_status = STAGE36_BOOTSTRAP_STATUS_OK;
        xnu_log_puts("high root platform result ok\n");
    } else {
        failures |= STAGE36_FAIL_PLATFORM_RESULT;
        state->platform_result_status = STAGE36_BOOTSTRAP_STATUS_BASE | STAGE36_FAIL_PLATFORM_RESULT;
        xnu_log_puts("high root platform result failed\n");
    }

    xnu_log_puts("high root service dispatcher begin\n");
    {
        const struct stage36_service_descriptor service_desc[STAGE36_SERVICE_COUNT] = {
            {
                STAGE36_SERVICE_LOGGING,
                1u,
                &state->service_logging_status,
                &state->service_logging_handler_result,
            },
            {
                STAGE36_SERVICE_TIMEBASE,
                (state->init_timebase_freq == 19200000u),
                &state->service_timebase_status,
                &state->service_timebase_handler_result,
            },
            {
                STAGE36_SERVICE_PLATFORM,
                (state->platform_result_status == STAGE36_BOOTSTRAP_STATUS_OK),
                &state->service_platform_status,
                &state->service_platform_handler_result,
            },
            {
                STAGE36_SERVICE_INTERRUPTS,
                ((state->init_gic_dist_ctlr & 1u) != 0u && (state->init_gic_cpu_ctlr & 1u) != 0u),
                &state->service_interrupts_status,
                &state->service_interrupts_handler_result,
            },
        };

        for (uint32_t i = 0; i < STAGE36_SERVICE_COUNT; i++) {
            uint32_t handler = stage36_dispatch_service(&service_desc[i],
                &service_dispatcher_available_mask, &service_dispatcher_order_mask);
            if (handler == STAGE36_BOOTSTRAP_STATUS_OK) {
                service_dispatcher_handler_mask |= (1u << service_desc[i].id);
            }
        }
    }
    state->service_available_mask = service_dispatcher_available_mask;
    state->service_dispatcher_order_mask = service_dispatcher_order_mask;
    state->service_dispatcher_handler_mask = service_dispatcher_handler_mask;
    state->service_dispatcher_status_checksum = state->service_dispatcher_count ^
        state->service_dispatcher_order_mask ^ state->service_dispatcher_handler_mask ^
        state->service_logging_descriptor_id ^ state->service_timebase_descriptor_id ^
        state->service_platform_descriptor_id ^ state->service_interrupts_descriptor_id ^
        state->service_logging_handler_result ^ state->service_timebase_handler_result ^
        state->service_platform_handler_result ^ state->service_interrupts_handler_result;
    if (state->service_dispatcher_count == STAGE36_SERVICE_COUNT &&
        state->service_dispatcher_order_mask == STAGE36_SERVICE_REQUIRED_MASK &&
        state->service_dispatcher_handler_mask == STAGE36_SERVICE_REQUIRED_MASK &&
        state->service_available_mask == STAGE36_SERVICE_REQUIRED_MASK &&
        state->service_logging_descriptor_id == STAGE36_SERVICE_LOGGING &&
        state->service_timebase_descriptor_id == STAGE36_SERVICE_TIMEBASE &&
        state->service_platform_descriptor_id == STAGE36_SERVICE_PLATFORM &&
        state->service_interrupts_descriptor_id == STAGE36_SERVICE_INTERRUPTS &&
        state->service_logging_handler_result == STAGE36_BOOTSTRAP_STATUS_OK &&
        state->service_timebase_handler_result == STAGE36_BOOTSTRAP_STATUS_OK &&
        state->service_platform_handler_result == STAGE36_BOOTSTRAP_STATUS_OK &&
        state->service_interrupts_handler_result == STAGE36_BOOTSTRAP_STATUS_OK &&
        state->service_dispatcher_status_checksum == (STAGE36_SERVICE_COUNT ^
            STAGE36_SERVICE_REQUIRED_MASK ^ STAGE36_SERVICE_REQUIRED_MASK ^
            STAGE36_SERVICE_LOGGING ^ STAGE36_SERVICE_TIMEBASE ^
            STAGE36_SERVICE_PLATFORM ^ STAGE36_SERVICE_INTERRUPTS ^
            STAGE36_BOOTSTRAP_STATUS_OK ^ STAGE36_BOOTSTRAP_STATUS_OK ^
            STAGE36_BOOTSTRAP_STATUS_OK ^ STAGE36_BOOTSTRAP_STATUS_OK)) {
        state->service_dispatcher_status = STAGE36_BOOTSTRAP_STATUS_OK;
        root_steps |= STAGE36_ROOT_STEP_SERVICE_DISPATCHER;
        xnu_log_puts("high root service dispatcher ok\n");
    } else {
        failures |= STAGE36_FAIL_SERVICE_DISPATCHER;
        state->service_dispatcher_status = STAGE36_BOOTSTRAP_STATUS_BASE | STAGE36_FAIL_SERVICE_DISPATCHER;
        xnu_log_puts("high root service dispatcher failed\n");
    }

    xnu_log_puts("high root service table begin\n");
    state->service_status_checksum = state->service_count ^ state->service_available_mask ^
        state->service_logging_status ^ state->service_timebase_status ^
        state->service_platform_status ^ state->service_interrupts_status;
    if (state->service_count == STAGE36_SERVICE_COUNT &&
        state->service_dispatcher_status == STAGE36_BOOTSTRAP_STATUS_OK &&
        state->service_available_mask == STAGE36_SERVICE_REQUIRED_MASK &&
        state->service_status_checksum == (STAGE36_SERVICE_COUNT ^ STAGE36_SERVICE_REQUIRED_MASK ^
            STAGE36_BOOTSTRAP_STATUS_OK ^ STAGE36_BOOTSTRAP_STATUS_OK ^
            STAGE36_BOOTSTRAP_STATUS_OK ^ STAGE36_BOOTSTRAP_STATUS_OK)) {
        root_steps |= STAGE36_ROOT_STEP_SERVICE_TABLE;
        xnu_log_puts("high root service table ok\n");
    } else {
        failures |= STAGE36_FAIL_SERVICE_TABLE;
        xnu_log_puts("high root service table failed\n");
    }

    xnu_log_puts("high root phase-service dependencies begin\n");
    state->phase_service_dependency_mask = state->phase0_required_services |
        state->phase1_required_services | state->phase2_required_services |
        state->phase3_required_services;
    if ((state->service_available_mask & state->phase0_required_services) == state->phase0_required_services) {
        state->phase_service_satisfied_mask |= (1u << STAGE36_PHASE_VALIDATE);
    }
    if ((state->service_available_mask & state->phase1_required_services) == state->phase1_required_services) {
        state->phase_service_satisfied_mask |= (1u << STAGE36_PHASE_DT_SUMMARY);
    }
    if ((state->service_available_mask & state->phase2_required_services) == state->phase2_required_services) {
        state->phase_service_satisfied_mask |= (1u << STAGE36_PHASE_PLATFORM_RESULT);
    }
    if ((state->service_available_mask & state->phase3_required_services) == state->phase3_required_services) {
        state->phase_service_satisfied_mask |= (1u << STAGE36_PHASE_RETURN_READY);
    }
    state->phase_service_status_checksum = state->phase_count ^ state->phase_service_dependency_mask ^
        state->phase_service_satisfied_mask ^ state->phase0_required_services ^
        state->phase1_required_services ^ state->phase2_required_services ^ state->phase3_required_services;
    if (state->phase_count == STAGE36_PHASE_COUNT &&
        state->phase_service_dependency_mask == STAGE36_PHASE_SERVICE_REQUIRED_MASK &&
        state->phase_service_satisfied_mask == STAGE36_PHASE_REQUIRED_MASK &&
        state->phase_service_status_checksum == (STAGE36_PHASE_COUNT ^ STAGE36_PHASE_SERVICE_REQUIRED_MASK ^
            STAGE36_PHASE_REQUIRED_MASK ^ STAGE36_PHASE_VALIDATE_SERVICES ^
            STAGE36_PHASE_DT_SUMMARY_SERVICES ^ STAGE36_PHASE_PLATFORM_RESULT_SERVICES ^
            STAGE36_PHASE_RETURN_READY_SERVICES)) {
        state->phase_service_dependency_status = STAGE36_BOOTSTRAP_STATUS_OK;
        root_steps |= STAGE36_ROOT_STEP_PHASE_SERVICE_DEPS;
        xnu_log_puts("high root phase-service dependencies ok\n");
    } else {
        failures |= STAGE36_FAIL_PHASE_SERVICE_DEPS;
        state->phase_service_dependency_status = STAGE36_BOOTSTRAP_STATUS_BASE | STAGE36_FAIL_PHASE_SERVICE_DEPS;
        xnu_log_puts("high root phase-service dependencies failed\n");
    }

    xnu_log_puts("high root phase dispatcher begin\n");
    {
        const struct stage36_phase_descriptor phase_desc[STAGE36_PHASE_COUNT] = {
            {
                STAGE36_PHASE_VALIDATE,
                state->phase0_required_services,
                ((steps & STAGE36_INIT_STEP_VALIDATE) != 0u),
                &state->phase0_status,
                &state->phase0_handler_result,
            },
            {
                STAGE36_PHASE_DT_SUMMARY,
                state->phase1_required_services,
                (state->root_dt_summary_status == STAGE36_BOOTSTRAP_STATUS_OK),
                &state->phase1_status,
                &state->phase1_handler_result,
            },
            {
                STAGE36_PHASE_PLATFORM_RESULT,
                state->phase2_required_services,
                (state->platform_result_status == STAGE36_BOOTSTRAP_STATUS_OK),
                &state->phase2_status,
                &state->phase2_handler_result,
            },
            {
                STAGE36_PHASE_RETURN_READY,
                state->phase3_required_services,
                ((root_steps & STAGE36_ROOT_STEP_ENTER) != 0u),
                &state->phase3_status,
                &state->phase3_handler_result,
            },
        };

        for (uint32_t i = 0; i < STAGE36_PHASE_COUNT; i++) {
            uint32_t handler = stage36_dispatch_phase(state, &phase_desc[i],
                &dispatcher_completed_mask, &dispatcher_order_mask);
            if (handler == STAGE36_BOOTSTRAP_STATUS_OK) {
                dispatcher_handler_mask |= (1u << phase_desc[i].id);
            }
        }
    }
    state->phase_completed_mask = dispatcher_completed_mask;
    state->phase_dispatcher_order_mask = dispatcher_order_mask;
    state->phase_dispatcher_handler_mask = dispatcher_handler_mask;
    state->phase_dispatcher_status_checksum = state->phase_dispatcher_count ^
        state->phase_dispatcher_order_mask ^ state->phase_dispatcher_handler_mask ^
        state->phase0_descriptor_id ^ state->phase1_descriptor_id ^
        state->phase2_descriptor_id ^ state->phase3_descriptor_id ^
        state->phase0_handler_result ^ state->phase1_handler_result ^
        state->phase2_handler_result ^ state->phase3_handler_result;
    if (state->phase_dispatcher_count == STAGE36_PHASE_COUNT &&
        state->phase_dispatcher_order_mask == STAGE36_PHASE_REQUIRED_MASK &&
        state->phase_dispatcher_handler_mask == STAGE36_PHASE_REQUIRED_MASK &&
        state->phase_completed_mask == STAGE36_PHASE_REQUIRED_MASK &&
        state->phase0_descriptor_id == STAGE36_PHASE_VALIDATE &&
        state->phase1_descriptor_id == STAGE36_PHASE_DT_SUMMARY &&
        state->phase2_descriptor_id == STAGE36_PHASE_PLATFORM_RESULT &&
        state->phase3_descriptor_id == STAGE36_PHASE_RETURN_READY &&
        state->phase0_handler_result == STAGE36_BOOTSTRAP_STATUS_OK &&
        state->phase1_handler_result == STAGE36_BOOTSTRAP_STATUS_OK &&
        state->phase2_handler_result == STAGE36_BOOTSTRAP_STATUS_OK &&
        state->phase3_handler_result == STAGE36_BOOTSTRAP_STATUS_OK &&
        state->phase_dispatcher_status_checksum == (STAGE36_PHASE_COUNT ^
            STAGE36_PHASE_REQUIRED_MASK ^ STAGE36_PHASE_REQUIRED_MASK ^
            STAGE36_PHASE_VALIDATE ^ STAGE36_PHASE_DT_SUMMARY ^
            STAGE36_PHASE_PLATFORM_RESULT ^ STAGE36_PHASE_RETURN_READY ^
            STAGE36_BOOTSTRAP_STATUS_OK ^ STAGE36_BOOTSTRAP_STATUS_OK ^
            STAGE36_BOOTSTRAP_STATUS_OK ^ STAGE36_BOOTSTRAP_STATUS_OK)) {
        state->phase_dispatcher_status = STAGE36_BOOTSTRAP_STATUS_OK;
        root_steps |= STAGE36_ROOT_STEP_PHASE_DISPATCHER;
        xnu_log_puts("high root phase dispatcher ok\n");
    } else {
        failures |= STAGE36_FAIL_PHASE_DISPATCHER;
        state->phase_dispatcher_status = STAGE36_BOOTSTRAP_STATUS_BASE | STAGE36_FAIL_PHASE_DISPATCHER;
        xnu_log_puts("high root phase dispatcher failed\n");
    }

    xnu_log_puts("high root bootstrap registry begin\n");
    state->registry_service_descriptor_mask = (1u << state->service_logging_descriptor_id) |
        (1u << state->service_timebase_descriptor_id) |
        (1u << state->service_platform_descriptor_id) |
        (1u << state->service_interrupts_descriptor_id);
    state->registry_phase_descriptor_mask = (1u << state->phase0_descriptor_id) |
        (1u << state->phase1_descriptor_id) |
        (1u << state->phase2_descriptor_id) |
        (1u << state->phase3_descriptor_id);
    if ((state->phase_service_dependency_mask & state->registry_service_descriptor_mask) ==
        state->phase_service_dependency_mask) {
        state->registry_dependency_coverage_mask = state->phase_service_dependency_mask;
    }
    state->registry_dispatch_coverage_mask = state->service_dispatcher_order_mask |
        (state->phase_dispatcher_order_mask << 16);
    state->registry_status_checksum = state->registry_version ^ state->registry_size ^
        state->registry_service_descriptor_mask ^ state->registry_phase_descriptor_mask ^
        state->registry_dependency_coverage_mask ^ state->registry_dispatch_coverage_mask;
    if (state->registry_version == STAGE36_REGISTRY_VERSION &&
        state->registry_size == sizeof(*state) &&
        state->registry_service_descriptor_mask == STAGE36_SERVICE_REQUIRED_MASK &&
        state->registry_phase_descriptor_mask == STAGE36_PHASE_REQUIRED_MASK &&
        state->registry_dependency_coverage_mask == STAGE36_PHASE_SERVICE_REQUIRED_MASK &&
        state->registry_dispatch_coverage_mask == STAGE36_REGISTRY_DISPATCH_COVERAGE_REQUIRED &&
        state->registry_status_checksum == (STAGE36_REGISTRY_VERSION ^ sizeof(*state) ^
            STAGE36_SERVICE_REQUIRED_MASK ^ STAGE36_PHASE_REQUIRED_MASK ^
            STAGE36_PHASE_SERVICE_REQUIRED_MASK ^ STAGE36_REGISTRY_DISPATCH_COVERAGE_REQUIRED)) {
        state->registry_status = STAGE36_BOOTSTRAP_STATUS_OK;
        root_steps |= STAGE36_ROOT_STEP_BOOTSTRAP_REGISTRY;
        xnu_log_puts("high root bootstrap registry ok\n");
    } else {
        failures |= STAGE36_FAIL_BOOTSTRAP_REGISTRY;
        state->registry_status = STAGE36_BOOTSTRAP_STATUS_BASE | STAGE36_FAIL_BOOTSTRAP_REGISTRY;
        xnu_log_puts("high root bootstrap registry failed\n");
    }

    xnu_log_puts("high root phase table begin\n");
    state->phase_status_checksum = state->phase_count ^ state->phase_completed_mask ^
        state->phase0_status ^ state->phase1_status ^ state->phase2_status ^ state->phase3_status;
    if (state->phase_count == STAGE36_PHASE_COUNT &&
        state->phase_service_dependency_status == STAGE36_BOOTSTRAP_STATUS_OK &&
        state->phase_dispatcher_status == STAGE36_BOOTSTRAP_STATUS_OK &&
        state->phase_completed_mask == STAGE36_PHASE_REQUIRED_MASK &&
        state->phase_status_checksum == (STAGE36_PHASE_COUNT ^ STAGE36_PHASE_REQUIRED_MASK ^
            STAGE36_BOOTSTRAP_STATUS_OK ^ STAGE36_BOOTSTRAP_STATUS_OK ^
            STAGE36_BOOTSTRAP_STATUS_OK ^ STAGE36_BOOTSTRAP_STATUS_OK)) {
        root_steps |= STAGE36_ROOT_STEP_PHASE_TABLE;
        xnu_log_puts("high root phase table ok\n");
    } else {
        failures |= STAGE36_FAIL_PHASE_TABLE;
        xnu_log_puts("high root phase table failed\n");
    }

    if ((steps & (STAGE36_INIT_STEP_VALIDATE | STAGE36_INIT_STEP_TIMEBASE | STAGE36_INIT_STEP_GIC_SUMMARY)) ==
        (STAGE36_INIT_STEP_VALIDATE | STAGE36_INIT_STEP_TIMEBASE | STAGE36_INIT_STEP_GIC_SUMMARY)) {
        steps |= STAGE36_INIT_STEP_COMPLETE;
        root_steps |= STAGE36_ROOT_STEP_INIT;
        xnu_log_puts("high init sequence complete\n");
    } else {
        failures |= STAGE36_FAIL_INIT_STEPS;
        xnu_log_puts("high init sequence incomplete\n");
    }

    xnu_log_puts("high root boot policy begin\n");
    state->boot_policy_observed_root_steps = root_steps & state->boot_policy_required_root_steps;
    state->boot_policy_observed_service_mask = state->registry_service_descriptor_mask;
    state->boot_policy_observed_phase_mask = state->registry_phase_descriptor_mask;
    state->boot_policy_observed_dependency_mask = state->registry_dependency_coverage_mask;
    state->boot_policy_observed_dispatch_coverage_mask = state->registry_dispatch_coverage_mask;
    state->boot_policy_observed_registry_status = state->registry_status;

    if (state->boot_policy_observed_root_steps == state->boot_policy_required_root_steps) {
        state->boot_policy_satisfied_mask |= STAGE36_BOOT_POLICY_SAT_ROOT_STEPS;
    }
    if (state->boot_policy_observed_service_mask == state->boot_policy_required_service_mask) {
        state->boot_policy_satisfied_mask |= STAGE36_BOOT_POLICY_SAT_SERVICE_MASK;
    }
    if (state->boot_policy_observed_phase_mask == state->boot_policy_required_phase_mask) {
        state->boot_policy_satisfied_mask |= STAGE36_BOOT_POLICY_SAT_PHASE_MASK;
    }
    if (state->boot_policy_observed_dependency_mask == state->boot_policy_required_dependency_mask) {
        state->boot_policy_satisfied_mask |= STAGE36_BOOT_POLICY_SAT_DEPENDENCY_MASK;
    }
    if (state->boot_policy_observed_dispatch_coverage_mask == state->boot_policy_required_dispatch_coverage_mask) {
        state->boot_policy_satisfied_mask |= STAGE36_BOOT_POLICY_SAT_DISPATCH_COVERAGE;
    }
    if (state->boot_policy_observed_registry_status == state->boot_policy_required_registry_status) {
        state->boot_policy_satisfied_mask |= STAGE36_BOOT_POLICY_SAT_REGISTRY_STATUS;
    }

    state->boot_policy_status_checksum = state->boot_policy_version ^ state->boot_policy_size ^
        state->boot_policy_required_root_steps ^ state->boot_policy_required_service_mask ^
        state->boot_policy_required_phase_mask ^ state->boot_policy_required_dependency_mask ^
        state->boot_policy_required_dispatch_coverage_mask ^ state->boot_policy_required_registry_status ^
        state->boot_policy_observed_root_steps ^ state->boot_policy_observed_service_mask ^
        state->boot_policy_observed_phase_mask ^ state->boot_policy_observed_dependency_mask ^
        state->boot_policy_observed_dispatch_coverage_mask ^ state->boot_policy_observed_registry_status ^
        state->boot_policy_satisfied_mask;
    if (state->boot_policy_version == STAGE36_BOOT_POLICY_VERSION &&
        state->boot_policy_size == sizeof(*state) &&
        state->boot_policy_required_root_steps == STAGE36_BOOT_POLICY_REQUIRED_ROOT_STEPS &&
        state->boot_policy_required_service_mask == STAGE36_SERVICE_REQUIRED_MASK &&
        state->boot_policy_required_phase_mask == STAGE36_PHASE_REQUIRED_MASK &&
        state->boot_policy_required_dependency_mask == STAGE36_PHASE_SERVICE_REQUIRED_MASK &&
        state->boot_policy_required_dispatch_coverage_mask == STAGE36_REGISTRY_DISPATCH_COVERAGE_REQUIRED &&
        state->boot_policy_required_registry_status == STAGE36_BOOTSTRAP_STATUS_OK &&
        state->boot_policy_satisfied_mask == STAGE36_BOOT_POLICY_SAT_REQUIRED &&
        state->boot_policy_status_checksum == (STAGE36_BOOT_POLICY_VERSION ^ sizeof(*state) ^
            STAGE36_BOOT_POLICY_REQUIRED_ROOT_STEPS ^ STAGE36_SERVICE_REQUIRED_MASK ^
            STAGE36_PHASE_REQUIRED_MASK ^ STAGE36_PHASE_SERVICE_REQUIRED_MASK ^
            STAGE36_REGISTRY_DISPATCH_COVERAGE_REQUIRED ^ STAGE36_BOOTSTRAP_STATUS_OK ^
            STAGE36_BOOT_POLICY_REQUIRED_ROOT_STEPS ^ STAGE36_SERVICE_REQUIRED_MASK ^
            STAGE36_PHASE_REQUIRED_MASK ^ STAGE36_PHASE_SERVICE_REQUIRED_MASK ^
            STAGE36_REGISTRY_DISPATCH_COVERAGE_REQUIRED ^ STAGE36_BOOTSTRAP_STATUS_OK ^
            STAGE36_BOOT_POLICY_SAT_REQUIRED)) {
        state->boot_policy_status = STAGE36_BOOTSTRAP_STATUS_OK;
        root_steps |= STAGE36_ROOT_STEP_BOOT_POLICY;
        xnu_log_puts("high root boot policy ok\n");
    } else {
        failures |= STAGE36_FAIL_BOOT_POLICY;
        state->boot_policy_status = STAGE36_BOOTSTRAP_STATUS_BASE | STAGE36_FAIL_BOOT_POLICY;
        xnu_log_puts("high root boot policy failed\n");
    }

    xnu_log_puts("high root bootstrap manifest begin\n");
    state->manifest_observed_root_steps = root_steps & state->manifest_required_root_steps;
    state->manifest_service_observed_mask = state->registry_service_descriptor_mask;
    state->manifest_phase_observed_mask = state->registry_phase_descriptor_mask;
    state->manifest_dependency_observed_mask = state->registry_dependency_coverage_mask;
    state->manifest_dispatch_observed_mask = state->registry_dispatch_coverage_mask;
    state->manifest_policy_observed_mask = state->boot_policy_satisfied_mask;
    state->manifest_boot_observed_status = state->boot_policy_status;
    {
        const struct stage36_manifest_record_descriptor manifest_desc[STAGE36_MANIFEST_RECORD_COUNT] = {
            {
                STAGE36_MANIFEST_RECORD_SERVICE,
                state->manifest_service_required_mask,
                state->manifest_service_observed_mask,
                &state->manifest_service_status,
            },
            {
                STAGE36_MANIFEST_RECORD_PHASE,
                state->manifest_phase_required_mask,
                state->manifest_phase_observed_mask,
                &state->manifest_phase_status,
            },
            {
                STAGE36_MANIFEST_RECORD_DEPENDENCY,
                state->manifest_dependency_required_mask,
                state->manifest_dependency_observed_mask,
                &state->manifest_dependency_status,
            },
            {
                STAGE36_MANIFEST_RECORD_DISPATCH,
                state->manifest_dispatch_required_mask,
                state->manifest_dispatch_observed_mask,
                &state->manifest_dispatch_status,
            },
            {
                STAGE36_MANIFEST_RECORD_POLICY,
                state->manifest_policy_required_mask,
                state->manifest_policy_observed_mask,
                &state->manifest_policy_status,
            },
            {
                STAGE36_MANIFEST_RECORD_STATUS,
                state->manifest_boot_required_status,
                state->manifest_boot_observed_status,
                &state->manifest_boot_status_record_status,
            },
        };

        for (uint32_t i = 0; i < STAGE36_MANIFEST_RECORD_COUNT; i++) {
            uint32_t handler = stage36_dispatch_manifest_record(&manifest_desc[i],
                &manifest_order_mask, &manifest_satisfied_mask);
            if (handler == STAGE36_BOOTSTRAP_STATUS_OK) {
                manifest_handler_mask |= (1u << manifest_desc[i].id);
            }
        }
    }
    state->manifest_order_mask = manifest_order_mask;
    state->manifest_satisfied_mask = manifest_satisfied_mask;
    state->manifest_status_checksum = state->manifest_version ^ state->manifest_record_count ^
        state->manifest_required_record_mask ^ state->manifest_order_mask ^ state->manifest_satisfied_mask ^
        state->manifest_required_root_steps ^ state->manifest_observed_root_steps ^
        state->manifest_service_required_mask ^ state->manifest_service_observed_mask ^ state->manifest_service_status ^
        state->manifest_phase_required_mask ^ state->manifest_phase_observed_mask ^ state->manifest_phase_status ^
        state->manifest_dependency_required_mask ^ state->manifest_dependency_observed_mask ^ state->manifest_dependency_status ^
        state->manifest_dispatch_required_mask ^ state->manifest_dispatch_observed_mask ^ state->manifest_dispatch_status ^
        state->manifest_policy_required_mask ^ state->manifest_policy_observed_mask ^ state->manifest_policy_status ^
        state->manifest_boot_required_status ^ state->manifest_boot_observed_status ^
        state->manifest_boot_status_record_status;
    if (state->manifest_version == STAGE36_MANIFEST_VERSION &&
        state->manifest_record_count == STAGE36_MANIFEST_RECORD_COUNT &&
        state->manifest_required_record_mask == STAGE36_MANIFEST_RECORD_REQUIRED_MASK &&
        state->manifest_order_mask == STAGE36_MANIFEST_RECORD_REQUIRED_MASK &&
        manifest_handler_mask == STAGE36_MANIFEST_RECORD_REQUIRED_MASK &&
        state->manifest_satisfied_mask == STAGE36_MANIFEST_RECORD_REQUIRED_MASK &&
        state->manifest_required_root_steps == STAGE36_MANIFEST_REQUIRED_ROOT_STEPS &&
        state->manifest_observed_root_steps == STAGE36_MANIFEST_REQUIRED_ROOT_STEPS &&
        state->manifest_service_required_mask == STAGE36_SERVICE_REQUIRED_MASK &&
        state->manifest_service_observed_mask == STAGE36_SERVICE_REQUIRED_MASK &&
        state->manifest_phase_required_mask == STAGE36_PHASE_REQUIRED_MASK &&
        state->manifest_phase_observed_mask == STAGE36_PHASE_REQUIRED_MASK &&
        state->manifest_dependency_required_mask == STAGE36_PHASE_SERVICE_REQUIRED_MASK &&
        state->manifest_dependency_observed_mask == STAGE36_PHASE_SERVICE_REQUIRED_MASK &&
        state->manifest_dispatch_required_mask == STAGE36_REGISTRY_DISPATCH_COVERAGE_REQUIRED &&
        state->manifest_dispatch_observed_mask == STAGE36_REGISTRY_DISPATCH_COVERAGE_REQUIRED &&
        state->manifest_policy_required_mask == STAGE36_BOOT_POLICY_SAT_REQUIRED &&
        state->manifest_policy_observed_mask == STAGE36_BOOT_POLICY_SAT_REQUIRED &&
        state->manifest_boot_required_status == STAGE36_BOOTSTRAP_STATUS_OK &&
        state->manifest_boot_observed_status == STAGE36_BOOTSTRAP_STATUS_OK &&
        state->manifest_service_status == STAGE36_BOOTSTRAP_STATUS_OK &&
        state->manifest_phase_status == STAGE36_BOOTSTRAP_STATUS_OK &&
        state->manifest_dependency_status == STAGE36_BOOTSTRAP_STATUS_OK &&
        state->manifest_dispatch_status == STAGE36_BOOTSTRAP_STATUS_OK &&
        state->manifest_policy_status == STAGE36_BOOTSTRAP_STATUS_OK &&
        state->manifest_boot_status_record_status == STAGE36_BOOTSTRAP_STATUS_OK &&
        state->manifest_status_checksum == (STAGE36_MANIFEST_VERSION ^ STAGE36_MANIFEST_RECORD_COUNT ^
            STAGE36_MANIFEST_RECORD_REQUIRED_MASK ^ STAGE36_MANIFEST_RECORD_REQUIRED_MASK ^
            STAGE36_MANIFEST_RECORD_REQUIRED_MASK ^ STAGE36_MANIFEST_REQUIRED_ROOT_STEPS ^
            STAGE36_MANIFEST_REQUIRED_ROOT_STEPS ^ STAGE36_SERVICE_REQUIRED_MASK ^
            STAGE36_SERVICE_REQUIRED_MASK ^ STAGE36_BOOTSTRAP_STATUS_OK ^ STAGE36_PHASE_REQUIRED_MASK ^
            STAGE36_PHASE_REQUIRED_MASK ^ STAGE36_BOOTSTRAP_STATUS_OK ^ STAGE36_PHASE_SERVICE_REQUIRED_MASK ^
            STAGE36_PHASE_SERVICE_REQUIRED_MASK ^ STAGE36_BOOTSTRAP_STATUS_OK ^
            STAGE36_REGISTRY_DISPATCH_COVERAGE_REQUIRED ^ STAGE36_REGISTRY_DISPATCH_COVERAGE_REQUIRED ^
            STAGE36_BOOTSTRAP_STATUS_OK ^ STAGE36_BOOT_POLICY_SAT_REQUIRED ^
            STAGE36_BOOT_POLICY_SAT_REQUIRED ^ STAGE36_BOOTSTRAP_STATUS_OK ^ STAGE36_BOOTSTRAP_STATUS_OK ^
            STAGE36_BOOTSTRAP_STATUS_OK ^ STAGE36_BOOTSTRAP_STATUS_OK)) {
        state->manifest_status = STAGE36_BOOTSTRAP_STATUS_OK;
        root_steps |= STAGE36_ROOT_STEP_BOOTSTRAP_MANIFEST;
        xnu_log_puts("high root bootstrap manifest ok\n");
    } else {
        failures |= STAGE36_FAIL_BOOTSTRAP_MANIFEST;
        state->manifest_status = STAGE36_BOOTSTRAP_STATUS_BASE | STAGE36_FAIL_BOOTSTRAP_MANIFEST;
        xnu_log_puts("high root bootstrap manifest failed\n");
    }

    xnu_log_puts("high root launch contract begin\n");
    state->launch_observed_root_steps = root_steps & state->launch_required_root_steps;
    state->launch_observed_root_status = (!failures &&
        state->boot_policy_status == STAGE36_BOOTSTRAP_STATUS_OK &&
        state->manifest_status == STAGE36_BOOTSTRAP_STATUS_OK) ?
        STAGE36_BOOTSTRAP_STATUS_OK : (STAGE36_BOOTSTRAP_STATUS_BASE | failures);
    state->launch_observed_manifest_status = state->manifest_status;
    state->launch_observed_policy_status = state->boot_policy_status;
    state->launch_observed_mmu_state = ((read_sctlr() & 1u) != 0u) ? STAGE36_LAUNCH_MMU_ENABLED : 0u;
    state->launch_observed_timebase_freq = state->init_timebase_freq;
    if ((state->init_gic_dist_ctlr & 1u) != 0u) {
        state->launch_observed_interrupt_mask |= STAGE36_LAUNCH_IRQ_READY_GIC_DIST;
    }
    if ((state->init_gic_cpu_ctlr & 1u) != 0u) {
        state->launch_observed_interrupt_mask |= STAGE36_LAUNCH_IRQ_READY_GIC_CPU;
    }
    if (state->service_interrupts_status == STAGE36_BOOTSTRAP_STATUS_OK) {
        state->launch_observed_interrupt_mask |= STAGE36_LAUNCH_IRQ_READY_SERVICE;
    }
    if (state->init_timebase_freq == 19200000u) {
        state->launch_observed_interrupt_mask |= STAGE36_LAUNCH_IRQ_READY_TIMEBASE;
    }

    if (state->launch_observed_root_steps == state->launch_required_root_steps) {
        state->launch_satisfied_mask |= STAGE36_LAUNCH_SAT_ROOT_STEPS;
    }
    if (state->launch_observed_root_status == state->launch_required_root_status) {
        state->launch_satisfied_mask |= STAGE36_LAUNCH_SAT_ROOT_STATUS;
    }
    if (state->launch_observed_manifest_status == state->launch_required_manifest_status) {
        state->launch_satisfied_mask |= STAGE36_LAUNCH_SAT_MANIFEST_STATUS;
    }
    if (state->launch_observed_policy_status == state->launch_required_policy_status) {
        state->launch_satisfied_mask |= STAGE36_LAUNCH_SAT_POLICY_STATUS;
    }
    if (state->launch_observed_mmu_state == state->launch_required_mmu_state) {
        state->launch_satisfied_mask |= STAGE36_LAUNCH_SAT_MMU_STATE;
    }
    if (state->launch_observed_timebase_freq == state->launch_required_timebase_freq) {
        state->launch_satisfied_mask |= STAGE36_LAUNCH_SAT_TIMEBASE;
    }
    if (state->launch_observed_interrupt_mask == state->launch_required_interrupt_mask) {
        state->launch_satisfied_mask |= STAGE36_LAUNCH_SAT_INTERRUPTS;
    }

    state->launch_status_checksum = state->launch_contract_version ^ state->launch_contract_size ^
        state->launch_required_root_steps ^ state->launch_observed_root_steps ^
        state->launch_required_root_status ^ state->launch_observed_root_status ^
        state->launch_required_manifest_status ^ state->launch_observed_manifest_status ^
        state->launch_required_policy_status ^ state->launch_observed_policy_status ^
        state->launch_required_mmu_state ^ state->launch_observed_mmu_state ^
        state->launch_required_timebase_freq ^ state->launch_observed_timebase_freq ^
        state->launch_required_interrupt_mask ^ state->launch_observed_interrupt_mask ^
        state->launch_satisfied_mask;
    if (state->launch_contract_version == STAGE36_LAUNCH_CONTRACT_VERSION &&
        state->launch_contract_size == sizeof(*state) &&
        state->launch_required_root_steps == STAGE36_LAUNCH_REQUIRED_ROOT_STEPS &&
        state->launch_observed_root_steps == STAGE36_LAUNCH_REQUIRED_ROOT_STEPS &&
        state->launch_required_root_status == STAGE36_BOOTSTRAP_STATUS_OK &&
        state->launch_observed_root_status == STAGE36_BOOTSTRAP_STATUS_OK &&
        state->launch_required_manifest_status == STAGE36_BOOTSTRAP_STATUS_OK &&
        state->launch_observed_manifest_status == STAGE36_BOOTSTRAP_STATUS_OK &&
        state->launch_required_policy_status == STAGE36_BOOTSTRAP_STATUS_OK &&
        state->launch_observed_policy_status == STAGE36_BOOTSTRAP_STATUS_OK &&
        state->launch_required_mmu_state == STAGE36_LAUNCH_MMU_ENABLED &&
        state->launch_observed_mmu_state == STAGE36_LAUNCH_MMU_ENABLED &&
        state->launch_required_timebase_freq == 19200000u &&
        state->launch_observed_timebase_freq == 19200000u &&
        state->launch_required_interrupt_mask == STAGE36_LAUNCH_IRQ_READY_REQUIRED &&
        state->launch_observed_interrupt_mask == STAGE36_LAUNCH_IRQ_READY_REQUIRED &&
        state->launch_satisfied_mask == STAGE36_LAUNCH_SAT_REQUIRED &&
        state->launch_status_checksum == (STAGE36_LAUNCH_CONTRACT_VERSION ^ sizeof(*state) ^
            STAGE36_LAUNCH_REQUIRED_ROOT_STEPS ^ STAGE36_LAUNCH_REQUIRED_ROOT_STEPS ^
            STAGE36_BOOTSTRAP_STATUS_OK ^ STAGE36_BOOTSTRAP_STATUS_OK ^
            STAGE36_BOOTSTRAP_STATUS_OK ^ STAGE36_BOOTSTRAP_STATUS_OK ^
            STAGE36_BOOTSTRAP_STATUS_OK ^ STAGE36_BOOTSTRAP_STATUS_OK ^
            STAGE36_LAUNCH_MMU_ENABLED ^ STAGE36_LAUNCH_MMU_ENABLED ^
            19200000u ^ 19200000u ^ STAGE36_LAUNCH_IRQ_READY_REQUIRED ^
            STAGE36_LAUNCH_IRQ_READY_REQUIRED ^ STAGE36_LAUNCH_SAT_REQUIRED)) {
        state->launch_status = STAGE36_BOOTSTRAP_STATUS_OK;
        root_steps |= STAGE36_ROOT_STEP_LAUNCH_CONTRACT;
        xnu_log_puts("high root launch contract ok\n");
    } else {
        failures |= STAGE36_FAIL_LAUNCH_CONTRACT;
        state->launch_status = STAGE36_BOOTSTRAP_STATUS_BASE | STAGE36_FAIL_LAUNCH_CONTRACT;
        xnu_log_puts("high root launch contract failed\n");
    }

    xnu_log_puts("high root startup boundary begin\n");
    state->startup_observed_root_steps = root_steps & state->startup_required_root_steps;
    state->startup_observed_launch_status = state->launch_status;
    state->startup_observed_root_status = state->launch_observed_root_status;
    state->startup_observed_manifest_status = state->manifest_status;
    state->startup_observed_boot_args_virt = state->root_boot_args_virt;
    state->startup_observed_dt_virt = state->root_dt_virt;
    state->startup_observed_timebase_freq = state->launch_observed_timebase_freq;
    state->startup_observed_interrupt_mask = state->launch_observed_interrupt_mask;

    if (state->startup_observed_root_steps == state->startup_required_root_steps) {
        state->startup_satisfied_mask |= STAGE36_STARTUP_SAT_ROOT_STEPS;
    }
    if (state->startup_observed_launch_status == state->startup_required_launch_status) {
        state->startup_satisfied_mask |= STAGE36_STARTUP_SAT_LAUNCH_STATUS;
    }
    if (state->startup_observed_root_status == state->startup_required_root_status) {
        state->startup_satisfied_mask |= STAGE36_STARTUP_SAT_ROOT_STATUS;
    }
    if (state->startup_observed_manifest_status == state->startup_required_manifest_status) {
        state->startup_satisfied_mask |= STAGE36_STARTUP_SAT_MANIFEST_STATUS;
    }
    if (state->startup_observed_boot_args_virt == state->startup_required_boot_args_virt) {
        state->startup_satisfied_mask |= STAGE36_STARTUP_SAT_BOOT_ARGS;
    }
    if (state->startup_observed_dt_virt == state->startup_required_dt_virt) {
        state->startup_satisfied_mask |= STAGE36_STARTUP_SAT_DT;
    }
    if (state->startup_observed_timebase_freq == state->startup_required_timebase_freq) {
        state->startup_satisfied_mask |= STAGE36_STARTUP_SAT_TIMEBASE;
    }
    if (state->startup_observed_interrupt_mask == state->startup_required_interrupt_mask) {
        state->startup_satisfied_mask |= STAGE36_STARTUP_SAT_INTERRUPTS;
    }

    state->startup_status_checksum = state->startup_boundary_version ^ state->startup_boundary_size ^
        state->startup_required_root_steps ^ state->startup_observed_root_steps ^
        state->startup_required_launch_status ^ state->startup_observed_launch_status ^
        state->startup_required_root_status ^ state->startup_observed_root_status ^
        state->startup_required_manifest_status ^ state->startup_observed_manifest_status ^
        state->startup_required_boot_args_virt ^ state->startup_observed_boot_args_virt ^
        state->startup_required_dt_virt ^ state->startup_observed_dt_virt ^
        state->startup_required_timebase_freq ^ state->startup_observed_timebase_freq ^
        state->startup_required_interrupt_mask ^ state->startup_observed_interrupt_mask ^
        state->startup_satisfied_mask;
    if (state->startup_boundary_version == STAGE36_STARTUP_BOUNDARY_VERSION &&
        state->startup_boundary_size == sizeof(*state) &&
        state->startup_required_root_steps == STAGE36_STARTUP_REQUIRED_ROOT_STEPS &&
        state->startup_observed_root_steps == STAGE36_STARTUP_REQUIRED_ROOT_STEPS &&
        state->startup_required_launch_status == STAGE36_BOOTSTRAP_STATUS_OK &&
        state->startup_observed_launch_status == STAGE36_BOOTSTRAP_STATUS_OK &&
        state->startup_required_root_status == STAGE36_BOOTSTRAP_STATUS_OK &&
        state->startup_observed_root_status == STAGE36_BOOTSTRAP_STATUS_OK &&
        state->startup_required_manifest_status == STAGE36_BOOTSTRAP_STATUS_OK &&
        state->startup_observed_manifest_status == STAGE36_BOOTSTRAP_STATUS_OK &&
        state->startup_required_boot_args_virt == state->root_boot_args_virt &&
        state->startup_observed_boot_args_virt == state->root_boot_args_virt &&
        state->startup_required_dt_virt == state->root_dt_virt &&
        state->startup_observed_dt_virt == state->root_dt_virt &&
        state->startup_required_timebase_freq == 19200000u &&
        state->startup_observed_timebase_freq == 19200000u &&
        state->startup_required_interrupt_mask == STAGE36_LAUNCH_IRQ_READY_REQUIRED &&
        state->startup_observed_interrupt_mask == STAGE36_LAUNCH_IRQ_READY_REQUIRED &&
        state->startup_satisfied_mask == STAGE36_STARTUP_SAT_REQUIRED &&
        state->startup_status_checksum == (STAGE36_STARTUP_BOUNDARY_VERSION ^ sizeof(*state) ^
            STAGE36_STARTUP_REQUIRED_ROOT_STEPS ^ STAGE36_STARTUP_REQUIRED_ROOT_STEPS ^
            STAGE36_BOOTSTRAP_STATUS_OK ^ STAGE36_BOOTSTRAP_STATUS_OK ^
            STAGE36_BOOTSTRAP_STATUS_OK ^ STAGE36_BOOTSTRAP_STATUS_OK ^
            STAGE36_BOOTSTRAP_STATUS_OK ^ STAGE36_BOOTSTRAP_STATUS_OK ^
            state->root_boot_args_virt ^ state->root_boot_args_virt ^ state->root_dt_virt ^
            state->root_dt_virt ^ 19200000u ^ 19200000u ^
            STAGE36_LAUNCH_IRQ_READY_REQUIRED ^ STAGE36_LAUNCH_IRQ_READY_REQUIRED ^
            STAGE36_STARTUP_SAT_REQUIRED)) {
        state->startup_status = STAGE36_BOOTSTRAP_STATUS_OK;
        root_steps |= STAGE36_ROOT_STEP_STARTUP_BOUNDARY;
        xnu_log_puts("high root startup boundary ok\n");
    } else {
        failures |= STAGE36_FAIL_STARTUP_BOUNDARY;
        state->startup_status = STAGE36_BOOTSTRAP_STATUS_BASE | STAGE36_FAIL_STARTUP_BOUNDARY;
        xnu_log_puts("high root startup boundary failed\n");
    }

    xnu_log_puts("high root startup routine begin\n");
    state->startup_routine_observed_root_steps = root_steps & state->startup_routine_required_root_steps;
    state->startup_routine_observed_startup_status = state->startup_status;
    state->startup_routine_observed_launch_status = state->launch_status;
    state->startup_routine_observed_root_status = state->startup_observed_root_status;
    state->startup_routine_observed_boot_args_virt = state->startup_observed_boot_args_virt;
    state->startup_routine_observed_dt_virt = state->startup_observed_dt_virt;
    state->startup_routine_observed_timebase_freq = state->startup_observed_timebase_freq;
    state->startup_routine_observed_interrupt_mask = state->startup_observed_interrupt_mask;

    if (state->startup_routine_observed_root_steps == state->startup_routine_required_root_steps) {
        state->startup_routine_satisfied_mask |= STAGE36_ROUTINE_SAT_ROOT_STEPS;
    }
    if (state->startup_routine_observed_startup_status == state->startup_routine_required_startup_status) {
        state->startup_routine_satisfied_mask |= STAGE36_ROUTINE_SAT_STARTUP_STATUS;
    }
    if (state->startup_routine_observed_launch_status == state->startup_routine_required_launch_status) {
        state->startup_routine_satisfied_mask |= STAGE36_ROUTINE_SAT_LAUNCH_STATUS;
    }
    if (state->startup_routine_observed_root_status == state->startup_routine_required_root_status) {
        state->startup_routine_satisfied_mask |= STAGE36_ROUTINE_SAT_ROOT_STATUS;
    }
    if (state->startup_routine_observed_boot_args_virt == state->startup_routine_required_boot_args_virt) {
        state->startup_routine_satisfied_mask |= STAGE36_ROUTINE_SAT_BOOT_ARGS;
    }
    if (state->startup_routine_observed_dt_virt == state->startup_routine_required_dt_virt) {
        state->startup_routine_satisfied_mask |= STAGE36_ROUTINE_SAT_DT;
    }
    if (state->startup_routine_observed_timebase_freq == state->startup_routine_required_timebase_freq) {
        state->startup_routine_satisfied_mask |= STAGE36_ROUTINE_SAT_TIMEBASE;
    }
    if (state->startup_routine_observed_interrupt_mask == state->startup_routine_required_interrupt_mask) {
        state->startup_routine_satisfied_mask |= STAGE36_ROUTINE_SAT_INTERRUPTS;
    }

    state->startup_routine_status_checksum = state->startup_routine_version ^ state->startup_routine_size ^
        state->startup_routine_required_root_steps ^ state->startup_routine_observed_root_steps ^
        state->startup_routine_required_startup_status ^ state->startup_routine_observed_startup_status ^
        state->startup_routine_required_launch_status ^ state->startup_routine_observed_launch_status ^
        state->startup_routine_required_root_status ^ state->startup_routine_observed_root_status ^
        state->startup_routine_required_boot_args_virt ^ state->startup_routine_observed_boot_args_virt ^
        state->startup_routine_required_dt_virt ^ state->startup_routine_observed_dt_virt ^
        state->startup_routine_required_timebase_freq ^ state->startup_routine_observed_timebase_freq ^
        state->startup_routine_required_interrupt_mask ^ state->startup_routine_observed_interrupt_mask ^
        state->startup_routine_satisfied_mask;
    if (state->startup_routine_version == STAGE36_STARTUP_ROUTINE_VERSION &&
        state->startup_routine_size == sizeof(*state) &&
        state->startup_routine_required_root_steps == STAGE36_ROUTINE_REQUIRED_ROOT_STEPS &&
        state->startup_routine_observed_root_steps == STAGE36_ROUTINE_REQUIRED_ROOT_STEPS &&
        state->startup_routine_required_startup_status == STAGE36_BOOTSTRAP_STATUS_OK &&
        state->startup_routine_observed_startup_status == STAGE36_BOOTSTRAP_STATUS_OK &&
        state->startup_routine_required_launch_status == STAGE36_BOOTSTRAP_STATUS_OK &&
        state->startup_routine_observed_launch_status == STAGE36_BOOTSTRAP_STATUS_OK &&
        state->startup_routine_required_root_status == STAGE36_BOOTSTRAP_STATUS_OK &&
        state->startup_routine_observed_root_status == STAGE36_BOOTSTRAP_STATUS_OK &&
        state->startup_routine_required_boot_args_virt == state->startup_required_boot_args_virt &&
        state->startup_routine_observed_boot_args_virt == state->startup_observed_boot_args_virt &&
        state->startup_routine_required_dt_virt == state->startup_required_dt_virt &&
        state->startup_routine_observed_dt_virt == state->startup_observed_dt_virt &&
        state->startup_routine_required_timebase_freq == 19200000u &&
        state->startup_routine_observed_timebase_freq == 19200000u &&
        state->startup_routine_required_interrupt_mask == STAGE36_LAUNCH_IRQ_READY_REQUIRED &&
        state->startup_routine_observed_interrupt_mask == STAGE36_LAUNCH_IRQ_READY_REQUIRED &&
        state->startup_routine_satisfied_mask == STAGE36_ROUTINE_SAT_REQUIRED &&
        state->startup_routine_status_checksum == (STAGE36_STARTUP_ROUTINE_VERSION ^ sizeof(*state) ^
            STAGE36_ROUTINE_REQUIRED_ROOT_STEPS ^ STAGE36_ROUTINE_REQUIRED_ROOT_STEPS ^
            STAGE36_BOOTSTRAP_STATUS_OK ^ STAGE36_BOOTSTRAP_STATUS_OK ^
            STAGE36_BOOTSTRAP_STATUS_OK ^ STAGE36_BOOTSTRAP_STATUS_OK ^
            STAGE36_BOOTSTRAP_STATUS_OK ^ STAGE36_BOOTSTRAP_STATUS_OK ^
            state->startup_required_boot_args_virt ^ state->startup_observed_boot_args_virt ^
            state->startup_required_dt_virt ^ state->startup_observed_dt_virt ^
            19200000u ^ 19200000u ^ STAGE36_LAUNCH_IRQ_READY_REQUIRED ^
            STAGE36_LAUNCH_IRQ_READY_REQUIRED ^ STAGE36_ROUTINE_SAT_REQUIRED)) {
        state->startup_routine_status = STAGE36_BOOTSTRAP_STATUS_OK;
        root_steps |= STAGE36_ROOT_STEP_STARTUP_ROUTINE;
        xnu_log_puts("high root startup routine ok\n");
    } else {
        failures |= STAGE36_FAIL_STARTUP_ROUTINE;
        state->startup_routine_status = STAGE36_BOOTSTRAP_STATUS_BASE | STAGE36_FAIL_STARTUP_ROUTINE;
        xnu_log_puts("high root startup routine failed\n");
    }

    xnu_log_puts("high root startup handoff begin\n");
    memset((void *)(uintptr_t)startup_handoff, 0, sizeof(*startup_handoff));
    memset((void *)(uintptr_t)kernel_context, 0, sizeof(*kernel_context));
    memset((void *)(uintptr_t)vm_plan, 0, sizeof(*vm_plan));
    memset((void *)(uintptr_t)vm_state, 0, sizeof(*vm_state));
    startup_handoff_root_status = (!failures &&
        state->boot_policy_status == STAGE36_BOOTSTRAP_STATUS_OK &&
        state->manifest_status == STAGE36_BOOTSTRAP_STATUS_OK &&
        state->launch_status == STAGE36_BOOTSTRAP_STATUS_OK &&
        state->startup_status == STAGE36_BOOTSTRAP_STATUS_OK &&
        state->startup_routine_status == STAGE36_BOOTSTRAP_STATUS_OK) ?
        STAGE36_BOOTSTRAP_STATUS_OK : (STAGE36_BOOTSTRAP_STATUS_BASE | failures | STAGE36_FAIL_STARTUP_ENTRY);
    startup_handoff->version = STAGE36_STARTUP_HANDOFF_VERSION;
    startup_handoff->size = sizeof(*startup_handoff);
    startup_handoff->root_steps = root_steps;
    startup_handoff->routine_status = state->startup_routine_status;
    startup_handoff->startup_status = state->startup_status;
    startup_handoff->root_status = startup_handoff_root_status;
    startup_handoff->boot_args_virt = state->startup_routine_observed_boot_args_virt;
    startup_handoff->dt_virt = state->startup_routine_observed_dt_virt;
    startup_handoff->timebase_freq = state->startup_routine_observed_timebase_freq;
    startup_handoff->interrupt_mask = state->startup_routine_observed_interrupt_mask;
    startup_handoff_checksum = startup_handoff->version ^ startup_handoff->size ^
        startup_handoff->root_steps ^ startup_handoff->routine_status ^
        startup_handoff->startup_status ^ startup_handoff->root_status ^
        startup_handoff->boot_args_virt ^ startup_handoff->dt_virt ^
        startup_handoff->timebase_freq ^ startup_handoff->interrupt_mask;
    startup_handoff->checksum = startup_handoff_checksum;
    if (startup_handoff->version == STAGE36_STARTUP_HANDOFF_VERSION &&
        startup_handoff->size == sizeof(*startup_handoff) &&
        startup_handoff->root_steps == STAGE36_ENTRY_REQUIRED_ROOT_STEPS &&
        startup_handoff->routine_status == STAGE36_BOOTSTRAP_STATUS_OK &&
        startup_handoff->startup_status == STAGE36_BOOTSTRAP_STATUS_OK &&
        startup_handoff->root_status == STAGE36_BOOTSTRAP_STATUS_OK &&
        startup_handoff->boot_args_virt == state->startup_entry_required_boot_args_virt &&
        startup_handoff->dt_virt == state->startup_entry_required_dt_virt &&
        startup_handoff->timebase_freq == 19200000u &&
        startup_handoff->interrupt_mask == STAGE36_LAUNCH_IRQ_READY_REQUIRED &&
        startup_handoff->checksum == startup_handoff_checksum) {
        startup_handoff->status = STAGE36_BOOTSTRAP_STATUS_OK;
        xnu_log_puts("high root startup handoff ok\n");
    } else {
        startup_handoff->status = STAGE36_BOOTSTRAP_STATUS_BASE | STAGE36_FAIL_STARTUP_ENTRY;
        xnu_log_puts("high root startup handoff failed\n");
    }

    xnu_log_puts("high root startup entry begin\n");
    startup_entry_result = startup_entry_fn(startup_handoff, state, kernel_context);
    if (startup_entry_result == STAGE36_BOOTSTRAP_STATUS_OK &&
        state->startup_entry_status == STAGE36_BOOTSTRAP_STATUS_OK &&
        state->kernel_callout_status == STAGE36_BOOTSTRAP_STATUS_OK &&
        state->kernel_context_status == STAGE36_BOOTSTRAP_STATUS_OK) {
        root_steps |= STAGE36_ROOT_STEP_STARTUP_ENTRY;
        root_steps |= STAGE36_ROOT_STEP_KERNEL_CALLOUT;
        root_steps |= STAGE36_ROOT_STEP_KERNEL_CONTEXT;
        xnu_log_puts("high root startup entry ok\n");
        xnu_log_puts("high root kernel callout table ok\n");
        xnu_log_puts("high root kernel context ok\n");
    } else {
        failures |= STAGE36_FAIL_STARTUP_ENTRY;
        if (state->kernel_callout_status != STAGE36_BOOTSTRAP_STATUS_OK) {
            failures |= STAGE36_FAIL_KERNEL_CALLOUT;
        }
        if (state->kernel_context_status != STAGE36_BOOTSTRAP_STATUS_OK) {
            failures |= STAGE36_FAIL_KERNEL_CONTEXT;
        }
        xnu_log_puts("high root startup entry failed\n");
    }

    xnu_log_puts("high root vm bootstrap plan begin\n");
    memset((void *)(uintptr_t)vm_plan, 0, sizeof(*vm_plan));
    vm_plan->version = STAGE36_VM_PLAN_VERSION;
    vm_plan->size = sizeof(*vm_plan);
    vm_plan->context_status = kernel_context->status;
    vm_plan->context_checksum = kernel_context->checksum;
    vm_plan->low_identity_base = STAGE36_VM_PLAN_IDENTITY_BASE;
    vm_plan->high_alias_base = STAGE36_HIGH_ALIAS_BASE;
    vm_plan->ram_console_alias_base = STAGE36_RAM_CONSOLE_ALIAS_BASE;
    vm_plan->gic_alias_base = STAGE36_GIC_ALIAS_BASE;
    vm_plan->l1_table_phys = (uint32_t)(uintptr_t)stage36_l1_table;
    vm_plan->l1_table_virt = STAGE36_HIGH_ALIAS_BASE + vm_plan->l1_table_phys;
    vm_plan->memory_base = state->platform_memory_base;
    vm_plan->memory_size = state->platform_memory_size;
    vm_plan->section_size = L1_SECTION_SIZE;
    vm_plan->section_descriptor = L1_DESC_SECTION_SO;
    vm_plan->mmu_enabled = ((read_sctlr() & 1u) != 0u) ? STAGE36_VM_PLAN_MMU_ENABLED : 0u;
    vm_plan->cache_policy = ((read_sctlr() & ((1u << 2) | (1u << 12))) == 0u) ?
        STAGE36_VM_PLAN_CACHES_DISABLED : 1u;

    if (vm_plan->context_status == STAGE36_BOOTSTRAP_STATUS_OK &&
        vm_plan->context_checksum == state->kernel_context_checksum) {
        vm_plan->satisfied_mask |= STAGE36_VM_PLAN_SAT_CONTEXT;
    }
    if (vm_plan->memory_base == RAM_PHYS_BASE &&
        vm_plan->memory_size == (RAM_CONSOLE_BASE - RAM_PHYS_BASE)) {
        vm_plan->satisfied_mask |= STAGE36_VM_PLAN_SAT_MEMORY;
    }
    if ((vm_plan->l1_table_phys & 0x00003fffu) == 0u &&
        vm_plan->l1_table_virt == (STAGE36_HIGH_ALIAS_BASE + vm_plan->l1_table_phys)) {
        vm_plan->satisfied_mask |= STAGE36_VM_PLAN_SAT_L1_TABLE;
    }
    if (vm_plan->low_identity_base == STAGE36_VM_PLAN_IDENTITY_BASE &&
        vm_plan->high_alias_base == STAGE36_HIGH_ALIAS_BASE &&
        vm_plan->ram_console_alias_base == STAGE36_RAM_CONSOLE_ALIAS_BASE &&
        vm_plan->gic_alias_base == STAGE36_GIC_ALIAS_BASE) {
        vm_plan->satisfied_mask |= STAGE36_VM_PLAN_SAT_ALIASES;
    }
    if (vm_plan->mmu_enabled == STAGE36_VM_PLAN_MMU_ENABLED) {
        vm_plan->satisfied_mask |= STAGE36_VM_PLAN_SAT_MMU;
    }
    if (vm_plan->cache_policy == STAGE36_VM_PLAN_CACHES_DISABLED) {
        vm_plan->satisfied_mask |= STAGE36_VM_PLAN_SAT_CACHES;
    }
    if (vm_plan->section_size == L1_SECTION_SIZE &&
        vm_plan->section_descriptor == L1_DESC_SECTION_SO) {
        vm_plan->satisfied_mask |= STAGE36_VM_PLAN_SAT_SECTION_POLICY;
    }

    vm_plan->checksum = stage36_vm_plan_checksum(vm_plan);
    if (vm_plan->version == STAGE36_VM_PLAN_VERSION &&
        vm_plan->size == sizeof(*vm_plan) &&
        vm_plan->satisfied_mask == STAGE36_VM_PLAN_SAT_REQUIRED &&
        vm_plan->checksum == stage36_vm_plan_checksum(vm_plan)) {
        vm_plan->status = STAGE36_BOOTSTRAP_STATUS_OK;
        state->vm_plan_status = STAGE36_BOOTSTRAP_STATUS_OK;
        root_steps |= STAGE36_ROOT_STEP_VM_PLAN;
        xnu_log_puts("high root vm bootstrap plan ok\n");
    } else {
        vm_plan->status = STAGE36_BOOTSTRAP_STATUS_BASE | STAGE36_FAIL_VM_PLAN;
        state->vm_plan_status = vm_plan->status;
        failures |= STAGE36_FAIL_VM_PLAN;
        xnu_log_puts("high root vm bootstrap plan failed\n");
    }
    state->vm_plan_version = vm_plan->version;
    state->vm_plan_size = vm_plan->size;
    state->vm_plan_required_mask = STAGE36_VM_PLAN_SAT_REQUIRED;
    state->vm_plan_satisfied_mask = vm_plan->satisfied_mask;
    state->vm_plan_checksum = vm_plan->checksum;
    state->vm_plan_status = vm_plan->status;

    xnu_log_puts("high root vm bootstrap state begin\n");
    memset((void *)(uintptr_t)vm_state, 0, sizeof(*vm_state));
    vm_state->version = STAGE36_VM_STATE_VERSION;
    vm_state->size = sizeof(*vm_state);
    vm_state->vm_plan_status = vm_plan->status;
    vm_state->vm_plan_checksum = vm_plan->checksum;
    vm_state->kernel_map_base = STAGE36_KERNEL_MAP_BASE;
    vm_state->kernel_map_limit = STAGE36_KERNEL_MAP_LIMIT;
    vm_state->available_memory_base = vm_plan->memory_base;
    vm_state->bootstrap_alloc_base = vm_plan->memory_base;
    vm_state->bootstrap_alloc_size = STAGE36_BOOTSTRAP_ALLOC_SIZE;
    vm_state->bootstrap_alloc_end = vm_state->bootstrap_alloc_base + vm_state->bootstrap_alloc_size;
    vm_state->available_memory_cursor = vm_state->bootstrap_alloc_end;
    vm_state->pmap_section_size = vm_plan->section_size;
    vm_state->pmap_section_descriptor = vm_plan->section_descriptor;
    vm_state->pmap_l1_table_phys = vm_plan->l1_table_phys;
    vm_state->pmap_l1_table_virt = vm_plan->l1_table_virt;
    vm_state->mmu_enabled = vm_plan->mmu_enabled;
    vm_state->cache_policy = vm_plan->cache_policy;

    if (vm_state->vm_plan_status == STAGE36_BOOTSTRAP_STATUS_OK &&
        vm_state->vm_plan_checksum == state->vm_plan_checksum) {
        vm_state->satisfied_mask |= STAGE36_VM_STATE_SAT_PLAN;
    }
    if (vm_state->kernel_map_base == STAGE36_KERNEL_MAP_BASE &&
        vm_state->kernel_map_limit == STAGE36_KERNEL_MAP_LIMIT &&
        vm_state->kernel_map_limit > vm_state->kernel_map_base) {
        vm_state->satisfied_mask |= STAGE36_VM_STATE_SAT_KERNEL_MAP;
    }
    if (vm_state->available_memory_base == RAM_PHYS_BASE &&
        vm_state->available_memory_cursor == (RAM_PHYS_BASE + STAGE36_BOOTSTRAP_ALLOC_SIZE) &&
        vm_state->available_memory_cursor <= (vm_plan->memory_base + vm_plan->memory_size)) {
        vm_state->satisfied_mask |= STAGE36_VM_STATE_SAT_MEMORY_CURSOR;
    }
    if (vm_state->bootstrap_alloc_base == RAM_PHYS_BASE &&
        vm_state->bootstrap_alloc_size == STAGE36_BOOTSTRAP_ALLOC_SIZE &&
        vm_state->bootstrap_alloc_end == (vm_state->bootstrap_alloc_base + vm_state->bootstrap_alloc_size) &&
        vm_state->bootstrap_alloc_end == vm_state->available_memory_cursor) {
        vm_state->satisfied_mask |= STAGE36_VM_STATE_SAT_BOOT_ALLOC;
    }
    if (vm_state->pmap_section_size == L1_SECTION_SIZE &&
        vm_state->pmap_section_descriptor == L1_DESC_SECTION_SO &&
        vm_state->pmap_l1_table_phys == (uint32_t)(uintptr_t)stage36_l1_table &&
        vm_state->pmap_l1_table_virt == (STAGE36_HIGH_ALIAS_BASE + (uint32_t)(uintptr_t)stage36_l1_table)) {
        vm_state->satisfied_mask |= STAGE36_VM_STATE_SAT_PMAP_POLICY;
    }
    if (vm_state->mmu_enabled == STAGE36_VM_PLAN_MMU_ENABLED &&
        vm_state->cache_policy == STAGE36_VM_PLAN_CACHES_DISABLED) {
        vm_state->satisfied_mask |= STAGE36_VM_STATE_SAT_MMU_CACHE_POLICY;
    }

    vm_state->checksum = stage36_vm_state_checksum(vm_state);
    if (vm_state->version == STAGE36_VM_STATE_VERSION &&
        vm_state->size == sizeof(*vm_state) &&
        vm_state->satisfied_mask == STAGE36_VM_STATE_SAT_REQUIRED &&
        vm_state->checksum == stage36_vm_state_checksum(vm_state)) {
        vm_state->status = STAGE36_BOOTSTRAP_STATUS_OK;
        state->vm_state_status = STAGE36_BOOTSTRAP_STATUS_OK;
        root_steps |= STAGE36_ROOT_STEP_VM_STATE;
        xnu_log_puts("high root vm bootstrap state ok\n");
    } else {
        vm_state->status = STAGE36_BOOTSTRAP_STATUS_BASE | STAGE36_FAIL_VM_STATE;
        state->vm_state_status = vm_state->status;
        failures |= STAGE36_FAIL_VM_STATE;
        xnu_log_puts("high root vm bootstrap state failed\n");
    }
    state->vm_state_version = vm_state->version;
    state->vm_state_size = vm_state->size;
    state->vm_state_required_mask = STAGE36_VM_STATE_SAT_REQUIRED;
    state->vm_state_satisfied_mask = vm_state->satisfied_mask;
    state->vm_state_checksum = vm_state->checksum;
    state->vm_state_status = vm_state->status;

    xnu_log_puts("high root bootstrap allocator begin\n");
    memset((void *)(uintptr_t)allocator, 0, sizeof(*allocator));
    allocator->version = STAGE36_BOOT_ALLOCATOR_VERSION;
    allocator->size = sizeof(*allocator);
    allocator->vm_state_status = vm_state->status;
    allocator->vm_state_checksum = vm_state->checksum;
    allocator->span_base = vm_state->bootstrap_alloc_base;
    allocator->span_size = vm_state->bootstrap_alloc_size;
    allocator->span_end = vm_state->bootstrap_alloc_end;
    allocator->initial_cursor = vm_state->bootstrap_alloc_base;
    allocator->current_cursor = vm_state->available_memory_cursor;
    allocator->remaining_bytes = (allocator->current_cursor <= allocator->span_end) ?
        (allocator->span_end - allocator->current_cursor) : 0xffffffffu;
    allocator->first_alloc_base = vm_state->bootstrap_alloc_base;
    allocator->first_alloc_size = vm_state->bootstrap_alloc_size;
    allocator->first_alloc_end = vm_state->bootstrap_alloc_end;
    allocator->first_alloc_tag = STAGE36_BOOT_ALLOCATOR_FIRST_TAG;
    allocator->alignment = STAGE36_BOOT_ALLOCATOR_ALIGNMENT;

    if (allocator->vm_state_status == STAGE36_BOOTSTRAP_STATUS_OK &&
        allocator->vm_state_checksum == state->vm_state_checksum) {
        allocator->satisfied_mask |= STAGE36_BOOT_ALLOCATOR_SAT_VM_STATE;
    }
    if (allocator->span_base == vm_state->bootstrap_alloc_base &&
        allocator->span_size == vm_state->bootstrap_alloc_size &&
        allocator->span_end == vm_state->bootstrap_alloc_end &&
        allocator->span_base == RAM_PHYS_BASE &&
        allocator->span_size == STAGE36_BOOTSTRAP_ALLOC_SIZE &&
        allocator->span_end == (allocator->span_base + allocator->span_size)) {
        allocator->satisfied_mask |= STAGE36_BOOT_ALLOCATOR_SAT_SPAN;
    }
    if (allocator->initial_cursor == allocator->span_base &&
        allocator->current_cursor == vm_state->available_memory_cursor &&
        allocator->current_cursor == allocator->span_end) {
        allocator->satisfied_mask |= STAGE36_BOOT_ALLOCATOR_SAT_CURSOR;
    }
    if (allocator->remaining_bytes == 0u &&
        allocator->current_cursor <= allocator->span_end) {
        allocator->satisfied_mask |= STAGE36_BOOT_ALLOCATOR_SAT_REMAINING;
    }
    if (allocator->first_alloc_base == allocator->span_base &&
        allocator->first_alloc_size == allocator->span_size &&
        allocator->first_alloc_end == allocator->span_end &&
        allocator->first_alloc_tag == STAGE36_BOOT_ALLOCATOR_FIRST_TAG) {
        allocator->satisfied_mask |= STAGE36_BOOT_ALLOCATOR_SAT_FIRST_ALLOC;
    }
    if (allocator->alignment == STAGE36_BOOT_ALLOCATOR_ALIGNMENT &&
        (allocator->span_base & (STAGE36_BOOT_ALLOCATOR_ALIGNMENT - 1u)) == 0u &&
        (allocator->span_end & (STAGE36_BOOT_ALLOCATOR_ALIGNMENT - 1u)) == 0u &&
        (allocator->current_cursor & (STAGE36_BOOT_ALLOCATOR_ALIGNMENT - 1u)) == 0u) {
        allocator->satisfied_mask |= STAGE36_BOOT_ALLOCATOR_SAT_POLICY;
    }

    allocator->checksum = stage36_boot_allocator_checksum(allocator);
    if (allocator->version == STAGE36_BOOT_ALLOCATOR_VERSION &&
        allocator->size == sizeof(*allocator) &&
        allocator->satisfied_mask == STAGE36_BOOT_ALLOCATOR_SAT_REQUIRED &&
        allocator->checksum == stage36_boot_allocator_checksum(allocator)) {
        allocator->status = STAGE36_BOOTSTRAP_STATUS_OK;
        state->allocator_status = STAGE36_BOOTSTRAP_STATUS_OK;
        root_steps |= STAGE36_ROOT_STEP_BOOT_ALLOCATOR;
        xnu_log_puts("high root bootstrap allocator ok\n");
    } else {
        allocator->status = STAGE36_BOOTSTRAP_STATUS_BASE | STAGE36_FAIL_BOOT_ALLOCATOR;
        state->allocator_status = allocator->status;
        failures |= STAGE36_FAIL_BOOT_ALLOCATOR;
        xnu_log_puts("high root bootstrap allocator failed\n");
    }
    state->allocator_version = allocator->version;
    state->allocator_size = allocator->size;
    state->allocator_required_mask = STAGE36_BOOT_ALLOCATOR_SAT_REQUIRED;
    state->allocator_satisfied_mask = allocator->satisfied_mask;
    state->allocator_checksum = allocator->checksum;
    state->allocator_status = allocator->status;

    if (!failures && state->boot_policy_status == STAGE36_BOOTSTRAP_STATUS_OK &&
        state->manifest_status == STAGE36_BOOTSTRAP_STATUS_OK &&
        state->launch_status == STAGE36_BOOTSTRAP_STATUS_OK &&
        state->startup_status == STAGE36_BOOTSTRAP_STATUS_OK &&
        state->startup_routine_status == STAGE36_BOOTSTRAP_STATUS_OK &&
        state->startup_entry_status == STAGE36_BOOTSTRAP_STATUS_OK &&
        state->kernel_callout_status == STAGE36_BOOTSTRAP_STATUS_OK &&
        state->kernel_context_status == STAGE36_BOOTSTRAP_STATUS_OK &&
        state->vm_plan_status == STAGE36_BOOTSTRAP_STATUS_OK &&
        state->vm_state_status == STAGE36_BOOTSTRAP_STATUS_OK &&
        state->allocator_status == STAGE36_BOOTSTRAP_STATUS_OK) {
        root_steps |= STAGE36_ROOT_STEP_RESULT;
    }
    root_steps |= STAGE36_ROOT_STEP_RETURN;

    state->validation_mask = failures;
    state->root_steps = root_steps;
    state->root_status = (root_steps == STAGE36_ROOT_REQUIRED_STEPS && !failures &&
        state->boot_policy_status == STAGE36_BOOTSTRAP_STATUS_OK &&
        state->manifest_status == STAGE36_BOOTSTRAP_STATUS_OK &&
        state->launch_status == STAGE36_BOOTSTRAP_STATUS_OK &&
        state->startup_status == STAGE36_BOOTSTRAP_STATUS_OK &&
        state->startup_routine_status == STAGE36_BOOTSTRAP_STATUS_OK &&
        state->startup_entry_status == STAGE36_BOOTSTRAP_STATUS_OK &&
        state->kernel_callout_status == STAGE36_BOOTSTRAP_STATUS_OK &&
        state->kernel_context_status == STAGE36_BOOTSTRAP_STATUS_OK &&
        state->vm_plan_status == STAGE36_BOOTSTRAP_STATUS_OK &&
        state->vm_state_status == STAGE36_BOOTSTRAP_STATUS_OK &&
        state->allocator_status == STAGE36_BOOTSTRAP_STATUS_OK) ?
        STAGE36_BOOTSTRAP_STATUS_OK : (STAGE36_BOOTSTRAP_STATUS_BASE | failures | STAGE36_FAIL_INIT_STEPS);
    state->init_steps = steps;
    state->init_status = failures ? (STAGE36_BOOTSTRAP_STATUS_BASE | failures) : STAGE36_BOOTSTRAP_STATUS_OK;
    state->status = state->root_status;
    state->checksum = stage36_bootstrap_checksum(state);

    xnu_log_kv32("high_root_steps", state->root_steps);
    xnu_log_kv32("high_root_status", state->root_status);
    xnu_log_kv32("high_bootstrap_validation_mask", state->validation_mask);
    xnu_log_kv32("high_bootstrap_init_steps", state->init_steps);
    xnu_log_kv32("high_bootstrap_init_status", state->init_status);
    xnu_log_kv32("high_bootstrap_timebase_freq", state->init_timebase_freq);
    xnu_log_kv32("high_bootstrap_timebase_delta_us", state->init_timebase_delta_us);
    xnu_log_kv32("high_bootstrap_gic_irq_count", state->init_gic_irq_count);
    xnu_log_kv32("high_bootstrap_gic_cpu_count", state->init_gic_cpu_count);
    xnu_log_kv32("high_root_boot_args_virt", state->root_boot_args_virt);
    xnu_log_kv32("high_root_dt_virt", state->root_dt_virt);
    xnu_log_kv32("high_root_dt_root_children", state->root_dt_root_children);
    xnu_log_kv32("high_root_dt_memory_base", state->root_dt_memory_base);
    xnu_log_kv32("high_root_dt_memory_size", state->root_dt_memory_size);
    xnu_log_kv32("high_root_dt_timer_frequency", state->root_dt_timer_frequency);
    xnu_log_kv32("high_root_dt_summary_status", state->root_dt_summary_status);
    xnu_log_kv32("high_platform_result_version", state->platform_result_version);
    xnu_log_kv32("high_platform_result_size", state->platform_result_size);
    xnu_log_kv32("high_platform_result_consistency", state->platform_result_consistency);
    xnu_log_kv32("high_platform_dt_cpu_count", state->platform_dt_cpu_count);
    xnu_log_kv32("high_platform_pe_cpu_count", state->platform_pe_cpu_count);
    xnu_log_kv32("high_platform_memory_base", state->platform_memory_base);
    xnu_log_kv32("high_platform_memory_size", state->platform_memory_size);
    xnu_log_kv32("high_platform_gic_dist_base", state->platform_gic_dist_base);
    xnu_log_kv32("high_platform_gic_cpu_base", state->platform_gic_cpu_base);
    xnu_log_kv32("high_platform_timer_frequency", state->platform_timer_frequency);
    xnu_log_kv32("high_platform_result_status", state->platform_result_status);
    xnu_log_kv32("high_phase_count", state->phase_count);
    xnu_log_kv32("high_phase_completed_mask", state->phase_completed_mask);
    xnu_log_kv32("high_phase_status_checksum", state->phase_status_checksum);
    xnu_log_kv32("high_phase0_status", state->phase0_status);
    xnu_log_kv32("high_phase1_status", state->phase1_status);
    xnu_log_kv32("high_phase2_status", state->phase2_status);
    xnu_log_kv32("high_phase3_status", state->phase3_status);
    xnu_log_kv32("high_phase0_required_services", state->phase0_required_services);
    xnu_log_kv32("high_phase1_required_services", state->phase1_required_services);
    xnu_log_kv32("high_phase2_required_services", state->phase2_required_services);
    xnu_log_kv32("high_phase3_required_services", state->phase3_required_services);
    xnu_log_kv32("high_phase_service_dependency_mask", state->phase_service_dependency_mask);
    xnu_log_kv32("high_phase_service_satisfied_mask", state->phase_service_satisfied_mask);
    xnu_log_kv32("high_phase_service_status_checksum", state->phase_service_status_checksum);
    xnu_log_kv32("high_phase_service_dependency_status", state->phase_service_dependency_status);
    xnu_log_kv32("high_phase_dispatcher_count", state->phase_dispatcher_count);
    xnu_log_kv32("high_phase_dispatcher_order_mask", state->phase_dispatcher_order_mask);
    xnu_log_kv32("high_phase_dispatcher_handler_mask", state->phase_dispatcher_handler_mask);
    xnu_log_kv32("high_phase_dispatcher_status_checksum", state->phase_dispatcher_status_checksum);
    xnu_log_kv32("high_phase0_descriptor_id", state->phase0_descriptor_id);
    xnu_log_kv32("high_phase1_descriptor_id", state->phase1_descriptor_id);
    xnu_log_kv32("high_phase2_descriptor_id", state->phase2_descriptor_id);
    xnu_log_kv32("high_phase3_descriptor_id", state->phase3_descriptor_id);
    xnu_log_kv32("high_phase0_handler_result", state->phase0_handler_result);
    xnu_log_kv32("high_phase1_handler_result", state->phase1_handler_result);
    xnu_log_kv32("high_phase2_handler_result", state->phase2_handler_result);
    xnu_log_kv32("high_phase3_handler_result", state->phase3_handler_result);
    xnu_log_kv32("high_phase_dispatcher_status", state->phase_dispatcher_status);
    xnu_log_kv32("high_service_count", state->service_count);
    xnu_log_kv32("high_service_available_mask", state->service_available_mask);
    xnu_log_kv32("high_service_status_checksum", state->service_status_checksum);
    xnu_log_kv32("high_service_logging_status", state->service_logging_status);
    xnu_log_kv32("high_service_timebase_status", state->service_timebase_status);
    xnu_log_kv32("high_service_platform_status", state->service_platform_status);
    xnu_log_kv32("high_service_interrupts_status", state->service_interrupts_status);
    xnu_log_kv32("high_service_dispatcher_count", state->service_dispatcher_count);
    xnu_log_kv32("high_service_dispatcher_order_mask", state->service_dispatcher_order_mask);
    xnu_log_kv32("high_service_dispatcher_handler_mask", state->service_dispatcher_handler_mask);
    xnu_log_kv32("high_service_dispatcher_status_checksum", state->service_dispatcher_status_checksum);
    xnu_log_kv32("high_service_logging_descriptor_id", state->service_logging_descriptor_id);
    xnu_log_kv32("high_service_timebase_descriptor_id", state->service_timebase_descriptor_id);
    xnu_log_kv32("high_service_platform_descriptor_id", state->service_platform_descriptor_id);
    xnu_log_kv32("high_service_interrupts_descriptor_id", state->service_interrupts_descriptor_id);
    xnu_log_kv32("high_service_logging_handler_result", state->service_logging_handler_result);
    xnu_log_kv32("high_service_timebase_handler_result", state->service_timebase_handler_result);
    xnu_log_kv32("high_service_platform_handler_result", state->service_platform_handler_result);
    xnu_log_kv32("high_service_interrupts_handler_result", state->service_interrupts_handler_result);
    xnu_log_kv32("high_service_dispatcher_status", state->service_dispatcher_status);
    xnu_log_kv32("high_registry_version", state->registry_version);
    xnu_log_kv32("high_registry_size", state->registry_size);
    xnu_log_kv32("high_registry_service_descriptor_mask", state->registry_service_descriptor_mask);
    xnu_log_kv32("high_registry_phase_descriptor_mask", state->registry_phase_descriptor_mask);
    xnu_log_kv32("high_registry_dependency_coverage_mask", state->registry_dependency_coverage_mask);
    xnu_log_kv32("high_registry_dispatch_coverage_mask", state->registry_dispatch_coverage_mask);
    xnu_log_kv32("high_registry_status_checksum", state->registry_status_checksum);
    xnu_log_kv32("high_registry_status", state->registry_status);
    xnu_log_kv32("high_boot_policy_version", state->boot_policy_version);
    xnu_log_kv32("high_boot_policy_size", state->boot_policy_size);
    xnu_log_kv32("high_boot_policy_required_root_steps", state->boot_policy_required_root_steps);
    xnu_log_kv32("high_boot_policy_required_service_mask", state->boot_policy_required_service_mask);
    xnu_log_kv32("high_boot_policy_required_phase_mask", state->boot_policy_required_phase_mask);
    xnu_log_kv32("high_boot_policy_required_dependency_mask", state->boot_policy_required_dependency_mask);
    xnu_log_kv32("high_boot_policy_required_dispatch_coverage_mask", state->boot_policy_required_dispatch_coverage_mask);
    xnu_log_kv32("high_boot_policy_required_registry_status", state->boot_policy_required_registry_status);
    xnu_log_kv32("high_boot_policy_observed_root_steps", state->boot_policy_observed_root_steps);
    xnu_log_kv32("high_boot_policy_observed_service_mask", state->boot_policy_observed_service_mask);
    xnu_log_kv32("high_boot_policy_observed_phase_mask", state->boot_policy_observed_phase_mask);
    xnu_log_kv32("high_boot_policy_observed_dependency_mask", state->boot_policy_observed_dependency_mask);
    xnu_log_kv32("high_boot_policy_observed_dispatch_coverage_mask", state->boot_policy_observed_dispatch_coverage_mask);
    xnu_log_kv32("high_boot_policy_observed_registry_status", state->boot_policy_observed_registry_status);
    xnu_log_kv32("high_boot_policy_satisfied_mask", state->boot_policy_satisfied_mask);
    xnu_log_kv32("high_boot_policy_status_checksum", state->boot_policy_status_checksum);
    xnu_log_kv32("high_boot_policy_status", state->boot_policy_status);
    xnu_log_kv32("high_manifest_version", state->manifest_version);
    xnu_log_kv32("high_manifest_record_count", state->manifest_record_count);
    xnu_log_kv32("high_manifest_required_record_mask", state->manifest_required_record_mask);
    xnu_log_kv32("high_manifest_order_mask", state->manifest_order_mask);
    xnu_log_kv32("high_manifest_satisfied_mask", state->manifest_satisfied_mask);
    xnu_log_kv32("high_manifest_required_root_steps", state->manifest_required_root_steps);
    xnu_log_kv32("high_manifest_observed_root_steps", state->manifest_observed_root_steps);
    xnu_log_kv32("high_manifest_service_required_mask", state->manifest_service_required_mask);
    xnu_log_kv32("high_manifest_service_observed_mask", state->manifest_service_observed_mask);
    xnu_log_kv32("high_manifest_service_status", state->manifest_service_status);
    xnu_log_kv32("high_manifest_phase_required_mask", state->manifest_phase_required_mask);
    xnu_log_kv32("high_manifest_phase_observed_mask", state->manifest_phase_observed_mask);
    xnu_log_kv32("high_manifest_phase_status", state->manifest_phase_status);
    xnu_log_kv32("high_manifest_dependency_required_mask", state->manifest_dependency_required_mask);
    xnu_log_kv32("high_manifest_dependency_observed_mask", state->manifest_dependency_observed_mask);
    xnu_log_kv32("high_manifest_dependency_status", state->manifest_dependency_status);
    xnu_log_kv32("high_manifest_dispatch_required_mask", state->manifest_dispatch_required_mask);
    xnu_log_kv32("high_manifest_dispatch_observed_mask", state->manifest_dispatch_observed_mask);
    xnu_log_kv32("high_manifest_dispatch_status", state->manifest_dispatch_status);
    xnu_log_kv32("high_manifest_policy_required_mask", state->manifest_policy_required_mask);
    xnu_log_kv32("high_manifest_policy_observed_mask", state->manifest_policy_observed_mask);
    xnu_log_kv32("high_manifest_policy_status", state->manifest_policy_status);
    xnu_log_kv32("high_manifest_boot_required_status", state->manifest_boot_required_status);
    xnu_log_kv32("high_manifest_boot_observed_status", state->manifest_boot_observed_status);
    xnu_log_kv32("high_manifest_boot_status_record_status", state->manifest_boot_status_record_status);
    xnu_log_kv32("high_manifest_status_checksum", state->manifest_status_checksum);
    xnu_log_kv32("high_manifest_status", state->manifest_status);
    xnu_log_kv32("high_launch_contract_version", state->launch_contract_version);
    xnu_log_kv32("high_launch_contract_size", state->launch_contract_size);
    xnu_log_kv32("high_launch_required_root_steps", state->launch_required_root_steps);
    xnu_log_kv32("high_launch_observed_root_steps", state->launch_observed_root_steps);
    xnu_log_kv32("high_launch_required_root_status", state->launch_required_root_status);
    xnu_log_kv32("high_launch_observed_root_status", state->launch_observed_root_status);
    xnu_log_kv32("high_launch_required_manifest_status", state->launch_required_manifest_status);
    xnu_log_kv32("high_launch_observed_manifest_status", state->launch_observed_manifest_status);
    xnu_log_kv32("high_launch_required_policy_status", state->launch_required_policy_status);
    xnu_log_kv32("high_launch_observed_policy_status", state->launch_observed_policy_status);
    xnu_log_kv32("high_launch_required_mmu_state", state->launch_required_mmu_state);
    xnu_log_kv32("high_launch_observed_mmu_state", state->launch_observed_mmu_state);
    xnu_log_kv32("high_launch_required_timebase_freq", state->launch_required_timebase_freq);
    xnu_log_kv32("high_launch_observed_timebase_freq", state->launch_observed_timebase_freq);
    xnu_log_kv32("high_launch_required_interrupt_mask", state->launch_required_interrupt_mask);
    xnu_log_kv32("high_launch_observed_interrupt_mask", state->launch_observed_interrupt_mask);
    xnu_log_kv32("high_launch_satisfied_mask", state->launch_satisfied_mask);
    xnu_log_kv32("high_launch_status_checksum", state->launch_status_checksum);
    xnu_log_kv32("high_launch_status", state->launch_status);
    xnu_log_kv32("high_startup_boundary_version", state->startup_boundary_version);
    xnu_log_kv32("high_startup_boundary_size", state->startup_boundary_size);
    xnu_log_kv32("high_startup_required_root_steps", state->startup_required_root_steps);
    xnu_log_kv32("high_startup_observed_root_steps", state->startup_observed_root_steps);
    xnu_log_kv32("high_startup_required_launch_status", state->startup_required_launch_status);
    xnu_log_kv32("high_startup_observed_launch_status", state->startup_observed_launch_status);
    xnu_log_kv32("high_startup_required_root_status", state->startup_required_root_status);
    xnu_log_kv32("high_startup_observed_root_status", state->startup_observed_root_status);
    xnu_log_kv32("high_startup_required_manifest_status", state->startup_required_manifest_status);
    xnu_log_kv32("high_startup_observed_manifest_status", state->startup_observed_manifest_status);
    xnu_log_kv32("high_startup_required_boot_args_virt", state->startup_required_boot_args_virt);
    xnu_log_kv32("high_startup_observed_boot_args_virt", state->startup_observed_boot_args_virt);
    xnu_log_kv32("high_startup_required_dt_virt", state->startup_required_dt_virt);
    xnu_log_kv32("high_startup_observed_dt_virt", state->startup_observed_dt_virt);
    xnu_log_kv32("high_startup_required_timebase_freq", state->startup_required_timebase_freq);
    xnu_log_kv32("high_startup_observed_timebase_freq", state->startup_observed_timebase_freq);
    xnu_log_kv32("high_startup_required_interrupt_mask", state->startup_required_interrupt_mask);
    xnu_log_kv32("high_startup_observed_interrupt_mask", state->startup_observed_interrupt_mask);
    xnu_log_kv32("high_startup_satisfied_mask", state->startup_satisfied_mask);
    xnu_log_kv32("high_startup_status_checksum", state->startup_status_checksum);
    xnu_log_kv32("high_startup_status", state->startup_status);
    xnu_log_kv32("high_startup_routine_version", state->startup_routine_version);
    xnu_log_kv32("high_startup_routine_size", state->startup_routine_size);
    xnu_log_kv32("high_startup_routine_required_root_steps", state->startup_routine_required_root_steps);
    xnu_log_kv32("high_startup_routine_observed_root_steps", state->startup_routine_observed_root_steps);
    xnu_log_kv32("high_startup_routine_required_startup_status", state->startup_routine_required_startup_status);
    xnu_log_kv32("high_startup_routine_observed_startup_status", state->startup_routine_observed_startup_status);
    xnu_log_kv32("high_startup_routine_required_launch_status", state->startup_routine_required_launch_status);
    xnu_log_kv32("high_startup_routine_observed_launch_status", state->startup_routine_observed_launch_status);
    xnu_log_kv32("high_startup_routine_required_root_status", state->startup_routine_required_root_status);
    xnu_log_kv32("high_startup_routine_observed_root_status", state->startup_routine_observed_root_status);
    xnu_log_kv32("high_startup_routine_required_boot_args_virt", state->startup_routine_required_boot_args_virt);
    xnu_log_kv32("high_startup_routine_observed_boot_args_virt", state->startup_routine_observed_boot_args_virt);
    xnu_log_kv32("high_startup_routine_required_dt_virt", state->startup_routine_required_dt_virt);
    xnu_log_kv32("high_startup_routine_observed_dt_virt", state->startup_routine_observed_dt_virt);
    xnu_log_kv32("high_startup_routine_required_timebase_freq", state->startup_routine_required_timebase_freq);
    xnu_log_kv32("high_startup_routine_observed_timebase_freq", state->startup_routine_observed_timebase_freq);
    xnu_log_kv32("high_startup_routine_required_interrupt_mask", state->startup_routine_required_interrupt_mask);
    xnu_log_kv32("high_startup_routine_observed_interrupt_mask", state->startup_routine_observed_interrupt_mask);
    xnu_log_kv32("high_startup_routine_satisfied_mask", state->startup_routine_satisfied_mask);
    xnu_log_kv32("high_startup_routine_status_checksum", state->startup_routine_status_checksum);
    xnu_log_kv32("high_startup_routine_status", state->startup_routine_status);
    xnu_log_kv32("high_startup_handoff_version", startup_handoff->version);
    xnu_log_kv32("high_startup_handoff_size", startup_handoff->size);
    xnu_log_kv32("high_startup_handoff_root_steps", startup_handoff->root_steps);
    xnu_log_kv32("high_startup_handoff_routine_status", startup_handoff->routine_status);
    xnu_log_kv32("high_startup_handoff_startup_status", startup_handoff->startup_status);
    xnu_log_kv32("high_startup_handoff_root_status", startup_handoff->root_status);
    xnu_log_kv32("high_startup_handoff_boot_args_virt", startup_handoff->boot_args_virt);
    xnu_log_kv32("high_startup_handoff_dt_virt", startup_handoff->dt_virt);
    xnu_log_kv32("high_startup_handoff_timebase_freq", startup_handoff->timebase_freq);
    xnu_log_kv32("high_startup_handoff_interrupt_mask", startup_handoff->interrupt_mask);
    xnu_log_kv32("high_startup_handoff_checksum", startup_handoff->checksum);
    xnu_log_kv32("high_startup_handoff_status", startup_handoff->status);
    xnu_log_kv32("high_startup_entry_version", state->startup_entry_version);
    xnu_log_kv32("high_startup_entry_size", state->startup_entry_size);
    xnu_log_kv32("high_startup_entry_required_root_steps", state->startup_entry_required_root_steps);
    xnu_log_kv32("high_startup_entry_observed_root_steps", state->startup_entry_observed_root_steps);
    xnu_log_kv32("high_startup_entry_required_routine_status", state->startup_entry_required_routine_status);
    xnu_log_kv32("high_startup_entry_observed_routine_status", state->startup_entry_observed_routine_status);
    xnu_log_kv32("high_startup_entry_required_startup_status", state->startup_entry_required_startup_status);
    xnu_log_kv32("high_startup_entry_observed_startup_status", state->startup_entry_observed_startup_status);
    xnu_log_kv32("high_startup_entry_required_root_status", state->startup_entry_required_root_status);
    xnu_log_kv32("high_startup_entry_observed_root_status", state->startup_entry_observed_root_status);
    xnu_log_kv32("high_startup_entry_required_boot_args_virt", state->startup_entry_required_boot_args_virt);
    xnu_log_kv32("high_startup_entry_observed_boot_args_virt", state->startup_entry_observed_boot_args_virt);
    xnu_log_kv32("high_startup_entry_required_dt_virt", state->startup_entry_required_dt_virt);
    xnu_log_kv32("high_startup_entry_observed_dt_virt", state->startup_entry_observed_dt_virt);
    xnu_log_kv32("high_startup_entry_required_timebase_freq", state->startup_entry_required_timebase_freq);
    xnu_log_kv32("high_startup_entry_observed_timebase_freq", state->startup_entry_observed_timebase_freq);
    xnu_log_kv32("high_startup_entry_required_interrupt_mask", state->startup_entry_required_interrupt_mask);
    xnu_log_kv32("high_startup_entry_observed_interrupt_mask", state->startup_entry_observed_interrupt_mask);
    xnu_log_kv32("high_startup_entry_satisfied_mask", state->startup_entry_satisfied_mask);
    xnu_log_kv32("high_startup_entry_status_checksum", state->startup_entry_status_checksum);
    xnu_log_kv32("high_startup_entry_status", state->startup_entry_status);
    xnu_log_kv32("high_kernel_callout_version", state->kernel_callout_version);
    xnu_log_kv32("high_kernel_callout_count", state->kernel_callout_count);
    xnu_log_kv32("high_kernel_callout_required_mask", state->kernel_callout_required_mask);
    xnu_log_kv32("high_kernel_callout_order_mask", state->kernel_callout_order_mask);
    xnu_log_kv32("high_kernel_callout_handler_mask", state->kernel_callout_handler_mask);
    xnu_log_kv32("high_kernel_callout_required_service_mask", state->kernel_callout_required_service_mask);
    xnu_log_kv32("high_kernel_callout_observed_service_mask", state->kernel_callout_observed_service_mask);
    xnu_log_kv32("high_kernel_callout_status_checksum", state->kernel_callout_status_checksum);
    xnu_log_kv32("high_kernel_callout0_status", state->kernel_callout0_status);
    xnu_log_kv32("high_kernel_callout1_status", state->kernel_callout1_status);
    xnu_log_kv32("high_kernel_callout2_status", state->kernel_callout2_status);
    xnu_log_kv32("high_kernel_callout3_status", state->kernel_callout3_status);
    xnu_log_kv32("high_kernel_callout_status", state->kernel_callout_status);
    xnu_log_kv32("high_kernel_context_version", state->kernel_context_version);
    xnu_log_kv32("high_kernel_context_size", state->kernel_context_size);
    xnu_log_kv32("high_kernel_context_required_mask", state->kernel_context_required_mask);
    xnu_log_kv32("high_kernel_context_satisfied_mask", state->kernel_context_satisfied_mask);
    xnu_log_kv32("high_kernel_context_checksum", state->kernel_context_checksum);
    xnu_log_kv32("high_kernel_context_status", state->kernel_context_status);
    xnu_log_kv32("high_vm_plan_version", state->vm_plan_version);
    xnu_log_kv32("high_vm_plan_size", state->vm_plan_size);
    xnu_log_kv32("high_vm_plan_required_mask", state->vm_plan_required_mask);
    xnu_log_kv32("high_vm_plan_satisfied_mask", state->vm_plan_satisfied_mask);
    xnu_log_kv32("high_vm_plan_checksum", state->vm_plan_checksum);
    xnu_log_kv32("high_vm_plan_status", state->vm_plan_status);
    xnu_log_kv32("high_vm_state_version", state->vm_state_version);
    xnu_log_kv32("high_vm_state_size", state->vm_state_size);
    xnu_log_kv32("high_vm_state_required_mask", state->vm_state_required_mask);
    xnu_log_kv32("high_vm_state_satisfied_mask", state->vm_state_satisfied_mask);
    xnu_log_kv32("high_vm_state_checksum", state->vm_state_checksum);
    xnu_log_kv32("high_vm_state_status", state->vm_state_status);
    xnu_log_kv32("high_allocator_version", state->allocator_version);
    xnu_log_kv32("high_allocator_size", state->allocator_size);
    xnu_log_kv32("high_allocator_required_mask", state->allocator_required_mask);
    xnu_log_kv32("high_allocator_satisfied_mask", state->allocator_satisfied_mask);
    xnu_log_kv32("high_allocator_checksum", state->allocator_checksum);
    xnu_log_kv32("high_allocator_status", state->allocator_status);
    xnu_log_kv32("high_context_boot_args_virt", kernel_context->boot_args_virt);
    xnu_log_kv32("high_context_dt_virt", kernel_context->dt_virt);
    xnu_log_kv32("high_context_platform_status", kernel_context->platform_status);
    xnu_log_kv32("high_context_platform_consistency", kernel_context->platform_consistency);
    xnu_log_kv32("high_context_timebase_freq", kernel_context->timebase_freq);
    xnu_log_kv32("high_context_interrupt_mask", kernel_context->interrupt_mask);
    xnu_log_kv32("high_context_callout_status", kernel_context->callout_status);
    xnu_log_kv32("high_context_callout_mask", kernel_context->callout_mask);
    xnu_log_kv32("high_context_root_steps", kernel_context->root_steps);
    xnu_log_kv32("high_context_satisfied_mask", kernel_context->satisfied_mask);
    xnu_log_kv32("high_context_checksum", kernel_context->checksum);
    xnu_log_kv32("high_context_status", kernel_context->status);
    xnu_log_kv32("high_plan_context_status", vm_plan->context_status);
    xnu_log_kv32("high_plan_context_checksum", vm_plan->context_checksum);
    xnu_log_kv32("high_plan_low_identity_base", vm_plan->low_identity_base);
    xnu_log_kv32("high_plan_high_alias_base", vm_plan->high_alias_base);
    xnu_log_kv32("high_plan_ram_console_alias_base", vm_plan->ram_console_alias_base);
    xnu_log_kv32("high_plan_gic_alias_base", vm_plan->gic_alias_base);
    xnu_log_kv32("high_plan_l1_table_phys", vm_plan->l1_table_phys);
    xnu_log_kv32("high_plan_l1_table_virt", vm_plan->l1_table_virt);
    xnu_log_kv32("high_plan_memory_base", vm_plan->memory_base);
    xnu_log_kv32("high_plan_memory_size", vm_plan->memory_size);
    xnu_log_kv32("high_plan_section_size", vm_plan->section_size);
    xnu_log_kv32("high_plan_section_descriptor", vm_plan->section_descriptor);
    xnu_log_kv32("high_plan_mmu_enabled", vm_plan->mmu_enabled);
    xnu_log_kv32("high_plan_cache_policy", vm_plan->cache_policy);
    xnu_log_kv32("high_plan_satisfied_mask", vm_plan->satisfied_mask);
    xnu_log_kv32("high_plan_checksum", vm_plan->checksum);
    xnu_log_kv32("high_plan_status", vm_plan->status);
    xnu_log_kv32("high_vmstate_plan_status", vm_state->vm_plan_status);
    xnu_log_kv32("high_vmstate_plan_checksum", vm_state->vm_plan_checksum);
    xnu_log_kv32("high_vmstate_kernel_map_base", vm_state->kernel_map_base);
    xnu_log_kv32("high_vmstate_kernel_map_limit", vm_state->kernel_map_limit);
    xnu_log_kv32("high_vmstate_available_memory_base", vm_state->available_memory_base);
    xnu_log_kv32("high_vmstate_available_memory_cursor", vm_state->available_memory_cursor);
    xnu_log_kv32("high_vmstate_bootstrap_alloc_base", vm_state->bootstrap_alloc_base);
    xnu_log_kv32("high_vmstate_bootstrap_alloc_size", vm_state->bootstrap_alloc_size);
    xnu_log_kv32("high_vmstate_bootstrap_alloc_end", vm_state->bootstrap_alloc_end);
    xnu_log_kv32("high_vmstate_pmap_section_size", vm_state->pmap_section_size);
    xnu_log_kv32("high_vmstate_pmap_section_descriptor", vm_state->pmap_section_descriptor);
    xnu_log_kv32("high_vmstate_pmap_l1_table_phys", vm_state->pmap_l1_table_phys);
    xnu_log_kv32("high_vmstate_pmap_l1_table_virt", vm_state->pmap_l1_table_virt);
    xnu_log_kv32("high_vmstate_mmu_enabled", vm_state->mmu_enabled);
    xnu_log_kv32("high_vmstate_cache_policy", vm_state->cache_policy);
    xnu_log_kv32("high_vmstate_satisfied_mask", vm_state->satisfied_mask);
    xnu_log_kv32("high_vmstate_checksum", vm_state->checksum);
    xnu_log_kv32("high_vmstate_status", vm_state->status);
    xnu_log_kv32("high_allocdesc_vm_state_status", allocator->vm_state_status);
    xnu_log_kv32("high_allocdesc_vm_state_checksum", allocator->vm_state_checksum);
    xnu_log_kv32("high_allocdesc_span_base", allocator->span_base);
    xnu_log_kv32("high_allocdesc_span_size", allocator->span_size);
    xnu_log_kv32("high_allocdesc_span_end", allocator->span_end);
    xnu_log_kv32("high_allocdesc_initial_cursor", allocator->initial_cursor);
    xnu_log_kv32("high_allocdesc_current_cursor", allocator->current_cursor);
    xnu_log_kv32("high_allocdesc_remaining_bytes", allocator->remaining_bytes);
    xnu_log_kv32("high_allocdesc_first_alloc_base", allocator->first_alloc_base);
    xnu_log_kv32("high_allocdesc_first_alloc_size", allocator->first_alloc_size);
    xnu_log_kv32("high_allocdesc_first_alloc_end", allocator->first_alloc_end);
    xnu_log_kv32("high_allocdesc_first_alloc_tag", allocator->first_alloc_tag);
    xnu_log_kv32("high_allocdesc_alignment", allocator->alignment);
    xnu_log_kv32("high_allocdesc_satisfied_mask", allocator->satisfied_mask);
    xnu_log_kv32("high_allocdesc_checksum", allocator->checksum);
    xnu_log_kv32("high_allocdesc_status", allocator->status);
    xnu_log_kv32("high_bootstrap_status", state->status);
    xnu_log_kv32("high_bootstrap_checksum", state->checksum);
    xnu_log_puts("high virtual kernel_root leaving\n");

    return state->status;
}

static inline uint32_t read_sctlr(void)
{
    uint32_t v;
    __asm__ volatile ("mrc p15, 0, %0, c1, c0, 0" : "=r"(v));
    return v;
}

static inline uint32_t read_ttbr0(void)
{
    uint32_t v;
    __asm__ volatile ("mrc p15, 0, %0, c2, c0, 0" : "=r"(v));
    return v;
}

static inline uint32_t read_dacr(void)
{
    uint32_t v;
    __asm__ volatile ("mrc p15, 0, %0, c3, c0, 0" : "=r"(v));
    return v;
}

static inline void write_ttbr0(uint32_t v)
{
    __asm__ volatile ("mcr p15, 0, %0, c2, c0, 0" :: "r"(v) : "memory");
}

static inline void write_ttbcr(uint32_t v)
{
    __asm__ volatile ("mcr p15, 0, %0, c2, c0, 2" :: "r"(v) : "memory");
}

static inline void write_dacr(uint32_t v)
{
    __asm__ volatile ("mcr p15, 0, %0, c3, c0, 0" :: "r"(v) : "memory");
}

static inline void invalidate_tlbs(void)
{
    uint32_t zero = 0;
    __asm__ volatile ("mcr p15, 0, %0, c8, c7, 0" :: "r"(zero) : "memory");
}

static inline void write_sctlr(uint32_t v)
{
    __asm__ volatile ("mcr p15, 0, %0, c1, c0, 0" :: "r"(v) : "memory");
}

static inline void dsb_isb(void)
{
    __asm__ volatile ("dsb sy\n\tisb" ::: "memory");
}

static void map_section(uint32_t va, uint32_t pa)
{
    stage36_l1_table[SECTION_INDEX(va)] = (pa & L1_SECTION_MASK) | L1_DESC_SECTION_SO;
}

static uint32_t table_entry_for(uint32_t va)
{
    return stage36_l1_table[SECTION_INDEX(va)];
}

static void build_identity_table(void)
{
    memset(stage36_l1_table, 0, sizeof(stage36_l1_table));

    /* Low payload/code/data/BSS/stack/VBAR page-table area. */
    map_section(0x00000000u, 0x00000000u);

    /* First controlled XNU-like high aliases for selected low code/data and debug/MMIO windows. */
    map_section(STAGE36_HIGH_ALIAS_BASE, 0x00000000u);
    map_section(STAGE36_RAM_CONSOLE_ALIAS_BASE, RAM_CONSOLE_BASE);
    map_section(STAGE36_GIC_ALIAS_BASE, 0xf9000000u);

    /* IMEM restart reason. */
    map_section(0x0fa00000u, 0x0fa00000u);

    /* Android ram_console persistent RAM window. */
    map_section(RAM_CONSOLE_BASE, RAM_CONSOLE_BASE);
    map_section(RAM_CONSOLE_BASE + L1_SECTION_SIZE, RAM_CONSOLE_BASE + L1_SECTION_SIZE);

    /* MSM8974 GIC + ARM timer MMIO share the 0xf9000000 section in this stage. */
    map_section(0xf9000000u, 0xf9000000u);

    /* MSM8974 PS_HOLD reset register lives in the 0xfc400000 section. */
    map_section(0xfc400000u, 0xfc400000u);
}

static void enable_identity_mmu(void)
{
    uint32_t sctlr;

    dsb_isb();
    write_ttbcr(0);
    write_ttbr0((uint32_t)(uintptr_t)stage36_l1_table);
    write_dacr(0x00000003u);          /* domain 0 manager during first bring-up */
    invalidate_tlbs();
    dsb_isb();

    sctlr = read_sctlr();
    sctlr &= ~((1u << 2) | (1u << 12)); /* keep D-cache/I-cache disabled */
    sctlr |= 1u;                         /* MMU enable */
    write_sctlr(sctlr);
    dsb_isb();
}

int mmu_identity_selftest(void)
{
    volatile uint32_t *gicd_ctlr = (volatile uint32_t *)(uintptr_t)PE_state_stage36.gicDistributorBase;
    volatile uint32_t *gicc_ctlr = (volatile uint32_t *)(uintptr_t)PE_state_stage36.gicCpuBase;
    volatile uint32_t *restart_reason = (volatile uint32_t *)(uintptr_t)RESTART_REASON;
    volatile uint32_t *ram_console_sig = (volatile uint32_t *)(uintptr_t)RAM_CONSOLE_BASE;

    xnu_log_puts("mmu identity selftest begin\n");
    xnu_log_kv32("mmu_sctlr_before", read_sctlr());
    xnu_log_kv32("mmu_ttbr0_before", read_ttbr0());
    xnu_log_kv32("mmu_dacr_before", read_dacr());
    xnu_log_kv32("mmu_l1_table", (uint32_t)(uintptr_t)stage36_l1_table);

    build_identity_table();
    xnu_log_kv32("mmu_entry_low", table_entry_for(0x00008000u));
    xnu_log_kv32("mmu_entry_high_alias", table_entry_for(STAGE36_HIGH_ALIAS_BASE));
    xnu_log_kv32("mmu_entry_high_ram_console", table_entry_for(STAGE36_RAM_CONSOLE_ALIAS_BASE));
    xnu_log_kv32("mmu_entry_high_gic", table_entry_for(STAGE36_GIC_ALIAS_BASE));
    xnu_log_kv32("mmu_entry_ram_console", table_entry_for(RAM_CONSOLE_BASE));
    xnu_log_kv32("mmu_entry_imem", table_entry_for(RESTART_REASON));
    xnu_log_kv32("mmu_entry_gic", table_entry_for(PE_state_stage36.gicDistributorBase));
    xnu_log_kv32("mmu_entry_timer", table_entry_for(PE_state_stage36.timerBase));
    xnu_log_kv32("mmu_entry_pshold", table_entry_for(MSM8974_PSHOLD));

    enable_identity_mmu();

    xnu_log_kv32("mmu_sctlr_after", read_sctlr());
    xnu_log_kv32("mmu_ttbr0_after", read_ttbr0());
    xnu_log_kv32("mmu_dacr_after", read_dacr());

    stage36_mmu_probe_word = 0x10aa55ffu;
    xnu_log_kv32("mmu_probe_word", stage36_mmu_probe_word);
    xnu_log_kv32("mmu_ram_console_sig", *ram_console_sig);
    xnu_log_kv32("mmu_restart_reason_read", *restart_reason);
    xnu_log_kv32("mmu_gicd_ctlr_read", *gicd_ctlr);
    xnu_log_kv32("mmu_gicc_ctlr_read", *gicc_ctlr);
    xnu_log_kv32("mmu_timer_freq_check", timebase_freq_hz());

    if ((read_sctlr() & 1u) == 0u) {
        xnu_log_puts("mmu identity selftest failed: SCTLR.M clear\n");
        return 0;
    }
    if (stage36_mmu_probe_word != 0x10aa55ffu) {
        xnu_log_puts("mmu identity selftest failed: data probe mismatch\n");
        return 0;
    }
    if (*ram_console_sig != RAM_CONSOLE_SIG) {
        xnu_log_puts("mmu identity selftest failed: ram_console sig mismatch\n");
        return 0;
    }
    if ((*gicd_ctlr & 1u) == 0u || (*gicc_ctlr & 1u) == 0u) {
        xnu_log_puts("mmu identity selftest failed: GIC control reads suspicious\n");
        return 0;
    }

    xnu_log_puts("mmu identity selftest ok\n");
    return 1;
}

int mmu_high_alias_selftest(void)
{
    volatile uint32_t *alias_probe;
    volatile uint32_t *identity_probe = &stage36_mmu_alias_probe;
    volatile uint32_t *alias_vector;
    volatile uint32_t *identity_vector = (volatile uint32_t *)(uintptr_t)stage36_vectors;
    volatile uint32_t *alias_ram_console;
    volatile uint32_t *identity_ram_console = (volatile uint32_t *)(uintptr_t)RAM_CONSOLE_BASE;
    volatile uint32_t *alias_gicd_ctlr;
    volatile uint32_t *identity_gicd_ctlr = (volatile uint32_t *)(uintptr_t)PE_state_stage36.gicDistributorBase;
    uint32_t probe_phys = (uint32_t)(uintptr_t)&stage36_mmu_alias_probe;
    uint32_t vector_phys = (uint32_t)(uintptr_t)stage36_vectors;

    xnu_log_puts("mmu high alias selftest begin\n");
    xnu_log_kv32("mmu_alias_base", STAGE36_HIGH_ALIAS_BASE);
    xnu_log_kv32("mmu_alias_entry", table_entry_for(STAGE36_HIGH_ALIAS_BASE));
    xnu_log_kv32("mmu_alias_probe_phys", probe_phys);
    xnu_log_kv32("mmu_alias_vector_phys", vector_phys);

    if ((read_sctlr() & 1u) == 0u) {
        xnu_log_puts("mmu high alias selftest failed: MMU disabled\n");
        return 0;
    }

    alias_probe = (volatile uint32_t *)(uintptr_t)(STAGE36_HIGH_ALIAS_BASE + probe_phys);
    alias_vector = (volatile uint32_t *)(uintptr_t)(STAGE36_HIGH_ALIAS_BASE + vector_phys);
    alias_ram_console = (volatile uint32_t *)(uintptr_t)STAGE36_RAM_CONSOLE_ALIAS_BASE;
    alias_gicd_ctlr = (volatile uint32_t *)(uintptr_t)(STAGE36_GIC_ALIAS_BASE + (PE_state_stage36.gicDistributorBase - 0xf9000000u));

    *identity_probe = 0x11112222u;
    xnu_log_kv32("mmu_alias_read_after_identity_write", *alias_probe);
    if (*alias_probe != 0x11112222u) {
        xnu_log_puts("mmu high alias selftest failed: alias read mismatch\n");
        return 0;
    }

    *alias_probe = 0x33334444u;
    xnu_log_kv32("mmu_identity_read_after_alias_write", *identity_probe);
    if (*identity_probe != 0x33334444u) {
        xnu_log_puts("mmu high alias selftest failed: identity read mismatch\n");
        return 0;
    }

    xnu_log_kv32("mmu_alias_vector_word", *alias_vector);
    xnu_log_kv32("mmu_identity_vector_word", *identity_vector);
    if (*alias_vector != *identity_vector) {
        xnu_log_puts("mmu high alias selftest failed: vector alias mismatch\n");
        return 0;
    }

    xnu_log_kv32("mmu_alias_ram_console_sig", *alias_ram_console);
    xnu_log_kv32("mmu_identity_ram_console_sig", *identity_ram_console);
    if (*alias_ram_console != RAM_CONSOLE_SIG) {
        xnu_log_puts("mmu high alias selftest failed: ram_console alias mismatch\n");
        return 0;
    }

    xnu_log_kv32("mmu_alias_gicd_ctlr", *alias_gicd_ctlr);
    xnu_log_kv32("mmu_identity_gicd_ctlr", *identity_gicd_ctlr);
    if ((*alias_gicd_ctlr & 1u) == 0u || *alias_gicd_ctlr != *identity_gicd_ctlr) {
        xnu_log_puts("mmu high alias selftest failed: GIC alias mismatch\n");
        return 0;
    }

    xnu_log_puts("mmu high alias selftest ok\n");
    return 1;
}

int mmu_high_call_selftest(void)
{
    typedef uint32_t (*alias_fn_t)(uint32_t, volatile struct stage36_alias_state *);

    const uint32_t input = 0x55667788u;
    const uint32_t expected = (input ^ 0x12c0ffeeu) + 0x1234u;
    uint32_t fn_phys = (uint32_t)(uintptr_t)stage36_high_alias_target;
    uint32_t state_phys = (uint32_t)(uintptr_t)&stage36_alias_state_block;
    alias_fn_t alias_fn = (alias_fn_t)(uintptr_t)(STAGE36_HIGH_ALIAS_BASE + fn_phys);
    volatile struct stage36_alias_state *alias_state =
        (volatile struct stage36_alias_state *)(uintptr_t)(STAGE36_HIGH_ALIAS_BASE + state_phys);
    uint32_t result;

    xnu_log_puts("mmu high call selftest begin\n");
    xnu_log_kv32("mmu_high_call_fn_phys", fn_phys);
    xnu_log_kv32("mmu_high_call_fn_virt", (uint32_t)(uintptr_t)alias_fn);
    xnu_log_kv32("mmu_high_call_state_phys", state_phys);
    xnu_log_kv32("mmu_high_call_state_virt", (uint32_t)(uintptr_t)alias_state);
    xnu_log_kv32("mmu_high_call_input", input);
    xnu_log_kv32("mmu_high_call_expected", expected);

    memset(&stage36_alias_state_block, 0, sizeof(stage36_alias_state_block));

    if ((read_sctlr() & 1u) == 0u) {
        xnu_log_puts("mmu high call selftest failed: MMU disabled\n");
        return 0;
    }

    result = alias_fn(input, alias_state);

    xnu_log_kv32("mmu_high_call_result", result);
    xnu_log_kv32("mmu_high_call_magic_id", stage36_alias_state_block.magic);
    xnu_log_kv32("mmu_high_call_input_id", stage36_alias_state_block.input);
    xnu_log_kv32("mmu_high_call_result_id", stage36_alias_state_block.result);
    xnu_log_kv32("mmu_high_call_checksum_id", stage36_alias_state_block.checksum);
    xnu_log_kv32("mmu_high_call_magic_alias", alias_state->magic);
    xnu_log_kv32("mmu_high_call_checksum_alias", alias_state->checksum);

    if (result != expected || stage36_alias_state_block.result != expected) {
        xnu_log_puts("mmu high call selftest failed: result mismatch\n");
        return 0;
    }
    if (stage36_alias_state_block.magic != 0x12001200u || stage36_alias_state_block.input != input) {
        xnu_log_puts("mmu high call selftest failed: state mismatch\n");
        return 0;
    }
    if (stage36_alias_state_block.checksum != (stage36_alias_state_block.magic ^ input ^ expected)) {
        xnu_log_puts("mmu high call selftest failed: checksum mismatch\n");
        return 0;
    }
    if (alias_state->checksum != stage36_alias_state_block.checksum) {
        xnu_log_puts("mmu high call selftest failed: alias state mismatch\n");
        return 0;
    }

    xnu_log_puts("mmu high call selftest ok\n");
    return 1;
}

int mmu_high_bootstrap_selftest(void)
{
    typedef uint32_t (*bootstrap_fn_t)(struct boot_args *,
                                       volatile struct pe_platform_state *,
                                       volatile struct stage36_bootstrap_state *);

    uint32_t fn_phys = (uint32_t)(uintptr_t)stage36_kernel_root;
    uint32_t args_phys = (uint32_t)(uintptr_t)PE_state_stage36.bootArgs;
    uint32_t pe_phys = (uint32_t)(uintptr_t)&PE_state_stage36;
    uint32_t state_phys = (uint32_t)(uintptr_t)&stage36_bootstrap_state_block;
    uint32_t handoff_phys = (uint32_t)(uintptr_t)&stage36_startup_handoff_block;
    uint32_t context_phys = (uint32_t)(uintptr_t)&stage36_kernel_context_block;
    uint32_t vm_plan_phys = (uint32_t)(uintptr_t)&stage36_vm_plan_block;
    uint32_t vm_state_phys = (uint32_t)(uintptr_t)&stage36_vm_state_block;
    uint32_t allocator_phys = (uint32_t)(uintptr_t)&stage36_boot_allocator_block;
    bootstrap_fn_t bootstrap_fn = (bootstrap_fn_t)(uintptr_t)(STAGE36_HIGH_ALIAS_BASE + fn_phys);
    struct boot_args *alias_args = (struct boot_args *)(uintptr_t)(STAGE36_HIGH_ALIAS_BASE + args_phys);
    volatile struct pe_platform_state *alias_pe =
        (volatile struct pe_platform_state *)(uintptr_t)(STAGE36_HIGH_ALIAS_BASE + pe_phys);
    volatile struct stage36_bootstrap_state *alias_state =
        (volatile struct stage36_bootstrap_state *)(uintptr_t)(STAGE36_HIGH_ALIAS_BASE + state_phys);
    volatile struct stage36_startup_handoff *alias_handoff =
        (volatile struct stage36_startup_handoff *)(uintptr_t)(STAGE36_HIGH_ALIAS_BASE + handoff_phys);
    volatile struct stage36_kernel_context *alias_context =
        (volatile struct stage36_kernel_context *)(uintptr_t)(STAGE36_HIGH_ALIAS_BASE + context_phys);
    volatile struct stage36_vm_bootstrap_plan *alias_vm_plan =
        (volatile struct stage36_vm_bootstrap_plan *)(uintptr_t)(STAGE36_HIGH_ALIAS_BASE + vm_plan_phys);
    volatile struct stage36_vm_bootstrap_state *alias_vm_state =
        (volatile struct stage36_vm_bootstrap_state *)(uintptr_t)(STAGE36_HIGH_ALIAS_BASE + vm_state_phys);
    volatile struct stage36_bootstrap_allocator *alias_allocator =
        (volatile struct stage36_bootstrap_allocator *)(uintptr_t)(STAGE36_HIGH_ALIAS_BASE + allocator_phys);
    uint32_t expected;
    uint32_t expected_handoff;
    uint32_t result;

    xnu_log_puts("mmu high bootstrap selftest begin\n");
    xnu_log_kv32("mmu_high_bootstrap_fn_phys", fn_phys);
    xnu_log_kv32("mmu_high_bootstrap_fn_virt", (uint32_t)(uintptr_t)bootstrap_fn);
    xnu_log_kv32("mmu_high_bootstrap_args_phys", args_phys);
    xnu_log_kv32("mmu_high_bootstrap_args_virt", (uint32_t)(uintptr_t)alias_args);
    xnu_log_kv32("mmu_high_bootstrap_pe_phys", pe_phys);
    xnu_log_kv32("mmu_high_bootstrap_pe_virt", (uint32_t)(uintptr_t)alias_pe);
    xnu_log_kv32("mmu_high_bootstrap_state_phys", state_phys);
    xnu_log_kv32("mmu_high_bootstrap_state_virt", (uint32_t)(uintptr_t)alias_state);
    xnu_log_kv32("mmu_high_bootstrap_handoff_phys", handoff_phys);
    xnu_log_kv32("mmu_high_bootstrap_handoff_virt", (uint32_t)(uintptr_t)alias_handoff);
    xnu_log_kv32("mmu_high_bootstrap_context_phys", context_phys);
    xnu_log_kv32("mmu_high_bootstrap_context_virt", (uint32_t)(uintptr_t)alias_context);
    xnu_log_kv32("mmu_high_bootstrap_vm_plan_phys", vm_plan_phys);
    xnu_log_kv32("mmu_high_bootstrap_vm_plan_virt", (uint32_t)(uintptr_t)alias_vm_plan);
    xnu_log_kv32("mmu_high_bootstrap_vm_state_phys", vm_state_phys);
    xnu_log_kv32("mmu_high_bootstrap_vm_state_virt", (uint32_t)(uintptr_t)alias_vm_state);
    xnu_log_kv32("mmu_high_bootstrap_allocator_phys", allocator_phys);
    xnu_log_kv32("mmu_high_bootstrap_allocator_virt", (uint32_t)(uintptr_t)alias_allocator);

    memset(&stage36_bootstrap_state_block, 0, sizeof(stage36_bootstrap_state_block));
    memset(&stage36_startup_handoff_block, 0, sizeof(stage36_startup_handoff_block));
    memset(&stage36_kernel_context_block, 0, sizeof(stage36_kernel_context_block));
    memset(&stage36_vm_plan_block, 0, sizeof(stage36_vm_plan_block));
    memset(&stage36_vm_state_block, 0, sizeof(stage36_vm_state_block));
    memset(&stage36_boot_allocator_block, 0, sizeof(stage36_boot_allocator_block));

    if ((read_sctlr() & 1u) == 0u) {
        xnu_log_puts("mmu high bootstrap selftest failed: MMU disabled\n");
        return 0;
    }

    result = bootstrap_fn(alias_args, alias_pe, alias_state);

    expected = stage36_bootstrap_checksum(&stage36_bootstrap_state_block);

    expected_handoff = stage36_startup_handoff_block.version ^ stage36_startup_handoff_block.size ^
        stage36_startup_handoff_block.root_steps ^ stage36_startup_handoff_block.routine_status ^
        stage36_startup_handoff_block.startup_status ^ stage36_startup_handoff_block.root_status ^
        stage36_startup_handoff_block.boot_args_virt ^ stage36_startup_handoff_block.dt_virt ^
        stage36_startup_handoff_block.timebase_freq ^ stage36_startup_handoff_block.interrupt_mask;

    xnu_log_kv32("mmu_high_bootstrap_result", result);
    xnu_log_kv32("mmu_high_bootstrap_expected_checksum", expected);
    xnu_log_kv32("mmu_high_bootstrap_magic_id", stage36_bootstrap_state_block.magic);
    xnu_log_kv32("mmu_high_bootstrap_rev_ver_id", stage36_bootstrap_state_block.boot_args_rev_ver);
    xnu_log_kv32("mmu_high_bootstrap_machine_id", stage36_bootstrap_state_block.machine_type);
    xnu_log_kv32("mmu_high_bootstrap_dt_len_id", stage36_bootstrap_state_block.device_tree_length);
    xnu_log_kv32("mmu_high_bootstrap_mem_base_id", stage36_bootstrap_state_block.pe_memory_base);
    xnu_log_kv32("mmu_high_bootstrap_mem_size_id", stage36_bootstrap_state_block.pe_memory_size);
    xnu_log_kv32("mmu_high_bootstrap_cpu_count_id", stage36_bootstrap_state_block.pe_cpu_count);
    xnu_log_kv32("mmu_high_bootstrap_gic_dist_id", stage36_bootstrap_state_block.pe_gic_dist_base);
    xnu_log_kv32("mmu_high_bootstrap_gic_cpu_id", stage36_bootstrap_state_block.pe_gic_cpu_base);
    xnu_log_kv32("mmu_high_bootstrap_timer_base_id", stage36_bootstrap_state_block.pe_timer_base);
    xnu_log_kv32("mmu_high_bootstrap_timer_freq_id", stage36_bootstrap_state_block.pe_timer_frequency);
    xnu_log_kv32("mmu_high_bootstrap_vector_id", stage36_bootstrap_state_block.pe_vector_base);
    xnu_log_kv32("mmu_high_bootstrap_boot_flags_id", stage36_bootstrap_state_block.boot_flags);
    xnu_log_kv32("mmu_high_bootstrap_validation_id", stage36_bootstrap_state_block.validation_mask);
    xnu_log_kv32("mmu_high_bootstrap_root_steps_id", stage36_bootstrap_state_block.root_steps);
    xnu_log_kv32("mmu_high_bootstrap_root_status_id", stage36_bootstrap_state_block.root_status);
    xnu_log_kv32("mmu_high_bootstrap_init_steps_id", stage36_bootstrap_state_block.init_steps);
    xnu_log_kv32("mmu_high_bootstrap_init_status_id", stage36_bootstrap_state_block.init_status);
    xnu_log_kv32("mmu_high_bootstrap_timebase_freq_id", stage36_bootstrap_state_block.init_timebase_freq);
    xnu_log_kv32("mmu_high_bootstrap_timebase_delta_us_id", stage36_bootstrap_state_block.init_timebase_delta_us);
    xnu_log_kv32("mmu_high_bootstrap_gic_irq_count_id", stage36_bootstrap_state_block.init_gic_irq_count);
    xnu_log_kv32("mmu_high_bootstrap_gic_cpu_count_id", stage36_bootstrap_state_block.init_gic_cpu_count);
    xnu_log_kv32("mmu_high_bootstrap_gic_dist_ctlr_id", stage36_bootstrap_state_block.init_gic_dist_ctlr);
    xnu_log_kv32("mmu_high_bootstrap_gic_cpu_ctlr_id", stage36_bootstrap_state_block.init_gic_cpu_ctlr);
    xnu_log_kv32("mmu_high_bootstrap_root_boot_args_virt_id", stage36_bootstrap_state_block.root_boot_args_virt);
    xnu_log_kv32("mmu_high_bootstrap_root_dt_virt_id", stage36_bootstrap_state_block.root_dt_virt);
    xnu_log_kv32("mmu_high_bootstrap_root_dt_children_id", stage36_bootstrap_state_block.root_dt_root_children);
    xnu_log_kv32("mmu_high_bootstrap_root_dt_mem_base_id", stage36_bootstrap_state_block.root_dt_memory_base);
    xnu_log_kv32("mmu_high_bootstrap_root_dt_mem_size_id", stage36_bootstrap_state_block.root_dt_memory_size);
    xnu_log_kv32("mmu_high_bootstrap_root_dt_timer_freq_id", stage36_bootstrap_state_block.root_dt_timer_frequency);
    xnu_log_kv32("mmu_high_bootstrap_root_dt_summary_status_id", stage36_bootstrap_state_block.root_dt_summary_status);
    xnu_log_kv32("mmu_high_bootstrap_platform_version_id", stage36_bootstrap_state_block.platform_result_version);
    xnu_log_kv32("mmu_high_bootstrap_platform_size_id", stage36_bootstrap_state_block.platform_result_size);
    xnu_log_kv32("mmu_high_bootstrap_platform_consistency_id", stage36_bootstrap_state_block.platform_result_consistency);
    xnu_log_kv32("mmu_high_bootstrap_platform_dt_cpu_count_id", stage36_bootstrap_state_block.platform_dt_cpu_count);
    xnu_log_kv32("mmu_high_bootstrap_platform_pe_cpu_count_id", stage36_bootstrap_state_block.platform_pe_cpu_count);
    xnu_log_kv32("mmu_high_bootstrap_platform_memory_base_id", stage36_bootstrap_state_block.platform_memory_base);
    xnu_log_kv32("mmu_high_bootstrap_platform_memory_size_id", stage36_bootstrap_state_block.platform_memory_size);
    xnu_log_kv32("mmu_high_bootstrap_platform_gic_dist_id", stage36_bootstrap_state_block.platform_gic_dist_base);
    xnu_log_kv32("mmu_high_bootstrap_platform_gic_cpu_id", stage36_bootstrap_state_block.platform_gic_cpu_base);
    xnu_log_kv32("mmu_high_bootstrap_platform_timer_freq_id", stage36_bootstrap_state_block.platform_timer_frequency);
    xnu_log_kv32("mmu_high_bootstrap_platform_status_id", stage36_bootstrap_state_block.platform_result_status);
    xnu_log_kv32("mmu_high_bootstrap_phase_count_id", stage36_bootstrap_state_block.phase_count);
    xnu_log_kv32("mmu_high_bootstrap_phase_mask_id", stage36_bootstrap_state_block.phase_completed_mask);
    xnu_log_kv32("mmu_high_bootstrap_phase_checksum_id", stage36_bootstrap_state_block.phase_status_checksum);
    xnu_log_kv32("mmu_high_bootstrap_phase0_status_id", stage36_bootstrap_state_block.phase0_status);
    xnu_log_kv32("mmu_high_bootstrap_phase1_status_id", stage36_bootstrap_state_block.phase1_status);
    xnu_log_kv32("mmu_high_bootstrap_phase2_status_id", stage36_bootstrap_state_block.phase2_status);
    xnu_log_kv32("mmu_high_bootstrap_phase3_status_id", stage36_bootstrap_state_block.phase3_status);
    xnu_log_kv32("mmu_high_bootstrap_phase0_required_services_id", stage36_bootstrap_state_block.phase0_required_services);
    xnu_log_kv32("mmu_high_bootstrap_phase1_required_services_id", stage36_bootstrap_state_block.phase1_required_services);
    xnu_log_kv32("mmu_high_bootstrap_phase2_required_services_id", stage36_bootstrap_state_block.phase2_required_services);
    xnu_log_kv32("mmu_high_bootstrap_phase3_required_services_id", stage36_bootstrap_state_block.phase3_required_services);
    xnu_log_kv32("mmu_high_bootstrap_phase_service_dependency_mask_id", stage36_bootstrap_state_block.phase_service_dependency_mask);
    xnu_log_kv32("mmu_high_bootstrap_phase_service_satisfied_mask_id", stage36_bootstrap_state_block.phase_service_satisfied_mask);
    xnu_log_kv32("mmu_high_bootstrap_phase_service_checksum_id", stage36_bootstrap_state_block.phase_service_status_checksum);
    xnu_log_kv32("mmu_high_bootstrap_phase_service_status_id", stage36_bootstrap_state_block.phase_service_dependency_status);
    xnu_log_kv32("mmu_high_bootstrap_phase_dispatcher_count_id", stage36_bootstrap_state_block.phase_dispatcher_count);
    xnu_log_kv32("mmu_high_bootstrap_phase_dispatcher_order_mask_id", stage36_bootstrap_state_block.phase_dispatcher_order_mask);
    xnu_log_kv32("mmu_high_bootstrap_phase_dispatcher_handler_mask_id", stage36_bootstrap_state_block.phase_dispatcher_handler_mask);
    xnu_log_kv32("mmu_high_bootstrap_phase_dispatcher_checksum_id", stage36_bootstrap_state_block.phase_dispatcher_status_checksum);
    xnu_log_kv32("mmu_high_bootstrap_phase0_descriptor_id", stage36_bootstrap_state_block.phase0_descriptor_id);
    xnu_log_kv32("mmu_high_bootstrap_phase1_descriptor_id", stage36_bootstrap_state_block.phase1_descriptor_id);
    xnu_log_kv32("mmu_high_bootstrap_phase2_descriptor_id", stage36_bootstrap_state_block.phase2_descriptor_id);
    xnu_log_kv32("mmu_high_bootstrap_phase3_descriptor_id", stage36_bootstrap_state_block.phase3_descriptor_id);
    xnu_log_kv32("mmu_high_bootstrap_phase0_handler_result_id", stage36_bootstrap_state_block.phase0_handler_result);
    xnu_log_kv32("mmu_high_bootstrap_phase1_handler_result_id", stage36_bootstrap_state_block.phase1_handler_result);
    xnu_log_kv32("mmu_high_bootstrap_phase2_handler_result_id", stage36_bootstrap_state_block.phase2_handler_result);
    xnu_log_kv32("mmu_high_bootstrap_phase3_handler_result_id", stage36_bootstrap_state_block.phase3_handler_result);
    xnu_log_kv32("mmu_high_bootstrap_phase_dispatcher_status_id", stage36_bootstrap_state_block.phase_dispatcher_status);
    xnu_log_kv32("mmu_high_bootstrap_service_count_id", stage36_bootstrap_state_block.service_count);
    xnu_log_kv32("mmu_high_bootstrap_service_mask_id", stage36_bootstrap_state_block.service_available_mask);
    xnu_log_kv32("mmu_high_bootstrap_service_checksum_id", stage36_bootstrap_state_block.service_status_checksum);
    xnu_log_kv32("mmu_high_bootstrap_service_logging_status_id", stage36_bootstrap_state_block.service_logging_status);
    xnu_log_kv32("mmu_high_bootstrap_service_timebase_status_id", stage36_bootstrap_state_block.service_timebase_status);
    xnu_log_kv32("mmu_high_bootstrap_service_platform_status_id", stage36_bootstrap_state_block.service_platform_status);
    xnu_log_kv32("mmu_high_bootstrap_service_interrupts_status_id", stage36_bootstrap_state_block.service_interrupts_status);
    xnu_log_kv32("mmu_high_bootstrap_service_dispatcher_count_id", stage36_bootstrap_state_block.service_dispatcher_count);
    xnu_log_kv32("mmu_high_bootstrap_service_dispatcher_order_mask_id", stage36_bootstrap_state_block.service_dispatcher_order_mask);
    xnu_log_kv32("mmu_high_bootstrap_service_dispatcher_handler_mask_id", stage36_bootstrap_state_block.service_dispatcher_handler_mask);
    xnu_log_kv32("mmu_high_bootstrap_service_dispatcher_checksum_id", stage36_bootstrap_state_block.service_dispatcher_status_checksum);
    xnu_log_kv32("mmu_high_bootstrap_service_logging_descriptor_id", stage36_bootstrap_state_block.service_logging_descriptor_id);
    xnu_log_kv32("mmu_high_bootstrap_service_timebase_descriptor_id", stage36_bootstrap_state_block.service_timebase_descriptor_id);
    xnu_log_kv32("mmu_high_bootstrap_service_platform_descriptor_id", stage36_bootstrap_state_block.service_platform_descriptor_id);
    xnu_log_kv32("mmu_high_bootstrap_service_interrupts_descriptor_id", stage36_bootstrap_state_block.service_interrupts_descriptor_id);
    xnu_log_kv32("mmu_high_bootstrap_service_logging_handler_result_id", stage36_bootstrap_state_block.service_logging_handler_result);
    xnu_log_kv32("mmu_high_bootstrap_service_timebase_handler_result_id", stage36_bootstrap_state_block.service_timebase_handler_result);
    xnu_log_kv32("mmu_high_bootstrap_service_platform_handler_result_id", stage36_bootstrap_state_block.service_platform_handler_result);
    xnu_log_kv32("mmu_high_bootstrap_service_interrupts_handler_result_id", stage36_bootstrap_state_block.service_interrupts_handler_result);
    xnu_log_kv32("mmu_high_bootstrap_service_dispatcher_status_id", stage36_bootstrap_state_block.service_dispatcher_status);
    xnu_log_kv32("mmu_high_bootstrap_registry_version_id", stage36_bootstrap_state_block.registry_version);
    xnu_log_kv32("mmu_high_bootstrap_registry_size_id", stage36_bootstrap_state_block.registry_size);
    xnu_log_kv32("mmu_high_bootstrap_registry_service_descriptor_mask_id", stage36_bootstrap_state_block.registry_service_descriptor_mask);
    xnu_log_kv32("mmu_high_bootstrap_registry_phase_descriptor_mask_id", stage36_bootstrap_state_block.registry_phase_descriptor_mask);
    xnu_log_kv32("mmu_high_bootstrap_registry_dependency_coverage_mask_id", stage36_bootstrap_state_block.registry_dependency_coverage_mask);
    xnu_log_kv32("mmu_high_bootstrap_registry_dispatch_coverage_mask_id", stage36_bootstrap_state_block.registry_dispatch_coverage_mask);
    xnu_log_kv32("mmu_high_bootstrap_registry_checksum_id", stage36_bootstrap_state_block.registry_status_checksum);
    xnu_log_kv32("mmu_high_bootstrap_registry_status_id", stage36_bootstrap_state_block.registry_status);
    xnu_log_kv32("mmu_high_bootstrap_boot_policy_version_id", stage36_bootstrap_state_block.boot_policy_version);
    xnu_log_kv32("mmu_high_bootstrap_boot_policy_size_id", stage36_bootstrap_state_block.boot_policy_size);
    xnu_log_kv32("mmu_high_bootstrap_boot_policy_required_root_steps_id", stage36_bootstrap_state_block.boot_policy_required_root_steps);
    xnu_log_kv32("mmu_high_bootstrap_boot_policy_required_service_mask_id", stage36_bootstrap_state_block.boot_policy_required_service_mask);
    xnu_log_kv32("mmu_high_bootstrap_boot_policy_required_phase_mask_id", stage36_bootstrap_state_block.boot_policy_required_phase_mask);
    xnu_log_kv32("mmu_high_bootstrap_boot_policy_required_dependency_mask_id", stage36_bootstrap_state_block.boot_policy_required_dependency_mask);
    xnu_log_kv32("mmu_high_bootstrap_boot_policy_required_dispatch_coverage_mask_id", stage36_bootstrap_state_block.boot_policy_required_dispatch_coverage_mask);
    xnu_log_kv32("mmu_high_bootstrap_boot_policy_required_registry_status_id", stage36_bootstrap_state_block.boot_policy_required_registry_status);
    xnu_log_kv32("mmu_high_bootstrap_boot_policy_observed_root_steps_id", stage36_bootstrap_state_block.boot_policy_observed_root_steps);
    xnu_log_kv32("mmu_high_bootstrap_boot_policy_observed_service_mask_id", stage36_bootstrap_state_block.boot_policy_observed_service_mask);
    xnu_log_kv32("mmu_high_bootstrap_boot_policy_observed_phase_mask_id", stage36_bootstrap_state_block.boot_policy_observed_phase_mask);
    xnu_log_kv32("mmu_high_bootstrap_boot_policy_observed_dependency_mask_id", stage36_bootstrap_state_block.boot_policy_observed_dependency_mask);
    xnu_log_kv32("mmu_high_bootstrap_boot_policy_observed_dispatch_coverage_mask_id", stage36_bootstrap_state_block.boot_policy_observed_dispatch_coverage_mask);
    xnu_log_kv32("mmu_high_bootstrap_boot_policy_observed_registry_status_id", stage36_bootstrap_state_block.boot_policy_observed_registry_status);
    xnu_log_kv32("mmu_high_bootstrap_boot_policy_satisfied_mask_id", stage36_bootstrap_state_block.boot_policy_satisfied_mask);
    xnu_log_kv32("mmu_high_bootstrap_boot_policy_checksum_id", stage36_bootstrap_state_block.boot_policy_status_checksum);
    xnu_log_kv32("mmu_high_bootstrap_boot_policy_status_id", stage36_bootstrap_state_block.boot_policy_status);
    xnu_log_kv32("mmu_high_bootstrap_manifest_version_id", stage36_bootstrap_state_block.manifest_version);
    xnu_log_kv32("mmu_high_bootstrap_manifest_record_count_id", stage36_bootstrap_state_block.manifest_record_count);
    xnu_log_kv32("mmu_high_bootstrap_manifest_order_mask_id", stage36_bootstrap_state_block.manifest_order_mask);
    xnu_log_kv32("mmu_high_bootstrap_manifest_satisfied_mask_id", stage36_bootstrap_state_block.manifest_satisfied_mask);
    xnu_log_kv32("mmu_high_bootstrap_manifest_required_root_steps_id", stage36_bootstrap_state_block.manifest_required_root_steps);
    xnu_log_kv32("mmu_high_bootstrap_manifest_observed_root_steps_id", stage36_bootstrap_state_block.manifest_observed_root_steps);
    xnu_log_kv32("mmu_high_bootstrap_manifest_service_observed_mask_id", stage36_bootstrap_state_block.manifest_service_observed_mask);
    xnu_log_kv32("mmu_high_bootstrap_manifest_phase_observed_mask_id", stage36_bootstrap_state_block.manifest_phase_observed_mask);
    xnu_log_kv32("mmu_high_bootstrap_manifest_dependency_observed_mask_id", stage36_bootstrap_state_block.manifest_dependency_observed_mask);
    xnu_log_kv32("mmu_high_bootstrap_manifest_dispatch_observed_mask_id", stage36_bootstrap_state_block.manifest_dispatch_observed_mask);
    xnu_log_kv32("mmu_high_bootstrap_manifest_policy_observed_mask_id", stage36_bootstrap_state_block.manifest_policy_observed_mask);
    xnu_log_kv32("mmu_high_bootstrap_manifest_boot_observed_status_id", stage36_bootstrap_state_block.manifest_boot_observed_status);
    xnu_log_kv32("mmu_high_bootstrap_manifest_checksum_id", stage36_bootstrap_state_block.manifest_status_checksum);
    xnu_log_kv32("mmu_high_bootstrap_manifest_status_id", stage36_bootstrap_state_block.manifest_status);
    xnu_log_kv32("mmu_high_bootstrap_launch_version_id", stage36_bootstrap_state_block.launch_contract_version);
    xnu_log_kv32("mmu_high_bootstrap_launch_size_id", stage36_bootstrap_state_block.launch_contract_size);
    xnu_log_kv32("mmu_high_bootstrap_launch_required_root_steps_id", stage36_bootstrap_state_block.launch_required_root_steps);
    xnu_log_kv32("mmu_high_bootstrap_launch_observed_root_steps_id", stage36_bootstrap_state_block.launch_observed_root_steps);
    xnu_log_kv32("mmu_high_bootstrap_launch_required_root_status_id", stage36_bootstrap_state_block.launch_required_root_status);
    xnu_log_kv32("mmu_high_bootstrap_launch_observed_root_status_id", stage36_bootstrap_state_block.launch_observed_root_status);
    xnu_log_kv32("mmu_high_bootstrap_launch_observed_manifest_status_id", stage36_bootstrap_state_block.launch_observed_manifest_status);
    xnu_log_kv32("mmu_high_bootstrap_launch_observed_policy_status_id", stage36_bootstrap_state_block.launch_observed_policy_status);
    xnu_log_kv32("mmu_high_bootstrap_launch_observed_mmu_state_id", stage36_bootstrap_state_block.launch_observed_mmu_state);
    xnu_log_kv32("mmu_high_bootstrap_launch_observed_timebase_freq_id", stage36_bootstrap_state_block.launch_observed_timebase_freq);
    xnu_log_kv32("mmu_high_bootstrap_launch_observed_interrupt_mask_id", stage36_bootstrap_state_block.launch_observed_interrupt_mask);
    xnu_log_kv32("mmu_high_bootstrap_launch_satisfied_mask_id", stage36_bootstrap_state_block.launch_satisfied_mask);
    xnu_log_kv32("mmu_high_bootstrap_launch_checksum_id", stage36_bootstrap_state_block.launch_status_checksum);
    xnu_log_kv32("mmu_high_bootstrap_launch_status_id", stage36_bootstrap_state_block.launch_status);
    xnu_log_kv32("mmu_high_bootstrap_startup_version_id", stage36_bootstrap_state_block.startup_boundary_version);
    xnu_log_kv32("mmu_high_bootstrap_startup_size_id", stage36_bootstrap_state_block.startup_boundary_size);
    xnu_log_kv32("mmu_high_bootstrap_startup_required_root_steps_id", stage36_bootstrap_state_block.startup_required_root_steps);
    xnu_log_kv32("mmu_high_bootstrap_startup_observed_root_steps_id", stage36_bootstrap_state_block.startup_observed_root_steps);
    xnu_log_kv32("mmu_high_bootstrap_startup_observed_launch_status_id", stage36_bootstrap_state_block.startup_observed_launch_status);
    xnu_log_kv32("mmu_high_bootstrap_startup_observed_root_status_id", stage36_bootstrap_state_block.startup_observed_root_status);
    xnu_log_kv32("mmu_high_bootstrap_startup_observed_manifest_status_id", stage36_bootstrap_state_block.startup_observed_manifest_status);
    xnu_log_kv32("mmu_high_bootstrap_startup_observed_boot_args_virt_id", stage36_bootstrap_state_block.startup_observed_boot_args_virt);
    xnu_log_kv32("mmu_high_bootstrap_startup_observed_dt_virt_id", stage36_bootstrap_state_block.startup_observed_dt_virt);
    xnu_log_kv32("mmu_high_bootstrap_startup_observed_timebase_freq_id", stage36_bootstrap_state_block.startup_observed_timebase_freq);
    xnu_log_kv32("mmu_high_bootstrap_startup_observed_interrupt_mask_id", stage36_bootstrap_state_block.startup_observed_interrupt_mask);
    xnu_log_kv32("mmu_high_bootstrap_startup_satisfied_mask_id", stage36_bootstrap_state_block.startup_satisfied_mask);
    xnu_log_kv32("mmu_high_bootstrap_startup_checksum_id", stage36_bootstrap_state_block.startup_status_checksum);
    xnu_log_kv32("mmu_high_bootstrap_startup_status_id", stage36_bootstrap_state_block.startup_status);
    xnu_log_kv32("mmu_high_bootstrap_startup_routine_version_id", stage36_bootstrap_state_block.startup_routine_version);
    xnu_log_kv32("mmu_high_bootstrap_startup_routine_size_id", stage36_bootstrap_state_block.startup_routine_size);
    xnu_log_kv32("mmu_high_bootstrap_startup_routine_required_root_steps_id", stage36_bootstrap_state_block.startup_routine_required_root_steps);
    xnu_log_kv32("mmu_high_bootstrap_startup_routine_observed_root_steps_id", stage36_bootstrap_state_block.startup_routine_observed_root_steps);
    xnu_log_kv32("mmu_high_bootstrap_startup_routine_observed_startup_status_id", stage36_bootstrap_state_block.startup_routine_observed_startup_status);
    xnu_log_kv32("mmu_high_bootstrap_startup_routine_observed_launch_status_id", stage36_bootstrap_state_block.startup_routine_observed_launch_status);
    xnu_log_kv32("mmu_high_bootstrap_startup_routine_observed_root_status_id", stage36_bootstrap_state_block.startup_routine_observed_root_status);
    xnu_log_kv32("mmu_high_bootstrap_startup_routine_observed_boot_args_virt_id", stage36_bootstrap_state_block.startup_routine_observed_boot_args_virt);
    xnu_log_kv32("mmu_high_bootstrap_startup_routine_observed_dt_virt_id", stage36_bootstrap_state_block.startup_routine_observed_dt_virt);
    xnu_log_kv32("mmu_high_bootstrap_startup_routine_observed_timebase_freq_id", stage36_bootstrap_state_block.startup_routine_observed_timebase_freq);
    xnu_log_kv32("mmu_high_bootstrap_startup_routine_observed_interrupt_mask_id", stage36_bootstrap_state_block.startup_routine_observed_interrupt_mask);
    xnu_log_kv32("mmu_high_bootstrap_startup_routine_satisfied_mask_id", stage36_bootstrap_state_block.startup_routine_satisfied_mask);
    xnu_log_kv32("mmu_high_bootstrap_startup_routine_checksum_id", stage36_bootstrap_state_block.startup_routine_status_checksum);
    xnu_log_kv32("mmu_high_bootstrap_startup_routine_status_id", stage36_bootstrap_state_block.startup_routine_status);
    xnu_log_kv32("mmu_high_bootstrap_startup_handoff_version_id", stage36_startup_handoff_block.version);
    xnu_log_kv32("mmu_high_bootstrap_startup_handoff_size_id", stage36_startup_handoff_block.size);
    xnu_log_kv32("mmu_high_bootstrap_startup_handoff_root_steps_id", stage36_startup_handoff_block.root_steps);
    xnu_log_kv32("mmu_high_bootstrap_startup_handoff_routine_status_id", stage36_startup_handoff_block.routine_status);
    xnu_log_kv32("mmu_high_bootstrap_startup_handoff_startup_status_id", stage36_startup_handoff_block.startup_status);
    xnu_log_kv32("mmu_high_bootstrap_startup_handoff_root_status_id", stage36_startup_handoff_block.root_status);
    xnu_log_kv32("mmu_high_bootstrap_startup_handoff_boot_args_virt_id", stage36_startup_handoff_block.boot_args_virt);
    xnu_log_kv32("mmu_high_bootstrap_startup_handoff_dt_virt_id", stage36_startup_handoff_block.dt_virt);
    xnu_log_kv32("mmu_high_bootstrap_startup_handoff_timebase_freq_id", stage36_startup_handoff_block.timebase_freq);
    xnu_log_kv32("mmu_high_bootstrap_startup_handoff_interrupt_mask_id", stage36_startup_handoff_block.interrupt_mask);
    xnu_log_kv32("mmu_high_bootstrap_startup_handoff_checksum_id", stage36_startup_handoff_block.checksum);
    xnu_log_kv32("mmu_high_bootstrap_startup_handoff_status_id", stage36_startup_handoff_block.status);
    xnu_log_kv32("mmu_high_bootstrap_startup_entry_version_id", stage36_bootstrap_state_block.startup_entry_version);
    xnu_log_kv32("mmu_high_bootstrap_startup_entry_size_id", stage36_bootstrap_state_block.startup_entry_size);
    xnu_log_kv32("mmu_high_bootstrap_startup_entry_required_root_steps_id", stage36_bootstrap_state_block.startup_entry_required_root_steps);
    xnu_log_kv32("mmu_high_bootstrap_startup_entry_observed_root_steps_id", stage36_bootstrap_state_block.startup_entry_observed_root_steps);
    xnu_log_kv32("mmu_high_bootstrap_startup_entry_observed_routine_status_id", stage36_bootstrap_state_block.startup_entry_observed_routine_status);
    xnu_log_kv32("mmu_high_bootstrap_startup_entry_observed_startup_status_id", stage36_bootstrap_state_block.startup_entry_observed_startup_status);
    xnu_log_kv32("mmu_high_bootstrap_startup_entry_observed_root_status_id", stage36_bootstrap_state_block.startup_entry_observed_root_status);
    xnu_log_kv32("mmu_high_bootstrap_startup_entry_observed_boot_args_virt_id", stage36_bootstrap_state_block.startup_entry_observed_boot_args_virt);
    xnu_log_kv32("mmu_high_bootstrap_startup_entry_observed_dt_virt_id", stage36_bootstrap_state_block.startup_entry_observed_dt_virt);
    xnu_log_kv32("mmu_high_bootstrap_startup_entry_observed_timebase_freq_id", stage36_bootstrap_state_block.startup_entry_observed_timebase_freq);
    xnu_log_kv32("mmu_high_bootstrap_startup_entry_observed_interrupt_mask_id", stage36_bootstrap_state_block.startup_entry_observed_interrupt_mask);
    xnu_log_kv32("mmu_high_bootstrap_startup_entry_satisfied_mask_id", stage36_bootstrap_state_block.startup_entry_satisfied_mask);
    xnu_log_kv32("mmu_high_bootstrap_startup_entry_checksum_id", stage36_bootstrap_state_block.startup_entry_status_checksum);
    xnu_log_kv32("mmu_high_bootstrap_startup_entry_status_id", stage36_bootstrap_state_block.startup_entry_status);
    xnu_log_kv32("mmu_high_bootstrap_kernel_callout_version_id", stage36_bootstrap_state_block.kernel_callout_version);
    xnu_log_kv32("mmu_high_bootstrap_kernel_callout_count_id", stage36_bootstrap_state_block.kernel_callout_count);
    xnu_log_kv32("mmu_high_bootstrap_kernel_callout_order_mask_id", stage36_bootstrap_state_block.kernel_callout_order_mask);
    xnu_log_kv32("mmu_high_bootstrap_kernel_callout_handler_mask_id", stage36_bootstrap_state_block.kernel_callout_handler_mask);
    xnu_log_kv32("mmu_high_bootstrap_kernel_callout_observed_service_mask_id", stage36_bootstrap_state_block.kernel_callout_observed_service_mask);
    xnu_log_kv32("mmu_high_bootstrap_kernel_callout_checksum_id", stage36_bootstrap_state_block.kernel_callout_status_checksum);
    xnu_log_kv32("mmu_high_bootstrap_kernel_callout_status_id", stage36_bootstrap_state_block.kernel_callout_status);
    xnu_log_kv32("mmu_high_bootstrap_kernel_context_satisfied_mask_id", stage36_bootstrap_state_block.kernel_context_satisfied_mask);
    xnu_log_kv32("mmu_high_bootstrap_kernel_context_checksum_id", stage36_bootstrap_state_block.kernel_context_checksum);
    xnu_log_kv32("mmu_high_bootstrap_kernel_context_status_id", stage36_bootstrap_state_block.kernel_context_status);
    xnu_log_kv32("mmu_high_bootstrap_vm_plan_version_id", stage36_bootstrap_state_block.vm_plan_version);
    xnu_log_kv32("mmu_high_bootstrap_vm_plan_size_id", stage36_bootstrap_state_block.vm_plan_size);
    xnu_log_kv32("mmu_high_bootstrap_vm_plan_required_mask_id", stage36_bootstrap_state_block.vm_plan_required_mask);
    xnu_log_kv32("mmu_high_bootstrap_vm_plan_satisfied_mask_id", stage36_bootstrap_state_block.vm_plan_satisfied_mask);
    xnu_log_kv32("mmu_high_bootstrap_vm_plan_checksum_id", stage36_bootstrap_state_block.vm_plan_checksum);
    xnu_log_kv32("mmu_high_bootstrap_vm_plan_status_id", stage36_bootstrap_state_block.vm_plan_status);
    xnu_log_kv32("mmu_high_bootstrap_vm_state_version_id", stage36_bootstrap_state_block.vm_state_version);
    xnu_log_kv32("mmu_high_bootstrap_vm_state_size_id", stage36_bootstrap_state_block.vm_state_size);
    xnu_log_kv32("mmu_high_bootstrap_vm_state_required_mask_id", stage36_bootstrap_state_block.vm_state_required_mask);
    xnu_log_kv32("mmu_high_bootstrap_vm_state_satisfied_mask_id", stage36_bootstrap_state_block.vm_state_satisfied_mask);
    xnu_log_kv32("mmu_high_bootstrap_vm_state_checksum_id", stage36_bootstrap_state_block.vm_state_checksum);
    xnu_log_kv32("mmu_high_bootstrap_vm_state_status_id", stage36_bootstrap_state_block.vm_state_status);
    xnu_log_kv32("mmu_high_bootstrap_allocator_version_id", stage36_bootstrap_state_block.allocator_version);
    xnu_log_kv32("mmu_high_bootstrap_allocator_size_id", stage36_bootstrap_state_block.allocator_size);
    xnu_log_kv32("mmu_high_bootstrap_allocator_required_mask_id", stage36_bootstrap_state_block.allocator_required_mask);
    xnu_log_kv32("mmu_high_bootstrap_allocator_satisfied_mask_id", stage36_bootstrap_state_block.allocator_satisfied_mask);
    xnu_log_kv32("mmu_high_bootstrap_allocator_checksum_id", stage36_bootstrap_state_block.allocator_checksum);
    xnu_log_kv32("mmu_high_bootstrap_allocator_status_id", stage36_bootstrap_state_block.allocator_status);
    xnu_log_kv32("mmu_high_bootstrap_context_satisfied_mask_id", stage36_kernel_context_block.satisfied_mask);
    xnu_log_kv32("mmu_high_bootstrap_context_checksum_id", stage36_kernel_context_block.checksum);
    xnu_log_kv32("mmu_high_bootstrap_context_status_id", stage36_kernel_context_block.status);
    xnu_log_kv32("mmu_high_bootstrap_plan_context_status_id", stage36_vm_plan_block.context_status);
    xnu_log_kv32("mmu_high_bootstrap_plan_context_checksum_id", stage36_vm_plan_block.context_checksum);
    xnu_log_kv32("mmu_high_bootstrap_plan_l1_table_phys_id", stage36_vm_plan_block.l1_table_phys);
    xnu_log_kv32("mmu_high_bootstrap_plan_l1_table_virt_id", stage36_vm_plan_block.l1_table_virt);
    xnu_log_kv32("mmu_high_bootstrap_plan_memory_base_id", stage36_vm_plan_block.memory_base);
    xnu_log_kv32("mmu_high_bootstrap_plan_memory_size_id", stage36_vm_plan_block.memory_size);
    xnu_log_kv32("mmu_high_bootstrap_plan_mmu_enabled_id", stage36_vm_plan_block.mmu_enabled);
    xnu_log_kv32("mmu_high_bootstrap_plan_cache_policy_id", stage36_vm_plan_block.cache_policy);
    xnu_log_kv32("mmu_high_bootstrap_plan_satisfied_mask_id", stage36_vm_plan_block.satisfied_mask);
    xnu_log_kv32("mmu_high_bootstrap_plan_checksum_id", stage36_vm_plan_block.checksum);
    xnu_log_kv32("mmu_high_bootstrap_plan_status_id", stage36_vm_plan_block.status);
    xnu_log_kv32("mmu_high_bootstrap_vmstate_plan_status_id", stage36_vm_state_block.vm_plan_status);
    xnu_log_kv32("mmu_high_bootstrap_vmstate_plan_checksum_id", stage36_vm_state_block.vm_plan_checksum);
    xnu_log_kv32("mmu_high_bootstrap_vmstate_kernel_map_base_id", stage36_vm_state_block.kernel_map_base);
    xnu_log_kv32("mmu_high_bootstrap_vmstate_kernel_map_limit_id", stage36_vm_state_block.kernel_map_limit);
    xnu_log_kv32("mmu_high_bootstrap_vmstate_available_memory_base_id", stage36_vm_state_block.available_memory_base);
    xnu_log_kv32("mmu_high_bootstrap_vmstate_available_memory_cursor_id", stage36_vm_state_block.available_memory_cursor);
    xnu_log_kv32("mmu_high_bootstrap_vmstate_bootstrap_alloc_base_id", stage36_vm_state_block.bootstrap_alloc_base);
    xnu_log_kv32("mmu_high_bootstrap_vmstate_bootstrap_alloc_size_id", stage36_vm_state_block.bootstrap_alloc_size);
    xnu_log_kv32("mmu_high_bootstrap_vmstate_bootstrap_alloc_end_id", stage36_vm_state_block.bootstrap_alloc_end);
    xnu_log_kv32("mmu_high_bootstrap_vmstate_pmap_section_size_id", stage36_vm_state_block.pmap_section_size);
    xnu_log_kv32("mmu_high_bootstrap_vmstate_pmap_section_descriptor_id", stage36_vm_state_block.pmap_section_descriptor);
    xnu_log_kv32("mmu_high_bootstrap_vmstate_pmap_l1_table_phys_id", stage36_vm_state_block.pmap_l1_table_phys);
    xnu_log_kv32("mmu_high_bootstrap_vmstate_pmap_l1_table_virt_id", stage36_vm_state_block.pmap_l1_table_virt);
    xnu_log_kv32("mmu_high_bootstrap_vmstate_mmu_enabled_id", stage36_vm_state_block.mmu_enabled);
    xnu_log_kv32("mmu_high_bootstrap_vmstate_cache_policy_id", stage36_vm_state_block.cache_policy);
    xnu_log_kv32("mmu_high_bootstrap_vmstate_satisfied_mask_id", stage36_vm_state_block.satisfied_mask);
    xnu_log_kv32("mmu_high_bootstrap_vmstate_checksum_id", stage36_vm_state_block.checksum);
    xnu_log_kv32("mmu_high_bootstrap_vmstate_status_id", stage36_vm_state_block.status);
    xnu_log_kv32("mmu_high_bootstrap_allocdesc_vm_state_status_id", stage36_boot_allocator_block.vm_state_status);
    xnu_log_kv32("mmu_high_bootstrap_allocdesc_vm_state_checksum_id", stage36_boot_allocator_block.vm_state_checksum);
    xnu_log_kv32("mmu_high_bootstrap_allocdesc_span_base_id", stage36_boot_allocator_block.span_base);
    xnu_log_kv32("mmu_high_bootstrap_allocdesc_span_size_id", stage36_boot_allocator_block.span_size);
    xnu_log_kv32("mmu_high_bootstrap_allocdesc_span_end_id", stage36_boot_allocator_block.span_end);
    xnu_log_kv32("mmu_high_bootstrap_allocdesc_initial_cursor_id", stage36_boot_allocator_block.initial_cursor);
    xnu_log_kv32("mmu_high_bootstrap_allocdesc_current_cursor_id", stage36_boot_allocator_block.current_cursor);
    xnu_log_kv32("mmu_high_bootstrap_allocdesc_remaining_bytes_id", stage36_boot_allocator_block.remaining_bytes);
    xnu_log_kv32("mmu_high_bootstrap_allocdesc_first_alloc_base_id", stage36_boot_allocator_block.first_alloc_base);
    xnu_log_kv32("mmu_high_bootstrap_allocdesc_first_alloc_size_id", stage36_boot_allocator_block.first_alloc_size);
    xnu_log_kv32("mmu_high_bootstrap_allocdesc_first_alloc_end_id", stage36_boot_allocator_block.first_alloc_end);
    xnu_log_kv32("mmu_high_bootstrap_allocdesc_first_alloc_tag_id", stage36_boot_allocator_block.first_alloc_tag);
    xnu_log_kv32("mmu_high_bootstrap_allocdesc_alignment_id", stage36_boot_allocator_block.alignment);
    xnu_log_kv32("mmu_high_bootstrap_allocdesc_satisfied_mask_id", stage36_boot_allocator_block.satisfied_mask);
    xnu_log_kv32("mmu_high_bootstrap_allocdesc_checksum_id", stage36_boot_allocator_block.checksum);
    xnu_log_kv32("mmu_high_bootstrap_allocdesc_status_id", stage36_boot_allocator_block.status);
    xnu_log_kv32("mmu_high_bootstrap_status_id", stage36_bootstrap_state_block.status);
    xnu_log_kv32("mmu_high_bootstrap_checksum_id", stage36_bootstrap_state_block.checksum);
    xnu_log_kv32("mmu_high_bootstrap_magic_alias", alias_state->magic);
    xnu_log_kv32("mmu_high_bootstrap_root_steps_alias", alias_state->root_steps);
    xnu_log_kv32("mmu_high_bootstrap_root_status_alias", alias_state->root_status);
    xnu_log_kv32("mmu_high_bootstrap_root_dt_virt_alias", alias_state->root_dt_virt);
    xnu_log_kv32("mmu_high_bootstrap_root_dt_summary_status_alias", alias_state->root_dt_summary_status);
    xnu_log_kv32("mmu_high_bootstrap_platform_consistency_alias", alias_state->platform_result_consistency);
    xnu_log_kv32("mmu_high_bootstrap_platform_status_alias", alias_state->platform_result_status);
    xnu_log_kv32("mmu_high_bootstrap_phase_mask_alias", alias_state->phase_completed_mask);
    xnu_log_kv32("mmu_high_bootstrap_phase_checksum_alias", alias_state->phase_status_checksum);
    xnu_log_kv32("mmu_high_bootstrap_phase_service_mask_alias", alias_state->phase_service_dependency_mask);
    xnu_log_kv32("mmu_high_bootstrap_phase_service_satisfied_alias", alias_state->phase_service_satisfied_mask);
    xnu_log_kv32("mmu_high_bootstrap_phase_service_status_alias", alias_state->phase_service_dependency_status);
    xnu_log_kv32("mmu_high_bootstrap_phase_dispatcher_order_alias", alias_state->phase_dispatcher_order_mask);
    xnu_log_kv32("mmu_high_bootstrap_phase_dispatcher_handler_alias", alias_state->phase_dispatcher_handler_mask);
    xnu_log_kv32("mmu_high_bootstrap_phase_dispatcher_status_alias", alias_state->phase_dispatcher_status);
    xnu_log_kv32("mmu_high_bootstrap_service_mask_alias", alias_state->service_available_mask);
    xnu_log_kv32("mmu_high_bootstrap_service_checksum_alias", alias_state->service_status_checksum);
    xnu_log_kv32("mmu_high_bootstrap_service_dispatcher_order_alias", alias_state->service_dispatcher_order_mask);
    xnu_log_kv32("mmu_high_bootstrap_service_dispatcher_handler_alias", alias_state->service_dispatcher_handler_mask);
    xnu_log_kv32("mmu_high_bootstrap_service_dispatcher_status_alias", alias_state->service_dispatcher_status);
    xnu_log_kv32("mmu_high_bootstrap_registry_status_alias", alias_state->registry_status);
    xnu_log_kv32("mmu_high_bootstrap_registry_dispatch_coverage_alias", alias_state->registry_dispatch_coverage_mask);
    xnu_log_kv32("mmu_high_bootstrap_boot_policy_satisfied_alias", alias_state->boot_policy_satisfied_mask);
    xnu_log_kv32("mmu_high_bootstrap_boot_policy_status_alias", alias_state->boot_policy_status);
    xnu_log_kv32("mmu_high_bootstrap_boot_policy_observed_root_steps_alias", alias_state->boot_policy_observed_root_steps);
    xnu_log_kv32("mmu_high_bootstrap_manifest_satisfied_alias", alias_state->manifest_satisfied_mask);
    xnu_log_kv32("mmu_high_bootstrap_manifest_status_alias", alias_state->manifest_status);
    xnu_log_kv32("mmu_high_bootstrap_manifest_observed_root_steps_alias", alias_state->manifest_observed_root_steps);
    xnu_log_kv32("mmu_high_bootstrap_launch_satisfied_alias", alias_state->launch_satisfied_mask);
    xnu_log_kv32("mmu_high_bootstrap_launch_status_alias", alias_state->launch_status);
    xnu_log_kv32("mmu_high_bootstrap_launch_observed_root_steps_alias", alias_state->launch_observed_root_steps);
    xnu_log_kv32("mmu_high_bootstrap_launch_observed_interrupt_mask_alias", alias_state->launch_observed_interrupt_mask);
    xnu_log_kv32("mmu_high_bootstrap_startup_handoff_status_alias", alias_handoff->status);
    xnu_log_kv32("mmu_high_bootstrap_startup_handoff_checksum_alias", alias_handoff->checksum);
    xnu_log_kv32("mmu_high_bootstrap_startup_entry_satisfied_alias", alias_state->startup_entry_satisfied_mask);
    xnu_log_kv32("mmu_high_bootstrap_startup_entry_status_alias", alias_state->startup_entry_status);
    xnu_log_kv32("mmu_high_bootstrap_kernel_callout_order_alias", alias_state->kernel_callout_order_mask);
    xnu_log_kv32("mmu_high_bootstrap_kernel_callout_handler_alias", alias_state->kernel_callout_handler_mask);
    xnu_log_kv32("mmu_high_bootstrap_kernel_callout_status_alias", alias_state->kernel_callout_status);
    xnu_log_kv32("mmu_high_bootstrap_kernel_context_status_alias", alias_state->kernel_context_status);
    xnu_log_kv32("mmu_high_bootstrap_vm_plan_status_alias", alias_state->vm_plan_status);
    xnu_log_kv32("mmu_high_bootstrap_vm_plan_checksum_alias", alias_state->vm_plan_checksum);
    xnu_log_kv32("mmu_high_bootstrap_context_status_alias", alias_context->status);
    xnu_log_kv32("mmu_high_bootstrap_context_checksum_alias", alias_context->checksum);
    xnu_log_kv32("mmu_high_bootstrap_plan_status_alias", alias_vm_plan->status);
    xnu_log_kv32("mmu_high_bootstrap_plan_checksum_alias", alias_vm_plan->checksum);
    xnu_log_kv32("mmu_high_bootstrap_vm_state_status_alias", alias_state->vm_state_status);
    xnu_log_kv32("mmu_high_bootstrap_vm_state_checksum_alias", alias_state->vm_state_checksum);
    xnu_log_kv32("mmu_high_bootstrap_vmstate_status_alias", alias_vm_state->status);
    xnu_log_kv32("mmu_high_bootstrap_vmstate_checksum_alias", alias_vm_state->checksum);
    xnu_log_kv32("mmu_high_bootstrap_allocator_status_alias", alias_state->allocator_status);
    xnu_log_kv32("mmu_high_bootstrap_allocator_checksum_alias", alias_state->allocator_checksum);
    xnu_log_kv32("mmu_high_bootstrap_allocdesc_status_alias", alias_allocator->status);
    xnu_log_kv32("mmu_high_bootstrap_allocdesc_checksum_alias", alias_allocator->checksum);
    xnu_log_kv32("mmu_high_bootstrap_allocdesc_current_cursor_alias", alias_allocator->current_cursor);
    xnu_log_kv32("mmu_high_bootstrap_allocdesc_remaining_alias", alias_allocator->remaining_bytes);
    xnu_log_kv32("mmu_high_bootstrap_init_steps_alias", alias_state->init_steps);
    xnu_log_kv32("mmu_high_bootstrap_status_alias", alias_state->status);
    xnu_log_kv32("mmu_high_bootstrap_checksum_alias", alias_state->checksum);

    if (result != STAGE36_BOOTSTRAP_STATUS_OK ||
        stage36_bootstrap_state_block.status != STAGE36_BOOTSTRAP_STATUS_OK ||
        stage36_bootstrap_state_block.validation_mask != 0u) {
        xnu_log_puts("mmu high bootstrap selftest failed: validation status\n");
        return 0;
    }
    if (stage36_bootstrap_state_block.checksum != expected) {
        xnu_log_puts("mmu high bootstrap selftest failed: checksum mismatch\n");
        return 0;
    }
    if (stage36_bootstrap_state_block.magic != 0x36003600u ||
        stage36_bootstrap_state_block.boot_args_rev_ver != 0x00020002u ||
        stage36_bootstrap_state_block.machine_type != MACHINE_TYPE_MSM8974) {
        xnu_log_puts("mmu high bootstrap selftest failed: boot args mismatch\n");
        return 0;
    }
    if (stage36_bootstrap_state_block.root_steps != STAGE36_ROOT_REQUIRED_STEPS ||
        stage36_bootstrap_state_block.root_status != STAGE36_BOOTSTRAP_STATUS_OK) {
        xnu_log_puts("mmu high bootstrap selftest failed: high root mismatch\n");
        return 0;
    }
    if (stage36_bootstrap_state_block.root_boot_args_virt != (uint32_t)(uintptr_t)alias_args ||
        stage36_bootstrap_state_block.root_dt_virt < STAGE36_HIGH_ALIAS_BASE ||
        stage36_bootstrap_state_block.root_dt_root_children != 6u ||
        stage36_bootstrap_state_block.root_dt_memory_base != RAM_PHYS_BASE ||
        stage36_bootstrap_state_block.root_dt_memory_size != (RAM_CONSOLE_BASE - RAM_PHYS_BASE) ||
        stage36_bootstrap_state_block.root_dt_timer_frequency != 19200000u ||
        stage36_bootstrap_state_block.root_dt_summary_status != STAGE36_BOOTSTRAP_STATUS_OK) {
        xnu_log_puts("mmu high bootstrap selftest failed: high DT summary mismatch\n");
        return 0;
    }
    if (stage36_bootstrap_state_block.platform_result_version != STAGE36_PLATFORM_RESULT_VERSION ||
        stage36_bootstrap_state_block.platform_result_size != sizeof(stage36_bootstrap_state_block) ||
        stage36_bootstrap_state_block.platform_result_consistency != STAGE36_PLATFORM_CONSIST_REQUIRED ||
        stage36_bootstrap_state_block.platform_dt_cpu_count != 4u ||
        stage36_bootstrap_state_block.platform_pe_cpu_count != 4u ||
        stage36_bootstrap_state_block.platform_memory_base != RAM_PHYS_BASE ||
        stage36_bootstrap_state_block.platform_memory_size != (RAM_CONSOLE_BASE - RAM_PHYS_BASE) ||
        stage36_bootstrap_state_block.platform_gic_dist_base != 0xf9000000u ||
        stage36_bootstrap_state_block.platform_gic_cpu_base != 0xf9002000u ||
        stage36_bootstrap_state_block.platform_timer_frequency != 19200000u ||
        stage36_bootstrap_state_block.platform_result_status != STAGE36_BOOTSTRAP_STATUS_OK) {
        xnu_log_puts("mmu high bootstrap selftest failed: platform result mismatch\n");
        return 0;
    }
    if (stage36_bootstrap_state_block.phase_count != STAGE36_PHASE_COUNT ||
        stage36_bootstrap_state_block.phase_completed_mask != STAGE36_PHASE_REQUIRED_MASK ||
        stage36_bootstrap_state_block.phase_status_checksum != (STAGE36_PHASE_COUNT ^ STAGE36_PHASE_REQUIRED_MASK ^
            STAGE36_BOOTSTRAP_STATUS_OK ^ STAGE36_BOOTSTRAP_STATUS_OK ^
            STAGE36_BOOTSTRAP_STATUS_OK ^ STAGE36_BOOTSTRAP_STATUS_OK) ||
        stage36_bootstrap_state_block.phase0_status != STAGE36_BOOTSTRAP_STATUS_OK ||
        stage36_bootstrap_state_block.phase1_status != STAGE36_BOOTSTRAP_STATUS_OK ||
        stage36_bootstrap_state_block.phase2_status != STAGE36_BOOTSTRAP_STATUS_OK ||
        stage36_bootstrap_state_block.phase3_status != STAGE36_BOOTSTRAP_STATUS_OK ||
        stage36_bootstrap_state_block.phase0_required_services != STAGE36_PHASE_VALIDATE_SERVICES ||
        stage36_bootstrap_state_block.phase1_required_services != STAGE36_PHASE_DT_SUMMARY_SERVICES ||
        stage36_bootstrap_state_block.phase2_required_services != STAGE36_PHASE_PLATFORM_RESULT_SERVICES ||
        stage36_bootstrap_state_block.phase3_required_services != STAGE36_PHASE_RETURN_READY_SERVICES ||
        stage36_bootstrap_state_block.phase_service_dependency_mask != STAGE36_PHASE_SERVICE_REQUIRED_MASK ||
        stage36_bootstrap_state_block.phase_service_satisfied_mask != STAGE36_PHASE_REQUIRED_MASK ||
        stage36_bootstrap_state_block.phase_service_status_checksum != (STAGE36_PHASE_COUNT ^ STAGE36_PHASE_SERVICE_REQUIRED_MASK ^
            STAGE36_PHASE_REQUIRED_MASK ^ STAGE36_PHASE_VALIDATE_SERVICES ^
            STAGE36_PHASE_DT_SUMMARY_SERVICES ^ STAGE36_PHASE_PLATFORM_RESULT_SERVICES ^
            STAGE36_PHASE_RETURN_READY_SERVICES) ||
        stage36_bootstrap_state_block.phase_service_dependency_status != STAGE36_BOOTSTRAP_STATUS_OK ||
        stage36_bootstrap_state_block.phase_dispatcher_count != STAGE36_PHASE_COUNT ||
        stage36_bootstrap_state_block.phase_dispatcher_order_mask != STAGE36_PHASE_REQUIRED_MASK ||
        stage36_bootstrap_state_block.phase_dispatcher_handler_mask != STAGE36_PHASE_REQUIRED_MASK ||
        stage36_bootstrap_state_block.phase_dispatcher_status_checksum != (STAGE36_PHASE_COUNT ^
            STAGE36_PHASE_REQUIRED_MASK ^ STAGE36_PHASE_REQUIRED_MASK ^
            STAGE36_PHASE_VALIDATE ^ STAGE36_PHASE_DT_SUMMARY ^
            STAGE36_PHASE_PLATFORM_RESULT ^ STAGE36_PHASE_RETURN_READY ^
            STAGE36_BOOTSTRAP_STATUS_OK ^ STAGE36_BOOTSTRAP_STATUS_OK ^
            STAGE36_BOOTSTRAP_STATUS_OK ^ STAGE36_BOOTSTRAP_STATUS_OK) ||
        stage36_bootstrap_state_block.phase0_descriptor_id != STAGE36_PHASE_VALIDATE ||
        stage36_bootstrap_state_block.phase1_descriptor_id != STAGE36_PHASE_DT_SUMMARY ||
        stage36_bootstrap_state_block.phase2_descriptor_id != STAGE36_PHASE_PLATFORM_RESULT ||
        stage36_bootstrap_state_block.phase3_descriptor_id != STAGE36_PHASE_RETURN_READY ||
        stage36_bootstrap_state_block.phase0_handler_result != STAGE36_BOOTSTRAP_STATUS_OK ||
        stage36_bootstrap_state_block.phase1_handler_result != STAGE36_BOOTSTRAP_STATUS_OK ||
        stage36_bootstrap_state_block.phase2_handler_result != STAGE36_BOOTSTRAP_STATUS_OK ||
        stage36_bootstrap_state_block.phase3_handler_result != STAGE36_BOOTSTRAP_STATUS_OK ||
        stage36_bootstrap_state_block.phase_dispatcher_status != STAGE36_BOOTSTRAP_STATUS_OK) {
        xnu_log_puts("mmu high bootstrap selftest failed: phase table mismatch\n");
        return 0;
    }
    if (stage36_bootstrap_state_block.service_count != STAGE36_SERVICE_COUNT ||
        stage36_bootstrap_state_block.service_available_mask != STAGE36_SERVICE_REQUIRED_MASK ||
        stage36_bootstrap_state_block.service_status_checksum != (STAGE36_SERVICE_COUNT ^ STAGE36_SERVICE_REQUIRED_MASK ^
            STAGE36_BOOTSTRAP_STATUS_OK ^ STAGE36_BOOTSTRAP_STATUS_OK ^
            STAGE36_BOOTSTRAP_STATUS_OK ^ STAGE36_BOOTSTRAP_STATUS_OK) ||
        stage36_bootstrap_state_block.service_logging_status != STAGE36_BOOTSTRAP_STATUS_OK ||
        stage36_bootstrap_state_block.service_timebase_status != STAGE36_BOOTSTRAP_STATUS_OK ||
        stage36_bootstrap_state_block.service_platform_status != STAGE36_BOOTSTRAP_STATUS_OK ||
        stage36_bootstrap_state_block.service_interrupts_status != STAGE36_BOOTSTRAP_STATUS_OK ||
        stage36_bootstrap_state_block.service_dispatcher_count != STAGE36_SERVICE_COUNT ||
        stage36_bootstrap_state_block.service_dispatcher_order_mask != STAGE36_SERVICE_REQUIRED_MASK ||
        stage36_bootstrap_state_block.service_dispatcher_handler_mask != STAGE36_SERVICE_REQUIRED_MASK ||
        stage36_bootstrap_state_block.service_dispatcher_status_checksum != (STAGE36_SERVICE_COUNT ^
            STAGE36_SERVICE_REQUIRED_MASK ^ STAGE36_SERVICE_REQUIRED_MASK ^
            STAGE36_SERVICE_LOGGING ^ STAGE36_SERVICE_TIMEBASE ^
            STAGE36_SERVICE_PLATFORM ^ STAGE36_SERVICE_INTERRUPTS ^
            STAGE36_BOOTSTRAP_STATUS_OK ^ STAGE36_BOOTSTRAP_STATUS_OK ^
            STAGE36_BOOTSTRAP_STATUS_OK ^ STAGE36_BOOTSTRAP_STATUS_OK) ||
        stage36_bootstrap_state_block.service_logging_descriptor_id != STAGE36_SERVICE_LOGGING ||
        stage36_bootstrap_state_block.service_timebase_descriptor_id != STAGE36_SERVICE_TIMEBASE ||
        stage36_bootstrap_state_block.service_platform_descriptor_id != STAGE36_SERVICE_PLATFORM ||
        stage36_bootstrap_state_block.service_interrupts_descriptor_id != STAGE36_SERVICE_INTERRUPTS ||
        stage36_bootstrap_state_block.service_logging_handler_result != STAGE36_BOOTSTRAP_STATUS_OK ||
        stage36_bootstrap_state_block.service_timebase_handler_result != STAGE36_BOOTSTRAP_STATUS_OK ||
        stage36_bootstrap_state_block.service_platform_handler_result != STAGE36_BOOTSTRAP_STATUS_OK ||
        stage36_bootstrap_state_block.service_interrupts_handler_result != STAGE36_BOOTSTRAP_STATUS_OK ||
        stage36_bootstrap_state_block.service_dispatcher_status != STAGE36_BOOTSTRAP_STATUS_OK) {
        xnu_log_puts("mmu high bootstrap selftest failed: service table mismatch\n");
        return 0;
    }
    if (stage36_bootstrap_state_block.registry_version != STAGE36_REGISTRY_VERSION ||
        stage36_bootstrap_state_block.registry_size != sizeof(stage36_bootstrap_state_block) ||
        stage36_bootstrap_state_block.registry_service_descriptor_mask != STAGE36_SERVICE_REQUIRED_MASK ||
        stage36_bootstrap_state_block.registry_phase_descriptor_mask != STAGE36_PHASE_REQUIRED_MASK ||
        stage36_bootstrap_state_block.registry_dependency_coverage_mask != STAGE36_PHASE_SERVICE_REQUIRED_MASK ||
        stage36_bootstrap_state_block.registry_dispatch_coverage_mask != STAGE36_REGISTRY_DISPATCH_COVERAGE_REQUIRED ||
        stage36_bootstrap_state_block.registry_status_checksum != (STAGE36_REGISTRY_VERSION ^
            sizeof(stage36_bootstrap_state_block) ^ STAGE36_SERVICE_REQUIRED_MASK ^
            STAGE36_PHASE_REQUIRED_MASK ^ STAGE36_PHASE_SERVICE_REQUIRED_MASK ^
            STAGE36_REGISTRY_DISPATCH_COVERAGE_REQUIRED) ||
        stage36_bootstrap_state_block.registry_status != STAGE36_BOOTSTRAP_STATUS_OK) {
        xnu_log_puts("mmu high bootstrap selftest failed: registry mismatch\n");
        return 0;
    }
    if (stage36_bootstrap_state_block.boot_policy_version != STAGE36_BOOT_POLICY_VERSION ||
        stage36_bootstrap_state_block.boot_policy_size != sizeof(stage36_bootstrap_state_block) ||
        stage36_bootstrap_state_block.boot_policy_required_root_steps != STAGE36_BOOT_POLICY_REQUIRED_ROOT_STEPS ||
        stage36_bootstrap_state_block.boot_policy_required_service_mask != STAGE36_SERVICE_REQUIRED_MASK ||
        stage36_bootstrap_state_block.boot_policy_required_phase_mask != STAGE36_PHASE_REQUIRED_MASK ||
        stage36_bootstrap_state_block.boot_policy_required_dependency_mask != STAGE36_PHASE_SERVICE_REQUIRED_MASK ||
        stage36_bootstrap_state_block.boot_policy_required_dispatch_coverage_mask != STAGE36_REGISTRY_DISPATCH_COVERAGE_REQUIRED ||
        stage36_bootstrap_state_block.boot_policy_required_registry_status != STAGE36_BOOTSTRAP_STATUS_OK ||
        stage36_bootstrap_state_block.boot_policy_observed_root_steps != STAGE36_BOOT_POLICY_REQUIRED_ROOT_STEPS ||
        stage36_bootstrap_state_block.boot_policy_observed_service_mask != STAGE36_SERVICE_REQUIRED_MASK ||
        stage36_bootstrap_state_block.boot_policy_observed_phase_mask != STAGE36_PHASE_REQUIRED_MASK ||
        stage36_bootstrap_state_block.boot_policy_observed_dependency_mask != STAGE36_PHASE_SERVICE_REQUIRED_MASK ||
        stage36_bootstrap_state_block.boot_policy_observed_dispatch_coverage_mask != STAGE36_REGISTRY_DISPATCH_COVERAGE_REQUIRED ||
        stage36_bootstrap_state_block.boot_policy_observed_registry_status != STAGE36_BOOTSTRAP_STATUS_OK ||
        stage36_bootstrap_state_block.boot_policy_satisfied_mask != STAGE36_BOOT_POLICY_SAT_REQUIRED ||
        stage36_bootstrap_state_block.boot_policy_status_checksum != (STAGE36_BOOT_POLICY_VERSION ^
            sizeof(stage36_bootstrap_state_block) ^ STAGE36_BOOT_POLICY_REQUIRED_ROOT_STEPS ^
            STAGE36_SERVICE_REQUIRED_MASK ^ STAGE36_PHASE_REQUIRED_MASK ^
            STAGE36_PHASE_SERVICE_REQUIRED_MASK ^ STAGE36_REGISTRY_DISPATCH_COVERAGE_REQUIRED ^
            STAGE36_BOOTSTRAP_STATUS_OK ^ STAGE36_BOOT_POLICY_REQUIRED_ROOT_STEPS ^
            STAGE36_SERVICE_REQUIRED_MASK ^ STAGE36_PHASE_REQUIRED_MASK ^
            STAGE36_PHASE_SERVICE_REQUIRED_MASK ^ STAGE36_REGISTRY_DISPATCH_COVERAGE_REQUIRED ^
            STAGE36_BOOTSTRAP_STATUS_OK ^ STAGE36_BOOT_POLICY_SAT_REQUIRED) ||
        stage36_bootstrap_state_block.boot_policy_status != STAGE36_BOOTSTRAP_STATUS_OK) {
        xnu_log_puts("mmu high bootstrap selftest failed: boot policy mismatch\n");
        return 0;
    }
    if (stage36_bootstrap_state_block.manifest_version != STAGE36_MANIFEST_VERSION ||
        stage36_bootstrap_state_block.manifest_record_count != STAGE36_MANIFEST_RECORD_COUNT ||
        stage36_bootstrap_state_block.manifest_required_record_mask != STAGE36_MANIFEST_RECORD_REQUIRED_MASK ||
        stage36_bootstrap_state_block.manifest_order_mask != STAGE36_MANIFEST_RECORD_REQUIRED_MASK ||
        stage36_bootstrap_state_block.manifest_satisfied_mask != STAGE36_MANIFEST_RECORD_REQUIRED_MASK ||
        stage36_bootstrap_state_block.manifest_required_root_steps != STAGE36_MANIFEST_REQUIRED_ROOT_STEPS ||
        stage36_bootstrap_state_block.manifest_observed_root_steps != STAGE36_MANIFEST_REQUIRED_ROOT_STEPS ||
        stage36_bootstrap_state_block.manifest_service_required_mask != STAGE36_SERVICE_REQUIRED_MASK ||
        stage36_bootstrap_state_block.manifest_service_observed_mask != STAGE36_SERVICE_REQUIRED_MASK ||
        stage36_bootstrap_state_block.manifest_service_status != STAGE36_BOOTSTRAP_STATUS_OK ||
        stage36_bootstrap_state_block.manifest_phase_required_mask != STAGE36_PHASE_REQUIRED_MASK ||
        stage36_bootstrap_state_block.manifest_phase_observed_mask != STAGE36_PHASE_REQUIRED_MASK ||
        stage36_bootstrap_state_block.manifest_phase_status != STAGE36_BOOTSTRAP_STATUS_OK ||
        stage36_bootstrap_state_block.manifest_dependency_required_mask != STAGE36_PHASE_SERVICE_REQUIRED_MASK ||
        stage36_bootstrap_state_block.manifest_dependency_observed_mask != STAGE36_PHASE_SERVICE_REQUIRED_MASK ||
        stage36_bootstrap_state_block.manifest_dependency_status != STAGE36_BOOTSTRAP_STATUS_OK ||
        stage36_bootstrap_state_block.manifest_dispatch_required_mask != STAGE36_REGISTRY_DISPATCH_COVERAGE_REQUIRED ||
        stage36_bootstrap_state_block.manifest_dispatch_observed_mask != STAGE36_REGISTRY_DISPATCH_COVERAGE_REQUIRED ||
        stage36_bootstrap_state_block.manifest_dispatch_status != STAGE36_BOOTSTRAP_STATUS_OK ||
        stage36_bootstrap_state_block.manifest_policy_required_mask != STAGE36_BOOT_POLICY_SAT_REQUIRED ||
        stage36_bootstrap_state_block.manifest_policy_observed_mask != STAGE36_BOOT_POLICY_SAT_REQUIRED ||
        stage36_bootstrap_state_block.manifest_policy_status != STAGE36_BOOTSTRAP_STATUS_OK ||
        stage36_bootstrap_state_block.manifest_boot_required_status != STAGE36_BOOTSTRAP_STATUS_OK ||
        stage36_bootstrap_state_block.manifest_boot_observed_status != STAGE36_BOOTSTRAP_STATUS_OK ||
        stage36_bootstrap_state_block.manifest_boot_status_record_status != STAGE36_BOOTSTRAP_STATUS_OK ||
        stage36_bootstrap_state_block.manifest_status_checksum != (STAGE36_MANIFEST_VERSION ^
            STAGE36_MANIFEST_RECORD_COUNT ^ STAGE36_MANIFEST_RECORD_REQUIRED_MASK ^
            STAGE36_MANIFEST_RECORD_REQUIRED_MASK ^ STAGE36_MANIFEST_RECORD_REQUIRED_MASK ^
            STAGE36_MANIFEST_REQUIRED_ROOT_STEPS ^ STAGE36_MANIFEST_REQUIRED_ROOT_STEPS ^
            STAGE36_SERVICE_REQUIRED_MASK ^ STAGE36_SERVICE_REQUIRED_MASK ^ STAGE36_BOOTSTRAP_STATUS_OK ^
            STAGE36_PHASE_REQUIRED_MASK ^ STAGE36_PHASE_REQUIRED_MASK ^ STAGE36_BOOTSTRAP_STATUS_OK ^
            STAGE36_PHASE_SERVICE_REQUIRED_MASK ^ STAGE36_PHASE_SERVICE_REQUIRED_MASK ^
            STAGE36_BOOTSTRAP_STATUS_OK ^ STAGE36_REGISTRY_DISPATCH_COVERAGE_REQUIRED ^
            STAGE36_REGISTRY_DISPATCH_COVERAGE_REQUIRED ^ STAGE36_BOOTSTRAP_STATUS_OK ^
            STAGE36_BOOT_POLICY_SAT_REQUIRED ^ STAGE36_BOOT_POLICY_SAT_REQUIRED ^
            STAGE36_BOOTSTRAP_STATUS_OK ^ STAGE36_BOOTSTRAP_STATUS_OK ^ STAGE36_BOOTSTRAP_STATUS_OK ^
            STAGE36_BOOTSTRAP_STATUS_OK) ||
        stage36_bootstrap_state_block.manifest_status != STAGE36_BOOTSTRAP_STATUS_OK) {
        xnu_log_puts("mmu high bootstrap selftest failed: manifest mismatch\n");
        return 0;
    }
    if (stage36_bootstrap_state_block.launch_contract_version != STAGE36_LAUNCH_CONTRACT_VERSION ||
        stage36_bootstrap_state_block.launch_contract_size != sizeof(stage36_bootstrap_state_block) ||
        stage36_bootstrap_state_block.launch_required_root_steps != STAGE36_LAUNCH_REQUIRED_ROOT_STEPS ||
        stage36_bootstrap_state_block.launch_observed_root_steps != STAGE36_LAUNCH_REQUIRED_ROOT_STEPS ||
        stage36_bootstrap_state_block.launch_required_root_status != STAGE36_BOOTSTRAP_STATUS_OK ||
        stage36_bootstrap_state_block.launch_observed_root_status != STAGE36_BOOTSTRAP_STATUS_OK ||
        stage36_bootstrap_state_block.launch_required_manifest_status != STAGE36_BOOTSTRAP_STATUS_OK ||
        stage36_bootstrap_state_block.launch_observed_manifest_status != STAGE36_BOOTSTRAP_STATUS_OK ||
        stage36_bootstrap_state_block.launch_required_policy_status != STAGE36_BOOTSTRAP_STATUS_OK ||
        stage36_bootstrap_state_block.launch_observed_policy_status != STAGE36_BOOTSTRAP_STATUS_OK ||
        stage36_bootstrap_state_block.launch_required_mmu_state != STAGE36_LAUNCH_MMU_ENABLED ||
        stage36_bootstrap_state_block.launch_observed_mmu_state != STAGE36_LAUNCH_MMU_ENABLED ||
        stage36_bootstrap_state_block.launch_required_timebase_freq != 19200000u ||
        stage36_bootstrap_state_block.launch_observed_timebase_freq != 19200000u ||
        stage36_bootstrap_state_block.launch_required_interrupt_mask != STAGE36_LAUNCH_IRQ_READY_REQUIRED ||
        stage36_bootstrap_state_block.launch_observed_interrupt_mask != STAGE36_LAUNCH_IRQ_READY_REQUIRED ||
        stage36_bootstrap_state_block.launch_satisfied_mask != STAGE36_LAUNCH_SAT_REQUIRED ||
        stage36_bootstrap_state_block.launch_status_checksum != (STAGE36_LAUNCH_CONTRACT_VERSION ^
            sizeof(stage36_bootstrap_state_block) ^ STAGE36_LAUNCH_REQUIRED_ROOT_STEPS ^
            STAGE36_LAUNCH_REQUIRED_ROOT_STEPS ^ STAGE36_BOOTSTRAP_STATUS_OK ^
            STAGE36_BOOTSTRAP_STATUS_OK ^ STAGE36_BOOTSTRAP_STATUS_OK ^
            STAGE36_BOOTSTRAP_STATUS_OK ^ STAGE36_BOOTSTRAP_STATUS_OK ^
            STAGE36_BOOTSTRAP_STATUS_OK ^ STAGE36_LAUNCH_MMU_ENABLED ^
            STAGE36_LAUNCH_MMU_ENABLED ^ 19200000u ^ 19200000u ^
            STAGE36_LAUNCH_IRQ_READY_REQUIRED ^ STAGE36_LAUNCH_IRQ_READY_REQUIRED ^
            STAGE36_LAUNCH_SAT_REQUIRED) ||
        stage36_bootstrap_state_block.launch_status != STAGE36_BOOTSTRAP_STATUS_OK) {
        xnu_log_puts("mmu high bootstrap selftest failed: launch contract mismatch\n");
        return 0;
    }
    if (stage36_bootstrap_state_block.startup_boundary_version != STAGE36_STARTUP_BOUNDARY_VERSION ||
        stage36_bootstrap_state_block.startup_boundary_size != sizeof(stage36_bootstrap_state_block) ||
        stage36_bootstrap_state_block.startup_required_root_steps != STAGE36_STARTUP_REQUIRED_ROOT_STEPS ||
        stage36_bootstrap_state_block.startup_observed_root_steps != STAGE36_STARTUP_REQUIRED_ROOT_STEPS ||
        stage36_bootstrap_state_block.startup_required_launch_status != STAGE36_BOOTSTRAP_STATUS_OK ||
        stage36_bootstrap_state_block.startup_observed_launch_status != STAGE36_BOOTSTRAP_STATUS_OK ||
        stage36_bootstrap_state_block.startup_required_root_status != STAGE36_BOOTSTRAP_STATUS_OK ||
        stage36_bootstrap_state_block.startup_observed_root_status != STAGE36_BOOTSTRAP_STATUS_OK ||
        stage36_bootstrap_state_block.startup_required_manifest_status != STAGE36_BOOTSTRAP_STATUS_OK ||
        stage36_bootstrap_state_block.startup_observed_manifest_status != STAGE36_BOOTSTRAP_STATUS_OK ||
        stage36_bootstrap_state_block.startup_required_boot_args_virt != (uint32_t)(uintptr_t)alias_args ||
        stage36_bootstrap_state_block.startup_observed_boot_args_virt != (uint32_t)(uintptr_t)alias_args ||
        stage36_bootstrap_state_block.startup_required_dt_virt != stage36_bootstrap_state_block.root_dt_virt ||
        stage36_bootstrap_state_block.startup_observed_dt_virt != stage36_bootstrap_state_block.root_dt_virt ||
        stage36_bootstrap_state_block.startup_required_timebase_freq != 19200000u ||
        stage36_bootstrap_state_block.startup_observed_timebase_freq != 19200000u ||
        stage36_bootstrap_state_block.startup_required_interrupt_mask != STAGE36_LAUNCH_IRQ_READY_REQUIRED ||
        stage36_bootstrap_state_block.startup_observed_interrupt_mask != STAGE36_LAUNCH_IRQ_READY_REQUIRED ||
        stage36_bootstrap_state_block.startup_satisfied_mask != STAGE36_STARTUP_SAT_REQUIRED ||
        stage36_bootstrap_state_block.startup_status_checksum != (STAGE36_STARTUP_BOUNDARY_VERSION ^
            sizeof(stage36_bootstrap_state_block) ^ STAGE36_STARTUP_REQUIRED_ROOT_STEPS ^
            STAGE36_STARTUP_REQUIRED_ROOT_STEPS ^ STAGE36_BOOTSTRAP_STATUS_OK ^
            STAGE36_BOOTSTRAP_STATUS_OK ^ STAGE36_BOOTSTRAP_STATUS_OK ^
            STAGE36_BOOTSTRAP_STATUS_OK ^ STAGE36_BOOTSTRAP_STATUS_OK ^
            STAGE36_BOOTSTRAP_STATUS_OK ^ (uint32_t)(uintptr_t)alias_args ^
            (uint32_t)(uintptr_t)alias_args ^ stage36_bootstrap_state_block.root_dt_virt ^
            stage36_bootstrap_state_block.root_dt_virt ^ 19200000u ^ 19200000u ^
            STAGE36_LAUNCH_IRQ_READY_REQUIRED ^ STAGE36_LAUNCH_IRQ_READY_REQUIRED ^
            STAGE36_STARTUP_SAT_REQUIRED) ||
        stage36_bootstrap_state_block.startup_status != STAGE36_BOOTSTRAP_STATUS_OK) {
        xnu_log_puts("mmu high bootstrap selftest failed: startup boundary mismatch\n");
        return 0;
    }

    if (stage36_bootstrap_state_block.startup_routine_version != STAGE36_STARTUP_ROUTINE_VERSION ||
        stage36_bootstrap_state_block.startup_routine_size != sizeof(stage36_bootstrap_state_block) ||
        stage36_bootstrap_state_block.startup_routine_required_root_steps != STAGE36_ROUTINE_REQUIRED_ROOT_STEPS ||
        stage36_bootstrap_state_block.startup_routine_observed_root_steps != STAGE36_ROUTINE_REQUIRED_ROOT_STEPS ||
        stage36_bootstrap_state_block.startup_routine_required_startup_status != STAGE36_BOOTSTRAP_STATUS_OK ||
        stage36_bootstrap_state_block.startup_routine_observed_startup_status != STAGE36_BOOTSTRAP_STATUS_OK ||
        stage36_bootstrap_state_block.startup_routine_required_launch_status != STAGE36_BOOTSTRAP_STATUS_OK ||
        stage36_bootstrap_state_block.startup_routine_observed_launch_status != STAGE36_BOOTSTRAP_STATUS_OK ||
        stage36_bootstrap_state_block.startup_routine_required_root_status != STAGE36_BOOTSTRAP_STATUS_OK ||
        stage36_bootstrap_state_block.startup_routine_observed_root_status != STAGE36_BOOTSTRAP_STATUS_OK ||
        stage36_bootstrap_state_block.startup_routine_required_boot_args_virt != (uint32_t)(uintptr_t)alias_args ||
        stage36_bootstrap_state_block.startup_routine_observed_boot_args_virt != (uint32_t)(uintptr_t)alias_args ||
        stage36_bootstrap_state_block.startup_routine_required_dt_virt != stage36_bootstrap_state_block.root_dt_virt ||
        stage36_bootstrap_state_block.startup_routine_observed_dt_virt != stage36_bootstrap_state_block.root_dt_virt ||
        stage36_bootstrap_state_block.startup_routine_required_timebase_freq != 19200000u ||
        stage36_bootstrap_state_block.startup_routine_observed_timebase_freq != 19200000u ||
        stage36_bootstrap_state_block.startup_routine_required_interrupt_mask != STAGE36_LAUNCH_IRQ_READY_REQUIRED ||
        stage36_bootstrap_state_block.startup_routine_observed_interrupt_mask != STAGE36_LAUNCH_IRQ_READY_REQUIRED ||
        stage36_bootstrap_state_block.startup_routine_satisfied_mask != STAGE36_ROUTINE_SAT_REQUIRED ||
        stage36_bootstrap_state_block.startup_routine_status_checksum != (STAGE36_STARTUP_ROUTINE_VERSION ^
            sizeof(stage36_bootstrap_state_block) ^ STAGE36_ROUTINE_REQUIRED_ROOT_STEPS ^
            STAGE36_ROUTINE_REQUIRED_ROOT_STEPS ^ STAGE36_BOOTSTRAP_STATUS_OK ^
            STAGE36_BOOTSTRAP_STATUS_OK ^ STAGE36_BOOTSTRAP_STATUS_OK ^
            STAGE36_BOOTSTRAP_STATUS_OK ^ STAGE36_BOOTSTRAP_STATUS_OK ^
            STAGE36_BOOTSTRAP_STATUS_OK ^ (uint32_t)(uintptr_t)alias_args ^
            (uint32_t)(uintptr_t)alias_args ^ stage36_bootstrap_state_block.root_dt_virt ^
            stage36_bootstrap_state_block.root_dt_virt ^ 19200000u ^ 19200000u ^
            STAGE36_LAUNCH_IRQ_READY_REQUIRED ^ STAGE36_LAUNCH_IRQ_READY_REQUIRED ^
            STAGE36_ROUTINE_SAT_REQUIRED) ||
        stage36_bootstrap_state_block.startup_routine_status != STAGE36_BOOTSTRAP_STATUS_OK) {
        xnu_log_puts("mmu high bootstrap selftest failed: startup routine mismatch\n");
        return 0;
    }

    if (stage36_startup_handoff_block.version != STAGE36_STARTUP_HANDOFF_VERSION ||
        stage36_startup_handoff_block.size != sizeof(stage36_startup_handoff_block) ||
        stage36_startup_handoff_block.root_steps != STAGE36_ENTRY_REQUIRED_ROOT_STEPS ||
        stage36_startup_handoff_block.routine_status != STAGE36_BOOTSTRAP_STATUS_OK ||
        stage36_startup_handoff_block.startup_status != STAGE36_BOOTSTRAP_STATUS_OK ||
        stage36_startup_handoff_block.root_status != STAGE36_BOOTSTRAP_STATUS_OK ||
        stage36_startup_handoff_block.boot_args_virt != (uint32_t)(uintptr_t)alias_args ||
        stage36_startup_handoff_block.dt_virt != stage36_bootstrap_state_block.root_dt_virt ||
        stage36_startup_handoff_block.timebase_freq != 19200000u ||
        stage36_startup_handoff_block.interrupt_mask != STAGE36_LAUNCH_IRQ_READY_REQUIRED ||
        stage36_startup_handoff_block.checksum != expected_handoff ||
        stage36_startup_handoff_block.status != STAGE36_BOOTSTRAP_STATUS_OK ||
        alias_handoff->checksum != stage36_startup_handoff_block.checksum ||
        alias_handoff->status != STAGE36_BOOTSTRAP_STATUS_OK) {
        xnu_log_puts("mmu high bootstrap selftest failed: startup handoff mismatch\n");
        return 0;
    }

    if (stage36_bootstrap_state_block.startup_entry_version != STAGE36_STARTUP_ENTRY_VERSION ||
        stage36_bootstrap_state_block.startup_entry_size != sizeof(stage36_bootstrap_state_block) ||
        stage36_bootstrap_state_block.startup_entry_required_root_steps != STAGE36_ENTRY_REQUIRED_ROOT_STEPS ||
        stage36_bootstrap_state_block.startup_entry_observed_root_steps != STAGE36_ENTRY_REQUIRED_ROOT_STEPS ||
        stage36_bootstrap_state_block.startup_entry_required_routine_status != STAGE36_BOOTSTRAP_STATUS_OK ||
        stage36_bootstrap_state_block.startup_entry_observed_routine_status != STAGE36_BOOTSTRAP_STATUS_OK ||
        stage36_bootstrap_state_block.startup_entry_required_startup_status != STAGE36_BOOTSTRAP_STATUS_OK ||
        stage36_bootstrap_state_block.startup_entry_observed_startup_status != STAGE36_BOOTSTRAP_STATUS_OK ||
        stage36_bootstrap_state_block.startup_entry_required_root_status != STAGE36_BOOTSTRAP_STATUS_OK ||
        stage36_bootstrap_state_block.startup_entry_observed_root_status != STAGE36_BOOTSTRAP_STATUS_OK ||
        stage36_bootstrap_state_block.startup_entry_required_boot_args_virt != (uint32_t)(uintptr_t)alias_args ||
        stage36_bootstrap_state_block.startup_entry_observed_boot_args_virt != (uint32_t)(uintptr_t)alias_args ||
        stage36_bootstrap_state_block.startup_entry_required_dt_virt != stage36_bootstrap_state_block.root_dt_virt ||
        stage36_bootstrap_state_block.startup_entry_observed_dt_virt != stage36_bootstrap_state_block.root_dt_virt ||
        stage36_bootstrap_state_block.startup_entry_required_timebase_freq != 19200000u ||
        stage36_bootstrap_state_block.startup_entry_observed_timebase_freq != 19200000u ||
        stage36_bootstrap_state_block.startup_entry_required_interrupt_mask != STAGE36_LAUNCH_IRQ_READY_REQUIRED ||
        stage36_bootstrap_state_block.startup_entry_observed_interrupt_mask != STAGE36_LAUNCH_IRQ_READY_REQUIRED ||
        stage36_bootstrap_state_block.startup_entry_satisfied_mask != STAGE36_ENTRY_SAT_REQUIRED ||
        stage36_bootstrap_state_block.startup_entry_status_checksum != (STAGE36_STARTUP_ENTRY_VERSION ^
            sizeof(stage36_bootstrap_state_block) ^ STAGE36_ENTRY_REQUIRED_ROOT_STEPS ^
            STAGE36_ENTRY_REQUIRED_ROOT_STEPS ^ STAGE36_BOOTSTRAP_STATUS_OK ^
            STAGE36_BOOTSTRAP_STATUS_OK ^ STAGE36_BOOTSTRAP_STATUS_OK ^
            STAGE36_BOOTSTRAP_STATUS_OK ^ STAGE36_BOOTSTRAP_STATUS_OK ^
            STAGE36_BOOTSTRAP_STATUS_OK ^ (uint32_t)(uintptr_t)alias_args ^
            (uint32_t)(uintptr_t)alias_args ^ stage36_bootstrap_state_block.root_dt_virt ^
            stage36_bootstrap_state_block.root_dt_virt ^ 19200000u ^ 19200000u ^
            STAGE36_LAUNCH_IRQ_READY_REQUIRED ^ STAGE36_LAUNCH_IRQ_READY_REQUIRED ^
            STAGE36_ENTRY_SAT_REQUIRED) ||
        stage36_bootstrap_state_block.startup_entry_status != STAGE36_BOOTSTRAP_STATUS_OK) {
        xnu_log_puts("mmu high bootstrap selftest failed: startup entry mismatch\n");
        return 0;
    }

    if (stage36_bootstrap_state_block.kernel_callout_version != STAGE36_KERNEL_CALLOUT_VERSION ||
        stage36_bootstrap_state_block.kernel_callout_count != STAGE36_KERNEL_CALLOUT_COUNT ||
        stage36_bootstrap_state_block.kernel_callout_required_mask != STAGE36_KERNEL_CALLOUT_REQUIRED_MASK ||
        stage36_bootstrap_state_block.kernel_callout_order_mask != STAGE36_KERNEL_CALLOUT_REQUIRED_MASK ||
        stage36_bootstrap_state_block.kernel_callout_handler_mask != STAGE36_KERNEL_CALLOUT_REQUIRED_MASK ||
        stage36_bootstrap_state_block.kernel_callout_required_service_mask != STAGE36_KERNEL_CALLOUT_REQUIRED_SERVICES ||
        stage36_bootstrap_state_block.kernel_callout_observed_service_mask != STAGE36_SERVICE_REQUIRED_MASK ||
        stage36_bootstrap_state_block.kernel_callout0_descriptor_id != STAGE36_KERNEL_CALLOUT_BOOTSTRAP ||
        stage36_bootstrap_state_block.kernel_callout1_descriptor_id != STAGE36_KERNEL_CALLOUT_PLATFORM ||
        stage36_bootstrap_state_block.kernel_callout2_descriptor_id != STAGE36_KERNEL_CALLOUT_TIMEBASE ||
        stage36_bootstrap_state_block.kernel_callout3_descriptor_id != STAGE36_KERNEL_CALLOUT_INTERRUPTS ||
        stage36_bootstrap_state_block.kernel_callout0_required_services != STAGE36_SERVICE_BIT_LOGGING ||
        stage36_bootstrap_state_block.kernel_callout1_required_services != STAGE36_SERVICE_BIT_PLATFORM ||
        stage36_bootstrap_state_block.kernel_callout2_required_services != STAGE36_SERVICE_BIT_TIMEBASE ||
        stage36_bootstrap_state_block.kernel_callout3_required_services != STAGE36_SERVICE_BIT_INTERRUPTS ||
        stage36_bootstrap_state_block.kernel_callout0_status != STAGE36_BOOTSTRAP_STATUS_OK ||
        stage36_bootstrap_state_block.kernel_callout1_status != STAGE36_BOOTSTRAP_STATUS_OK ||
        stage36_bootstrap_state_block.kernel_callout2_status != STAGE36_BOOTSTRAP_STATUS_OK ||
        stage36_bootstrap_state_block.kernel_callout3_status != STAGE36_BOOTSTRAP_STATUS_OK ||
        stage36_bootstrap_state_block.kernel_callout_status_checksum != (STAGE36_KERNEL_CALLOUT_VERSION ^
            STAGE36_KERNEL_CALLOUT_COUNT ^ STAGE36_KERNEL_CALLOUT_REQUIRED_MASK ^
            STAGE36_KERNEL_CALLOUT_REQUIRED_MASK ^ STAGE36_KERNEL_CALLOUT_REQUIRED_MASK ^
            STAGE36_KERNEL_CALLOUT_REQUIRED_SERVICES ^ STAGE36_SERVICE_REQUIRED_MASK ^
            STAGE36_KERNEL_CALLOUT_BOOTSTRAP ^ STAGE36_KERNEL_CALLOUT_PLATFORM ^
            STAGE36_KERNEL_CALLOUT_TIMEBASE ^ STAGE36_KERNEL_CALLOUT_INTERRUPTS ^
            STAGE36_SERVICE_BIT_LOGGING ^ STAGE36_SERVICE_BIT_PLATFORM ^
            STAGE36_SERVICE_BIT_TIMEBASE ^ STAGE36_SERVICE_BIT_INTERRUPTS ^
            STAGE36_BOOTSTRAP_STATUS_OK ^ STAGE36_BOOTSTRAP_STATUS_OK ^
            STAGE36_BOOTSTRAP_STATUS_OK ^ STAGE36_BOOTSTRAP_STATUS_OK) ||
        stage36_bootstrap_state_block.kernel_callout_status != STAGE36_BOOTSTRAP_STATUS_OK) {
        xnu_log_puts("mmu high bootstrap selftest failed: kernel callout mismatch\n");
        return 0;
    }

    if (stage36_bootstrap_state_block.kernel_context_version != STAGE36_KERNEL_CONTEXT_VERSION ||
        stage36_bootstrap_state_block.kernel_context_size != sizeof(stage36_kernel_context_block) ||
        stage36_bootstrap_state_block.kernel_context_required_mask != STAGE36_KERNEL_CONTEXT_SAT_REQUIRED ||
        stage36_bootstrap_state_block.kernel_context_satisfied_mask != STAGE36_KERNEL_CONTEXT_SAT_REQUIRED ||
        stage36_bootstrap_state_block.kernel_context_checksum != stage36_kernel_context_block.checksum ||
        stage36_bootstrap_state_block.kernel_context_status != STAGE36_BOOTSTRAP_STATUS_OK ||
        stage36_kernel_context_block.version != STAGE36_KERNEL_CONTEXT_VERSION ||
        stage36_kernel_context_block.size != sizeof(stage36_kernel_context_block) ||
        stage36_kernel_context_block.boot_args_virt != (uint32_t)(uintptr_t)alias_args ||
        stage36_kernel_context_block.dt_virt != stage36_bootstrap_state_block.root_dt_virt ||
        stage36_kernel_context_block.platform_status != STAGE36_BOOTSTRAP_STATUS_OK ||
        stage36_kernel_context_block.platform_consistency != STAGE36_PLATFORM_CONSIST_REQUIRED ||
        stage36_kernel_context_block.timebase_freq != 19200000u ||
        stage36_kernel_context_block.interrupt_mask != STAGE36_LAUNCH_IRQ_READY_REQUIRED ||
        stage36_kernel_context_block.callout_status != STAGE36_BOOTSTRAP_STATUS_OK ||
        stage36_kernel_context_block.callout_mask != STAGE36_KERNEL_CALLOUT_REQUIRED_MASK ||
        stage36_kernel_context_block.root_steps != STAGE36_ENTRY_REQUIRED_ROOT_STEPS ||
        stage36_kernel_context_block.satisfied_mask != STAGE36_KERNEL_CONTEXT_SAT_REQUIRED ||
        stage36_kernel_context_block.checksum != stage36_kernel_context_checksum(&stage36_kernel_context_block) ||
        stage36_kernel_context_block.status != STAGE36_BOOTSTRAP_STATUS_OK ||
        alias_context->checksum != stage36_kernel_context_block.checksum ||
        alias_context->status != STAGE36_BOOTSTRAP_STATUS_OK) {
        xnu_log_puts("mmu high bootstrap selftest failed: kernel context mismatch\n");
        return 0;
    }

    if (stage36_bootstrap_state_block.vm_plan_version != STAGE36_VM_PLAN_VERSION ||
        stage36_bootstrap_state_block.vm_plan_size != sizeof(stage36_vm_plan_block) ||
        stage36_bootstrap_state_block.vm_plan_required_mask != STAGE36_VM_PLAN_SAT_REQUIRED ||
        stage36_bootstrap_state_block.vm_plan_satisfied_mask != STAGE36_VM_PLAN_SAT_REQUIRED ||
        stage36_bootstrap_state_block.vm_plan_checksum != stage36_vm_plan_block.checksum ||
        stage36_bootstrap_state_block.vm_plan_status != STAGE36_BOOTSTRAP_STATUS_OK ||
        stage36_vm_plan_block.version != STAGE36_VM_PLAN_VERSION ||
        stage36_vm_plan_block.size != sizeof(stage36_vm_plan_block) ||
        stage36_vm_plan_block.context_status != STAGE36_BOOTSTRAP_STATUS_OK ||
        stage36_vm_plan_block.context_checksum != stage36_kernel_context_block.checksum ||
        stage36_vm_plan_block.low_identity_base != STAGE36_VM_PLAN_IDENTITY_BASE ||
        stage36_vm_plan_block.high_alias_base != STAGE36_HIGH_ALIAS_BASE ||
        stage36_vm_plan_block.ram_console_alias_base != STAGE36_RAM_CONSOLE_ALIAS_BASE ||
        stage36_vm_plan_block.gic_alias_base != STAGE36_GIC_ALIAS_BASE ||
        stage36_vm_plan_block.l1_table_phys != (uint32_t)(uintptr_t)stage36_l1_table ||
        stage36_vm_plan_block.l1_table_virt != (STAGE36_HIGH_ALIAS_BASE + (uint32_t)(uintptr_t)stage36_l1_table) ||
        stage36_vm_plan_block.memory_base != RAM_PHYS_BASE ||
        stage36_vm_plan_block.memory_size != (RAM_CONSOLE_BASE - RAM_PHYS_BASE) ||
        stage36_vm_plan_block.section_size != L1_SECTION_SIZE ||
        stage36_vm_plan_block.section_descriptor != L1_DESC_SECTION_SO ||
        stage36_vm_plan_block.mmu_enabled != STAGE36_VM_PLAN_MMU_ENABLED ||
        stage36_vm_plan_block.cache_policy != STAGE36_VM_PLAN_CACHES_DISABLED ||
        stage36_vm_plan_block.satisfied_mask != STAGE36_VM_PLAN_SAT_REQUIRED ||
        stage36_vm_plan_block.checksum != stage36_vm_plan_checksum(&stage36_vm_plan_block) ||
        stage36_vm_plan_block.status != STAGE36_BOOTSTRAP_STATUS_OK ||
        alias_vm_plan->checksum != stage36_vm_plan_block.checksum ||
        alias_vm_plan->status != STAGE36_BOOTSTRAP_STATUS_OK) {
        xnu_log_puts("mmu high bootstrap selftest failed: VM plan mismatch\n");
        return 0;
    }

    if (stage36_bootstrap_state_block.vm_state_version != STAGE36_VM_STATE_VERSION ||
        stage36_bootstrap_state_block.vm_state_size != sizeof(stage36_vm_state_block) ||
        stage36_bootstrap_state_block.vm_state_required_mask != STAGE36_VM_STATE_SAT_REQUIRED ||
        stage36_bootstrap_state_block.vm_state_satisfied_mask != STAGE36_VM_STATE_SAT_REQUIRED ||
        stage36_bootstrap_state_block.vm_state_checksum != stage36_vm_state_block.checksum ||
        stage36_bootstrap_state_block.vm_state_status != STAGE36_BOOTSTRAP_STATUS_OK ||
        stage36_vm_state_block.version != STAGE36_VM_STATE_VERSION ||
        stage36_vm_state_block.size != sizeof(stage36_vm_state_block) ||
        stage36_vm_state_block.vm_plan_status != STAGE36_BOOTSTRAP_STATUS_OK ||
        stage36_vm_state_block.vm_plan_checksum != stage36_vm_plan_block.checksum ||
        stage36_vm_state_block.kernel_map_base != STAGE36_KERNEL_MAP_BASE ||
        stage36_vm_state_block.kernel_map_limit != STAGE36_KERNEL_MAP_LIMIT ||
        stage36_vm_state_block.available_memory_base != RAM_PHYS_BASE ||
        stage36_vm_state_block.available_memory_cursor != (RAM_PHYS_BASE + STAGE36_BOOTSTRAP_ALLOC_SIZE) ||
        stage36_vm_state_block.bootstrap_alloc_base != RAM_PHYS_BASE ||
        stage36_vm_state_block.bootstrap_alloc_size != STAGE36_BOOTSTRAP_ALLOC_SIZE ||
        stage36_vm_state_block.bootstrap_alloc_end != (RAM_PHYS_BASE + STAGE36_BOOTSTRAP_ALLOC_SIZE) ||
        stage36_vm_state_block.pmap_section_size != L1_SECTION_SIZE ||
        stage36_vm_state_block.pmap_section_descriptor != L1_DESC_SECTION_SO ||
        stage36_vm_state_block.pmap_l1_table_phys != (uint32_t)(uintptr_t)stage36_l1_table ||
        stage36_vm_state_block.pmap_l1_table_virt != (STAGE36_HIGH_ALIAS_BASE + (uint32_t)(uintptr_t)stage36_l1_table) ||
        stage36_vm_state_block.mmu_enabled != STAGE36_VM_PLAN_MMU_ENABLED ||
        stage36_vm_state_block.cache_policy != STAGE36_VM_PLAN_CACHES_DISABLED ||
        stage36_vm_state_block.satisfied_mask != STAGE36_VM_STATE_SAT_REQUIRED ||
        stage36_vm_state_block.checksum != stage36_vm_state_checksum(&stage36_vm_state_block) ||
        stage36_vm_state_block.status != STAGE36_BOOTSTRAP_STATUS_OK ||
        alias_vm_state->checksum != stage36_vm_state_block.checksum ||
        alias_vm_state->status != STAGE36_BOOTSTRAP_STATUS_OK) {
        xnu_log_puts("mmu high bootstrap selftest failed: VM state mismatch\n");
        return 0;
    }


    if (stage36_bootstrap_state_block.allocator_version != STAGE36_BOOT_ALLOCATOR_VERSION ||
        stage36_bootstrap_state_block.allocator_size != sizeof(stage36_boot_allocator_block) ||
        stage36_bootstrap_state_block.allocator_required_mask != STAGE36_BOOT_ALLOCATOR_SAT_REQUIRED ||
        stage36_bootstrap_state_block.allocator_satisfied_mask != STAGE36_BOOT_ALLOCATOR_SAT_REQUIRED ||
        stage36_bootstrap_state_block.allocator_checksum != stage36_boot_allocator_block.checksum ||
        stage36_bootstrap_state_block.allocator_status != STAGE36_BOOTSTRAP_STATUS_OK ||
        stage36_boot_allocator_block.version != STAGE36_BOOT_ALLOCATOR_VERSION ||
        stage36_boot_allocator_block.size != sizeof(stage36_boot_allocator_block) ||
        stage36_boot_allocator_block.vm_state_status != STAGE36_BOOTSTRAP_STATUS_OK ||
        stage36_boot_allocator_block.vm_state_checksum != stage36_vm_state_block.checksum ||
        stage36_boot_allocator_block.span_base != RAM_PHYS_BASE ||
        stage36_boot_allocator_block.span_size != STAGE36_BOOTSTRAP_ALLOC_SIZE ||
        stage36_boot_allocator_block.span_end != (RAM_PHYS_BASE + STAGE36_BOOTSTRAP_ALLOC_SIZE) ||
        stage36_boot_allocator_block.initial_cursor != RAM_PHYS_BASE ||
        stage36_boot_allocator_block.current_cursor != (RAM_PHYS_BASE + STAGE36_BOOTSTRAP_ALLOC_SIZE) ||
        stage36_boot_allocator_block.remaining_bytes != 0u ||
        stage36_boot_allocator_block.first_alloc_base != RAM_PHYS_BASE ||
        stage36_boot_allocator_block.first_alloc_size != STAGE36_BOOTSTRAP_ALLOC_SIZE ||
        stage36_boot_allocator_block.first_alloc_end != (RAM_PHYS_BASE + STAGE36_BOOTSTRAP_ALLOC_SIZE) ||
        stage36_boot_allocator_block.first_alloc_tag != STAGE36_BOOT_ALLOCATOR_FIRST_TAG ||
        stage36_boot_allocator_block.alignment != STAGE36_BOOT_ALLOCATOR_ALIGNMENT ||
        stage36_boot_allocator_block.satisfied_mask != STAGE36_BOOT_ALLOCATOR_SAT_REQUIRED ||
        stage36_boot_allocator_block.checksum != stage36_boot_allocator_checksum(&stage36_boot_allocator_block) ||
        stage36_boot_allocator_block.status != STAGE36_BOOTSTRAP_STATUS_OK ||
        alias_allocator->checksum != stage36_boot_allocator_block.checksum ||
        alias_allocator->status != STAGE36_BOOTSTRAP_STATUS_OK ||
        alias_allocator->current_cursor != stage36_boot_allocator_block.current_cursor ||
        alias_allocator->remaining_bytes != 0u) {
        xnu_log_puts("mmu high bootstrap selftest failed: bootstrap allocator mismatch\n");
        return 0;
    }

    if (stage36_bootstrap_state_block.init_steps != STAGE36_INIT_REQUIRED_STEPS ||
        stage36_bootstrap_state_block.init_timebase_freq != 19200000u ||
        stage36_bootstrap_state_block.init_timebase_delta_us < 900u ||
        stage36_bootstrap_state_block.init_gic_irq_count < 288u ||
        stage36_bootstrap_state_block.init_gic_cpu_count != 4u ||
        (stage36_bootstrap_state_block.init_gic_dist_ctlr & 1u) == 0u ||
        (stage36_bootstrap_state_block.init_gic_cpu_ctlr & 1u) == 0u) {
        xnu_log_puts("mmu high bootstrap selftest failed: high init sequence mismatch\n");
        return 0;
    }
    if (stage36_bootstrap_state_block.pe_memory_base != PE_state_stage36.memoryBase ||
        stage36_bootstrap_state_block.pe_memory_size != PE_state_stage36.memorySize ||
        stage36_bootstrap_state_block.pe_cpu_count != PE_state_stage36.cpuCount ||
        stage36_bootstrap_state_block.pe_gic_dist_base != PE_state_stage36.gicDistributorBase ||
        stage36_bootstrap_state_block.pe_gic_cpu_base != PE_state_stage36.gicCpuBase ||
        stage36_bootstrap_state_block.pe_timer_base != PE_state_stage36.timerBase ||
        stage36_bootstrap_state_block.pe_timer_frequency != 19200000u ||
        stage36_bootstrap_state_block.pe_vector_base != (uint32_t)(uintptr_t)stage36_vectors) {
        xnu_log_puts("mmu high bootstrap selftest failed: PE state mismatch\n");
        return 0;
    }
    if (alias_state->checksum != stage36_bootstrap_state_block.checksum ||
        alias_state->status != STAGE36_BOOTSTRAP_STATUS_OK ||
        alias_state->root_status != STAGE36_BOOTSTRAP_STATUS_OK ||
        alias_state->root_steps != STAGE36_ROOT_REQUIRED_STEPS ||
        alias_state->root_dt_summary_status != STAGE36_BOOTSTRAP_STATUS_OK ||
        alias_state->root_dt_memory_base != RAM_PHYS_BASE ||
        alias_state->root_dt_timer_frequency != 19200000u ||
        alias_state->platform_result_consistency != STAGE36_PLATFORM_CONSIST_REQUIRED ||
        alias_state->platform_result_status != STAGE36_BOOTSTRAP_STATUS_OK ||
        alias_state->platform_dt_cpu_count != 4u ||
        alias_state->phase_completed_mask != STAGE36_PHASE_REQUIRED_MASK ||
        alias_state->phase_status_checksum != stage36_bootstrap_state_block.phase_status_checksum ||
        alias_state->phase0_required_services != STAGE36_PHASE_VALIDATE_SERVICES ||
        alias_state->phase3_required_services != STAGE36_PHASE_RETURN_READY_SERVICES ||
        alias_state->phase_service_dependency_mask != STAGE36_PHASE_SERVICE_REQUIRED_MASK ||
        alias_state->phase_service_satisfied_mask != STAGE36_PHASE_REQUIRED_MASK ||
        alias_state->phase_service_status_checksum != stage36_bootstrap_state_block.phase_service_status_checksum ||
        alias_state->phase_service_dependency_status != STAGE36_BOOTSTRAP_STATUS_OK ||
        alias_state->phase_dispatcher_order_mask != STAGE36_PHASE_REQUIRED_MASK ||
        alias_state->phase_dispatcher_handler_mask != STAGE36_PHASE_REQUIRED_MASK ||
        alias_state->phase_dispatcher_status_checksum != stage36_bootstrap_state_block.phase_dispatcher_status_checksum ||
        alias_state->phase0_descriptor_id != STAGE36_PHASE_VALIDATE ||
        alias_state->phase3_descriptor_id != STAGE36_PHASE_RETURN_READY ||
        alias_state->phase0_handler_result != STAGE36_BOOTSTRAP_STATUS_OK ||
        alias_state->phase3_handler_result != STAGE36_BOOTSTRAP_STATUS_OK ||
        alias_state->phase_dispatcher_status != STAGE36_BOOTSTRAP_STATUS_OK ||
        alias_state->service_available_mask != STAGE36_SERVICE_REQUIRED_MASK ||
        alias_state->service_status_checksum != stage36_bootstrap_state_block.service_status_checksum ||
        alias_state->service_platform_status != STAGE36_BOOTSTRAP_STATUS_OK ||
        alias_state->service_dispatcher_order_mask != STAGE36_SERVICE_REQUIRED_MASK ||
        alias_state->service_dispatcher_handler_mask != STAGE36_SERVICE_REQUIRED_MASK ||
        alias_state->service_dispatcher_status_checksum != stage36_bootstrap_state_block.service_dispatcher_status_checksum ||
        alias_state->service_logging_descriptor_id != STAGE36_SERVICE_LOGGING ||
        alias_state->service_interrupts_descriptor_id != STAGE36_SERVICE_INTERRUPTS ||
        alias_state->service_logging_handler_result != STAGE36_BOOTSTRAP_STATUS_OK ||
        alias_state->service_interrupts_handler_result != STAGE36_BOOTSTRAP_STATUS_OK ||
        alias_state->service_dispatcher_status != STAGE36_BOOTSTRAP_STATUS_OK ||
        alias_state->registry_service_descriptor_mask != STAGE36_SERVICE_REQUIRED_MASK ||
        alias_state->registry_phase_descriptor_mask != STAGE36_PHASE_REQUIRED_MASK ||
        alias_state->registry_dependency_coverage_mask != STAGE36_PHASE_SERVICE_REQUIRED_MASK ||
        alias_state->registry_dispatch_coverage_mask != STAGE36_REGISTRY_DISPATCH_COVERAGE_REQUIRED ||
        alias_state->registry_status_checksum != stage36_bootstrap_state_block.registry_status_checksum ||
        alias_state->registry_status != STAGE36_BOOTSTRAP_STATUS_OK ||
        alias_state->boot_policy_required_root_steps != STAGE36_BOOT_POLICY_REQUIRED_ROOT_STEPS ||
        alias_state->boot_policy_observed_root_steps != STAGE36_BOOT_POLICY_REQUIRED_ROOT_STEPS ||
        alias_state->boot_policy_observed_service_mask != STAGE36_SERVICE_REQUIRED_MASK ||
        alias_state->boot_policy_observed_phase_mask != STAGE36_PHASE_REQUIRED_MASK ||
        alias_state->boot_policy_observed_dependency_mask != STAGE36_PHASE_SERVICE_REQUIRED_MASK ||
        alias_state->boot_policy_observed_dispatch_coverage_mask != STAGE36_REGISTRY_DISPATCH_COVERAGE_REQUIRED ||
        alias_state->boot_policy_observed_registry_status != STAGE36_BOOTSTRAP_STATUS_OK ||
        alias_state->boot_policy_satisfied_mask != STAGE36_BOOT_POLICY_SAT_REQUIRED ||
        alias_state->boot_policy_status_checksum != stage36_bootstrap_state_block.boot_policy_status_checksum ||
        alias_state->boot_policy_status != STAGE36_BOOTSTRAP_STATUS_OK ||
        alias_state->manifest_order_mask != STAGE36_MANIFEST_RECORD_REQUIRED_MASK ||
        alias_state->manifest_satisfied_mask != STAGE36_MANIFEST_RECORD_REQUIRED_MASK ||
        alias_state->manifest_observed_root_steps != STAGE36_MANIFEST_REQUIRED_ROOT_STEPS ||
        alias_state->manifest_service_observed_mask != STAGE36_SERVICE_REQUIRED_MASK ||
        alias_state->manifest_phase_observed_mask != STAGE36_PHASE_REQUIRED_MASK ||
        alias_state->manifest_dependency_observed_mask != STAGE36_PHASE_SERVICE_REQUIRED_MASK ||
        alias_state->manifest_dispatch_observed_mask != STAGE36_REGISTRY_DISPATCH_COVERAGE_REQUIRED ||
        alias_state->manifest_policy_observed_mask != STAGE36_BOOT_POLICY_SAT_REQUIRED ||
        alias_state->manifest_boot_observed_status != STAGE36_BOOTSTRAP_STATUS_OK ||
        alias_state->manifest_status_checksum != stage36_bootstrap_state_block.manifest_status_checksum ||
        alias_state->manifest_status != STAGE36_BOOTSTRAP_STATUS_OK ||
        alias_state->launch_observed_root_steps != STAGE36_LAUNCH_REQUIRED_ROOT_STEPS ||
        alias_state->launch_observed_root_status != STAGE36_BOOTSTRAP_STATUS_OK ||
        alias_state->launch_observed_manifest_status != STAGE36_BOOTSTRAP_STATUS_OK ||
        alias_state->launch_observed_policy_status != STAGE36_BOOTSTRAP_STATUS_OK ||
        alias_state->launch_observed_mmu_state != STAGE36_LAUNCH_MMU_ENABLED ||
        alias_state->launch_observed_timebase_freq != 19200000u ||
        alias_state->launch_observed_interrupt_mask != STAGE36_LAUNCH_IRQ_READY_REQUIRED ||
        alias_state->launch_satisfied_mask != STAGE36_LAUNCH_SAT_REQUIRED ||
        alias_state->launch_status_checksum != stage36_bootstrap_state_block.launch_status_checksum ||
        alias_state->launch_status != STAGE36_BOOTSTRAP_STATUS_OK ||
        alias_state->startup_observed_root_steps != STAGE36_STARTUP_REQUIRED_ROOT_STEPS ||
        alias_state->startup_observed_launch_status != STAGE36_BOOTSTRAP_STATUS_OK ||
        alias_state->startup_observed_root_status != STAGE36_BOOTSTRAP_STATUS_OK ||
        alias_state->startup_observed_boot_args_virt != (uint32_t)(uintptr_t)alias_args ||
        alias_state->startup_observed_dt_virt != stage36_bootstrap_state_block.root_dt_virt ||
        alias_state->startup_observed_timebase_freq != 19200000u ||
        alias_state->startup_observed_interrupt_mask != STAGE36_LAUNCH_IRQ_READY_REQUIRED ||
        alias_state->startup_satisfied_mask != STAGE36_STARTUP_SAT_REQUIRED ||
        alias_state->startup_status_checksum != stage36_bootstrap_state_block.startup_status_checksum ||
        alias_state->startup_status != STAGE36_BOOTSTRAP_STATUS_OK ||
        alias_state->startup_routine_observed_root_steps != STAGE36_ROUTINE_REQUIRED_ROOT_STEPS ||
        alias_state->startup_routine_observed_startup_status != STAGE36_BOOTSTRAP_STATUS_OK ||
        alias_state->startup_routine_observed_launch_status != STAGE36_BOOTSTRAP_STATUS_OK ||
        alias_state->startup_routine_observed_root_status != STAGE36_BOOTSTRAP_STATUS_OK ||
        alias_state->startup_routine_observed_boot_args_virt != (uint32_t)(uintptr_t)alias_args ||
        alias_state->startup_routine_observed_dt_virt != stage36_bootstrap_state_block.root_dt_virt ||
        alias_state->startup_routine_observed_timebase_freq != 19200000u ||
        alias_state->startup_routine_observed_interrupt_mask != STAGE36_LAUNCH_IRQ_READY_REQUIRED ||
        alias_state->startup_routine_satisfied_mask != STAGE36_ROUTINE_SAT_REQUIRED ||
        alias_state->startup_routine_status_checksum != stage36_bootstrap_state_block.startup_routine_status_checksum ||
        alias_state->startup_routine_status != STAGE36_BOOTSTRAP_STATUS_OK ||
        alias_state->startup_entry_observed_root_steps != STAGE36_ENTRY_REQUIRED_ROOT_STEPS ||
        alias_state->startup_entry_observed_routine_status != STAGE36_BOOTSTRAP_STATUS_OK ||
        alias_state->startup_entry_observed_startup_status != STAGE36_BOOTSTRAP_STATUS_OK ||
        alias_state->startup_entry_observed_root_status != STAGE36_BOOTSTRAP_STATUS_OK ||
        alias_state->startup_entry_observed_boot_args_virt != (uint32_t)(uintptr_t)alias_args ||
        alias_state->startup_entry_observed_dt_virt != stage36_bootstrap_state_block.root_dt_virt ||
        alias_state->startup_entry_observed_timebase_freq != 19200000u ||
        alias_state->startup_entry_observed_interrupt_mask != STAGE36_LAUNCH_IRQ_READY_REQUIRED ||
        alias_state->startup_entry_satisfied_mask != STAGE36_ENTRY_SAT_REQUIRED ||
        alias_state->startup_entry_status_checksum != stage36_bootstrap_state_block.startup_entry_status_checksum ||
        alias_state->startup_entry_status != STAGE36_BOOTSTRAP_STATUS_OK ||
        alias_state->kernel_callout_order_mask != STAGE36_KERNEL_CALLOUT_REQUIRED_MASK ||
        alias_state->kernel_callout_handler_mask != STAGE36_KERNEL_CALLOUT_REQUIRED_MASK ||
        alias_state->kernel_callout_observed_service_mask != STAGE36_SERVICE_REQUIRED_MASK ||
        alias_state->kernel_callout_status_checksum != stage36_bootstrap_state_block.kernel_callout_status_checksum ||
        alias_state->kernel_callout_status != STAGE36_BOOTSTRAP_STATUS_OK ||
        alias_state->kernel_context_satisfied_mask != STAGE36_KERNEL_CONTEXT_SAT_REQUIRED ||
        alias_state->kernel_context_checksum != stage36_bootstrap_state_block.kernel_context_checksum ||
        alias_state->kernel_context_status != STAGE36_BOOTSTRAP_STATUS_OK ||
        alias_state->vm_plan_satisfied_mask != STAGE36_VM_PLAN_SAT_REQUIRED ||
        alias_state->vm_plan_checksum != stage36_bootstrap_state_block.vm_plan_checksum ||
        alias_state->vm_plan_status != STAGE36_BOOTSTRAP_STATUS_OK ||
        alias_state->vm_state_satisfied_mask != STAGE36_VM_STATE_SAT_REQUIRED ||
        alias_state->vm_state_checksum != stage36_bootstrap_state_block.vm_state_checksum ||
        alias_state->vm_state_status != STAGE36_BOOTSTRAP_STATUS_OK ||
        alias_state->allocator_satisfied_mask != STAGE36_BOOT_ALLOCATOR_SAT_REQUIRED ||
        alias_state->allocator_checksum != stage36_bootstrap_state_block.allocator_checksum ||
        alias_state->allocator_status != STAGE36_BOOTSTRAP_STATUS_OK ||
        alias_state->init_status != STAGE36_BOOTSTRAP_STATUS_OK ||
        alias_state->init_steps != STAGE36_INIT_REQUIRED_STEPS ||
        alias_state->validation_mask != 0u) {
        xnu_log_puts("mmu high bootstrap selftest failed: alias state mismatch\n");
        return 0;
    }

    xnu_log_puts("mmu high bootstrap selftest ok\n");
    return 1;
}
