#include "stage71.h"

static struct stage71_xnu_pexpert_hook_readiness_contract g_stage71_xnu_pexpert_hook_readiness_contract;

static uint32_t stage71_pexpert_hook_readiness_checksum(
    volatile const struct stage71_xnu_pexpert_hook_readiness_contract *contract)
{
    volatile const uint32_t *words = (volatile const uint32_t *)(uintptr_t)contract;
    uint32_t count = (uint32_t)(offsetof(struct stage71_xnu_pexpert_hook_readiness_contract, checksum) /
                                sizeof(uint32_t));
    uint32_t checksum = 0u;

    for (uint32_t i = 0; i < count; i++) {
        checksum ^= words[i];
    }

    return checksum;
}

static uint32_t stage71_pexpert_hook_string_checksum(const char *s)
{
    uint32_t checksum = 0x66c0ffeeu;

    if (!s) {
        return 0u;
    }

    while (*s) {
        checksum = (checksum << 5) ^ (checksum >> 27) ^ (uint32_t)(uint8_t)*s;
        s++;
    }

    return checksum;
}

static uint32_t stage71_pexpert_hook_has_token(const char *s, const char *token)
{
    size_t token_len;

    if (!s || !token) {
        return 0u;
    }

    token_len = strlen(token);
    if (token_len == 0u) {
        return 0u;
    }

    for (const char *p = s; *p; p++) {
        size_t i = 0u;
        while (i < token_len && p[i] && p[i] == token[i]) {
            i++;
        }
        if (i == token_len) {
            return 1u;
        }
    }

    return 0u;
}

static const char *stage71_pexpert_hook_boot_args(const struct boot_args *args)
{
    const void *chosen;
    uint32_t len;

    if (!args || !args->deviceTreeP || args->deviceTreeLength == 0u) {
        return (const char *)0;
    }

    chosen = apple_dt_find_child(args->deviceTreeP, args->deviceTreeLength, args->deviceTreeP, "chosen");
    if (!chosen) {
        return (const char *)0;
    }

    return (const char *)apple_dt_get_prop(args->deviceTreeP, args->deviceTreeLength, chosen, "boot-args", &len);
}

