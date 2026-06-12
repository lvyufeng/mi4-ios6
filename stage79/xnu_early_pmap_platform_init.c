#include "stage79.h"

/*
 * Stage79 Stage-owned early pmap/platform-init micro-sequence.
 *
 * This is deliberately still not public XNU pmap, pexpert, IOKit, _start, or
 * arm_init runtime.  The sequence is invoked from the Stage-owned arm-init stub,
 * samples read-only ARM control state, imports the already completed Stage79
 * pmap-transition dry-run and PE_state/platform facts, records a result ABI, and
 * returns without installing tables or mutating control registers/TLB/cache state.
 */

static struct stage79_xnu_early_pmap_platform_init_result g_stage79_xnu_early_init_result;

static inline uint32_t stage79_early_init_read_ttbr0(void)
{
    uint32_t v;
    __asm__ volatile ("mrc p15, 0, %0, c2, c0, 0" : "=r"(v));
    return v;
}

static inline uint32_t stage79_early_init_read_ttbcr(void)
{
    uint32_t v;
    __asm__ volatile ("mrc p15, 0, %0, c2, c0, 2" : "=r"(v));
    return v;
}

static inline uint32_t stage79_early_init_read_dacr(void)
{
    uint32_t v;
    __asm__ volatile ("mrc p15, 0, %0, c3, c0, 0" : "=r"(v));
    return v;
}

static inline uint32_t stage79_early_init_read_sctlr(void)
{
    uint32_t v;
    __asm__ volatile ("mrc p15, 0, %0, c1, c0, 0" : "=r"(v));
    return v;
}

static uint32_t stage79_early_init_checksum(
    const struct stage79_xnu_early_pmap_platform_init_result *r)
{
    const uint32_t *words = (const uint32_t *)r;
    uint32_t checksum = 0u;
    uint32_t count = (uint32_t)(offsetof(struct stage79_xnu_early_pmap_platform_init_result, checksum) /
                                sizeof(uint32_t));
    uint32_t i;

    for (i = 0u; i < count; i++) {
        checksum ^= words[i];
    }

    return checksum;
}

