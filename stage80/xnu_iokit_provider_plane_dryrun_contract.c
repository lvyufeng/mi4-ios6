#include "stage80.h"

static struct stage80_xnu_iokit_provider_plane_dryrun_contract g_stage80_xnu_iokit_provider_plane_dryrun_contract;

static uint32_t stage80_iokit_provider_plane_dryrun_checksum(
    volatile const struct stage80_xnu_iokit_provider_plane_dryrun_contract *contract)
{
    volatile const uint32_t *words = (volatile const uint32_t *)(uintptr_t)contract;
    uint32_t count = (uint32_t)(offsetof(struct stage80_xnu_iokit_provider_plane_dryrun_contract, checksum) /
                                sizeof(uint32_t));
    uint32_t checksum = 0u;

    for (uint32_t i = 0; i < count; i++) {
        checksum ^= words[i];
    }

    return checksum;
}

static const char *prop_string(const struct boot_args *args, const void *node, const char *name, uint32_t *len_out)
{
    uint32_t len;
    const char *value;

    if (len_out) {
        *len_out = 0u;
    }
    if (!args || !node) {
        return (const char *)0;
    }

    value = (const char *)apple_dt_get_prop(args->deviceTreeP, args->deviceTreeLength, node, name, &len);
    if (!value || len == 0u) {
        return (const char *)0;
    }
    if (len_out) {
        *len_out = len;
    }
    return value;
}

static uint32_t prop_present(const struct boot_args *args, const void *node, const char *name)
{
    uint32_t len;
    return prop_string(args, node, name, &len) ? 1u : 0u;
}

static uint32_t string_prop_equals(const struct boot_args *args, const void *node,
                                   const char *name, const char *expected)
{
    uint32_t len;
    const char *value = prop_string(args, node, name, &len);

    if (!value || len == 0u || !expected) {
        return 0u;
    }

    return (strcmp(value, expected) == 0) ? 1u : 0u;
}