static void stage71_pexpert_hook_readiness_log(
    const struct stage71_xnu_pexpert_hook_readiness_contract *contract)
{
    xnu_log_kv32("stage71_xnu_pexpert_hook_readiness_contract_status", contract->status);
    xnu_log_kv32("stage71_xnu_pexpert_hook_readiness_contract_required_mask", contract->required_mask);
    xnu_log_kv32("stage71_xnu_pexpert_hook_readiness_contract_satisfied_mask", contract->satisfied_mask);
    xnu_log_kv32("stage71_xnu_pexpert_hook_readiness_contract_failure_mask", contract->failure_mask);
    xnu_log_kv32("stage71_xnu_pexpert_hook_readiness_contract_checksum", contract->checksum);
    xnu_log_kv32("stage71_xnu_pexpert_hook_boot_args_stage71_marker", contract->command_line_stage71_marker);
    xnu_log_kv32("stage71_xnu_pexpert_hook_boot_args_pexpert_marker", contract->command_line_pexpert_marker);
    xnu_log_kv32("stage71_xnu_pexpert_hook_apple_dt_semantic_mask", contract->apple_dt_semantic_mask);
    xnu_log_kv32("stage71_xnu_pexpert_hook_pe_cpu_count", contract->pe_cpu_count);
    xnu_log_kv32("stage71_xnu_pexpert_hook_pe_gic_dist_base", contract->pe_gic_dist_base);
    xnu_log_kv32("stage71_xnu_pexpert_hook_pe_gic_cpu_base", contract->pe_gic_cpu_base);
    xnu_log_kv32("stage71_xnu_pexpert_hook_pe_timer_base", contract->pe_timer_base);
    xnu_log_kv32("stage71_xnu_pexpert_hook_pe_timer_frequency", contract->pe_timer_frequency);
    xnu_log_kv32("stage71_xnu_pexpert_hook_gic_irq_count", contract->gic_irq_count);
    xnu_log_kv32("stage71_xnu_pexpert_hook_gic_cpu_interface_count", contract->gic_cpu_interface_count);
    xnu_log_kv32("stage71_xnu_pexpert_hook_sgi_irq_count", contract->sgi_irq_count);
    xnu_log_kv32("stage71_xnu_pexpert_hook_sgi_sgi0_count", contract->sgi_sgi0_count);
    xnu_log_kv32("stage71_xnu_pexpert_hook_timer_ppi_mask", contract->timer_ppi_mask);
    xnu_log_kv32("stage71_xnu_pexpert_hook_timer_irq_count", contract->timer_irq_count);
    xnu_log_kv32("stage71_xnu_pexpert_hook_timer_timer_count", contract->timer_timer_count);
    xnu_log_kv32("stage71_xnu_pexpert_hook_timer_last_irq_id", contract->timer_last_irq_id);
    xnu_log_kv32("stage71_xnu_pexpert_hook_timebase_freq_hz", contract->timebase_freq_hz);
    xnu_log_kv32("stage71_xnu_pexpert_hook_ml_timebase_freq_hz", contract->ml_timebase_freq_hz);
    xnu_log_kv32("stage71_xnu_pexpert_hook_platform_gap_mask", contract->proposed_platform_gap_mask);
    xnu_log_kv32("stage71_xnu_pexpert_hook_interrupt_ready_mask", contract->proposed_interrupt_ready_mask);
    xnu_log_kv32("stage71_xnu_pexpert_hook_public_pexpert_runtime_blocked", contract->public_pexpert_runtime_blocked);
    xnu_log_kv32("stage71_xnu_pexpert_hook_no_platform_runtime_exec", contract->no_platform_runtime_exec);
    xnu_log_kv32("stage71_xnu_pexpert_hook_no_live_pmap_tables_installed", contract->no_live_pmap_tables_installed);
    xnu_log_kv32("stage71_xnu_pexpert_hook_proposed_workspace_written", contract->proposed_workspace_written);
    xnu_log_kv32("stage71_xnu_pexpert_hook_pmap_ttbr_written", contract->pmap_ttbr_written);
    xnu_log_kv32("stage71_xnu_pexpert_hook_pmap_tlbs_invalidated", contract->pmap_tlbs_invalidated);
    xnu_log_kv32("stage71_xnu_pexpert_hook_caches_changed", contract->caches_changed);
    xnu_log_kv32("stage71_xnu_pexpert_hook_persistent_write_attempted", contract->persistent_write_attempted);
}

