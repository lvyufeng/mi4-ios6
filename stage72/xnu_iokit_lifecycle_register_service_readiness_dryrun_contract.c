#include "stage72.h"

static struct stage72_xnu_iokit_lifecycle_register_service_readiness_dryrun_contract
    g_stage72_xnu_iokit_lifecycle_register_service_readiness_dryrun_contract;

static uint32_t stage72_iokit_lifecycle_register_service_dryrun_checksum(
    volatile const struct stage72_xnu_iokit_lifecycle_register_service_readiness_dryrun_contract *contract)
{
    volatile const uint32_t *words = (volatile const uint32_t *)(uintptr_t)contract;
    uint32_t count = (uint32_t)(offsetof(struct stage72_xnu_iokit_lifecycle_register_service_readiness_dryrun_contract,
                                        checksum) / sizeof(uint32_t));
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

static uint32_t u32_prop_equals(const struct boot_args *args, const void *node,
                                const char *name, uint32_t expected)
{
    if (!args || !node) {
        return 0u;
    }

    return (apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength,
                                  node, name, expected ^ 0xffffffffu) == expected) ? 1u : 0u;
}

static void stage72_iokit_lifecycle_register_service_dryrun_log(
    const struct stage72_xnu_iokit_lifecycle_register_service_readiness_dryrun_contract *contract)
{
    xnu_log_kv32("stage72_xnu_iokit_lifecycle_register_service_readiness_dryrun_contract_status", contract->status);
    xnu_log_kv32("stage72_xnu_iokit_lifecycle_register_service_readiness_dryrun_contract_required_mask", contract->required_mask);
    xnu_log_kv32("stage72_xnu_iokit_lifecycle_register_service_readiness_dryrun_contract_satisfied_mask", contract->satisfied_mask);
    xnu_log_kv32("stage72_xnu_iokit_lifecycle_register_service_readiness_dryrun_contract_failure_mask", contract->failure_mask);
    xnu_log_kv32("stage72_xnu_iokit_lifecycle_register_service_readiness_dryrun_contract_checksum", contract->checksum);
    xnu_log_kv32("stage72_xnu_iokit_lifecycle_register_service_source_attach_start_status", contract->source_attach_start_status);
    xnu_log_kv32("stage72_xnu_iokit_lifecycle_register_service_source_attach_start_satisfied_mask", contract->source_attach_start_satisfied_mask);
    xnu_log_kv32("stage72_xnu_iokit_lifecycle_register_service_source_attach_start_failure_mask", contract->source_attach_start_failure_mask);
    xnu_log_kv32("stage72_xnu_iokit_lifecycle_register_service_source_attach_start_checksum", contract->source_attach_start_checksum);
    xnu_log_kv32("stage72_xnu_iokit_lifecycle_register_service_source_selected_attach_start_entry_mask", contract->source_selected_attach_start_entry_mask);
    xnu_log_kv32("stage72_xnu_iokit_lifecycle_register_service_source_rejected_attach_start_mask", contract->source_rejected_attach_start_mask);
    xnu_log_kv32("stage72_xnu_iokit_lifecycle_register_service_source_attach_start_matrix", contract->source_attach_start_matrix);
    xnu_log_kv32("stage72_xnu_iokit_lifecycle_register_service_apple_dt_semantic_mask", contract->apple_dt_semantic_mask);
    xnu_log_kv32("stage72_xnu_iokit_lifecycle_register_service_platform_props", contract->platform_personality_prop_count);
    xnu_log_kv32("stage72_xnu_iokit_lifecycle_register_service_interrupt_props", contract->interrupt_personality_prop_count);
    xnu_log_kv32("stage72_xnu_iokit_lifecycle_register_service_timer_props", contract->timer_personality_prop_count);
    xnu_log_kv32("stage72_xnu_iokit_lifecycle_register_service_cpu_props", contract->cpu_personality_prop_count);
    xnu_log_kv32("stage72_xnu_iokit_lifecycle_register_service_rejected_props", contract->rejected_personality_prop_count);
    xnu_log_kv32("stage72_xnu_iokit_lifecycle_register_service_matched_mask", contract->matched_mask);
    xnu_log_kv32("stage72_xnu_iokit_lifecycle_register_service_provider_published_mask", contract->provider_published_mask);
    xnu_log_kv32("stage72_xnu_iokit_lifecycle_register_service_registry_linked_mask", contract->registry_linked_mask);
    xnu_log_kv32("stage72_xnu_iokit_lifecycle_register_service_topology_linked_mask", contract->topology_linked_mask);
    xnu_log_kv32("stage72_xnu_iokit_lifecycle_register_service_attach_readiness_mask", contract->attach_readiness_mask);
    xnu_log_kv32("stage72_xnu_iokit_lifecycle_register_service_start_readiness_mask", contract->start_readiness_mask);
    xnu_log_kv32("stage72_xnu_iokit_lifecycle_register_service_register_service_readiness_mask", contract->register_service_readiness_mask);
    xnu_log_kv32("stage72_xnu_iokit_lifecycle_register_service_service_registered_mask", contract->service_registered_mask);
    xnu_log_kv32("stage72_xnu_iokit_lifecycle_register_service_notification_ready_mask", contract->notification_ready_mask);
    xnu_log_kv32("stage72_xnu_iokit_lifecycle_register_service_dependency_ready_mask", contract->dependency_ready_mask);
    xnu_log_kv32("stage72_xnu_iokit_lifecycle_register_service_lifecycle_provenance_match_mask", contract->lifecycle_provenance_match_mask);
    xnu_log_kv32("stage72_xnu_iokit_lifecycle_register_service_register_runtime_blocked_mask", contract->register_runtime_blocked_mask);
    xnu_log_kv32("stage72_xnu_iokit_lifecycle_register_service_notification_runtime_blocked_mask", contract->notification_runtime_blocked_mask);
    xnu_log_kv32("stage72_xnu_iokit_lifecycle_register_service_register_order_match_mask", contract->register_order_match_mask);
    xnu_log_kv32("stage72_xnu_iokit_lifecycle_register_service_notification_order_match_mask", contract->notification_order_match_mask);
    xnu_log_kv32("stage72_xnu_iokit_lifecycle_register_service_provider_dependency_match_mask", contract->provider_dependency_match_mask);
    xnu_log_kv32("stage72_xnu_iokit_lifecycle_register_service_register_provider_start_required_mask", contract->register_provider_start_required_mask);
    xnu_log_kv32("stage72_xnu_iokit_lifecycle_register_service_register_provider_start_satisfied_mask", contract->register_provider_start_satisfied_mask);
    xnu_log_kv32("stage72_xnu_iokit_lifecycle_register_service_selected_lifecycle_entry_mask", contract->selected_lifecycle_entry_mask);
    xnu_log_kv32("stage72_xnu_iokit_lifecycle_register_service_rejected_lifecycle_mask", contract->rejected_lifecycle_mask);
    xnu_log_kv32("stage72_xnu_iokit_lifecycle_register_service_lifecycle_matrix", contract->lifecycle_matrix);
    xnu_log_kv32("stage72_xnu_iokit_lifecycle_register_service_register_order_checksum", contract->register_order_checksum);
    xnu_log_kv32("stage72_xnu_iokit_lifecycle_register_service_notification_order_checksum", contract->notification_order_checksum);
    xnu_log_kv32("stage72_xnu_iokit_lifecycle_register_service_path_hash", contract->lifecycle_path_hash);
    xnu_log_kv32("stage72_xnu_iokit_lifecycle_register_service_checksum", contract->lifecycle_checksum);
    xnu_log_kv32("stage72_xnu_iokit_lifecycle_register_service_expected_checksum", contract->expected_lifecycle_checksum);
    xnu_log_kv32("stage72_xnu_iokit_lifecycle_register_service_candidate_count", contract->lifecycle_candidate_count);
    xnu_log_kv32("stage72_xnu_iokit_lifecycle_register_service_selected_entry_count", contract->selected_lifecycle_entry_count);
    xnu_log_kv32("stage72_xnu_iokit_lifecycle_register_service_rejected_entry_count", contract->rejected_lifecycle_entry_count);
    xnu_log_kv32("stage72_xnu_iokit_lifecycle_register_service_register_service_readiness_count", contract->register_service_readiness_count);
    xnu_log_kv32("stage72_xnu_iokit_lifecycle_register_service_service_registered_count", contract->service_registered_count);
    xnu_log_kv32("stage72_xnu_iokit_lifecycle_register_service_notification_ready_count", contract->notification_ready_count);
    xnu_log_kv32("stage72_xnu_iokit_lifecycle_register_service_dependency_ready_count", contract->dependency_ready_count);
    xnu_log_kv32("stage72_xnu_iokit_lifecycle_register_service_rejected_unregistered_count", contract->rejected_unregistered_count);
    xnu_log_kv32("stage72_xnu_iokit_lifecycle_register_service_public_compile_count", contract->iokit_public_compile_count);
    xnu_log_kv32("stage72_xnu_iokit_lifecycle_register_service_public_link_count", contract->iokit_public_link_count);
    xnu_log_kv32("stage72_xnu_iokit_lifecycle_register_service_no_iokit_runtime_exec", contract->no_iokit_runtime_exec);
    xnu_log_kv32("stage72_xnu_iokit_lifecycle_register_service_no_register_service_runtime_exec", contract->no_register_service_runtime_exec);
    xnu_log_kv32("stage72_xnu_iokit_lifecycle_register_service_no_notification_runtime_exec", contract->no_notification_runtime_exec);
    xnu_log_kv32("stage72_xnu_iokit_lifecycle_register_service_no_live_pmap_tables_installed", contract->no_live_pmap_tables_installed);
    xnu_log_kv32("stage72_xnu_iokit_lifecycle_register_service_proposed_workspace_written", contract->proposed_workspace_written);
    xnu_log_kv32("stage72_xnu_iokit_lifecycle_register_service_pmap_ttbr_written", contract->pmap_ttbr_written);
    xnu_log_kv32("stage72_xnu_iokit_lifecycle_register_service_pmap_tlbs_invalidated", contract->pmap_tlbs_invalidated);
    xnu_log_kv32("stage72_xnu_iokit_lifecycle_register_service_caches_changed", contract->caches_changed);
    xnu_log_kv32("stage72_xnu_iokit_lifecycle_register_service_persistent_write_attempted", contract->persistent_write_attempted);
}