static void stage79_early_init_log(const struct stage79_xnu_early_pmap_platform_init_result *r)
{
    xnu_log_kv32("stage79_xnu_early_init_status", r->status);
    xnu_log_kv32("stage79_xnu_early_init_required_mask", r->required_mask);
    xnu_log_kv32("stage79_xnu_early_init_satisfied_mask", r->satisfied_mask);
    xnu_log_kv32("stage79_xnu_early_init_failure_mask", r->failure_mask);
    xnu_log_kv32("stage79_xnu_early_init_checksum", r->checksum);
    xnu_log_kv32("stage79_xnu_early_init_boot_args_ptr", r->boot_args_ptr);
    xnu_log_kv32("stage79_xnu_early_init_boot_args_rev_ver", r->boot_args_rev_ver);
    xnu_log_kv32("stage79_xnu_early_init_boot_args_valid", r->boot_args_valid);
    xnu_log_kv32("stage79_xnu_early_init_device_tree_valid", r->device_tree_valid);
    xnu_log_kv32("stage79_xnu_early_init_source_entry_status", r->source_entry_status);
    xnu_log_kv32("stage79_xnu_early_init_source_pmap_transition_status", r->source_pmap_transition_status);
    xnu_log_kv32("stage79_xnu_early_init_source_pmap_transition_satisfied_mask", r->source_pmap_transition_satisfied_mask);
    xnu_log_kv32("stage79_xnu_early_init_source_pmap_transition_failure_mask", r->source_pmap_transition_failure_mask);
    xnu_log_kv32("stage79_xnu_early_init_source_pmap_transition_checksum", r->source_pmap_transition_checksum);
    xnu_log_kv32("stage79_xnu_early_init_source_pe_state_valid", r->source_pe_state_valid);
    xnu_log_kv32("stage79_xnu_early_init_ttbr0_before", r->ttbr0_before);
    xnu_log_kv32("stage79_xnu_early_init_ttbr0_after", r->ttbr0_after);
    xnu_log_kv32("stage79_xnu_early_init_ttbcr_before", r->ttbcr_before);
    xnu_log_kv32("stage79_xnu_early_init_ttbcr_after", r->ttbcr_after);
    xnu_log_kv32("stage79_xnu_early_init_dacr_before", r->dacr_before);
    xnu_log_kv32("stage79_xnu_early_init_dacr_after", r->dacr_after);
    xnu_log_kv32("stage79_xnu_early_init_sctlr_before", r->sctlr_before);
    xnu_log_kv32("stage79_xnu_early_init_sctlr_after", r->sctlr_after);
    xnu_log_kv32("stage79_xnu_early_init_control_registers_sampled", r->control_registers_sampled);
    xnu_log_kv32("stage79_xnu_early_init_control_registers_unchanged", r->control_registers_unchanged);
    xnu_log_kv32("stage79_xnu_early_init_candidate_l1_base", r->candidate_l1_base);
    xnu_log_kv32("stage79_xnu_early_init_candidate_l1_limit", r->candidate_l1_limit);
    xnu_log_kv32("stage79_xnu_early_init_candidate_l1_alignment", r->candidate_l1_alignment);
    xnu_log_kv32("stage79_xnu_early_init_proposed_ttbr0", r->proposed_ttbr0);
    xnu_log_kv32("stage79_xnu_early_init_proposed_ttbcr", r->proposed_ttbcr);
    xnu_log_kv32("stage79_xnu_early_init_proposed_dacr", r->proposed_dacr);
    xnu_log_kv32("stage79_xnu_early_init_proposed_sctlr", r->proposed_sctlr);
    xnu_log_kv32("stage79_xnu_early_init_proposed_plan_readonly", r->proposed_plan_readonly);
    xnu_log_kv32("stage79_xnu_early_init_translation_kernel_pa", r->translation_kernel_pa);
    xnu_log_kv32("stage79_xnu_early_init_translation_workspace_pa", r->translation_workspace_pa);
    xnu_log_kv32("stage79_xnu_early_init_translation_ram_console_pa", r->translation_ram_console_pa);
    xnu_log_kv32("stage79_xnu_early_init_translation_device_pa", r->translation_device_pa);
    xnu_log_kv32("stage79_xnu_early_init_translation_case_count", r->translation_case_count);
    xnu_log_kv32("stage79_xnu_early_init_translation_plan_reused", r->translation_plan_reused);
    xnu_log_kv32("stage79_xnu_early_init_recovery_continuity_inherited", r->recovery_continuity_inherited);
    xnu_log_kv32("stage79_xnu_early_init_platform_memory_base", r->platform_memory_base);
    xnu_log_kv32("stage79_xnu_early_init_platform_memory_size", r->platform_memory_size);
    xnu_log_kv32("stage79_xnu_early_init_platform_cpu_count", r->platform_cpu_count);
    xnu_log_kv32("stage79_xnu_early_init_platform_gic_dist_base", r->platform_gic_dist_base);
    xnu_log_kv32("stage79_xnu_early_init_platform_gic_cpu_base", r->platform_gic_cpu_base);
    xnu_log_kv32("stage79_xnu_early_init_platform_timer_base", r->platform_timer_base);
    xnu_log_kv32("stage79_xnu_early_init_platform_timer_frequency", r->platform_timer_frequency);
    xnu_log_kv32("stage79_xnu_early_init_platform_machine_type", r->platform_machine_type);
    xnu_log_kv32("stage79_xnu_early_init_platform_vector_base", r->platform_vector_base);
    xnu_log_kv32("stage79_xnu_early_init_platform_facts_valid", r->platform_facts_valid);
    xnu_log_kv32("stage79_xnu_early_init_stage_owned_local_only", r->stage_owned_local_only);
    xnu_log_kv32("stage79_xnu_early_init_public_xnu_start_executed", r->public_xnu_start_executed);
    xnu_log_kv32("stage79_xnu_early_init_public_arm_init_executed", r->public_arm_init_executed);
    xnu_log_kv32("stage79_xnu_early_init_public_pmap_runtime_executed", r->public_pmap_runtime_executed);
    xnu_log_kv32("stage79_xnu_early_init_public_pexpert_runtime_executed", r->public_pexpert_runtime_executed);
    xnu_log_kv32("stage79_xnu_early_init_public_iokit_runtime_executed", r->public_iokit_runtime_executed);
    xnu_log_kv32("stage79_xnu_early_init_generated_macho_executed", r->generated_macho_executed);
    xnu_log_kv32("stage79_xnu_early_init_live_pmap_installed", r->live_pmap_installed);
    xnu_log_kv32("stage79_xnu_early_init_ttbr_written", r->ttbr_written);
    xnu_log_kv32("stage79_xnu_early_init_ttbcr_written", r->ttbcr_written);
    xnu_log_kv32("stage79_xnu_early_init_dacr_written", r->dacr_written);
    xnu_log_kv32("stage79_xnu_early_init_sctlr_written", r->sctlr_written);
    xnu_log_kv32("stage79_xnu_early_init_tlb_invalidated", r->tlb_invalidated);
    xnu_log_kv32("stage79_xnu_early_init_cache_policy_changed", r->cache_policy_changed);
    xnu_log_kv32("stage79_xnu_early_init_persistent_write_attempted", r->persistent_write_attempted);
    xnu_log_kv32("stage79_xnu_early_init_safety_boundary_preserved", r->safety_boundary_preserved);
}

