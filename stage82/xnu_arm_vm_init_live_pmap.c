/* Stage82 Stage-owned arm_vm_init-shaped live-pmap installation window.
 * Models public ARM XNU arm_vm_init(memory_size, boot_args*) / pmap_bootstrap(vstart)
 * but still Stage-owned and using the proven mmu_stage82_ttbr0_roundtrip_selftest()
 * pattern. Installs a Stage-owned candidate L1 pmap, invalidates TLBs, verifies
 * live translation (high-alias/RAM console/GIC reads), then restores original TTBR0.
 * This is the first Method-C stage to install a live pmap and invalidate TLBs,
 * but it still avoids public XNU/pmap runtime, generated Mach-O execution, and
 * persistent writes.
 *
 * Public ARM XNU reference (not executed):
 *   external/xnu-4570.1.46/osfmk/arm/arm_vm_init.c
 *   external/xnu-4570.1.46/osfmk/arm/pmap.c
 */

#include "stage82.h"
#include <stddef.h>

#define STAGE82_HIGH_ALIAS_BASE        0xc0000000u
#define STAGE82_RAM_CONSOLE_ALIAS_BASE 0xc0100000u
#define STAGE82_GIC_ALIAS_BASE         0xc0200000u
#define STAGE82_XNU_TTE_L1_ENTRY_COUNT 4096u

static struct stage82_xnu_arm_vm_init_live_pmap_result g_result;
static volatile uint32_t stage82_live_pmap_probe_word;

/* 16KB-aligned candidate L1 table for the live-pmap install window. */
static uint32_t stage82_live_pmap_candidate_l1[STAGE82_XNU_TTE_L1_ENTRY_COUNT]
    __attribute__((aligned(16384)));

static uint32_t stage82_xnu_arm_vm_init_live_pmap_checksum(
    volatile const struct stage82_xnu_arm_vm_init_live_pmap_result *r)
{
    volatile const uint32_t *words = (volatile const uint32_t *)r;
    uint32_t count = (uint32_t)(offsetof(struct stage82_xnu_arm_vm_init_live_pmap_result, checksum) / sizeof(uint32_t));
    uint32_t chk = 0;
    for (uint32_t i = 0; i < count; i++) {
        chk ^= words[i];
    }
    return chk;
}