static void selected_masks_for_node(const struct boot_args *args, const void *node, uint32_t bit,
                                    const char *provider_path, uint32_t provider_ordinal,
                                    uint32_t order, uint32_t provider_required,
                                    struct stage72_xnu_iokit_lifecycle_register_service_readiness_dryrun_contract *contract)
{
    uint32_t provider_path_ok = string_prop_equals(args, node, "register-provider-path", provider_path);
    uint32_t provider_ordinal_ok = u32_prop_equals(args, node, "register-provider-ordinal", provider_ordinal);
    uint32_t provider_required_ok = u32_prop_equals(args, node, "register-provider-start-req", provider_required);
    uint32_t dependency_ok = u32_prop_equals(args, node, "register-dependency-ready", 1u);

    if (u32_prop_equals(args, node, "lifecycle-regsvc-dryrun", 1u) &&
        u32_prop_equals(args, node, "lifecycle-state-matched", 1u)) {
        contract->matched_mask |= bit;
    }
    if (u32_prop_equals(args, node, "lifecycle-provider-published", 1u)) {
        contract->provider_published_mask |= bit;
    }
    if (u32_prop_equals(args, node, "lifecycle-registry-linked", 1u)) {
        contract->registry_linked_mask |= bit;
    }
    if (u32_prop_equals(args, node, "lifecycle-topology-linked", 1u)) {
        contract->topology_linked_mask |= bit;
    }
    if (u32_prop_equals(args, node, "attach-readiness", 1u)) {
        contract->attach_readiness_mask |= bit;
    }
    if (u32_prop_equals(args, node, "start-readiness", 1u)) {
        contract->start_readiness_mask |= bit;
    }
    if (u32_prop_equals(args, node, "register-service-readiness", 1u)) {
        contract->register_service_readiness_mask |= bit;
    }
    if (u32_prop_equals(args, node, "service-registered", 1u)) {
        contract->service_registered_mask |= bit;
    }
    if (u32_prop_equals(args, node, "notification-ready", 1u)) {
        contract->notification_ready_mask |= bit;
    }
    if (dependency_ok != 0u) {
        contract->dependency_ready_mask |= bit;
    }
    if (string_prop_equals(args, node, "lifecycle-provenance", "stage72-attach-start-readiness")) {
        contract->lifecycle_provenance_match_mask |= bit;
    }
    if (u32_prop_equals(args, node, "register-service-runtime-exec", 0u)) {
        contract->register_runtime_blocked_mask |= bit;
    }
    if (u32_prop_equals(args, node, "notification-runtime-exec", 0u)) {
        contract->notification_runtime_blocked_mask |= bit;
    }
    if (u32_prop_equals(args, node, "register-service-order", order)) {
        contract->register_order_match_mask |= bit;
    }
    if (u32_prop_equals(args, node, "notification-order", order)) {
        contract->notification_order_match_mask |= bit;
    }
    if (provider_required != 0u) {
        contract->register_provider_start_required_mask |= bit;
        if (provider_required_ok != 0u) {
            contract->register_provider_start_satisfied_mask |= bit;
        }
    }
    if (provider_path_ok != 0u && provider_ordinal_ok != 0u && provider_required_ok != 0u && dependency_ok != 0u) {
        contract->provider_dependency_match_mask |= bit;
    }
}