int stage79_xnu_early_pmap_platform_init_run(
    struct boot_args *args,
    struct stage79_xnu_entry_stub_result *entry_result)
{
    struct stage79_xnu_early_pmap_platform_init_result *r = &g_stage79_xnu_early_init_result;
    const struct stage79_xnu_pmap_transition_dryrun_contract *pmap;
    uint32_t proposed_plan_bits;
    uint32_t valid_args = 0u;
    uint32_t valid_dt = 0u;
    uint32_t entry_active = 0u;
    uint32_t pmap_ok;
    uint32_t platform_source_ok;
    uint32_t platform_facts_ok;
    uint32_t public_boundary_ok;
    uint32_t mutation_boundary_ok;

    xnu_log_puts("stage79_xnu_early_init: entered Stage-owned early pmap/platform-init path\n");
    memset(r, 0, sizeof(*r));

    r->version = STAGE79_XNU_EARLY_INIT_VERSION;
    r->size = sizeof(*r);
    r->status = STAGE79_STATUS_BASE;
    r->required_mask = STAGE79_XNU_EARLY_INIT_REQUIRED_MASK;
    r->boot_args_ptr = (uint32_t)(uintptr_t)args;

    r->ttbr0_before = stage79_early_init_read_ttbr0();
    r->ttbcr_before = stage79_early_init_read_ttbcr();
    r->dacr_before = stage79_early_init_read_dacr();
    r->sctlr_before = stage79_early_init_read_sctlr();
    r->control_registers_sampled = 1u;

    if (args) {
        r->boot_args_rev_ver = ((uint32_t)args->Version << 16) | args->Revision;
        valid_args = (args->Revision == BOOT_ARGS_REVISION &&
                      args->Version == BOOT_ARGS_VERSION &&
                      args->physBase == STAGE79_BASE &&
                      args->machineType == MACHINE_TYPE_MSM8974) ? 1u : 0u;
        valid_dt = (args->deviceTreeP != NULL && args->deviceTreeLength != 0u) ? 1u : 0u;
    }
    r->boot_args_valid = valid_args;
    r->device_tree_valid = valid_dt;

    if (valid_args == 1u) {
        r->satisfied_mask |= STAGE79_XNU_EARLY_INIT_SAT_BOOT_ARGS_VALID;
    } else {
        r->failure_mask |= STAGE79_XNU_EARLY_INIT_FAIL_BOOT_ARGS;
    }
    if (valid_dt == 1u) {
        r->satisfied_mask |= STAGE79_XNU_EARLY_INIT_SAT_DEVICE_TREE_VALID;
    } else {
        r->failure_mask |= STAGE79_XNU_EARLY_INIT_FAIL_DEVICE_TREE;
    }