static void stage82_xnu_arm_vm_init_live_pmap_log(
    volatile const struct stage82_xnu_arm_vm_init_live_pmap_result *r)
{
    xnu_log_puts("stage82_xnu_arm_vm_init_live_pmap result:\n");
    xnu_log_kv32("stage82_xnu_arm_vm_init_live_pmap_version", r->version);
    xnu_log_kv32("stage82_xnu_arm_vm_init_live_pmap_size", r->size);
    xnu_log_kv32("stage82_xnu_arm_vm_init_live_pmap_status", r->status);
    xnu_log_kv32("stage82_xnu_arm_vm_init_live_pmap_required_mask", r->required_mask);
    xnu_log_kv32("stage82_xnu_arm_vm_init_live_pmap_satisfied_mask", r->satisfied_mask);
    xnu_log_kv32("stage82_xnu_arm_vm_init_live_pmap_failure_mask", r->failure_mask);
    xnu_log_kv32("stage82_xnu_arm_vm_init_live_pmap_source_post_pe_status", r->source_post_pe_status);
    xnu_log_kv32("stage82_xnu_arm_vm_init_live_pmap_source_post_pe_checksum", r->source_post_pe_checksum);
    xnu_log_kv32("stage82_xnu_arm_vm_init_live_pmap_boot_args_ptr", r->boot_args_ptr);
    xnu_log_kv32("stage82_xnu_arm_vm_init_live_pmap_boot_args_valid", r->boot_args_valid);
    xnu_log_kv32("stage82_xnu_arm_vm_init_live_pmap_memory_size", r->memory_size);
    xnu_log_kv32("stage82_xnu_arm_vm_init_live_pmap_phys_base", r->phys_base);
    xnu_log_kv32("stage82_xnu_arm_vm_init_live_pmap_candidate_l1_base", r->candidate_l1_base);
    xnu_log_kv32("stage82_xnu_arm_vm_init_live_pmap_candidate_l1_checksum", r->candidate_l1_checksum);
    xnu_log_kv32("stage82_xnu_arm_vm_init_live_pmap_original_ttbr0", r->original_ttbr0);
    xnu_log_kv32("stage82_xnu_arm_vm_init_live_pmap_original_ttbcr", r->original_ttbcr);
    xnu_log_kv32("stage82_xnu_arm_vm_init_live_pmap_original_dacr", r->original_dacr);
    xnu_log_kv32("stage82_xnu_arm_vm_init_live_pmap_original_sctlr", r->original_sctlr);
    xnu_log_kv32("stage82_xnu_arm_vm_init_live_pmap_live_ttbr0", r->live_ttbr0);
    xnu_log_kv32("stage82_xnu_arm_vm_init_live_pmap_live_ttbcr", r->live_ttbcr);
    xnu_log_kv32("stage82_xnu_arm_vm_init_live_pmap_live_dacr", r->live_dacr);
    xnu_log_kv32("stage82_xnu_arm_vm_init_live_pmap_live_sctlr", r->live_sctlr);
    xnu_log_kv32("stage82_xnu_arm_vm_init_live_pmap_restored_ttbr0", r->restored_ttbr0);
    xnu_log_kv32("stage82_xnu_arm_vm_init_live_pmap_restored_ttbcr", r->restored_ttbcr);
    xnu_log_kv32("stage82_xnu_arm_vm_init_live_pmap_restored_dacr", r->restored_dacr);
    xnu_log_kv32("stage82_xnu_arm_vm_init_live_pmap_restored_sctlr", r->restored_sctlr);
    xnu_log_kv32("stage82_xnu_arm_vm_init_live_pmap_ttbr0_write_count", r->ttbr0_write_count);
    xnu_log_kv32("stage82_xnu_arm_vm_init_live_pmap_tlb_invalidate_count", r->tlb_invalidate_count);
    xnu_log_kv32("stage82_xnu_arm_vm_init_live_pmap_live_pmap_installed", r->live_pmap_installed);
    xnu_log_kv32("stage82_xnu_arm_vm_init_live_pmap_live_pmap_verified", r->live_pmap_verified);
    xnu_log_kv32("stage82_xnu_arm_vm_init_live_pmap_live_pmap_restored", r->live_pmap_restored);
    xnu_log_kv32("stage82_xnu_arm_vm_init_live_pmap_high_alias_verified", r->high_alias_verified);
    xnu_log_kv32("stage82_xnu_arm_vm_init_live_pmap_ram_console_verified", r->ram_console_verified);
    xnu_log_kv32("stage82_xnu_arm_vm_init_live_pmap_gic_verified", r->gic_verified);
    xnu_log_kv32("stage82_xnu_arm_vm_init_live_pmap_public_arm_vm_init_executed", r->public_arm_vm_init_executed);
    xnu_log_kv32("stage82_xnu_arm_vm_init_live_pmap_public_pmap_runtime_executed", r->public_pmap_runtime_executed);
    xnu_log_kv32("stage82_xnu_arm_vm_init_live_pmap_persistent_write_attempted", r->persistent_write_attempted);
    xnu_log_kv32("stage82_xnu_arm_vm_init_live_pmap_safety_boundary_preserved", r->safety_boundary_preserved);
    xnu_log_kv32("stage82_xnu_arm_vm_init_live_pmap_checksum", r->checksum);
}

static inline uint32_t read_ttbr0(void)
{
    uint32_t v;
    __asm__ volatile("mrc p15, 0, %0, c2, c0, 0" : "=r"(v));
    return v;
}

static inline uint32_t read_ttbcr(void)
{
    uint32_t v;
    __asm__ volatile("mrc p15, 0, %0, c2, c0, 2" : "=r"(v));
    return v;
}

static inline uint32_t read_dacr(void)
{
    uint32_t v;
    __asm__ volatile("mrc p15, 0, %0, c3, c0, 0" : "=r"(v));
    return v;
}

static inline uint32_t read_sctlr(void)
{
    uint32_t v;
    __asm__ volatile("mrc p15, 0, %0, c1, c0, 0" : "=r"(v));
    return v;
}

static inline void write_ttbr0(uint32_t v)
{
    __asm__ volatile("mcr p15, 0, %0, c2, c0, 0" : : "r"(v));
}

static inline void invalidate_tlbs(void)
{
    __asm__ volatile("mcr p15, 0, %0, c8, c7, 0" : : "r"(0));
}

static inline void dsb_isb(void)
{
    __asm__ volatile("dsb\nisb" : : : "memory");
}

static uint32_t candidate_l1_checksum(const uint32_t *l1, uint32_t count)
{
    uint32_t chk = 0;
    for (uint32_t i = 0; i < count; i++) {
        chk ^= l1[i];
    }
    return chk;
}

