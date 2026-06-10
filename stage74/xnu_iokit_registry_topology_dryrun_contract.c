#include "stage74.h"

static struct stage74_xnu_iokit_registry_topology_dryrun_contract
    g_stage74_xnu_iokit_registry_topology_dryrun_contract;

static uint32_t stage74_iokit_registry_topology_dryrun_checksum(
    volatile const struct stage74_xnu_iokit_registry_topology_dryrun_contract *contract)
{
    volatile const uint32_t *words = (volatile const uint32_t *)(uintptr_t)contract;
    uint32_t count = (uint32_t)(offsetof(struct stage74_xnu_iokit_registry_topology_dryrun_contract,
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

static void stage74_iokit_registry_topology_dryrun_log(
    const struct stage74_xnu_iokit_registry_topology_dryrun_contract *contract)
{
    xnu_log_kv32("stage74_xnu_iokit_registry_topology_dryrun_contract_status", contract->status);
    xnu_log_kv32("stage74_xnu_iokit_registry_topology_dryrun_contract_required_mask", contract->required_mask);
    xnu_log_kv32("stage74_xnu_iokit_registry_topology_dryrun_contract_satisfied_mask", contract->satisfied_mask);
    xnu_log_kv32("stage74_xnu_iokit_registry_topology_dryrun_contract_failure_mask", contract->failure_mask);
    xnu_log_kv32("stage74_xnu_iokit_registry_topology_dryrun_contract_checksum", contract->checksum);
    xnu_log_kv32("stage74_xnu_iokit_registry_topology_source_property_inheritance_status", contract->source_property_inheritance_status);
    xnu_log_kv32("stage74_xnu_iokit_registry_topology_source_property_inheritance_satisfied_mask", contract->source_property_inheritance_satisfied_mask);
    xnu_log_kv32("stage74_xnu_iokit_registry_topology_source_property_inheritance_failure_mask", contract->source_property_inheritance_failure_mask);
    xnu_log_kv32("stage74_xnu_iokit_registry_topology_source_property_inheritance_checksum", contract->source_property_inheritance_checksum);
    xnu_log_kv32("stage74_xnu_iokit_registry_topology_source_property_checksum", contract->source_property_inheritance_property_checksum);
    xnu_log_kv32("stage74_xnu_iokit_registry_topology_source_selected_registry_entry_mask", contract->source_selected_registry_entry_mask);
    xnu_log_kv32("stage74_xnu_iokit_registry_topology_source_rejected_unattached_mask", contract->source_rejected_unattached_mask);
    xnu_log_kv32("stage74_xnu_iokit_registry_topology_source_inherited_property_mask", contract->source_inherited_property_mask);
    xnu_log_kv32("stage74_xnu_iokit_registry_topology_source_instance_property_mask", contract->source_instance_property_mask);
    xnu_log_kv32("stage74_xnu_iokit_registry_topology_source_attach_deferred_mask", contract->source_attach_deferred_mask);
    xnu_log_kv32("stage74_xnu_iokit_registry_topology_source_start_deferred_mask", contract->source_start_deferred_mask);
    xnu_log_kv32("stage74_xnu_iokit_registry_topology_source_matrix", contract->source_property_inheritance_matrix);
    xnu_log_kv32("stage74_xnu_iokit_registry_topology_apple_dt_semantic_mask", contract->apple_dt_semantic_mask);
    xnu_log_kv32("stage74_xnu_iokit_registry_topology_platform_props", contract->platform_personality_prop_count);
    xnu_log_kv32("stage74_xnu_iokit_registry_topology_interrupt_props", contract->interrupt_personality_prop_count);
    xnu_log_kv32("stage74_xnu_iokit_registry_topology_timer_props", contract->timer_personality_prop_count);
    xnu_log_kv32("stage74_xnu_iokit_registry_topology_cpu_props", contract->cpu_personality_prop_count);
    xnu_log_kv32("stage74_xnu_iokit_registry_topology_rejected_props", contract->rejected_personality_prop_count);
    xnu_log_kv32("stage74_xnu_iokit_registry_topology_topology_marker_mask", contract->topology_marker_mask);
    xnu_log_kv32("stage74_xnu_iokit_registry_topology_registry_plane_match_mask", contract->registry_plane_match_mask);
    xnu_log_kv32("stage74_xnu_iokit_registry_topology_parent_path_match_mask", contract->parent_path_match_mask);
    xnu_log_kv32("stage74_xnu_iokit_registry_topology_entry_path_match_mask", contract->entry_path_match_mask);
    xnu_log_kv32("stage74_xnu_iokit_registry_topology_parent_ordinal_match_mask", contract->parent_ordinal_match_mask);
    xnu_log_kv32("stage74_xnu_iokit_registry_topology_sibling_order_match_mask", contract->sibling_order_match_mask);
    xnu_log_kv32("stage74_xnu_iokit_registry_topology_provenance_match_mask", contract->provenance_match_mask);
    xnu_log_kv32("stage74_xnu_iokit_registry_topology_selected_topology_entry_mask", contract->selected_topology_entry_mask);
    xnu_log_kv32("stage74_xnu_iokit_registry_topology_rejected_unlinked_mask", contract->rejected_unlinked_mask);
    xnu_log_kv32("stage74_xnu_iokit_registry_topology_topology_matrix", contract->topology_matrix);
    xnu_log_kv32("stage74_xnu_iokit_registry_topology_platform_parent_ordinal", contract->platform_parent_ordinal);
    xnu_log_kv32("stage74_xnu_iokit_registry_topology_interrupt_parent_ordinal", contract->interrupt_parent_ordinal);
    xnu_log_kv32("stage74_xnu_iokit_registry_topology_timer_parent_ordinal", contract->timer_parent_ordinal);
    xnu_log_kv32("stage74_xnu_iokit_registry_topology_cpu_parent_ordinal", contract->cpu_parent_ordinal);
    xnu_log_kv32("stage74_xnu_iokit_registry_topology_rejected_parent_ordinal", contract->rejected_parent_ordinal);
    xnu_log_kv32("stage74_xnu_iokit_registry_topology_platform_sibling_order", contract->platform_sibling_order);
    xnu_log_kv32("stage74_xnu_iokit_registry_topology_interrupt_sibling_order", contract->interrupt_sibling_order);
    xnu_log_kv32("stage74_xnu_iokit_registry_topology_timer_sibling_order", contract->timer_sibling_order);
    xnu_log_kv32("stage74_xnu_iokit_registry_topology_cpu_sibling_order", contract->cpu_sibling_order);
    xnu_log_kv32("stage74_xnu_iokit_registry_topology_rejected_sibling_order", contract->rejected_sibling_order);
    xnu_log_kv32("stage74_xnu_iokit_registry_topology_order_checksum", contract->topology_order_checksum);
    xnu_log_kv32("stage74_xnu_iokit_registry_topology_parent_order_checksum", contract->parent_order_checksum);
    xnu_log_kv32("stage74_xnu_iokit_registry_topology_path_checksum", contract->path_checksum);
    xnu_log_kv32("stage74_xnu_iokit_registry_topology_registry_plane_hash", contract->registry_plane_hash);
    xnu_log_kv32("stage74_xnu_iokit_registry_topology_checksum", contract->topology_checksum);
    xnu_log_kv32("stage74_xnu_iokit_registry_topology_expected_checksum", contract->expected_topology_checksum);
    xnu_log_kv32("stage74_xnu_iokit_registry_topology_candidate_count", contract->topology_candidate_count);
    xnu_log_kv32("stage74_xnu_iokit_registry_topology_selected_entry_count", contract->selected_topology_entry_count);
    xnu_log_kv32("stage74_xnu_iokit_registry_topology_rejected_entry_count", contract->rejected_topology_entry_count);
    xnu_log_kv32("stage74_xnu_iokit_registry_topology_dryrun_count", contract->topology_dryrun_count);
    xnu_log_kv32("stage74_xnu_iokit_registry_topology_relationship_count", contract->topology_relationship_count);
    xnu_log_kv32("stage74_xnu_iokit_registry_topology_rejected_unlinked_count", contract->rejected_unlinked_count);
    xnu_log_kv32("stage74_xnu_iokit_registry_topology_public_compile_count", contract->iokit_public_compile_count);
    xnu_log_kv32("stage74_xnu_iokit_registry_topology_public_link_count", contract->iokit_public_link_count);
    xnu_log_kv32("stage74_xnu_iokit_registry_topology_no_iokit_runtime_exec", contract->no_iokit_runtime_exec);
    xnu_log_kv32("stage74_xnu_iokit_registry_topology_no_registry_entry_runtime_exec", contract->no_registry_entry_runtime_exec);
    xnu_log_kv32("stage74_xnu_iokit_registry_topology_no_registry_topology_runtime_exec", contract->no_registry_topology_runtime_exec);
    xnu_log_kv32("stage74_xnu_iokit_registry_topology_no_property_runtime_exec", contract->no_property_runtime_exec);
    xnu_log_kv32("stage74_xnu_iokit_registry_topology_no_attach_runtime_exec", contract->no_attach_runtime_exec);
    xnu_log_kv32("stage74_xnu_iokit_registry_topology_no_start_runtime_exec", contract->no_start_runtime_exec);
    xnu_log_kv32("stage74_xnu_iokit_registry_topology_no_provider_runtime_exec", contract->no_provider_runtime_exec);
    xnu_log_kv32("stage74_xnu_iokit_registry_topology_no_platform_driver_exec", contract->no_platform_driver_exec);
    xnu_log_kv32("stage74_xnu_iokit_registry_topology_no_live_pmap_tables_installed", contract->no_live_pmap_tables_installed);
    xnu_log_kv32("stage74_xnu_iokit_registry_topology_proposed_workspace_written", contract->proposed_workspace_written);
    xnu_log_kv32("stage74_xnu_iokit_registry_topology_pmap_ttbr_written", contract->pmap_ttbr_written);
    xnu_log_kv32("stage74_xnu_iokit_registry_topology_pmap_tlbs_invalidated", contract->pmap_tlbs_invalidated);
    xnu_log_kv32("stage74_xnu_iokit_registry_topology_caches_changed", contract->caches_changed);
    xnu_log_kv32("stage74_xnu_iokit_registry_topology_persistent_write_attempted", contract->persistent_write_attempted);
}

int stage74_xnu_iokit_registry_topology_dryrun_contract_selftest(
    const struct stage74_loader_preflight *preflight)
{
    struct stage74_xnu_iokit_registry_topology_dryrun_contract *contract =
        &g_stage74_xnu_iokit_registry_topology_dryrun_contract;
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
    uint32_t public_boundary_ok;
    uint32_t pmap_boundary_ok;

    memset(contract, 0, sizeof(*contract));
    contract->version = STAGE74_XNU_IOKIT_REGISTRY_TOPOLOGY_DRYRUN_CONTRACT_VERSION;
    contract->size = sizeof(*contract);
    contract->status = STAGE74_STATUS_BASE;
    contract->required_mask = STAGE74_XNU_IOKIT_REGISTRY_TOPOLOGY_DRYRUN_REQUIRED_MASK;

    if (!preflight || !args || !root) {
        contract->failure_mask = STAGE74_XNU_IOKIT_REGISTRY_TOPOLOGY_DRYRUN_FAIL_SOURCE |
            STAGE74_XNU_IOKIT_REGISTRY_TOPOLOGY_DRYRUN_FAIL_SAFETY_BOUNDARY;
        contract->checksum = stage74_iokit_registry_topology_dryrun_checksum(contract);
        stage74_iokit_registry_topology_dryrun_log(contract);
        return 0;
    }

    contract->source_property_inheritance_status = preflight->xnu_iokit_property_inheritance_dryrun_contract_status;
    contract->source_property_inheritance_satisfied_mask =
        preflight->xnu_iokit_property_inheritance_dryrun_contract_satisfied_mask;
    contract->source_property_inheritance_failure_mask =
        preflight->xnu_iokit_property_inheritance_dryrun_contract_failure_mask;
    contract->source_property_inheritance_checksum = preflight->xnu_iokit_property_inheritance_dryrun_contract_checksum;
    contract->source_property_inheritance_property_checksum =
        preflight->xnu_iokit_property_inheritance_dryrun_contract.property_inheritance_checksum;
    contract->source_selected_registry_entry_mask =
        preflight->xnu_iokit_property_inheritance_dryrun_contract.selected_registry_entry_mask;
    contract->source_rejected_unattached_mask =
        preflight->xnu_iokit_property_inheritance_dryrun_contract.rejected_unattached_mask;
    contract->source_inherited_property_mask =
        preflight->xnu_iokit_property_inheritance_dryrun_contract.inherited_property_mask;
    contract->source_instance_property_mask =
        preflight->xnu_iokit_property_inheritance_dryrun_contract.instance_property_mask;
    contract->source_attach_deferred_mask =
        preflight->xnu_iokit_property_inheritance_dryrun_contract.attach_deferred_mask;
    contract->source_start_deferred_mask =
        preflight->xnu_iokit_property_inheritance_dryrun_contract.start_deferred_mask;
    contract->source_property_inheritance_matrix =
        preflight->xnu_iokit_property_inheritance_dryrun_contract.property_inheritance_matrix;
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
    contract->source_loader_safety_required_mask = STAGE74_LOADER_SAFETY_REQUIRED;
    contract->boot_args_ptr = preflight->boot_args_ptr;
    contract->device_tree_ptr = preflight->device_tree_ptr;
    contract->device_tree_length = preflight->device_tree_length;
    contract->apple_dt_semantic_mask = preflight->apple_dt_semantic_mask;

    contract->platform_personality_node_ptr = (uint32_t)(uintptr_t)apple_dt_find_child(args->deviceTreeP,
                                                                                       args->deviceTreeLength,
                                                                                       root,
                                                                                       "stage74-platform-personality");
    contract->interrupt_personality_node_ptr = (uint32_t)(uintptr_t)apple_dt_find_child(args->deviceTreeP,
                                                                                        args->deviceTreeLength,
                                                                                        root,
                                                                                        "stage74-interrupt-personality");
    contract->timer_personality_node_ptr = (uint32_t)(uintptr_t)apple_dt_find_child(args->deviceTreeP,
                                                                                    args->deviceTreeLength,
                                                                                    root,
                                                                                    "stage74-timer-personality");
    contract->cpu_personality_node_ptr = (uint32_t)(uintptr_t)apple_dt_find_child(args->deviceTreeP,
                                                                                  args->deviceTreeLength,
                                                                                  root,
                                                                                  "stage74-cpu-personality");
    contract->rejected_personality_node_ptr = (uint32_t)(uintptr_t)apple_dt_find_child(args->deviceTreeP,
                                                                                       args->deviceTreeLength,
                                                                                       root,
                                                                                       "stage74-rejected-personality");

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

    if (u32_prop_equals(args, platform_node, "registry-topology-dryrun", 1u)) {
        contract->topology_marker_mask |= STAGE74_XNU_IOKIT_REGISTRY_SERVICE_PLATFORM_BIT;
    }
    if (u32_prop_equals(args, interrupt_node, "registry-topology-dryrun", 1u)) {
        contract->topology_marker_mask |= STAGE74_XNU_IOKIT_REGISTRY_SERVICE_INTERRUPT_BIT;
    }
    if (u32_prop_equals(args, timer_node, "registry-topology-dryrun", 1u)) {
        contract->topology_marker_mask |= STAGE74_XNU_IOKIT_REGISTRY_SERVICE_TIMER_BIT;
    }
    if (u32_prop_equals(args, cpu_node, "registry-topology-dryrun", 1u)) {
        contract->topology_marker_mask |= STAGE74_XNU_IOKIT_REGISTRY_SERVICE_CPU_BIT;
    }

    if (string_prop_equals(args, platform_node, "registry-plane", "IODeviceTree")) {
        contract->registry_plane_match_mask |= STAGE74_XNU_IOKIT_REGISTRY_SERVICE_PLATFORM_BIT;
    }
    if (string_prop_equals(args, interrupt_node, "registry-plane", "IODeviceTree")) {
        contract->registry_plane_match_mask |= STAGE74_XNU_IOKIT_REGISTRY_SERVICE_INTERRUPT_BIT;
    }
    if (string_prop_equals(args, timer_node, "registry-plane", "IODeviceTree")) {
        contract->registry_plane_match_mask |= STAGE74_XNU_IOKIT_REGISTRY_SERVICE_TIMER_BIT;
    }
    if (string_prop_equals(args, cpu_node, "registry-plane", "IODeviceTree")) {
        contract->registry_plane_match_mask |= STAGE74_XNU_IOKIT_REGISTRY_SERVICE_CPU_BIT;
    }

    if (string_prop_equals(args, platform_node, "registry-parent-path", "IODeviceTree:/")) {
        contract->parent_path_match_mask |= STAGE74_XNU_IOKIT_REGISTRY_SERVICE_PLATFORM_BIT;
    }
    if (string_prop_equals(args, interrupt_node, "registry-parent-path",
                           "IODeviceTree:/stage74-platform-personality")) {
        contract->parent_path_match_mask |= STAGE74_XNU_IOKIT_REGISTRY_SERVICE_INTERRUPT_BIT;
    }
    if (string_prop_equals(args, timer_node, "registry-parent-path",
                           "IODeviceTree:/stage74-platform-personality")) {
        contract->parent_path_match_mask |= STAGE74_XNU_IOKIT_REGISTRY_SERVICE_TIMER_BIT;
    }
    if (string_prop_equals(args, cpu_node, "registry-parent-path",
                           "IODeviceTree:/stage74-platform-personality")) {
        contract->parent_path_match_mask |= STAGE74_XNU_IOKIT_REGISTRY_SERVICE_CPU_BIT;
    }

    if (string_prop_equals(args, platform_node, "registry-entry-path",
                           "IODeviceTree:/stage74-platform-personality")) {
        contract->entry_path_match_mask |= STAGE74_XNU_IOKIT_REGISTRY_SERVICE_PLATFORM_BIT;
    }
    if (string_prop_equals(args, interrupt_node, "registry-entry-path",
                           "IODeviceTree:/stage74-interrupt-personality")) {
        contract->entry_path_match_mask |= STAGE74_XNU_IOKIT_REGISTRY_SERVICE_INTERRUPT_BIT;
    }
    if (string_prop_equals(args, timer_node, "registry-entry-path", "IODeviceTree:/stage74-timer-personality")) {
        contract->entry_path_match_mask |= STAGE74_XNU_IOKIT_REGISTRY_SERVICE_TIMER_BIT;
    }
    if (string_prop_equals(args, cpu_node, "registry-entry-path", "IODeviceTree:/stage74-cpu-personality")) {
        contract->entry_path_match_mask |= STAGE74_XNU_IOKIT_REGISTRY_SERVICE_CPU_BIT;
    }

    contract->platform_parent_ordinal = apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength,
                                                              platform_node, "registry-parent-ordinal", 0u);
    contract->interrupt_parent_ordinal = apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength,
                                                               interrupt_node, "registry-parent-ordinal", 0u);
    contract->timer_parent_ordinal = apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength,
                                                           timer_node, "registry-parent-ordinal", 0u);
    contract->cpu_parent_ordinal = apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength,
                                                         cpu_node, "registry-parent-ordinal", 0u);
    contract->rejected_parent_ordinal = apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength,
                                                              rejected_node, "registry-parent-ordinal", 0u);
    if (contract->platform_parent_ordinal == STAGE74_XNU_IOKIT_REGISTRY_TOPOLOGY_REJECTED_ORDINAL) {
        contract->parent_ordinal_match_mask |= STAGE74_XNU_IOKIT_REGISTRY_SERVICE_PLATFORM_BIT;
    }
    if (contract->interrupt_parent_ordinal == STAGE74_XNU_IOKIT_REGISTRY_TOPOLOGY_PLATFORM_ORDINAL) {
        contract->parent_ordinal_match_mask |= STAGE74_XNU_IOKIT_REGISTRY_SERVICE_INTERRUPT_BIT;
    }
    if (contract->timer_parent_ordinal == STAGE74_XNU_IOKIT_REGISTRY_TOPOLOGY_PLATFORM_ORDINAL) {
        contract->parent_ordinal_match_mask |= STAGE74_XNU_IOKIT_REGISTRY_SERVICE_TIMER_BIT;
    }
    if (contract->cpu_parent_ordinal == STAGE74_XNU_IOKIT_REGISTRY_TOPOLOGY_PLATFORM_ORDINAL) {
        contract->parent_ordinal_match_mask |= STAGE74_XNU_IOKIT_REGISTRY_SERVICE_CPU_BIT;
    }

    contract->platform_sibling_order = apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength,
                                                             platform_node, "registry-sibling-order", 0xffffffffu);
    contract->interrupt_sibling_order = apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength,
                                                              interrupt_node, "registry-sibling-order", 0xffffffffu);
    contract->timer_sibling_order = apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength,
                                                          timer_node, "registry-sibling-order", 0xffffffffu);
    contract->cpu_sibling_order = apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength,
                                                        cpu_node, "registry-sibling-order", 0xffffffffu);
    contract->rejected_sibling_order = apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength,
                                                             rejected_node, "registry-sibling-order", 0u);
    if (contract->platform_sibling_order == STAGE74_XNU_IOKIT_REGISTRY_TOPOLOGY_PLATFORM_ORDINAL) {
        contract->sibling_order_match_mask |= STAGE74_XNU_IOKIT_REGISTRY_SERVICE_PLATFORM_BIT;
    }
    if (contract->interrupt_sibling_order == STAGE74_XNU_IOKIT_REGISTRY_TOPOLOGY_INTERRUPT_ORDINAL) {
        contract->sibling_order_match_mask |= STAGE74_XNU_IOKIT_REGISTRY_SERVICE_INTERRUPT_BIT;
    }
    if (contract->timer_sibling_order == STAGE74_XNU_IOKIT_REGISTRY_TOPOLOGY_TIMER_ORDINAL) {
        contract->sibling_order_match_mask |= STAGE74_XNU_IOKIT_REGISTRY_SERVICE_TIMER_BIT;
    }
    if (contract->cpu_sibling_order == STAGE74_XNU_IOKIT_REGISTRY_TOPOLOGY_CPU_ORDINAL) {
        contract->sibling_order_match_mask |= STAGE74_XNU_IOKIT_REGISTRY_SERVICE_CPU_BIT;
    }

    if (string_prop_equals(args, platform_node, "registry-topology-provenance", "stage74-property-inheritance")) {
        contract->provenance_match_mask |= STAGE74_XNU_IOKIT_REGISTRY_SERVICE_PLATFORM_BIT;
    }
    if (string_prop_equals(args, interrupt_node, "registry-topology-provenance", "stage74-property-inheritance")) {
        contract->provenance_match_mask |= STAGE74_XNU_IOKIT_REGISTRY_SERVICE_INTERRUPT_BIT;
    }
    if (string_prop_equals(args, timer_node, "registry-topology-provenance", "stage74-property-inheritance")) {
        contract->provenance_match_mask |= STAGE74_XNU_IOKIT_REGISTRY_SERVICE_TIMER_BIT;
    }
    if (string_prop_equals(args, cpu_node, "registry-topology-provenance", "stage74-property-inheritance")) {
        contract->provenance_match_mask |= STAGE74_XNU_IOKIT_REGISTRY_SERVICE_CPU_BIT;
    }

    contract->selected_topology_entry_mask = contract->topology_marker_mask & contract->registry_plane_match_mask &
        contract->parent_path_match_mask & contract->entry_path_match_mask & contract->parent_ordinal_match_mask &
        contract->sibling_order_match_mask & contract->provenance_match_mask & contract->source_selected_registry_entry_mask;

    if (u32_prop_equals(args, rejected_node, "registry-topology-dryrun", 0u) &&
        string_prop_equals(args, rejected_node, "registry-parent-path", "IODeviceTree:/unlinked") &&
        string_prop_equals(args, rejected_node, "registry-entry-path", "IODeviceTree:/stage74-rejected-personality") &&
        u32_prop_equals(args, rejected_node, "registry-parent-ordinal",
                        STAGE74_XNU_IOKIT_REGISTRY_TOPOLOGY_REJECTED_ORDINAL) &&
        u32_prop_equals(args, rejected_node, "registry-sibling-order",
                        STAGE74_XNU_IOKIT_REGISTRY_TOPOLOGY_REJECTED_ORDINAL) &&
        string_prop_equals(args, rejected_node, "registry-topology-provenance", "stage74-rejected-unlinked") &&
        u32_prop_equals(args, rejected_node, "registry-entry-linked", 0u) &&
        u32_prop_equals(args, rejected_node, "registry-entry-dryrun", 0u) &&
        u32_prop_equals(args, rejected_node, "registry-entry-attached", 0u) &&
        u32_prop_equals(args, rejected_node, "provider-plane-published", 0u) &&
        u32_prop_equals(args, rejected_node, "rejected-candidate", 1u) &&
        u32_prop_equals(args, rejected_node, "stage-owned-local-only", 1u)) {
        contract->rejected_unlinked_mask |= STAGE74_XNU_IOKIT_REGISTRY_SERVICE_REJECTED_BIT;
    }

    contract->topology_matrix = contract->selected_topology_entry_mask | contract->rejected_unlinked_mask;
    contract->topology_order_checksum = contract->platform_sibling_order ^ contract->interrupt_sibling_order ^
        contract->timer_sibling_order ^ contract->cpu_sibling_order ^ contract->rejected_sibling_order;
    contract->parent_order_checksum = contract->platform_parent_ordinal ^ contract->interrupt_parent_ordinal ^
        contract->timer_parent_ordinal ^ contract->cpu_parent_ordinal ^ contract->rejected_parent_ordinal;
    contract->registry_plane_hash = STAGE74_XNU_IOKIT_REGISTRY_TOPOLOGY_PLANE_HASH;
    contract->path_checksum = contract->registry_plane_hash ^ contract->parent_path_match_mask ^
        contract->entry_path_match_mask ^ contract->sibling_order_match_mask ^ contract->provenance_match_mask;
    contract->topology_candidate_count = STAGE74_XNU_IOKIT_REGISTRY_TOPOLOGY_CANDIDATE_COUNT;
    contract->selected_topology_entry_count = (contract->selected_topology_entry_mask ==
                                               STAGE74_XNU_IOKIT_REGISTRY_SERVICE_SELECTED_REQUIRED) ?
        STAGE74_XNU_IOKIT_REGISTRY_TOPOLOGY_ENTRY_COUNT : 0u;
    contract->rejected_topology_entry_count = (contract->rejected_unlinked_mask ==
                                               STAGE74_XNU_IOKIT_REGISTRY_SERVICE_REJECTED_REQUIRED) ?
        STAGE74_XNU_IOKIT_REGISTRY_TOPOLOGY_REJECTED_COUNT : 0u;
    contract->topology_dryrun_count = contract->selected_topology_entry_count;
    contract->topology_relationship_count = contract->selected_topology_entry_count;
    contract->rejected_unlinked_count = contract->rejected_topology_entry_count;
    contract->topology_checksum = contract->source_property_inheritance_property_checksum ^
        contract->source_selected_registry_entry_mask ^ contract->source_rejected_unattached_mask ^
        contract->topology_marker_mask ^ contract->registry_plane_match_mask ^ contract->parent_path_match_mask ^
        contract->entry_path_match_mask ^ contract->parent_ordinal_match_mask ^ contract->sibling_order_match_mask ^
        contract->provenance_match_mask ^ contract->selected_topology_entry_mask ^ contract->rejected_unlinked_mask ^
        contract->topology_matrix ^ contract->topology_order_checksum ^ contract->parent_order_checksum ^
        contract->path_checksum ^ contract->topology_candidate_count ^ contract->selected_topology_entry_count ^
        contract->rejected_topology_entry_count ^ contract->topology_dryrun_count ^
        contract->topology_relationship_count ^ contract->rejected_unlinked_count ^ contract->registry_plane_hash;
    contract->expected_topology_checksum = STAGE74_XNU_IOKIT_REGISTRY_TOPOLOGY_EXPECTED_TOPOLOGY_CHECKSUM;

    contract->iokit_reference_mask = preflight->xnu_iokit_property_inheritance_dryrun_contract.iokit_reference_mask;
    contract->iokit_runtime_blocked_mask = preflight->xnu_iokit_property_inheritance_dryrun_contract.iokit_runtime_blocked_mask;
    contract->iokit_reference_count = preflight->xnu_iokit_property_inheritance_dryrun_contract.iokit_reference_count;
    contract->iokit_public_compile_count = preflight->xnu_iokit_property_inheritance_dryrun_contract.iokit_public_compile_count;
    contract->iokit_public_link_count = preflight->xnu_iokit_property_inheritance_dryrun_contract.iokit_public_link_count;
    contract->iokit_reference_only = preflight->xnu_iokit_property_inheritance_dryrun_contract.iokit_reference_only;
    contract->no_iokit_runtime_exec = preflight->xnu_iokit_property_inheritance_dryrun_contract.no_iokit_runtime_exec;
    contract->no_catalog_runtime_exec = preflight->xnu_iokit_property_inheritance_dryrun_contract.no_catalog_runtime_exec;
    contract->no_provider_runtime_exec = preflight->xnu_iokit_property_inheritance_dryrun_contract.no_provider_runtime_exec;
    contract->no_registry_entry_runtime_exec =
        preflight->xnu_iokit_property_inheritance_dryrun_contract.no_registry_entry_runtime_exec;
    contract->no_registry_topology_runtime_exec = 1u;
    contract->no_property_runtime_exec = preflight->xnu_iokit_property_inheritance_dryrun_contract.no_property_runtime_exec;
    contract->no_attach_runtime_exec = preflight->xnu_iokit_property_inheritance_dryrun_contract.no_attach_runtime_exec;
    contract->no_start_runtime_exec = preflight->xnu_iokit_property_inheritance_dryrun_contract.no_start_runtime_exec;
    contract->no_platform_driver_exec = preflight->xnu_iokit_property_inheritance_dryrun_contract.no_platform_driver_exec;
    contract->no_public_xnu_exec = preflight->xnu_iokit_property_inheritance_dryrun_contract.no_public_xnu_exec;
    contract->no_platform_runtime_exec = preflight->xnu_iokit_property_inheritance_dryrun_contract.no_platform_runtime_exec;
    contract->no_public_pmap_exec = preflight->xnu_iokit_property_inheritance_dryrun_contract.no_public_pmap_exec;
    contract->no_live_pmap_tables_installed =
        preflight->xnu_iokit_property_inheritance_dryrun_contract.no_live_pmap_tables_installed;
    contract->proposed_workspace_written =
        preflight->xnu_iokit_property_inheritance_dryrun_contract.proposed_workspace_written;
    contract->pmap_ttbr_written = preflight->xnu_iokit_property_inheritance_dryrun_contract.pmap_ttbr_written;
    contract->pmap_ttbcr_written = preflight->xnu_iokit_property_inheritance_dryrun_contract.pmap_ttbcr_written;
    contract->pmap_dacr_written = preflight->xnu_iokit_property_inheritance_dryrun_contract.pmap_dacr_written;
    contract->pmap_sctlr_written = preflight->xnu_iokit_property_inheritance_dryrun_contract.pmap_sctlr_written;
    contract->pmap_tlbs_invalidated = preflight->xnu_iokit_property_inheritance_dryrun_contract.pmap_tlbs_invalidated;
    contract->caches_changed = preflight->xnu_iokit_property_inheritance_dryrun_contract.caches_changed;
    contract->persistent_write_attempted =
        preflight->xnu_iokit_property_inheritance_dryrun_contract.persistent_write_attempted;
    contract->xnu_start_executed = preflight->xnu_iokit_property_inheritance_dryrun_contract.xnu_start_executed;
    contract->generated_macho_executed =
        preflight->xnu_iokit_property_inheritance_dryrun_contract.generated_macho_executed;
    contract->proposed_platform_gap_mask = preflight->platform_gap_mask;
    contract->proposed_pexpert_gap_mask = preflight->pexpert_gap_mask;
    contract->local_only = 1u;
    contract->fail_closed = 1u;

    source_ok = (contract->source_property_inheritance_status == STAGE74_STATUS_OK &&
                 contract->source_property_inheritance_satisfied_mask ==
                 STAGE74_XNU_IOKIT_PROPERTY_INHERITANCE_DRYRUN_REQUIRED_MASK &&
                 contract->source_property_inheritance_failure_mask == 0u &&
                 contract->source_selected_registry_entry_mask == STAGE74_XNU_IOKIT_REGISTRY_SERVICE_SELECTED_REQUIRED &&
                 contract->source_rejected_unattached_mask == STAGE74_XNU_IOKIT_REGISTRY_SERVICE_REJECTED_REQUIRED &&
                 contract->source_inherited_property_mask == STAGE74_XNU_IOKIT_REGISTRY_SERVICE_SELECTED_REQUIRED &&
                 contract->source_instance_property_mask == STAGE74_XNU_IOKIT_REGISTRY_SERVICE_SELECTED_REQUIRED &&
                 contract->source_attach_deferred_mask == STAGE74_XNU_IOKIT_REGISTRY_SERVICE_SELECTED_REQUIRED &&
                 contract->source_start_deferred_mask == STAGE74_XNU_IOKIT_REGISTRY_SERVICE_SELECTED_REQUIRED &&
                 contract->source_property_inheritance_matrix ==
                 (STAGE74_XNU_IOKIT_REGISTRY_SERVICE_SELECTED_REQUIRED |
                  STAGE74_XNU_IOKIT_REGISTRY_SERVICE_REJECTED_REQUIRED) &&
                 contract->source_catalog_status == STAGE74_STATUS_OK &&
                 contract->source_provider_status == STAGE74_STATUS_OK &&
                 contract->source_registry_status == STAGE74_STATUS_OK &&
                 contract->source_match_status == STAGE74_STATUS_OK &&
                 contract->source_scaffold_status == STAGE74_STATUS_OK &&
                 contract->source_pexpert_status == STAGE74_STATUS_OK &&
                 contract->source_pmap_transition_status == STAGE74_STATUS_OK &&
                 contract->source_compile_graph_status == STAGE74_STATUS_OK &&
                 contract->source_object_subset_status == STAGE74_STATUS_OK &&
                 contract->source_link_status == STAGE74_STATUS_OK &&
                 contract->source_loader_safety_mask == contract->source_loader_safety_required_mask) ? 1u : 0u;
    if (source_ok) {
        contract->satisfied_mask |= STAGE74_XNU_IOKIT_REGISTRY_TOPOLOGY_DRYRUN_SAT_SOURCE_PROPERTY_INHERITANCE;
    } else {
        contract->failure_mask |= STAGE74_XNU_IOKIT_REGISTRY_TOPOLOGY_DRYRUN_FAIL_SOURCE;
    }

    if (contract->apple_dt_semantic_mask == STAGE74_DT_READY_REQUIRED) {
        contract->satisfied_mask |= STAGE74_XNU_IOKIT_REGISTRY_TOPOLOGY_DRYRUN_SAT_APPLE_DT;
    } else {
        contract->failure_mask |= STAGE74_XNU_IOKIT_REGISTRY_TOPOLOGY_DRYRUN_FAIL_DT;
    }

    personality_nodes_ok = (contract->platform_personality_node_ptr != 0u &&
                            contract->platform_personality_prop_count >= 23u &&
                            contract->interrupt_personality_node_ptr != 0u &&
                            contract->interrupt_personality_prop_count >= 23u &&
                            contract->timer_personality_node_ptr != 0u &&
                            contract->timer_personality_prop_count >= 23u &&
                            contract->cpu_personality_node_ptr != 0u &&
                            contract->cpu_personality_prop_count >= 23u &&
                            contract->rejected_personality_node_ptr != 0u &&
                            contract->rejected_personality_prop_count >= 22u) ? 1u : 0u;
    if (personality_nodes_ok) {
        contract->satisfied_mask |= STAGE74_XNU_IOKIT_REGISTRY_TOPOLOGY_DRYRUN_SAT_PERSONALITY_NODE_SET;
    } else {
        contract->failure_mask |= STAGE74_XNU_IOKIT_REGISTRY_TOPOLOGY_DRYRUN_FAIL_PERSONALITY;
    }

    if (contract->topology_marker_mask == STAGE74_XNU_IOKIT_REGISTRY_SERVICE_SELECTED_REQUIRED) {
        contract->satisfied_mask |= STAGE74_XNU_IOKIT_REGISTRY_TOPOLOGY_DRYRUN_SAT_TOPOLOGY_MARKER_MATRIX;
    } else {
        contract->failure_mask |= STAGE74_XNU_IOKIT_REGISTRY_TOPOLOGY_DRYRUN_FAIL_TOPOLOGY;
    }
    if (contract->registry_plane_match_mask == STAGE74_XNU_IOKIT_REGISTRY_SERVICE_SELECTED_REQUIRED &&
        contract->registry_plane_hash == STAGE74_XNU_IOKIT_REGISTRY_TOPOLOGY_PLANE_HASH) {
        contract->satisfied_mask |= STAGE74_XNU_IOKIT_REGISTRY_TOPOLOGY_DRYRUN_SAT_REGISTRY_PLANE_MATRIX;
    } else {
        contract->failure_mask |= STAGE74_XNU_IOKIT_REGISTRY_TOPOLOGY_DRYRUN_FAIL_TOPOLOGY;
    }
    if (contract->parent_path_match_mask == STAGE74_XNU_IOKIT_REGISTRY_SERVICE_SELECTED_REQUIRED) {
        contract->satisfied_mask |= STAGE74_XNU_IOKIT_REGISTRY_TOPOLOGY_DRYRUN_SAT_PARENT_PATH_MATRIX;
    } else {
        contract->failure_mask |= STAGE74_XNU_IOKIT_REGISTRY_TOPOLOGY_DRYRUN_FAIL_TOPOLOGY;
    }
    if (contract->entry_path_match_mask == STAGE74_XNU_IOKIT_REGISTRY_SERVICE_SELECTED_REQUIRED) {
        contract->satisfied_mask |= STAGE74_XNU_IOKIT_REGISTRY_TOPOLOGY_DRYRUN_SAT_ENTRY_PATH_MATRIX;
    } else {
        contract->failure_mask |= STAGE74_XNU_IOKIT_REGISTRY_TOPOLOGY_DRYRUN_FAIL_TOPOLOGY;
    }
    if (contract->parent_ordinal_match_mask == STAGE74_XNU_IOKIT_REGISTRY_SERVICE_SELECTED_REQUIRED &&
        contract->parent_order_checksum == 0u) {
        contract->satisfied_mask |= STAGE74_XNU_IOKIT_REGISTRY_TOPOLOGY_DRYRUN_SAT_PARENT_ORDINAL_MATRIX;
    } else {
        contract->failure_mask |= STAGE74_XNU_IOKIT_REGISTRY_TOPOLOGY_DRYRUN_FAIL_TOPOLOGY;
    }
    if (contract->sibling_order_match_mask == STAGE74_XNU_IOKIT_REGISTRY_SERVICE_SELECTED_REQUIRED &&
        contract->topology_order_checksum == STAGE74_XNU_IOKIT_REGISTRY_TOPOLOGY_REJECTED_ORDINAL) {
        contract->satisfied_mask |= STAGE74_XNU_IOKIT_REGISTRY_TOPOLOGY_DRYRUN_SAT_SIBLING_ORDER_MATRIX;
    } else {
        contract->failure_mask |= STAGE74_XNU_IOKIT_REGISTRY_TOPOLOGY_DRYRUN_FAIL_TOPOLOGY;
    }
    if (contract->provenance_match_mask == STAGE74_XNU_IOKIT_REGISTRY_SERVICE_SELECTED_REQUIRED) {
        contract->satisfied_mask |= STAGE74_XNU_IOKIT_REGISTRY_TOPOLOGY_DRYRUN_SAT_PROVENANCE_MATRIX;
    } else {
        contract->failure_mask |= STAGE74_XNU_IOKIT_REGISTRY_TOPOLOGY_DRYRUN_FAIL_TOPOLOGY;
    }
    if (contract->selected_topology_entry_mask == STAGE74_XNU_IOKIT_REGISTRY_SERVICE_SELECTED_REQUIRED) {
        contract->satisfied_mask |= STAGE74_XNU_IOKIT_REGISTRY_TOPOLOGY_DRYRUN_SAT_SELECTED_TOPOLOGY_MATRIX;
    } else {
        contract->failure_mask |= STAGE74_XNU_IOKIT_REGISTRY_TOPOLOGY_DRYRUN_FAIL_TOPOLOGY;
    }

    if (contract->rejected_unlinked_mask == STAGE74_XNU_IOKIT_REGISTRY_SERVICE_REJECTED_REQUIRED &&
        contract->source_rejected_unattached_mask == STAGE74_XNU_IOKIT_REGISTRY_SERVICE_REJECTED_REQUIRED &&
        contract->rejected_parent_ordinal == STAGE74_XNU_IOKIT_REGISTRY_TOPOLOGY_REJECTED_ORDINAL &&
        contract->rejected_sibling_order == STAGE74_XNU_IOKIT_REGISTRY_TOPOLOGY_REJECTED_ORDINAL &&
        contract->rejected_unlinked_count == STAGE74_XNU_IOKIT_REGISTRY_TOPOLOGY_REJECTED_COUNT) {
        contract->satisfied_mask |= STAGE74_XNU_IOKIT_REGISTRY_TOPOLOGY_DRYRUN_SAT_REJECT_UNLINKED;
    } else {
        contract->failure_mask |= STAGE74_XNU_IOKIT_REGISTRY_TOPOLOGY_DRYRUN_FAIL_REJECTED;
    }

    if (contract->topology_candidate_count == STAGE74_XNU_IOKIT_REGISTRY_TOPOLOGY_CANDIDATE_COUNT &&
        contract->selected_topology_entry_count == STAGE74_XNU_IOKIT_REGISTRY_TOPOLOGY_ENTRY_COUNT &&
        contract->rejected_topology_entry_count == STAGE74_XNU_IOKIT_REGISTRY_TOPOLOGY_REJECTED_COUNT &&
        contract->topology_dryrun_count == STAGE74_XNU_IOKIT_REGISTRY_TOPOLOGY_ENTRY_COUNT &&
        contract->topology_relationship_count == STAGE74_XNU_IOKIT_REGISTRY_TOPOLOGY_ENTRY_COUNT &&
        contract->topology_matrix == (STAGE74_XNU_IOKIT_REGISTRY_SERVICE_SELECTED_REQUIRED |
                                      STAGE74_XNU_IOKIT_REGISTRY_SERVICE_REJECTED_REQUIRED)) {
        contract->satisfied_mask |= STAGE74_XNU_IOKIT_REGISTRY_TOPOLOGY_DRYRUN_SAT_DRYRUN_COUNTS;
    } else {
        contract->failure_mask |= STAGE74_XNU_IOKIT_REGISTRY_TOPOLOGY_DRYRUN_FAIL_COUNTS;
    }
    if (contract->path_checksum == STAGE74_XNU_IOKIT_REGISTRY_TOPOLOGY_PLANE_HASH) {
        contract->satisfied_mask |= STAGE74_XNU_IOKIT_REGISTRY_TOPOLOGY_DRYRUN_SAT_PATH_CHECKSUM;
    } else {
        contract->failure_mask |= STAGE74_XNU_IOKIT_REGISTRY_TOPOLOGY_DRYRUN_FAIL_CHECKSUM;
    }
    if (contract->topology_checksum == contract->expected_topology_checksum) {
        contract->satisfied_mask |= STAGE74_XNU_IOKIT_REGISTRY_TOPOLOGY_DRYRUN_SAT_TOPOLOGY_CHECKSUM;
    } else {
        contract->failure_mask |= STAGE74_XNU_IOKIT_REGISTRY_TOPOLOGY_DRYRUN_FAIL_CHECKSUM;
    }

    if (contract->iokit_reference_mask == STAGE74_XNU_IOKIT_REFERENCE_REQUIRED &&
        contract->iokit_runtime_blocked_mask == STAGE74_XNU_IOKIT_REFERENCE_REQUIRED &&
        contract->iokit_reference_count >= 3u && contract->iokit_public_compile_count == 0u &&
        contract->iokit_public_link_count == 0u && contract->iokit_reference_only == 1u) {
        contract->satisfied_mask |= STAGE74_XNU_IOKIT_REGISTRY_TOPOLOGY_DRYRUN_SAT_IOKIT_REFERENCE;
    } else {
        contract->failure_mask |= STAGE74_XNU_IOKIT_REGISTRY_TOPOLOGY_DRYRUN_FAIL_PUBLIC_BOUNDARY;
    }

    public_boundary_ok = (contract->no_public_xnu_exec == 1u && contract->no_platform_runtime_exec == 1u &&
                          contract->no_iokit_runtime_exec == 1u && contract->no_catalog_runtime_exec == 1u &&
                          contract->no_provider_runtime_exec == 1u &&
                          contract->no_registry_entry_runtime_exec == 1u &&
                          contract->no_registry_topology_runtime_exec == 1u &&
                          contract->no_property_runtime_exec == 1u && contract->no_attach_runtime_exec == 1u &&
                          contract->no_start_runtime_exec == 1u && contract->no_platform_driver_exec == 1u &&
                          contract->source_object_subset_status == STAGE74_STATUS_OK &&
                          contract->source_link_status == STAGE74_STATUS_OK) ? 1u : 0u;
    if (public_boundary_ok) {
        contract->satisfied_mask |= STAGE74_XNU_IOKIT_REGISTRY_TOPOLOGY_DRYRUN_SAT_PUBLIC_BOUNDARY |
            STAGE74_XNU_IOKIT_REGISTRY_TOPOLOGY_DRYRUN_SAT_NO_IOKIT_RUNTIME_EXEC |
            STAGE74_XNU_IOKIT_REGISTRY_TOPOLOGY_DRYRUN_SAT_NO_REGISTRY_ENTRY_RUNTIME_EXEC |
            STAGE74_XNU_IOKIT_REGISTRY_TOPOLOGY_DRYRUN_SAT_NO_REGISTRY_TOPOLOGY_RUNTIME_EXEC |
            STAGE74_XNU_IOKIT_REGISTRY_TOPOLOGY_DRYRUN_SAT_NO_PROPERTY_RUNTIME_EXEC |
            STAGE74_XNU_IOKIT_REGISTRY_TOPOLOGY_DRYRUN_SAT_NO_ATTACH_START_RUNTIME_EXEC |
            STAGE74_XNU_IOKIT_REGISTRY_TOPOLOGY_DRYRUN_SAT_NO_PUBLIC_XNU_EXEC;
    } else {
        contract->failure_mask |= STAGE74_XNU_IOKIT_REGISTRY_TOPOLOGY_DRYRUN_FAIL_PUBLIC_BOUNDARY;
    }

    if (contract->no_public_pmap_exec == 1u) {
        contract->satisfied_mask |= STAGE74_XNU_IOKIT_REGISTRY_TOPOLOGY_DRYRUN_SAT_NO_PUBLIC_PMAP_EXEC;
    } else {
        contract->failure_mask |= STAGE74_XNU_IOKIT_REGISTRY_TOPOLOGY_DRYRUN_FAIL_PMAP_BOUNDARY;
    }

    pmap_boundary_ok = (contract->no_live_pmap_tables_installed == 0u &&
                        contract->proposed_workspace_written == 0u &&
                        contract->pmap_ttbr_written == 0u && contract->pmap_ttbcr_written == 0u &&
                        contract->pmap_dacr_written == 0u && contract->pmap_sctlr_written == 0u &&
                        contract->pmap_tlbs_invalidated == 0u && contract->caches_changed == 0u) ? 1u : 0u;
    if (contract->no_live_pmap_tables_installed == 0u) {
        contract->satisfied_mask |= STAGE74_XNU_IOKIT_REGISTRY_TOPOLOGY_DRYRUN_SAT_NO_LIVE_PMAP_INSTALL;
    }
    if (contract->pmap_tlbs_invalidated == 0u && contract->caches_changed == 0u) {
        contract->satisfied_mask |= STAGE74_XNU_IOKIT_REGISTRY_TOPOLOGY_DRYRUN_SAT_NO_TLB_CACHE_CHANGE;
    }
    if (pmap_boundary_ok && contract->source_pmap_transition_status == STAGE74_STATUS_OK) {
        contract->satisfied_mask |= STAGE74_XNU_IOKIT_REGISTRY_TOPOLOGY_DRYRUN_SAT_PMAP_BOUNDARY;
    } else {
        contract->failure_mask |= STAGE74_XNU_IOKIT_REGISTRY_TOPOLOGY_DRYRUN_FAIL_PMAP_BOUNDARY;
    }

    if (contract->generated_macho_executed == 0u && contract->xnu_start_executed == 0u) {
        contract->satisfied_mask |= STAGE74_XNU_IOKIT_REGISTRY_TOPOLOGY_DRYRUN_SAT_NO_XNU_MACHO_EXEC;
    } else {
        contract->failure_mask |= STAGE74_XNU_IOKIT_REGISTRY_TOPOLOGY_DRYRUN_FAIL_SAFETY_BOUNDARY;
    }
    if (contract->persistent_write_attempted == 0u) {
        contract->satisfied_mask |= STAGE74_XNU_IOKIT_REGISTRY_TOPOLOGY_DRYRUN_SAT_NO_PERSIST_WRITE;
    } else {
        contract->failure_mask |= STAGE74_XNU_IOKIT_REGISTRY_TOPOLOGY_DRYRUN_FAIL_SAFETY_BOUNDARY;
    }
    if (contract->local_only == 1u) {
        contract->satisfied_mask |= STAGE74_XNU_IOKIT_REGISTRY_TOPOLOGY_DRYRUN_SAT_LOCAL_ONLY;
    } else {
        contract->failure_mask |= STAGE74_XNU_IOKIT_REGISTRY_TOPOLOGY_DRYRUN_FAIL_SAFETY_BOUNDARY;
    }

    if (contract->local_only == 1u && contract->fail_closed == 1u &&
        contract->persistent_write_attempted == 0u && contract->xnu_start_executed == 0u &&
        contract->generated_macho_executed == 0u &&
        contract->proposed_platform_gap_mask == (STAGE74_PLATFORM_GAP_REQUIRED_RECORDED & ~STAGE74_PLATFORM_GAP_IOKIT_STACK) &&
        contract->proposed_pexpert_gap_mask == 0u &&
        contract->selected_topology_entry_mask == STAGE74_XNU_IOKIT_REGISTRY_SERVICE_SELECTED_REQUIRED &&
        contract->rejected_unlinked_mask == STAGE74_XNU_IOKIT_REGISTRY_SERVICE_REJECTED_REQUIRED) {
        contract->satisfied_mask |= STAGE74_XNU_IOKIT_REGISTRY_TOPOLOGY_DRYRUN_SAT_FAIL_CLOSED;
    } else {
        contract->failure_mask |= STAGE74_XNU_IOKIT_REGISTRY_TOPOLOGY_DRYRUN_FAIL_ROLLUP |
            STAGE74_XNU_IOKIT_REGISTRY_TOPOLOGY_DRYRUN_FAIL_SAFETY_BOUNDARY;
    }

    contract->checksum = stage74_iokit_registry_topology_dryrun_checksum(contract);
    if (contract->checksum != stage74_iokit_registry_topology_dryrun_checksum(contract)) {
        contract->failure_mask |= STAGE74_XNU_IOKIT_REGISTRY_TOPOLOGY_DRYRUN_FAIL_CHECKSUM;
    }

    if (contract->satisfied_mask == contract->required_mask && contract->failure_mask == 0u) {
        contract->status = STAGE74_STATUS_OK;
    } else {
        contract->status = STAGE74_STATUS_BASE | contract->failure_mask;
    }

    stage74_iokit_registry_topology_dryrun_log(contract);
    return contract->status == STAGE74_STATUS_OK;
}

const struct stage74_xnu_iokit_registry_topology_dryrun_contract *
stage74_xnu_iokit_registry_topology_dryrun_contract_result(void)
{
    return &g_stage74_xnu_iokit_registry_topology_dryrun_contract;
}
