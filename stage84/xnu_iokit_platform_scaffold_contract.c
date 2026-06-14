#include "stage84.h"

static struct stage84_xnu_iokit_platform_scaffold_contract g_stage84_xnu_iokit_platform_scaffold_contract;

static uint32_t stage84_iokit_platform_scaffold_checksum(
    volatile const struct stage84_xnu_iokit_platform_scaffold_contract *contract)
{
    volatile const uint32_t *words = (volatile const uint32_t *)(uintptr_t)contract;
    uint32_t count = (uint32_t)(offsetof(struct stage84_xnu_iokit_platform_scaffold_contract, checksum) /
                                sizeof(uint32_t));
    uint32_t checksum = 0u;

    for (uint32_t i = 0; i < count; i++) {
        checksum ^= words[i];
    }

    return checksum;
}

static uint32_t prop_present(const struct boot_args *args, const void *node, const char *name)
{
    uint32_t len;

    if (!args || !node) {
        return 0u;
    }

    return (apple_dt_get_prop(args->deviceTreeP, args->deviceTreeLength, node, name, &len) && len != 0u) ? 1u : 0u;
}

static uint32_t reg_word(const struct boot_args *args, const void *node, uint32_t index)
{
    uint32_t len;
    const uint32_t *reg;

    if (!args || !node) {
        return 0u;
    }

    reg = (const uint32_t *)apple_dt_get_prop(args->deviceTreeP, args->deviceTreeLength, node, "reg", &len);
    if (!reg || len < (index + 1u) * sizeof(uint32_t)) {
        return 0u;
    }

    return reg[index];
}

static void stage84_iokit_platform_scaffold_log(
    const struct stage84_xnu_iokit_platform_scaffold_contract *contract)
{
    xnu_log_kv32("stage84_xnu_iokit_platform_scaffold_contract_status", contract->status);
    xnu_log_kv32("stage84_xnu_iokit_platform_scaffold_contract_required_mask", contract->required_mask);
    xnu_log_kv32("stage84_xnu_iokit_platform_scaffold_contract_satisfied_mask", contract->satisfied_mask);
    xnu_log_kv32("stage84_xnu_iokit_platform_scaffold_contract_failure_mask", contract->failure_mask);
    xnu_log_kv32("stage84_xnu_iokit_platform_scaffold_contract_checksum", contract->checksum);
    xnu_log_kv32("stage84_xnu_iokit_platform_apple_dt_semantic_mask", contract->apple_dt_semantic_mask);
    xnu_log_kv32("stage84_xnu_iokit_platform_device_tree_props", contract->device_tree_node_prop_count);
    xnu_log_kv32("stage84_xnu_iokit_platform_node_props", contract->iokit_platform_prop_count);
    xnu_log_kv32("stage84_xnu_iokit_platform_driver_props", contract->platform_driver_prop_count);
    xnu_log_kv32("stage84_xnu_iokit_platform_driver_gic_base", contract->platform_driver_gic_base);
    xnu_log_kv32("stage84_xnu_iokit_platform_driver_timer_base", contract->platform_driver_timer_base);
    xnu_log_kv32("stage84_xnu_iokit_platform_driver_timebase_frequency", contract->platform_driver_timebase_frequency);
    xnu_log_kv32("stage84_xnu_iokit_platform_driver_cpu_count", contract->platform_driver_cpu_count);
    xnu_log_kv32("stage84_xnu_iokit_reference_mask", contract->iokit_reference_mask);
    xnu_log_kv32("stage84_xnu_iokit_runtime_blocked_mask", contract->iokit_runtime_blocked_mask);
    xnu_log_kv32("stage84_xnu_iokit_reference_count", contract->iokit_reference_count);
    xnu_log_kv32("stage84_xnu_iokit_public_compile_count", contract->iokit_public_compile_count);
    xnu_log_kv32("stage84_xnu_iokit_public_link_count", contract->iokit_public_link_count);
    xnu_log_kv32("stage84_xnu_iokit_reference_only", contract->iokit_reference_only);
    xnu_log_kv32("stage84_xnu_iokit_no_iokit_runtime_exec", contract->no_iokit_runtime_exec);
    xnu_log_kv32("stage84_xnu_iokit_no_platform_driver_exec", contract->no_platform_driver_exec);
    xnu_log_kv32("stage84_xnu_iokit_no_public_xnu_exec", contract->no_public_xnu_exec);
    xnu_log_kv32("stage84_xnu_iokit_no_live_pmap_tables_installed", contract->no_live_pmap_tables_installed);
    xnu_log_kv32("stage84_xnu_iokit_proposed_workspace_written", contract->proposed_workspace_written);
    xnu_log_kv32("stage84_xnu_iokit_pmap_ttbr_written", contract->pmap_ttbr_written);
    xnu_log_kv32("stage84_xnu_iokit_pmap_tlbs_invalidated", contract->pmap_tlbs_invalidated);
    xnu_log_kv32("stage84_xnu_iokit_caches_changed", contract->caches_changed);
    xnu_log_kv32("stage84_xnu_iokit_persistent_write_attempted", contract->persistent_write_attempted);
    xnu_log_kv32("stage84_xnu_iokit_platform_gap_mask", contract->proposed_platform_gap_mask);
}