int stage72_xnu_iokit_lifecycle_register_service_readiness_dryrun_contract_selftest(
    const struct stage72_loader_preflight *preflight)
{
    struct stage72_xnu_iokit_lifecycle_register_service_readiness_dryrun_contract *contract =
        &g_stage72_xnu_iokit_lifecycle_register_service_readiness_dryrun_contract;
    const struct boot_args *args = preflight ? (const struct boot_args *)(uintptr_t)preflight->boot_args_ptr :
        (const struct boot_args *)0;
    const void *root = args ? args->deviceTreeP : (const void *)0;
    const void *platform_node;
    const void *interrupt_node;
    const void *timer_node;
    const void *cpu_node;
    const void *rejected_node;
    uint32_t source_ok;
    uint32_t personality_nodes_ok;
    uint32_t order_ok;
    uint32_t dependency_ok;
    uint32_t public_boundary_ok;
    uint32_t pmap_boundary_ok;

    memset(contract, 0, sizeof(*contract));
    contract->version = STAGE72_XNU_IOKIT_LIFECYCLE_REGISTER_SERVICE_DRYRUN_CONTRACT_VERSION;
    contract->size = sizeof(*contract);
    contract->status = STAGE72_STATUS_BASE;
    contract->required_mask = STAGE72_XNU_IOKIT_LIFECYCLE_REGISTER_SERVICE_DRYRUN_REQUIRED_MASK;

    if (!preflight || !args || !root) {
        contract->failure_mask = STAGE72_XNU_IOKIT_LIFECYCLE_REGISTER_SERVICE_DRYRUN_FAIL_SOURCE |
            STAGE72_XNU_IOKIT_LIFECYCLE_REGISTER_SERVICE_DRYRUN_FAIL_SAFETY_BOUNDARY;
        contract->checksum = stage72_iokit_lifecycle_register_service_dryrun_checksum(contract);
        stage72_iokit_lifecycle_register_service_dryrun_log(contract);
        return 0;
    }

    contract->source_attach_start_status = preflight->xnu_iokit_attach_start_readiness_dryrun_contract_status;
    contract->source_attach_start_satisfied_mask =
        preflight->xnu_iokit_attach_start_readiness_dryrun_contract_satisfied_mask;
    contract->source_attach_start_failure_mask =
        preflight->xnu_iokit_attach_start_readiness_dryrun_contract_failure_mask;
    contract->source_attach_start_contract_checksum = preflight->xnu_iokit_attach_start_readiness_dryrun_contract_checksum;
    contract->source_attach_start_checksum = preflight->xnu_iokit_attach_start_readiness_dryrun_contract.attach_start_checksum;
    contract->source_selected_attach_start_entry_mask =
        preflight->xnu_iokit_attach_start_readiness_dryrun_contract.selected_attach_start_entry_mask;
    contract->source_rejected_attach_start_mask =
        preflight->xnu_iokit_attach_start_readiness_dryrun_contract.rejected_attach_start_mask;
    contract->source_attach_start_matrix =
        preflight->xnu_iokit_attach_start_readiness_dryrun_contract.attach_start_matrix;
    contract->source_attach_readiness_mask =
        preflight->xnu_iokit_attach_start_readiness_dryrun_contract.attach_readiness_mask;
    contract->source_start_readiness_mask =
        preflight->xnu_iokit_attach_start_readiness_dryrun_contract.start_readiness_mask;
    contract->source_topology_status = preflight->xnu_iokit_registry_topology_dryrun_contract_status;
    contract->source_property_inheritance_status = preflight->xnu_iokit_property_inheritance_dryrun_contract_status;
    contract->source_catalog_status = preflight->xnu_iokit_catalog_property_dryrun_contract_status;
    contract->source_provider_status = preflight->xnu_iokit_provider_plane_dryrun_contract_status;
    contract->source_registry_status = preflight->xnu_iokit_registry_service_dryrun_contract_status;
    contract->source_match_status = preflight->xnu_iokit_match_dryrun_contract_status;
    contract->source_scaffold_status = preflight->xnu_iokit_platform_scaffold_contract_status;
    contract->source_pexpert_status = preflight->xnu_pexpert_hook_readiness_contract_status;
    contract->source_pmap_transition_status = preflight->xnu_pmap_transition_dryrun_contract_status;
    contract->source_compile_graph_status = preflight->xnu_compile_graph_status;
    contract->source_object_subset_status = preflight->xnu_object_subset_status;
    contract->source_link_status = preflight->xnu_link_status;
    contract->source_loader_safety_mask = preflight->safety_mask;
    contract->source_loader_safety_required_mask = STAGE72_LOADER_SAFETY_REQUIRED;
    contract->boot_args_ptr = preflight->boot_args_ptr;
    contract->device_tree_ptr = preflight->device_tree_ptr;
    contract->device_tree_length = preflight->device_tree_length;
    contract->apple_dt_semantic_mask = preflight->apple_dt_semantic_mask;

    contract->platform_personality_node_ptr = (uint32_t)(uintptr_t)apple_dt_find_child(args->deviceTreeP,
                                                                                       args->deviceTreeLength,
                                                                                       root,
                                                                                       "stage72-platform-personality");
    contract->interrupt_personality_node_ptr = (uint32_t)(uintptr_t)apple_dt_find_child(args->deviceTreeP,
                                                                                        args->deviceTreeLength,
                                                                                        root,
                                                                                        "stage72-interrupt-personality");
    contract->timer_personality_node_ptr = (uint32_t)(uintptr_t)apple_dt_find_child(args->deviceTreeP,
                                                                                    args->deviceTreeLength,
                                                                                    root,
                                                                                    "stage72-timer-personality");
    contract->cpu_personality_node_ptr = (uint32_t)(uintptr_t)apple_dt_find_child(args->deviceTreeP,
                                                                                  args->deviceTreeLength,
                                                                                  root,
                                                                                  "stage72-cpu-personality");
    contract->rejected_personality_node_ptr = (uint32_t)(uintptr_t)apple_dt_find_child(args->deviceTreeP,
                                                                                       args->deviceTreeLength,
                                                                                       root,
                                                                                       "stage72-rejected-personality");

    platform_node = (const void *)(uintptr_t)contract->platform_personality_node_ptr;
    interrupt_node = (const void *)(uintptr_t)contract->interrupt_personality_node_ptr;
    timer_node = (const void *)(uintptr_t)contract->timer_personality_node_ptr;
    cpu_node = (const void *)(uintptr_t)contract->cpu_personality_node_ptr;
    rejected_node = (const void *)(uintptr_t)contract->rejected_personality_node_ptr;

    contract->platform_personality_prop_count = apple_dt_node_prop_count(args->deviceTreeP, args->deviceTreeLength,
                                                                         platform_node);
    contract->interrupt_personality_prop_count = apple_dt_node_prop_count(args->deviceTreeP, args->deviceTreeLength,
                                                                          interrupt_node);
    contract->timer_personality_prop_count = apple_dt_node_prop_count(args->deviceTreeP, args->deviceTreeLength,
                                                                      timer_node);
    contract->cpu_personality_prop_count = apple_dt_node_prop_count(args->deviceTreeP, args->deviceTreeLength,
                                                                    cpu_node);
    contract->rejected_personality_prop_count = apple_dt_node_prop_count(args->deviceTreeP, args->deviceTreeLength,
                                                                         rejected_node);

    selected_masks_for_node(args, platform_node, STAGE72_XNU_IOKIT_REGISTRY_SERVICE_PLATFORM_BIT,
                            "IODeviceTree:/", STAGE72_XNU_IOKIT_REGISTRY_TOPOLOGY_REJECTED_ORDINAL,
                            STAGE72_XNU_IOKIT_REGISTRY_TOPOLOGY_PLATFORM_ORDINAL, 0u, contract);
    selected_masks_for_node(args, interrupt_node, STAGE72_XNU_IOKIT_REGISTRY_SERVICE_INTERRUPT_BIT,
                            "IODeviceTree:/stage72-platform-personality",
                            STAGE72_XNU_IOKIT_REGISTRY_TOPOLOGY_PLATFORM_ORDINAL,
                            STAGE72_XNU_IOKIT_REGISTRY_TOPOLOGY_INTERRUPT_ORDINAL, 1u, contract);
    selected_masks_for_node(args, timer_node, STAGE72_XNU_IOKIT_REGISTRY_SERVICE_TIMER_BIT,
                            "IODeviceTree:/stage72-platform-personality",
                            STAGE72_XNU_IOKIT_REGISTRY_TOPOLOGY_PLATFORM_ORDINAL,
                            STAGE72_XNU_IOKIT_REGISTRY_TOPOLOGY_TIMER_ORDINAL, 1u, contract);
    selected_masks_for_node(args, cpu_node, STAGE72_XNU_IOKIT_REGISTRY_SERVICE_CPU_BIT,
                            "IODeviceTree:/stage72-platform-personality",
                            STAGE72_XNU_IOKIT_REGISTRY_TOPOLOGY_PLATFORM_ORDINAL,
                            STAGE72_XNU_IOKIT_REGISTRY_TOPOLOGY_CPU_ORDINAL, 1u, contract);

    contract->platform_register_provider_ordinal = apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength,
                                                                         platform_node, "register-provider-ordinal", 0u);
    contract->interrupt_register_provider_ordinal = apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength,
                                                                          interrupt_node, "register-provider-ordinal", 0xffffffffu);
    contract->timer_register_provider_ordinal = apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength,
                                                                      timer_node, "register-provider-ordinal", 0xffffffffu);
    contract->cpu_register_provider_ordinal = apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength,
                                                                    cpu_node, "register-provider-ordinal", 0xffffffffu);
    contract->rejected_register_provider_ordinal = apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength,
                                                                         rejected_node, "register-provider-ordinal", 0u);
    contract->platform_register_order = apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength,
                                                              platform_node, "register-service-order", 0xffffffffu);
    contract->interrupt_register_order = apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength,
                                                               interrupt_node, "register-service-order", 0xffffffffu);
    contract->timer_register_order = apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength,
                                                           timer_node, "register-service-order", 0xffffffffu);
    contract->cpu_register_order = apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength,
                                                         cpu_node, "register-service-order", 0xffffffffu);
    contract->rejected_register_order = apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength,
                                                              rejected_node, "register-service-order", 0u);
    contract->platform_notification_order = apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength,
                                                                  platform_node, "notification-order", 0xffffffffu);
    contract->interrupt_notification_order = apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength,
                                                                   interrupt_node, "notification-order", 0xffffffffu);
    contract->timer_notification_order = apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength,
                                                               timer_node, "notification-order", 0xffffffffu);
    contract->cpu_notification_order = apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength,
                                                             cpu_node, "notification-order", 0xffffffffu);
    contract->rejected_notification_order = apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength,
                                                                  rejected_node, "notification-order", 0u);

    contract->selected_lifecycle_entry_mask = contract->matched_mask & contract->provider_published_mask &
        contract->registry_linked_mask & contract->topology_linked_mask & contract->attach_readiness_mask &
        contract->start_readiness_mask & contract->register_service_readiness_mask & contract->service_registered_mask &
        contract->notification_ready_mask & contract->dependency_ready_mask & contract->lifecycle_provenance_match_mask &
        contract->register_runtime_blocked_mask & contract->notification_runtime_blocked_mask &
        contract->register_order_match_mask & contract->notification_order_match_mask &
        contract->provider_dependency_match_mask & contract->source_selected_attach_start_entry_mask;

    if (u32_prop_equals(args, rejected_node, "provider-plane-published", 0u) &&
        u32_prop_equals(args, rejected_node, "registry-entry-linked", 0u) &&
        u32_prop_equals(args, rejected_node, "registry-entry-attached", 0u) &&
        u32_prop_equals(args, rejected_node, "registry-topology-dryrun", 0u) &&
        u32_prop_equals(args, rejected_node, "attach-start-readiness-dryrun", 0u) &&
        u32_prop_equals(args, rejected_node, "attach-readiness", 0u) &&
        u32_prop_equals(args, rejected_node, "start-readiness", 0u) &&
        u32_prop_equals(args, rejected_node, "lifecycle-regsvc-dryrun", 0u) &&
        u32_prop_equals(args, rejected_node, "lifecycle-state-matched", 0u) &&
        u32_prop_equals(args, rejected_node, "lifecycle-provider-published", 0u) &&
        u32_prop_equals(args, rejected_node, "lifecycle-registry-linked", 0u) &&
        u32_prop_equals(args, rejected_node, "lifecycle-topology-linked", 0u) &&
        u32_prop_equals(args, rejected_node, "register-service-readiness", 0u) &&
        u32_prop_equals(args, rejected_node, "service-registered", 0u) &&
        u32_prop_equals(args, rejected_node, "register-service-runtime-exec", 0u) &&
        u32_prop_equals(args, rejected_node, "notification-ready", 0u) &&
        u32_prop_equals(args, rejected_node, "notification-runtime-exec", 0u) &&
        string_prop_equals(args, rejected_node, "register-provider-path", "IODeviceTree:/unlinked") &&
        u32_prop_equals(args, rejected_node, "register-provider-ordinal",
                        STAGE72_XNU_IOKIT_REGISTRY_TOPOLOGY_REJECTED_ORDINAL) &&
        u32_prop_equals(args, rejected_node, "register-provider-start-req", 0u) &&
        u32_prop_equals(args, rejected_node, "register-service-order", STAGE72_XNU_IOKIT_REGISTRY_TOPOLOGY_REJECTED_ORDINAL) &&
        u32_prop_equals(args, rejected_node, "notification-order", STAGE72_XNU_IOKIT_REGISTRY_TOPOLOGY_REJECTED_ORDINAL) &&
        u32_prop_equals(args, rejected_node, "register-dependency-ready", 0u) &&
        string_prop_equals(args, rejected_node, "lifecycle-provenance", "stage72-rejected-unregistered") &&
        u32_prop_equals(args, rejected_node, "rejected-candidate", 1u) &&
        u32_prop_equals(args, rejected_node, "stage-owned-local-only", 1u)) {
        contract->rejected_lifecycle_mask |= STAGE72_XNU_IOKIT_REGISTRY_SERVICE_REJECTED_BIT;
    }

    contract->lifecycle_matrix = contract->selected_lifecycle_entry_mask | contract->rejected_lifecycle_mask;
    contract->register_order_checksum = contract->platform_register_order ^ contract->interrupt_register_order ^
        contract->timer_register_order ^ contract->cpu_register_order ^ contract->rejected_register_order;
    contract->notification_order_checksum = contract->platform_notification_order ^ contract->interrupt_notification_order ^
        contract->timer_notification_order ^ contract->cpu_notification_order ^ contract->rejected_notification_order;
    contract->lifecycle_path_hash = STAGE72_XNU_IOKIT_LIFECYCLE_REGISTER_SERVICE_PATH_HASH;
    contract->lifecycle_candidate_count = STAGE72_XNU_IOKIT_LIFECYCLE_REGISTER_SERVICE_CANDIDATE_COUNT;
    contract->selected_lifecycle_entry_count = (contract->selected_lifecycle_entry_mask ==
                                                STAGE72_XNU_IOKIT_REGISTRY_SERVICE_SELECTED_REQUIRED) ?
        STAGE72_XNU_IOKIT_LIFECYCLE_REGISTER_SERVICE_ENTRY_COUNT : 0u;
    contract->rejected_lifecycle_entry_count = (contract->rejected_lifecycle_mask ==
                                                STAGE72_XNU_IOKIT_REGISTRY_SERVICE_REJECTED_REQUIRED) ?
        STAGE72_XNU_IOKIT_LIFECYCLE_REGISTER_SERVICE_REJECTED_COUNT : 0u;
    contract->register_service_readiness_count = contract->selected_lifecycle_entry_count;
    contract->service_registered_count = contract->selected_lifecycle_entry_count;
    contract->notification_ready_count = contract->selected_lifecycle_entry_count;
    contract->dependency_ready_count = contract->selected_lifecycle_entry_count;
    contract->rejected_unregistered_count = contract->rejected_lifecycle_entry_count;
    contract->register_provider_required_count =
        (contract->register_provider_start_required_mask == (STAGE72_XNU_IOKIT_REGISTRY_SERVICE_INTERRUPT_BIT |
                                                             STAGE72_XNU_IOKIT_REGISTRY_SERVICE_TIMER_BIT |
                                                             STAGE72_XNU_IOKIT_REGISTRY_SERVICE_CPU_BIT)) ? 3u : 0u;
    contract->register_provider_satisfied_count =
        ((contract->register_provider_start_satisfied_mask & (STAGE72_XNU_IOKIT_REGISTRY_SERVICE_INTERRUPT_BIT |
                                                              STAGE72_XNU_IOKIT_REGISTRY_SERVICE_TIMER_BIT |
                                                              STAGE72_XNU_IOKIT_REGISTRY_SERVICE_CPU_BIT)) ==
         (STAGE72_XNU_IOKIT_REGISTRY_SERVICE_INTERRUPT_BIT |
          STAGE72_XNU_IOKIT_REGISTRY_SERVICE_TIMER_BIT |
          STAGE72_XNU_IOKIT_REGISTRY_SERVICE_CPU_BIT)) ? 3u : 0u;
    contract->lifecycle_checksum = contract->source_attach_start_checksum ^
        contract->source_selected_attach_start_entry_mask ^ contract->source_rejected_attach_start_mask ^
        contract->source_attach_start_matrix ^ contract->source_attach_readiness_mask ^ contract->source_start_readiness_mask ^
        contract->matched_mask ^ contract->provider_published_mask ^ contract->registry_linked_mask ^
        contract->topology_linked_mask ^ contract->attach_readiness_mask ^ contract->start_readiness_mask ^
        contract->register_service_readiness_mask ^ contract->service_registered_mask ^ contract->notification_ready_mask ^
        contract->dependency_ready_mask ^ contract->lifecycle_provenance_match_mask ^
        contract->register_runtime_blocked_mask ^ contract->notification_runtime_blocked_mask ^
        contract->register_order_match_mask ^ contract->notification_order_match_mask ^
        contract->provider_dependency_match_mask ^ contract->register_provider_start_required_mask ^
        contract->register_provider_start_satisfied_mask ^ contract->selected_lifecycle_entry_mask ^
        contract->rejected_lifecycle_mask ^ contract->lifecycle_matrix ^ contract->register_order_checksum ^
        contract->notification_order_checksum ^ contract->lifecycle_path_hash ^ contract->lifecycle_candidate_count ^
        contract->selected_lifecycle_entry_count ^ contract->rejected_lifecycle_entry_count ^
        contract->register_service_readiness_count ^ contract->service_registered_count ^
        contract->notification_ready_count ^ contract->dependency_ready_count ^ contract->rejected_unregistered_count ^
        contract->register_provider_required_count ^ contract->register_provider_satisfied_count;
    contract->expected_lifecycle_checksum = STAGE72_XNU_IOKIT_LIFECYCLE_REGISTER_SERVICE_EXPECTED_CHECKSUM;

    contract->iokit_reference_mask = preflight->xnu_iokit_attach_start_readiness_dryrun_contract.iokit_reference_mask;
    contract->iokit_runtime_blocked_mask = preflight->xnu_iokit_attach_start_readiness_dryrun_contract.iokit_runtime_blocked_mask;
    contract->iokit_reference_count = preflight->xnu_iokit_attach_start_readiness_dryrun_contract.iokit_reference_count;
    contract->iokit_public_compile_count = preflight->xnu_iokit_attach_start_readiness_dryrun_contract.iokit_public_compile_count;
    contract->iokit_public_link_count = preflight->xnu_iokit_attach_start_readiness_dryrun_contract.iokit_public_link_count;
    contract->iokit_reference_only = preflight->xnu_iokit_attach_start_readiness_dryrun_contract.iokit_reference_only;
    contract->no_iokit_runtime_exec = preflight->xnu_iokit_attach_start_readiness_dryrun_contract.no_iokit_runtime_exec;
    contract->no_catalog_runtime_exec = preflight->xnu_iokit_attach_start_readiness_dryrun_contract.no_catalog_runtime_exec;
    contract->no_provider_runtime_exec = preflight->xnu_iokit_attach_start_readiness_dryrun_contract.no_provider_runtime_exec;
    contract->no_registry_entry_runtime_exec =
        preflight->xnu_iokit_attach_start_readiness_dryrun_contract.no_registry_entry_runtime_exec;
    contract->no_registry_topology_runtime_exec =
        preflight->xnu_iokit_attach_start_readiness_dryrun_contract.no_registry_topology_runtime_exec;
    contract->no_property_runtime_exec = preflight->xnu_iokit_attach_start_readiness_dryrun_contract.no_property_runtime_exec;
    contract->no_attach_runtime_exec = preflight->xnu_iokit_attach_start_readiness_dryrun_contract.no_attach_runtime_exec;
    contract->no_start_runtime_exec = preflight->xnu_iokit_attach_start_readiness_dryrun_contract.no_start_runtime_exec;
    contract->no_register_service_runtime_exec = 1u;
    contract->no_notification_runtime_exec = 1u;
    contract->no_platform_driver_exec = preflight->xnu_iokit_attach_start_readiness_dryrun_contract.no_platform_driver_exec;
    contract->no_public_xnu_exec = preflight->xnu_iokit_attach_start_readiness_dryrun_contract.no_public_xnu_exec;
    contract->no_platform_runtime_exec = preflight->xnu_iokit_attach_start_readiness_dryrun_contract.no_platform_runtime_exec;
    contract->no_public_pmap_exec = preflight->xnu_iokit_attach_start_readiness_dryrun_contract.no_public_pmap_exec;
    contract->no_live_pmap_tables_installed =
        preflight->xnu_iokit_attach_start_readiness_dryrun_contract.no_live_pmap_tables_installed;
    contract->proposed_workspace_written = preflight->xnu_iokit_attach_start_readiness_dryrun_contract.proposed_workspace_written;
    contract->pmap_ttbr_written = preflight->xnu_iokit_attach_start_readiness_dryrun_contract.pmap_ttbr_written;
    contract->pmap_ttbcr_written = preflight->xnu_iokit_attach_start_readiness_dryrun_contract.pmap_ttbcr_written;
    contract->pmap_dacr_written = preflight->xnu_iokit_attach_start_readiness_dryrun_contract.pmap_dacr_written;
    contract->pmap_sctlr_written = preflight->xnu_iokit_attach_start_readiness_dryrun_contract.pmap_sctlr_written;
    contract->pmap_tlbs_invalidated = preflight->xnu_iokit_attach_start_readiness_dryrun_contract.pmap_tlbs_invalidated;
    contract->caches_changed = preflight->xnu_iokit_attach_start_readiness_dryrun_contract.caches_changed;
    contract->persistent_write_attempted = preflight->xnu_iokit_attach_start_readiness_dryrun_contract.persistent_write_attempted;
    contract->xnu_start_executed = preflight->xnu_iokit_attach_start_readiness_dryrun_contract.xnu_start_executed;
    contract->generated_macho_executed = preflight->xnu_iokit_attach_start_readiness_dryrun_contract.generated_macho_executed;
    contract->proposed_platform_gap_mask = preflight->platform_gap_mask;
    contract->proposed_pexpert_gap_mask = preflight->pexpert_gap_mask;
    contract->local_only = 1u;
    contract->fail_closed = 1u;

    source_ok = (contract->source_attach_start_status == STAGE72_STATUS_OK &&
                 contract->source_attach_start_satisfied_mask ==
                 STAGE72_XNU_IOKIT_ATTACH_START_READINESS_DRYRUN_REQUIRED_MASK &&
                 contract->source_attach_start_failure_mask == 0u &&
                 contract->source_attach_start_checksum == STAGE72_XNU_IOKIT_ATTACH_START_READINESS_EXPECTED_CHECKSUM &&
                 contract->source_selected_attach_start_entry_mask == STAGE72_XNU_IOKIT_REGISTRY_SERVICE_SELECTED_REQUIRED &&
                 contract->source_rejected_attach_start_mask == STAGE72_XNU_IOKIT_REGISTRY_SERVICE_REJECTED_REQUIRED &&
                 contract->source_attach_start_matrix == (STAGE72_XNU_IOKIT_REGISTRY_SERVICE_SELECTED_REQUIRED |
                                                          STAGE72_XNU_IOKIT_REGISTRY_SERVICE_REJECTED_REQUIRED) &&
                 contract->source_attach_readiness_mask == STAGE72_XNU_IOKIT_REGISTRY_SERVICE_SELECTED_REQUIRED &&
                 contract->source_start_readiness_mask == STAGE72_XNU_IOKIT_REGISTRY_SERVICE_SELECTED_REQUIRED &&
                 contract->source_topology_status == STAGE72_STATUS_OK &&
                 contract->source_property_inheritance_status == STAGE72_STATUS_OK &&
                 contract->source_catalog_status == STAGE72_STATUS_OK &&
                 contract->source_provider_status == STAGE72_STATUS_OK &&
                 contract->source_registry_status == STAGE72_STATUS_OK &&
                 contract->source_match_status == STAGE72_STATUS_OK &&
                 contract->source_scaffold_status == STAGE72_STATUS_OK &&
                 contract->source_pexpert_status == STAGE72_STATUS_OK &&
                 contract->source_pmap_transition_status == STAGE72_STATUS_OK &&
                 contract->source_compile_graph_status == STAGE72_STATUS_OK &&
                 contract->source_object_subset_status == STAGE72_STATUS_OK &&
                 contract->source_link_status == STAGE72_STATUS_OK &&
                 contract->source_loader_safety_mask == contract->source_loader_safety_required_mask) ? 1u : 0u;
    if (source_ok) {
        contract->satisfied_mask |= STAGE72_XNU_IOKIT_LIFECYCLE_REGISTER_SERVICE_DRYRUN_SAT_SOURCE_ATTACH_START;
    } else {
        contract->failure_mask |= STAGE72_XNU_IOKIT_LIFECYCLE_REGISTER_SERVICE_DRYRUN_FAIL_SOURCE;
    }

    if (contract->apple_dt_semantic_mask == STAGE72_DT_READY_REQUIRED) {
        contract->satisfied_mask |= STAGE72_XNU_IOKIT_LIFECYCLE_REGISTER_SERVICE_DRYRUN_SAT_APPLE_DT;
    } else {
        contract->failure_mask |= STAGE72_XNU_IOKIT_LIFECYCLE_REGISTER_SERVICE_DRYRUN_FAIL_DT;
    }

    personality_nodes_ok = (contract->platform_personality_node_ptr != 0u &&
                            contract->platform_personality_prop_count == 53u &&
                            contract->interrupt_personality_node_ptr != 0u &&
                            contract->interrupt_personality_prop_count == 53u &&
                            contract->timer_personality_node_ptr != 0u &&
                            contract->timer_personality_prop_count == 53u &&
                            contract->cpu_personality_node_ptr != 0u &&
                            contract->cpu_personality_prop_count == 53u &&
                            contract->rejected_personality_node_ptr != 0u &&
                            contract->rejected_personality_prop_count == 52u) ? 1u : 0u;
    if (personality_nodes_ok) {
        contract->satisfied_mask |= STAGE72_XNU_IOKIT_LIFECYCLE_REGISTER_SERVICE_DRYRUN_SAT_PERSONALITY_NODE_SET;
    } else {
        contract->failure_mask |= STAGE72_XNU_IOKIT_LIFECYCLE_REGISTER_SERVICE_DRYRUN_FAIL_PERSONALITY;
    }

    if (contract->matched_mask == STAGE72_XNU_IOKIT_REGISTRY_SERVICE_SELECTED_REQUIRED) {
        contract->satisfied_mask |= STAGE72_XNU_IOKIT_LIFECYCLE_REGISTER_SERVICE_DRYRUN_SAT_MATCHED_MATRIX;
    } else {
        contract->failure_mask |= STAGE72_XNU_IOKIT_LIFECYCLE_REGISTER_SERVICE_DRYRUN_FAIL_LIFECYCLE;
    }
    if (contract->provider_published_mask == STAGE72_XNU_IOKIT_REGISTRY_SERVICE_SELECTED_REQUIRED) {
        contract->satisfied_mask |= STAGE72_XNU_IOKIT_LIFECYCLE_REGISTER_SERVICE_DRYRUN_SAT_PROVIDER_PUBLISHED_MATRIX;
    } else {
        contract->failure_mask |= STAGE72_XNU_IOKIT_LIFECYCLE_REGISTER_SERVICE_DRYRUN_FAIL_LIFECYCLE;
    }
    if (contract->registry_linked_mask == STAGE72_XNU_IOKIT_REGISTRY_SERVICE_SELECTED_REQUIRED) {
        contract->satisfied_mask |= STAGE72_XNU_IOKIT_LIFECYCLE_REGISTER_SERVICE_DRYRUN_SAT_REGISTRY_LINKED_MATRIX;
    } else {
        contract->failure_mask |= STAGE72_XNU_IOKIT_LIFECYCLE_REGISTER_SERVICE_DRYRUN_FAIL_LIFECYCLE;
    }
    if (contract->topology_linked_mask == STAGE72_XNU_IOKIT_REGISTRY_SERVICE_SELECTED_REQUIRED) {
        contract->satisfied_mask |= STAGE72_XNU_IOKIT_LIFECYCLE_REGISTER_SERVICE_DRYRUN_SAT_TOPOLOGY_LINKED_MATRIX;
    } else {
        contract->failure_mask |= STAGE72_XNU_IOKIT_LIFECYCLE_REGISTER_SERVICE_DRYRUN_FAIL_LIFECYCLE;
    }
    if (contract->attach_readiness_mask == STAGE72_XNU_IOKIT_REGISTRY_SERVICE_SELECTED_REQUIRED &&
        contract->start_readiness_mask == STAGE72_XNU_IOKIT_REGISTRY_SERVICE_SELECTED_REQUIRED &&
        contract->source_selected_attach_start_entry_mask == STAGE72_XNU_IOKIT_REGISTRY_SERVICE_SELECTED_REQUIRED) {
        contract->satisfied_mask |= STAGE72_XNU_IOKIT_LIFECYCLE_REGISTER_SERVICE_DRYRUN_SAT_ATTACH_START_SOURCE_MATRIX;
    } else {
        contract->failure_mask |= STAGE72_XNU_IOKIT_LIFECYCLE_REGISTER_SERVICE_DRYRUN_FAIL_SOURCE;
    }
    if (contract->register_service_readiness_mask == STAGE72_XNU_IOKIT_REGISTRY_SERVICE_SELECTED_REQUIRED) {
        contract->satisfied_mask |= STAGE72_XNU_IOKIT_LIFECYCLE_REGISTER_SERVICE_DRYRUN_SAT_REGISTER_SERVICE_READINESS;
    } else {
        contract->failure_mask |= STAGE72_XNU_IOKIT_LIFECYCLE_REGISTER_SERVICE_DRYRUN_FAIL_REGISTER_SERVICE;
    }
    if (contract->service_registered_mask == STAGE72_XNU_IOKIT_REGISTRY_SERVICE_SELECTED_REQUIRED) {
        contract->satisfied_mask |= STAGE72_XNU_IOKIT_LIFECYCLE_REGISTER_SERVICE_DRYRUN_SAT_SERVICE_REGISTERED;
    } else {
        contract->failure_mask |= STAGE72_XNU_IOKIT_LIFECYCLE_REGISTER_SERVICE_DRYRUN_FAIL_REGISTER_SERVICE;
    }
    if (contract->notification_ready_mask == STAGE72_XNU_IOKIT_REGISTRY_SERVICE_SELECTED_REQUIRED) {
        contract->satisfied_mask |= STAGE72_XNU_IOKIT_LIFECYCLE_REGISTER_SERVICE_DRYRUN_SAT_NOTIFICATION_READY;
    } else {
        contract->failure_mask |= STAGE72_XNU_IOKIT_LIFECYCLE_REGISTER_SERVICE_DRYRUN_FAIL_NOTIFICATION;
    }

    order_ok = (contract->register_order_match_mask == STAGE72_XNU_IOKIT_REGISTRY_SERVICE_SELECTED_REQUIRED &&
                contract->notification_order_match_mask == STAGE72_XNU_IOKIT_REGISTRY_SERVICE_SELECTED_REQUIRED &&
                contract->register_order_checksum == STAGE72_XNU_IOKIT_REGISTRY_TOPOLOGY_REJECTED_ORDINAL &&
                contract->notification_order_checksum == STAGE72_XNU_IOKIT_REGISTRY_TOPOLOGY_REJECTED_ORDINAL) ? 1u : 0u;
    if (order_ok) {
        contract->satisfied_mask |= STAGE72_XNU_IOKIT_LIFECYCLE_REGISTER_SERVICE_DRYRUN_SAT_ORDER_MATRIX;
    } else {
        contract->failure_mask |= STAGE72_XNU_IOKIT_LIFECYCLE_REGISTER_SERVICE_DRYRUN_FAIL_ORDER_DEPENDENCY;
    }

    dependency_ok = (contract->dependency_ready_mask == STAGE72_XNU_IOKIT_REGISTRY_SERVICE_SELECTED_REQUIRED &&
                     contract->provider_dependency_match_mask == STAGE72_XNU_IOKIT_REGISTRY_SERVICE_SELECTED_REQUIRED &&
                     contract->register_provider_start_required_mask == (STAGE72_XNU_IOKIT_REGISTRY_SERVICE_INTERRUPT_BIT |
                                                                         STAGE72_XNU_IOKIT_REGISTRY_SERVICE_TIMER_BIT |
                                                                         STAGE72_XNU_IOKIT_REGISTRY_SERVICE_CPU_BIT) &&
                     contract->register_provider_start_satisfied_mask == (STAGE72_XNU_IOKIT_REGISTRY_SERVICE_INTERRUPT_BIT |
                                                                          STAGE72_XNU_IOKIT_REGISTRY_SERVICE_TIMER_BIT |
                                                                          STAGE72_XNU_IOKIT_REGISTRY_SERVICE_CPU_BIT) &&
                     contract->register_provider_required_count == 3u &&
                     contract->register_provider_satisfied_count == 3u) ? 1u : 0u;
    if (dependency_ok) {
        contract->satisfied_mask |= STAGE72_XNU_IOKIT_LIFECYCLE_REGISTER_SERVICE_DRYRUN_SAT_PROVIDER_DEPENDENCY;
    } else {
        contract->failure_mask |= STAGE72_XNU_IOKIT_LIFECYCLE_REGISTER_SERVICE_DRYRUN_FAIL_ORDER_DEPENDENCY;
    }

    if (contract->register_runtime_blocked_mask == STAGE72_XNU_IOKIT_REGISTRY_SERVICE_SELECTED_REQUIRED &&
        contract->notification_runtime_blocked_mask == STAGE72_XNU_IOKIT_REGISTRY_SERVICE_SELECTED_REQUIRED) {
        contract->satisfied_mask |= STAGE72_XNU_IOKIT_LIFECYCLE_REGISTER_SERVICE_DRYRUN_SAT_RUNTIME_BLOCKED;
    } else {
        contract->failure_mask |= STAGE72_XNU_IOKIT_LIFECYCLE_REGISTER_SERVICE_DRYRUN_FAIL_REGISTER_SERVICE |
            STAGE72_XNU_IOKIT_LIFECYCLE_REGISTER_SERVICE_DRYRUN_FAIL_NOTIFICATION;
    }
    if (contract->lifecycle_provenance_match_mask == STAGE72_XNU_IOKIT_REGISTRY_SERVICE_SELECTED_REQUIRED) {
        contract->satisfied_mask |= STAGE72_XNU_IOKIT_LIFECYCLE_REGISTER_SERVICE_DRYRUN_SAT_PROVENANCE;
    } else {
        contract->failure_mask |= STAGE72_XNU_IOKIT_LIFECYCLE_REGISTER_SERVICE_DRYRUN_FAIL_LIFECYCLE;
    }
    if (contract->selected_lifecycle_entry_mask == STAGE72_XNU_IOKIT_REGISTRY_SERVICE_SELECTED_REQUIRED) {
        contract->satisfied_mask |= STAGE72_XNU_IOKIT_LIFECYCLE_REGISTER_SERVICE_DRYRUN_SAT_SELECTED_LIFECYCLE_MATRIX;
    } else {
        contract->failure_mask |= STAGE72_XNU_IOKIT_LIFECYCLE_REGISTER_SERVICE_DRYRUN_FAIL_LIFECYCLE;
    }
    if (contract->rejected_lifecycle_mask == STAGE72_XNU_IOKIT_REGISTRY_SERVICE_REJECTED_REQUIRED &&
        contract->source_rejected_attach_start_mask == STAGE72_XNU_IOKIT_REGISTRY_SERVICE_REJECTED_REQUIRED &&
        contract->rejected_unregistered_count == STAGE72_XNU_IOKIT_LIFECYCLE_REGISTER_SERVICE_REJECTED_COUNT) {
        contract->satisfied_mask |= STAGE72_XNU_IOKIT_LIFECYCLE_REGISTER_SERVICE_DRYRUN_SAT_REJECT_UNREGISTERED;
    } else {
        contract->failure_mask |= STAGE72_XNU_IOKIT_LIFECYCLE_REGISTER_SERVICE_DRYRUN_FAIL_REJECTED;
    }
    if (contract->lifecycle_candidate_count == STAGE72_XNU_IOKIT_LIFECYCLE_REGISTER_SERVICE_CANDIDATE_COUNT &&
        contract->selected_lifecycle_entry_count == STAGE72_XNU_IOKIT_LIFECYCLE_REGISTER_SERVICE_ENTRY_COUNT &&
        contract->rejected_lifecycle_entry_count == STAGE72_XNU_IOKIT_LIFECYCLE_REGISTER_SERVICE_REJECTED_COUNT &&
        contract->register_service_readiness_count == STAGE72_XNU_IOKIT_LIFECYCLE_REGISTER_SERVICE_ENTRY_COUNT &&
        contract->service_registered_count == STAGE72_XNU_IOKIT_LIFECYCLE_REGISTER_SERVICE_ENTRY_COUNT &&
        contract->notification_ready_count == STAGE72_XNU_IOKIT_LIFECYCLE_REGISTER_SERVICE_ENTRY_COUNT &&
        contract->dependency_ready_count == STAGE72_XNU_IOKIT_LIFECYCLE_REGISTER_SERVICE_ENTRY_COUNT &&
        contract->lifecycle_matrix == (STAGE72_XNU_IOKIT_REGISTRY_SERVICE_SELECTED_REQUIRED |
                                       STAGE72_XNU_IOKIT_REGISTRY_SERVICE_REJECTED_REQUIRED)) {
        contract->satisfied_mask |= STAGE72_XNU_IOKIT_LIFECYCLE_REGISTER_SERVICE_DRYRUN_SAT_DRYRUN_COUNTS;
    } else {
        contract->failure_mask |= STAGE72_XNU_IOKIT_LIFECYCLE_REGISTER_SERVICE_DRYRUN_FAIL_COUNTS;
    }
    if (contract->lifecycle_checksum == contract->expected_lifecycle_checksum) {
        contract->satisfied_mask |= STAGE72_XNU_IOKIT_LIFECYCLE_REGISTER_SERVICE_DRYRUN_SAT_LIFECYCLE_CHECKSUM;
    } else {
        contract->failure_mask |= STAGE72_XNU_IOKIT_LIFECYCLE_REGISTER_SERVICE_DRYRUN_FAIL_CHECKSUM;
    }

    if (contract->iokit_reference_mask == STAGE72_XNU_IOKIT_REFERENCE_REQUIRED &&
        contract->iokit_runtime_blocked_mask == STAGE72_XNU_IOKIT_REFERENCE_REQUIRED &&
        contract->iokit_reference_count >= 3u && contract->iokit_public_compile_count == 0u &&
        contract->iokit_public_link_count == 0u && contract->iokit_reference_only == 1u) {
        contract->satisfied_mask |= STAGE72_XNU_IOKIT_LIFECYCLE_REGISTER_SERVICE_DRYRUN_SAT_IOKIT_REFERENCE;
    } else {
        contract->failure_mask |= STAGE72_XNU_IOKIT_LIFECYCLE_REGISTER_SERVICE_DRYRUN_FAIL_PUBLIC_BOUNDARY;
    }

    public_boundary_ok = (contract->no_public_xnu_exec == 1u && contract->no_platform_runtime_exec == 1u &&
                          contract->no_iokit_runtime_exec == 1u && contract->no_catalog_runtime_exec == 1u &&
                          contract->no_provider_runtime_exec == 1u &&
                          contract->no_registry_entry_runtime_exec == 1u &&
                          contract->no_registry_topology_runtime_exec == 1u &&
                          contract->no_property_runtime_exec == 1u && contract->no_attach_runtime_exec == 1u &&
                          contract->no_start_runtime_exec == 1u &&
                          contract->no_register_service_runtime_exec == 1u &&
                          contract->no_notification_runtime_exec == 1u &&
                          contract->no_platform_driver_exec == 1u &&
                          contract->source_object_subset_status == STAGE72_STATUS_OK &&
                          contract->source_link_status == STAGE72_STATUS_OK) ? 1u : 0u;
    if (public_boundary_ok) {
        contract->satisfied_mask |= STAGE72_XNU_IOKIT_LIFECYCLE_REGISTER_SERVICE_DRYRUN_SAT_PUBLIC_BOUNDARY |
            STAGE72_XNU_IOKIT_LIFECYCLE_REGISTER_SERVICE_DRYRUN_SAT_NO_IOKIT_RUNTIME_EXEC |
            STAGE72_XNU_IOKIT_LIFECYCLE_REGISTER_SERVICE_DRYRUN_SAT_NO_REGISTER_SERVICE_RUNTIME_EXEC |
            STAGE72_XNU_IOKIT_LIFECYCLE_REGISTER_SERVICE_DRYRUN_SAT_NO_NOTIFICATION_RUNTIME_EXEC |
            STAGE72_XNU_IOKIT_LIFECYCLE_REGISTER_SERVICE_DRYRUN_SAT_NO_PUBLIC_XNU_EXEC;
    } else {
        contract->failure_mask |= STAGE72_XNU_IOKIT_LIFECYCLE_REGISTER_SERVICE_DRYRUN_FAIL_PUBLIC_BOUNDARY;
    }

    if (contract->no_public_pmap_exec == 1u) {
        contract->satisfied_mask |= STAGE72_XNU_IOKIT_LIFECYCLE_REGISTER_SERVICE_DRYRUN_SAT_NO_PUBLIC_PMAP_EXEC;
    } else {
        contract->failure_mask |= STAGE72_XNU_IOKIT_LIFECYCLE_REGISTER_SERVICE_DRYRUN_FAIL_PMAP_BOUNDARY;
    }

    pmap_boundary_ok = (contract->no_live_pmap_tables_installed == 0u &&
                        contract->proposed_workspace_written == 0u &&
                        contract->pmap_ttbr_written == 0u && contract->pmap_ttbcr_written == 0u &&
                        contract->pmap_dacr_written == 0u && contract->pmap_sctlr_written == 0u &&
                        contract->pmap_tlbs_invalidated == 0u && contract->caches_changed == 0u) ? 1u : 0u;
    if (contract->no_live_pmap_tables_installed == 0u) {
        contract->satisfied_mask |= STAGE72_XNU_IOKIT_LIFECYCLE_REGISTER_SERVICE_DRYRUN_SAT_NO_LIVE_PMAP_INSTALL;
    }
    if (contract->pmap_tlbs_invalidated == 0u && contract->caches_changed == 0u) {
        contract->satisfied_mask |= STAGE72_XNU_IOKIT_LIFECYCLE_REGISTER_SERVICE_DRYRUN_SAT_NO_TLB_CACHE_CHANGE;
    }
    if (pmap_boundary_ok && contract->source_pmap_transition_status == STAGE72_STATUS_OK) {
        contract->satisfied_mask |= STAGE72_XNU_IOKIT_LIFECYCLE_REGISTER_SERVICE_DRYRUN_SAT_PMAP_BOUNDARY;
    } else {
        contract->failure_mask |= STAGE72_XNU_IOKIT_LIFECYCLE_REGISTER_SERVICE_DRYRUN_FAIL_PMAP_BOUNDARY;
    }

    if (contract->generated_macho_executed == 0u && contract->xnu_start_executed == 0u) {
        contract->satisfied_mask |= STAGE72_XNU_IOKIT_LIFECYCLE_REGISTER_SERVICE_DRYRUN_SAT_NO_XNU_MACHO_EXEC;
    } else {
        contract->failure_mask |= STAGE72_XNU_IOKIT_LIFECYCLE_REGISTER_SERVICE_DRYRUN_FAIL_SAFETY_BOUNDARY;
    }
    if (contract->local_only == 1u && contract->fail_closed == 1u &&
        contract->persistent_write_attempted == 0u && contract->xnu_start_executed == 0u &&
        contract->generated_macho_executed == 0u &&
        contract->proposed_platform_gap_mask == (STAGE72_PLATFORM_GAP_REQUIRED_RECORDED & ~STAGE72_PLATFORM_GAP_IOKIT_STACK) &&
        contract->proposed_pexpert_gap_mask == 0u &&
        contract->selected_lifecycle_entry_mask == STAGE72_XNU_IOKIT_REGISTRY_SERVICE_SELECTED_REQUIRED &&
        contract->rejected_lifecycle_mask == STAGE72_XNU_IOKIT_REGISTRY_SERVICE_REJECTED_REQUIRED) {
        contract->satisfied_mask |= STAGE72_XNU_IOKIT_LIFECYCLE_REGISTER_SERVICE_DRYRUN_SAT_LOCAL_ONLY_FAIL_CLOSED;
    } else {
        contract->failure_mask |= STAGE72_XNU_IOKIT_LIFECYCLE_REGISTER_SERVICE_DRYRUN_FAIL_ROLLUP |
            STAGE72_XNU_IOKIT_LIFECYCLE_REGISTER_SERVICE_DRYRUN_FAIL_SAFETY_BOUNDARY;
    }

    contract->checksum = stage72_iokit_lifecycle_register_service_dryrun_checksum(contract);
    if (contract->checksum != stage72_iokit_lifecycle_register_service_dryrun_checksum(contract)) {
        contract->failure_mask |= STAGE72_XNU_IOKIT_LIFECYCLE_REGISTER_SERVICE_DRYRUN_FAIL_CHECKSUM;
    }

    if (contract->satisfied_mask == contract->required_mask && contract->failure_mask == 0u) {
        contract->status = STAGE72_STATUS_OK;
    } else {
        contract->status = STAGE72_STATUS_BASE | contract->failure_mask;
    }

    stage72_iokit_lifecycle_register_service_dryrun_log(contract);
    return contract->status == STAGE72_STATUS_OK;
}

const struct stage72_xnu_iokit_lifecycle_register_service_readiness_dryrun_contract *
stage72_xnu_iokit_lifecycle_register_service_readiness_dryrun_contract_result(void)
{
    return &g_stage72_xnu_iokit_lifecycle_register_service_readiness_dryrun_contract;
}