#define L1_SECTION_SIZE        0x00100000u
#define L1_SECTION_MASK        0xfff00000u
#define L1_DESC_SECTION_SO     0x00010c02u
#define SECTION_INDEX(va)      (((va) >> 20) & 0xfffu)

static void map_candidate_section(uint32_t *l1, uint32_t va, uint32_t pa)
{
    l1[SECTION_INDEX(va)] = (pa & L1_SECTION_MASK) | L1_DESC_SECTION_SO;
}

int stage82_xnu_arm_vm_init_live_pmap_run(
    struct boot_args *args,
    struct stage82_xnu_entry_stub_result *entry_result)
{
    struct stage82_xnu_arm_vm_init_live_pmap_result *r = &g_result;
    const struct stage82_xnu_arm_init_post_pe_bootstrap_result *post_pe;
    uint32_t candidate_l1_base;
    uint32_t switched;
    volatile uint32_t *identity_probe;
    volatile uint32_t *alias_probe;
    volatile uint32_t *alias_ram_console;
    volatile uint32_t *identity_ram_console;
    volatile uint32_t *alias_gicd_ctlr;
    volatile uint32_t *identity_gicd_ctlr;

    (void)entry_result;
    memset(r, 0, sizeof(*r));
    r->version = STAGE82_XNU_ARM_VM_INIT_LIVE_PMAP_VERSION;
    r->size = sizeof(*r);
    r->status = STAGE82_STATUS_BASE;
    r->required_mask = STAGE82_XNU_ARM_VM_INIT_LIVE_PMAP_REQUIRED_MASK;
    switched = 0;

    post_pe = stage82_xnu_arm_init_post_pe_bootstrap_result();
    if (!post_pe || post_pe->status != STAGE82_STATUS_OK) {
        r->failure_mask |= STAGE82_XNU_ARM_VM_INIT_LIVE_PMAP_FAIL_SOURCE_POST_PE;
        goto finish;
    }
    r->source_post_pe_status = post_pe->status;
    r->source_post_pe_checksum = post_pe->checksum;
    r->satisfied_mask |= STAGE82_XNU_ARM_VM_INIT_LIVE_PMAP_SAT_SOURCE_POST_PE_OK;

    if (!args || args->Revision != BOOT_ARGS_REVISION ||
        args->Version != BOOT_ARGS_VERSION ||
        args->physBase != STAGE82_BASE ||
        args->machineType != MACHINE_TYPE_MSM8974 ||
        args->memSize != (RAM_CONSOLE_BASE - RAM_PHYS_BASE)) {
        r->failure_mask |= STAGE82_XNU_ARM_VM_INIT_LIVE_PMAP_FAIL_BOOT_ARGS;
        goto finish;
    }
    r->boot_args_ptr = (uint32_t)(uintptr_t)args;
    r->boot_args_valid = 1;
    r->memory_size = args->memSize;
    r->phys_base = args->physBase;
    r->satisfied_mask |= STAGE82_XNU_ARM_VM_INIT_LIVE_PMAP_SAT_BOOT_ARGS_VALID;

    candidate_l1_base = (uint32_t)(uintptr_t)stage82_live_pmap_candidate_l1;
    if ((candidate_l1_base & 0x3fff) != 0) {
        r->failure_mask |= STAGE82_XNU_ARM_VM_INIT_LIVE_PMAP_FAIL_CANDIDATE_L1_ALIGN;
        goto finish;
    }
    r->candidate_l1_base = candidate_l1_base;
    r->satisfied_mask |= STAGE82_XNU_ARM_VM_INIT_LIVE_PMAP_SAT_CANDIDATE_L1_VALID;