static void stage80_iokit_provider_plane_dryrun_log(
    const struct stage80_xnu_iokit_provider_plane_dryrun_contract *contract)
{
    xnu_log_kv32("stage80_xnu_iokit_provider_plane_dryrun_contract_status", contract->status);
    xnu_log_kv32("stage80_xnu_iokit_provider_plane_dryrun_contract_required_mask", contract->required_mask);
    xnu_log_kv32("stage80_xnu_iokit_provider_plane_dryrun_contract_satisfied_mask", contract->satisfied_mask);
    xnu_log_kv32("stage80_xnu_iokit_provider_plane_dryrun_contract_failure_mask", contract->failure_mask);
    xnu_log_kv32("stage80_xnu_iokit_provider_plane_dryrun_contract_checksum", contract->checksum);
    xnu_log_kv32("stage80_xnu_iokit_provider_plane_source_registry_status", contract->source_registry_status);
    xnu_log_kv32("stage80_xnu_iokit_provider_plane_source_registry_satisfied_mask", contract->source_registry_satisfied_mask);
    xnu_log_kv32("stage80_xnu_iokit_provider_plane_apple_dt_semantic_mask", contract->apple_dt_semantic_mask);
    xnu_log_kv32("stage80_xnu_iokit_provider_plane_registry_root_props", contract->registry_root_prop_count);
    xnu_log_kv32("stage80_xnu_iokit_provider_plane_platform_props", contract->platform_driver_prop_count);
    xnu_log_kv32("stage80_xnu_iokit_provider_plane_interrupt_props", contract->interrupt_service_prop_count);
    xnu_log_kv32("stage80_xnu_iokit_provider_plane_timer_props", contract->timer_service_prop_count);
    xnu_log_kv32("stage80_xnu_iokit_provider_plane_cpu_props", contract->cpu_service_prop_count);
    xnu_log_kv32("stage80_xnu_iokit_provider_plane_rejected_props", contract->rejected_driver_prop_count);
    xnu_log_kv32("stage80_xnu_iokit_provider_plane_parent_match_mask", contract->provider_parent_match_mask);
    xnu_log_kv32("stage80_xnu_iokit_provider_plane_dependency_mask", contract->provider_dependency_mask);
    xnu_log_kv32("stage80_xnu_iokit_provider_plane_publish_order_mask", contract->provider_publish_order_mask);
    xnu_log_kv32("stage80_xnu_iokit_provider_plane_published_service_mask", contract->provider_published_service_mask);
    xnu_log_kv32("stage80_xnu_iokit_provider_plane_unpublished_service_mask", contract->provider_unpublished_service_mask);
    xnu_log_kv32("stage80_xnu_iokit_provider_plane_matrix", contract->provider_plane_matrix);
    xnu_log_kv32("stage80_xnu_iokit_provider_plane_service_fact_mask", contract->service_fact_mask);
    xnu_log_kv32("stage80_xnu_iokit_provider_plane_root_publish_ordinal", contract->root_publish_ordinal);
    xnu_log_kv32("stage80_xnu_iokit_provider_plane_platform_publish_ordinal", contract->platform_publish_ordinal);
    xnu_log_kv32("stage80_xnu_iokit_provider_plane_interrupt_publish_ordinal", contract->interrupt_publish_ordinal);
    xnu_log_kv32("stage80_xnu_iokit_provider_plane_timer_publish_ordinal", contract->timer_publish_ordinal);
    xnu_log_kv32("stage80_xnu_iokit_provider_plane_cpu_publish_ordinal", contract->cpu_publish_ordinal);
    xnu_log_kv32("stage80_xnu_iokit_provider_plane_rejected_publish_ordinal", contract->rejected_publish_ordinal);
    xnu_log_kv32("stage80_xnu_iokit_provider_plane_publish_order_checksum", contract->publish_order_checksum);
    xnu_log_kv32("stage80_xnu_iokit_provider_plane_provider_candidate_count", contract->provider_candidate_count);
    xnu_log_kv32("stage80_xnu_iokit_provider_plane_service_candidate_count", contract->service_candidate_count);
    xnu_log_kv32("stage80_xnu_iokit_provider_plane_published_service_count", contract->published_service_count);
    xnu_log_kv32("stage80_xnu_iokit_provider_plane_unpublished_candidate_count", contract->unpublished_candidate_count);
    xnu_log_kv32("stage80_xnu_iokit_provider_plane_dryrun_publish_count", contract->dryrun_publish_count);
    xnu_log_kv32("stage80_xnu_iokit_provider_plane_provider_plane_count", contract->provider_plane_count);
    xnu_log_kv32("stage80_xnu_iokit_provider_plane_registry_plane_count", contract->registry_plane_count);
    xnu_log_kv32("stage80_xnu_iokit_provider_plane_attach_deferred_count", contract->attach_deferred_count);
    xnu_log_kv32("stage80_xnu_iokit_provider_plane_start_deferred_count", contract->start_deferred_count);
    xnu_log_kv32("stage80_xnu_iokit_provider_plane_iokit_reference_mask", contract->iokit_reference_mask);
    xnu_log_kv32("stage80_xnu_iokit_provider_plane_iokit_runtime_blocked_mask", contract->iokit_runtime_blocked_mask);
    xnu_log_kv32("stage80_xnu_iokit_provider_plane_public_compile_count", contract->iokit_public_compile_count);
    xnu_log_kv32("stage80_xnu_iokit_provider_plane_public_link_count", contract->iokit_public_link_count);
    xnu_log_kv32("stage80_xnu_iokit_provider_plane_no_iokit_runtime_exec", contract->no_iokit_runtime_exec);
    xnu_log_kv32("stage80_xnu_iokit_provider_plane_no_provider_runtime_exec", contract->no_provider_runtime_exec);
    xnu_log_kv32("stage80_xnu_iokit_provider_plane_no_platform_driver_exec", contract->no_platform_driver_exec);
    xnu_log_kv32("stage80_xnu_iokit_provider_plane_no_live_pmap_tables_installed", contract->no_live_pmap_tables_installed);
    xnu_log_kv32("stage80_xnu_iokit_provider_plane_proposed_workspace_written", contract->proposed_workspace_written);
    xnu_log_kv32("stage80_xnu_iokit_provider_plane_pmap_ttbr_written", contract->pmap_ttbr_written);
    xnu_log_kv32("stage80_xnu_iokit_provider_plane_pmap_tlbs_invalidated", contract->pmap_tlbs_invalidated);
    xnu_log_kv32("stage80_xnu_iokit_provider_plane_caches_changed", contract->caches_changed);
    xnu_log_kv32("stage80_xnu_iokit_provider_plane_persistent_write_attempted", contract->persistent_write_attempted);
    xnu_log_kv32("stage80_xnu_iokit_provider_plane_platform_gap_mask", contract->proposed_platform_gap_mask);
}