    if (entry_result) {
        entry_active = (entry_result->start_entered == 1u &&
                        entry_result->arm_init_called == 1u &&
                        entry_result->boot_args_valid == 1u &&
                        entry_result->device_tree_valid == 1u) ? 1u : 0u;
    }
    r->source_entry_status = (entry_active == 1u) ? STAGE79_STATUS_OK : STAGE79_STATUS_BASE;
    if (entry_active == 1u) {
        r->satisfied_mask |= STAGE79_XNU_EARLY_INIT_SAT_SOURCE_ENTRY_ACTIVE;
    } else {
        r->failure_mask |= STAGE79_XNU_EARLY_INIT_FAIL_SOURCE_ENTRY;
    }

    pmap = stage79_xnu_pmap_transition_dryrun_contract_result();
    r->source_pmap_transition_status = pmap->status;
    r->source_pmap_transition_satisfied_mask = pmap->satisfied_mask;
    r->source_pmap_transition_failure_mask = pmap->failure_mask;
    r->source_pmap_transition_checksum = pmap->checksum;
    r->candidate_l1_base = pmap->candidate_l1_base;
    r->candidate_l1_limit = pmap->candidate_l1_limit;
    r->candidate_l1_alignment = pmap->candidate_l1_alignment;
    r->proposed_ttbr0 = pmap->proposed_ttbr0;
    r->proposed_ttbcr = pmap->proposed_ttbcr;
    r->proposed_dacr = pmap->proposed_dacr;
    r->proposed_sctlr = pmap->proposed_sctlr;
    r->proposed_plan_readonly = pmap->control_register_plan_readonly;
    r->translation_kernel_pa = pmap->translation_kernel_pa;
    r->translation_workspace_pa = pmap->translation_workspace_pa;
    r->translation_ram_console_pa = pmap->translation_ram_console_pa;
    r->translation_device_pa = pmap->translation_device_pa;
    r->translation_case_count = pmap->translation_case_count;

    pmap_ok = (pmap->status == STAGE79_STATUS_OK &&
               pmap->satisfied_mask == STAGE79_XNU_PMAP_TRANSITION_DRYRUN_REQUIRED_MASK &&
               pmap->failure_mask == 0u) ? 1u : 0u;
    if (pmap_ok == 1u) {
        r->satisfied_mask |= STAGE79_XNU_EARLY_INIT_SAT_SOURCE_PMAP_TRANSITION;
    } else {
        r->failure_mask |= STAGE79_XNU_EARLY_INIT_FAIL_PMAP_TRANSITION;
    }

    if (pmap_ok == 1u && r->candidate_l1_base != 0u &&
        r->candidate_l1_limit > r->candidate_l1_base &&
        pmap->candidate_l1_bytes == STAGE79_XNU_PMAP_TRANSITION_DRYRUN_L1_BYTES &&
        r->candidate_l1_alignment == 0u && pmap->candidate_not_installed == 1u) {
        r->satisfied_mask |= STAGE79_XNU_EARLY_INIT_SAT_CANDIDATE_L1_IMPORTED;
    } else {
        r->failure_mask |= STAGE79_XNU_EARLY_INIT_FAIL_CANDIDATE_L1;
    }

    proposed_plan_bits = STAGE79_XNU_PMAP_TRANSITION_DRYRUN_SAT_PROPOSED_TTBR0_PLAN |
        STAGE79_XNU_PMAP_TRANSITION_DRYRUN_SAT_PROPOSED_TTBCR_PLAN |
        STAGE79_XNU_PMAP_TRANSITION_DRYRUN_SAT_PROPOSED_DACR_PLAN |
        STAGE79_XNU_PMAP_TRANSITION_DRYRUN_SAT_PROPOSED_SCTLR_PLAN;
    if (pmap_ok == 1u && (pmap->satisfied_mask & proposed_plan_bits) == proposed_plan_bits &&
        r->proposed_plan_readonly == 1u) {
        r->satisfied_mask |= STAGE79_XNU_EARLY_INIT_SAT_PROPOSED_REG_PLAN;
    } else {
        r->failure_mask |= STAGE79_XNU_EARLY_INIT_FAIL_PROPOSED_REG_PLAN;
    }