    memset(stage82_live_pmap_candidate_l1, 0, sizeof(stage82_live_pmap_candidate_l1));
    map_candidate_section(stage82_live_pmap_candidate_l1, 0x00000000u, 0x00000000u);
    map_candidate_section(stage82_live_pmap_candidate_l1, STAGE82_HIGH_ALIAS_BASE, 0x00000000u);
    map_candidate_section(stage82_live_pmap_candidate_l1, STAGE82_RAM_CONSOLE_ALIAS_BASE, RAM_CONSOLE_BASE);
    map_candidate_section(stage82_live_pmap_candidate_l1, STAGE82_GIC_ALIAS_BASE, 0xf9000000u);
    map_candidate_section(stage82_live_pmap_candidate_l1, 0x0fa00000u, 0x0fa00000u);
    map_candidate_section(stage82_live_pmap_candidate_l1, RAM_CONSOLE_BASE, RAM_CONSOLE_BASE);
    map_candidate_section(stage82_live_pmap_candidate_l1, RAM_CONSOLE_BASE + L1_SECTION_SIZE, RAM_CONSOLE_BASE + L1_SECTION_SIZE);
    map_candidate_section(stage82_live_pmap_candidate_l1, 0xf9000000u, 0xf9000000u);
    map_candidate_section(stage82_live_pmap_candidate_l1, 0xfc400000u, 0xfc400000u);
    map_candidate_section(stage82_live_pmap_candidate_l1, candidate_l1_base, candidate_l1_base);
    r->candidate_l1_checksum = candidate_l1_checksum(stage82_live_pmap_candidate_l1, STAGE82_XNU_TTE_L1_ENTRY_COUNT);
    r->satisfied_mask |= STAGE82_XNU_ARM_VM_INIT_LIVE_PMAP_SAT_CANDIDATE_L1_POPULATED;

    r->original_ttbr0 = read_ttbr0();
    r->original_ttbcr = read_ttbcr();
    r->original_dacr = read_dacr();
    r->original_sctlr = read_sctlr();
    if ((r->original_sctlr & 1u) == 0u) {
        r->failure_mask |= STAGE82_XNU_ARM_VM_INIT_LIVE_PMAP_FAIL_MMU_DISABLED;
        goto finish;
    }
    r->satisfied_mask |= STAGE82_XNU_ARM_VM_INIT_LIVE_PMAP_SAT_CONTROL_REGS_SAVED;

    identity_probe = (volatile uint32_t *)(uintptr_t)&stage82_live_pmap_probe_word;
    alias_probe = (volatile uint32_t *)(uintptr_t)(STAGE82_HIGH_ALIAS_BASE +
        (uint32_t)(uintptr_t)&stage82_live_pmap_probe_word);
    identity_ram_console = (volatile uint32_t *)(uintptr_t)RAM_CONSOLE_BASE;
    alias_ram_console = (volatile uint32_t *)(uintptr_t)STAGE82_RAM_CONSOLE_ALIAS_BASE;
    identity_gicd_ctlr = (volatile uint32_t *)(uintptr_t)PE_state_stage82.gicDistributorBase;
    alias_gicd_ctlr = (volatile uint32_t *)(uintptr_t)(STAGE82_GIC_ALIAS_BASE + (PE_state_stage82.gicDistributorBase - 0xf9000000u));

    dsb_isb();
    write_ttbr0(candidate_l1_base);
    r->ttbr0_write_count++;
    switched = 1;
    invalidate_tlbs();
    r->tlb_invalidate_count++;
    dsb_isb();

    r->live_ttbr0 = read_ttbr0();
    r->live_ttbcr = read_ttbcr();
    r->live_dacr = read_dacr();
    r->live_sctlr = read_sctlr();
    r->live_pmap_installed = 1;
    r->satisfied_mask |= STAGE82_XNU_ARM_VM_INIT_LIVE_PMAP_SAT_LIVE_PMAP_INSTALLED;

    if ((r->live_ttbr0 & 0xffffc000u) != candidate_l1_base) {
        r->failure_mask |= STAGE82_XNU_ARM_VM_INIT_LIVE_PMAP_FAIL_TTBR0_MISMATCH;
    }
    if (r->live_ttbcr != r->original_ttbcr || r->live_dacr != r->original_dacr) {
        r->failure_mask |= STAGE82_XNU_ARM_VM_INIT_LIVE_PMAP_FAIL_CONTROL_REG_CHANGED;
    }
    if (r->failure_mask == 0) {
        r->satisfied_mask |= STAGE82_XNU_ARM_VM_INIT_LIVE_PMAP_SAT_LIVE_PMAP_VERIFIED;
        r->live_pmap_verified = 1;
    }

    *identity_probe = 0x11223344u;
    if (*alias_probe == 0x11223344u) {
        r->high_alias_verified = 1;
        r->satisfied_mask |= STAGE82_XNU_ARM_VM_INIT_LIVE_PMAP_SAT_HIGH_ALIAS_VERIFIED;
    } else {
        r->failure_mask |= STAGE82_XNU_ARM_VM_INIT_LIVE_PMAP_FAIL_HIGH_ALIAS;
    }