int stage80_xnu_iokit_provider_plane_dryrun_contract_selftest(
    const struct stage80_loader_preflight *preflight)
{
    struct stage80_xnu_iokit_provider_plane_dryrun_contract *contract =
        &g_stage80_xnu_iokit_provider_plane_dryrun_contract;
    const struct boot_args *args = preflight ? (const struct boot_args *)(uintptr_t)preflight->boot_args_ptr :
        (const struct boot_args *)0;
    const void *root = args ? args->deviceTreeP : (const void *)0;
    const void *registry_root;
    const void *platform_node;
    const void *interrupt_node;
    const void *timer_node;
    const void *cpu_node;
    const void *rejected_node;
    uint32_t source_ok;
    uint32_t service_nodes_ok;
    uint32_t public_boundary_ok;
    uint32_t pmap_boundary_ok;

    memset(contract, 0, sizeof(*contract));
    contract->version = STAGE80_XNU_IOKIT_PROVIDER_PLANE_DRYRUN_CONTRACT_VERSION;
    contract->size = sizeof(*contract);
    contract->status = STAGE80_STATUS_BASE;
    contract->required_mask = STAGE80_XNU_IOKIT_PROVIDER_PLANE_DRYRUN_REQUIRED_MASK;

    if (!preflight || !args || !root) {
        contract->failure_mask = STAGE80_XNU_IOKIT_PROVIDER_PLANE_DRYRUN_FAIL_SOURCE |
            STAGE80_XNU_IOKIT_PROVIDER_PLANE_DRYRUN_FAIL_SAFETY_BOUNDARY;
        contract->checksum = stage80_iokit_provider_plane_dryrun_checksum(contract);
        stage80_iokit_provider_plane_dryrun_log(contract);
        return 0;
    }

    contract->source_registry_status = preflight->xnu_iokit_registry_service_dryrun_contract_status;
    contract->source_registry_satisfied_mask = preflight->xnu_iokit_registry_service_dryrun_contract_satisfied_mask;
    contract->source_registry_failure_mask = preflight->xnu_iokit_registry_service_dryrun_contract_failure_mask;
    contract->source_registry_checksum = preflight->xnu_iokit_registry_service_dryrun_contract_checksum;
    contract->source_match_status = preflight->xnu_iokit_match_dryrun_contract_status;
    contract->source_match_failure_mask = preflight->xnu_iokit_match_dryrun_contract_failure_mask;
    contract->source_scaffold_status = preflight->xnu_iokit_platform_scaffold_contract_status;
    contract->source_scaffold_failure_mask = preflight->xnu_iokit_platform_scaffold_contract_failure_mask;
    contract->source_pexpert_status = preflight->xnu_pexpert_hook_readiness_contract_status;
    contract->source_pexpert_failure_mask = preflight->xnu_pexpert_hook_readiness_contract_failure_mask;
    contract->source_pmap_transition_status = preflight->xnu_pmap_transition_dryrun_contract_status;
    contract->source_pmap_transition_failure_mask = preflight->xnu_pmap_transition_dryrun_contract_failure_mask;
    contract->source_compile_graph_status = preflight->xnu_compile_graph_status;
    contract->source_compile_graph_failure_mask = preflight->xnu_compile_graph_failure_mask;
    contract->source_object_subset_status = preflight->xnu_object_subset_status;
    contract->source_object_subset_failure_mask = preflight->xnu_object_subset_failure_mask;
    contract->source_link_status = preflight->xnu_link_status;
    contract->source_link_failure_mask = preflight->xnu_link_failure_mask;
    contract->source_loader_safety_mask = preflight->safety_mask;
    contract->source_loader_safety_required_mask = STAGE80_LOADER_SAFETY_REQUIRED;
    contract->boot_args_ptr = preflight->boot_args_ptr;
    contract->device_tree_ptr = preflight->device_tree_ptr;
    contract->device_tree_length = preflight->device_tree_length;
    contract->apple_dt_semantic_mask = preflight->apple_dt_semantic_mask;

    contract->registry_root_node_ptr = preflight->xnu_iokit_registry_service_dryrun_contract.registry_root_node_ptr;
    contract->platform_driver_node_ptr = preflight->xnu_iokit_registry_service_dryrun_contract.platform_driver_node_ptr;
    contract->interrupt_service_node_ptr = preflight->xnu_iokit_registry_service_dryrun_contract.interrupt_service_node_ptr;
    contract->timer_service_node_ptr = preflight->xnu_iokit_registry_service_dryrun_contract.timer_service_node_ptr;
    contract->cpu_service_node_ptr = preflight->xnu_iokit_registry_service_dryrun_contract.cpu_service_node_ptr;
    contract->rejected_driver_node_ptr = preflight->xnu_iokit_registry_service_dryrun_contract.rejected_driver_node_ptr;

    registry_root = (const void *)(uintptr_t)contract->registry_root_node_ptr;
    platform_node = (const void *)(uintptr_t)contract->platform_driver_node_ptr;
    interrupt_node = (const void *)(uintptr_t)contract->interrupt_service_node_ptr;
    timer_node = (const void *)(uintptr_t)contract->timer_service_node_ptr;
    cpu_node = (const void *)(uintptr_t)contract->cpu_service_node_ptr;
    rejected_node = (const void *)(uintptr_t)contract->rejected_driver_node_ptr;

    contract->registry_root_prop_count = apple_dt_node_prop_count(args->deviceTreeP, args->deviceTreeLength,
                                                                  registry_root);
    contract->registry_root_provider_ok =
        (string_prop_equals(args, registry_root, "IOClass", "IOPlatformExpertDevice") == 1u &&
         string_prop_equals(args, registry_root, "registry-plane", "IODeviceTree") == 1u &&
         string_prop_equals(args, registry_root, "IOProviderClass", "IODeviceTree:/") == 1u) ? 1u : 0u;
    contract->platform_driver_prop_count = apple_dt_node_prop_count(args->deviceTreeP, args->deviceTreeLength,
                                                                    platform_node);
    contract->interrupt_service_prop_count = apple_dt_node_prop_count(args->deviceTreeP, args->deviceTreeLength,
                                                                      interrupt_node);
    contract->timer_service_prop_count = apple_dt_node_prop_count(args->deviceTreeP, args->deviceTreeLength,
                                                                  timer_node);
    contract->cpu_service_prop_count = apple_dt_node_prop_count(args->deviceTreeP, args->deviceTreeLength,
                                                                cpu_node);
    contract->rejected_driver_prop_count = apple_dt_node_prop_count(args->deviceTreeP, args->deviceTreeLength,
                                                                    rejected_node);

    contract->source_selected_service_mask = preflight->xnu_iokit_registry_service_dryrun_contract.selected_service_mask;
    contract->source_rejected_service_mask = preflight->xnu_iokit_registry_service_dryrun_contract.rejected_service_mask;
    contract->service_fact_mask = preflight->xnu_iokit_registry_service_dryrun_contract.service_fact_mask;

    if (string_prop_equals(args, platform_node, "IOProviderClass", "IOPlatformExpertDevice")) {
        contract->provider_parent_match_mask |= STAGE80_XNU_IOKIT_REGISTRY_SERVICE_PLATFORM_BIT;
    }
    if (string_prop_equals(args, interrupt_node, "IOProviderClass", "IOPlatformExpertDevice")) {
        contract->provider_parent_match_mask |= STAGE80_XNU_IOKIT_REGISTRY_SERVICE_INTERRUPT_BIT;
    }
    if (string_prop_equals(args, timer_node, "IOProviderClass", "IOPlatformExpertDevice")) {
        contract->provider_parent_match_mask |= STAGE80_XNU_IOKIT_REGISTRY_SERVICE_TIMER_BIT;
    }
    if (string_prop_equals(args, cpu_node, "IOProviderClass", "IOPlatformExpertDevice")) {
        contract->provider_parent_match_mask |= STAGE80_XNU_IOKIT_REGISTRY_SERVICE_CPU_BIT;
    }

    contract->provider_dependency_mask = contract->provider_parent_match_mask &
        preflight->xnu_iokit_registry_service_dryrun_contract.local_only_mask &
        preflight->xnu_iokit_registry_service_dryrun_contract.provider_class_match_mask;
    contract->provider_published_service_mask = contract->source_selected_service_mask & contract->provider_dependency_mask;
    if (prop_present(args, rejected_node, "rejected-candidate") == 1u &&
        !string_prop_equals(args, rejected_node, "IOProviderClass", "IOPlatformExpertDevice")) {
        contract->provider_unpublished_service_mask |= STAGE80_XNU_IOKIT_REGISTRY_SERVICE_REJECTED_BIT;
    }
    contract->provider_plane_matrix = contract->provider_published_service_mask |
        contract->provider_unpublished_service_mask;

    contract->root_publish_ordinal = STAGE80_XNU_IOKIT_PROVIDER_PLANE_ROOT_ORDINAL;
    contract->platform_publish_ordinal = STAGE80_XNU_IOKIT_PROVIDER_PLANE_PLATFORM_ORDINAL;
    contract->interrupt_publish_ordinal = STAGE80_XNU_IOKIT_PROVIDER_PLANE_INTERRUPT_ORDINAL;
    contract->timer_publish_ordinal = STAGE80_XNU_IOKIT_PROVIDER_PLANE_TIMER_ORDINAL;
    contract->cpu_publish_ordinal = STAGE80_XNU_IOKIT_PROVIDER_PLANE_CPU_ORDINAL;
    contract->rejected_publish_ordinal = STAGE80_XNU_IOKIT_PROVIDER_PLANE_REJECTED_ORDINAL;
    if (contract->root_publish_ordinal == 0u) {
        contract->provider_publish_order_mask |= STAGE80_XNU_IOKIT_REGISTRY_SERVICE_PLATFORM_BIT;
    }
    if (contract->platform_publish_ordinal < contract->interrupt_publish_ordinal) {
        contract->provider_publish_order_mask |= STAGE80_XNU_IOKIT_REGISTRY_SERVICE_INTERRUPT_BIT;
    }
    if (contract->interrupt_publish_ordinal < contract->timer_publish_ordinal) {
        contract->provider_publish_order_mask |= STAGE80_XNU_IOKIT_REGISTRY_SERVICE_TIMER_BIT;
    }
    if (contract->timer_publish_ordinal < contract->cpu_publish_ordinal) {
        contract->provider_publish_order_mask |= STAGE80_XNU_IOKIT_REGISTRY_SERVICE_CPU_BIT;
    }
    contract->publish_order_checksum = contract->root_publish_ordinal ^ contract->platform_publish_ordinal ^
        contract->interrupt_publish_ordinal ^ contract->timer_publish_ordinal ^ contract->cpu_publish_ordinal ^
        contract->rejected_publish_ordinal;

    contract->provider_candidate_count = STAGE80_XNU_IOKIT_PROVIDER_PLANE_PROVIDER_COUNT;
    contract->service_candidate_count = STAGE80_XNU_IOKIT_PROVIDER_PLANE_SERVICE_CANDIDATE_COUNT;
    contract->published_service_count = (contract->provider_published_service_mask ==
                                         STAGE80_XNU_IOKIT_REGISTRY_SERVICE_SELECTED_REQUIRED) ?
        STAGE80_XNU_IOKIT_PROVIDER_PLANE_PUBLISHED_COUNT : 0u;
    contract->unpublished_candidate_count = (contract->provider_unpublished_service_mask ==
                                             STAGE80_XNU_IOKIT_REGISTRY_SERVICE_REJECTED_REQUIRED) ?
        STAGE80_XNU_IOKIT_PROVIDER_PLANE_REJECTED_COUNT : 0u;
    contract->dryrun_publish_count = contract->published_service_count;
    contract->provider_plane_count = (contract->registry_root_provider_ok == 1u) ? 1u : 0u;
    contract->registry_plane_count = preflight->xnu_iokit_registry_service_dryrun_contract.registry_plane_count;
    contract->attach_deferred_count = contract->published_service_count;
    contract->start_deferred_count = contract->published_service_count;
    contract->attach_deferred = 1u;
    contract->start_deferred = 1u;

    contract->iokit_reference_mask = preflight->xnu_iokit_registry_service_dryrun_contract.iokit_reference_mask;
    contract->iokit_runtime_blocked_mask = preflight->xnu_iokit_registry_service_dryrun_contract.iokit_runtime_blocked_mask;
    contract->iokit_reference_count = preflight->xnu_iokit_registry_service_dryrun_contract.iokit_reference_count;
    contract->iokit_public_compile_count = preflight->xnu_iokit_registry_service_dryrun_contract.iokit_public_compile_count;
    contract->iokit_public_link_count = preflight->xnu_iokit_registry_service_dryrun_contract.iokit_public_link_count;
    contract->iokit_reference_only = preflight->xnu_iokit_registry_service_dryrun_contract.iokit_reference_only;
    contract->no_iokit_runtime_exec = preflight->xnu_iokit_registry_service_dryrun_contract.no_iokit_runtime_exec;
    contract->no_provider_runtime_exec = 1u;
    contract->no_platform_driver_exec = preflight->xnu_iokit_registry_service_dryrun_contract.no_platform_driver_exec;
    contract->no_public_xnu_exec = preflight->xnu_iokit_registry_service_dryrun_contract.no_public_xnu_exec;
    contract->no_platform_runtime_exec = preflight->xnu_iokit_registry_service_dryrun_contract.no_platform_runtime_exec;
    contract->no_public_pmap_exec = preflight->xnu_iokit_registry_service_dryrun_contract.no_public_pmap_exec;
    contract->no_live_pmap_tables_installed = preflight->xnu_iokit_registry_service_dryrun_contract.no_live_pmap_tables_installed;
    contract->proposed_workspace_written = preflight->xnu_iokit_registry_service_dryrun_contract.proposed_workspace_written;
    contract->pmap_ttbr_written = preflight->xnu_iokit_registry_service_dryrun_contract.pmap_ttbr_written;
    contract->pmap_ttbcr_written = preflight->xnu_iokit_registry_service_dryrun_contract.pmap_ttbcr_written;
    contract->pmap_dacr_written = preflight->xnu_iokit_registry_service_dryrun_contract.pmap_dacr_written;
    contract->pmap_sctlr_written = preflight->xnu_iokit_registry_service_dryrun_contract.pmap_sctlr_written;
    contract->pmap_tlbs_invalidated = preflight->xnu_iokit_registry_service_dryrun_contract.pmap_tlbs_invalidated;
    contract->caches_changed = preflight->xnu_iokit_registry_service_dryrun_contract.caches_changed;
    contract->persistent_write_attempted = preflight->xnu_iokit_registry_service_dryrun_contract.persistent_write_attempted;
    contract->xnu_start_executed = preflight->xnu_iokit_registry_service_dryrun_contract.xnu_start_executed;
    contract->generated_macho_executed = preflight->xnu_iokit_registry_service_dryrun_contract.generated_macho_executed;
    contract->proposed_platform_gap_mask = preflight->platform_gap_mask;
    contract->proposed_pexpert_gap_mask = preflight->pexpert_gap_mask;
    contract->local_only = 1u;
    contract->fail_closed = 1u;

    source_ok = (contract->source_registry_status == STAGE80_STATUS_OK &&
                 contract->source_registry_satisfied_mask == STAGE80_XNU_IOKIT_REGISTRY_SERVICE_DRYRUN_REQUIRED_MASK &&
                 contract->source_registry_failure_mask == 0u &&
                 contract->source_match_status == STAGE80_STATUS_OK &&
                 contract->source_match_failure_mask == 0u &&
                 contract->source_scaffold_status == STAGE80_STATUS_OK &&
                 contract->source_scaffold_failure_mask == 0u &&
                 contract->source_pexpert_status == STAGE80_STATUS_OK &&
                 contract->source_pexpert_failure_mask == 0u &&
                 contract->source_pmap_transition_status == STAGE80_STATUS_OK &&
                 contract->source_pmap_transition_failure_mask == 0u &&
                 contract->source_compile_graph_status == STAGE80_STATUS_OK &&
                 contract->source_compile_graph_failure_mask == 0u &&
                 contract->source_object_subset_status == STAGE80_STATUS_OK &&
                 contract->source_object_subset_failure_mask == 0u &&
                 contract->source_link_status == STAGE80_STATUS_OK &&
                 contract->source_link_failure_mask == 0u &&
                 contract->source_loader_safety_mask == contract->source_loader_safety_required_mask) ? 1u : 0u;
    if (source_ok) {
        contract->satisfied_mask |= STAGE80_XNU_IOKIT_PROVIDER_PLANE_DRYRUN_SAT_SOURCE_REGISTRY;
    } else {
        contract->failure_mask |= STAGE80_XNU_IOKIT_PROVIDER_PLANE_DRYRUN_FAIL_SOURCE;
    }

    if (contract->apple_dt_semantic_mask == STAGE80_DT_READY_REQUIRED) {
        contract->satisfied_mask |= STAGE80_XNU_IOKIT_PROVIDER_PLANE_DRYRUN_SAT_APPLE_DT;
    } else {
        contract->failure_mask |= STAGE80_XNU_IOKIT_PROVIDER_PLANE_DRYRUN_FAIL_DT;
    }

    if (contract->registry_root_node_ptr != 0u && contract->registry_root_prop_count >= 6u &&
        contract->registry_root_provider_ok == 1u && contract->provider_plane_count == 1u &&
        contract->registry_plane_count == 1u) {
        contract->satisfied_mask |= STAGE80_XNU_IOKIT_PROVIDER_PLANE_DRYRUN_SAT_PROVIDER_ROOT;
    } else {
        contract->failure_mask |= STAGE80_XNU_IOKIT_PROVIDER_PLANE_DRYRUN_FAIL_PROVIDER;
    }

    service_nodes_ok = (contract->platform_driver_node_ptr != 0u && contract->platform_driver_prop_count >= 10u &&
                        contract->interrupt_service_node_ptr != 0u && contract->interrupt_service_prop_count >= 10u &&
                        contract->timer_service_node_ptr != 0u && contract->timer_service_prop_count >= 10u &&
                        contract->cpu_service_node_ptr != 0u && contract->cpu_service_prop_count >= 10u &&
                        contract->rejected_driver_node_ptr != 0u && contract->rejected_driver_prop_count >= 8u) ? 1u : 0u;
    if (service_nodes_ok) {
        contract->satisfied_mask |= STAGE80_XNU_IOKIT_PROVIDER_PLANE_DRYRUN_SAT_SERVICE_NODE_SET;
    } else {
        contract->failure_mask |= STAGE80_XNU_IOKIT_PROVIDER_PLANE_DRYRUN_FAIL_PROVIDER;
    }

    if (contract->provider_parent_match_mask == STAGE80_XNU_IOKIT_REGISTRY_SERVICE_SELECTED_REQUIRED) {
        contract->satisfied_mask |= STAGE80_XNU_IOKIT_PROVIDER_PLANE_DRYRUN_SAT_PARENT_MATRIX;
    } else {
        contract->failure_mask |= STAGE80_XNU_IOKIT_PROVIDER_PLANE_DRYRUN_FAIL_PROVIDER;
    }
    if (contract->provider_publish_order_mask == STAGE80_XNU_IOKIT_REGISTRY_SERVICE_SELECTED_REQUIRED &&
        contract->publish_order_checksum == 0xfffffffbu) {
        contract->satisfied_mask |= STAGE80_XNU_IOKIT_PROVIDER_PLANE_DRYRUN_SAT_PUBLISH_ORDER;
    } else {
        contract->failure_mask |= STAGE80_XNU_IOKIT_PROVIDER_PLANE_DRYRUN_FAIL_PUBLISH;
    }
    if (contract->provider_dependency_mask == STAGE80_XNU_IOKIT_REGISTRY_SERVICE_SELECTED_REQUIRED) {
        contract->satisfied_mask |= STAGE80_XNU_IOKIT_PROVIDER_PLANE_DRYRUN_SAT_DEPENDENCY_MATRIX;
    } else {
        contract->failure_mask |= STAGE80_XNU_IOKIT_PROVIDER_PLANE_DRYRUN_FAIL_DEPENDENCY;
    }
    if (contract->service_fact_mask == STAGE80_XNU_IOKIT_REGISTRY_SERVICE_SELECTED_REQUIRED) {
        contract->satisfied_mask |= STAGE80_XNU_IOKIT_PROVIDER_PLANE_DRYRUN_SAT_SERVICE_FACTS;
    } else {
        contract->failure_mask |= STAGE80_XNU_IOKIT_PROVIDER_PLANE_DRYRUN_FAIL_FACTS;
    }
    if (contract->provider_unpublished_service_mask == STAGE80_XNU_IOKIT_REGISTRY_SERVICE_REJECTED_REQUIRED &&
        contract->unpublished_candidate_count == STAGE80_XNU_IOKIT_PROVIDER_PLANE_REJECTED_COUNT &&
        contract->rejected_publish_ordinal == STAGE80_XNU_IOKIT_PROVIDER_PLANE_REJECTED_ORDINAL) {
        contract->satisfied_mask |= STAGE80_XNU_IOKIT_PROVIDER_PLANE_DRYRUN_SAT_REJECT_UNPUBLISHED;
    } else {
        contract->failure_mask |= STAGE80_XNU_IOKIT_PROVIDER_PLANE_DRYRUN_FAIL_PUBLISH;
    }
    if (contract->provider_candidate_count == STAGE80_XNU_IOKIT_PROVIDER_PLANE_PROVIDER_COUNT &&
        contract->service_candidate_count == STAGE80_XNU_IOKIT_PROVIDER_PLANE_SERVICE_CANDIDATE_COUNT &&
        contract->published_service_count == STAGE80_XNU_IOKIT_PROVIDER_PLANE_PUBLISHED_COUNT &&
        contract->unpublished_candidate_count == STAGE80_XNU_IOKIT_PROVIDER_PLANE_REJECTED_COUNT &&
        contract->dryrun_publish_count == STAGE80_XNU_IOKIT_PROVIDER_PLANE_PUBLISHED_COUNT &&
        contract->provider_plane_matrix == (STAGE80_XNU_IOKIT_REGISTRY_SERVICE_SELECTED_REQUIRED |
                                            STAGE80_XNU_IOKIT_REGISTRY_SERVICE_REJECTED_REQUIRED)) {
        contract->satisfied_mask |= STAGE80_XNU_IOKIT_PROVIDER_PLANE_DRYRUN_SAT_DRYRUN_COUNTS;
    } else {
        contract->failure_mask |= STAGE80_XNU_IOKIT_PROVIDER_PLANE_DRYRUN_FAIL_PUBLISH;
    }

    if (contract->attach_deferred == 1u && contract->start_deferred == 1u &&
        contract->attach_deferred_count == STAGE80_XNU_IOKIT_PROVIDER_PLANE_PUBLISHED_COUNT &&
        contract->start_deferred_count == STAGE80_XNU_IOKIT_PROVIDER_PLANE_PUBLISHED_COUNT) {
        contract->satisfied_mask |= STAGE80_XNU_IOKIT_PROVIDER_PLANE_DRYRUN_SAT_ATTACH_START_DEFERRED;
    } else {
        contract->failure_mask |= STAGE80_XNU_IOKIT_PROVIDER_PLANE_DRYRUN_FAIL_SAFETY_BOUNDARY;
    }

    if (contract->iokit_reference_mask == STAGE80_XNU_IOKIT_REFERENCE_REQUIRED &&
        contract->iokit_runtime_blocked_mask == STAGE80_XNU_IOKIT_REFERENCE_REQUIRED &&
        contract->iokit_reference_count >= 3u && contract->iokit_public_compile_count == 0u &&
        contract->iokit_public_link_count == 0u && contract->iokit_reference_only == 1u) {
        contract->satisfied_mask |= STAGE80_XNU_IOKIT_PROVIDER_PLANE_DRYRUN_SAT_IOKIT_REFERENCE;
    } else {
        contract->failure_mask |= STAGE80_XNU_IOKIT_PROVIDER_PLANE_DRYRUN_FAIL_PUBLIC_BOUNDARY;
    }

    public_boundary_ok = (contract->no_public_xnu_exec == 1u && contract->no_platform_runtime_exec == 1u &&
                          contract->no_iokit_runtime_exec == 1u && contract->no_provider_runtime_exec == 1u &&
                          contract->no_platform_driver_exec == 1u &&
                          contract->source_object_subset_status == STAGE80_STATUS_OK &&
                          contract->source_link_status == STAGE80_STATUS_OK) ? 1u : 0u;
    if (public_boundary_ok) {
        contract->satisfied_mask |= STAGE80_XNU_IOKIT_PROVIDER_PLANE_DRYRUN_SAT_PUBLIC_BOUNDARY |
            STAGE80_XNU_IOKIT_PROVIDER_PLANE_DRYRUN_SAT_NO_IOKIT_RUNTIME_EXEC |
            STAGE80_XNU_IOKIT_PROVIDER_PLANE_DRYRUN_SAT_NO_PROVIDER_RUNTIME_EXEC |
            STAGE80_XNU_IOKIT_PROVIDER_PLANE_DRYRUN_SAT_NO_PLATFORM_DRIVER_EXEC |
            STAGE80_XNU_IOKIT_PROVIDER_PLANE_DRYRUN_SAT_NO_PUBLIC_XNU_EXEC;
    } else {
        contract->failure_mask |= STAGE80_XNU_IOKIT_PROVIDER_PLANE_DRYRUN_FAIL_PUBLIC_BOUNDARY;
    }

    if (contract->no_public_pmap_exec == 1u) {
        contract->satisfied_mask |= STAGE80_XNU_IOKIT_PROVIDER_PLANE_DRYRUN_SAT_NO_PUBLIC_PMAP_EXEC;
    } else {
        contract->failure_mask |= STAGE80_XNU_IOKIT_PROVIDER_PLANE_DRYRUN_FAIL_PMAP_BOUNDARY;
    }

    pmap_boundary_ok = (contract->no_live_pmap_tables_installed == 0u &&
                        contract->proposed_workspace_written == 0u &&
                        contract->pmap_ttbr_written == 0u && contract->pmap_ttbcr_written == 0u &&
                        contract->pmap_dacr_written == 0u && contract->pmap_sctlr_written == 0u &&
                        contract->pmap_tlbs_invalidated == 0u && contract->caches_changed == 0u) ? 1u : 0u;
    if (contract->no_live_pmap_tables_installed == 0u) {
        contract->satisfied_mask |= STAGE80_XNU_IOKIT_PROVIDER_PLANE_DRYRUN_SAT_NO_LIVE_PMAP_INSTALL;
    }
    if (contract->proposed_workspace_written == 0u) {
        contract->satisfied_mask |= STAGE80_XNU_IOKIT_PROVIDER_PLANE_DRYRUN_SAT_NO_PROPOSED_PMAP_WRITE;
    }
    if (contract->pmap_ttbr_written == 0u && contract->pmap_ttbcr_written == 0u &&
        contract->pmap_dacr_written == 0u && contract->pmap_sctlr_written == 0u) {
        contract->satisfied_mask |= STAGE80_XNU_IOKIT_PROVIDER_PLANE_DRYRUN_SAT_NO_PMAP_CONTROL_WRITE;
    }
    if (contract->pmap_tlbs_invalidated == 0u && contract->caches_changed == 0u) {
        contract->satisfied_mask |= STAGE80_XNU_IOKIT_PROVIDER_PLANE_DRYRUN_SAT_NO_TLB_CACHE_CHANGE;
    }
    if (pmap_boundary_ok && contract->source_pmap_transition_status == STAGE80_STATUS_OK &&
        contract->source_pmap_transition_failure_mask == 0u) {
        contract->satisfied_mask |= STAGE80_XNU_IOKIT_PROVIDER_PLANE_DRYRUN_SAT_PMAP_BOUNDARY;
    } else {
        contract->failure_mask |= STAGE80_XNU_IOKIT_PROVIDER_PLANE_DRYRUN_FAIL_PMAP_BOUNDARY;
    }

    if (contract->xnu_start_executed == 0u && contract->generated_macho_executed == 0u) {
        contract->satisfied_mask |= STAGE80_XNU_IOKIT_PROVIDER_PLANE_DRYRUN_SAT_NO_XNU_MACHO_EXEC;
    } else {
        contract->failure_mask |= STAGE80_XNU_IOKIT_PROVIDER_PLANE_DRYRUN_FAIL_SAFETY_BOUNDARY;
    }
    if (contract->persistent_write_attempted == 0u) {
        contract->satisfied_mask |= STAGE80_XNU_IOKIT_PROVIDER_PLANE_DRYRUN_SAT_NO_PERSIST_WRITE;
    } else {
        contract->failure_mask |= STAGE80_XNU_IOKIT_PROVIDER_PLANE_DRYRUN_FAIL_SAFETY_BOUNDARY;
    }

    if (contract->proposed_platform_gap_mask == (STAGE80_PLATFORM_GAP_REQUIRED_RECORDED &
                                                 ~STAGE80_PLATFORM_GAP_IOKIT_STACK) &&
        contract->proposed_pexpert_gap_mask == 0u) {
        contract->satisfied_mask |= STAGE80_XNU_IOKIT_PROVIDER_PLANE_DRYRUN_SAT_PLATFORM_ROLLUP;
    } else {
        contract->failure_mask |= STAGE80_XNU_IOKIT_PROVIDER_PLANE_DRYRUN_FAIL_ROLLUP;
    }

    if (contract->local_only == 1u && contract->fail_closed == 1u &&
        contract->provider_published_service_mask == STAGE80_XNU_IOKIT_REGISTRY_SERVICE_SELECTED_REQUIRED &&
        contract->provider_unpublished_service_mask == STAGE80_XNU_IOKIT_REGISTRY_SERVICE_REJECTED_REQUIRED) {
        contract->satisfied_mask |= STAGE80_XNU_IOKIT_PROVIDER_PLANE_DRYRUN_SAT_FAIL_CLOSED;
    } else {
        contract->failure_mask |= STAGE80_XNU_IOKIT_PROVIDER_PLANE_DRYRUN_FAIL_SAFETY_BOUNDARY;
    }

    contract->checksum = stage80_iokit_provider_plane_dryrun_checksum(contract);
    if (contract->checksum != stage80_iokit_provider_plane_dryrun_checksum(contract)) {
        contract->failure_mask |= STAGE80_XNU_IOKIT_PROVIDER_PLANE_DRYRUN_FAIL_CHECKSUM;
    }

    if (contract->satisfied_mask == contract->required_mask && contract->failure_mask == 0u) {
        contract->status = STAGE80_STATUS_OK;
    } else {
        contract->status = STAGE80_STATUS_FAIL(contract->failure_mask);
    }

    stage80_iokit_provider_plane_dryrun_log(contract);
    return contract->status == STAGE80_STATUS_OK;
}

const struct stage80_xnu_iokit_provider_plane_dryrun_contract *
stage80_xnu_iokit_provider_plane_dryrun_contract_result(void)
{
    return &g_stage80_xnu_iokit_provider_plane_dryrun_contract;
}