    if (pmap_ok == 1u && r->translation_case_count == 4u &&
        pmap->translation_kernel_pa == pmap->translation_kernel_expected_pa &&
        pmap->translation_workspace_pa == pmap->translation_workspace_expected_pa &&
        pmap->translation_ram_console_pa == pmap->translation_ram_console_expected_pa &&
        pmap->translation_device_pa == pmap->translation_device_expected_pa) {
        r->translation_plan_reused = 1u;
        r->satisfied_mask |= STAGE79_XNU_EARLY_INIT_SAT_TRANSLATION_CONTINUITY;
    } else {
        r->failure_mask |= STAGE79_XNU_EARLY_INIT_FAIL_TRANSLATION;
    }

    if (pmap_ok == 1u && pmap->recovery_l1_distinct == 1u &&
        pmap->candidate_workspace_distinct == 1u && pmap->candidate_not_installed == 1u &&
        (pmap->satisfied_mask & STAGE79_XNU_PMAP_TRANSITION_DRYRUN_SAT_RECOVERY_CONTINUITY) != 0u) {
        r->recovery_continuity_inherited = 1u;
        r->satisfied_mask |= STAGE79_XNU_EARLY_INIT_SAT_RECOVERY_CONTINUITY;
    } else {
        r->failure_mask |= STAGE79_XNU_EARLY_INIT_FAIL_RECOVERY;
    }

    pe_state_init_from_boot_args(args);
    platform_source_ok = (pe_state_validate() != 0) ? 1u : 0u;
    r->source_pe_state_valid = platform_source_ok;
    r->platform_memory_base = PE_state_stage79.memoryBase;
    r->platform_memory_size = PE_state_stage79.memorySize;
    r->platform_cpu_count = PE_state_stage79.cpuCount;
    r->platform_gic_dist_base = PE_state_stage79.gicDistributorBase;
    r->platform_gic_cpu_base = PE_state_stage79.gicCpuBase;
    r->platform_timer_base = PE_state_stage79.timerBase;
    r->platform_timer_frequency = PE_state_stage79.timerFrequency;
    r->platform_machine_type = PE_state_stage79.machineType;
    r->platform_vector_base = PE_state_stage79.vectorBase;

    if (platform_source_ok == 1u) {
        r->satisfied_mask |= STAGE79_XNU_EARLY_INIT_SAT_SOURCE_PLATFORM_FACTS |
            STAGE79_XNU_EARLY_INIT_SAT_PE_STATE_VALIDATED;
    } else {
        r->failure_mask |= STAGE79_XNU_EARLY_INIT_FAIL_PLATFORM_FACTS |
            STAGE79_XNU_EARLY_INIT_FAIL_PE_STATE;
    }

    platform_facts_ok = (platform_source_ok == 1u &&
                         PE_state_stage79.bootArgs == args &&
                         PE_state_stage79.deviceTreeHead == (args ? args->deviceTreeP : NULL) &&
                         PE_state_stage79.memoryBase == RAM_PHYS_BASE &&
                         PE_state_stage79.memorySize == (RAM_CONSOLE_BASE - RAM_PHYS_BASE) &&
                         PE_state_stage79.cpuCount == 4u &&
                         PE_state_stage79.gicDistributorBase == 0xf9000000u &&
                         PE_state_stage79.gicCpuBase == 0xf9002000u &&
                         PE_state_stage79.timerBase == 0xf9020000u &&
                         PE_state_stage79.timerFrequency == 19200000u &&
                         PE_state_stage79.machineType == MACHINE_TYPE_MSM8974 &&
                         PE_state_stage79.vectorBase != 0u) ? 1u : 0u;
    r->platform_facts_valid = platform_facts_ok;
    if (platform_facts_ok == 1u) {
        r->satisfied_mask |= STAGE79_XNU_EARLY_INIT_SAT_PLATFORM_FACTS_MATCHED;
    } else {
        r->failure_mask |= STAGE79_XNU_EARLY_INIT_FAIL_PLATFORM_FACTS;
    }