    if (*alias_ram_console == RAM_CONSOLE_SIG && *alias_ram_console == *identity_ram_console) {
        r->ram_console_verified = 1;
        r->satisfied_mask |= STAGE82_XNU_ARM_VM_INIT_LIVE_PMAP_SAT_RAM_CONSOLE_VERIFIED;
    } else {
        r->failure_mask |= STAGE82_XNU_ARM_VM_INIT_LIVE_PMAP_FAIL_RAM_CONSOLE;
    }

    if ((*alias_gicd_ctlr & 1u) != 0u && *alias_gicd_ctlr == *identity_gicd_ctlr) {
        r->gic_verified = 1;
        r->satisfied_mask |= STAGE82_XNU_ARM_VM_INIT_LIVE_PMAP_SAT_GIC_VERIFIED;
    } else {
        r->failure_mask |= STAGE82_XNU_ARM_VM_INIT_LIVE_PMAP_FAIL_GIC;
    }

    dsb_isb();
    write_ttbr0(r->original_ttbr0);
    r->ttbr0_write_count++;
    invalidate_tlbs();
    r->tlb_invalidate_count++;
    dsb_isb();
    switched = 0;

    r->restored_ttbr0 = read_ttbr0();
    r->restored_ttbcr = read_ttbcr();
    r->restored_dacr = read_dacr();
    r->restored_sctlr = read_sctlr();
    r->live_pmap_restored = 1;
    r->satisfied_mask |= STAGE82_XNU_ARM_VM_INIT_LIVE_PMAP_SAT_LIVE_PMAP_RESTORED;

    if ((r->restored_ttbr0 & 0xffffc000u) == (r->original_ttbr0 & 0xffffc000u) &&
        r->restored_ttbcr == r->original_ttbcr &&
        r->restored_dacr == r->original_dacr &&
        r->restored_sctlr == r->original_sctlr) {
        r->satisfied_mask |= STAGE82_XNU_ARM_VM_INIT_LIVE_PMAP_SAT_ORIGINAL_RESTORED;
    } else {
        r->failure_mask |= STAGE82_XNU_ARM_VM_INIT_LIVE_PMAP_FAIL_RESTORE;
    }

finish:
    if (switched) {
        dsb_isb();
        write_ttbr0(r->original_ttbr0);
        r->ttbr0_write_count++;
        invalidate_tlbs();
        r->tlb_invalidate_count++;
        dsb_isb();
        r->restored_ttbr0 = read_ttbr0();
        r->restored_ttbcr = read_ttbcr();
        r->restored_dacr = read_dacr();
        r->restored_sctlr = read_sctlr();
        r->live_pmap_restored = 1;
    }

    r->public_arm_vm_init_executed = 0;
    r->public_pmap_runtime_executed = 0;
    r->persistent_write_attempted = 0;
    r->satisfied_mask |= STAGE82_XNU_ARM_VM_INIT_LIVE_PMAP_SAT_NO_PUBLIC_ARM_VM_INIT |
                         STAGE82_XNU_ARM_VM_INIT_LIVE_PMAP_SAT_NO_PUBLIC_PMAP_RUNTIME |
                         STAGE82_XNU_ARM_VM_INIT_LIVE_PMAP_SAT_NO_PERSISTENT_WRITE;

    if ((r->satisfied_mask | STAGE82_XNU_ARM_VM_INIT_LIVE_PMAP_SAT_SAFETY_BOUNDARY) ==
            r->required_mask &&
        r->failure_mask == 0) {
        r->safety_boundary_preserved = 1;
        r->satisfied_mask |= STAGE82_XNU_ARM_VM_INIT_LIVE_PMAP_SAT_SAFETY_BOUNDARY;
    } else {
        r->failure_mask |= STAGE82_XNU_ARM_VM_INIT_LIVE_PMAP_FAIL_SAFETY_BOUNDARY;
    }

    r->status = (r->satisfied_mask == r->required_mask && r->failure_mask == 0) ?
                STAGE82_STATUS_OK : STAGE82_STATUS_FAIL(r->failure_mask);
    r->checksum = stage82_xnu_arm_vm_init_live_pmap_checksum(r);

    stage82_xnu_arm_vm_init_live_pmap_log(r);
    return (r->status == STAGE82_STATUS_OK) ? 1 : 0;
}

const struct stage82_xnu_arm_vm_init_live_pmap_result *
stage82_xnu_arm_vm_init_live_pmap_result(void)
{
    return &g_result;
}