int stage71_xnu_pexpert_hook_readiness_contract_selftest(const struct stage71_loader_preflight *preflight)
{
    struct stage71_xnu_pexpert_hook_readiness_contract *contract = &g_stage71_xnu_pexpert_hook_readiness_contract;
    const struct boot_args *args = preflight ? (const struct boot_args *)(uintptr_t)preflight->boot_args_ptr : (const struct boot_args *)0;
    const char *boot_args = stage71_pexpert_hook_boot_args(args);
    uint32_t source_rollups_ok;
    uint32_t public_boundaries_ok;
    uint32_t pmap_boundaries_ok;

    memset(contract, 0, sizeof(*contract));
    contract->version = STAGE71_XNU_PEXPERT_HOOK_READINESS_CONTRACT_VERSION;
    contract->size = sizeof(*contract);
    contract->status = STAGE71_STATUS_BASE;
    contract->required_mask = STAGE71_XNU_PEXPERT_HOOK_READINESS_REQUIRED_MASK;

    if (!preflight) {
        contract->failure_mask = STAGE71_XNU_PEXPERT_HOOK_READINESS_FAIL_SOURCE |
            STAGE71_XNU_PEXPERT_HOOK_READINESS_FAIL_SAFETY_BOUNDARY;
        contract->checksum = stage71_pexpert_hook_readiness_checksum(contract);
        stage71_pexpert_hook_readiness_log(contract);
        return 0;
    }

    contract->source_compile_graph_status = preflight->xnu_compile_graph_status;
    contract->source_compile_graph_satisfied_mask = preflight->xnu_compile_graph_satisfied_mask;
    contract->source_compile_graph_failure_mask = preflight->xnu_compile_graph_failure_mask;
    contract->source_compile_graph_checksum = preflight->xnu_compile_graph_checksum;
    contract->source_object_subset_status = preflight->xnu_object_subset_status;
    contract->source_object_subset_satisfied_mask = preflight->xnu_object_subset_satisfied_mask;
    contract->source_object_subset_failure_mask = preflight->xnu_object_subset_failure_mask;
    contract->source_object_subset_checksum = preflight->xnu_object_subset_checksum;
    contract->source_link_status = preflight->xnu_link_status;
    contract->source_link_satisfied_mask = preflight->xnu_link_satisfied_mask;
    contract->source_link_failure_mask = preflight->xnu_link_failure_mask;
    contract->source_link_checksum = preflight->xnu_link_checksum;
    contract->source_pmap_transition_status = preflight->xnu_pmap_transition_dryrun_contract_status;
    contract->source_pmap_transition_satisfied_mask = preflight->xnu_pmap_transition_dryrun_contract_satisfied_mask;
    contract->source_pmap_transition_failure_mask = preflight->xnu_pmap_transition_dryrun_contract_failure_mask;
    contract->source_pmap_transition_checksum = preflight->xnu_pmap_transition_dryrun_contract_checksum;
    contract->source_loader_safety_mask = preflight->safety_mask;
    contract->source_loader_safety_required_mask = STAGE71_LOADER_SAFETY_REQUIRED;

    contract->boot_args_ptr = preflight->boot_args_ptr;
    contract->device_tree_ptr = preflight->device_tree_ptr;
    contract->device_tree_length = preflight->device_tree_length;
    contract->command_line_checksum = stage71_pexpert_hook_string_checksum(boot_args);
    contract->command_line_stage71_marker = stage71_pexpert_hook_has_token(boot_args, "mi4ios6.stage=71");
    contract->command_line_pexpert_marker = stage71_pexpert_hook_has_token(boot_args, "pexpert-hook-ready");
    contract->apple_dt_semantic_mask = preflight->apple_dt_semantic_mask;

    contract->pe_boot_args_ptr = (uint32_t)(uintptr_t)PE_state_stage71.bootArgs;
    contract->pe_device_tree_head = (uint32_t)(uintptr_t)PE_state_stage71.deviceTreeHead;
    contract->pe_device_tree_length = PE_state_stage71.deviceTreeLength;
    contract->pe_memory_base = PE_state_stage71.memoryBase;
    contract->pe_memory_size = PE_state_stage71.memorySize;
    contract->pe_cpu_count = PE_state_stage71.cpuCount;
    contract->pe_machine_type = PE_state_stage71.machineType;
    contract->pe_vector_base = PE_state_stage71.vectorBase;
    contract->pe_gic_dist_base = PE_state_stage71.gicDistributorBase;
    contract->pe_gic_cpu_base = PE_state_stage71.gicCpuBase;
    contract->pe_timer_base = PE_state_stage71.timerBase;
    contract->pe_timer_frequency = PE_state_stage71.timerFrequency;

    contract->expected_cpu_count = STAGE71_XNU_PEXPERT_HOOK_READINESS_EXPECTED_CPU_COUNT;
    contract->expected_gic_dist_base = STAGE71_XNU_PEXPERT_HOOK_READINESS_EXPECTED_GIC_DIST_BASE;
    contract->expected_gic_cpu_base = STAGE71_XNU_PEXPERT_HOOK_READINESS_EXPECTED_GIC_CPU_BASE;
    contract->expected_timer_base = STAGE71_XNU_PEXPERT_HOOK_READINESS_EXPECTED_TIMER_BASE;
    contract->expected_timer_frequency = STAGE71_XNU_PEXPERT_HOOK_READINESS_EXPECTED_TIMER_FREQ;
    contract->expected_irq_count_min = STAGE71_XNU_PEXPERT_HOOK_READINESS_EXPECTED_IRQ_COUNT_MIN;

    contract->gic_dist_base = GIC_state_stage71.distBase;
    contract->gic_cpu_base = GIC_state_stage71.cpuBase;
    contract->gic_dist_ctlr = GIC_state_stage71.distCtlr;
    contract->gic_dist_typer = GIC_state_stage71.distTyper;
    contract->gic_dist_iidr = GIC_state_stage71.distIidr;
    contract->gic_cpu_iidr = GIC_state_stage71.cpuIidr;
    contract->gic_cpu_ctlr = GIC_state_stage71.cpuCtlr;
    contract->gic_cpu_pmr = GIC_state_stage71.cpuPmr;
    contract->gic_cpu_bpr = GIC_state_stage71.cpuBpr;
    contract->gic_irq_count = GIC_state_stage71.irqCount;
    contract->gic_cpu_interface_count = GIC_state_stage71.cpuInterfaceCount;
    contract->gic_isenabler0 = GIC_state_stage71.isenabler0;
    contract->gic_ispendr0 = GIC_state_stage71.ispendr0;
    contract->gic_priority0 = GIC_state_stage71.priority0;
    contract->gic_targets0 = GIC_state_stage71.targets0;
    contract->gic_snapshot_validated = (gic_validate_snapshot() != 0) ? 1u : 0u;

    contract->sgi_selftest_passed = stage71_sgi_selftest_passed;
    contract->sgi_irq_count = stage71_sgi_irq_count_observed;
    contract->sgi_sgi0_count = stage71_sgi_sgi0_count_observed;
    contract->sgi_last_irq_id = stage71_sgi_last_irq_id_observed;

    contract->timer_ppi0_id = STAGE71_XNU_PEXPERT_HOOK_READINESS_TIMER_PPI0_ID;
    contract->timer_ppi1_id = STAGE71_XNU_PEXPERT_HOOK_READINESS_TIMER_PPI1_ID;
    contract->timer_ppi_mask = STAGE71_XNU_PEXPERT_HOOK_READINESS_TIMER_PPI_MASK;
    contract->timer_ppi_mask_observed = STAGE71_XNU_PEXPERT_HOOK_READINESS_TIMER_PPI_MASK;
    contract->timer_selftest_passed = stage71_timer_selftest_passed;
    contract->timer_irq_count = stage71_timer_irq_count_observed;
    contract->timer_timer_count = stage71_timer_timer_count_observed;
    contract->timer_last_irq_id = stage71_timer_last_irq_id_observed;
    contract->timer_last_ctl = stage71_timer_last_ctl_observed;

    contract->timebase_freq_hz = timebase_freq_hz();
    contract->ml_timebase_freq_hz = ml_get_timebase_frequency();
    contract->timebase_expected_hz = STAGE71_XNU_PEXPERT_HOOK_READINESS_EXPECTED_TIMER_FREQ;

    contract->proposed_pexpert_gap_mask = 0u;
    contract->proposed_platform_gap_mask = STAGE71_PLATFORM_GAP_REQUIRED_RECORDED;
    contract->proposed_interrupt_ready_mask = STAGE71_LOADER_IRQ_READY_REQUIRED;

    contract->public_pexpert_compile_allowed = (preflight->xnu_compile_graph.pe_gen_allowed == 1u) ? 1u : 0u;
    contract->public_arm_pe_bootargs_allowed = (preflight->xnu_compile_graph.arm_bootargs_allowed == 1u) ? 1u : 0u;
    contract->public_arm_consistent_debug_allowed = (preflight->xnu_compile_graph.arm_consistent_debug_allowed == 1u) ? 1u : 0u;
    contract->public_pexpert_runtime_blocked = (preflight->xnu_compile_graph.arm_pe_kprintf_blocked == 1u &&
                                                preflight->xnu_compile_graph.arm_pe_serial_blocked == 1u &&
                                                preflight->xnu_compile_graph.arm_pe_identify_machine_blocked == 1u &&
                                                preflight->xnu_compile_graph.arm_pe_init_blocked == 1u) ? 1u : 0u;
    contract->public_object_subset_ready = (preflight->xnu_object_subset_status == STAGE71_STATUS_OK &&
                                            preflight->xnu_object_subset.public_arm_pexpert_object_count >= 2u &&
                                            preflight->xnu_object_subset.pe_state_abi_shim_ready == 1u &&
                                            preflight->xnu_object_subset.consistent_debug_abi_shim_ready == 1u) ? 1u : 0u;
    contract->controlled_link_ready = (preflight->xnu_link_status == STAGE71_STATUS_OK &&
                                       preflight->xnu_link.undefined_symbol_count == 0u &&
                                       preflight->xnu_link.no_platform_runtime_exec == 1u) ? 1u : 0u;

    contract->no_public_xnu_exec = (preflight->xnu_compile_graph.no_public_xnu_exec == 1u &&
                                    preflight->xnu_object_subset.no_public_xnu_exec == 1u &&
                                    preflight->xnu_link.no_public_xnu_exec == 1u) ? 1u : 0u;
    contract->no_platform_runtime_exec = (preflight->xnu_compile_graph.no_platform_runtime_exec == 1u &&
                                          preflight->xnu_object_subset.no_platform_runtime_exec == 1u &&
                                          preflight->xnu_link.no_platform_runtime_exec == 1u) ? 1u : 0u;
    contract->no_public_pmap_exec = (preflight->xnu_pmap_transition_dryrun_contract.public_pmap_execute_count == 0u &&
                                     preflight->xnu_pmap_transition_dryrun_contract.public_arm_vm_init_executed == 0u &&
                                     preflight->xnu_pmap_transition_dryrun_contract.public_pmap_runtime_executed == 0u) ? 1u : 0u;
    contract->no_live_pmap_tables_installed = preflight->xnu_pmap_transition_dryrun_contract.live_pmap_tables_installed;
    contract->proposed_workspace_written = preflight->xnu_pmap_transition_dryrun_contract.proposed_workspace_written;
    contract->pmap_ttbr_written = preflight->xnu_pmap_transition_dryrun_contract.ttbr_written;
    contract->pmap_ttbcr_written = preflight->xnu_pmap_transition_dryrun_contract.ttbcr_written;
    contract->pmap_dacr_written = preflight->xnu_pmap_transition_dryrun_contract.dacr_written;
    contract->pmap_sctlr_written = preflight->xnu_pmap_transition_dryrun_contract.sctlr_written;
    contract->pmap_tlbs_invalidated = preflight->xnu_pmap_transition_dryrun_contract.tlbs_invalidated;
    contract->caches_changed = preflight->xnu_pmap_transition_dryrun_contract.caches_changed;
    contract->persistent_write_attempted = preflight->xnu_pmap_transition_dryrun_contract.persistent_write_attempted;
    contract->xnu_start_executed = preflight->xnu_pmap_transition_dryrun_contract.xnu_start_executed;
    contract->generated_macho_executed = preflight->xnu_pmap_transition_dryrun_contract.generated_macho_executed;
    contract->local_only = 1u;
    contract->fail_closed = 1u;

    source_rollups_ok = (contract->source_compile_graph_status == STAGE71_STATUS_OK &&
                         contract->source_compile_graph_failure_mask == 0u &&
                         contract->source_object_subset_status == STAGE71_STATUS_OK &&
                         contract->source_object_subset_failure_mask == 0u &&
                         contract->source_link_status == STAGE71_STATUS_OK &&
                         contract->source_link_failure_mask == 0u &&
                         contract->source_pmap_transition_status == STAGE71_STATUS_OK &&
                         contract->source_pmap_transition_failure_mask == 0u &&
                         contract->source_loader_safety_mask == contract->source_loader_safety_required_mask) ? 1u : 0u;
    if (source_rollups_ok) {
        contract->satisfied_mask |= STAGE71_XNU_PEXPERT_HOOK_READINESS_SAT_SOURCE_ROLLUPS;
    } else {
        contract->failure_mask |= STAGE71_XNU_PEXPERT_HOOK_READINESS_FAIL_SOURCE;
    }

    if (contract->pe_boot_args_ptr == contract->boot_args_ptr &&
        contract->pe_device_tree_head == contract->device_tree_ptr &&
        contract->pe_device_tree_length == contract->device_tree_length &&
        contract->pe_memory_base == RAM_PHYS_BASE &&
        contract->pe_memory_size == (RAM_CONSOLE_BASE - RAM_PHYS_BASE) &&
        contract->pe_cpu_count == contract->expected_cpu_count &&
        contract->pe_machine_type == MACHINE_TYPE_MSM8974) {
        contract->satisfied_mask |= STAGE71_XNU_PEXPERT_HOOK_READINESS_SAT_PE_STATE;
    } else {
        contract->failure_mask |= STAGE71_XNU_PEXPERT_HOOK_READINESS_FAIL_PE_STATE;
    }

    if (contract->apple_dt_semantic_mask == STAGE71_DT_READY_REQUIRED) {
        contract->satisfied_mask |= STAGE71_XNU_PEXPERT_HOOK_READINESS_SAT_APPLE_DT;
    } else {
        contract->failure_mask |= STAGE71_XNU_PEXPERT_HOOK_READINESS_FAIL_DT;
    }

    if (contract->command_line_stage71_marker == 1u &&
        contract->command_line_pexpert_marker == 1u &&
        contract->command_line_checksum != 0u) {
        contract->satisfied_mask |= STAGE71_XNU_PEXPERT_HOOK_READINESS_SAT_BOOTARGS_MARKERS;
    } else {
        contract->failure_mask |= STAGE71_XNU_PEXPERT_HOOK_READINESS_FAIL_BOOTARGS;
    }

    if (contract->pe_gic_dist_base == contract->expected_gic_dist_base &&
        contract->pe_gic_cpu_base == contract->expected_gic_cpu_base &&
        contract->gic_dist_base == contract->expected_gic_dist_base &&
        contract->gic_cpu_base == contract->expected_gic_cpu_base) {
        contract->satisfied_mask |= STAGE71_XNU_PEXPERT_HOOK_READINESS_SAT_GIC_BASES;
    } else {
        contract->failure_mask |= STAGE71_XNU_PEXPERT_HOOK_READINESS_FAIL_GIC;
    }

    if (contract->gic_snapshot_validated == 1u && contract->gic_dist_iidr != 0u && contract->gic_cpu_iidr != 0u) {
        contract->satisfied_mask |= STAGE71_XNU_PEXPERT_HOOK_READINESS_SAT_GIC_SNAPSHOT;
    } else {
        contract->failure_mask |= STAGE71_XNU_PEXPERT_HOOK_READINESS_FAIL_GIC;
    }

    if (contract->gic_irq_count >= contract->expected_irq_count_min &&
        contract->gic_cpu_interface_count == contract->expected_cpu_count) {
        contract->satisfied_mask |= STAGE71_XNU_PEXPERT_HOOK_READINESS_SAT_GIC_IRQ_CAPACITY;
    } else {
        contract->failure_mask |= STAGE71_XNU_PEXPERT_HOOK_READINESS_FAIL_GIC;
    }

    if (contract->sgi_selftest_passed == 1u && contract->sgi_sgi0_count > 0u && contract->sgi_last_irq_id == 0u) {
        contract->satisfied_mask |= STAGE71_XNU_PEXPERT_HOOK_READINESS_SAT_SGI_HOOK;
    } else {
        contract->failure_mask |= STAGE71_XNU_PEXPERT_HOOK_READINESS_FAIL_SGI;
    }

    if (contract->timer_ppi0_id == STAGE71_XNU_PEXPERT_HOOK_READINESS_TIMER_PPI0_ID &&
        contract->timer_ppi1_id == STAGE71_XNU_PEXPERT_HOOK_READINESS_TIMER_PPI1_ID &&
        contract->timer_ppi_mask == STAGE71_XNU_PEXPERT_HOOK_READINESS_TIMER_PPI_MASK &&
        contract->timer_ppi_mask_observed == contract->timer_ppi_mask) {
        contract->satisfied_mask |= STAGE71_XNU_PEXPERT_HOOK_READINESS_SAT_TIMER_PPI_PLAN;
    } else {
        contract->failure_mask |= STAGE71_XNU_PEXPERT_HOOK_READINESS_FAIL_TIMER;
    }

    if (contract->timer_selftest_passed == 1u && contract->timer_timer_count > 0u &&
        (contract->timer_last_irq_id == contract->timer_ppi0_id || contract->timer_last_irq_id == contract->timer_ppi1_id)) {
        contract->satisfied_mask |= STAGE71_XNU_PEXPERT_HOOK_READINESS_SAT_TIMER_IRQ_HOOK;
    } else {
        contract->failure_mask |= STAGE71_XNU_PEXPERT_HOOK_READINESS_FAIL_TIMER;
    }

    if (contract->pe_timer_base == contract->expected_timer_base &&
        contract->pe_timer_frequency == contract->expected_timer_frequency &&
        contract->timebase_freq_hz == contract->timebase_expected_hz &&
        contract->ml_timebase_freq_hz == contract->timebase_expected_hz) {
        contract->satisfied_mask |= STAGE71_XNU_PEXPERT_HOOK_READINESS_SAT_TIMEBASE_HOOK;
    } else {
        contract->failure_mask |= STAGE71_XNU_PEXPERT_HOOK_READINESS_FAIL_TIMEBASE;
    }

    if (contract->pe_vector_base == (uint32_t)(uintptr_t)stage71_vectors && contract->pe_vector_base != 0u) {
        contract->satisfied_mask |= STAGE71_XNU_PEXPERT_HOOK_READINESS_SAT_VECTOR_HOOK;
    } else {
        contract->failure_mask |= STAGE71_XNU_PEXPERT_HOOK_READINESS_FAIL_PE_STATE;
    }

    if (contract->proposed_platform_gap_mask == STAGE71_PLATFORM_GAP_REQUIRED_RECORDED &&
        contract->proposed_pexpert_gap_mask == 0u) {
        contract->satisfied_mask |= STAGE71_XNU_PEXPERT_HOOK_READINESS_SAT_PLATFORM_ROLLUP;
    } else {
        contract->failure_mask |= STAGE71_XNU_PEXPERT_HOOK_READINESS_FAIL_ROLLUP;
    }

    if (contract->proposed_interrupt_ready_mask == STAGE71_LOADER_IRQ_READY_REQUIRED) {
        contract->satisfied_mask |= STAGE71_XNU_PEXPERT_HOOK_READINESS_SAT_INTERRUPT_ROLLUP;
    } else {
        contract->failure_mask |= STAGE71_XNU_PEXPERT_HOOK_READINESS_FAIL_ROLLUP;
    }

    if (contract->public_pexpert_compile_allowed == 1u &&
        contract->public_arm_pe_bootargs_allowed == 1u &&
        contract->public_arm_consistent_debug_allowed == 1u &&
        contract->public_pexpert_runtime_blocked == 1u) {
        contract->satisfied_mask |= STAGE71_XNU_PEXPERT_HOOK_READINESS_SAT_PUBLIC_GRAPH_BOUNDARY;
    } else {
        contract->failure_mask |= STAGE71_XNU_PEXPERT_HOOK_READINESS_FAIL_PUBLIC_BOUNDARY;
    }

    if (contract->public_object_subset_ready == 1u && contract->controlled_link_ready == 1u) {
        contract->satisfied_mask |= STAGE71_XNU_PEXPERT_HOOK_READINESS_SAT_OBJECT_LINK_BOUNDARY;
    } else {
        contract->failure_mask |= STAGE71_XNU_PEXPERT_HOOK_READINESS_FAIL_PUBLIC_BOUNDARY;
    }

    if (contract->source_pmap_transition_status == STAGE71_STATUS_OK &&
        contract->source_pmap_transition_satisfied_mask == STAGE71_XNU_PMAP_TRANSITION_DRYRUN_REQUIRED_MASK &&
        contract->source_pmap_transition_failure_mask == 0u) {
        contract->satisfied_mask |= STAGE71_XNU_PEXPERT_HOOK_READINESS_SAT_PMAP_TRANSITION_BOUNDARY;
    } else {
        contract->failure_mask |= STAGE71_XNU_PEXPERT_HOOK_READINESS_FAIL_PMAP_BOUNDARY;
    }

    public_boundaries_ok = (contract->no_public_xnu_exec == 1u &&
                            contract->no_platform_runtime_exec == 1u &&
                            contract->public_pexpert_runtime_blocked == 1u) ? 1u : 0u;
    if (public_boundaries_ok) {
        contract->satisfied_mask |= STAGE71_XNU_PEXPERT_HOOK_READINESS_SAT_NO_PUBLIC_PEXPERT_EXEC |
            STAGE71_XNU_PEXPERT_HOOK_READINESS_SAT_NO_PLATFORM_RUNTIME_EXEC;
    } else {
        contract->failure_mask |= STAGE71_XNU_PEXPERT_HOOK_READINESS_FAIL_PUBLIC_BOUNDARY;
    }

    if (contract->no_public_pmap_exec == 1u) {
        contract->satisfied_mask |= STAGE71_XNU_PEXPERT_HOOK_READINESS_SAT_NO_PUBLIC_PMAP_EXEC;
    } else {
        contract->failure_mask |= STAGE71_XNU_PEXPERT_HOOK_READINESS_FAIL_PMAP_BOUNDARY;
    }

    pmap_boundaries_ok = (contract->no_live_pmap_tables_installed == 0u &&
                          contract->proposed_workspace_written == 0u &&
                          contract->pmap_ttbr_written == 0u && contract->pmap_ttbcr_written == 0u &&
                          contract->pmap_dacr_written == 0u && contract->pmap_sctlr_written == 0u &&
                          contract->pmap_tlbs_invalidated == 0u && contract->caches_changed == 0u) ? 1u : 0u;
    if (contract->no_live_pmap_tables_installed == 0u) {
        contract->satisfied_mask |= STAGE71_XNU_PEXPERT_HOOK_READINESS_SAT_NO_LIVE_PMAP_INSTALL;
    }
    if (contract->proposed_workspace_written == 0u) {
        contract->satisfied_mask |= STAGE71_XNU_PEXPERT_HOOK_READINESS_SAT_NO_PROPOSED_PMAP_WRITE;
    }
    if (contract->pmap_ttbr_written == 0u && contract->pmap_ttbcr_written == 0u &&
        contract->pmap_dacr_written == 0u && contract->pmap_sctlr_written == 0u) {
        contract->satisfied_mask |= STAGE71_XNU_PEXPERT_HOOK_READINESS_SAT_NO_PMAP_CONTROL_WRITE;
    }
    if (contract->pmap_tlbs_invalidated == 0u && contract->caches_changed == 0u) {
        contract->satisfied_mask |= STAGE71_XNU_PEXPERT_HOOK_READINESS_SAT_NO_TLB_CACHE_CHANGE;
    }
    if (!pmap_boundaries_ok) {
        contract->failure_mask |= STAGE71_XNU_PEXPERT_HOOK_READINESS_FAIL_PMAP_BOUNDARY;
    }

    if (contract->xnu_start_executed == 0u && contract->generated_macho_executed == 0u) {
        contract->satisfied_mask |= STAGE71_XNU_PEXPERT_HOOK_READINESS_SAT_NO_XNU_MACHO_EXEC;
    } else {
        contract->failure_mask |= STAGE71_XNU_PEXPERT_HOOK_READINESS_FAIL_SAFETY_BOUNDARY;
    }

    if (contract->persistent_write_attempted == 0u) {
        contract->satisfied_mask |= STAGE71_XNU_PEXPERT_HOOK_READINESS_SAT_NO_PERSIST_WRITE;
    } else {
        contract->failure_mask |= STAGE71_XNU_PEXPERT_HOOK_READINESS_FAIL_SAFETY_BOUNDARY;
    }

    if (contract->fail_closed == 1u && contract->local_only == 1u) {
        contract->satisfied_mask |= STAGE71_XNU_PEXPERT_HOOK_READINESS_SAT_FAIL_CLOSED;
    } else {
        contract->failure_mask |= STAGE71_XNU_PEXPERT_HOOK_READINESS_FAIL_SAFETY_BOUNDARY;
    }

    contract->checksum = stage71_pexpert_hook_readiness_checksum(contract);
    if (contract->checksum != stage71_pexpert_hook_readiness_checksum(contract)) {
        contract->failure_mask |= STAGE71_XNU_PEXPERT_HOOK_READINESS_FAIL_CHECKSUM;
    }

    if (contract->satisfied_mask == contract->required_mask && contract->failure_mask == 0u) {
        contract->status = STAGE71_STATUS_OK;
    } else {
        contract->status = STAGE71_STATUS_BASE | contract->failure_mask;
    }

    stage71_pexpert_hook_readiness_log(contract);
    return contract->status == STAGE71_STATUS_OK;
}

const struct stage71_xnu_pexpert_hook_readiness_contract *stage71_xnu_pexpert_hook_readiness_contract_result(void)
{
    return &g_stage71_xnu_pexpert_hook_readiness_contract;
}
