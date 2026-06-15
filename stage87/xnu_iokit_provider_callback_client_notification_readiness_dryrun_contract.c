#include "stage87.h"

static struct stage87_xnu_iokit_provider_callback_client_notification_readiness_dryrun_contract
    g_stage87_xnu_iokit_provider_callback_client_notification_readiness_dryrun_contract;

static uint32_t stage87_iokit_provider_callback_client_notification_dryrun_checksum(
    volatile const struct stage87_xnu_iokit_provider_callback_client_notification_readiness_dryrun_contract *contract)
{
    volatile const uint32_t *words = (volatile const uint32_t *)(uintptr_t)contract;
    uint32_t count = (uint32_t)(offsetof(struct stage87_xnu_iokit_provider_callback_client_notification_readiness_dryrun_contract,
                                        checksum) / sizeof(uint32_t));
    uint32_t checksum = 0u;

    for (uint32_t i = 0; i < count; i++) {
        checksum ^= words[i];
    }

    return checksum;
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

static uint32_t string_prop_equals(const struct boot_args *args, const void *node,
                                   const char *name, const char *expected)
{
    uint32_t len = 0u;
    const char *value;

    if (!args || !node || !expected) {
        return 0u;
    }

    value = (const char *)apple_dt_get_prop(args->deviceTreeP, args->deviceTreeLength, node, name, &len);
    if (!value || len == 0u) {
        return 0u;
    }

    return (strcmp(value, expected) == 0) ? 1u : 0u;
}

static void selected_masks_for_node(const struct boot_args *args, const void *node, uint32_t bit,
                                    const char *provider_path, uint32_t provider_ordinal,
                                    uint32_t order,
                                    struct stage87_xnu_iokit_provider_callback_client_notification_readiness_dryrun_contract *contract)
{
    if (u32_prop_equals(args, node, "prov-callback-dryrun", 1u) &&
        u32_prop_equals(args, node, "callback-ready", 1u)) {
        contract->provider_callback_ready_mask |= bit;
    }
    if (u32_prop_equals(args, node, "client-notify-ready", 1u)) {
        contract->client_notification_ready_mask |= bit;
    }
    if (string_prop_equals(args, node, "callback-provider-path", provider_path)) {
        contract->callback_provider_path_match_mask |= bit;
    }
    if (u32_prop_equals(args, node, "callback-provider-ordinal", provider_ordinal)) {
        contract->callback_provider_ordinal_match_mask |= bit;
    }
    if (u32_prop_equals(args, node, "callback-type-mask", bit)) {
        contract->callback_type_match_mask |= bit;
    }
    if (u32_prop_equals(args, node, "expected-callback-mask", bit)) {
        contract->expected_callback_mask_match_mask |= bit;
    }
    if (u32_prop_equals(args, node, "delivered-callback-mask", bit)) {
        contract->delivered_callback_mask_match_mask |= bit;
    }
    if (u32_prop_equals(args, node, "client-ack-mask", bit)) {
        contract->client_ack_mask_match_mask |= bit;
    }
    if (u32_prop_equals(args, node, "callback-dependency-ready", 1u)) {
        contract->callback_dependency_ready_mask |= bit;
    }
    if (u32_prop_equals(args, node, "callback-runtime-exec", 0u)) {
        contract->callback_runtime_blocked_mask |= bit;
    }
    if (u32_prop_equals(args, node, "client-notify-runtime-exec", 0u)) {
        contract->client_notification_runtime_blocked_mask |= bit;
    }
    if (u32_prop_equals(args, node, "callback-order", order)) {
        contract->callback_order_match_mask |= bit;
    }
    if (u32_prop_equals(args, node, "client-notify-order", order)) {
        contract->client_notification_order_match_mask |= bit;
    }
    if (string_prop_equals(args, node, "callback-provenance", "stage87-provider-notify")) {
        contract->callback_provenance_match_mask |= bit;
    }
}

static void stage87_iokit_provider_callback_client_notification_dryrun_log(
    const struct stage87_xnu_iokit_provider_callback_client_notification_readiness_dryrun_contract *contract)
{
    xnu_log_kv32("stage87_xnu_iokit_provider_callback_client_notification_readiness_dryrun_contract_status", contract->status);
    xnu_log_kv32("stage87_xnu_iokit_provider_callback_client_notification_readiness_dryrun_contract_required_mask", contract->required_mask);
    xnu_log_kv32("stage87_xnu_iokit_provider_callback_client_notification_readiness_dryrun_contract_satisfied_mask", contract->satisfied_mask);
    xnu_log_kv32("stage87_xnu_iokit_provider_callback_client_notification_readiness_dryrun_contract_failure_mask", contract->failure_mask);
    xnu_log_kv32("stage87_xnu_iokit_provider_callback_client_notification_readiness_dryrun_contract_checksum", contract->checksum);
    xnu_log_kv32("stage87_xnu_iokit_provider_callback_source_status", contract->source_provider_notification_status);
    xnu_log_kv32("stage87_xnu_iokit_provider_callback_source_satisfied_mask", contract->source_provider_notification_satisfied_mask);
    xnu_log_kv32("stage87_xnu_iokit_provider_callback_source_failure_mask", contract->source_provider_notification_failure_mask);
    xnu_log_kv32("stage87_xnu_iokit_provider_callback_source_checksum", contract->source_provider_notification_checksum);
    xnu_log_kv32("stage87_xnu_iokit_provider_callback_source_selected_provider_notification_entry_mask", contract->source_selected_provider_notification_entry_mask);
    xnu_log_kv32("stage87_xnu_iokit_provider_callback_source_rejected_provider_notification_mask", contract->source_rejected_provider_notification_mask);
    xnu_log_kv32("stage87_xnu_iokit_provider_callback_source_provider_notification_matrix", contract->source_provider_notification_matrix);
    xnu_log_kv32("stage87_xnu_iokit_provider_callback_source_interest_ready_mask", contract->source_interest_ready_mask);
    xnu_log_kv32("stage87_xnu_iokit_provider_callback_source_delivery_ready_mask", contract->source_delivery_ready_mask);
    xnu_log_kv32("stage87_xnu_iokit_provider_callback_apple_dt_semantic_mask", contract->apple_dt_semantic_mask);
    xnu_log_kv32("stage87_xnu_iokit_provider_callback_platform_props", contract->platform_personality_prop_count);
    xnu_log_kv32("stage87_xnu_iokit_provider_callback_interrupt_props", contract->interrupt_personality_prop_count);
    xnu_log_kv32("stage87_xnu_iokit_provider_callback_timer_props", contract->timer_personality_prop_count);
    xnu_log_kv32("stage87_xnu_iokit_provider_callback_cpu_props", contract->cpu_personality_prop_count);
    xnu_log_kv32("stage87_xnu_iokit_provider_callback_rejected_props", contract->rejected_personality_prop_count);
    xnu_log_kv32("stage87_xnu_iokit_provider_callback_ready_mask", contract->provider_callback_ready_mask);
    xnu_log_kv32("stage87_xnu_iokit_provider_callback_client_notification_ready_mask", contract->client_notification_ready_mask);
    xnu_log_kv32("stage87_xnu_iokit_provider_callback_provider_path_match_mask", contract->callback_provider_path_match_mask);
    xnu_log_kv32("stage87_xnu_iokit_provider_callback_provider_ordinal_match_mask", contract->callback_provider_ordinal_match_mask);
    xnu_log_kv32("stage87_xnu_iokit_provider_callback_type_match_mask", contract->callback_type_match_mask);
    xnu_log_kv32("stage87_xnu_iokit_provider_callback_expected_callback_mask_match_mask", contract->expected_callback_mask_match_mask);
    xnu_log_kv32("stage87_xnu_iokit_provider_callback_delivered_callback_mask_match_mask", contract->delivered_callback_mask_match_mask);
    xnu_log_kv32("stage87_xnu_iokit_provider_callback_client_ack_mask_match_mask", contract->client_ack_mask_match_mask);
    xnu_log_kv32("stage87_xnu_iokit_provider_callback_dependency_ready_mask", contract->callback_dependency_ready_mask);
    xnu_log_kv32("stage87_xnu_iokit_provider_callback_runtime_blocked_mask", contract->callback_runtime_blocked_mask);
    xnu_log_kv32("stage87_xnu_iokit_provider_callback_client_notification_runtime_blocked_mask", contract->client_notification_runtime_blocked_mask);
    xnu_log_kv32("stage87_xnu_iokit_provider_callback_order_match_mask", contract->callback_order_match_mask);
    xnu_log_kv32("stage87_xnu_iokit_provider_callback_client_notification_order_match_mask", contract->client_notification_order_match_mask);
    xnu_log_kv32("stage87_xnu_iokit_provider_callback_provenance_match_mask", contract->callback_provenance_match_mask);
    xnu_log_kv32("stage87_xnu_iokit_provider_callback_selected_provider_callback_entry_mask", contract->selected_provider_callback_entry_mask);
    xnu_log_kv32("stage87_xnu_iokit_provider_callback_rejected_provider_callback_mask", contract->rejected_provider_callback_mask);
    xnu_log_kv32("stage87_xnu_iokit_provider_callback_matrix", contract->provider_callback_client_notification_matrix);
    xnu_log_kv32("stage87_xnu_iokit_provider_callback_callback_order_checksum", contract->callback_order_checksum);
    xnu_log_kv32("stage87_xnu_iokit_provider_callback_client_notification_order_checksum", contract->client_notification_order_checksum);
    xnu_log_kv32("stage87_xnu_iokit_provider_callback_mask_checksum", contract->callback_mask_checksum);
    xnu_log_kv32("stage87_xnu_iokit_provider_callback_path_hash", contract->provider_callback_path_hash);
    xnu_log_kv32("stage87_xnu_iokit_provider_callback_checksum", contract->provider_callback_checksum);
    xnu_log_kv32("stage87_xnu_iokit_provider_callback_expected_checksum", contract->expected_provider_callback_checksum);
    xnu_log_kv32("stage87_xnu_iokit_provider_callback_candidate_count", contract->provider_callback_candidate_count);
    xnu_log_kv32("stage87_xnu_iokit_provider_callback_selected_entry_count", contract->selected_provider_callback_entry_count);
    xnu_log_kv32("stage87_xnu_iokit_provider_callback_rejected_entry_count", contract->rejected_provider_callback_entry_count);
    xnu_log_kv32("stage87_xnu_iokit_provider_callback_ready_count", contract->provider_callback_ready_count);
    xnu_log_kv32("stage87_xnu_iokit_provider_callback_client_notification_ready_count", contract->client_notification_ready_count);
    xnu_log_kv32("stage87_xnu_iokit_provider_callback_dependency_ready_count", contract->callback_dependency_ready_count);
    xnu_log_kv32("stage87_xnu_iokit_provider_callback_client_ack_ready_count", contract->client_ack_ready_count);
    xnu_log_kv32("stage87_xnu_iokit_provider_callback_rejected_no_callback_count", contract->rejected_no_callback_count);
    xnu_log_kv32("stage87_xnu_iokit_provider_callback_public_compile_count", contract->iokit_public_compile_count);
    xnu_log_kv32("stage87_xnu_iokit_provider_callback_public_link_count", contract->iokit_public_link_count);
    xnu_log_kv32("stage87_xnu_iokit_provider_callback_no_iokit_runtime_exec", contract->no_iokit_runtime_exec);
    xnu_log_kv32("stage87_xnu_iokit_provider_callback_no_callback_runtime_exec", contract->no_callback_runtime_exec);
    xnu_log_kv32("stage87_xnu_iokit_provider_callback_no_client_notification_runtime_exec", contract->no_client_notification_runtime_exec);
    xnu_log_kv32("stage87_xnu_iokit_provider_callback_no_live_pmap_tables_installed", contract->no_live_pmap_tables_installed);
    xnu_log_kv32("stage87_xnu_iokit_provider_callback_proposed_workspace_written", contract->proposed_workspace_written);
    xnu_log_kv32("stage87_xnu_iokit_provider_callback_pmap_ttbr_written", contract->pmap_ttbr_written);
    xnu_log_kv32("stage87_xnu_iokit_provider_callback_pmap_tlbs_invalidated", contract->pmap_tlbs_invalidated);
    xnu_log_kv32("stage87_xnu_iokit_provider_callback_caches_changed", contract->caches_changed);
    xnu_log_kv32("stage87_xnu_iokit_provider_callback_persistent_write_attempted", contract->persistent_write_attempted);
}

int stage87_xnu_iokit_provider_callback_client_notification_readiness_dryrun_contract_selftest(
    const struct stage87_loader_preflight *preflight)
{
    struct stage87_xnu_iokit_provider_callback_client_notification_readiness_dryrun_contract *contract =
        &g_stage87_xnu_iokit_provider_callback_client_notification_readiness_dryrun_contract;
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
    uint32_t callback_dependency_ok;
    uint32_t public_boundary_ok;
    uint32_t pmap_boundary_ok;

    memset(contract, 0, sizeof(*contract));
    contract->version = STAGE87_XNU_IOKIT_PROVIDER_CALLBACK_CLIENT_NOTIFICATION_DRYRUN_CONTRACT_VERSION;
    contract->size = sizeof(*contract);
    contract->status = STAGE87_STATUS_BASE;
    contract->required_mask = STAGE87_XNU_IOKIT_PROVIDER_CALLBACK_CLIENT_NOTIFICATION_DRYRUN_REQUIRED_MASK;

    if (!preflight || !args || !root) {
        contract->failure_mask = STAGE87_XNU_IOKIT_PROVIDER_CALLBACK_CLIENT_NOTIFICATION_DRYRUN_FAIL_SOURCE |
            STAGE87_XNU_IOKIT_PROVIDER_CALLBACK_CLIENT_NOTIFICATION_DRYRUN_FAIL_SAFETY_BOUNDARY;
        contract->checksum = stage87_iokit_provider_callback_client_notification_dryrun_checksum(contract);
        stage87_iokit_provider_callback_client_notification_dryrun_log(contract);
        return 0;
    }

    contract->source_provider_notification_status =
        preflight->xnu_iokit_provider_notification_delivery_readiness_dryrun_contract_status;
    contract->source_provider_notification_satisfied_mask =
        preflight->xnu_iokit_provider_notification_delivery_readiness_dryrun_contract_satisfied_mask;
    contract->source_provider_notification_failure_mask =
        preflight->xnu_iokit_provider_notification_delivery_readiness_dryrun_contract_failure_mask;
    contract->source_provider_notification_contract_checksum =
        preflight->xnu_iokit_provider_notification_delivery_readiness_dryrun_contract_checksum;
    contract->source_provider_notification_checksum =
        preflight->xnu_iokit_provider_notification_delivery_readiness_dryrun_contract.provider_notification_checksum;
    contract->source_selected_provider_notification_entry_mask =
        preflight->xnu_iokit_provider_notification_delivery_readiness_dryrun_contract.selected_provider_notification_entry_mask;
    contract->source_rejected_provider_notification_mask =
        preflight->xnu_iokit_provider_notification_delivery_readiness_dryrun_contract.rejected_provider_notification_mask;
    contract->source_provider_notification_matrix =
        preflight->xnu_iokit_provider_notification_delivery_readiness_dryrun_contract.provider_notification_matrix;
    contract->source_provider_notification_ready_mask =
        preflight->xnu_iokit_provider_notification_delivery_readiness_dryrun_contract.provider_notification_ready_mask;
    contract->source_interest_ready_mask =
        preflight->xnu_iokit_provider_notification_delivery_readiness_dryrun_contract.interest_ready_mask;
    contract->source_delivery_ready_mask =
        preflight->xnu_iokit_provider_notification_delivery_readiness_dryrun_contract.delivery_ready_mask;
    contract->source_no_interest_runtime_exec =
        preflight->xnu_iokit_provider_notification_delivery_readiness_dryrun_contract.no_interest_runtime_exec;
    contract->source_no_delivery_runtime_exec =
        preflight->xnu_iokit_provider_notification_delivery_readiness_dryrun_contract.no_delivery_runtime_exec;
    contract->source_lifecycle_status = preflight->xnu_iokit_lifecycle_register_service_readiness_dryrun_contract_status;
    contract->source_attach_start_status = preflight->xnu_iokit_attach_start_readiness_dryrun_contract_status;
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
    contract->source_loader_safety_required_mask = STAGE87_LOADER_SAFETY_REQUIRED;
    contract->boot_args_ptr = preflight->boot_args_ptr;
    contract->device_tree_ptr = preflight->device_tree_ptr;
    contract->device_tree_length = preflight->device_tree_length;
    contract->apple_dt_semantic_mask = preflight->apple_dt_semantic_mask;

    contract->platform_personality_node_ptr = (uint32_t)(uintptr_t)apple_dt_find_child(args->deviceTreeP,
                                                                                       args->deviceTreeLength,
                                                                                       root,
                                                                                       "stage87-platform-personality");
    contract->interrupt_personality_node_ptr = (uint32_t)(uintptr_t)apple_dt_find_child(args->deviceTreeP,
                                                                                        args->deviceTreeLength,
                                                                                        root,
                                                                                        "stage87-interrupt-personality");
    contract->timer_personality_node_ptr = (uint32_t)(uintptr_t)apple_dt_find_child(args->deviceTreeP,
                                                                                    args->deviceTreeLength,
                                                                                    root,
                                                                                    "stage87-timer-personality");
    contract->cpu_personality_node_ptr = (uint32_t)(uintptr_t)apple_dt_find_child(args->deviceTreeP,
                                                                                  args->deviceTreeLength,
                                                                                  root,
                                                                                  "stage87-cpu-personality");
    contract->rejected_personality_node_ptr = (uint32_t)(uintptr_t)apple_dt_find_child(args->deviceTreeP,
                                                                                       args->deviceTreeLength,
                                                                                       root,
                                                                                       "stage87-rejected-personality");

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

    selected_masks_for_node(args, platform_node, STAGE87_XNU_IOKIT_REGISTRY_SERVICE_PLATFORM_BIT,
                            "IODeviceTree:/", STAGE87_XNU_IOKIT_REGISTRY_TOPOLOGY_REJECTED_ORDINAL,
                            STAGE87_XNU_IOKIT_REGISTRY_TOPOLOGY_PLATFORM_ORDINAL, contract);
    selected_masks_for_node(args, interrupt_node, STAGE87_XNU_IOKIT_REGISTRY_SERVICE_INTERRUPT_BIT,
                            "IODeviceTree:/stage87-platform-personality",
                            STAGE87_XNU_IOKIT_REGISTRY_TOPOLOGY_PLATFORM_ORDINAL,
                            STAGE87_XNU_IOKIT_REGISTRY_TOPOLOGY_INTERRUPT_ORDINAL, contract);
    selected_masks_for_node(args, timer_node, STAGE87_XNU_IOKIT_REGISTRY_SERVICE_TIMER_BIT,
                            "IODeviceTree:/stage87-platform-personality",
                            STAGE87_XNU_IOKIT_REGISTRY_TOPOLOGY_PLATFORM_ORDINAL,
                            STAGE87_XNU_IOKIT_REGISTRY_TOPOLOGY_TIMER_ORDINAL, contract);
    selected_masks_for_node(args, cpu_node, STAGE87_XNU_IOKIT_REGISTRY_SERVICE_CPU_BIT,
                            "IODeviceTree:/stage87-platform-personality",
                            STAGE87_XNU_IOKIT_REGISTRY_TOPOLOGY_PLATFORM_ORDINAL,
                            STAGE87_XNU_IOKIT_REGISTRY_TOPOLOGY_CPU_ORDINAL, contract);

    contract->platform_callback_provider_ordinal = apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength,
                                                                         platform_node, "callback-provider-ordinal", 0u);
    contract->interrupt_callback_provider_ordinal = apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength,
                                                                          interrupt_node, "callback-provider-ordinal", 0xffffffffu);
    contract->timer_callback_provider_ordinal = apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength,
                                                                      timer_node, "callback-provider-ordinal", 0xffffffffu);
    contract->cpu_callback_provider_ordinal = apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength,
                                                                    cpu_node, "callback-provider-ordinal", 0xffffffffu);
    contract->rejected_callback_provider_ordinal = apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength,
                                                                         rejected_node, "callback-provider-ordinal", 0u);
    contract->platform_callback_type_mask = apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength,
                                                                  platform_node, "callback-type-mask", 0u);
    contract->interrupt_callback_type_mask = apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength,
                                                                   interrupt_node, "callback-type-mask", 0u);
    contract->timer_callback_type_mask = apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength,
                                                               timer_node, "callback-type-mask", 0u);
    contract->cpu_callback_type_mask = apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength,
                                                             cpu_node, "callback-type-mask", 0u);
    contract->rejected_callback_type_mask = apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength,
                                                                  rejected_node, "callback-type-mask", 0xffffffffu);
    contract->platform_expected_callback_mask = apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength,
                                                                      platform_node, "expected-callback-mask", 0u);
    contract->interrupt_expected_callback_mask = apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength,
                                                                       interrupt_node, "expected-callback-mask", 0u);
    contract->timer_expected_callback_mask = apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength,
                                                                   timer_node, "expected-callback-mask", 0u);
    contract->cpu_expected_callback_mask = apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength,
                                                                 cpu_node, "expected-callback-mask", 0u);
    contract->rejected_expected_callback_mask = apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength,
                                                                      rejected_node, "expected-callback-mask", 0xffffffffu);
    contract->platform_delivered_callback_mask = apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength,
                                                                       platform_node, "delivered-callback-mask", 0u);
    contract->interrupt_delivered_callback_mask = apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength,
                                                                        interrupt_node, "delivered-callback-mask", 0u);
    contract->timer_delivered_callback_mask = apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength,
                                                                    timer_node, "delivered-callback-mask", 0u);
    contract->cpu_delivered_callback_mask = apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength,
                                                                  cpu_node, "delivered-callback-mask", 0u);
    contract->rejected_delivered_callback_mask = apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength,
                                                                       rejected_node, "delivered-callback-mask", 0xffffffffu);
    contract->platform_client_ack_mask = apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength,
                                                               platform_node, "client-ack-mask", 0u);
    contract->interrupt_client_ack_mask = apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength,
                                                                interrupt_node, "client-ack-mask", 0u);
    contract->timer_client_ack_mask = apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength,
                                                            timer_node, "client-ack-mask", 0u);
    contract->cpu_client_ack_mask = apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength,
                                                          cpu_node, "client-ack-mask", 0u);
    contract->rejected_client_ack_mask = apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength,
                                                               rejected_node, "client-ack-mask", 0xffffffffu);
    contract->platform_callback_order = apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength,
                                                              platform_node, "callback-order", 0xffffffffu);
    contract->interrupt_callback_order = apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength,
                                                               interrupt_node, "callback-order", 0xffffffffu);
    contract->timer_callback_order = apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength,
                                                           timer_node, "callback-order", 0xffffffffu);
    contract->cpu_callback_order = apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength,
                                                         cpu_node, "callback-order", 0xffffffffu);
    contract->rejected_callback_order = apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength,
                                                              rejected_node, "callback-order", 0u);
    contract->platform_client_notification_order = apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength,
                                                                         platform_node, "client-notify-order", 0xffffffffu);
    contract->interrupt_client_notification_order = apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength,
                                                                          interrupt_node, "client-notify-order", 0xffffffffu);
    contract->timer_client_notification_order = apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength,
                                                                      timer_node, "client-notify-order", 0xffffffffu);
    contract->cpu_client_notification_order = apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength,
                                                                    cpu_node, "client-notify-order", 0xffffffffu);
    contract->rejected_client_notification_order = apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength,
                                                                         rejected_node, "client-notify-order", 0u);

    contract->selected_provider_callback_entry_mask = contract->provider_callback_ready_mask &
        contract->client_notification_ready_mask & contract->callback_provider_path_match_mask &
        contract->callback_provider_ordinal_match_mask & contract->callback_type_match_mask &
        contract->expected_callback_mask_match_mask & contract->delivered_callback_mask_match_mask &
        contract->client_ack_mask_match_mask & contract->callback_dependency_ready_mask &
        contract->callback_runtime_blocked_mask & contract->client_notification_runtime_blocked_mask &
        contract->callback_order_match_mask & contract->client_notification_order_match_mask &
        contract->callback_provenance_match_mask & contract->source_selected_provider_notification_entry_mask;

    if (u32_prop_equals(args, rejected_node, "prov-notify-dryrun", 0u) &&
        u32_prop_equals(args, rejected_node, "prov-callback-dryrun", 0u) &&
        u32_prop_equals(args, rejected_node, "callback-ready", 0u) &&
        u32_prop_equals(args, rejected_node, "client-notify-ready", 0u) &&
        u32_prop_equals(args, rejected_node, "callback-runtime-exec", 0u) &&
        u32_prop_equals(args, rejected_node, "client-notify-runtime-exec", 0u) &&
        string_prop_equals(args, rejected_node, "callback-provider-path", "IODeviceTree:/unlinked") &&
        u32_prop_equals(args, rejected_node, "callback-provider-ordinal",
                        STAGE87_XNU_IOKIT_REGISTRY_TOPOLOGY_REJECTED_ORDINAL) &&
        u32_prop_equals(args, rejected_node, "callback-type-mask", 0u) &&
        u32_prop_equals(args, rejected_node, "expected-callback-mask", 0u) &&
        u32_prop_equals(args, rejected_node, "delivered-callback-mask", 0u) &&
        u32_prop_equals(args, rejected_node, "client-ack-mask", 0u) &&
        u32_prop_equals(args, rejected_node, "callback-order", STAGE87_XNU_IOKIT_REGISTRY_TOPOLOGY_REJECTED_ORDINAL) &&
        u32_prop_equals(args, rejected_node, "client-notify-order", STAGE87_XNU_IOKIT_REGISTRY_TOPOLOGY_REJECTED_ORDINAL) &&
        u32_prop_equals(args, rejected_node, "callback-dependency-ready", 0u) &&
        string_prop_equals(args, rejected_node, "callback-provenance", "stage87-rejected-nocallback") &&
        u32_prop_equals(args, rejected_node, "rejected-candidate", 1u) &&
        u32_prop_equals(args, rejected_node, "stage-owned-local-only", 1u)) {
        contract->rejected_provider_callback_mask |= STAGE87_XNU_IOKIT_REGISTRY_SERVICE_REJECTED_BIT;
    }

    contract->provider_callback_client_notification_matrix = contract->selected_provider_callback_entry_mask |
        contract->rejected_provider_callback_mask;
    contract->callback_order_checksum = contract->platform_callback_order ^ contract->interrupt_callback_order ^
        contract->timer_callback_order ^ contract->cpu_callback_order ^ contract->rejected_callback_order;
    contract->client_notification_order_checksum = contract->platform_client_notification_order ^
        contract->interrupt_client_notification_order ^ contract->timer_client_notification_order ^
        contract->cpu_client_notification_order ^ contract->rejected_client_notification_order;
    contract->callback_mask_checksum = contract->platform_expected_callback_mask ^
        contract->interrupt_expected_callback_mask ^ contract->timer_expected_callback_mask ^
        contract->cpu_expected_callback_mask ^ contract->rejected_expected_callback_mask ^
        contract->platform_delivered_callback_mask ^ contract->interrupt_delivered_callback_mask ^
        contract->timer_delivered_callback_mask ^ contract->cpu_delivered_callback_mask ^
        contract->rejected_delivered_callback_mask ^ contract->platform_client_ack_mask ^
        contract->interrupt_client_ack_mask ^ contract->timer_client_ack_mask ^
        contract->cpu_client_ack_mask ^ contract->rejected_client_ack_mask;
    contract->provider_callback_path_hash = STAGE87_XNU_IOKIT_PROVIDER_CALLBACK_CLIENT_NOTIFICATION_PATH_HASH;
    contract->provider_callback_candidate_count = STAGE87_XNU_IOKIT_PROVIDER_CALLBACK_CLIENT_NOTIFICATION_CANDIDATE_COUNT;
    contract->selected_provider_callback_entry_count =
        (contract->selected_provider_callback_entry_mask == STAGE87_XNU_IOKIT_REGISTRY_SERVICE_SELECTED_REQUIRED) ?
        STAGE87_XNU_IOKIT_PROVIDER_CALLBACK_CLIENT_NOTIFICATION_ENTRY_COUNT : 0u;
    contract->rejected_provider_callback_entry_count =
        (contract->rejected_provider_callback_mask == STAGE87_XNU_IOKIT_REGISTRY_SERVICE_REJECTED_REQUIRED) ?
        STAGE87_XNU_IOKIT_PROVIDER_CALLBACK_CLIENT_NOTIFICATION_REJECTED_COUNT : 0u;
    contract->provider_callback_ready_count = contract->selected_provider_callback_entry_count;
    contract->client_notification_ready_count = contract->selected_provider_callback_entry_count;
    contract->callback_dependency_ready_count = contract->selected_provider_callback_entry_count;
    contract->client_ack_ready_count = contract->selected_provider_callback_entry_count;
    contract->rejected_no_callback_count = contract->rejected_provider_callback_entry_count;
    contract->no_callback_runtime_exec = 1u;
    contract->no_client_notification_runtime_exec = 1u;
    contract->provider_callback_checksum = contract->source_provider_notification_checksum ^
        contract->source_selected_provider_notification_entry_mask ^ contract->source_rejected_provider_notification_mask ^
        contract->source_provider_notification_matrix ^ contract->source_provider_notification_ready_mask ^
        contract->source_interest_ready_mask ^ contract->source_delivery_ready_mask ^
        contract->source_no_interest_runtime_exec ^ contract->source_no_delivery_runtime_exec ^
        contract->provider_callback_ready_mask ^ contract->client_notification_ready_mask ^
        contract->callback_provider_path_match_mask ^ contract->callback_provider_ordinal_match_mask ^
        contract->callback_type_match_mask ^ contract->expected_callback_mask_match_mask ^
        contract->delivered_callback_mask_match_mask ^ contract->client_ack_mask_match_mask ^
        contract->callback_dependency_ready_mask ^ contract->callback_runtime_blocked_mask ^
        contract->client_notification_runtime_blocked_mask ^ contract->callback_order_match_mask ^
        contract->client_notification_order_match_mask ^ contract->callback_provenance_match_mask ^
        contract->selected_provider_callback_entry_mask ^ contract->rejected_provider_callback_mask ^
        contract->provider_callback_client_notification_matrix ^ contract->callback_order_checksum ^
        contract->client_notification_order_checksum ^ contract->callback_mask_checksum ^
        contract->provider_callback_path_hash ^ contract->provider_callback_candidate_count ^
        contract->selected_provider_callback_entry_count ^ contract->rejected_provider_callback_entry_count ^
        contract->provider_callback_ready_count ^ contract->client_notification_ready_count ^
        contract->callback_dependency_ready_count ^ contract->client_ack_ready_count ^
        contract->rejected_no_callback_count ^ contract->no_callback_runtime_exec ^
        contract->no_client_notification_runtime_exec;
    contract->expected_provider_callback_checksum =
        STAGE87_XNU_IOKIT_PROVIDER_CALLBACK_CLIENT_NOTIFICATION_EXPECTED_CHECKSUM;

    contract->iokit_reference_mask =
        preflight->xnu_iokit_provider_notification_delivery_readiness_dryrun_contract.iokit_reference_mask;
    contract->iokit_runtime_blocked_mask =
        preflight->xnu_iokit_provider_notification_delivery_readiness_dryrun_contract.iokit_runtime_blocked_mask;
    contract->iokit_reference_count =
        preflight->xnu_iokit_provider_notification_delivery_readiness_dryrun_contract.iokit_reference_count;
    contract->iokit_public_compile_count =
        preflight->xnu_iokit_provider_notification_delivery_readiness_dryrun_contract.iokit_public_compile_count;
    contract->iokit_public_link_count =
        preflight->xnu_iokit_provider_notification_delivery_readiness_dryrun_contract.iokit_public_link_count;
    contract->iokit_reference_only =
        preflight->xnu_iokit_provider_notification_delivery_readiness_dryrun_contract.iokit_reference_only;
    contract->no_iokit_runtime_exec =
        preflight->xnu_iokit_provider_notification_delivery_readiness_dryrun_contract.no_iokit_runtime_exec;
    contract->no_catalog_runtime_exec =
        preflight->xnu_iokit_provider_notification_delivery_readiness_dryrun_contract.no_catalog_runtime_exec;
    contract->no_provider_runtime_exec =
        preflight->xnu_iokit_provider_notification_delivery_readiness_dryrun_contract.no_provider_runtime_exec;
    contract->no_registry_entry_runtime_exec =
        preflight->xnu_iokit_provider_notification_delivery_readiness_dryrun_contract.no_registry_entry_runtime_exec;
    contract->no_registry_topology_runtime_exec =
        preflight->xnu_iokit_provider_notification_delivery_readiness_dryrun_contract.no_registry_topology_runtime_exec;
    contract->no_property_runtime_exec =
        preflight->xnu_iokit_provider_notification_delivery_readiness_dryrun_contract.no_property_runtime_exec;
    contract->no_attach_runtime_exec =
        preflight->xnu_iokit_provider_notification_delivery_readiness_dryrun_contract.no_attach_runtime_exec;
    contract->no_start_runtime_exec =
        preflight->xnu_iokit_provider_notification_delivery_readiness_dryrun_contract.no_start_runtime_exec;
    contract->no_register_service_runtime_exec =
        preflight->xnu_iokit_provider_notification_delivery_readiness_dryrun_contract.no_register_service_runtime_exec;
    contract->no_notification_runtime_exec =
        preflight->xnu_iokit_provider_notification_delivery_readiness_dryrun_contract.no_notification_runtime_exec;
    contract->no_interest_runtime_exec =
        preflight->xnu_iokit_provider_notification_delivery_readiness_dryrun_contract.no_interest_runtime_exec;
    contract->no_delivery_runtime_exec =
        preflight->xnu_iokit_provider_notification_delivery_readiness_dryrun_contract.no_delivery_runtime_exec;
    contract->no_platform_driver_exec =
        preflight->xnu_iokit_provider_notification_delivery_readiness_dryrun_contract.no_platform_driver_exec;
    contract->no_public_xnu_exec =
        preflight->xnu_iokit_provider_notification_delivery_readiness_dryrun_contract.no_public_xnu_exec;
    contract->no_platform_runtime_exec =
        preflight->xnu_iokit_provider_notification_delivery_readiness_dryrun_contract.no_platform_runtime_exec;
    contract->no_public_pmap_exec =
        preflight->xnu_iokit_provider_notification_delivery_readiness_dryrun_contract.no_public_pmap_exec;
    contract->no_live_pmap_tables_installed =
        preflight->xnu_iokit_provider_notification_delivery_readiness_dryrun_contract.no_live_pmap_tables_installed;
    contract->proposed_workspace_written =
        preflight->xnu_iokit_provider_notification_delivery_readiness_dryrun_contract.proposed_workspace_written;
    contract->pmap_ttbr_written = preflight->xnu_iokit_provider_notification_delivery_readiness_dryrun_contract.pmap_ttbr_written;
    contract->pmap_ttbcr_written = preflight->xnu_iokit_provider_notification_delivery_readiness_dryrun_contract.pmap_ttbcr_written;
    contract->pmap_dacr_written = preflight->xnu_iokit_provider_notification_delivery_readiness_dryrun_contract.pmap_dacr_written;
    contract->pmap_sctlr_written = preflight->xnu_iokit_provider_notification_delivery_readiness_dryrun_contract.pmap_sctlr_written;
    contract->pmap_tlbs_invalidated =
        preflight->xnu_iokit_provider_notification_delivery_readiness_dryrun_contract.pmap_tlbs_invalidated;
    contract->caches_changed = preflight->xnu_iokit_provider_notification_delivery_readiness_dryrun_contract.caches_changed;
    contract->persistent_write_attempted =
        preflight->xnu_iokit_provider_notification_delivery_readiness_dryrun_contract.persistent_write_attempted;
    contract->xnu_start_executed =
        preflight->xnu_iokit_provider_notification_delivery_readiness_dryrun_contract.xnu_start_executed;
    contract->generated_macho_executed =
        preflight->xnu_iokit_provider_notification_delivery_readiness_dryrun_contract.generated_macho_executed;
    contract->proposed_platform_gap_mask = preflight->platform_gap_mask;
    contract->proposed_pexpert_gap_mask = preflight->pexpert_gap_mask;
    contract->local_only = 1u;
    contract->fail_closed = 1u;

    source_ok = (contract->source_provider_notification_status == STAGE87_STATUS_OK &&
                 contract->source_provider_notification_satisfied_mask ==
                 STAGE87_XNU_IOKIT_PROVIDER_NOTIFICATION_DELIVERY_DRYRUN_REQUIRED_MASK &&
                 contract->source_provider_notification_failure_mask == 0u &&
                 contract->source_provider_notification_checksum == STAGE87_XNU_IOKIT_PROVIDER_NOTIFICATION_DELIVERY_EXPECTED_CHECKSUM &&
                 contract->source_selected_provider_notification_entry_mask == STAGE87_XNU_IOKIT_REGISTRY_SERVICE_SELECTED_REQUIRED &&
                 contract->source_rejected_provider_notification_mask == STAGE87_XNU_IOKIT_REGISTRY_SERVICE_REJECTED_REQUIRED &&
                 contract->source_provider_notification_matrix == (STAGE87_XNU_IOKIT_REGISTRY_SERVICE_SELECTED_REQUIRED |
                                                                   STAGE87_XNU_IOKIT_REGISTRY_SERVICE_REJECTED_REQUIRED) &&
                 contract->source_provider_notification_ready_mask == STAGE87_XNU_IOKIT_REGISTRY_SERVICE_SELECTED_REQUIRED &&
                 contract->source_interest_ready_mask == STAGE87_XNU_IOKIT_REGISTRY_SERVICE_SELECTED_REQUIRED &&
                 contract->source_delivery_ready_mask == STAGE87_XNU_IOKIT_REGISTRY_SERVICE_SELECTED_REQUIRED &&
                 contract->source_no_interest_runtime_exec == 1u &&
                 contract->source_no_delivery_runtime_exec == 1u &&
                 contract->source_lifecycle_status == STAGE87_STATUS_OK &&
                 contract->source_attach_start_status == STAGE87_STATUS_OK &&
                 contract->source_topology_status == STAGE87_STATUS_OK &&
                 contract->source_property_inheritance_status == STAGE87_STATUS_OK &&
                 contract->source_catalog_status == STAGE87_STATUS_OK &&
                 contract->source_provider_status == STAGE87_STATUS_OK &&
                 contract->source_registry_status == STAGE87_STATUS_OK &&
                 contract->source_match_status == STAGE87_STATUS_OK &&
                 contract->source_scaffold_status == STAGE87_STATUS_OK &&
                 contract->source_pexpert_status == STAGE87_STATUS_OK &&
                 contract->source_pmap_transition_status == STAGE87_STATUS_OK &&
                 contract->source_compile_graph_status == STAGE87_STATUS_OK &&
                 contract->source_object_subset_status == STAGE87_STATUS_OK &&
                 contract->source_link_status == STAGE87_STATUS_OK &&
                 contract->source_loader_safety_mask == contract->source_loader_safety_required_mask) ? 1u : 0u;
    if (source_ok) {
        contract->satisfied_mask |= STAGE87_XNU_IOKIT_PROVIDER_CALLBACK_CLIENT_NOTIFICATION_DRYRUN_SAT_SOURCE_PROVIDER_NOTIFICATION;
    } else {
        contract->failure_mask |= STAGE87_XNU_IOKIT_PROVIDER_CALLBACK_CLIENT_NOTIFICATION_DRYRUN_FAIL_SOURCE;
    }

    if (contract->apple_dt_semantic_mask == STAGE87_DT_READY_REQUIRED) {
        contract->satisfied_mask |= STAGE87_XNU_IOKIT_PROVIDER_CALLBACK_CLIENT_NOTIFICATION_DRYRUN_SAT_APPLE_DT;
    } else {
        contract->failure_mask |= STAGE87_XNU_IOKIT_PROVIDER_CALLBACK_CLIENT_NOTIFICATION_DRYRUN_FAIL_DT;
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
        contract->satisfied_mask |= STAGE87_XNU_IOKIT_PROVIDER_CALLBACK_CLIENT_NOTIFICATION_DRYRUN_SAT_PERSONALITY_NODE_SET;
    } else {
        contract->failure_mask |= STAGE87_XNU_IOKIT_PROVIDER_CALLBACK_CLIENT_NOTIFICATION_DRYRUN_FAIL_PERSONALITY;
    }

    if (contract->provider_callback_ready_mask == STAGE87_XNU_IOKIT_REGISTRY_SERVICE_SELECTED_REQUIRED) {
        contract->satisfied_mask |= STAGE87_XNU_IOKIT_PROVIDER_CALLBACK_CLIENT_NOTIFICATION_DRYRUN_SAT_PROVIDER_CALLBACK_READY;
    } else {
        contract->failure_mask |= STAGE87_XNU_IOKIT_PROVIDER_CALLBACK_CLIENT_NOTIFICATION_DRYRUN_FAIL_CALLBACK;
    }
    if (contract->client_notification_ready_mask == STAGE87_XNU_IOKIT_REGISTRY_SERVICE_SELECTED_REQUIRED) {
        contract->satisfied_mask |= STAGE87_XNU_IOKIT_PROVIDER_CALLBACK_CLIENT_NOTIFICATION_DRYRUN_SAT_CLIENT_NOTIFICATION_READY;
    } else {
        contract->failure_mask |= STAGE87_XNU_IOKIT_PROVIDER_CALLBACK_CLIENT_NOTIFICATION_DRYRUN_FAIL_CLIENT_NOTIFICATION;
    }
    if (contract->callback_provider_path_match_mask == STAGE87_XNU_IOKIT_REGISTRY_SERVICE_SELECTED_REQUIRED) {
        contract->satisfied_mask |= STAGE87_XNU_IOKIT_PROVIDER_CALLBACK_CLIENT_NOTIFICATION_DRYRUN_SAT_PROVIDER_PATH_MATRIX;
    } else {
        contract->failure_mask |= STAGE87_XNU_IOKIT_PROVIDER_CALLBACK_CLIENT_NOTIFICATION_DRYRUN_FAIL_CALLBACK;
    }
    if (contract->callback_provider_ordinal_match_mask == STAGE87_XNU_IOKIT_REGISTRY_SERVICE_SELECTED_REQUIRED) {
        contract->satisfied_mask |= STAGE87_XNU_IOKIT_PROVIDER_CALLBACK_CLIENT_NOTIFICATION_DRYRUN_SAT_PROVIDER_ORDINAL_MATRIX;
    } else {
        contract->failure_mask |= STAGE87_XNU_IOKIT_PROVIDER_CALLBACK_CLIENT_NOTIFICATION_DRYRUN_FAIL_CALLBACK;
    }
    if (contract->callback_type_match_mask == STAGE87_XNU_IOKIT_REGISTRY_SERVICE_SELECTED_REQUIRED) {
        contract->satisfied_mask |= STAGE87_XNU_IOKIT_PROVIDER_CALLBACK_CLIENT_NOTIFICATION_DRYRUN_SAT_CALLBACK_TYPE_MATRIX;
    } else {
        contract->failure_mask |= STAGE87_XNU_IOKIT_PROVIDER_CALLBACK_CLIENT_NOTIFICATION_DRYRUN_FAIL_CALLBACK;
    }
    if (contract->expected_callback_mask_match_mask == STAGE87_XNU_IOKIT_REGISTRY_SERVICE_SELECTED_REQUIRED) {
        contract->satisfied_mask |= STAGE87_XNU_IOKIT_PROVIDER_CALLBACK_CLIENT_NOTIFICATION_DRYRUN_SAT_EXPECTED_CALLBACK_MATRIX;
    } else {
        contract->failure_mask |= STAGE87_XNU_IOKIT_PROVIDER_CALLBACK_CLIENT_NOTIFICATION_DRYRUN_FAIL_CALLBACK;
    }
    if (contract->delivered_callback_mask_match_mask == STAGE87_XNU_IOKIT_REGISTRY_SERVICE_SELECTED_REQUIRED) {
        contract->satisfied_mask |= STAGE87_XNU_IOKIT_PROVIDER_CALLBACK_CLIENT_NOTIFICATION_DRYRUN_SAT_DELIVERED_CALLBACK_MATRIX;
    } else {
        contract->failure_mask |= STAGE87_XNU_IOKIT_PROVIDER_CALLBACK_CLIENT_NOTIFICATION_DRYRUN_FAIL_CALLBACK;
    }
    if (contract->client_ack_mask_match_mask == STAGE87_XNU_IOKIT_REGISTRY_SERVICE_SELECTED_REQUIRED) {
        contract->satisfied_mask |= STAGE87_XNU_IOKIT_PROVIDER_CALLBACK_CLIENT_NOTIFICATION_DRYRUN_SAT_CLIENT_ACK_MATRIX;
    } else {
        contract->failure_mask |= STAGE87_XNU_IOKIT_PROVIDER_CALLBACK_CLIENT_NOTIFICATION_DRYRUN_FAIL_ACK;
    }

    order_ok = (contract->callback_order_match_mask == STAGE87_XNU_IOKIT_REGISTRY_SERVICE_SELECTED_REQUIRED &&
                contract->client_notification_order_match_mask == STAGE87_XNU_IOKIT_REGISTRY_SERVICE_SELECTED_REQUIRED &&
                contract->callback_order_checksum == STAGE87_XNU_IOKIT_REGISTRY_TOPOLOGY_REJECTED_ORDINAL &&
                contract->client_notification_order_checksum == STAGE87_XNU_IOKIT_REGISTRY_TOPOLOGY_REJECTED_ORDINAL) ? 1u : 0u;
    if (order_ok) {
        contract->satisfied_mask |= STAGE87_XNU_IOKIT_PROVIDER_CALLBACK_CLIENT_NOTIFICATION_DRYRUN_SAT_ORDER_MATRIX;
    } else {
        contract->failure_mask |= STAGE87_XNU_IOKIT_PROVIDER_CALLBACK_CLIENT_NOTIFICATION_DRYRUN_FAIL_ORDER_DEPENDENCY;
    }

    callback_dependency_ok =
        (contract->callback_dependency_ready_mask == STAGE87_XNU_IOKIT_REGISTRY_SERVICE_SELECTED_REQUIRED &&
         contract->callback_dependency_ready_count == STAGE87_XNU_IOKIT_PROVIDER_CALLBACK_CLIENT_NOTIFICATION_ENTRY_COUNT) ? 1u : 0u;
    if (callback_dependency_ok) {
        contract->satisfied_mask |= STAGE87_XNU_IOKIT_PROVIDER_CALLBACK_CLIENT_NOTIFICATION_DRYRUN_SAT_DEPENDENCY_MATRIX;
    } else {
        contract->failure_mask |= STAGE87_XNU_IOKIT_PROVIDER_CALLBACK_CLIENT_NOTIFICATION_DRYRUN_FAIL_ORDER_DEPENDENCY;
    }

    if (contract->callback_runtime_blocked_mask == STAGE87_XNU_IOKIT_REGISTRY_SERVICE_SELECTED_REQUIRED &&
        contract->client_notification_runtime_blocked_mask == STAGE87_XNU_IOKIT_REGISTRY_SERVICE_SELECTED_REQUIRED &&
        contract->no_callback_runtime_exec == 1u && contract->no_client_notification_runtime_exec == 1u) {
        contract->satisfied_mask |= STAGE87_XNU_IOKIT_PROVIDER_CALLBACK_CLIENT_NOTIFICATION_DRYRUN_SAT_RUNTIME_BLOCKED;
    } else {
        contract->failure_mask |= STAGE87_XNU_IOKIT_PROVIDER_CALLBACK_CLIENT_NOTIFICATION_DRYRUN_FAIL_CALLBACK |
            STAGE87_XNU_IOKIT_PROVIDER_CALLBACK_CLIENT_NOTIFICATION_DRYRUN_FAIL_CLIENT_NOTIFICATION;
    }
    if (contract->callback_provenance_match_mask == STAGE87_XNU_IOKIT_REGISTRY_SERVICE_SELECTED_REQUIRED) {
        contract->satisfied_mask |= STAGE87_XNU_IOKIT_PROVIDER_CALLBACK_CLIENT_NOTIFICATION_DRYRUN_SAT_PROVENANCE;
    } else {
        contract->failure_mask |= STAGE87_XNU_IOKIT_PROVIDER_CALLBACK_CLIENT_NOTIFICATION_DRYRUN_FAIL_CALLBACK;
    }
    if (contract->selected_provider_callback_entry_mask == STAGE87_XNU_IOKIT_REGISTRY_SERVICE_SELECTED_REQUIRED) {
        contract->satisfied_mask |= STAGE87_XNU_IOKIT_PROVIDER_CALLBACK_CLIENT_NOTIFICATION_DRYRUN_SAT_SELECTED_CALLBACK_MATRIX;
    } else {
        contract->failure_mask |= STAGE87_XNU_IOKIT_PROVIDER_CALLBACK_CLIENT_NOTIFICATION_DRYRUN_FAIL_CALLBACK;
    }
    if (contract->rejected_provider_callback_mask == STAGE87_XNU_IOKIT_REGISTRY_SERVICE_REJECTED_REQUIRED &&
        contract->source_rejected_provider_notification_mask == STAGE87_XNU_IOKIT_REGISTRY_SERVICE_REJECTED_REQUIRED &&
        contract->rejected_no_callback_count == STAGE87_XNU_IOKIT_PROVIDER_CALLBACK_CLIENT_NOTIFICATION_REJECTED_COUNT) {
        contract->satisfied_mask |= STAGE87_XNU_IOKIT_PROVIDER_CALLBACK_CLIENT_NOTIFICATION_DRYRUN_SAT_REJECT_NO_CALLBACK;
    } else {
        contract->failure_mask |= STAGE87_XNU_IOKIT_PROVIDER_CALLBACK_CLIENT_NOTIFICATION_DRYRUN_FAIL_REJECTED;
    }
    if (contract->provider_callback_candidate_count == STAGE87_XNU_IOKIT_PROVIDER_CALLBACK_CLIENT_NOTIFICATION_CANDIDATE_COUNT &&
        contract->selected_provider_callback_entry_count == STAGE87_XNU_IOKIT_PROVIDER_CALLBACK_CLIENT_NOTIFICATION_ENTRY_COUNT &&
        contract->rejected_provider_callback_entry_count == STAGE87_XNU_IOKIT_PROVIDER_CALLBACK_CLIENT_NOTIFICATION_REJECTED_COUNT &&
        contract->provider_callback_ready_count == STAGE87_XNU_IOKIT_PROVIDER_CALLBACK_CLIENT_NOTIFICATION_ENTRY_COUNT &&
        contract->client_notification_ready_count == STAGE87_XNU_IOKIT_PROVIDER_CALLBACK_CLIENT_NOTIFICATION_ENTRY_COUNT &&
        contract->callback_dependency_ready_count == STAGE87_XNU_IOKIT_PROVIDER_CALLBACK_CLIENT_NOTIFICATION_ENTRY_COUNT &&
        contract->client_ack_ready_count == STAGE87_XNU_IOKIT_PROVIDER_CALLBACK_CLIENT_NOTIFICATION_ENTRY_COUNT &&
        contract->provider_callback_client_notification_matrix == (STAGE87_XNU_IOKIT_REGISTRY_SERVICE_SELECTED_REQUIRED |
                                                                  STAGE87_XNU_IOKIT_REGISTRY_SERVICE_REJECTED_REQUIRED)) {
        contract->satisfied_mask |= STAGE87_XNU_IOKIT_PROVIDER_CALLBACK_CLIENT_NOTIFICATION_DRYRUN_SAT_DRYRUN_COUNTS;
    } else {
        contract->failure_mask |= STAGE87_XNU_IOKIT_PROVIDER_CALLBACK_CLIENT_NOTIFICATION_DRYRUN_FAIL_COUNTS;
    }
    if (contract->provider_callback_checksum == contract->expected_provider_callback_checksum) {
        contract->satisfied_mask |= STAGE87_XNU_IOKIT_PROVIDER_CALLBACK_CLIENT_NOTIFICATION_DRYRUN_SAT_CALLBACK_CHECKSUM;
    } else {
        contract->failure_mask |= STAGE87_XNU_IOKIT_PROVIDER_CALLBACK_CLIENT_NOTIFICATION_DRYRUN_FAIL_CHECKSUM;
    }

    if (contract->iokit_reference_mask == STAGE87_XNU_IOKIT_REFERENCE_REQUIRED &&
        contract->iokit_runtime_blocked_mask == STAGE87_XNU_IOKIT_REFERENCE_REQUIRED &&
        contract->iokit_reference_count >= 3u && contract->iokit_public_compile_count == 0u &&
        contract->iokit_public_link_count == 0u && contract->iokit_reference_only == 1u) {
        contract->satisfied_mask |= STAGE87_XNU_IOKIT_PROVIDER_CALLBACK_CLIENT_NOTIFICATION_DRYRUN_SAT_IOKIT_REFERENCE;
    } else {
        contract->failure_mask |= STAGE87_XNU_IOKIT_PROVIDER_CALLBACK_CLIENT_NOTIFICATION_DRYRUN_FAIL_PUBLIC_BOUNDARY;
    }

    public_boundary_ok = (contract->no_public_xnu_exec == 1u && contract->no_platform_runtime_exec == 1u &&
                          contract->no_iokit_runtime_exec == 1u && contract->no_catalog_runtime_exec == 1u &&
                          contract->no_provider_runtime_exec == 1u &&
                          contract->no_registry_entry_runtime_exec == 1u &&
                          contract->no_registry_topology_runtime_exec == 1u &&
                          contract->no_property_runtime_exec == 1u && contract->no_attach_runtime_exec == 1u &&
                          contract->no_start_runtime_exec == 1u &&
                          contract->no_register_service_runtime_exec == 1u &&
                          contract->no_notification_runtime_exec == 1u && contract->no_interest_runtime_exec == 1u &&
                          contract->no_delivery_runtime_exec == 1u && contract->no_callback_runtime_exec == 1u &&
                          contract->no_client_notification_runtime_exec == 1u && contract->no_platform_driver_exec == 1u &&
                          contract->source_object_subset_status == STAGE87_STATUS_OK &&
                          contract->source_link_status == STAGE87_STATUS_OK) ? 1u : 0u;
    if (public_boundary_ok) {
        contract->satisfied_mask |= STAGE87_XNU_IOKIT_PROVIDER_CALLBACK_CLIENT_NOTIFICATION_DRYRUN_SAT_PUBLIC_BOUNDARY |
            STAGE87_XNU_IOKIT_PROVIDER_CALLBACK_CLIENT_NOTIFICATION_DRYRUN_SAT_NO_IOKIT_RUNTIME_EXEC |
            STAGE87_XNU_IOKIT_PROVIDER_CALLBACK_CLIENT_NOTIFICATION_DRYRUN_SAT_NO_CALLBACK_RUNTIME_EXEC |
            STAGE87_XNU_IOKIT_PROVIDER_CALLBACK_CLIENT_NOTIFICATION_DRYRUN_SAT_NO_CLIENT_NOTIFICATION_RUNTIME_EXEC |
            STAGE87_XNU_IOKIT_PROVIDER_CALLBACK_CLIENT_NOTIFICATION_DRYRUN_SAT_NO_PUBLIC_XNU_EXEC;
    } else {
        contract->failure_mask |= STAGE87_XNU_IOKIT_PROVIDER_CALLBACK_CLIENT_NOTIFICATION_DRYRUN_FAIL_PUBLIC_BOUNDARY;
    }

    if (contract->no_public_pmap_exec == 1u) {
        contract->satisfied_mask |= STAGE87_XNU_IOKIT_PROVIDER_CALLBACK_CLIENT_NOTIFICATION_DRYRUN_SAT_NO_PUBLIC_PMAP_EXEC;
    } else {
        contract->failure_mask |= STAGE87_XNU_IOKIT_PROVIDER_CALLBACK_CLIENT_NOTIFICATION_DRYRUN_FAIL_PMAP_BOUNDARY;
    }

    pmap_boundary_ok = (contract->no_live_pmap_tables_installed == 0u &&
                        contract->proposed_workspace_written == 0u &&
                        contract->pmap_ttbr_written == 0u && contract->pmap_ttbcr_written == 0u &&
                        contract->pmap_dacr_written == 0u && contract->pmap_sctlr_written == 0u &&
                        contract->pmap_tlbs_invalidated == 0u && contract->caches_changed == 0u) ? 1u : 0u;
    if (contract->no_live_pmap_tables_installed == 0u) {
        contract->satisfied_mask |= STAGE87_XNU_IOKIT_PROVIDER_CALLBACK_CLIENT_NOTIFICATION_DRYRUN_SAT_NO_LIVE_PMAP_INSTALL;
    }
    if (contract->pmap_tlbs_invalidated == 0u && contract->caches_changed == 0u) {
        contract->satisfied_mask |= STAGE87_XNU_IOKIT_PROVIDER_CALLBACK_CLIENT_NOTIFICATION_DRYRUN_SAT_NO_TLB_CACHE_CHANGE;
    }
    if (pmap_boundary_ok && contract->source_pmap_transition_status == STAGE87_STATUS_OK) {
        contract->satisfied_mask |= STAGE87_XNU_IOKIT_PROVIDER_CALLBACK_CLIENT_NOTIFICATION_DRYRUN_SAT_PMAP_BOUNDARY;
    } else {
        contract->failure_mask |= STAGE87_XNU_IOKIT_PROVIDER_CALLBACK_CLIENT_NOTIFICATION_DRYRUN_FAIL_PMAP_BOUNDARY;
    }

    if (contract->generated_macho_executed == 0u && contract->xnu_start_executed == 0u) {
        contract->satisfied_mask |= STAGE87_XNU_IOKIT_PROVIDER_CALLBACK_CLIENT_NOTIFICATION_DRYRUN_SAT_NO_XNU_MACHO_EXEC;
    } else {
        contract->failure_mask |= STAGE87_XNU_IOKIT_PROVIDER_CALLBACK_CLIENT_NOTIFICATION_DRYRUN_FAIL_SAFETY_BOUNDARY;
    }
    if (contract->local_only == 1u && contract->fail_closed == 1u &&
        contract->persistent_write_attempted == 0u && contract->xnu_start_executed == 0u &&
        contract->generated_macho_executed == 0u &&
        contract->proposed_platform_gap_mask == (STAGE87_PLATFORM_GAP_REQUIRED_RECORDED & ~STAGE87_PLATFORM_GAP_IOKIT_STACK) &&
        contract->proposed_pexpert_gap_mask == 0u &&
        contract->selected_provider_callback_entry_mask == STAGE87_XNU_IOKIT_REGISTRY_SERVICE_SELECTED_REQUIRED &&
        contract->rejected_provider_callback_mask == STAGE87_XNU_IOKIT_REGISTRY_SERVICE_REJECTED_REQUIRED) {
        contract->satisfied_mask |= STAGE87_XNU_IOKIT_PROVIDER_CALLBACK_CLIENT_NOTIFICATION_DRYRUN_SAT_LOCAL_ONLY_FAIL_CLOSED;
    } else {
        contract->failure_mask |= STAGE87_XNU_IOKIT_PROVIDER_CALLBACK_CLIENT_NOTIFICATION_DRYRUN_FAIL_ROLLUP |
            STAGE87_XNU_IOKIT_PROVIDER_CALLBACK_CLIENT_NOTIFICATION_DRYRUN_FAIL_SAFETY_BOUNDARY;
    }

    contract->checksum = stage87_iokit_provider_callback_client_notification_dryrun_checksum(contract);
    if (contract->checksum != stage87_iokit_provider_callback_client_notification_dryrun_checksum(contract)) {
        contract->failure_mask |= STAGE87_XNU_IOKIT_PROVIDER_CALLBACK_CLIENT_NOTIFICATION_DRYRUN_FAIL_CHECKSUM;
    }

    if (contract->satisfied_mask == contract->required_mask && contract->failure_mask == 0u) {
        contract->status = STAGE87_STATUS_OK;
    } else {
        contract->status = STAGE87_STATUS_FAIL(contract->failure_mask);
    }

    stage87_iokit_provider_callback_client_notification_dryrun_log(contract);
    return contract->status == STAGE87_STATUS_OK;
}

const struct stage87_xnu_iokit_provider_callback_client_notification_readiness_dryrun_contract *
stage87_xnu_iokit_provider_callback_client_notification_readiness_dryrun_contract_result(void)
{
    return &g_stage87_xnu_iokit_provider_callback_client_notification_readiness_dryrun_contract;
}
