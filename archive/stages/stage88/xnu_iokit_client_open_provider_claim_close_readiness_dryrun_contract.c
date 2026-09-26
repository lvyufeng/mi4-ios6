#include "stage88.h"

static struct stage88_xnu_iokit_client_open_provider_claim_close_readiness_dryrun_contract
    g_stage88_xnu_iokit_client_open_provider_claim_close_readiness_dryrun_contract;

static uint32_t stage88_iokit_client_open_provider_claim_close_dryrun_checksum(
    volatile const struct stage88_xnu_iokit_client_open_provider_claim_close_readiness_dryrun_contract *contract)
{
    volatile const uint32_t *words = (volatile const uint32_t *)(uintptr_t)contract;
    uint32_t count = (uint32_t)(offsetof(struct stage88_xnu_iokit_client_open_provider_claim_close_readiness_dryrun_contract,
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
                                    struct stage88_xnu_iokit_client_open_provider_claim_close_readiness_dryrun_contract *contract)
{
    if (u32_prop_equals(args, node, "client-open-dryrun", 1u) &&
        u32_prop_equals(args, node, "client-open-ready", 1u)) {
        contract->client_open_ready_mask |= bit;
    }
    if (u32_prop_equals(args, node, "provider-claim-ready", 1u)) {
        contract->provider_claim_ready_mask |= bit;
    }
    if (u32_prop_equals(args, node, "client-close-ready", 1u)) {
        contract->client_close_ready_mask |= bit;
    }
    if (string_prop_equals(args, node, "open-provider-path", provider_path)) {
        contract->open_provider_path_match_mask |= bit;
    }
    if (u32_prop_equals(args, node, "open-provider-ordinal", provider_ordinal)) {
        contract->open_provider_ordinal_match_mask |= bit;
    }
    if (u32_prop_equals(args, node, "open-type-mask", bit)) {
        contract->open_type_match_mask |= bit;
    }
    if (u32_prop_equals(args, node, "expected-open-mask", bit)) {
        contract->expected_open_mask_match_mask |= bit;
    }
    if (u32_prop_equals(args, node, "claimed-open-mask", bit)) {
        contract->claimed_open_mask_match_mask |= bit;
    }
    if (u32_prop_equals(args, node, "client-close-mask", bit)) {
        contract->client_close_mask_match_mask |= bit;
    }
    if (u32_prop_equals(args, node, "open-dependency-ready", 1u)) {
        contract->open_dependency_ready_mask |= bit;
    }
    if (u32_prop_equals(args, node, "open-runtime-exec", 0u)) {
        contract->open_runtime_blocked_mask |= bit;
    }
    if (u32_prop_equals(args, node, "claim-runtime-exec", 0u)) {
        contract->provider_claim_runtime_blocked_mask |= bit;
    }
    if (u32_prop_equals(args, node, "close-runtime-exec", 0u)) {
        contract->client_close_runtime_blocked_mask |= bit;
    }
    if (u32_prop_equals(args, node, "open-order", order)) {
        contract->open_order_match_mask |= bit;
    }
    if (u32_prop_equals(args, node, "close-order", order)) {
        contract->close_order_match_mask |= bit;
    }
    if (string_prop_equals(args, node, "open-provenance", "stage88-provider-callback")) {
        contract->open_provenance_match_mask |= bit;
    }
}

static void stage88_iokit_client_open_provider_claim_close_dryrun_log(
    const struct stage88_xnu_iokit_client_open_provider_claim_close_readiness_dryrun_contract *contract)
{
    xnu_log_kv32("stage88_xnu_iokit_client_open_provider_claim_close_readiness_dryrun_contract_status", contract->status);
    xnu_log_kv32("stage88_xnu_iokit_client_open_provider_claim_close_readiness_dryrun_contract_required_mask", contract->required_mask);
    xnu_log_kv32("stage88_xnu_iokit_client_open_provider_claim_close_readiness_dryrun_contract_satisfied_mask", contract->satisfied_mask);
    xnu_log_kv32("stage88_xnu_iokit_client_open_provider_claim_close_readiness_dryrun_contract_failure_mask", contract->failure_mask);
    xnu_log_kv32("stage88_xnu_iokit_client_open_provider_claim_close_readiness_dryrun_contract_checksum", contract->checksum);
    xnu_log_kv32("stage88_xnu_iokit_client_open_source_status", contract->source_provider_callback_status);
    xnu_log_kv32("stage88_xnu_iokit_client_open_source_satisfied_mask", contract->source_provider_callback_satisfied_mask);
    xnu_log_kv32("stage88_xnu_iokit_client_open_source_failure_mask", contract->source_provider_callback_failure_mask);
    xnu_log_kv32("stage88_xnu_iokit_client_open_source_checksum", contract->source_provider_callback_checksum);
    xnu_log_kv32("stage88_xnu_iokit_client_open_source_selected_provider_callback_entry_mask", contract->source_selected_provider_callback_entry_mask);
    xnu_log_kv32("stage88_xnu_iokit_client_open_source_rejected_provider_callback_mask", contract->source_rejected_provider_callback_mask);
    xnu_log_kv32("stage88_xnu_iokit_client_open_source_provider_callback_matrix", contract->source_provider_callback_matrix);
    xnu_log_kv32("stage88_xnu_iokit_client_open_source_client_ack_mask_match_mask", contract->source_client_ack_mask_match_mask);
    xnu_log_kv32("stage88_xnu_iokit_client_open_apple_dt_semantic_mask", contract->apple_dt_semantic_mask);
    xnu_log_kv32("stage88_xnu_iokit_client_open_platform_props", contract->platform_personality_prop_count);
    xnu_log_kv32("stage88_xnu_iokit_client_open_interrupt_props", contract->interrupt_personality_prop_count);
    xnu_log_kv32("stage88_xnu_iokit_client_open_timer_props", contract->timer_personality_prop_count);
    xnu_log_kv32("stage88_xnu_iokit_client_open_cpu_props", contract->cpu_personality_prop_count);
    xnu_log_kv32("stage88_xnu_iokit_client_open_rejected_props", contract->rejected_personality_prop_count);
    xnu_log_kv32("stage88_xnu_iokit_client_open_ready_mask", contract->client_open_ready_mask);
    xnu_log_kv32("stage88_xnu_iokit_client_open_provider_claim_ready_mask", contract->provider_claim_ready_mask);
    xnu_log_kv32("stage88_xnu_iokit_client_open_client_close_ready_mask", contract->client_close_ready_mask);
    xnu_log_kv32("stage88_xnu_iokit_client_open_provider_path_match_mask", contract->open_provider_path_match_mask);
    xnu_log_kv32("stage88_xnu_iokit_client_open_provider_ordinal_match_mask", contract->open_provider_ordinal_match_mask);
    xnu_log_kv32("stage88_xnu_iokit_client_open_type_match_mask", contract->open_type_match_mask);
    xnu_log_kv32("stage88_xnu_iokit_client_open_expected_open_mask_match_mask", contract->expected_open_mask_match_mask);
    xnu_log_kv32("stage88_xnu_iokit_client_open_claimed_open_mask_match_mask", contract->claimed_open_mask_match_mask);
    xnu_log_kv32("stage88_xnu_iokit_client_open_client_close_mask_match_mask", contract->client_close_mask_match_mask);
    xnu_log_kv32("stage88_xnu_iokit_client_open_dependency_ready_mask", contract->open_dependency_ready_mask);
    xnu_log_kv32("stage88_xnu_iokit_client_open_runtime_blocked_mask", contract->open_runtime_blocked_mask);
    xnu_log_kv32("stage88_xnu_iokit_client_open_claim_runtime_blocked_mask", contract->provider_claim_runtime_blocked_mask);
    xnu_log_kv32("stage88_xnu_iokit_client_open_close_runtime_blocked_mask", contract->client_close_runtime_blocked_mask);
    xnu_log_kv32("stage88_xnu_iokit_client_open_order_match_mask", contract->open_order_match_mask);
    xnu_log_kv32("stage88_xnu_iokit_client_open_close_order_match_mask", contract->close_order_match_mask);
    xnu_log_kv32("stage88_xnu_iokit_client_open_provenance_match_mask", contract->open_provenance_match_mask);
    xnu_log_kv32("stage88_xnu_iokit_client_open_selected_client_open_entry_mask", contract->selected_client_open_entry_mask);
    xnu_log_kv32("stage88_xnu_iokit_client_open_rejected_client_open_mask", contract->rejected_client_open_mask);
    xnu_log_kv32("stage88_xnu_iokit_client_open_matrix", contract->client_open_provider_claim_close_matrix);
    xnu_log_kv32("stage88_xnu_iokit_client_open_open_order_checksum", contract->open_order_checksum);
    xnu_log_kv32("stage88_xnu_iokit_client_open_close_order_checksum", contract->close_order_checksum);
    xnu_log_kv32("stage88_xnu_iokit_client_open_mask_checksum", contract->client_open_mask_checksum);
    xnu_log_kv32("stage88_xnu_iokit_client_open_path_hash", contract->client_open_path_hash);
    xnu_log_kv32("stage88_xnu_iokit_client_open_checksum", contract->client_open_checksum);
    xnu_log_kv32("stage88_xnu_iokit_client_open_expected_checksum", contract->expected_client_open_checksum);
    xnu_log_kv32("stage88_xnu_iokit_client_open_candidate_count", contract->client_open_candidate_count);
    xnu_log_kv32("stage88_xnu_iokit_client_open_selected_entry_count", contract->selected_client_open_entry_count);
    xnu_log_kv32("stage88_xnu_iokit_client_open_rejected_entry_count", contract->rejected_client_open_entry_count);
    xnu_log_kv32("stage88_xnu_iokit_client_open_open_ready_count", contract->client_open_ready_count);
    xnu_log_kv32("stage88_xnu_iokit_client_open_claim_ready_count", contract->provider_claim_ready_count);
    xnu_log_kv32("stage88_xnu_iokit_client_open_close_ready_count", contract->client_close_ready_count);
    xnu_log_kv32("stage88_xnu_iokit_client_open_rejected_no_open_count", contract->rejected_no_open_count);
    xnu_log_kv32("stage88_xnu_iokit_client_open_public_compile_count", contract->iokit_public_compile_count);
    xnu_log_kv32("stage88_xnu_iokit_client_open_public_link_count", contract->iokit_public_link_count);
    xnu_log_kv32("stage88_xnu_iokit_client_open_no_iokit_runtime_exec", contract->no_iokit_runtime_exec);
    xnu_log_kv32("stage88_xnu_iokit_client_open_no_open_runtime_exec", contract->no_open_runtime_exec);
    xnu_log_kv32("stage88_xnu_iokit_client_open_no_claim_runtime_exec", contract->no_claim_runtime_exec);
    xnu_log_kv32("stage88_xnu_iokit_client_open_no_close_runtime_exec", contract->no_close_runtime_exec);
    xnu_log_kv32("stage88_xnu_iokit_client_open_no_live_pmap_tables_installed", contract->no_live_pmap_tables_installed);
    xnu_log_kv32("stage88_xnu_iokit_client_open_proposed_workspace_written", contract->proposed_workspace_written);
    xnu_log_kv32("stage88_xnu_iokit_client_open_pmap_ttbr_written", contract->pmap_ttbr_written);
    xnu_log_kv32("stage88_xnu_iokit_client_open_pmap_tlbs_invalidated", contract->pmap_tlbs_invalidated);
    xnu_log_kv32("stage88_xnu_iokit_client_open_caches_changed", contract->caches_changed);
    xnu_log_kv32("stage88_xnu_iokit_client_open_persistent_write_attempted", contract->persistent_write_attempted);
}

int stage88_xnu_iokit_client_open_provider_claim_close_readiness_dryrun_contract_selftest(
    const struct stage88_loader_preflight *preflight)
{
    struct stage88_xnu_iokit_client_open_provider_claim_close_readiness_dryrun_contract *contract =
        &g_stage88_xnu_iokit_client_open_provider_claim_close_readiness_dryrun_contract;
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
    uint32_t open_dependency_ok;
    uint32_t public_boundary_ok;
    uint32_t pmap_boundary_ok;

    memset(contract, 0, sizeof(*contract));
    contract->version = STAGE88_XNU_IOKIT_CLIENT_OPEN_PROVIDER_CLAIM_CLOSE_DRYRUN_CONTRACT_VERSION;
    contract->size = sizeof(*contract);
    contract->status = STAGE88_STATUS_BASE;
    contract->required_mask = STAGE88_XNU_IOKIT_CLIENT_OPEN_PROVIDER_CLAIM_CLOSE_DRYRUN_REQUIRED_MASK;

    if (!preflight || !args || !root) {
        contract->failure_mask = STAGE88_XNU_IOKIT_CLIENT_OPEN_PROVIDER_CLAIM_CLOSE_DRYRUN_FAIL_SOURCE |
            STAGE88_XNU_IOKIT_CLIENT_OPEN_PROVIDER_CLAIM_CLOSE_DRYRUN_FAIL_SAFETY_BOUNDARY;
        contract->checksum = stage88_iokit_client_open_provider_claim_close_dryrun_checksum(contract);
        stage88_iokit_client_open_provider_claim_close_dryrun_log(contract);
        return 0;
    }

    contract->source_provider_callback_status =
        preflight->xnu_iokit_provider_callback_client_notification_readiness_dryrun_contract_status;
    contract->source_provider_callback_satisfied_mask =
        preflight->xnu_iokit_provider_callback_client_notification_readiness_dryrun_contract_satisfied_mask;
    contract->source_provider_callback_failure_mask =
        preflight->xnu_iokit_provider_callback_client_notification_readiness_dryrun_contract_failure_mask;
    contract->source_provider_callback_contract_checksum =
        preflight->xnu_iokit_provider_callback_client_notification_readiness_dryrun_contract_checksum;
    contract->source_provider_callback_checksum =
        preflight->xnu_iokit_provider_callback_client_notification_readiness_dryrun_contract.provider_callback_checksum;
    contract->source_selected_provider_callback_entry_mask =
        preflight->xnu_iokit_provider_callback_client_notification_readiness_dryrun_contract.selected_provider_callback_entry_mask;
    contract->source_rejected_provider_callback_mask =
        preflight->xnu_iokit_provider_callback_client_notification_readiness_dryrun_contract.rejected_provider_callback_mask;
    contract->source_provider_callback_matrix =
        preflight->xnu_iokit_provider_callback_client_notification_readiness_dryrun_contract.provider_callback_client_notification_matrix;
    contract->source_client_ack_mask_match_mask =
        preflight->xnu_iokit_provider_callback_client_notification_readiness_dryrun_contract.client_ack_mask_match_mask;
    contract->source_no_callback_runtime_exec =
        preflight->xnu_iokit_provider_callback_client_notification_readiness_dryrun_contract.no_callback_runtime_exec;
    contract->source_no_client_notification_runtime_exec =
        preflight->xnu_iokit_provider_callback_client_notification_readiness_dryrun_contract.no_client_notification_runtime_exec;
    contract->source_provider_notification_status =
        preflight->xnu_iokit_provider_notification_delivery_readiness_dryrun_contract_status;
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
    contract->source_loader_safety_required_mask = STAGE88_LOADER_SAFETY_REQUIRED;
    contract->boot_args_ptr = preflight->boot_args_ptr;
    contract->device_tree_ptr = preflight->device_tree_ptr;
    contract->device_tree_length = preflight->device_tree_length;
    contract->apple_dt_semantic_mask = preflight->apple_dt_semantic_mask;

    contract->platform_personality_node_ptr = (uint32_t)(uintptr_t)apple_dt_find_child(args->deviceTreeP,
                                                                                       args->deviceTreeLength,
                                                                                       root,
                                                                                       "stage88-platform-personality");
    contract->interrupt_personality_node_ptr = (uint32_t)(uintptr_t)apple_dt_find_child(args->deviceTreeP,
                                                                                        args->deviceTreeLength,
                                                                                        root,
                                                                                        "stage88-interrupt-personality");
    contract->timer_personality_node_ptr = (uint32_t)(uintptr_t)apple_dt_find_child(args->deviceTreeP,
                                                                                    args->deviceTreeLength,
                                                                                    root,
                                                                                    "stage88-timer-personality");
    contract->cpu_personality_node_ptr = (uint32_t)(uintptr_t)apple_dt_find_child(args->deviceTreeP,
                                                                                  args->deviceTreeLength,
                                                                                  root,
                                                                                  "stage88-cpu-personality");
    contract->rejected_personality_node_ptr = (uint32_t)(uintptr_t)apple_dt_find_child(args->deviceTreeP,
                                                                                       args->deviceTreeLength,
                                                                                       root,
                                                                                       "stage88-rejected-personality");

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

    selected_masks_for_node(args, platform_node, STAGE88_XNU_IOKIT_REGISTRY_SERVICE_PLATFORM_BIT,
                            "IODeviceTree:/", STAGE88_XNU_IOKIT_REGISTRY_TOPOLOGY_REJECTED_ORDINAL,
                            STAGE88_XNU_IOKIT_REGISTRY_TOPOLOGY_PLATFORM_ORDINAL, contract);
    selected_masks_for_node(args, interrupt_node, STAGE88_XNU_IOKIT_REGISTRY_SERVICE_INTERRUPT_BIT,
                            "IODeviceTree:/stage88-platform-personality",
                            STAGE88_XNU_IOKIT_REGISTRY_TOPOLOGY_PLATFORM_ORDINAL,
                            STAGE88_XNU_IOKIT_REGISTRY_TOPOLOGY_INTERRUPT_ORDINAL, contract);
    selected_masks_for_node(args, timer_node, STAGE88_XNU_IOKIT_REGISTRY_SERVICE_TIMER_BIT,
                            "IODeviceTree:/stage88-platform-personality",
                            STAGE88_XNU_IOKIT_REGISTRY_TOPOLOGY_PLATFORM_ORDINAL,
                            STAGE88_XNU_IOKIT_REGISTRY_TOPOLOGY_TIMER_ORDINAL, contract);
    selected_masks_for_node(args, cpu_node, STAGE88_XNU_IOKIT_REGISTRY_SERVICE_CPU_BIT,
                            "IODeviceTree:/stage88-platform-personality",
                            STAGE88_XNU_IOKIT_REGISTRY_TOPOLOGY_PLATFORM_ORDINAL,
                            STAGE88_XNU_IOKIT_REGISTRY_TOPOLOGY_CPU_ORDINAL, contract);

    contract->platform_open_provider_ordinal = apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength,
                                                                     platform_node, "open-provider-ordinal", 0u);
    contract->interrupt_open_provider_ordinal = apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength,
                                                                      interrupt_node, "open-provider-ordinal", 0xffffffffu);
    contract->timer_open_provider_ordinal = apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength,
                                                                  timer_node, "open-provider-ordinal", 0xffffffffu);
    contract->cpu_open_provider_ordinal = apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength,
                                                                cpu_node, "open-provider-ordinal", 0xffffffffu);
    contract->rejected_open_provider_ordinal = apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength,
                                                                     rejected_node, "open-provider-ordinal", 0u);
    contract->platform_open_type_mask = apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength,
                                                              platform_node, "open-type-mask", 0u);
    contract->interrupt_open_type_mask = apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength,
                                                               interrupt_node, "open-type-mask", 0u);
    contract->timer_open_type_mask = apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength,
                                                           timer_node, "open-type-mask", 0u);
    contract->cpu_open_type_mask = apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength,
                                                         cpu_node, "open-type-mask", 0u);
    contract->rejected_open_type_mask = apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength,
                                                              rejected_node, "open-type-mask", 0xffffffffu);
    contract->platform_expected_open_mask = apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength,
                                                                  platform_node, "expected-open-mask", 0u);
    contract->interrupt_expected_open_mask = apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength,
                                                                   interrupt_node, "expected-open-mask", 0u);
    contract->timer_expected_open_mask = apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength,
                                                               timer_node, "expected-open-mask", 0u);
    contract->cpu_expected_open_mask = apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength,
                                                             cpu_node, "expected-open-mask", 0u);
    contract->rejected_expected_open_mask = apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength,
                                                                  rejected_node, "expected-open-mask", 0xffffffffu);
    contract->platform_claimed_open_mask = apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength,
                                                                 platform_node, "claimed-open-mask", 0u);
    contract->interrupt_claimed_open_mask = apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength,
                                                                  interrupt_node, "claimed-open-mask", 0u);
    contract->timer_claimed_open_mask = apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength,
                                                              timer_node, "claimed-open-mask", 0u);
    contract->cpu_claimed_open_mask = apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength,
                                                            cpu_node, "claimed-open-mask", 0u);
    contract->rejected_claimed_open_mask = apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength,
                                                                 rejected_node, "claimed-open-mask", 0xffffffffu);
    contract->platform_client_close_mask = apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength,
                                                                 platform_node, "client-close-mask", 0u);
    contract->interrupt_client_close_mask = apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength,
                                                                  interrupt_node, "client-close-mask", 0u);
    contract->timer_client_close_mask = apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength,
                                                              timer_node, "client-close-mask", 0u);
    contract->cpu_client_close_mask = apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength,
                                                            cpu_node, "client-close-mask", 0u);
    contract->rejected_client_close_mask = apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength,
                                                                 rejected_node, "client-close-mask", 0xffffffffu);
    contract->platform_open_order = apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength,
                                                          platform_node, "open-order", 0xffffffffu);
    contract->interrupt_open_order = apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength,
                                                           interrupt_node, "open-order", 0xffffffffu);
    contract->timer_open_order = apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength,
                                                       timer_node, "open-order", 0xffffffffu);
    contract->cpu_open_order = apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength,
                                                     cpu_node, "open-order", 0xffffffffu);
    contract->rejected_open_order = apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength,
                                                          rejected_node, "open-order", 0u);
    contract->platform_close_order = apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength,
                                                           platform_node, "close-order", 0xffffffffu);
    contract->interrupt_close_order = apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength,
                                                            interrupt_node, "close-order", 0xffffffffu);
    contract->timer_close_order = apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength,
                                                        timer_node, "close-order", 0xffffffffu);
    contract->cpu_close_order = apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength,
                                                      cpu_node, "close-order", 0xffffffffu);
    contract->rejected_close_order = apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength,
                                                           rejected_node, "close-order", 0u);

    contract->selected_client_open_entry_mask = contract->client_open_ready_mask &
        contract->provider_claim_ready_mask & contract->client_close_ready_mask &
        contract->open_provider_path_match_mask & contract->open_provider_ordinal_match_mask &
        contract->open_type_match_mask & contract->expected_open_mask_match_mask &
        contract->claimed_open_mask_match_mask & contract->client_close_mask_match_mask &
        contract->open_dependency_ready_mask & contract->open_runtime_blocked_mask &
        contract->provider_claim_runtime_blocked_mask & contract->client_close_runtime_blocked_mask &
        contract->open_order_match_mask & contract->close_order_match_mask &
        contract->open_provenance_match_mask & contract->source_selected_provider_callback_entry_mask;

    if (u32_prop_equals(args, rejected_node, "client-open-dryrun", 0u) &&
        u32_prop_equals(args, rejected_node, "client-open-ready", 0u) &&
        u32_prop_equals(args, rejected_node, "provider-claim-ready", 0u) &&
        u32_prop_equals(args, rejected_node, "client-close-ready", 0u) &&
        u32_prop_equals(args, rejected_node, "open-runtime-exec", 0u) &&
        u32_prop_equals(args, rejected_node, "claim-runtime-exec", 0u) &&
        u32_prop_equals(args, rejected_node, "close-runtime-exec", 0u) &&
        string_prop_equals(args, rejected_node, "open-provider-path", "IODeviceTree:/unlinked") &&
        u32_prop_equals(args, rejected_node, "open-provider-ordinal",
                        STAGE88_XNU_IOKIT_REGISTRY_TOPOLOGY_REJECTED_ORDINAL) &&
        u32_prop_equals(args, rejected_node, "open-type-mask", 0u) &&
        u32_prop_equals(args, rejected_node, "expected-open-mask", 0u) &&
        u32_prop_equals(args, rejected_node, "claimed-open-mask", 0u) &&
        u32_prop_equals(args, rejected_node, "client-close-mask", 0u) &&
        u32_prop_equals(args, rejected_node, "open-order", STAGE88_XNU_IOKIT_REGISTRY_TOPOLOGY_REJECTED_ORDINAL) &&
        u32_prop_equals(args, rejected_node, "close-order", STAGE88_XNU_IOKIT_REGISTRY_TOPOLOGY_REJECTED_ORDINAL) &&
        u32_prop_equals(args, rejected_node, "open-dependency-ready", 0u) &&
        string_prop_equals(args, rejected_node, "open-provenance", "stage88-rejected-noopen") &&
        u32_prop_equals(args, rejected_node, "rejected-candidate", 1u) &&
        u32_prop_equals(args, rejected_node, "stage-owned-local-only", 1u)) {
        contract->rejected_client_open_mask |= STAGE88_XNU_IOKIT_REGISTRY_SERVICE_REJECTED_BIT;
    }

    contract->client_open_provider_claim_close_matrix = contract->selected_client_open_entry_mask |
        contract->rejected_client_open_mask;
    contract->open_order_checksum = contract->platform_open_order ^ contract->interrupt_open_order ^
        contract->timer_open_order ^ contract->cpu_open_order ^ contract->rejected_open_order;
    contract->close_order_checksum = contract->platform_close_order ^ contract->interrupt_close_order ^
        contract->timer_close_order ^ contract->cpu_close_order ^ contract->rejected_close_order;
    contract->client_open_mask_checksum = contract->platform_expected_open_mask ^
        contract->interrupt_expected_open_mask ^ contract->timer_expected_open_mask ^
        contract->cpu_expected_open_mask ^ contract->rejected_expected_open_mask ^
        contract->platform_claimed_open_mask ^ contract->interrupt_claimed_open_mask ^
        contract->timer_claimed_open_mask ^ contract->cpu_claimed_open_mask ^
        contract->rejected_claimed_open_mask ^ contract->platform_client_close_mask ^
        contract->interrupt_client_close_mask ^ contract->timer_client_close_mask ^
        contract->cpu_client_close_mask ^ contract->rejected_client_close_mask;
    contract->client_open_path_hash = STAGE88_XNU_IOKIT_CLIENT_OPEN_PROVIDER_CLAIM_CLOSE_PATH_HASH;
    contract->client_open_candidate_count = STAGE88_XNU_IOKIT_CLIENT_OPEN_PROVIDER_CLAIM_CLOSE_CANDIDATE_COUNT;
    contract->selected_client_open_entry_count =
        (contract->selected_client_open_entry_mask == STAGE88_XNU_IOKIT_REGISTRY_SERVICE_SELECTED_REQUIRED) ?
        STAGE88_XNU_IOKIT_CLIENT_OPEN_PROVIDER_CLAIM_CLOSE_ENTRY_COUNT : 0u;
    contract->rejected_client_open_entry_count =
        (contract->rejected_client_open_mask == STAGE88_XNU_IOKIT_REGISTRY_SERVICE_REJECTED_REQUIRED) ?
        STAGE88_XNU_IOKIT_CLIENT_OPEN_PROVIDER_CLAIM_CLOSE_REJECTED_COUNT : 0u;
    contract->client_open_ready_count = contract->selected_client_open_entry_count;
    contract->provider_claim_ready_count = contract->selected_client_open_entry_count;
    contract->client_close_ready_count = contract->selected_client_open_entry_count;
    contract->rejected_no_open_count = contract->rejected_client_open_entry_count;
    contract->no_open_runtime_exec = 1u;
    contract->no_claim_runtime_exec = 1u;
    contract->no_close_runtime_exec = 1u;
    contract->client_open_checksum = contract->source_provider_callback_checksum ^
        contract->source_selected_provider_callback_entry_mask ^ contract->source_rejected_provider_callback_mask ^
        contract->source_provider_callback_matrix ^ contract->source_client_ack_mask_match_mask ^
        contract->source_no_callback_runtime_exec ^ contract->source_no_client_notification_runtime_exec ^
        contract->client_open_ready_mask ^ contract->provider_claim_ready_mask ^ contract->client_close_ready_mask ^
        contract->open_provider_path_match_mask ^ contract->open_provider_ordinal_match_mask ^
        contract->open_type_match_mask ^ contract->expected_open_mask_match_mask ^
        contract->claimed_open_mask_match_mask ^ contract->client_close_mask_match_mask ^
        contract->open_dependency_ready_mask ^ contract->open_runtime_blocked_mask ^
        contract->provider_claim_runtime_blocked_mask ^ contract->client_close_runtime_blocked_mask ^
        contract->open_order_match_mask ^ contract->close_order_match_mask ^ contract->open_provenance_match_mask ^
        contract->selected_client_open_entry_mask ^ contract->rejected_client_open_mask ^
        contract->client_open_provider_claim_close_matrix ^ contract->open_order_checksum ^
        contract->close_order_checksum ^ contract->client_open_mask_checksum ^ contract->client_open_path_hash ^
        contract->client_open_candidate_count ^ contract->selected_client_open_entry_count ^
        contract->rejected_client_open_entry_count ^ contract->client_open_ready_count ^
        contract->provider_claim_ready_count ^ contract->client_close_ready_count ^
        contract->rejected_no_open_count ^ contract->no_open_runtime_exec ^
        contract->no_claim_runtime_exec ^ contract->no_close_runtime_exec;
    contract->expected_client_open_checksum =
        STAGE88_XNU_IOKIT_CLIENT_OPEN_PROVIDER_CLAIM_CLOSE_EXPECTED_CHECKSUM;

    contract->iokit_reference_mask =
        preflight->xnu_iokit_provider_callback_client_notification_readiness_dryrun_contract.iokit_reference_mask;
    contract->iokit_runtime_blocked_mask =
        preflight->xnu_iokit_provider_callback_client_notification_readiness_dryrun_contract.iokit_runtime_blocked_mask;
    contract->iokit_reference_count =
        preflight->xnu_iokit_provider_callback_client_notification_readiness_dryrun_contract.iokit_reference_count;
    contract->iokit_public_compile_count =
        preflight->xnu_iokit_provider_callback_client_notification_readiness_dryrun_contract.iokit_public_compile_count;
    contract->iokit_public_link_count =
        preflight->xnu_iokit_provider_callback_client_notification_readiness_dryrun_contract.iokit_public_link_count;
    contract->iokit_reference_only =
        preflight->xnu_iokit_provider_callback_client_notification_readiness_dryrun_contract.iokit_reference_only;
    contract->no_iokit_runtime_exec =
        preflight->xnu_iokit_provider_callback_client_notification_readiness_dryrun_contract.no_iokit_runtime_exec;
    contract->no_catalog_runtime_exec =
        preflight->xnu_iokit_provider_callback_client_notification_readiness_dryrun_contract.no_catalog_runtime_exec;
    contract->no_provider_runtime_exec =
        preflight->xnu_iokit_provider_callback_client_notification_readiness_dryrun_contract.no_provider_runtime_exec;
    contract->no_registry_entry_runtime_exec =
        preflight->xnu_iokit_provider_callback_client_notification_readiness_dryrun_contract.no_registry_entry_runtime_exec;
    contract->no_registry_topology_runtime_exec =
        preflight->xnu_iokit_provider_callback_client_notification_readiness_dryrun_contract.no_registry_topology_runtime_exec;
    contract->no_property_runtime_exec =
        preflight->xnu_iokit_provider_callback_client_notification_readiness_dryrun_contract.no_property_runtime_exec;
    contract->no_attach_runtime_exec =
        preflight->xnu_iokit_provider_callback_client_notification_readiness_dryrun_contract.no_attach_runtime_exec;
    contract->no_start_runtime_exec =
        preflight->xnu_iokit_provider_callback_client_notification_readiness_dryrun_contract.no_start_runtime_exec;
    contract->no_register_service_runtime_exec =
        preflight->xnu_iokit_provider_callback_client_notification_readiness_dryrun_contract.no_register_service_runtime_exec;
    contract->no_notification_runtime_exec =
        preflight->xnu_iokit_provider_callback_client_notification_readiness_dryrun_contract.no_notification_runtime_exec;
    contract->no_interest_runtime_exec =
        preflight->xnu_iokit_provider_callback_client_notification_readiness_dryrun_contract.no_interest_runtime_exec;
    contract->no_delivery_runtime_exec =
        preflight->xnu_iokit_provider_callback_client_notification_readiness_dryrun_contract.no_delivery_runtime_exec;
    contract->no_callback_runtime_exec =
        preflight->xnu_iokit_provider_callback_client_notification_readiness_dryrun_contract.no_callback_runtime_exec;
    contract->no_client_notification_runtime_exec =
        preflight->xnu_iokit_provider_callback_client_notification_readiness_dryrun_contract.no_client_notification_runtime_exec;
    contract->no_platform_driver_exec =
        preflight->xnu_iokit_provider_callback_client_notification_readiness_dryrun_contract.no_platform_driver_exec;
    contract->no_public_xnu_exec =
        preflight->xnu_iokit_provider_callback_client_notification_readiness_dryrun_contract.no_public_xnu_exec;
    contract->no_platform_runtime_exec =
        preflight->xnu_iokit_provider_callback_client_notification_readiness_dryrun_contract.no_platform_runtime_exec;
    contract->no_public_pmap_exec =
        preflight->xnu_iokit_provider_callback_client_notification_readiness_dryrun_contract.no_public_pmap_exec;
    contract->no_live_pmap_tables_installed =
        preflight->xnu_iokit_provider_callback_client_notification_readiness_dryrun_contract.no_live_pmap_tables_installed;
    contract->proposed_workspace_written =
        preflight->xnu_iokit_provider_callback_client_notification_readiness_dryrun_contract.proposed_workspace_written;
    contract->pmap_ttbr_written = preflight->xnu_iokit_provider_callback_client_notification_readiness_dryrun_contract.pmap_ttbr_written;
    contract->pmap_ttbcr_written = preflight->xnu_iokit_provider_callback_client_notification_readiness_dryrun_contract.pmap_ttbcr_written;
    contract->pmap_dacr_written = preflight->xnu_iokit_provider_callback_client_notification_readiness_dryrun_contract.pmap_dacr_written;
    contract->pmap_sctlr_written = preflight->xnu_iokit_provider_callback_client_notification_readiness_dryrun_contract.pmap_sctlr_written;
    contract->pmap_tlbs_invalidated =
        preflight->xnu_iokit_provider_callback_client_notification_readiness_dryrun_contract.pmap_tlbs_invalidated;
    contract->caches_changed = preflight->xnu_iokit_provider_callback_client_notification_readiness_dryrun_contract.caches_changed;
    contract->persistent_write_attempted =
        preflight->xnu_iokit_provider_callback_client_notification_readiness_dryrun_contract.persistent_write_attempted;
    contract->xnu_start_executed =
        preflight->xnu_iokit_provider_callback_client_notification_readiness_dryrun_contract.xnu_start_executed;
    contract->generated_macho_executed =
        preflight->xnu_iokit_provider_callback_client_notification_readiness_dryrun_contract.generated_macho_executed;
    contract->proposed_platform_gap_mask = preflight->platform_gap_mask;
    contract->proposed_pexpert_gap_mask = preflight->pexpert_gap_mask;
    contract->local_only = 1u;
    contract->fail_closed = 1u;

    source_ok = (contract->source_provider_callback_status == STAGE88_STATUS_OK &&
                 contract->source_provider_callback_satisfied_mask ==
                 STAGE88_XNU_IOKIT_PROVIDER_CALLBACK_CLIENT_NOTIFICATION_DRYRUN_REQUIRED_MASK &&
                 contract->source_provider_callback_failure_mask == 0u &&
                 contract->source_provider_callback_checksum ==
                 STAGE88_XNU_IOKIT_PROVIDER_CALLBACK_CLIENT_NOTIFICATION_EXPECTED_CHECKSUM &&
                 contract->source_selected_provider_callback_entry_mask == STAGE88_XNU_IOKIT_REGISTRY_SERVICE_SELECTED_REQUIRED &&
                 contract->source_rejected_provider_callback_mask == STAGE88_XNU_IOKIT_REGISTRY_SERVICE_REJECTED_REQUIRED &&
                 contract->source_provider_callback_matrix == (STAGE88_XNU_IOKIT_REGISTRY_SERVICE_SELECTED_REQUIRED |
                                                               STAGE88_XNU_IOKIT_REGISTRY_SERVICE_REJECTED_REQUIRED) &&
                 contract->source_client_ack_mask_match_mask == STAGE88_XNU_IOKIT_REGISTRY_SERVICE_SELECTED_REQUIRED &&
                 contract->source_no_callback_runtime_exec == 1u &&
                 contract->source_no_client_notification_runtime_exec == 1u &&
                 contract->source_provider_notification_status == STAGE88_STATUS_OK &&
                 contract->source_lifecycle_status == STAGE88_STATUS_OK &&
                 contract->source_attach_start_status == STAGE88_STATUS_OK &&
                 contract->source_topology_status == STAGE88_STATUS_OK &&
                 contract->source_property_inheritance_status == STAGE88_STATUS_OK &&
                 contract->source_catalog_status == STAGE88_STATUS_OK &&
                 contract->source_provider_status == STAGE88_STATUS_OK &&
                 contract->source_registry_status == STAGE88_STATUS_OK &&
                 contract->source_match_status == STAGE88_STATUS_OK &&
                 contract->source_scaffold_status == STAGE88_STATUS_OK &&
                 contract->source_pexpert_status == STAGE88_STATUS_OK &&
                 contract->source_pmap_transition_status == STAGE88_STATUS_OK &&
                 contract->source_compile_graph_status == STAGE88_STATUS_OK &&
                 contract->source_object_subset_status == STAGE88_STATUS_OK &&
                 contract->source_link_status == STAGE88_STATUS_OK &&
                 contract->source_loader_safety_mask == contract->source_loader_safety_required_mask) ? 1u : 0u;
    if (source_ok) {
        contract->satisfied_mask |= STAGE88_XNU_IOKIT_CLIENT_OPEN_PROVIDER_CLAIM_CLOSE_DRYRUN_SAT_SOURCE_PROVIDER_CALLBACK;
    } else {
        contract->failure_mask |= STAGE88_XNU_IOKIT_CLIENT_OPEN_PROVIDER_CLAIM_CLOSE_DRYRUN_FAIL_SOURCE;
    }

    if (contract->apple_dt_semantic_mask == STAGE88_DT_READY_REQUIRED) {
        contract->satisfied_mask |= STAGE88_XNU_IOKIT_CLIENT_OPEN_PROVIDER_CLAIM_CLOSE_DRYRUN_SAT_APPLE_DT;
    } else {
        contract->failure_mask |= STAGE88_XNU_IOKIT_CLIENT_OPEN_PROVIDER_CLAIM_CLOSE_DRYRUN_FAIL_DT;
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
        contract->satisfied_mask |= STAGE88_XNU_IOKIT_CLIENT_OPEN_PROVIDER_CLAIM_CLOSE_DRYRUN_SAT_PERSONALITY_NODE_SET;
    } else {
        contract->failure_mask |= STAGE88_XNU_IOKIT_CLIENT_OPEN_PROVIDER_CLAIM_CLOSE_DRYRUN_FAIL_PERSONALITY;
    }

    if (contract->client_open_ready_mask == STAGE88_XNU_IOKIT_REGISTRY_SERVICE_SELECTED_REQUIRED) {
        contract->satisfied_mask |= STAGE88_XNU_IOKIT_CLIENT_OPEN_PROVIDER_CLAIM_CLOSE_DRYRUN_SAT_CLIENT_OPEN_READY;
    } else {
        contract->failure_mask |= STAGE88_XNU_IOKIT_CLIENT_OPEN_PROVIDER_CLAIM_CLOSE_DRYRUN_FAIL_OPEN;
    }
    if (contract->provider_claim_ready_mask == STAGE88_XNU_IOKIT_REGISTRY_SERVICE_SELECTED_REQUIRED) {
        contract->satisfied_mask |= STAGE88_XNU_IOKIT_CLIENT_OPEN_PROVIDER_CLAIM_CLOSE_DRYRUN_SAT_PROVIDER_CLAIM_READY;
    } else {
        contract->failure_mask |= STAGE88_XNU_IOKIT_CLIENT_OPEN_PROVIDER_CLAIM_CLOSE_DRYRUN_FAIL_CLAIM;
    }
    if (contract->client_close_ready_mask == STAGE88_XNU_IOKIT_REGISTRY_SERVICE_SELECTED_REQUIRED) {
        contract->satisfied_mask |= STAGE88_XNU_IOKIT_CLIENT_OPEN_PROVIDER_CLAIM_CLOSE_DRYRUN_SAT_CLIENT_CLOSE_READY;
    } else {
        contract->failure_mask |= STAGE88_XNU_IOKIT_CLIENT_OPEN_PROVIDER_CLAIM_CLOSE_DRYRUN_FAIL_CLOSE;
    }
    if (contract->open_provider_path_match_mask == STAGE88_XNU_IOKIT_REGISTRY_SERVICE_SELECTED_REQUIRED) {
        contract->satisfied_mask |= STAGE88_XNU_IOKIT_CLIENT_OPEN_PROVIDER_CLAIM_CLOSE_DRYRUN_SAT_PROVIDER_PATH_MATRIX;
    } else {
        contract->failure_mask |= STAGE88_XNU_IOKIT_CLIENT_OPEN_PROVIDER_CLAIM_CLOSE_DRYRUN_FAIL_OPEN;
    }
    if (contract->open_provider_ordinal_match_mask == STAGE88_XNU_IOKIT_REGISTRY_SERVICE_SELECTED_REQUIRED) {
        contract->satisfied_mask |= STAGE88_XNU_IOKIT_CLIENT_OPEN_PROVIDER_CLAIM_CLOSE_DRYRUN_SAT_PROVIDER_ORDINAL_MATRIX;
    } else {
        contract->failure_mask |= STAGE88_XNU_IOKIT_CLIENT_OPEN_PROVIDER_CLAIM_CLOSE_DRYRUN_FAIL_OPEN;
    }
    if (contract->open_type_match_mask == STAGE88_XNU_IOKIT_REGISTRY_SERVICE_SELECTED_REQUIRED) {
        contract->satisfied_mask |= STAGE88_XNU_IOKIT_CLIENT_OPEN_PROVIDER_CLAIM_CLOSE_DRYRUN_SAT_OPEN_TYPE_MATRIX;
    } else {
        contract->failure_mask |= STAGE88_XNU_IOKIT_CLIENT_OPEN_PROVIDER_CLAIM_CLOSE_DRYRUN_FAIL_OPEN;
    }
    if (contract->expected_open_mask_match_mask == STAGE88_XNU_IOKIT_REGISTRY_SERVICE_SELECTED_REQUIRED) {
        contract->satisfied_mask |= STAGE88_XNU_IOKIT_CLIENT_OPEN_PROVIDER_CLAIM_CLOSE_DRYRUN_SAT_EXPECTED_OPEN_MATRIX;
    } else {
        contract->failure_mask |= STAGE88_XNU_IOKIT_CLIENT_OPEN_PROVIDER_CLAIM_CLOSE_DRYRUN_FAIL_OPEN;
    }
    if (contract->claimed_open_mask_match_mask == STAGE88_XNU_IOKIT_REGISTRY_SERVICE_SELECTED_REQUIRED) {
        contract->satisfied_mask |= STAGE88_XNU_IOKIT_CLIENT_OPEN_PROVIDER_CLAIM_CLOSE_DRYRUN_SAT_CLAIMED_OPEN_MATRIX;
    } else {
        contract->failure_mask |= STAGE88_XNU_IOKIT_CLIENT_OPEN_PROVIDER_CLAIM_CLOSE_DRYRUN_FAIL_CLAIM;
    }
    if (contract->client_close_mask_match_mask == STAGE88_XNU_IOKIT_REGISTRY_SERVICE_SELECTED_REQUIRED) {
        contract->satisfied_mask |= STAGE88_XNU_IOKIT_CLIENT_OPEN_PROVIDER_CLAIM_CLOSE_DRYRUN_SAT_CLIENT_CLOSE_MATRIX;
    } else {
        contract->failure_mask |= STAGE88_XNU_IOKIT_CLIENT_OPEN_PROVIDER_CLAIM_CLOSE_DRYRUN_FAIL_CLOSE;
    }

    order_ok = (contract->open_order_match_mask == STAGE88_XNU_IOKIT_REGISTRY_SERVICE_SELECTED_REQUIRED &&
                contract->close_order_match_mask == STAGE88_XNU_IOKIT_REGISTRY_SERVICE_SELECTED_REQUIRED) ? 1u : 0u;
    if (order_ok) {
        contract->satisfied_mask |= STAGE88_XNU_IOKIT_CLIENT_OPEN_PROVIDER_CLAIM_CLOSE_DRYRUN_SAT_ORDER_MATRIX;
    } else {
        contract->failure_mask |= STAGE88_XNU_IOKIT_CLIENT_OPEN_PROVIDER_CLAIM_CLOSE_DRYRUN_FAIL_ORDER_DEPENDENCY;
    }
    open_dependency_ok = (contract->open_dependency_ready_mask == STAGE88_XNU_IOKIT_REGISTRY_SERVICE_SELECTED_REQUIRED);
    if (open_dependency_ok) {
        contract->satisfied_mask |= STAGE88_XNU_IOKIT_CLIENT_OPEN_PROVIDER_CLAIM_CLOSE_DRYRUN_SAT_DEPENDENCY_MATRIX;
    } else {
        contract->failure_mask |= STAGE88_XNU_IOKIT_CLIENT_OPEN_PROVIDER_CLAIM_CLOSE_DRYRUN_FAIL_ORDER_DEPENDENCY;
    }
    if (contract->open_runtime_blocked_mask == STAGE88_XNU_IOKIT_REGISTRY_SERVICE_SELECTED_REQUIRED &&
        contract->provider_claim_runtime_blocked_mask == STAGE88_XNU_IOKIT_REGISTRY_SERVICE_SELECTED_REQUIRED &&
        contract->client_close_runtime_blocked_mask == STAGE88_XNU_IOKIT_REGISTRY_SERVICE_SELECTED_REQUIRED) {
        contract->satisfied_mask |= STAGE88_XNU_IOKIT_CLIENT_OPEN_PROVIDER_CLAIM_CLOSE_DRYRUN_SAT_RUNTIME_BLOCKED;
    } else {
        contract->failure_mask |= STAGE88_XNU_IOKIT_CLIENT_OPEN_PROVIDER_CLAIM_CLOSE_DRYRUN_FAIL_SAFETY_BOUNDARY;
    }
    if (contract->open_provenance_match_mask == STAGE88_XNU_IOKIT_REGISTRY_SERVICE_SELECTED_REQUIRED) {
        contract->satisfied_mask |= STAGE88_XNU_IOKIT_CLIENT_OPEN_PROVIDER_CLAIM_CLOSE_DRYRUN_SAT_PROVENANCE;
    } else {
        contract->failure_mask |= STAGE88_XNU_IOKIT_CLIENT_OPEN_PROVIDER_CLAIM_CLOSE_DRYRUN_FAIL_OPEN;
    }
    if (contract->selected_client_open_entry_mask == STAGE88_XNU_IOKIT_REGISTRY_SERVICE_SELECTED_REQUIRED) {
        contract->satisfied_mask |= STAGE88_XNU_IOKIT_CLIENT_OPEN_PROVIDER_CLAIM_CLOSE_DRYRUN_SAT_SELECTED_OPEN_MATRIX;
    } else {
        contract->failure_mask |= STAGE88_XNU_IOKIT_CLIENT_OPEN_PROVIDER_CLAIM_CLOSE_DRYRUN_FAIL_OPEN;
    }
    if (contract->rejected_client_open_mask == STAGE88_XNU_IOKIT_REGISTRY_SERVICE_REJECTED_REQUIRED) {
        contract->satisfied_mask |= STAGE88_XNU_IOKIT_CLIENT_OPEN_PROVIDER_CLAIM_CLOSE_DRYRUN_SAT_REJECT_NO_OPEN;
    } else {
        contract->failure_mask |= STAGE88_XNU_IOKIT_CLIENT_OPEN_PROVIDER_CLAIM_CLOSE_DRYRUN_FAIL_REJECTED;
    }
    if (contract->client_open_candidate_count == STAGE88_XNU_IOKIT_CLIENT_OPEN_PROVIDER_CLAIM_CLOSE_CANDIDATE_COUNT &&
        contract->selected_client_open_entry_count == STAGE88_XNU_IOKIT_CLIENT_OPEN_PROVIDER_CLAIM_CLOSE_ENTRY_COUNT &&
        contract->rejected_client_open_entry_count == STAGE88_XNU_IOKIT_CLIENT_OPEN_PROVIDER_CLAIM_CLOSE_REJECTED_COUNT &&
        contract->client_open_ready_count == STAGE88_XNU_IOKIT_CLIENT_OPEN_PROVIDER_CLAIM_CLOSE_ENTRY_COUNT &&
        contract->provider_claim_ready_count == STAGE88_XNU_IOKIT_CLIENT_OPEN_PROVIDER_CLAIM_CLOSE_ENTRY_COUNT &&
        contract->client_close_ready_count == STAGE88_XNU_IOKIT_CLIENT_OPEN_PROVIDER_CLAIM_CLOSE_ENTRY_COUNT &&
        contract->rejected_no_open_count == STAGE88_XNU_IOKIT_CLIENT_OPEN_PROVIDER_CLAIM_CLOSE_REJECTED_COUNT) {
        contract->satisfied_mask |= STAGE88_XNU_IOKIT_CLIENT_OPEN_PROVIDER_CLAIM_CLOSE_DRYRUN_SAT_DRYRUN_COUNTS;
    } else {
        contract->failure_mask |= STAGE88_XNU_IOKIT_CLIENT_OPEN_PROVIDER_CLAIM_CLOSE_DRYRUN_FAIL_COUNTS;
    }
    if (contract->client_open_checksum == contract->expected_client_open_checksum) {
        contract->satisfied_mask |= STAGE88_XNU_IOKIT_CLIENT_OPEN_PROVIDER_CLAIM_CLOSE_DRYRUN_SAT_OPEN_CHECKSUM;
    } else {
        contract->failure_mask |= STAGE88_XNU_IOKIT_CLIENT_OPEN_PROVIDER_CLAIM_CLOSE_DRYRUN_FAIL_CHECKSUM;
    }

    if (contract->iokit_reference_mask == STAGE88_XNU_IOKIT_REFERENCE_REQUIRED &&
        contract->iokit_runtime_blocked_mask == STAGE88_XNU_IOKIT_REFERENCE_REQUIRED &&
        contract->iokit_reference_only == 1u && contract->iokit_public_compile_count == 0u &&
        contract->iokit_public_link_count == 0u) {
        contract->satisfied_mask |= STAGE88_XNU_IOKIT_CLIENT_OPEN_PROVIDER_CLAIM_CLOSE_DRYRUN_SAT_IOKIT_REFERENCE;
    } else {
        contract->failure_mask |= STAGE88_XNU_IOKIT_CLIENT_OPEN_PROVIDER_CLAIM_CLOSE_DRYRUN_FAIL_PUBLIC_BOUNDARY;
    }
    public_boundary_ok = (contract->no_public_xnu_exec == 1u && contract->no_platform_runtime_exec == 1u &&
                          contract->xnu_start_executed == 0u && contract->generated_macho_executed == 0u &&
                          contract->no_platform_driver_exec == 1u);
    if (public_boundary_ok) {
        contract->satisfied_mask |= STAGE88_XNU_IOKIT_CLIENT_OPEN_PROVIDER_CLAIM_CLOSE_DRYRUN_SAT_PUBLIC_BOUNDARY;
        contract->satisfied_mask |= STAGE88_XNU_IOKIT_CLIENT_OPEN_PROVIDER_CLAIM_CLOSE_DRYRUN_SAT_NO_PUBLIC_XNU_EXEC;
    } else {
        contract->failure_mask |= STAGE88_XNU_IOKIT_CLIENT_OPEN_PROVIDER_CLAIM_CLOSE_DRYRUN_FAIL_PUBLIC_BOUNDARY;
    }
    pmap_boundary_ok = (contract->no_public_pmap_exec == 1u && contract->no_live_pmap_tables_installed == 0u &&
                        contract->proposed_workspace_written == 0u && contract->pmap_ttbr_written == 0u &&
                        contract->pmap_ttbcr_written == 0u && contract->pmap_dacr_written == 0u &&
                        contract->pmap_sctlr_written == 0u && contract->pmap_tlbs_invalidated == 0u &&
                        contract->caches_changed == 0u && contract->source_pmap_transition_status == STAGE88_STATUS_OK);
    if (pmap_boundary_ok) {
        contract->satisfied_mask |= STAGE88_XNU_IOKIT_CLIENT_OPEN_PROVIDER_CLAIM_CLOSE_DRYRUN_SAT_PMAP_BOUNDARY;
        contract->satisfied_mask |= STAGE88_XNU_IOKIT_CLIENT_OPEN_PROVIDER_CLAIM_CLOSE_DRYRUN_SAT_NO_PUBLIC_PMAP_EXEC;
        contract->satisfied_mask |= STAGE88_XNU_IOKIT_CLIENT_OPEN_PROVIDER_CLAIM_CLOSE_DRYRUN_SAT_NO_LIVE_PMAP_INSTALL;
    } else {
        contract->failure_mask |= STAGE88_XNU_IOKIT_CLIENT_OPEN_PROVIDER_CLAIM_CLOSE_DRYRUN_FAIL_PMAP_BOUNDARY;
    }
    if (contract->no_iokit_runtime_exec == 1u && contract->no_callback_runtime_exec == 1u &&
        contract->no_client_notification_runtime_exec == 1u) {
        contract->satisfied_mask |= STAGE88_XNU_IOKIT_CLIENT_OPEN_PROVIDER_CLAIM_CLOSE_DRYRUN_SAT_NO_IOKIT_RUNTIME_EXEC;
    } else {
        contract->failure_mask |= STAGE88_XNU_IOKIT_CLIENT_OPEN_PROVIDER_CLAIM_CLOSE_DRYRUN_FAIL_SAFETY_BOUNDARY;
    }
    if (contract->no_open_runtime_exec == 1u) {
        contract->satisfied_mask |= STAGE88_XNU_IOKIT_CLIENT_OPEN_PROVIDER_CLAIM_CLOSE_DRYRUN_SAT_NO_OPEN_RUNTIME_EXEC;
    } else {
        contract->failure_mask |= STAGE88_XNU_IOKIT_CLIENT_OPEN_PROVIDER_CLAIM_CLOSE_DRYRUN_FAIL_SAFETY_BOUNDARY;
    }
    if (contract->no_claim_runtime_exec == 1u) {
        contract->satisfied_mask |= STAGE88_XNU_IOKIT_CLIENT_OPEN_PROVIDER_CLAIM_CLOSE_DRYRUN_SAT_NO_CLAIM_RUNTIME_EXEC;
    } else {
        contract->failure_mask |= STAGE88_XNU_IOKIT_CLIENT_OPEN_PROVIDER_CLAIM_CLOSE_DRYRUN_FAIL_SAFETY_BOUNDARY;
    }
    if (contract->no_close_runtime_exec == 1u) {
        contract->satisfied_mask |= STAGE88_XNU_IOKIT_CLIENT_OPEN_PROVIDER_CLAIM_CLOSE_DRYRUN_SAT_NO_CLOSE_RUNTIME_EXEC;
    } else {
        contract->failure_mask |= STAGE88_XNU_IOKIT_CLIENT_OPEN_PROVIDER_CLAIM_CLOSE_DRYRUN_FAIL_SAFETY_BOUNDARY;
    }
    if (contract->persistent_write_attempted == 0u && contract->local_only == 1u && contract->fail_closed == 1u) {
        contract->satisfied_mask |= STAGE88_XNU_IOKIT_CLIENT_OPEN_PROVIDER_CLAIM_CLOSE_DRYRUN_SAT_LOCAL_ONLY_FAIL_CLOSED;
    } else {
        contract->failure_mask |= STAGE88_XNU_IOKIT_CLIENT_OPEN_PROVIDER_CLAIM_CLOSE_DRYRUN_FAIL_SAFETY_BOUNDARY;
    }

    contract->checksum = stage88_iokit_client_open_provider_claim_close_dryrun_checksum(contract);
    if (contract->checksum != stage88_iokit_client_open_provider_claim_close_dryrun_checksum(contract)) {
        contract->failure_mask |= STAGE88_XNU_IOKIT_CLIENT_OPEN_PROVIDER_CLAIM_CLOSE_DRYRUN_FAIL_CHECKSUM;
    }

    contract->status = (contract->satisfied_mask == contract->required_mask && contract->failure_mask == 0u) ?
        STAGE88_STATUS_OK : (STAGE88_STATUS_FAIL(contract->failure_mask));
    if (contract->status != STAGE88_STATUS_OK) {
        contract->failure_mask |= STAGE88_XNU_IOKIT_CLIENT_OPEN_PROVIDER_CLAIM_CLOSE_DRYRUN_FAIL_ROLLUP;
        contract->status = STAGE88_STATUS_FAIL(contract->failure_mask);
        contract->checksum = stage88_iokit_client_open_provider_claim_close_dryrun_checksum(contract);
    }

    stage88_iokit_client_open_provider_claim_close_dryrun_log(contract);
    return contract->status == STAGE88_STATUS_OK;
}

const struct stage88_xnu_iokit_client_open_provider_claim_close_readiness_dryrun_contract *
stage88_xnu_iokit_client_open_provider_claim_close_readiness_dryrun_contract_result(void)
{
    return &g_stage88_xnu_iokit_client_open_provider_claim_close_readiness_dryrun_contract;
}