    r->stage_owned_local_only = 1u;
    r->public_xnu_start_executed = (entry_result ? entry_result->public_xnu_start_executed : 0u) |
        pmap->xnu_start_executed;
    r->public_arm_init_executed = entry_result ? entry_result->public_arm_init_executed : 0u;
    r->public_pmap_runtime_executed = pmap->public_pmap_execute_count |
        pmap->public_arm_vm_init_executed | pmap->public_pmap_runtime_executed;
    r->public_pexpert_runtime_executed = 0u;
    r->public_iokit_runtime_executed = entry_result ? entry_result->public_iokit_runtime_executed : 0u;
    r->generated_macho_executed = (entry_result ? entry_result->generated_macho_executed : 0u) |
        pmap->generated_macho_executed;
    r->live_pmap_installed = (entry_result ? entry_result->live_pmap_installed : 0u) |
        pmap->live_pmap_tables_installed;
    r->ttbr_written = pmap->ttbr_written;
    r->ttbcr_written = pmap->ttbcr_written;
    r->dacr_written = pmap->dacr_written;
    r->sctlr_written = pmap->sctlr_written;
    r->tlb_invalidated = (entry_result ? entry_result->tlb_invalidated : 0u) | pmap->tlbs_invalidated;
    r->cache_policy_changed = (entry_result ? entry_result->cache_policy_changed : 0u) | pmap->caches_changed;
    r->persistent_write_attempted = (entry_result ? entry_result->persistent_write_attempted : 0u) |
        pmap->persistent_write_attempted;

    r->ttbr0_after = stage79_early_init_read_ttbr0();
    r->ttbcr_after = stage79_early_init_read_ttbcr();
    r->dacr_after = stage79_early_init_read_dacr();
    r->sctlr_after = stage79_early_init_read_sctlr();

    if (r->control_registers_sampled == 1u) {
        r->satisfied_mask |= STAGE79_XNU_EARLY_INIT_SAT_CONTROL_REGS_SAMPLED;
    } else {
        r->failure_mask |= STAGE79_XNU_EARLY_INIT_FAIL_CONTROL_REG_SAMPLE;
    }

    if (r->ttbr0_before == r->ttbr0_after && r->ttbcr_before == r->ttbcr_after &&
        r->dacr_before == r->dacr_after && r->sctlr_before == r->sctlr_after) {
        r->control_registers_unchanged = 1u;
        r->satisfied_mask |= STAGE79_XNU_EARLY_INIT_SAT_CONTROL_REGS_UNCHANGED;
    } else {
        r->failure_mask |= STAGE79_XNU_EARLY_INIT_FAIL_CONTROL_REG_CHANGED;
    }

    if (r->stage_owned_local_only == 1u) {
        r->satisfied_mask |= STAGE79_XNU_EARLY_INIT_SAT_LOCAL_ONLY;
    } else {
        r->failure_mask |= STAGE79_XNU_EARLY_INIT_FAIL_SAFETY_BOUNDARY;
    }

    public_boundary_ok = (r->public_xnu_start_executed == 0u &&
                          r->public_arm_init_executed == 0u &&
                          r->public_pmap_runtime_executed == 0u &&
                          r->public_pexpert_runtime_executed == 0u &&
                          r->public_iokit_runtime_executed == 0u &&
                          r->generated_macho_executed == 0u) ? 1u : 0u;
    if (r->public_xnu_start_executed == 0u && r->public_arm_init_executed == 0u) {
        r->satisfied_mask |= STAGE79_XNU_EARLY_INIT_SAT_NO_PUBLIC_XNU_ENTRY;
    } else {
        r->failure_mask |= STAGE79_XNU_EARLY_INIT_FAIL_PUBLIC_RUNTIME;
    }
    if (r->public_pmap_runtime_executed == 0u) {
        r->satisfied_mask |= STAGE79_XNU_EARLY_INIT_SAT_NO_PUBLIC_PMAP_RUNTIME;
    } else {
        r->failure_mask |= STAGE79_XNU_EARLY_INIT_FAIL_PUBLIC_RUNTIME;
    }
    if (r->public_pexpert_runtime_executed == 0u) {
        r->satisfied_mask |= STAGE79_XNU_EARLY_INIT_SAT_NO_PUBLIC_PEXPERT;
    } else {
        r->failure_mask |= STAGE79_XNU_EARLY_INIT_FAIL_PUBLIC_RUNTIME;
    }
    if (r->public_iokit_runtime_executed == 0u) {
        r->satisfied_mask |= STAGE79_XNU_EARLY_INIT_SAT_NO_PUBLIC_IOKIT;
    } else {
        r->failure_mask |= STAGE79_XNU_EARLY_INIT_FAIL_PUBLIC_RUNTIME;
    }
    if (r->generated_macho_executed == 0u) {
        r->satisfied_mask |= STAGE79_XNU_EARLY_INIT_SAT_NO_MACHO_EXEC;
    } else {
        r->failure_mask |= STAGE79_XNU_EARLY_INIT_FAIL_PUBLIC_RUNTIME;
    }