int stage84_xnu_iokit_platform_scaffold_contract_selftest(const struct stage84_loader_preflight *preflight)
{
    struct stage84_xnu_iokit_platform_scaffold_contract *contract = &g_stage84_xnu_iokit_platform_scaffold_contract;
    const struct boot_args *args = preflight ? (const struct boot_args *)(uintptr_t)preflight->boot_args_ptr :
        (const struct boot_args *)0;
    const void *root = args ? args->deviceTreeP : (const void *)0;
    uint32_t source_ok;
    uint32_t iokit_dt_ok;
    uint32_t driver_ok;
    uint32_t public_boundary_ok;
    uint32_t pmap_boundary_ok;

    memset(contract, 0, sizeof(*contract));
    contract->version = STAGE84_XNU_IOKIT_PLATFORM_SCAFFOLD_CONTRACT_VERSION;
    contract->size = sizeof(*contract);
    contract->status = STAGE84_STATUS_BASE;
    contract->required_mask = STAGE84_XNU_IOKIT_PLATFORM_SCAFFOLD_REQUIRED_MASK;

    if (!preflight || !args || !root) {
        contract->failure_mask = STAGE84_XNU_IOKIT_PLATFORM_SCAFFOLD_FAIL_SOURCE |
            STAGE84_XNU_IOKIT_PLATFORM_SCAFFOLD_FAIL_SAFETY_BOUNDARY;
        contract->checksum = stage84_iokit_platform_scaffold_checksum(contract);
        stage84_iokit_platform_scaffold_log(contract);
        return 0;
    }

    contract->source_pexpert_status = preflight->xnu_pexpert_hook_readiness_contract_status;
    contract->source_pexpert_satisfied_mask = preflight->xnu_pexpert_hook_readiness_contract_satisfied_mask;
    contract->source_pexpert_failure_mask = preflight->xnu_pexpert_hook_readiness_contract_failure_mask;
    contract->source_pexpert_checksum = preflight->xnu_pexpert_hook_readiness_contract_checksum;
    contract->source_compile_graph_status = preflight->xnu_compile_graph_status;
    contract->source_compile_graph_satisfied_mask = preflight->xnu_compile_graph_satisfied_mask;
    contract->source_compile_graph_failure_mask = preflight->xnu_compile_graph_failure_mask;
    contract->source_compile_graph_checksum = preflight->xnu_compile_graph_checksum;
    contract->source_object_subset_status = preflight->xnu_object_subset_status;
    contract->source_object_subset_failure_mask = preflight->xnu_object_subset_failure_mask;
    contract->source_link_status = preflight->xnu_link_status;
    contract->source_link_failure_mask = preflight->xnu_link_failure_mask;
    contract->source_pmap_transition_status = preflight->xnu_pmap_transition_dryrun_contract_status;
    contract->source_pmap_transition_satisfied_mask = preflight->xnu_pmap_transition_dryrun_contract_satisfied_mask;
    contract->source_pmap_transition_failure_mask = preflight->xnu_pmap_transition_dryrun_contract_failure_mask;
    contract->source_pmap_transition_checksum = preflight->xnu_pmap_transition_dryrun_contract_checksum;
    contract->source_loader_safety_mask = preflight->safety_mask;
    contract->source_loader_safety_required_mask = STAGE84_LOADER_SAFETY_REQUIRED;

    contract->boot_args_ptr = preflight->boot_args_ptr;
    contract->device_tree_ptr = preflight->device_tree_ptr;
    contract->device_tree_length = preflight->device_tree_length;
    contract->apple_dt_semantic_mask = preflight->apple_dt_semantic_mask;

    contract->device_tree_node_ptr = (uint32_t)(uintptr_t)apple_dt_find_child(args->deviceTreeP, args->deviceTreeLength,
                                                                              root, "device-tree");
    contract->iokit_platform_node_ptr = (uint32_t)(uintptr_t)apple_dt_find_child(args->deviceTreeP, args->deviceTreeLength,
                                                                                 root, "iokit-platform-scaffold");
    contract->platform_driver_node_ptr = (uint32_t)(uintptr_t)apple_dt_find_child(args->deviceTreeP, args->deviceTreeLength,
                                                                                  root, "msm8974-platform-driver");

    contract->device_tree_node_prop_count = apple_dt_node_prop_count(args->deviceTreeP, args->deviceTreeLength,
                                                                     (const void *)(uintptr_t)contract->device_tree_node_ptr);
    contract->device_tree_target_type_present = prop_present(args, (const void *)(uintptr_t)contract->device_tree_node_ptr,
                                                             "target-type");
    contract->device_tree_model_present = prop_present(args, (const void *)(uintptr_t)contract->device_tree_node_ptr,
                                                       "model");

    contract->iokit_platform_prop_count = apple_dt_node_prop_count(args->deviceTreeP, args->deviceTreeLength,
                                                                   (const void *)(uintptr_t)contract->iokit_platform_node_ptr);
    contract->iokit_platform_compatible_present = prop_present(args, (const void *)(uintptr_t)contract->iokit_platform_node_ptr,
                                                               "compatible");
    contract->iokit_platform_ioclass_present = prop_present(args, (const void *)(uintptr_t)contract->iokit_platform_node_ptr,
                                                            "IOClass");
    contract->iokit_platform_provider_present = prop_present(args, (const void *)(uintptr_t)contract->iokit_platform_node_ptr,
                                                             "IOProviderClass");
    contract->iokit_platform_device_type_present = prop_present(args, (const void *)(uintptr_t)contract->iokit_platform_node_ptr,
                                                                "device_type");
    contract->iokit_platform_registry_plane_present = prop_present(args, (const void *)(uintptr_t)contract->iokit_platform_node_ptr,
                                                                   "registry-plane");

    contract->platform_driver_prop_count = apple_dt_node_prop_count(args->deviceTreeP, args->deviceTreeLength,
                                                                    (const void *)(uintptr_t)contract->platform_driver_node_ptr);
    contract->platform_driver_compatible_present = prop_present(args, (const void *)(uintptr_t)contract->platform_driver_node_ptr,
                                                                "compatible");
    contract->platform_driver_ioclass_present = prop_present(args, (const void *)(uintptr_t)contract->platform_driver_node_ptr,
                                                             "IOClass");
    contract->platform_driver_provider_present = prop_present(args, (const void *)(uintptr_t)contract->platform_driver_node_ptr,
                                                              "IOProviderClass");
    contract->platform_driver_match_category_present = prop_present(args, (const void *)(uintptr_t)contract->platform_driver_node_ptr,
                                                                    "IOMatchCategory");
    contract->platform_driver_gic_base = reg_word(args, (const void *)(uintptr_t)contract->platform_driver_node_ptr, 0u);
    contract->platform_driver_timer_base = reg_word(args, (const void *)(uintptr_t)contract->platform_driver_node_ptr, 2u);
    contract->platform_driver_timebase_frequency = apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength,
                                                                         (const void *)(uintptr_t)contract->platform_driver_node_ptr,
                                                                         "timebase-frequency", 0u);
    contract->platform_driver_cpu_count = apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength,
                                                                (const void *)(uintptr_t)contract->platform_driver_node_ptr,
                                                                "cpu-count", 0u);

    contract->iokit_reference_mask = preflight->xnu_compile_graph.iokit_reference_mask;
    contract->iokit_runtime_blocked_mask = preflight->xnu_compile_graph.iokit_runtime_blocked_mask;
    contract->iokit_reference_count = preflight->xnu_compile_graph.iokit_reference_count;
    contract->iokit_public_compile_count = preflight->xnu_compile_graph.iokit_public_compile_count;
    contract->iokit_public_link_count = preflight->xnu_compile_graph.iokit_public_link_count;
    contract->iokit_reference_only = preflight->xnu_compile_graph.iokit_reference_only;
    contract->iodevice_tree_support_reference = 1u;
    contract->ioplatform_expert_reference = 1u;
    contract->iocpu_reference = 1u;

    contract->public_pexpert_runtime_blocked = preflight->xnu_pexpert_hook_readiness_contract.public_pexpert_runtime_blocked;
    contract->public_object_subset_ready = (preflight->xnu_object_subset_status == STAGE84_STATUS_OK &&
                                            preflight->xnu_object_subset_failure_mask == 0u) ? 1u : 0u;
    contract->controlled_link_ready = (preflight->xnu_link_status == STAGE84_STATUS_OK &&
                                       preflight->xnu_link_failure_mask == 0u &&
                                       preflight->xnu_link.undefined_symbol_count == 0u) ? 1u : 0u;
    contract->pexpert_hook_ready = (contract->source_pexpert_status == STAGE84_STATUS_OK &&
                                    contract->source_pexpert_satisfied_mask == STAGE84_XNU_PEXPERT_HOOK_READINESS_REQUIRED_MASK &&
                                    contract->source_pexpert_failure_mask == 0u) ? 1u : 0u;
    contract->pmap_transition_ready = (contract->source_pmap_transition_status == STAGE84_STATUS_OK &&
                                       contract->source_pmap_transition_satisfied_mask == STAGE84_XNU_PMAP_TRANSITION_DRYRUN_REQUIRED_MASK &&
                                       contract->source_pmap_transition_failure_mask == 0u) ? 1u : 0u;

    contract->no_iokit_runtime_exec = 1u;
    contract->no_platform_driver_exec = 1u;
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
    contract->proposed_platform_gap_mask = STAGE84_PLATFORM_GAP_REQUIRED_RECORDED & ~STAGE84_PLATFORM_GAP_IOKIT_STACK;
    contract->proposed_pexpert_gap_mask = preflight->pexpert_gap_mask;
    contract->local_only = 1u;
    contract->fail_closed = 1u;

    source_ok = (contract->source_pexpert_status == STAGE84_STATUS_OK &&
                 contract->source_pexpert_failure_mask == 0u &&
                 contract->source_compile_graph_status == STAGE84_STATUS_OK &&
                 contract->source_compile_graph_failure_mask == 0u &&
                 contract->source_object_subset_status == STAGE84_STATUS_OK &&
                 contract->source_object_subset_failure_mask == 0u &&
                 contract->source_link_status == STAGE84_STATUS_OK &&
                 contract->source_link_failure_mask == 0u &&
                 contract->source_pmap_transition_status == STAGE84_STATUS_OK &&
                 contract->source_pmap_transition_failure_mask == 0u &&
                 contract->source_loader_safety_mask == contract->source_loader_safety_required_mask) ? 1u : 0u;
    if (source_ok) {
        contract->satisfied_mask |= STAGE84_XNU_IOKIT_PLATFORM_SCAFFOLD_SAT_SOURCE_PEXPERT;
    } else {
        contract->failure_mask |= STAGE84_XNU_IOKIT_PLATFORM_SCAFFOLD_FAIL_SOURCE;
    }

    if (contract->apple_dt_semantic_mask == STAGE84_DT_READY_REQUIRED) {
        contract->satisfied_mask |= STAGE84_XNU_IOKIT_PLATFORM_SCAFFOLD_SAT_APPLE_DT;
    } else {
        contract->failure_mask |= STAGE84_XNU_IOKIT_PLATFORM_SCAFFOLD_FAIL_DT;
    }

    if (contract->device_tree_node_ptr != 0u && contract->device_tree_node_prop_count >= 4u &&
        contract->device_tree_target_type_present == 1u && contract->device_tree_model_present == 1u) {
        contract->satisfied_mask |= STAGE84_XNU_IOKIT_PLATFORM_SCAFFOLD_SAT_DEVICE_TREE_IMPORT;
    } else {
        contract->failure_mask |= STAGE84_XNU_IOKIT_PLATFORM_SCAFFOLD_FAIL_DT;
    }

    iokit_dt_ok = (contract->iokit_platform_node_ptr != 0u && contract->iokit_platform_prop_count >= 6u &&
                   contract->iokit_platform_compatible_present == 1u &&
                   contract->iokit_platform_ioclass_present == 1u &&
                   contract->iokit_platform_provider_present == 1u &&
                   contract->iokit_platform_device_type_present == 1u &&
                   contract->iokit_platform_registry_plane_present == 1u) ? 1u : 0u;
    if (iokit_dt_ok) {
        contract->satisfied_mask |= STAGE84_XNU_IOKIT_PLATFORM_SCAFFOLD_SAT_IOPLATFORM_NODE;
    } else {
        contract->failure_mask |= STAGE84_XNU_IOKIT_PLATFORM_SCAFFOLD_FAIL_IOKIT;
    }

    if (PE_state_stage84.cpuCount == STAGE84_XNU_IOKIT_PLATFORM_EXPECTED_CPU_COUNT &&
        contract->platform_driver_cpu_count == STAGE84_XNU_IOKIT_PLATFORM_EXPECTED_CPU_COUNT) {
        contract->satisfied_mask |= STAGE84_XNU_IOKIT_PLATFORM_SCAFFOLD_SAT_IOCPU_TOPOLOGY;
    } else {
        contract->failure_mask |= STAGE84_XNU_IOKIT_PLATFORM_SCAFFOLD_FAIL_DRIVER;
    }

    driver_ok = (contract->platform_driver_node_ptr != 0u && contract->platform_driver_prop_count >= 9u &&
                 contract->platform_driver_compatible_present == 1u &&
                 contract->platform_driver_ioclass_present == 1u &&
                 contract->platform_driver_provider_present == 1u &&
                 contract->platform_driver_match_category_present == 1u) ? 1u : 0u;
    if (driver_ok) {
        contract->satisfied_mask |= STAGE84_XNU_IOKIT_PLATFORM_SCAFFOLD_SAT_PLATFORM_DRIVER_NODE;
    } else {
        contract->failure_mask |= STAGE84_XNU_IOKIT_PLATFORM_SCAFFOLD_FAIL_DRIVER;
    }

    if (contract->platform_driver_gic_base == STAGE84_XNU_IOKIT_PLATFORM_EXPECTED_GIC_DIST_BASE &&
        contract->platform_driver_timer_base == STAGE84_XNU_IOKIT_PLATFORM_EXPECTED_TIMER_BASE &&
        contract->platform_driver_timebase_frequency == STAGE84_XNU_IOKIT_PLATFORM_EXPECTED_TIMEBASE_FREQ &&
        contract->platform_driver_cpu_count == STAGE84_XNU_IOKIT_PLATFORM_EXPECTED_CPU_COUNT) {
        contract->satisfied_mask |= STAGE84_XNU_IOKIT_PLATFORM_SCAFFOLD_SAT_MSM8974_DRIVER_FACTS;
    } else {
        contract->failure_mask |= STAGE84_XNU_IOKIT_PLATFORM_SCAFFOLD_FAIL_DRIVER;
    }

    if (preflight->xnu_pexpert_hook_readiness_contract.gic_dist_base == STAGE84_XNU_IOKIT_PLATFORM_EXPECTED_GIC_DIST_BASE &&
        preflight->xnu_pexpert_hook_readiness_contract.pe_timer_base == STAGE84_XNU_IOKIT_PLATFORM_EXPECTED_TIMER_BASE &&
        preflight->xnu_pexpert_hook_readiness_contract.timebase_freq_hz == STAGE84_XNU_IOKIT_PLATFORM_EXPECTED_TIMEBASE_FREQ &&
        preflight->xnu_pexpert_hook_readiness_contract.proposed_interrupt_ready_mask == STAGE84_LOADER_IRQ_READY_REQUIRED) {
        contract->satisfied_mask |= STAGE84_XNU_IOKIT_PLATFORM_SCAFFOLD_SAT_GIC_TIMER_PROVIDER_PLAN;
    } else {
        contract->failure_mask |= STAGE84_XNU_IOKIT_PLATFORM_SCAFFOLD_FAIL_DRIVER;
    }

    if (contract->iokit_reference_mask == STAGE84_XNU_IOKIT_REFERENCE_REQUIRED &&
        contract->iokit_runtime_blocked_mask == STAGE84_XNU_IOKIT_REFERENCE_REQUIRED &&
        contract->iokit_reference_count >= 3u && contract->iokit_public_compile_count == 0u &&
        contract->iokit_public_link_count == 0u && contract->iokit_reference_only == 1u &&
        contract->iodevice_tree_support_reference == 1u && contract->ioplatform_expert_reference == 1u &&
        contract->iocpu_reference == 1u) {
        contract->satisfied_mask |= STAGE84_XNU_IOKIT_PLATFORM_SCAFFOLD_SAT_PUBLIC_IOKIT_REFERENCE;
    } else {
        contract->failure_mask |= STAGE84_XNU_IOKIT_PLATFORM_SCAFFOLD_FAIL_IOKIT;
    }

    public_boundary_ok = (contract->public_pexpert_runtime_blocked == 1u &&
                          contract->public_object_subset_ready == 1u &&
                          contract->controlled_link_ready == 1u &&
                          contract->no_public_xnu_exec == 1u &&
                          contract->no_platform_runtime_exec == 1u) ? 1u : 0u;
    if (public_boundary_ok) {
        contract->satisfied_mask |= STAGE84_XNU_IOKIT_PLATFORM_SCAFFOLD_SAT_PUBLIC_PEXPERT_BOUNDARY |
            STAGE84_XNU_IOKIT_PLATFORM_SCAFFOLD_SAT_OBJECT_LINK_BOUNDARY;
    } else {
        contract->failure_mask |= STAGE84_XNU_IOKIT_PLATFORM_SCAFFOLD_FAIL_PUBLIC_BOUNDARY;
    }

    if (contract->pexpert_hook_ready == 1u && contract->pmap_transition_ready == 1u) {
        contract->satisfied_mask |= STAGE84_XNU_IOKIT_PLATFORM_SCAFFOLD_SAT_PMAP_TRANSITION_BOUNDARY;
    } else {
        contract->failure_mask |= STAGE84_XNU_IOKIT_PLATFORM_SCAFFOLD_FAIL_PMAP_BOUNDARY;
    }

    if (contract->no_iokit_runtime_exec == 1u) {
        contract->satisfied_mask |= STAGE84_XNU_IOKIT_PLATFORM_SCAFFOLD_SAT_NO_IOKIT_RUNTIME_EXEC;
    } else {
        contract->failure_mask |= STAGE84_XNU_IOKIT_PLATFORM_SCAFFOLD_FAIL_SAFETY_BOUNDARY;
    }
    if (contract->no_platform_driver_exec == 1u) {
        contract->satisfied_mask |= STAGE84_XNU_IOKIT_PLATFORM_SCAFFOLD_SAT_NO_PLATFORM_DRIVER_EXEC;
    } else {
        contract->failure_mask |= STAGE84_XNU_IOKIT_PLATFORM_SCAFFOLD_FAIL_SAFETY_BOUNDARY;
    }
    if (contract->no_public_xnu_exec == 1u) {
        contract->satisfied_mask |= STAGE84_XNU_IOKIT_PLATFORM_SCAFFOLD_SAT_NO_PUBLIC_XNU_EXEC;
    } else {
        contract->failure_mask |= STAGE84_XNU_IOKIT_PLATFORM_SCAFFOLD_FAIL_PUBLIC_BOUNDARY;
    }
    if (contract->no_public_pmap_exec == 1u) {
        contract->satisfied_mask |= STAGE84_XNU_IOKIT_PLATFORM_SCAFFOLD_SAT_NO_PUBLIC_PMAP_EXEC;
    } else {
        contract->failure_mask |= STAGE84_XNU_IOKIT_PLATFORM_SCAFFOLD_FAIL_PMAP_BOUNDARY;
    }

    pmap_boundary_ok = (contract->no_live_pmap_tables_installed == 0u &&
                        contract->proposed_workspace_written == 0u &&
                        contract->pmap_ttbr_written == 0u && contract->pmap_ttbcr_written == 0u &&
                        contract->pmap_dacr_written == 0u && contract->pmap_sctlr_written == 0u &&
                        contract->pmap_tlbs_invalidated == 0u && contract->caches_changed == 0u) ? 1u : 0u;
    if (contract->no_live_pmap_tables_installed == 0u) {
        contract->satisfied_mask |= STAGE84_XNU_IOKIT_PLATFORM_SCAFFOLD_SAT_NO_LIVE_PMAP_INSTALL;
    }
    if (contract->proposed_workspace_written == 0u) {
        contract->satisfied_mask |= STAGE84_XNU_IOKIT_PLATFORM_SCAFFOLD_SAT_NO_PROPOSED_PMAP_WRITE;
    }
    if (contract->pmap_ttbr_written == 0u && contract->pmap_ttbcr_written == 0u &&
        contract->pmap_dacr_written == 0u && contract->pmap_sctlr_written == 0u) {
        contract->satisfied_mask |= STAGE84_XNU_IOKIT_PLATFORM_SCAFFOLD_SAT_NO_PMAP_CONTROL_WRITE;
    }
    if (contract->pmap_tlbs_invalidated == 0u && contract->caches_changed == 0u) {
        contract->satisfied_mask |= STAGE84_XNU_IOKIT_PLATFORM_SCAFFOLD_SAT_NO_TLB_CACHE_CHANGE;
    }
    if (!pmap_boundary_ok) {
        contract->failure_mask |= STAGE84_XNU_IOKIT_PLATFORM_SCAFFOLD_FAIL_PMAP_BOUNDARY;
    }

    if (contract->xnu_start_executed == 0u && contract->generated_macho_executed == 0u) {
        contract->satisfied_mask |= STAGE84_XNU_IOKIT_PLATFORM_SCAFFOLD_SAT_NO_XNU_MACHO_EXEC;
    } else {
        contract->failure_mask |= STAGE84_XNU_IOKIT_PLATFORM_SCAFFOLD_FAIL_SAFETY_BOUNDARY;
    }
    if (contract->persistent_write_attempted == 0u) {
        contract->satisfied_mask |= STAGE84_XNU_IOKIT_PLATFORM_SCAFFOLD_SAT_NO_PERSIST_WRITE;
    } else {
        contract->failure_mask |= STAGE84_XNU_IOKIT_PLATFORM_SCAFFOLD_FAIL_SAFETY_BOUNDARY;
    }

    if (contract->proposed_platform_gap_mask == (STAGE84_PLATFORM_GAP_REQUIRED_RECORDED & ~STAGE84_PLATFORM_GAP_IOKIT_STACK) &&
        contract->proposed_pexpert_gap_mask == 0u) {
        contract->satisfied_mask |= STAGE84_XNU_IOKIT_PLATFORM_SCAFFOLD_SAT_PLATFORM_ROLLUP;
    } else {
        contract->failure_mask |= STAGE84_XNU_IOKIT_PLATFORM_SCAFFOLD_FAIL_ROLLUP;
    }

    if (contract->fail_closed == 1u && contract->local_only == 1u) {
        contract->satisfied_mask |= STAGE84_XNU_IOKIT_PLATFORM_SCAFFOLD_SAT_FAIL_CLOSED;
    } else {
        contract->failure_mask |= STAGE84_XNU_IOKIT_PLATFORM_SCAFFOLD_FAIL_SAFETY_BOUNDARY;
    }

    contract->checksum = stage84_iokit_platform_scaffold_checksum(contract);
    if (contract->checksum != stage84_iokit_platform_scaffold_checksum(contract)) {
        contract->failure_mask |= STAGE84_XNU_IOKIT_PLATFORM_SCAFFOLD_FAIL_CHECKSUM;
    }

    if (contract->satisfied_mask == contract->required_mask && contract->failure_mask == 0u) {
        contract->status = STAGE84_STATUS_OK;
    } else {
        contract->status = STAGE84_STATUS_FAIL(contract->failure_mask);
    }

    stage84_iokit_platform_scaffold_log(contract);
    return contract->status == STAGE84_STATUS_OK;
}

const struct stage84_xnu_iokit_platform_scaffold_contract *stage84_xnu_iokit_platform_scaffold_contract_result(void)
{
    return &g_stage84_xnu_iokit_platform_scaffold_contract;
}
