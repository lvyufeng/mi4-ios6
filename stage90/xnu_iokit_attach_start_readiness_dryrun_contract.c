#include "stage90.h"

static struct stage90_xnu_iokit_attach_start_readiness_dryrun_contract
    g_stage90_xnu_iokit_attach_start_readiness_dryrun_contract;

static uint32_t stage90_iokit_attach_start_readiness_dryrun_checksum(
    volatile const struct stage90_xnu_iokit_attach_start_readiness_dryrun_contract *contract)
{
    volatile const uint32_t *words = (volatile const uint32_t *)(uintptr_t)contract;
    uint32_t count = (uint32_t)(offsetof(struct stage90_xnu_iokit_attach_start_readiness_dryrun_contract,
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

static void stage90_iokit_attach_start_readiness_dryrun_log(
    const struct stage90_xnu_iokit_attach_start_readiness_dryrun_contract *contract)
{
    xnu_log_kv32("stage90_xnu_iokit_attach_start_readiness_dryrun_contract_status", contract->status);
    xnu_log_kv32("stage90_xnu_iokit_attach_start_readiness_dryrun_contract_required_mask", contract->required_mask);
    xnu_log_kv32("stage90_xnu_iokit_attach_start_readiness_dryrun_contract_satisfied_mask", contract->satisfied_mask);
    xnu_log_kv32("stage90_xnu_iokit_attach_start_readiness_dryrun_contract_failure_mask", contract->failure_mask);
    xnu_log_kv32("stage90_xnu_iokit_attach_start_readiness_dryrun_contract_checksum", contract->checksum);
    xnu_log_kv32("stage90_xnu_iokit_attach_start_readiness_source_topology_status", contract->source_topology_status);
    xnu_log_kv32("stage90_xnu_iokit_attach_start_readiness_source_topology_satisfied_mask", contract->source_topology_satisfied_mask);
    xnu_log_kv32("stage90_xnu_iokit_attach_start_readiness_source_topology_failure_mask", contract->source_topology_failure_mask);
    xnu_log_kv32("stage90_xnu_iokit_attach_start_readiness_source_topology_checksum", contract->source_topology_checksum);
    xnu_log_kv32("stage90_xnu_iokit_attach_start_readiness_source_topology_dryrun_checksum", contract->source_topology_topology_checksum);
    xnu_log_kv32("stage90_xnu_iokit_attach_start_readiness_source_selected_topology_entry_mask", contract->source_selected_topology_entry_mask);
    xnu_log_kv32("stage90_xnu_iokit_attach_start_readiness_source_rejected_unlinked_mask", contract->source_rejected_unlinked_mask);
    xnu_log_kv32("stage90_xnu_iokit_attach_start_readiness_source_topology_matrix", contract->source_topology_matrix);
    xnu_log_kv32("stage90_xnu_iokit_attach_start_readiness_apple_dt_semantic_mask", contract->apple_dt_semantic_mask);
    xnu_log_kv32("stage90_xnu_iokit_attach_start_readiness_platform_props", contract->platform_personality_prop_count);
    xnu_log_kv32("stage90_xnu_iokit_attach_start_readiness_interrupt_props", contract->interrupt_personality_prop_count);
    xnu_log_kv32("stage90_xnu_iokit_attach_start_readiness_timer_props", contract->timer_personality_prop_count);
    xnu_log_kv32("stage90_xnu_iokit_attach_start_readiness_cpu_props", contract->cpu_personality_prop_count);
    xnu_log_kv32("stage90_xnu_iokit_attach_start_readiness_rejected_props", contract->rejected_personality_prop_count);
    xnu_log_kv32("stage90_xnu_iokit_attach_start_readiness_attach_readiness_mask", contract->attach_readiness_mask);
    xnu_log_kv32("stage90_xnu_iokit_attach_start_readiness_start_readiness_mask", contract->start_readiness_mask);
    xnu_log_kv32("stage90_xnu_iokit_attach_start_readiness_attach_provider_path_match_mask", contract->attach_provider_path_match_mask);
    xnu_log_kv32("stage90_xnu_iokit_attach_start_readiness_start_provider_path_match_mask", contract->start_provider_path_match_mask);
    xnu_log_kv32("stage90_xnu_iokit_attach_start_readiness_attach_provider_ordinal_match_mask", contract->attach_provider_ordinal_match_mask);
    xnu_log_kv32("stage90_xnu_iokit_attach_start_readiness_start_provider_ordinal_match_mask", contract->start_provider_ordinal_match_mask);
    xnu_log_kv32("stage90_xnu_iokit_attach_start_readiness_attach_order_match_mask", contract->attach_order_match_mask);
    xnu_log_kv32("stage90_xnu_iokit_attach_start_readiness_start_order_match_mask", contract->start_order_match_mask);
    xnu_log_kv32("stage90_xnu_iokit_attach_start_readiness_provider_start_required_mask", contract->provider_start_required_mask);
    xnu_log_kv32("stage90_xnu_iokit_attach_start_readiness_provider_start_satisfied_mask", contract->provider_start_satisfied_mask);
    xnu_log_kv32("stage90_xnu_iokit_attach_start_readiness_attach_runtime_blocked_mask", contract->attach_runtime_blocked_mask);
    xnu_log_kv32("stage90_xnu_iokit_attach_start_readiness_start_runtime_blocked_mask", contract->start_runtime_blocked_mask);
    xnu_log_kv32("stage90_xnu_iokit_attach_start_readiness_provenance_match_mask", contract->attach_start_provenance_match_mask);
    xnu_log_kv32("stage90_xnu_iokit_attach_start_readiness_source_attach_deferred_mask", contract->source_attach_deferred_mask);
    xnu_log_kv32("stage90_xnu_iokit_attach_start_readiness_source_start_deferred_mask", contract->source_start_deferred_mask);
    xnu_log_kv32("stage90_xnu_iokit_attach_start_readiness_selected_attach_start_entry_mask", contract->selected_attach_start_entry_mask);
    xnu_log_kv32("stage90_xnu_iokit_attach_start_readiness_rejected_attach_start_mask", contract->rejected_attach_start_mask);
    xnu_log_kv32("stage90_xnu_iokit_attach_start_readiness_attach_start_matrix", contract->attach_start_matrix);
    xnu_log_kv32("stage90_xnu_iokit_attach_start_readiness_attach_order_checksum", contract->attach_order_checksum);
    xnu_log_kv32("stage90_xnu_iokit_attach_start_readiness_start_order_checksum", contract->start_order_checksum);
    xnu_log_kv32("stage90_xnu_iokit_attach_start_readiness_path_hash", contract->attach_start_path_hash);
    xnu_log_kv32("stage90_xnu_iokit_attach_start_readiness_checksum", contract->attach_start_checksum);
    xnu_log_kv32("stage90_xnu_iokit_attach_start_readiness_expected_checksum", contract->expected_attach_start_checksum);
    xnu_log_kv32("stage90_xnu_iokit_attach_start_readiness_candidate_count", contract->attach_start_candidate_count);
    xnu_log_kv32("stage90_xnu_iokit_attach_start_readiness_selected_entry_count", contract->selected_attach_start_entry_count);
    xnu_log_kv32("stage90_xnu_iokit_attach_start_readiness_rejected_entry_count", contract->rejected_attach_start_entry_count);
    xnu_log_kv32("stage90_xnu_iokit_attach_start_readiness_attach_count", contract->attach_readiness_count);
    xnu_log_kv32("stage90_xnu_iokit_attach_start_readiness_start_count", contract->start_readiness_count);
    xnu_log_kv32("stage90_xnu_iokit_attach_start_readiness_provider_required_count", contract->provider_start_required_count);
    xnu_log_kv32("stage90_xnu_iokit_attach_start_readiness_provider_satisfied_count", contract->provider_start_satisfied_count);
    xnu_log_kv32("stage90_xnu_iokit_attach_start_readiness_rejected_unattached_unstarted_count", contract->rejected_unattached_unstarted_count);
    xnu_log_kv32("stage90_xnu_iokit_attach_start_readiness_public_compile_count", contract->iokit_public_compile_count);
    xnu_log_kv32("stage90_xnu_iokit_attach_start_readiness_public_link_count", contract->iokit_public_link_count);
    xnu_log_kv32("stage90_xnu_iokit_attach_start_readiness_no_iokit_runtime_exec", contract->no_iokit_runtime_exec);
    xnu_log_kv32("stage90_xnu_iokit_attach_start_readiness_no_registry_topology_runtime_exec", contract->no_registry_topology_runtime_exec);
    xnu_log_kv32("stage90_xnu_iokit_attach_start_readiness_no_attach_runtime_exec", contract->no_attach_runtime_exec);
    xnu_log_kv32("stage90_xnu_iokit_attach_start_readiness_no_start_runtime_exec", contract->no_start_runtime_exec);
    xnu_log_kv32("stage90_xnu_iokit_attach_start_readiness_no_live_pmap_tables_installed", contract->no_live_pmap_tables_installed);
    xnu_log_kv32("stage90_xnu_iokit_attach_start_readiness_proposed_workspace_written", contract->proposed_workspace_written);
    xnu_log_kv32("stage90_xnu_iokit_attach_start_readiness_pmap_ttbr_written", contract->pmap_ttbr_written);
    xnu_log_kv32("stage90_xnu_iokit_attach_start_readiness_pmap_tlbs_invalidated", contract->pmap_tlbs_invalidated);
    xnu_log_kv32("stage90_xnu_iokit_attach_start_readiness_caches_changed", contract->caches_changed);
    xnu_log_kv32("stage90_xnu_iokit_attach_start_readiness_persistent_write_attempted", contract->persistent_write_attempted);
}

static void selected_masks_for_node(const struct boot_args *args, const void *node, uint32_t bit,
                                    const char *provider_path, uint32_t provider_ordinal,
                                    uint32_t order, uint32_t provider_required,
                                    struct stage90_xnu_iokit_attach_start_readiness_dryrun_contract *contract)
{
    if (u32_prop_equals(args, node, "attach-start-readiness-dryrun", 1u) &&
        u32_prop_equals(args, node, "attach-readiness", 1u)) {
        contract->attach_readiness_mask |= bit;
    }
    if (u32_prop_equals(args, node, "attach-start-readiness-dryrun", 1u) &&
        u32_prop_equals(args, node, "start-readiness", 1u)) {
        contract->start_readiness_mask |= bit;
    }
    if (string_prop_equals(args, node, "attach-provider-path", provider_path)) {
        contract->attach_provider_path_match_mask |= bit;
    }
    if (string_prop_equals(args, node, "start-provider-path", provider_path)) {
        contract->start_provider_path_match_mask |= bit;
    }
    if (u32_prop_equals(args, node, "attach-provider-ordinal", provider_ordinal)) {
        contract->attach_provider_ordinal_match_mask |= bit;
    }
    if (u32_prop_equals(args, node, "start-provider-ordinal", provider_ordinal)) {
        contract->start_provider_ordinal_match_mask |= bit;
    }
    if (u32_prop_equals(args, node, "attach-order", order)) {
        contract->attach_order_match_mask |= bit;
    }
    if (u32_prop_equals(args, node, "start-order", order)) {
        contract->start_order_match_mask |= bit;
    }
    if (provider_required != 0u) {
        contract->provider_start_required_mask |= bit;
        if (u32_prop_equals(args, node, "provider-start-required", provider_required)) {
            contract->provider_start_satisfied_mask |= bit;
        }
    } else {
        (void)u32_prop_equals(args, node, "provider-start-required", 0u);
    }
    if (u32_prop_equals(args, node, "attach-runtime-exec", 0u)) {
        contract->attach_runtime_blocked_mask |= bit;
    }
    if (u32_prop_equals(args, node, "start-runtime-exec", 0u)) {
        contract->start_runtime_blocked_mask |= bit;
    }
    if (string_prop_equals(args, node, "attach-start-provenance", "stage90-registry-topology")) {
        contract->attach_start_provenance_match_mask |= bit;
    }
}

int stage90_xnu_iokit_attach_start_readiness_dryrun_contract_selftest(
    const struct stage90_loader_preflight *preflight)
{
    struct stage90_xnu_iokit_attach_start_readiness_dryrun_contract *contract =
        &g_stage90_xnu_iokit_attach_start_readiness_dryrun_contract;
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
    contract->version = STAGE90_XNU_IOKIT_ATTACH_START_READINESS_DRYRUN_CONTRACT_VERSION;
    contract->size = sizeof(*contract);
    contract->status = STAGE90_STATUS_BASE;
    contract->required_mask = STAGE90_XNU_IOKIT_ATTACH_START_READINESS_DRYRUN_REQUIRED_MASK;

    if (!preflight || !args || !root) {
        contract->failure_mask = STAGE90_XNU_IOKIT_ATTACH_START_READINESS_DRYRUN_FAIL_SOURCE |
            STAGE90_XNU_IOKIT_ATTACH_START_READINESS_DRYRUN_FAIL_SAFETY_BOUNDARY;
        contract->checksum = stage90_iokit_attach_start_readiness_dryrun_checksum(contract);
        stage90_iokit_attach_start_readiness_dryrun_log(contract);
        return 0;
    }

    contract->source_topology_status = preflight->xnu_iokit_registry_topology_dryrun_contract_status;
    contract->source_topology_satisfied_mask = preflight->xnu_iokit_registry_topology_dryrun_contract_satisfied_mask;
    contract->source_topology_failure_mask = preflight->xnu_iokit_registry_topology_dryrun_contract_failure_mask;
    contract->source_topology_checksum = preflight->xnu_iokit_registry_topology_dryrun_contract_checksum;
    contract->source_topology_topology_checksum = preflight->xnu_iokit_registry_topology_dryrun_contract.topology_checksum;
    contract->source_selected_topology_entry_mask =
        preflight->xnu_iokit_registry_topology_dryrun_contract.selected_topology_entry_mask;
    contract->source_rejected_unlinked_mask = preflight->xnu_iokit_registry_topology_dryrun_contract.rejected_unlinked_mask;
    contract->source_topology_matrix = preflight->xnu_iokit_registry_topology_dryrun_contract.topology_matrix;
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
    contract->source_loader_safety_required_mask = STAGE90_LOADER_SAFETY_REQUIRED;
    contract->boot_args_ptr = preflight->boot_args_ptr;
    contract->device_tree_ptr = preflight->device_tree_ptr;
    contract->device_tree_length = preflight->device_tree_length;
    contract->apple_dt_semantic_mask = preflight->apple_dt_semantic_mask;
    contract->source_attach_deferred_mask =
        preflight->xnu_iokit_property_inheritance_dryrun_contract.attach_deferred_mask;
    contract->source_start_deferred_mask =
        preflight->xnu_iokit_property_inheritance_dryrun_contract.start_deferred_mask;

    contract->platform_personality_node_ptr = (uint32_t)(uintptr_t)apple_dt_find_child(args->deviceTreeP,
                                                                                       args->deviceTreeLength,
                                                                                       root,
                                                                                       "stage90-platform-personality");
    contract->interrupt_personality_node_ptr = (uint32_t)(uintptr_t)apple_dt_find_child(args->deviceTreeP,
                                                                                        args->deviceTreeLength,
                                                                                        root,
                                                                                        "stage90-interrupt-personality");
    contract->timer_personality_node_ptr = (uint32_t)(uintptr_t)apple_dt_find_child(args->deviceTreeP,
                                                                                    args->deviceTreeLength,
                                                                                    root,
                                                                                    "stage90-timer-personality");
    contract->cpu_personality_node_ptr = (uint32_t)(uintptr_t)apple_dt_find_child(args->deviceTreeP,
                                                                                  args->deviceTreeLength,
                                                                                  root,
                                                                                  "stage90-cpu-personality");
    contract->rejected_personality_node_ptr = (uint32_t)(uintptr_t)apple_dt_find_child(args->deviceTreeP,
                                                                                       args->deviceTreeLength,
                                                                                       root,
                                                                                       "stage90-rejected-personality");

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

    selected_masks_for_node(args, platform_node, STAGE90_XNU_IOKIT_REGISTRY_SERVICE_PLATFORM_BIT,
                            "IODeviceTree:/", STAGE90_XNU_IOKIT_REGISTRY_TOPOLOGY_REJECTED_ORDINAL,
                            STAGE90_XNU_IOKIT_REGISTRY_TOPOLOGY_PLATFORM_ORDINAL, 0u, contract);
    selected_masks_for_node(args, interrupt_node, STAGE90_XNU_IOKIT_REGISTRY_SERVICE_INTERRUPT_BIT,
                            "IODeviceTree:/stage90-platform-personality",
                            STAGE90_XNU_IOKIT_REGISTRY_TOPOLOGY_PLATFORM_ORDINAL,
                            STAGE90_XNU_IOKIT_REGISTRY_TOPOLOGY_INTERRUPT_ORDINAL, 1u, contract);
    selected_masks_for_node(args, timer_node, STAGE90_XNU_IOKIT_REGISTRY_SERVICE_TIMER_BIT,
                            "IODeviceTree:/stage90-platform-personality",
                            STAGE90_XNU_IOKIT_REGISTRY_TOPOLOGY_PLATFORM_ORDINAL,
                            STAGE90_XNU_IOKIT_REGISTRY_TOPOLOGY_TIMER_ORDINAL, 1u, contract);
    selected_masks_for_node(args, cpu_node, STAGE90_XNU_IOKIT_REGISTRY_SERVICE_CPU_BIT,
                            "IODeviceTree:/stage90-platform-personality",
                            STAGE90_XNU_IOKIT_REGISTRY_TOPOLOGY_PLATFORM_ORDINAL,
                            STAGE90_XNU_IOKIT_REGISTRY_TOPOLOGY_CPU_ORDINAL, 1u, contract);

    contract->platform_attach_provider_ordinal = apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength,
                                                                       platform_node, "attach-provider-ordinal", 0u);
    contract->platform_start_provider_ordinal = apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength,
                                                                      platform_node, "start-provider-ordinal", 0u);
    contract->interrupt_attach_provider_ordinal = apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength,
                                                                        interrupt_node, "attach-provider-ordinal", 0xffffffffu);
    contract->interrupt_start_provider_ordinal = apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength,
                                                                       interrupt_node, "start-provider-ordinal", 0xffffffffu);
    contract->timer_attach_provider_ordinal = apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength,
                                                                    timer_node, "attach-provider-ordinal", 0xffffffffu);
    contract->timer_start_provider_ordinal = apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength,
                                                                   timer_node, "start-provider-ordinal", 0xffffffffu);
    contract->cpu_attach_provider_ordinal = apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength,
                                                                  cpu_node, "attach-provider-ordinal", 0xffffffffu);
    contract->cpu_start_provider_ordinal = apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength,
                                                                 cpu_node, "start-provider-ordinal", 0xffffffffu);
    contract->platform_attach_order = apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength,
                                                            platform_node, "attach-order", 0xffffffffu);
    contract->interrupt_attach_order = apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength,
                                                             interrupt_node, "attach-order", 0xffffffffu);
    contract->timer_attach_order = apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength,
                                                         timer_node, "attach-order", 0xffffffffu);
    contract->cpu_attach_order = apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength,
                                                       cpu_node, "attach-order", 0xffffffffu);
    contract->rejected_attach_order = apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength,
                                                            rejected_node, "attach-order", 0u);
    contract->platform_start_order = apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength,
                                                           platform_node, "start-order", 0xffffffffu);
    contract->interrupt_start_order = apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength,
                                                            interrupt_node, "start-order", 0xffffffffu);
    contract->timer_start_order = apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength,
                                                        timer_node, "start-order", 0xffffffffu);
    contract->cpu_start_order = apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength,
                                                      cpu_node, "start-order", 0xffffffffu);
    contract->rejected_start_order = apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength,
                                                           rejected_node, "start-order", 0u);

    contract->selected_attach_start_entry_mask = contract->attach_readiness_mask & contract->start_readiness_mask &
        contract->attach_provider_path_match_mask & contract->start_provider_path_match_mask &
        contract->attach_provider_ordinal_match_mask & contract->start_provider_ordinal_match_mask &
        contract->attach_order_match_mask & contract->start_order_match_mask &
        contract->attach_runtime_blocked_mask & contract->start_runtime_blocked_mask &
        contract->attach_start_provenance_match_mask & contract->source_selected_topology_entry_mask;

    if (u32_prop_equals(args, rejected_node, "attach-start-readiness-dryrun", 0u) &&
        u32_prop_equals(args, rejected_node, "attach-readiness", 0u) &&
        u32_prop_equals(args, rejected_node, "start-readiness", 0u) &&
        u32_prop_equals(args, rejected_node, "attach-runtime-exec", 0u) &&
        u32_prop_equals(args, rejected_node, "start-runtime-exec", 0u) &&
        string_prop_equals(args, rejected_node, "attach-provider-path", "IODeviceTree:/unlinked") &&
        string_prop_equals(args, rejected_node, "start-provider-path", "IODeviceTree:/unlinked") &&
        u32_prop_equals(args, rejected_node, "attach-provider-ordinal",
                        STAGE90_XNU_IOKIT_REGISTRY_TOPOLOGY_REJECTED_ORDINAL) &&
        u32_prop_equals(args, rejected_node, "start-provider-ordinal",
                        STAGE90_XNU_IOKIT_REGISTRY_TOPOLOGY_REJECTED_ORDINAL) &&
        u32_prop_equals(args, rejected_node, "attach-order", STAGE90_XNU_IOKIT_REGISTRY_TOPOLOGY_REJECTED_ORDINAL) &&
        u32_prop_equals(args, rejected_node, "start-order", STAGE90_XNU_IOKIT_REGISTRY_TOPOLOGY_REJECTED_ORDINAL) &&
        u32_prop_equals(args, rejected_node, "provider-start-required", 0u) &&
        string_prop_equals(args, rejected_node, "attach-start-provenance", "stage90-rejected-unattached") &&
        u32_prop_equals(args, rejected_node, "registry-entry-linked", 0u) &&
        u32_prop_equals(args, rejected_node, "registry-entry-attached", 0u) &&
        u32_prop_equals(args, rejected_node, "provider-plane-published", 0u) &&
        u32_prop_equals(args, rejected_node, "rejected-candidate", 1u) &&
        u32_prop_equals(args, rejected_node, "stage-owned-local-only", 1u)) {
        contract->rejected_attach_start_mask |= STAGE90_XNU_IOKIT_REGISTRY_SERVICE_REJECTED_BIT;
    }

    contract->attach_start_matrix = contract->selected_attach_start_entry_mask | contract->rejected_attach_start_mask;
    contract->attach_order_checksum = contract->platform_attach_order ^ contract->interrupt_attach_order ^
        contract->timer_attach_order ^ contract->cpu_attach_order ^ contract->rejected_attach_order;
    contract->start_order_checksum = contract->platform_start_order ^ contract->interrupt_start_order ^
        contract->timer_start_order ^ contract->cpu_start_order ^ contract->rejected_start_order;
    contract->attach_start_path_hash = STAGE90_XNU_IOKIT_ATTACH_START_READINESS_PATH_HASH;
    contract->attach_start_candidate_count = STAGE90_XNU_IOKIT_ATTACH_START_READINESS_CANDIDATE_COUNT;
    contract->selected_attach_start_entry_count = (contract->selected_attach_start_entry_mask ==
                                                  STAGE90_XNU_IOKIT_REGISTRY_SERVICE_SELECTED_REQUIRED) ?
        STAGE90_XNU_IOKIT_ATTACH_START_READINESS_ENTRY_COUNT : 0u;
    contract->rejected_attach_start_entry_count = (contract->rejected_attach_start_mask ==
                                                  STAGE90_XNU_IOKIT_REGISTRY_SERVICE_REJECTED_REQUIRED) ?
        STAGE90_XNU_IOKIT_ATTACH_START_READINESS_REJECTED_COUNT : 0u;
    contract->attach_readiness_count = contract->selected_attach_start_entry_count;
    contract->start_readiness_count = contract->selected_attach_start_entry_count;
    contract->provider_start_required_count = (contract->provider_start_required_mask ==
                                               (STAGE90_XNU_IOKIT_REGISTRY_SERVICE_INTERRUPT_BIT |
                                                STAGE90_XNU_IOKIT_REGISTRY_SERVICE_TIMER_BIT |
                                                STAGE90_XNU_IOKIT_REGISTRY_SERVICE_CPU_BIT)) ? 3u : 0u;
    contract->provider_start_satisfied_count = ((contract->provider_start_satisfied_mask &
                                                (STAGE90_XNU_IOKIT_REGISTRY_SERVICE_INTERRUPT_BIT |
                                                 STAGE90_XNU_IOKIT_REGISTRY_SERVICE_TIMER_BIT |
                                                 STAGE90_XNU_IOKIT_REGISTRY_SERVICE_CPU_BIT)) ==
                                               (STAGE90_XNU_IOKIT_REGISTRY_SERVICE_INTERRUPT_BIT |
                                                STAGE90_XNU_IOKIT_REGISTRY_SERVICE_TIMER_BIT |
                                                STAGE90_XNU_IOKIT_REGISTRY_SERVICE_CPU_BIT)) ? 3u : 0u;
    contract->rejected_unattached_unstarted_count = contract->rejected_attach_start_entry_count;
    contract->attach_start_checksum = contract->source_topology_topology_checksum ^
        contract->source_selected_topology_entry_mask ^ contract->source_rejected_unlinked_mask ^
        contract->source_topology_matrix ^ contract->attach_readiness_mask ^ contract->start_readiness_mask ^
        contract->attach_provider_path_match_mask ^ contract->start_provider_path_match_mask ^
        contract->attach_provider_ordinal_match_mask ^ contract->start_provider_ordinal_match_mask ^
        contract->attach_order_match_mask ^ contract->start_order_match_mask ^ contract->provider_start_required_mask ^
        contract->provider_start_satisfied_mask ^ contract->attach_runtime_blocked_mask ^ contract->start_runtime_blocked_mask ^
        contract->attach_start_provenance_match_mask ^ contract->source_attach_deferred_mask ^
        contract->source_start_deferred_mask ^ contract->selected_attach_start_entry_mask ^
        contract->rejected_attach_start_mask ^ contract->attach_start_matrix ^ contract->attach_order_checksum ^
        contract->start_order_checksum ^ contract->attach_start_path_hash ^ contract->attach_start_candidate_count ^
        contract->selected_attach_start_entry_count ^ contract->rejected_attach_start_entry_count ^
        contract->attach_readiness_count ^ contract->start_readiness_count ^ contract->provider_start_required_count ^
        contract->provider_start_satisfied_count ^ contract->rejected_unattached_unstarted_count;
    contract->expected_attach_start_checksum = STAGE90_XNU_IOKIT_ATTACH_START_READINESS_EXPECTED_CHECKSUM;

    contract->iokit_reference_mask = preflight->xnu_iokit_registry_topology_dryrun_contract.iokit_reference_mask;
    contract->iokit_runtime_blocked_mask = preflight->xnu_iokit_registry_topology_dryrun_contract.iokit_runtime_blocked_mask;
    contract->iokit_reference_count = preflight->xnu_iokit_registry_topology_dryrun_contract.iokit_reference_count;
    contract->iokit_public_compile_count = preflight->xnu_iokit_registry_topology_dryrun_contract.iokit_public_compile_count;
    contract->iokit_public_link_count = preflight->xnu_iokit_registry_topology_dryrun_contract.iokit_public_link_count;
    contract->iokit_reference_only = preflight->xnu_iokit_registry_topology_dryrun_contract.iokit_reference_only;
    contract->no_iokit_runtime_exec = preflight->xnu_iokit_registry_topology_dryrun_contract.no_iokit_runtime_exec;
    contract->no_catalog_runtime_exec = preflight->xnu_iokit_registry_topology_dryrun_contract.no_catalog_runtime_exec;
    contract->no_provider_runtime_exec = preflight->xnu_iokit_registry_topology_dryrun_contract.no_provider_runtime_exec;
    contract->no_registry_entry_runtime_exec =
        preflight->xnu_iokit_registry_topology_dryrun_contract.no_registry_entry_runtime_exec;
    contract->no_registry_topology_runtime_exec =
        preflight->xnu_iokit_registry_topology_dryrun_contract.no_registry_topology_runtime_exec;
    contract->no_property_runtime_exec = preflight->xnu_iokit_registry_topology_dryrun_contract.no_property_runtime_exec;
    contract->no_attach_runtime_exec = 1u;
    contract->no_start_runtime_exec = 1u;
    contract->no_platform_driver_exec = preflight->xnu_iokit_registry_topology_dryrun_contract.no_platform_driver_exec;
    contract->no_public_xnu_exec = preflight->xnu_iokit_registry_topology_dryrun_contract.no_public_xnu_exec;
    contract->no_platform_runtime_exec = preflight->xnu_iokit_registry_topology_dryrun_contract.no_platform_runtime_exec;
    contract->no_public_pmap_exec = preflight->xnu_iokit_registry_topology_dryrun_contract.no_public_pmap_exec;
    contract->no_live_pmap_tables_installed =
        preflight->xnu_iokit_registry_topology_dryrun_contract.no_live_pmap_tables_installed;
    contract->proposed_workspace_written = preflight->xnu_iokit_registry_topology_dryrun_contract.proposed_workspace_written;
    contract->pmap_ttbr_written = preflight->xnu_iokit_registry_topology_dryrun_contract.pmap_ttbr_written;
    contract->pmap_ttbcr_written = preflight->xnu_iokit_registry_topology_dryrun_contract.pmap_ttbcr_written;
    contract->pmap_dacr_written = preflight->xnu_iokit_registry_topology_dryrun_contract.pmap_dacr_written;
    contract->pmap_sctlr_written = preflight->xnu_iokit_registry_topology_dryrun_contract.pmap_sctlr_written;
    contract->pmap_tlbs_invalidated = preflight->xnu_iokit_registry_topology_dryrun_contract.pmap_tlbs_invalidated;
    contract->caches_changed = preflight->xnu_iokit_registry_topology_dryrun_contract.caches_changed;
    contract->persistent_write_attempted = preflight->xnu_iokit_registry_topology_dryrun_contract.persistent_write_attempted;
    contract->xnu_start_executed = preflight->xnu_iokit_registry_topology_dryrun_contract.xnu_start_executed;
    contract->generated_macho_executed = preflight->xnu_iokit_registry_topology_dryrun_contract.generated_macho_executed;
    contract->proposed_platform_gap_mask = preflight->platform_gap_mask;
    contract->proposed_pexpert_gap_mask = preflight->pexpert_gap_mask;
    contract->local_only = 1u;
    contract->fail_closed = 1u;

    source_ok = (contract->source_topology_status == STAGE90_STATUS_OK &&
                 contract->source_topology_satisfied_mask == STAGE90_XNU_IOKIT_REGISTRY_TOPOLOGY_DRYRUN_REQUIRED_MASK &&
                 contract->source_topology_failure_mask == 0u &&
                 contract->source_topology_topology_checksum == STAGE90_XNU_IOKIT_REGISTRY_TOPOLOGY_EXPECTED_TOPOLOGY_CHECKSUM &&
                 contract->source_selected_topology_entry_mask == STAGE90_XNU_IOKIT_REGISTRY_SERVICE_SELECTED_REQUIRED &&
                 contract->source_rejected_unlinked_mask == STAGE90_XNU_IOKIT_REGISTRY_SERVICE_REJECTED_REQUIRED &&
                 contract->source_topology_matrix == (STAGE90_XNU_IOKIT_REGISTRY_SERVICE_SELECTED_REQUIRED |
                                                      STAGE90_XNU_IOKIT_REGISTRY_SERVICE_REJECTED_REQUIRED) &&
                 contract->source_property_inheritance_status == STAGE90_STATUS_OK &&
                 contract->source_catalog_status == STAGE90_STATUS_OK &&
                 contract->source_provider_status == STAGE90_STATUS_OK &&
                 contract->source_registry_status == STAGE90_STATUS_OK &&
                 contract->source_match_status == STAGE90_STATUS_OK &&
                 contract->source_scaffold_status == STAGE90_STATUS_OK &&
                 contract->source_pexpert_status == STAGE90_STATUS_OK &&
                 contract->source_pmap_transition_status == STAGE90_STATUS_OK &&
                 contract->source_compile_graph_status == STAGE90_STATUS_OK &&
                 contract->source_object_subset_status == STAGE90_STATUS_OK &&
                 contract->source_link_status == STAGE90_STATUS_OK &&
                 contract->source_loader_safety_mask == contract->source_loader_safety_required_mask) ? 1u : 0u;
    if (source_ok) {
        contract->satisfied_mask |= STAGE90_XNU_IOKIT_ATTACH_START_READINESS_DRYRUN_SAT_SOURCE_TOPOLOGY;
    } else {
        contract->failure_mask |= STAGE90_XNU_IOKIT_ATTACH_START_READINESS_DRYRUN_FAIL_SOURCE;
    }

    if (contract->apple_dt_semantic_mask == STAGE90_DT_READY_REQUIRED) {
        contract->satisfied_mask |= STAGE90_XNU_IOKIT_ATTACH_START_READINESS_DRYRUN_SAT_APPLE_DT;
    } else {
        contract->failure_mask |= STAGE90_XNU_IOKIT_ATTACH_START_READINESS_DRYRUN_FAIL_DT;
    }

    personality_nodes_ok = (contract->platform_personality_node_ptr != 0u &&
                            contract->platform_personality_prop_count == 100u &&
                            contract->interrupt_personality_node_ptr != 0u &&
                            contract->interrupt_personality_prop_count == 100u &&
                            contract->timer_personality_node_ptr != 0u &&
                            contract->timer_personality_prop_count == 100u &&
                            contract->cpu_personality_node_ptr != 0u &&
                            contract->cpu_personality_prop_count == 100u &&
                            contract->rejected_personality_node_ptr != 0u &&
                            contract->rejected_personality_prop_count == 99u) ? 1u : 0u;
    if (personality_nodes_ok) {
        contract->satisfied_mask |= STAGE90_XNU_IOKIT_ATTACH_START_READINESS_DRYRUN_SAT_PERSONALITY_NODE_SET;
    } else {
        contract->failure_mask |= STAGE90_XNU_IOKIT_ATTACH_START_READINESS_DRYRUN_FAIL_PERSONALITY;
    }

    if (contract->attach_readiness_mask == STAGE90_XNU_IOKIT_REGISTRY_SERVICE_SELECTED_REQUIRED) {
        contract->satisfied_mask |= STAGE90_XNU_IOKIT_ATTACH_START_READINESS_DRYRUN_SAT_ATTACH_READINESS_MATRIX;
    } else {
        contract->failure_mask |= STAGE90_XNU_IOKIT_ATTACH_START_READINESS_DRYRUN_FAIL_ATTACH;
    }
    if (contract->start_readiness_mask == STAGE90_XNU_IOKIT_REGISTRY_SERVICE_SELECTED_REQUIRED) {
        contract->satisfied_mask |= STAGE90_XNU_IOKIT_ATTACH_START_READINESS_DRYRUN_SAT_START_READINESS_MATRIX;
    } else {
        contract->failure_mask |= STAGE90_XNU_IOKIT_ATTACH_START_READINESS_DRYRUN_FAIL_START;
    }
    if (contract->attach_provider_path_match_mask == STAGE90_XNU_IOKIT_REGISTRY_SERVICE_SELECTED_REQUIRED) {
        contract->satisfied_mask |= STAGE90_XNU_IOKIT_ATTACH_START_READINESS_DRYRUN_SAT_ATTACH_PROVIDER_PATH_MATRIX;
    } else {
        contract->failure_mask |= STAGE90_XNU_IOKIT_ATTACH_START_READINESS_DRYRUN_FAIL_PROVIDER;
    }
    if (contract->start_provider_path_match_mask == STAGE90_XNU_IOKIT_REGISTRY_SERVICE_SELECTED_REQUIRED) {
        contract->satisfied_mask |= STAGE90_XNU_IOKIT_ATTACH_START_READINESS_DRYRUN_SAT_START_PROVIDER_PATH_MATRIX;
    } else {
        contract->failure_mask |= STAGE90_XNU_IOKIT_ATTACH_START_READINESS_DRYRUN_FAIL_PROVIDER;
    }
    if (contract->attach_provider_ordinal_match_mask == STAGE90_XNU_IOKIT_REGISTRY_SERVICE_SELECTED_REQUIRED) {
        contract->satisfied_mask |= STAGE90_XNU_IOKIT_ATTACH_START_READINESS_DRYRUN_SAT_ATTACH_PROVIDER_ORDINAL_MATRIX;
    } else {
        contract->failure_mask |= STAGE90_XNU_IOKIT_ATTACH_START_READINESS_DRYRUN_FAIL_PROVIDER;
    }
    if (contract->start_provider_ordinal_match_mask == STAGE90_XNU_IOKIT_REGISTRY_SERVICE_SELECTED_REQUIRED) {
        contract->satisfied_mask |= STAGE90_XNU_IOKIT_ATTACH_START_READINESS_DRYRUN_SAT_START_PROVIDER_ORDINAL_MATRIX;
    } else {
        contract->failure_mask |= STAGE90_XNU_IOKIT_ATTACH_START_READINESS_DRYRUN_FAIL_PROVIDER;
    }
    if (contract->attach_order_match_mask == STAGE90_XNU_IOKIT_REGISTRY_SERVICE_SELECTED_REQUIRED &&
        contract->attach_order_checksum == STAGE90_XNU_IOKIT_REGISTRY_TOPOLOGY_REJECTED_ORDINAL) {
        contract->satisfied_mask |= STAGE90_XNU_IOKIT_ATTACH_START_READINESS_DRYRUN_SAT_ATTACH_ORDER_MATRIX;
    } else {
        contract->failure_mask |= STAGE90_XNU_IOKIT_ATTACH_START_READINESS_DRYRUN_FAIL_ORDER;
    }
    if (contract->start_order_match_mask == STAGE90_XNU_IOKIT_REGISTRY_SERVICE_SELECTED_REQUIRED &&
        contract->start_order_checksum == STAGE90_XNU_IOKIT_REGISTRY_TOPOLOGY_REJECTED_ORDINAL) {
        contract->satisfied_mask |= STAGE90_XNU_IOKIT_ATTACH_START_READINESS_DRYRUN_SAT_START_ORDER_MATRIX;
    } else {
        contract->failure_mask |= STAGE90_XNU_IOKIT_ATTACH_START_READINESS_DRYRUN_FAIL_ORDER;
    }
    if (contract->provider_start_required_mask == (STAGE90_XNU_IOKIT_REGISTRY_SERVICE_INTERRUPT_BIT |
                                                  STAGE90_XNU_IOKIT_REGISTRY_SERVICE_TIMER_BIT |
                                                  STAGE90_XNU_IOKIT_REGISTRY_SERVICE_CPU_BIT) &&
        contract->provider_start_satisfied_mask == (STAGE90_XNU_IOKIT_REGISTRY_SERVICE_INTERRUPT_BIT |
                                                   STAGE90_XNU_IOKIT_REGISTRY_SERVICE_TIMER_BIT |
                                                   STAGE90_XNU_IOKIT_REGISTRY_SERVICE_CPU_BIT) &&
        contract->provider_start_required_count == 3u && contract->provider_start_satisfied_count == 3u) {
        contract->satisfied_mask |= STAGE90_XNU_IOKIT_ATTACH_START_READINESS_DRYRUN_SAT_PROVIDER_START_DEPENDENCY;
    } else {
        contract->failure_mask |= STAGE90_XNU_IOKIT_ATTACH_START_READINESS_DRYRUN_FAIL_ORDER;
    }
    if (contract->source_attach_deferred_mask == STAGE90_XNU_IOKIT_REGISTRY_SERVICE_SELECTED_REQUIRED &&
        contract->source_start_deferred_mask == STAGE90_XNU_IOKIT_REGISTRY_SERVICE_SELECTED_REQUIRED) {
        contract->satisfied_mask |= STAGE90_XNU_IOKIT_ATTACH_START_READINESS_DRYRUN_SAT_SOURCE_DEFERRED;
    } else {
        contract->failure_mask |= STAGE90_XNU_IOKIT_ATTACH_START_READINESS_DRYRUN_FAIL_SOURCE;
    }
    if (contract->selected_attach_start_entry_mask == STAGE90_XNU_IOKIT_REGISTRY_SERVICE_SELECTED_REQUIRED &&
        contract->attach_runtime_blocked_mask == STAGE90_XNU_IOKIT_REGISTRY_SERVICE_SELECTED_REQUIRED &&
        contract->start_runtime_blocked_mask == STAGE90_XNU_IOKIT_REGISTRY_SERVICE_SELECTED_REQUIRED &&
        contract->attach_start_provenance_match_mask == STAGE90_XNU_IOKIT_REGISTRY_SERVICE_SELECTED_REQUIRED) {
        contract->satisfied_mask |= STAGE90_XNU_IOKIT_ATTACH_START_READINESS_DRYRUN_SAT_SELECTED_ATTACH_START_MATRIX;
    } else {
        contract->failure_mask |= STAGE90_XNU_IOKIT_ATTACH_START_READINESS_DRYRUN_FAIL_ATTACH |
            STAGE90_XNU_IOKIT_ATTACH_START_READINESS_DRYRUN_FAIL_START;
    }
    if (contract->rejected_attach_start_mask == STAGE90_XNU_IOKIT_REGISTRY_SERVICE_REJECTED_REQUIRED &&
        contract->source_rejected_unlinked_mask == STAGE90_XNU_IOKIT_REGISTRY_SERVICE_REJECTED_REQUIRED &&
        contract->rejected_unattached_unstarted_count == STAGE90_XNU_IOKIT_ATTACH_START_READINESS_REJECTED_COUNT) {
        contract->satisfied_mask |= STAGE90_XNU_IOKIT_ATTACH_START_READINESS_DRYRUN_SAT_REJECT_UNATTACHED_UNSTARTED;
    } else {
        contract->failure_mask |= STAGE90_XNU_IOKIT_ATTACH_START_READINESS_DRYRUN_FAIL_REJECTED;
    }
    if (contract->attach_start_candidate_count == STAGE90_XNU_IOKIT_ATTACH_START_READINESS_CANDIDATE_COUNT &&
        contract->selected_attach_start_entry_count == STAGE90_XNU_IOKIT_ATTACH_START_READINESS_ENTRY_COUNT &&
        contract->rejected_attach_start_entry_count == STAGE90_XNU_IOKIT_ATTACH_START_READINESS_REJECTED_COUNT &&
        contract->attach_readiness_count == STAGE90_XNU_IOKIT_ATTACH_START_READINESS_ENTRY_COUNT &&
        contract->start_readiness_count == STAGE90_XNU_IOKIT_ATTACH_START_READINESS_ENTRY_COUNT &&
        contract->attach_start_matrix == (STAGE90_XNU_IOKIT_REGISTRY_SERVICE_SELECTED_REQUIRED |
                                          STAGE90_XNU_IOKIT_REGISTRY_SERVICE_REJECTED_REQUIRED)) {
        contract->satisfied_mask |= STAGE90_XNU_IOKIT_ATTACH_START_READINESS_DRYRUN_SAT_DRYRUN_COUNTS;
    } else {
        contract->failure_mask |= STAGE90_XNU_IOKIT_ATTACH_START_READINESS_DRYRUN_FAIL_COUNTS;
    }
    if (contract->attach_start_checksum == contract->expected_attach_start_checksum) {
        contract->satisfied_mask |= STAGE90_XNU_IOKIT_ATTACH_START_READINESS_DRYRUN_SAT_ATTACH_START_CHECKSUM;
    } else {
        contract->failure_mask |= STAGE90_XNU_IOKIT_ATTACH_START_READINESS_DRYRUN_FAIL_CHECKSUM;
    }

    if (contract->iokit_reference_mask == STAGE90_XNU_IOKIT_REFERENCE_REQUIRED &&
        contract->iokit_runtime_blocked_mask == STAGE90_XNU_IOKIT_REFERENCE_REQUIRED &&
        contract->iokit_reference_count >= 3u && contract->iokit_public_compile_count == 0u &&
        contract->iokit_public_link_count == 0u && contract->iokit_reference_only == 1u) {
        contract->satisfied_mask |= STAGE90_XNU_IOKIT_ATTACH_START_READINESS_DRYRUN_SAT_IOKIT_REFERENCE;
    } else {
        contract->failure_mask |= STAGE90_XNU_IOKIT_ATTACH_START_READINESS_DRYRUN_FAIL_PUBLIC_BOUNDARY;
    }

    public_boundary_ok = (contract->no_public_xnu_exec == 1u && contract->no_platform_runtime_exec == 1u &&
                          contract->no_iokit_runtime_exec == 1u && contract->no_catalog_runtime_exec == 1u &&
                          contract->no_provider_runtime_exec == 1u &&
                          contract->no_registry_entry_runtime_exec == 1u &&
                          contract->no_registry_topology_runtime_exec == 1u &&
                          contract->no_property_runtime_exec == 1u && contract->no_attach_runtime_exec == 1u &&
                          contract->no_start_runtime_exec == 1u && contract->no_platform_driver_exec == 1u &&
                          contract->source_object_subset_status == STAGE90_STATUS_OK &&
                          contract->source_link_status == STAGE90_STATUS_OK) ? 1u : 0u;
    if (public_boundary_ok) {
        contract->satisfied_mask |= STAGE90_XNU_IOKIT_ATTACH_START_READINESS_DRYRUN_SAT_PUBLIC_BOUNDARY |
            STAGE90_XNU_IOKIT_ATTACH_START_READINESS_DRYRUN_SAT_NO_IOKIT_RUNTIME_EXEC |
            STAGE90_XNU_IOKIT_ATTACH_START_READINESS_DRYRUN_SAT_NO_ATTACH_RUNTIME_EXEC |
            STAGE90_XNU_IOKIT_ATTACH_START_READINESS_DRYRUN_SAT_NO_START_RUNTIME_EXEC |
            STAGE90_XNU_IOKIT_ATTACH_START_READINESS_DRYRUN_SAT_NO_REGISTRY_TOPOLOGY_RUNTIME_EXEC |
            STAGE90_XNU_IOKIT_ATTACH_START_READINESS_DRYRUN_SAT_NO_PUBLIC_XNU_EXEC;
    } else {
        contract->failure_mask |= STAGE90_XNU_IOKIT_ATTACH_START_READINESS_DRYRUN_FAIL_PUBLIC_BOUNDARY;
    }

    if (contract->no_public_pmap_exec == 1u) {
        contract->satisfied_mask |= STAGE90_XNU_IOKIT_ATTACH_START_READINESS_DRYRUN_SAT_NO_PUBLIC_PMAP_EXEC;
    } else {
        contract->failure_mask |= STAGE90_XNU_IOKIT_ATTACH_START_READINESS_DRYRUN_FAIL_PMAP_BOUNDARY;
    }

    pmap_boundary_ok = (contract->no_live_pmap_tables_installed == 0u &&
                        contract->proposed_workspace_written == 0u &&
                        contract->pmap_ttbr_written == 0u && contract->pmap_ttbcr_written == 0u &&
                        contract->pmap_dacr_written == 0u && contract->pmap_sctlr_written == 0u &&
                        contract->pmap_tlbs_invalidated == 0u && contract->caches_changed == 0u) ? 1u : 0u;
    if (contract->no_live_pmap_tables_installed == 0u) {
        contract->satisfied_mask |= STAGE90_XNU_IOKIT_ATTACH_START_READINESS_DRYRUN_SAT_NO_LIVE_PMAP_INSTALL;
    }
    if (contract->pmap_tlbs_invalidated == 0u && contract->caches_changed == 0u) {
        contract->satisfied_mask |= STAGE90_XNU_IOKIT_ATTACH_START_READINESS_DRYRUN_SAT_NO_TLB_CACHE_CHANGE;
    }
    if (pmap_boundary_ok && contract->source_pmap_transition_status == STAGE90_STATUS_OK) {
        contract->satisfied_mask |= STAGE90_XNU_IOKIT_ATTACH_START_READINESS_DRYRUN_SAT_PMAP_BOUNDARY;
    } else {
        contract->failure_mask |= STAGE90_XNU_IOKIT_ATTACH_START_READINESS_DRYRUN_FAIL_PMAP_BOUNDARY;
    }

    if (contract->generated_macho_executed == 0u && contract->xnu_start_executed == 0u) {
        contract->satisfied_mask |= STAGE90_XNU_IOKIT_ATTACH_START_READINESS_DRYRUN_SAT_NO_XNU_MACHO_EXEC;
    } else {
        contract->failure_mask |= STAGE90_XNU_IOKIT_ATTACH_START_READINESS_DRYRUN_FAIL_SAFETY_BOUNDARY;
    }
    if (contract->persistent_write_attempted == 0u) {
        contract->satisfied_mask |= STAGE90_XNU_IOKIT_ATTACH_START_READINESS_DRYRUN_SAT_NO_PERSIST_WRITE;
    } else {
        contract->failure_mask |= STAGE90_XNU_IOKIT_ATTACH_START_READINESS_DRYRUN_FAIL_SAFETY_BOUNDARY;
    }
    if (contract->local_only == 1u && contract->fail_closed == 1u &&
        contract->persistent_write_attempted == 0u && contract->xnu_start_executed == 0u &&
        contract->generated_macho_executed == 0u &&
        contract->proposed_platform_gap_mask == (STAGE90_PLATFORM_GAP_REQUIRED_RECORDED & ~STAGE90_PLATFORM_GAP_IOKIT_STACK) &&
        contract->proposed_pexpert_gap_mask == 0u &&
        contract->selected_attach_start_entry_mask == STAGE90_XNU_IOKIT_REGISTRY_SERVICE_SELECTED_REQUIRED &&
        contract->rejected_attach_start_mask == STAGE90_XNU_IOKIT_REGISTRY_SERVICE_REJECTED_REQUIRED) {
        contract->satisfied_mask |= STAGE90_XNU_IOKIT_ATTACH_START_READINESS_DRYRUN_SAT_LOCAL_ONLY_FAIL_CLOSED;
    } else {
        contract->failure_mask |= STAGE90_XNU_IOKIT_ATTACH_START_READINESS_DRYRUN_FAIL_ROLLUP |
            STAGE90_XNU_IOKIT_ATTACH_START_READINESS_DRYRUN_FAIL_SAFETY_BOUNDARY;
    }

    contract->checksum = stage90_iokit_attach_start_readiness_dryrun_checksum(contract);
    if (contract->checksum != stage90_iokit_attach_start_readiness_dryrun_checksum(contract)) {
        contract->failure_mask |= STAGE90_XNU_IOKIT_ATTACH_START_READINESS_DRYRUN_FAIL_CHECKSUM;
    }

    if (contract->satisfied_mask == contract->required_mask && contract->failure_mask == 0u) {
        contract->status = STAGE90_STATUS_OK;
    } else {
        contract->status = STAGE90_STATUS_FAIL(contract->failure_mask);
    }

    stage90_iokit_attach_start_readiness_dryrun_log(contract);
    return contract->status == STAGE90_STATUS_OK;
}

const struct stage90_xnu_iokit_attach_start_readiness_dryrun_contract *
stage90_xnu_iokit_attach_start_readiness_dryrun_contract_result(void)
{
    return &g_stage90_xnu_iokit_attach_start_readiness_dryrun_contract;
}
