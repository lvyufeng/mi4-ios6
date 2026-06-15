#include "stage85.h"

static struct stage85_xnu_iokit_provider_notification_delivery_readiness_dryrun_contract
    g_stage85_xnu_iokit_provider_notification_delivery_readiness_dryrun_contract;

static uint32_t stage85_iokit_provider_notification_delivery_dryrun_checksum(
    volatile const struct stage85_xnu_iokit_provider_notification_delivery_readiness_dryrun_contract *contract)
{
    volatile const uint32_t *words = (volatile const uint32_t *)(uintptr_t)contract;
    uint32_t count = (uint32_t)(offsetof(struct stage85_xnu_iokit_provider_notification_delivery_readiness_dryrun_contract,
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

static void stage85_iokit_provider_notification_delivery_dryrun_log(
    const struct stage85_xnu_iokit_provider_notification_delivery_readiness_dryrun_contract *contract)
{
    xnu_log_kv32("stage85_xnu_iokit_provider_notification_delivery_readiness_dryrun_contract_status", contract->status);
    xnu_log_kv32("stage85_xnu_iokit_provider_notification_delivery_readiness_dryrun_contract_required_mask", contract->required_mask);
    xnu_log_kv32("stage85_xnu_iokit_provider_notification_delivery_readiness_dryrun_contract_satisfied_mask", contract->satisfied_mask);
    xnu_log_kv32("stage85_xnu_iokit_provider_notification_delivery_readiness_dryrun_contract_failure_mask", contract->failure_mask);
    xnu_log_kv32("stage85_xnu_iokit_provider_notification_delivery_readiness_dryrun_contract_checksum", contract->checksum);
    xnu_log_kv32("stage85_xnu_iokit_provider_notification_source_lifecycle_status", contract->source_lifecycle_status);
    xnu_log_kv32("stage85_xnu_iokit_provider_notification_source_lifecycle_satisfied_mask", contract->source_lifecycle_satisfied_mask);
    xnu_log_kv32("stage85_xnu_iokit_provider_notification_source_lifecycle_failure_mask", contract->source_lifecycle_failure_mask);
    xnu_log_kv32("stage85_xnu_iokit_provider_notification_source_lifecycle_checksum", contract->source_lifecycle_checksum);
    xnu_log_kv32("stage85_xnu_iokit_provider_notification_source_selected_lifecycle_entry_mask", contract->source_selected_lifecycle_entry_mask);
    xnu_log_kv32("stage85_xnu_iokit_provider_notification_source_rejected_lifecycle_mask", contract->source_rejected_lifecycle_mask);
    xnu_log_kv32("stage85_xnu_iokit_provider_notification_source_lifecycle_matrix", contract->source_lifecycle_matrix);
    xnu_log_kv32("stage85_xnu_iokit_provider_notification_apple_dt_semantic_mask", contract->apple_dt_semantic_mask);
    xnu_log_kv32("stage85_xnu_iokit_provider_notification_platform_props", contract->platform_personality_prop_count);
    xnu_log_kv32("stage85_xnu_iokit_provider_notification_interrupt_props", contract->interrupt_personality_prop_count);
    xnu_log_kv32("stage85_xnu_iokit_provider_notification_timer_props", contract->timer_personality_prop_count);
    xnu_log_kv32("stage85_xnu_iokit_provider_notification_cpu_props", contract->cpu_personality_prop_count);
    xnu_log_kv32("stage85_xnu_iokit_provider_notification_rejected_props", contract->rejected_personality_prop_count);
    xnu_log_kv32("stage85_xnu_iokit_provider_notification_ready_mask", contract->provider_notification_ready_mask);
    xnu_log_kv32("stage85_xnu_iokit_provider_notification_interest_ready_mask", contract->interest_ready_mask);
    xnu_log_kv32("stage85_xnu_iokit_provider_notification_delivery_ready_mask", contract->delivery_ready_mask);
    xnu_log_kv32("stage85_xnu_iokit_provider_notification_interest_provider_path_match_mask", contract->interest_provider_path_match_mask);
    xnu_log_kv32("stage85_xnu_iokit_provider_notification_interest_provider_ordinal_match_mask", contract->interest_provider_ordinal_match_mask);
    xnu_log_kv32("stage85_xnu_iokit_provider_notification_interest_type_match_mask", contract->interest_type_match_mask);
    xnu_log_kv32("stage85_xnu_iokit_provider_notification_expected_notify_mask_match_mask", contract->expected_notify_mask_match_mask);
    xnu_log_kv32("stage85_xnu_iokit_provider_notification_delivered_notify_mask_match_mask", contract->delivered_notify_mask_match_mask);
    xnu_log_kv32("stage85_xnu_iokit_provider_notification_notify_dependency_ready_mask", contract->notify_dependency_ready_mask);
    xnu_log_kv32("stage85_xnu_iokit_provider_notification_interest_runtime_blocked_mask", contract->interest_runtime_blocked_mask);
    xnu_log_kv32("stage85_xnu_iokit_provider_notification_delivery_runtime_blocked_mask", contract->delivery_runtime_blocked_mask);
    xnu_log_kv32("stage85_xnu_iokit_provider_notification_interest_order_match_mask", contract->interest_order_match_mask);
    xnu_log_kv32("stage85_xnu_iokit_provider_notification_delivery_order_match_mask", contract->delivery_order_match_mask);
    xnu_log_kv32("stage85_xnu_iokit_provider_notification_notify_provenance_match_mask", contract->notify_provenance_match_mask);
    xnu_log_kv32("stage85_xnu_iokit_provider_notification_selected_provider_notification_entry_mask", contract->selected_provider_notification_entry_mask);
    xnu_log_kv32("stage85_xnu_iokit_provider_notification_rejected_provider_notification_mask", contract->rejected_provider_notification_mask);
    xnu_log_kv32("stage85_xnu_iokit_provider_notification_matrix", contract->provider_notification_matrix);
    xnu_log_kv32("stage85_xnu_iokit_provider_notification_interest_order_checksum", contract->interest_order_checksum);
    xnu_log_kv32("stage85_xnu_iokit_provider_notification_delivery_order_checksum", contract->delivery_order_checksum);
    xnu_log_kv32("stage85_xnu_iokit_provider_notification_notify_mask_checksum", contract->notify_mask_checksum);
    xnu_log_kv32("stage85_xnu_iokit_provider_notification_path_hash", contract->provider_notification_path_hash);
    xnu_log_kv32("stage85_xnu_iokit_provider_notification_checksum", contract->provider_notification_checksum);
    xnu_log_kv32("stage85_xnu_iokit_provider_notification_expected_checksum", contract->expected_provider_notification_checksum);
    xnu_log_kv32("stage85_xnu_iokit_provider_notification_candidate_count", contract->provider_notification_candidate_count);
    xnu_log_kv32("stage85_xnu_iokit_provider_notification_selected_entry_count", contract->selected_provider_notification_entry_count);
    xnu_log_kv32("stage85_xnu_iokit_provider_notification_rejected_entry_count", contract->rejected_provider_notification_entry_count);
    xnu_log_kv32("stage85_xnu_iokit_provider_notification_ready_count", contract->provider_notification_ready_count);
    xnu_log_kv32("stage85_xnu_iokit_provider_notification_interest_ready_count", contract->interest_ready_count);
    xnu_log_kv32("stage85_xnu_iokit_provider_notification_delivery_ready_count", contract->delivery_ready_count);
    xnu_log_kv32("stage85_xnu_iokit_provider_notification_dependency_ready_count", contract->notify_dependency_ready_count);
    xnu_log_kv32("stage85_xnu_iokit_provider_notification_rejected_undelivered_count", contract->rejected_undelivered_count);
    xnu_log_kv32("stage85_xnu_iokit_provider_notification_public_compile_count", contract->iokit_public_compile_count);
    xnu_log_kv32("stage85_xnu_iokit_provider_notification_public_link_count", contract->iokit_public_link_count);
    xnu_log_kv32("stage85_xnu_iokit_provider_notification_no_iokit_runtime_exec", contract->no_iokit_runtime_exec);
    xnu_log_kv32("stage85_xnu_iokit_provider_notification_no_interest_runtime_exec", contract->no_interest_runtime_exec);
    xnu_log_kv32("stage85_xnu_iokit_provider_notification_no_delivery_runtime_exec", contract->no_delivery_runtime_exec);
    xnu_log_kv32("stage85_xnu_iokit_provider_notification_no_live_pmap_tables_installed", contract->no_live_pmap_tables_installed);
    xnu_log_kv32("stage85_xnu_iokit_provider_notification_proposed_workspace_written", contract->proposed_workspace_written);
    xnu_log_kv32("stage85_xnu_iokit_provider_notification_pmap_ttbr_written", contract->pmap_ttbr_written);
    xnu_log_kv32("stage85_xnu_iokit_provider_notification_pmap_tlbs_invalidated", contract->pmap_tlbs_invalidated);
    xnu_log_kv32("stage85_xnu_iokit_provider_notification_caches_changed", contract->caches_changed);
    xnu_log_kv32("stage85_xnu_iokit_provider_notification_persistent_write_attempted", contract->persistent_write_attempted);
}

static void selected_masks_for_node(const struct boot_args *args, const void *node, uint32_t bit,
                                    const char *provider_path, uint32_t provider_ordinal,
                                    uint32_t order,
                                    struct stage85_xnu_iokit_provider_notification_delivery_readiness_dryrun_contract *contract)
{
    uint32_t provider_path_ok = string_prop_equals(args, node, "interest-provider-path", provider_path);
    uint32_t provider_ordinal_ok = u32_prop_equals(args, node, "interest-provider-ordinal", provider_ordinal);
    uint32_t interest_type_ok = u32_prop_equals(args, node, "interest-type-mask", bit);
    uint32_t expected_notify_ok = u32_prop_equals(args, node, "expected-notify-mask", bit);
    uint32_t delivered_notify_ok = u32_prop_equals(args, node, "delivered-notify-mask", bit);
    uint32_t dependency_ok = u32_prop_equals(args, node, "notify-dependency-ready", 1u);

    if (u32_prop_equals(args, node, "prov-notify-dryrun", 1u) &&
        u32_prop_equals(args, node, "prov-notify-ready", 1u)) {
        contract->provider_notification_ready_mask |= bit;
    }
    if (u32_prop_equals(args, node, "interest-ready", 1u)) {
        contract->interest_ready_mask |= bit;
    }
    if (u32_prop_equals(args, node, "delivery-ready", 1u)) {
        contract->delivery_ready_mask |= bit;
    }
    if (provider_path_ok != 0u) {
        contract->interest_provider_path_match_mask |= bit;
    }
    if (provider_ordinal_ok != 0u) {
        contract->interest_provider_ordinal_match_mask |= bit;
    }
    if (interest_type_ok != 0u) {
        contract->interest_type_match_mask |= bit;
    }
    if (expected_notify_ok != 0u) {
        contract->expected_notify_mask_match_mask |= bit;
    }
    if (delivered_notify_ok != 0u) {
        contract->delivered_notify_mask_match_mask |= bit;
    }
    if (dependency_ok != 0u) {
        contract->notify_dependency_ready_mask |= bit;
    }
    if (u32_prop_equals(args, node, "interest-runtime-exec", 0u)) {
        contract->interest_runtime_blocked_mask |= bit;
    }
    if (u32_prop_equals(args, node, "delivery-runtime-exec", 0u)) {
        contract->delivery_runtime_blocked_mask |= bit;
    }
    if (u32_prop_equals(args, node, "interest-order", order)) {
        contract->interest_order_match_mask |= bit;
    }
    if (u32_prop_equals(args, node, "delivery-order", order)) {
        contract->delivery_order_match_mask |= bit;
    }
    if (string_prop_equals(args, node, "notify-provenance", "stage85-lifecycle-regsvc")) {
        contract->notify_provenance_match_mask |= bit;
    }
}

int stage85_xnu_iokit_provider_notification_delivery_readiness_dryrun_contract_selftest(
    const struct stage85_loader_preflight *preflight)
{
    struct stage85_xnu_iokit_provider_notification_delivery_readiness_dryrun_contract *contract =
        &g_stage85_xnu_iokit_provider_notification_delivery_readiness_dryrun_contract;
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
    uint32_t notification_dependency_ok;
    uint32_t public_boundary_ok;
    uint32_t pmap_boundary_ok;

    memset(contract, 0, sizeof(*contract));
    contract->version = STAGE85_XNU_IOKIT_PROVIDER_NOTIFICATION_DELIVERY_DRYRUN_CONTRACT_VERSION;
    contract->size = sizeof(*contract);
    contract->status = STAGE85_STATUS_BASE;
    contract->required_mask = STAGE85_XNU_IOKIT_PROVIDER_NOTIFICATION_DELIVERY_DRYRUN_REQUIRED_MASK;

    if (!preflight || !args || !root) {
        contract->failure_mask = STAGE85_XNU_IOKIT_PROVIDER_NOTIFICATION_DELIVERY_DRYRUN_FAIL_SOURCE |
            STAGE85_XNU_IOKIT_PROVIDER_NOTIFICATION_DELIVERY_DRYRUN_FAIL_SAFETY_BOUNDARY;
        contract->checksum = stage85_iokit_provider_notification_delivery_dryrun_checksum(contract);
        stage85_iokit_provider_notification_delivery_dryrun_log(contract);
        return 0;
    }

    contract->source_lifecycle_status = preflight->xnu_iokit_lifecycle_register_service_readiness_dryrun_contract_status;
    contract->source_lifecycle_satisfied_mask =
        preflight->xnu_iokit_lifecycle_register_service_readiness_dryrun_contract_satisfied_mask;
    contract->source_lifecycle_failure_mask =
        preflight->xnu_iokit_lifecycle_register_service_readiness_dryrun_contract_failure_mask;
    contract->source_lifecycle_contract_checksum =
        preflight->xnu_iokit_lifecycle_register_service_readiness_dryrun_contract_checksum;
    contract->source_lifecycle_checksum =
        preflight->xnu_iokit_lifecycle_register_service_readiness_dryrun_contract.lifecycle_checksum;
    contract->source_selected_lifecycle_entry_mask =
        preflight->xnu_iokit_lifecycle_register_service_readiness_dryrun_contract.selected_lifecycle_entry_mask;
    contract->source_rejected_lifecycle_mask =
        preflight->xnu_iokit_lifecycle_register_service_readiness_dryrun_contract.rejected_lifecycle_mask;
    contract->source_lifecycle_matrix =
        preflight->xnu_iokit_lifecycle_register_service_readiness_dryrun_contract.lifecycle_matrix;
    contract->source_notification_ready_mask =
        preflight->xnu_iokit_lifecycle_register_service_readiness_dryrun_contract.notification_ready_mask;
    contract->source_register_service_readiness_mask =
        preflight->xnu_iokit_lifecycle_register_service_readiness_dryrun_contract.register_service_readiness_mask;
    contract->source_service_registered_mask =
        preflight->xnu_iokit_lifecycle_register_service_readiness_dryrun_contract.service_registered_mask;
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
    contract->source_loader_safety_required_mask = STAGE85_LOADER_SAFETY_REQUIRED;
    contract->boot_args_ptr = preflight->boot_args_ptr;
    contract->device_tree_ptr = preflight->device_tree_ptr;
    contract->device_tree_length = preflight->device_tree_length;
    contract->apple_dt_semantic_mask = preflight->apple_dt_semantic_mask;

    contract->platform_personality_node_ptr = (uint32_t)(uintptr_t)apple_dt_find_child(args->deviceTreeP,
                                                                                       args->deviceTreeLength,
                                                                                       root,
                                                                                       "stage85-platform-personality");
    contract->interrupt_personality_node_ptr = (uint32_t)(uintptr_t)apple_dt_find_child(args->deviceTreeP,
                                                                                        args->deviceTreeLength,
                                                                                        root,
                                                                                        "stage85-interrupt-personality");
    contract->timer_personality_node_ptr = (uint32_t)(uintptr_t)apple_dt_find_child(args->deviceTreeP,
                                                                                    args->deviceTreeLength,
                                                                                    root,
                                                                                    "stage85-timer-personality");
    contract->cpu_personality_node_ptr = (uint32_t)(uintptr_t)apple_dt_find_child(args->deviceTreeP,
                                                                                  args->deviceTreeLength,
                                                                                  root,
                                                                                  "stage85-cpu-personality");
    contract->rejected_personality_node_ptr = (uint32_t)(uintptr_t)apple_dt_find_child(args->deviceTreeP,
                                                                                       args->deviceTreeLength,
                                                                                       root,
                                                                                       "stage85-rejected-personality");

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

    selected_masks_for_node(args, platform_node, STAGE85_XNU_IOKIT_REGISTRY_SERVICE_PLATFORM_BIT,
                            "IODeviceTree:/", STAGE85_XNU_IOKIT_REGISTRY_TOPOLOGY_REJECTED_ORDINAL,
                            STAGE85_XNU_IOKIT_REGISTRY_TOPOLOGY_PLATFORM_ORDINAL, contract);
    selected_masks_for_node(args, interrupt_node, STAGE85_XNU_IOKIT_REGISTRY_SERVICE_INTERRUPT_BIT,
                            "IODeviceTree:/stage85-platform-personality",
                            STAGE85_XNU_IOKIT_REGISTRY_TOPOLOGY_PLATFORM_ORDINAL,
                            STAGE85_XNU_IOKIT_REGISTRY_TOPOLOGY_INTERRUPT_ORDINAL, contract);
    selected_masks_for_node(args, timer_node, STAGE85_XNU_IOKIT_REGISTRY_SERVICE_TIMER_BIT,
                            "IODeviceTree:/stage85-platform-personality",
                            STAGE85_XNU_IOKIT_REGISTRY_TOPOLOGY_PLATFORM_ORDINAL,
                            STAGE85_XNU_IOKIT_REGISTRY_TOPOLOGY_TIMER_ORDINAL, contract);
    selected_masks_for_node(args, cpu_node, STAGE85_XNU_IOKIT_REGISTRY_SERVICE_CPU_BIT,
                            "IODeviceTree:/stage85-platform-personality",
                            STAGE85_XNU_IOKIT_REGISTRY_TOPOLOGY_PLATFORM_ORDINAL,
                            STAGE85_XNU_IOKIT_REGISTRY_TOPOLOGY_CPU_ORDINAL, contract);

    contract->platform_interest_provider_ordinal = apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength,
                                                                         platform_node, "interest-provider-ordinal", 0u);
    contract->interrupt_interest_provider_ordinal = apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength,
                                                                          interrupt_node, "interest-provider-ordinal", 0xffffffffu);
    contract->timer_interest_provider_ordinal = apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength,
                                                                      timer_node, "interest-provider-ordinal", 0xffffffffu);
    contract->cpu_interest_provider_ordinal = apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength,
                                                                    cpu_node, "interest-provider-ordinal", 0xffffffffu);
    contract->rejected_interest_provider_ordinal = apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength,
                                                                         rejected_node, "interest-provider-ordinal", 0u);
    contract->platform_interest_type_mask = apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength,
                                                                  platform_node, "interest-type-mask", 0u);
    contract->interrupt_interest_type_mask = apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength,
                                                                   interrupt_node, "interest-type-mask", 0u);
    contract->timer_interest_type_mask = apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength,
                                                               timer_node, "interest-type-mask", 0u);
    contract->cpu_interest_type_mask = apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength,
                                                             cpu_node, "interest-type-mask", 0u);
    contract->rejected_interest_type_mask = apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength,
                                                                  rejected_node, "interest-type-mask", 0xffffffffu);
    contract->platform_expected_notify_mask = apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength,
                                                                    platform_node, "expected-notify-mask", 0u);
    contract->interrupt_expected_notify_mask = apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength,
                                                                     interrupt_node, "expected-notify-mask", 0u);
    contract->timer_expected_notify_mask = apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength,
                                                                 timer_node, "expected-notify-mask", 0u);
    contract->cpu_expected_notify_mask = apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength,
                                                               cpu_node, "expected-notify-mask", 0u);
    contract->rejected_expected_notify_mask = apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength,
                                                                    rejected_node, "expected-notify-mask", 0xffffffffu);
    contract->platform_delivered_notify_mask = apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength,
                                                                     platform_node, "delivered-notify-mask", 0u);
    contract->interrupt_delivered_notify_mask = apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength,
                                                                      interrupt_node, "delivered-notify-mask", 0u);
    contract->timer_delivered_notify_mask = apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength,
                                                                  timer_node, "delivered-notify-mask", 0u);
    contract->cpu_delivered_notify_mask = apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength,
                                                                cpu_node, "delivered-notify-mask", 0u);
    contract->rejected_delivered_notify_mask = apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength,
                                                                     rejected_node, "delivered-notify-mask", 0xffffffffu);
    contract->platform_interest_order = apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength,
                                                              platform_node, "interest-order", 0xffffffffu);
    contract->interrupt_interest_order = apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength,
                                                               interrupt_node, "interest-order", 0xffffffffu);
    contract->timer_interest_order = apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength,
                                                           timer_node, "interest-order", 0xffffffffu);
    contract->cpu_interest_order = apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength,
                                                         cpu_node, "interest-order", 0xffffffffu);
    contract->rejected_interest_order = apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength,
                                                              rejected_node, "interest-order", 0u);
    contract->platform_delivery_order = apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength,
                                                              platform_node, "delivery-order", 0xffffffffu);
    contract->interrupt_delivery_order = apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength,
                                                               interrupt_node, "delivery-order", 0xffffffffu);
    contract->timer_delivery_order = apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength,
                                                           timer_node, "delivery-order", 0xffffffffu);
    contract->cpu_delivery_order = apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength,
                                                         cpu_node, "delivery-order", 0xffffffffu);
    contract->rejected_delivery_order = apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength,
                                                              rejected_node, "delivery-order", 0u);

    contract->selected_provider_notification_entry_mask = contract->provider_notification_ready_mask &
        contract->interest_ready_mask & contract->delivery_ready_mask & contract->interest_provider_path_match_mask &
        contract->interest_provider_ordinal_match_mask & contract->interest_type_match_mask &
        contract->expected_notify_mask_match_mask & contract->delivered_notify_mask_match_mask &
        contract->notify_dependency_ready_mask & contract->interest_runtime_blocked_mask &
        contract->delivery_runtime_blocked_mask & contract->interest_order_match_mask &
        contract->delivery_order_match_mask & contract->notify_provenance_match_mask &
        contract->source_selected_lifecycle_entry_mask;

    if (u32_prop_equals(args, rejected_node, "lifecycle-regsvc-dryrun", 0u) &&
        u32_prop_equals(args, rejected_node, "service-registered", 0u) &&
        u32_prop_equals(args, rejected_node, "notification-ready", 0u) &&
        u32_prop_equals(args, rejected_node, "prov-notify-dryrun", 0u) &&
        u32_prop_equals(args, rejected_node, "prov-notify-ready", 0u) &&
        u32_prop_equals(args, rejected_node, "interest-ready", 0u) &&
        u32_prop_equals(args, rejected_node, "interest-runtime-exec", 0u) &&
        u32_prop_equals(args, rejected_node, "delivery-ready", 0u) &&
        u32_prop_equals(args, rejected_node, "delivery-runtime-exec", 0u) &&
        string_prop_equals(args, rejected_node, "interest-provider-path", "IODeviceTree:/unlinked") &&
        u32_prop_equals(args, rejected_node, "interest-provider-ordinal",
                        STAGE85_XNU_IOKIT_REGISTRY_TOPOLOGY_REJECTED_ORDINAL) &&
        u32_prop_equals(args, rejected_node, "interest-type-mask", 0u) &&
        u32_prop_equals(args, rejected_node, "expected-notify-mask", 0u) &&
        u32_prop_equals(args, rejected_node, "delivered-notify-mask", 0u) &&
        u32_prop_equals(args, rejected_node, "interest-order", STAGE85_XNU_IOKIT_REGISTRY_TOPOLOGY_REJECTED_ORDINAL) &&
        u32_prop_equals(args, rejected_node, "delivery-order", STAGE85_XNU_IOKIT_REGISTRY_TOPOLOGY_REJECTED_ORDINAL) &&
        u32_prop_equals(args, rejected_node, "notify-dependency-ready", 0u) &&
        string_prop_equals(args, rejected_node, "notify-provenance", "stage85-rejected-undelivered") &&
        u32_prop_equals(args, rejected_node, "rejected-candidate", 1u) &&
        u32_prop_equals(args, rejected_node, "stage-owned-local-only", 1u)) {
        contract->rejected_provider_notification_mask |= STAGE85_XNU_IOKIT_REGISTRY_SERVICE_REJECTED_BIT;
    }

    contract->provider_notification_matrix = contract->selected_provider_notification_entry_mask |
        contract->rejected_provider_notification_mask;
    contract->interest_order_checksum = contract->platform_interest_order ^ contract->interrupt_interest_order ^
        contract->timer_interest_order ^ contract->cpu_interest_order ^ contract->rejected_interest_order;
    contract->delivery_order_checksum = contract->platform_delivery_order ^ contract->interrupt_delivery_order ^
        contract->timer_delivery_order ^ contract->cpu_delivery_order ^ contract->rejected_delivery_order;
    contract->notify_mask_checksum = contract->platform_expected_notify_mask ^ contract->interrupt_expected_notify_mask ^
        contract->timer_expected_notify_mask ^ contract->cpu_expected_notify_mask ^ contract->rejected_expected_notify_mask ^
        contract->platform_delivered_notify_mask ^ contract->interrupt_delivered_notify_mask ^
        contract->timer_delivered_notify_mask ^ contract->cpu_delivered_notify_mask ^ contract->rejected_delivered_notify_mask;
    contract->provider_notification_path_hash = STAGE85_XNU_IOKIT_PROVIDER_NOTIFICATION_DELIVERY_PATH_HASH;
    contract->provider_notification_candidate_count = STAGE85_XNU_IOKIT_PROVIDER_NOTIFICATION_DELIVERY_CANDIDATE_COUNT;
    contract->selected_provider_notification_entry_count =
        (contract->selected_provider_notification_entry_mask == STAGE85_XNU_IOKIT_REGISTRY_SERVICE_SELECTED_REQUIRED) ?
        STAGE85_XNU_IOKIT_PROVIDER_NOTIFICATION_DELIVERY_ENTRY_COUNT : 0u;
    contract->rejected_provider_notification_entry_count =
        (contract->rejected_provider_notification_mask == STAGE85_XNU_IOKIT_REGISTRY_SERVICE_REJECTED_REQUIRED) ?
        STAGE85_XNU_IOKIT_PROVIDER_NOTIFICATION_DELIVERY_REJECTED_COUNT : 0u;
    contract->provider_notification_ready_count = contract->selected_provider_notification_entry_count;
    contract->interest_ready_count = contract->selected_provider_notification_entry_count;
    contract->delivery_ready_count = contract->selected_provider_notification_entry_count;
    contract->notify_dependency_ready_count = contract->selected_provider_notification_entry_count;
    contract->rejected_undelivered_count = contract->rejected_provider_notification_entry_count;
    contract->provider_notification_checksum = contract->source_lifecycle_checksum ^
        contract->source_selected_lifecycle_entry_mask ^ contract->source_rejected_lifecycle_mask ^
        contract->source_lifecycle_matrix ^ contract->source_notification_ready_mask ^
        contract->source_register_service_readiness_mask ^ contract->source_service_registered_mask ^
        contract->provider_notification_ready_mask ^ contract->interest_ready_mask ^ contract->delivery_ready_mask ^
        contract->interest_provider_path_match_mask ^ contract->interest_provider_ordinal_match_mask ^
        contract->interest_type_match_mask ^ contract->expected_notify_mask_match_mask ^
        contract->delivered_notify_mask_match_mask ^ contract->notify_dependency_ready_mask ^
        contract->interest_runtime_blocked_mask ^ contract->delivery_runtime_blocked_mask ^
        contract->interest_order_match_mask ^ contract->delivery_order_match_mask ^
        contract->notify_provenance_match_mask ^ contract->selected_provider_notification_entry_mask ^
        contract->rejected_provider_notification_mask ^ contract->provider_notification_matrix ^
        contract->interest_order_checksum ^ contract->delivery_order_checksum ^ contract->notify_mask_checksum ^
        contract->provider_notification_path_hash ^ contract->provider_notification_candidate_count ^
        contract->selected_provider_notification_entry_count ^ contract->rejected_provider_notification_entry_count ^
        contract->provider_notification_ready_count ^ contract->interest_ready_count ^ contract->delivery_ready_count ^
        contract->notify_dependency_ready_count ^ contract->rejected_undelivered_count ^
        contract->no_interest_runtime_exec ^ contract->no_delivery_runtime_exec;
    contract->expected_provider_notification_checksum =
        STAGE85_XNU_IOKIT_PROVIDER_NOTIFICATION_DELIVERY_EXPECTED_CHECKSUM;

    contract->iokit_reference_mask = preflight->xnu_iokit_lifecycle_register_service_readiness_dryrun_contract.iokit_reference_mask;
    contract->iokit_runtime_blocked_mask =
        preflight->xnu_iokit_lifecycle_register_service_readiness_dryrun_contract.iokit_runtime_blocked_mask;
    contract->iokit_reference_count = preflight->xnu_iokit_lifecycle_register_service_readiness_dryrun_contract.iokit_reference_count;
    contract->iokit_public_compile_count =
        preflight->xnu_iokit_lifecycle_register_service_readiness_dryrun_contract.iokit_public_compile_count;
    contract->iokit_public_link_count =
        preflight->xnu_iokit_lifecycle_register_service_readiness_dryrun_contract.iokit_public_link_count;
    contract->iokit_reference_only = preflight->xnu_iokit_lifecycle_register_service_readiness_dryrun_contract.iokit_reference_only;
    contract->no_iokit_runtime_exec =
        preflight->xnu_iokit_lifecycle_register_service_readiness_dryrun_contract.no_iokit_runtime_exec;
    contract->no_catalog_runtime_exec =
        preflight->xnu_iokit_lifecycle_register_service_readiness_dryrun_contract.no_catalog_runtime_exec;
    contract->no_provider_runtime_exec =
        preflight->xnu_iokit_lifecycle_register_service_readiness_dryrun_contract.no_provider_runtime_exec;
    contract->no_registry_entry_runtime_exec =
        preflight->xnu_iokit_lifecycle_register_service_readiness_dryrun_contract.no_registry_entry_runtime_exec;
    contract->no_registry_topology_runtime_exec =
        preflight->xnu_iokit_lifecycle_register_service_readiness_dryrun_contract.no_registry_topology_runtime_exec;
    contract->no_property_runtime_exec =
        preflight->xnu_iokit_lifecycle_register_service_readiness_dryrun_contract.no_property_runtime_exec;
    contract->no_attach_runtime_exec =
        preflight->xnu_iokit_lifecycle_register_service_readiness_dryrun_contract.no_attach_runtime_exec;
    contract->no_start_runtime_exec =
        preflight->xnu_iokit_lifecycle_register_service_readiness_dryrun_contract.no_start_runtime_exec;
    contract->no_register_service_runtime_exec =
        preflight->xnu_iokit_lifecycle_register_service_readiness_dryrun_contract.no_register_service_runtime_exec;
    contract->no_notification_runtime_exec =
        preflight->xnu_iokit_lifecycle_register_service_readiness_dryrun_contract.no_notification_runtime_exec;
    contract->no_interest_runtime_exec = 1u;
    contract->no_delivery_runtime_exec = 1u;
    contract->no_platform_driver_exec =
        preflight->xnu_iokit_lifecycle_register_service_readiness_dryrun_contract.no_platform_driver_exec;
    contract->no_public_xnu_exec =
        preflight->xnu_iokit_lifecycle_register_service_readiness_dryrun_contract.no_public_xnu_exec;
    contract->no_platform_runtime_exec =
        preflight->xnu_iokit_lifecycle_register_service_readiness_dryrun_contract.no_platform_runtime_exec;
    contract->no_public_pmap_exec =
        preflight->xnu_iokit_lifecycle_register_service_readiness_dryrun_contract.no_public_pmap_exec;
    contract->no_live_pmap_tables_installed =
        preflight->xnu_iokit_lifecycle_register_service_readiness_dryrun_contract.no_live_pmap_tables_installed;
    contract->proposed_workspace_written =
        preflight->xnu_iokit_lifecycle_register_service_readiness_dryrun_contract.proposed_workspace_written;
    contract->pmap_ttbr_written = preflight->xnu_iokit_lifecycle_register_service_readiness_dryrun_contract.pmap_ttbr_written;
    contract->pmap_ttbcr_written = preflight->xnu_iokit_lifecycle_register_service_readiness_dryrun_contract.pmap_ttbcr_written;
    contract->pmap_dacr_written = preflight->xnu_iokit_lifecycle_register_service_readiness_dryrun_contract.pmap_dacr_written;
    contract->pmap_sctlr_written = preflight->xnu_iokit_lifecycle_register_service_readiness_dryrun_contract.pmap_sctlr_written;
    contract->pmap_tlbs_invalidated =
        preflight->xnu_iokit_lifecycle_register_service_readiness_dryrun_contract.pmap_tlbs_invalidated;
    contract->caches_changed = preflight->xnu_iokit_lifecycle_register_service_readiness_dryrun_contract.caches_changed;
    contract->persistent_write_attempted =
        preflight->xnu_iokit_lifecycle_register_service_readiness_dryrun_contract.persistent_write_attempted;
    contract->xnu_start_executed =
        preflight->xnu_iokit_lifecycle_register_service_readiness_dryrun_contract.xnu_start_executed;
    contract->generated_macho_executed =
        preflight->xnu_iokit_lifecycle_register_service_readiness_dryrun_contract.generated_macho_executed;
    contract->proposed_platform_gap_mask = preflight->platform_gap_mask;
    contract->proposed_pexpert_gap_mask = preflight->pexpert_gap_mask;
    contract->local_only = 1u;
    contract->fail_closed = 1u;

    contract->provider_notification_checksum ^= contract->no_interest_runtime_exec ^ contract->no_delivery_runtime_exec;

    source_ok = (contract->source_lifecycle_status == STAGE85_STATUS_OK &&
                 contract->source_lifecycle_satisfied_mask ==
                 STAGE85_XNU_IOKIT_LIFECYCLE_REGISTER_SERVICE_DRYRUN_REQUIRED_MASK &&
                 contract->source_lifecycle_failure_mask == 0u &&
                 contract->source_lifecycle_checksum == STAGE85_XNU_IOKIT_LIFECYCLE_REGISTER_SERVICE_EXPECTED_CHECKSUM &&
                 contract->source_selected_lifecycle_entry_mask == STAGE85_XNU_IOKIT_REGISTRY_SERVICE_SELECTED_REQUIRED &&
                 contract->source_rejected_lifecycle_mask == STAGE85_XNU_IOKIT_REGISTRY_SERVICE_REJECTED_REQUIRED &&
                 contract->source_lifecycle_matrix == (STAGE85_XNU_IOKIT_REGISTRY_SERVICE_SELECTED_REQUIRED |
                                                       STAGE85_XNU_IOKIT_REGISTRY_SERVICE_REJECTED_REQUIRED) &&
                 contract->source_notification_ready_mask == STAGE85_XNU_IOKIT_REGISTRY_SERVICE_SELECTED_REQUIRED &&
                 contract->source_register_service_readiness_mask == STAGE85_XNU_IOKIT_REGISTRY_SERVICE_SELECTED_REQUIRED &&
                 contract->source_service_registered_mask == STAGE85_XNU_IOKIT_REGISTRY_SERVICE_SELECTED_REQUIRED &&
                 contract->source_attach_start_status == STAGE85_STATUS_OK &&
                 contract->source_topology_status == STAGE85_STATUS_OK &&
                 contract->source_property_inheritance_status == STAGE85_STATUS_OK &&
                 contract->source_catalog_status == STAGE85_STATUS_OK &&
                 contract->source_provider_status == STAGE85_STATUS_OK &&
                 contract->source_registry_status == STAGE85_STATUS_OK &&
                 contract->source_match_status == STAGE85_STATUS_OK &&
                 contract->source_scaffold_status == STAGE85_STATUS_OK &&
                 contract->source_pexpert_status == STAGE85_STATUS_OK &&
                 contract->source_pmap_transition_status == STAGE85_STATUS_OK &&
                 contract->source_compile_graph_status == STAGE85_STATUS_OK &&
                 contract->source_object_subset_status == STAGE85_STATUS_OK &&
                 contract->source_link_status == STAGE85_STATUS_OK &&
                 contract->source_loader_safety_mask == contract->source_loader_safety_required_mask) ? 1u : 0u;
    if (source_ok) {
        contract->satisfied_mask |= STAGE85_XNU_IOKIT_PROVIDER_NOTIFICATION_DELIVERY_DRYRUN_SAT_SOURCE_LIFECYCLE;
    } else {
        contract->failure_mask |= STAGE85_XNU_IOKIT_PROVIDER_NOTIFICATION_DELIVERY_DRYRUN_FAIL_SOURCE;
    }

    if (contract->apple_dt_semantic_mask == STAGE85_DT_READY_REQUIRED) {
        contract->satisfied_mask |= STAGE85_XNU_IOKIT_PROVIDER_NOTIFICATION_DELIVERY_DRYRUN_SAT_APPLE_DT;
    } else {
        contract->failure_mask |= STAGE85_XNU_IOKIT_PROVIDER_NOTIFICATION_DELIVERY_DRYRUN_FAIL_DT;
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
        contract->satisfied_mask |= STAGE85_XNU_IOKIT_PROVIDER_NOTIFICATION_DELIVERY_DRYRUN_SAT_PERSONALITY_NODE_SET;
    } else {
        contract->failure_mask |= STAGE85_XNU_IOKIT_PROVIDER_NOTIFICATION_DELIVERY_DRYRUN_FAIL_PERSONALITY;
    }

    if (contract->provider_notification_ready_mask == STAGE85_XNU_IOKIT_REGISTRY_SERVICE_SELECTED_REQUIRED) {
        contract->satisfied_mask |= STAGE85_XNU_IOKIT_PROVIDER_NOTIFICATION_DELIVERY_DRYRUN_SAT_PROVIDER_NOTIFICATION_READY;
    } else {
        contract->failure_mask |= STAGE85_XNU_IOKIT_PROVIDER_NOTIFICATION_DELIVERY_DRYRUN_FAIL_PROVIDER_NOTIFICATION;
    }
    if (contract->interest_ready_mask == STAGE85_XNU_IOKIT_REGISTRY_SERVICE_SELECTED_REQUIRED) {
        contract->satisfied_mask |= STAGE85_XNU_IOKIT_PROVIDER_NOTIFICATION_DELIVERY_DRYRUN_SAT_INTEREST_READY;
    } else {
        contract->failure_mask |= STAGE85_XNU_IOKIT_PROVIDER_NOTIFICATION_DELIVERY_DRYRUN_FAIL_INTEREST;
    }
    if (contract->delivery_ready_mask == STAGE85_XNU_IOKIT_REGISTRY_SERVICE_SELECTED_REQUIRED) {
        contract->satisfied_mask |= STAGE85_XNU_IOKIT_PROVIDER_NOTIFICATION_DELIVERY_DRYRUN_SAT_DELIVERY_READY;
    } else {
        contract->failure_mask |= STAGE85_XNU_IOKIT_PROVIDER_NOTIFICATION_DELIVERY_DRYRUN_FAIL_DELIVERY;
    }
    if (contract->interest_provider_path_match_mask == STAGE85_XNU_IOKIT_REGISTRY_SERVICE_SELECTED_REQUIRED) {
        contract->satisfied_mask |= STAGE85_XNU_IOKIT_PROVIDER_NOTIFICATION_DELIVERY_DRYRUN_SAT_PROVIDER_PATH_MATRIX;
    } else {
        contract->failure_mask |= STAGE85_XNU_IOKIT_PROVIDER_NOTIFICATION_DELIVERY_DRYRUN_FAIL_INTEREST;
    }
    if (contract->interest_provider_ordinal_match_mask == STAGE85_XNU_IOKIT_REGISTRY_SERVICE_SELECTED_REQUIRED) {
        contract->satisfied_mask |= STAGE85_XNU_IOKIT_PROVIDER_NOTIFICATION_DELIVERY_DRYRUN_SAT_PROVIDER_ORDINAL_MATRIX;
    } else {
        contract->failure_mask |= STAGE85_XNU_IOKIT_PROVIDER_NOTIFICATION_DELIVERY_DRYRUN_FAIL_INTEREST;
    }
    if (contract->interest_type_match_mask == STAGE85_XNU_IOKIT_REGISTRY_SERVICE_SELECTED_REQUIRED) {
        contract->satisfied_mask |= STAGE85_XNU_IOKIT_PROVIDER_NOTIFICATION_DELIVERY_DRYRUN_SAT_INTEREST_TYPE_MATRIX;
    } else {
        contract->failure_mask |= STAGE85_XNU_IOKIT_PROVIDER_NOTIFICATION_DELIVERY_DRYRUN_FAIL_INTEREST;
    }
    if (contract->expected_notify_mask_match_mask == STAGE85_XNU_IOKIT_REGISTRY_SERVICE_SELECTED_REQUIRED) {
        contract->satisfied_mask |= STAGE85_XNU_IOKIT_PROVIDER_NOTIFICATION_DELIVERY_DRYRUN_SAT_EXPECTED_NOTIFY_MATRIX;
    } else {
        contract->failure_mask |= STAGE85_XNU_IOKIT_PROVIDER_NOTIFICATION_DELIVERY_DRYRUN_FAIL_PROVIDER_NOTIFICATION;
    }
    if (contract->delivered_notify_mask_match_mask == STAGE85_XNU_IOKIT_REGISTRY_SERVICE_SELECTED_REQUIRED) {
        contract->satisfied_mask |= STAGE85_XNU_IOKIT_PROVIDER_NOTIFICATION_DELIVERY_DRYRUN_SAT_DELIVERED_NOTIFY_MATRIX;
    } else {
        contract->failure_mask |= STAGE85_XNU_IOKIT_PROVIDER_NOTIFICATION_DELIVERY_DRYRUN_FAIL_DELIVERY;
    }

    order_ok = (contract->interest_order_match_mask == STAGE85_XNU_IOKIT_REGISTRY_SERVICE_SELECTED_REQUIRED &&
                contract->delivery_order_match_mask == STAGE85_XNU_IOKIT_REGISTRY_SERVICE_SELECTED_REQUIRED &&
                contract->interest_order_checksum == STAGE85_XNU_IOKIT_REGISTRY_TOPOLOGY_REJECTED_ORDINAL &&
                contract->delivery_order_checksum == STAGE85_XNU_IOKIT_REGISTRY_TOPOLOGY_REJECTED_ORDINAL) ? 1u : 0u;
    if (order_ok) {
        contract->satisfied_mask |= STAGE85_XNU_IOKIT_PROVIDER_NOTIFICATION_DELIVERY_DRYRUN_SAT_ORDER_MATRIX;
    } else {
        contract->failure_mask |= STAGE85_XNU_IOKIT_PROVIDER_NOTIFICATION_DELIVERY_DRYRUN_FAIL_ORDER_DEPENDENCY;
    }

    notification_dependency_ok =
        (contract->notify_dependency_ready_mask == STAGE85_XNU_IOKIT_REGISTRY_SERVICE_SELECTED_REQUIRED &&
         contract->notify_dependency_ready_count == STAGE85_XNU_IOKIT_PROVIDER_NOTIFICATION_DELIVERY_ENTRY_COUNT) ? 1u : 0u;
    if (notification_dependency_ok) {
        contract->satisfied_mask |= STAGE85_XNU_IOKIT_PROVIDER_NOTIFICATION_DELIVERY_DRYRUN_SAT_DEPENDENCY_MATRIX;
    } else {
        contract->failure_mask |= STAGE85_XNU_IOKIT_PROVIDER_NOTIFICATION_DELIVERY_DRYRUN_FAIL_ORDER_DEPENDENCY;
    }

    if (contract->interest_runtime_blocked_mask == STAGE85_XNU_IOKIT_REGISTRY_SERVICE_SELECTED_REQUIRED &&
        contract->delivery_runtime_blocked_mask == STAGE85_XNU_IOKIT_REGISTRY_SERVICE_SELECTED_REQUIRED &&
        contract->no_interest_runtime_exec == 1u && contract->no_delivery_runtime_exec == 1u) {
        contract->satisfied_mask |= STAGE85_XNU_IOKIT_PROVIDER_NOTIFICATION_DELIVERY_DRYRUN_SAT_RUNTIME_BLOCKED;
    } else {
        contract->failure_mask |= STAGE85_XNU_IOKIT_PROVIDER_NOTIFICATION_DELIVERY_DRYRUN_FAIL_INTEREST |
            STAGE85_XNU_IOKIT_PROVIDER_NOTIFICATION_DELIVERY_DRYRUN_FAIL_DELIVERY;
    }
    if (contract->notify_provenance_match_mask == STAGE85_XNU_IOKIT_REGISTRY_SERVICE_SELECTED_REQUIRED) {
        contract->satisfied_mask |= STAGE85_XNU_IOKIT_PROVIDER_NOTIFICATION_DELIVERY_DRYRUN_SAT_PROVENANCE;
    } else {
        contract->failure_mask |= STAGE85_XNU_IOKIT_PROVIDER_NOTIFICATION_DELIVERY_DRYRUN_FAIL_PROVIDER_NOTIFICATION;
    }
    if (contract->selected_provider_notification_entry_mask == STAGE85_XNU_IOKIT_REGISTRY_SERVICE_SELECTED_REQUIRED) {
        contract->satisfied_mask |= STAGE85_XNU_IOKIT_PROVIDER_NOTIFICATION_DELIVERY_DRYRUN_SAT_SELECTED_NOTIFICATION_MATRIX;
    } else {
        contract->failure_mask |= STAGE85_XNU_IOKIT_PROVIDER_NOTIFICATION_DELIVERY_DRYRUN_FAIL_PROVIDER_NOTIFICATION;
    }
    if (contract->rejected_provider_notification_mask == STAGE85_XNU_IOKIT_REGISTRY_SERVICE_REJECTED_REQUIRED &&
        contract->source_rejected_lifecycle_mask == STAGE85_XNU_IOKIT_REGISTRY_SERVICE_REJECTED_REQUIRED &&
        contract->rejected_undelivered_count == STAGE85_XNU_IOKIT_PROVIDER_NOTIFICATION_DELIVERY_REJECTED_COUNT) {
        contract->satisfied_mask |= STAGE85_XNU_IOKIT_PROVIDER_NOTIFICATION_DELIVERY_DRYRUN_SAT_REJECT_UNDELIVERED;
    } else {
        contract->failure_mask |= STAGE85_XNU_IOKIT_PROVIDER_NOTIFICATION_DELIVERY_DRYRUN_FAIL_REJECTED;
    }
    if (contract->provider_notification_candidate_count == STAGE85_XNU_IOKIT_PROVIDER_NOTIFICATION_DELIVERY_CANDIDATE_COUNT &&
        contract->selected_provider_notification_entry_count == STAGE85_XNU_IOKIT_PROVIDER_NOTIFICATION_DELIVERY_ENTRY_COUNT &&
        contract->rejected_provider_notification_entry_count == STAGE85_XNU_IOKIT_PROVIDER_NOTIFICATION_DELIVERY_REJECTED_COUNT &&
        contract->provider_notification_ready_count == STAGE85_XNU_IOKIT_PROVIDER_NOTIFICATION_DELIVERY_ENTRY_COUNT &&
        contract->interest_ready_count == STAGE85_XNU_IOKIT_PROVIDER_NOTIFICATION_DELIVERY_ENTRY_COUNT &&
        contract->delivery_ready_count == STAGE85_XNU_IOKIT_PROVIDER_NOTIFICATION_DELIVERY_ENTRY_COUNT &&
        contract->notify_dependency_ready_count == STAGE85_XNU_IOKIT_PROVIDER_NOTIFICATION_DELIVERY_ENTRY_COUNT &&
        contract->provider_notification_matrix == (STAGE85_XNU_IOKIT_REGISTRY_SERVICE_SELECTED_REQUIRED |
                                                  STAGE85_XNU_IOKIT_REGISTRY_SERVICE_REJECTED_REQUIRED)) {
        contract->satisfied_mask |= STAGE85_XNU_IOKIT_PROVIDER_NOTIFICATION_DELIVERY_DRYRUN_SAT_DRYRUN_COUNTS;
    } else {
        contract->failure_mask |= STAGE85_XNU_IOKIT_PROVIDER_NOTIFICATION_DELIVERY_DRYRUN_FAIL_COUNTS;
    }
    if (contract->provider_notification_checksum == contract->expected_provider_notification_checksum) {
        contract->satisfied_mask |= STAGE85_XNU_IOKIT_PROVIDER_NOTIFICATION_DELIVERY_DRYRUN_SAT_NOTIFICATION_CHECKSUM;
    } else {
        contract->failure_mask |= STAGE85_XNU_IOKIT_PROVIDER_NOTIFICATION_DELIVERY_DRYRUN_FAIL_CHECKSUM;
    }

    if (contract->iokit_reference_mask == STAGE85_XNU_IOKIT_REFERENCE_REQUIRED &&
        contract->iokit_runtime_blocked_mask == STAGE85_XNU_IOKIT_REFERENCE_REQUIRED &&
        contract->iokit_reference_count >= 3u && contract->iokit_public_compile_count == 0u &&
        contract->iokit_public_link_count == 0u && contract->iokit_reference_only == 1u) {
        contract->satisfied_mask |= STAGE85_XNU_IOKIT_PROVIDER_NOTIFICATION_DELIVERY_DRYRUN_SAT_IOKIT_REFERENCE;
    } else {
        contract->failure_mask |= STAGE85_XNU_IOKIT_PROVIDER_NOTIFICATION_DELIVERY_DRYRUN_FAIL_PUBLIC_BOUNDARY;
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
                          contract->no_interest_runtime_exec == 1u && contract->no_delivery_runtime_exec == 1u &&
                          contract->no_platform_driver_exec == 1u &&
                          contract->source_object_subset_status == STAGE85_STATUS_OK &&
                          contract->source_link_status == STAGE85_STATUS_OK) ? 1u : 0u;
    if (public_boundary_ok) {
        contract->satisfied_mask |= STAGE85_XNU_IOKIT_PROVIDER_NOTIFICATION_DELIVERY_DRYRUN_SAT_PUBLIC_BOUNDARY |
            STAGE85_XNU_IOKIT_PROVIDER_NOTIFICATION_DELIVERY_DRYRUN_SAT_NO_IOKIT_RUNTIME_EXEC |
            STAGE85_XNU_IOKIT_PROVIDER_NOTIFICATION_DELIVERY_DRYRUN_SAT_NO_INTEREST_RUNTIME_EXEC |
            STAGE85_XNU_IOKIT_PROVIDER_NOTIFICATION_DELIVERY_DRYRUN_SAT_NO_DELIVERY_RUNTIME_EXEC |
            STAGE85_XNU_IOKIT_PROVIDER_NOTIFICATION_DELIVERY_DRYRUN_SAT_NO_PUBLIC_XNU_EXEC;
    } else {
        contract->failure_mask |= STAGE85_XNU_IOKIT_PROVIDER_NOTIFICATION_DELIVERY_DRYRUN_FAIL_PUBLIC_BOUNDARY;
    }

    if (contract->no_public_pmap_exec == 1u) {
        contract->satisfied_mask |= STAGE85_XNU_IOKIT_PROVIDER_NOTIFICATION_DELIVERY_DRYRUN_SAT_NO_PUBLIC_PMAP_EXEC;
    } else {
        contract->failure_mask |= STAGE85_XNU_IOKIT_PROVIDER_NOTIFICATION_DELIVERY_DRYRUN_FAIL_PMAP_BOUNDARY;
    }

    pmap_boundary_ok = (contract->no_live_pmap_tables_installed == 0u &&
                        contract->proposed_workspace_written == 0u &&
                        contract->pmap_ttbr_written == 0u && contract->pmap_ttbcr_written == 0u &&
                        contract->pmap_dacr_written == 0u && contract->pmap_sctlr_written == 0u &&
                        contract->pmap_tlbs_invalidated == 0u && contract->caches_changed == 0u) ? 1u : 0u;
    if (contract->no_live_pmap_tables_installed == 0u) {
        contract->satisfied_mask |= STAGE85_XNU_IOKIT_PROVIDER_NOTIFICATION_DELIVERY_DRYRUN_SAT_NO_LIVE_PMAP_INSTALL;
    }
    if (contract->pmap_tlbs_invalidated == 0u && contract->caches_changed == 0u) {
        contract->satisfied_mask |= STAGE85_XNU_IOKIT_PROVIDER_NOTIFICATION_DELIVERY_DRYRUN_SAT_NO_TLB_CACHE_CHANGE;
    }
    if (pmap_boundary_ok && contract->source_pmap_transition_status == STAGE85_STATUS_OK) {
        contract->satisfied_mask |= STAGE85_XNU_IOKIT_PROVIDER_NOTIFICATION_DELIVERY_DRYRUN_SAT_PMAP_BOUNDARY;
    } else {
        contract->failure_mask |= STAGE85_XNU_IOKIT_PROVIDER_NOTIFICATION_DELIVERY_DRYRUN_FAIL_PMAP_BOUNDARY;
    }

    if (contract->generated_macho_executed == 0u && contract->xnu_start_executed == 0u) {
        contract->satisfied_mask |= STAGE85_XNU_IOKIT_PROVIDER_NOTIFICATION_DELIVERY_DRYRUN_SAT_NO_XNU_MACHO_EXEC;
    } else {
        contract->failure_mask |= STAGE85_XNU_IOKIT_PROVIDER_NOTIFICATION_DELIVERY_DRYRUN_FAIL_SAFETY_BOUNDARY;
    }
    if (contract->local_only == 1u && contract->fail_closed == 1u &&
        contract->persistent_write_attempted == 0u && contract->xnu_start_executed == 0u &&
        contract->generated_macho_executed == 0u &&
        contract->proposed_platform_gap_mask == (STAGE85_PLATFORM_GAP_REQUIRED_RECORDED & ~STAGE85_PLATFORM_GAP_IOKIT_STACK) &&
        contract->proposed_pexpert_gap_mask == 0u &&
        contract->selected_provider_notification_entry_mask == STAGE85_XNU_IOKIT_REGISTRY_SERVICE_SELECTED_REQUIRED &&
        contract->rejected_provider_notification_mask == STAGE85_XNU_IOKIT_REGISTRY_SERVICE_REJECTED_REQUIRED) {
        contract->satisfied_mask |= STAGE85_XNU_IOKIT_PROVIDER_NOTIFICATION_DELIVERY_DRYRUN_SAT_LOCAL_ONLY_FAIL_CLOSED;
    } else {
        contract->failure_mask |= STAGE85_XNU_IOKIT_PROVIDER_NOTIFICATION_DELIVERY_DRYRUN_FAIL_ROLLUP |
            STAGE85_XNU_IOKIT_PROVIDER_NOTIFICATION_DELIVERY_DRYRUN_FAIL_SAFETY_BOUNDARY;
    }

    contract->checksum = stage85_iokit_provider_notification_delivery_dryrun_checksum(contract);
    if (contract->checksum != stage85_iokit_provider_notification_delivery_dryrun_checksum(contract)) {
        contract->failure_mask |= STAGE85_XNU_IOKIT_PROVIDER_NOTIFICATION_DELIVERY_DRYRUN_FAIL_CHECKSUM;
    }

    if (contract->satisfied_mask == contract->required_mask && contract->failure_mask == 0u) {
        contract->status = STAGE85_STATUS_OK;
    } else {
        contract->status = STAGE85_STATUS_FAIL(contract->failure_mask);
    }

    stage85_iokit_provider_notification_delivery_dryrun_log(contract);
    return contract->status == STAGE85_STATUS_OK;
}

const struct stage85_xnu_iokit_provider_notification_delivery_readiness_dryrun_contract *
stage85_xnu_iokit_provider_notification_delivery_readiness_dryrun_contract_result(void)
{
    return &g_stage85_xnu_iokit_provider_notification_delivery_readiness_dryrun_contract;
}