    mutation_boundary_ok = (r->live_pmap_installed == 0u &&
                            r->ttbr_written == 0u && r->ttbcr_written == 0u &&
                            r->dacr_written == 0u && r->sctlr_written == 0u &&
                            r->tlb_invalidated == 0u && r->cache_policy_changed == 0u &&
                            r->persistent_write_attempted == 0u) ? 1u : 0u;
    if (r->live_pmap_installed == 0u) {
        r->satisfied_mask |= STAGE79_XNU_EARLY_INIT_SAT_NO_LIVE_PMAP_INSTALL;
    } else {
        r->failure_mask |= STAGE79_XNU_EARLY_INIT_FAIL_MUTATION;
    }
    if (r->ttbr_written == 0u && r->ttbcr_written == 0u &&
        r->dacr_written == 0u && r->sctlr_written == 0u) {
        r->satisfied_mask |= STAGE79_XNU_EARLY_INIT_SAT_NO_CONTROL_REG_WRITES;
    } else {
        r->failure_mask |= STAGE79_XNU_EARLY_INIT_FAIL_MUTATION;
    }
    if (r->tlb_invalidated == 0u) {
        r->satisfied_mask |= STAGE79_XNU_EARLY_INIT_SAT_NO_TLB_INVALIDATE;
    } else {
        r->failure_mask |= STAGE79_XNU_EARLY_INIT_FAIL_MUTATION;
    }
    if (r->cache_policy_changed == 0u) {
        r->satisfied_mask |= STAGE79_XNU_EARLY_INIT_SAT_NO_CACHE_POLICY_CHANGE;
    } else {
        r->failure_mask |= STAGE79_XNU_EARLY_INIT_FAIL_MUTATION;
    }
    if (r->persistent_write_attempted == 0u) {
        r->satisfied_mask |= STAGE79_XNU_EARLY_INIT_SAT_NO_PERSISTENT_WRITE;
    } else {
        r->failure_mask |= STAGE79_XNU_EARLY_INIT_FAIL_MUTATION;
    }

    if (public_boundary_ok == 1u && mutation_boundary_ok == 1u &&
        r->stage_owned_local_only == 1u && r->control_registers_unchanged == 1u &&
        pmap->candidate_not_installed == 1u && pmap->control_register_plan_readonly == 1u &&
        pmap->tlb_plan_readonly == 1u) {
        r->safety_boundary_preserved = 1u;
        r->satisfied_mask |= STAGE79_XNU_EARLY_INIT_SAT_SAFETY_BOUNDARY;
    } else {
        r->failure_mask |= STAGE79_XNU_EARLY_INIT_FAIL_SAFETY_BOUNDARY;
    }

    if (r->satisfied_mask != r->required_mask && r->failure_mask == 0u) {
        r->failure_mask |= STAGE79_XNU_EARLY_INIT_FAIL_SAFETY_BOUNDARY;
    }
    r->status = (r->satisfied_mask == r->required_mask && r->failure_mask == 0u) ?
        STAGE79_STATUS_OK : (STAGE79_STATUS_BASE | r->failure_mask);
    r->checksum = stage79_early_init_checksum(r);

    stage79_early_init_log(r);
    if (r->status == STAGE79_STATUS_OK) {
        xnu_log_puts("stage79_xnu_early_init: returning to Stage-owned arm_init stub\n");
    } else {
        xnu_log_puts("stage79_xnu_early_init: failed, returning to Stage-owned arm_init stub\n");
    }

    return r->status == STAGE79_STATUS_OK;
}

const struct stage79_xnu_early_pmap_platform_init_result *stage79_xnu_early_pmap_platform_init_result(void)
{
    return &g_stage79_xnu_early_init_result;
}
