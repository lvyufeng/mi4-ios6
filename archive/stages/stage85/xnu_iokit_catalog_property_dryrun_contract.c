#include "stage85.h"

static struct stage85_xnu_iokit_catalog_property_dryrun_contract g_stage85_xnu_iokit_catalog_property_dryrun_contract;

static uint32_t stage85_iokit_catalog_property_dryrun_checksum(
    volatile const struct stage85_xnu_iokit_catalog_property_dryrun_contract *contract)
{
    volatile const uint32_t *words = (volatile const uint32_t *)(uintptr_t)contract;
    uint32_t count = (uint32_t)(offsetof(struct stage85_xnu_iokit_catalog_property_dryrun_contract, checksum) /
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

static void stage85_iokit_catalog_property_dryrun_log(
    const struct stage85_xnu_iokit_catalog_property_dryrun_contract *contract)
{
    xnu_log_kv32("stage85_xnu_iokit_catalog_property_dryrun_contract_status", contract->status);
    xnu_log_kv32("stage85_xnu_iokit_catalog_property_dryrun_contract_required_mask", contract->required_mask);
    xnu_log_kv32("stage85_xnu_iokit_catalog_property_dryrun_contract_satisfied_mask", contract->satisfied_mask);
    xnu_log_kv32("stage85_xnu_iokit_catalog_property_dryrun_contract_failure_mask", contract->failure_mask);
    xnu_log_kv32("stage85_xnu_iokit_catalog_property_dryrun_contract_checksum", contract->checksum);
    xnu_log_kv32("stage85_xnu_iokit_catalog_property_source_provider_status", contract->source_provider_status);
    xnu_log_kv32("stage85_xnu_iokit_catalog_property_source_provider_satisfied_mask", contract->source_provider_satisfied_mask);
    xnu_log_kv32("stage85_xnu_iokit_catalog_property_source_registry_status", contract->source_registry_status);
    xnu_log_kv32("stage85_xnu_iokit_catalog_property_apple_dt_semantic_mask", contract->apple_dt_semantic_mask);
    xnu_log_kv32("stage85_xnu_iokit_catalog_property_root_props", contract->catalog_root_prop_count);
    xnu_log_kv32("stage85_xnu_iokit_catalog_property_catalog_version", contract->catalog_version);
    xnu_log_kv32("stage85_xnu_iokit_catalog_property_catalog_local_only", contract->catalog_local_only);
    xnu_log_kv32("stage85_xnu_iokit_catalog_property_platform_props", contract->platform_personality_prop_count);
    xnu_log_kv32("stage85_xnu_iokit_catalog_property_interrupt_props", contract->interrupt_personality_prop_count);
    xnu_log_kv32("stage85_xnu_iokit_catalog_property_timer_props", contract->timer_personality_prop_count);
    xnu_log_kv32("stage85_xnu_iokit_catalog_property_cpu_props", contract->cpu_personality_prop_count);
    xnu_log_kv32("stage85_xnu_iokit_catalog_property_rejected_props", contract->rejected_personality_prop_count);
    xnu_log_kv32("stage85_xnu_iokit_catalog_property_source_selected_mask", contract->source_selected_service_mask);
    xnu_log_kv32("stage85_xnu_iokit_catalog_property_source_rejected_mask", contract->source_rejected_service_mask);
    xnu_log_kv32("stage85_xnu_iokit_catalog_property_source_fact_mask", contract->source_service_fact_mask);
    xnu_log_kv32("stage85_xnu_iokit_catalog_property_source_parent_mask", contract->source_parent_match_mask);
    xnu_log_kv32("stage85_xnu_iokit_catalog_property_source_dependency_mask", contract->source_dependency_mask);
    xnu_log_kv32("stage85_xnu_iokit_catalog_property_source_publish_order_mask", contract->source_publish_order_mask);
    xnu_log_kv32("stage85_xnu_iokit_catalog_property_name_match_mask", contract->personality_name_match_mask);
    xnu_log_kv32("stage85_xnu_iokit_catalog_property_bundle_match_mask", contract->bundle_identifier_match_mask);
    xnu_log_kv32("stage85_xnu_iokit_catalog_property_catalog_marker_mask", contract->catalog_marker_match_mask);
    xnu_log_kv32("stage85_xnu_iokit_catalog_property_provider_class_match_mask", contract->provider_class_match_mask);
    xnu_log_kv32("stage85_xnu_iokit_catalog_property_category_match_mask", contract->category_match_mask);
    xnu_log_kv32("stage85_xnu_iokit_catalog_property_compatible_match_mask", contract->compatible_match_mask);
    xnu_log_kv32("stage85_xnu_iokit_catalog_property_probe_score_match_mask", contract->probe_score_match_mask);
    xnu_log_kv32("stage85_xnu_iokit_catalog_property_local_only_match_mask", contract->local_only_match_mask);
    xnu_log_kv32("stage85_xnu_iokit_catalog_property_selected_personality_mask", contract->selected_personality_mask);
    xnu_log_kv32("stage85_xnu_iokit_catalog_property_rejected_personality_mask", contract->rejected_personality_mask);
    xnu_log_kv32("stage85_xnu_iokit_catalog_property_matrix", contract->catalog_property_matrix);
    xnu_log_kv32("stage85_xnu_iokit_catalog_property_platform_ordinal", contract->platform_personality_ordinal);
    xnu_log_kv32("stage85_xnu_iokit_catalog_property_interrupt_ordinal", contract->interrupt_personality_ordinal);
    xnu_log_kv32("stage85_xnu_iokit_catalog_property_timer_ordinal", contract->timer_personality_ordinal);
    xnu_log_kv32("stage85_xnu_iokit_catalog_property_cpu_ordinal", contract->cpu_personality_ordinal);
    xnu_log_kv32("stage85_xnu_iokit_catalog_property_rejected_ordinal", contract->rejected_personality_ordinal);
    xnu_log_kv32("stage85_xnu_iokit_catalog_property_order_checksum", contract->personality_order_checksum);
    xnu_log_kv32("stage85_xnu_iokit_catalog_property_checksum", contract->personality_property_checksum);
    xnu_log_kv32("stage85_xnu_iokit_catalog_property_expected_checksum", contract->expected_property_checksum);
    xnu_log_kv32("stage85_xnu_iokit_catalog_property_catalog_count", contract->catalog_count);
    xnu_log_kv32("stage85_xnu_iokit_catalog_property_personality_candidate_count", contract->personality_candidate_count);
    xnu_log_kv32("stage85_xnu_iokit_catalog_property_selected_personality_count", contract->selected_personality_count);
    xnu_log_kv32("stage85_xnu_iokit_catalog_property_rejected_personality_count", contract->rejected_personality_count);
    xnu_log_kv32("stage85_xnu_iokit_catalog_property_dryrun_count", contract->property_dryrun_count);
    xnu_log_kv32("stage85_xnu_iokit_catalog_property_attach_deferred_count", contract->attach_deferred_count);
    xnu_log_kv32("stage85_xnu_iokit_catalog_property_start_deferred_count", contract->start_deferred_count);
    xnu_log_kv32("stage85_xnu_iokit_catalog_property_iokit_reference_mask", contract->iokit_reference_mask);
    xnu_log_kv32("stage85_xnu_iokit_catalog_property_iokit_runtime_blocked_mask", contract->iokit_runtime_blocked_mask);
    xnu_log_kv32("stage85_xnu_iokit_catalog_property_public_compile_count", contract->iokit_public_compile_count);
    xnu_log_kv32("stage85_xnu_iokit_catalog_property_public_link_count", contract->iokit_public_link_count);
    xnu_log_kv32("stage85_xnu_iokit_catalog_property_no_iokit_runtime_exec", contract->no_iokit_runtime_exec);
    xnu_log_kv32("stage85_xnu_iokit_catalog_property_no_catalog_runtime_exec", contract->no_catalog_runtime_exec);
    xnu_log_kv32("stage85_xnu_iokit_catalog_property_no_provider_runtime_exec", contract->no_provider_runtime_exec);
    xnu_log_kv32("stage85_xnu_iokit_catalog_property_no_platform_driver_exec", contract->no_platform_driver_exec);
    xnu_log_kv32("stage85_xnu_iokit_catalog_property_no_live_pmap_tables_installed", contract->no_live_pmap_tables_installed);
    xnu_log_kv32("stage85_xnu_iokit_catalog_property_proposed_workspace_written", contract->proposed_workspace_written);
    xnu_log_kv32("stage85_xnu_iokit_catalog_property_pmap_ttbr_written", contract->pmap_ttbr_written);
    xnu_log_kv32("stage85_xnu_iokit_catalog_property_pmap_tlbs_invalidated", contract->pmap_tlbs_invalidated);
    xnu_log_kv32("stage85_xnu_iokit_catalog_property_caches_changed", contract->caches_changed);
    xnu_log_kv32("stage85_xnu_iokit_catalog_property_persistent_write_attempted", contract->persistent_write_attempted);
    xnu_log_kv32("stage85_xnu_iokit_catalog_property_platform_gap_mask", contract->proposed_platform_gap_mask);
}

int stage85_xnu_iokit_catalog_property_dryrun_contract_selftest(
    const struct stage85_loader_preflight *preflight)
{
    struct stage85_xnu_iokit_catalog_property_dryrun_contract *contract =
        &g_stage85_xnu_iokit_catalog_property_dryrun_contract;
    const struct boot_args *args = preflight ? (const struct boot_args *)(uintptr_t)preflight->boot_args_ptr :
        (const struct boot_args *)0;
    const void *root = args ? args->deviceTreeP : (const void *)0;
    const void *catalog_node;
    const void *platform_node;
    const void *interrupt_node;
    const void *timer_node;
    const void *cpu_node;
    const void *rejected_node;
    uint32_t source_ok;
    uint32_t catalog_ok;
    uint32_t personality_nodes_ok;
    uint32_t public_boundary_ok;
    uint32_t pmap_boundary_ok;

    memset(contract, 0, sizeof(*contract));
    contract->version = STAGE85_XNU_IOKIT_CATALOG_PROPERTY_DRYRUN_CONTRACT_VERSION;
    contract->size = sizeof(*contract);
    contract->status = STAGE85_STATUS_BASE;
    contract->required_mask = STAGE85_XNU_IOKIT_CATALOG_PROPERTY_DRYRUN_REQUIRED_MASK;

    if (!preflight || !args || !root) {
        contract->failure_mask = STAGE85_XNU_IOKIT_CATALOG_PROPERTY_DRYRUN_FAIL_SOURCE |
            STAGE85_XNU_IOKIT_CATALOG_PROPERTY_DRYRUN_FAIL_SAFETY_BOUNDARY;
        contract->checksum = stage85_iokit_catalog_property_dryrun_checksum(contract);
        stage85_iokit_catalog_property_dryrun_log(contract);
        return 0;
    }

    contract->source_provider_status = preflight->xnu_iokit_provider_plane_dryrun_contract_status;
    contract->source_provider_satisfied_mask = preflight->xnu_iokit_provider_plane_dryrun_contract_satisfied_mask;
    contract->source_provider_failure_mask = preflight->xnu_iokit_provider_plane_dryrun_contract_failure_mask;
    contract->source_provider_checksum = preflight->xnu_iokit_provider_plane_dryrun_contract_checksum;
    contract->source_registry_status = preflight->xnu_iokit_registry_service_dryrun_contract_status;
    contract->source_registry_satisfied_mask = preflight->xnu_iokit_registry_service_dryrun_contract_satisfied_mask;
    contract->source_registry_failure_mask = preflight->xnu_iokit_registry_service_dryrun_contract_failure_mask;
    contract->source_registry_checksum = preflight->xnu_iokit_registry_service_dryrun_contract_checksum;
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

    contract->source_selected_service_mask = preflight->xnu_iokit_provider_plane_dryrun_contract.provider_published_service_mask;
    contract->source_rejected_service_mask = preflight->xnu_iokit_provider_plane_dryrun_contract.provider_unpublished_service_mask;
    contract->source_service_fact_mask = preflight->xnu_iokit_provider_plane_dryrun_contract.service_fact_mask;
    contract->source_parent_match_mask = preflight->xnu_iokit_provider_plane_dryrun_contract.provider_parent_match_mask;
    contract->source_dependency_mask = preflight->xnu_iokit_provider_plane_dryrun_contract.provider_dependency_mask;
    contract->source_publish_order_mask = preflight->xnu_iokit_provider_plane_dryrun_contract.provider_publish_order_mask;

    contract->catalog_root_node_ptr = (uint32_t)(uintptr_t)apple_dt_find_child(args->deviceTreeP,
                                                                               args->deviceTreeLength,
                                                                               root,
                                                                               "iokit-catalog-property-dryrun");
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

    catalog_node = (const void *)(uintptr_t)contract->catalog_root_node_ptr;
    platform_node = (const void *)(uintptr_t)contract->platform_personality_node_ptr;
    interrupt_node = (const void *)(uintptr_t)contract->interrupt_personality_node_ptr;
    timer_node = (const void *)(uintptr_t)contract->timer_personality_node_ptr;
    cpu_node = (const void *)(uintptr_t)contract->cpu_personality_node_ptr;
    rejected_node = (const void *)(uintptr_t)contract->rejected_personality_node_ptr;

    contract->catalog_root_prop_count = apple_dt_node_prop_count(args->deviceTreeP, args->deviceTreeLength,
                                                                 catalog_node);
    contract->catalog_name_present = string_prop_equals(args, catalog_node, "name", "iokit-catalog-property-dryrun");
    contract->catalog_version = apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength,
                                                      catalog_node, "catalog-version", 0u);
    contract->catalog_local_only = apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength,
                                                         catalog_node, "stage-owned-local-only", 0u);
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

    if (string_prop_equals(args, platform_node, "name", "stage85-platform-personality")) {
        contract->personality_name_match_mask |= STAGE85_XNU_IOKIT_REGISTRY_SERVICE_PLATFORM_BIT;
    }
    if (string_prop_equals(args, interrupt_node, "name", "stage85-interrupt-personality")) {
        contract->personality_name_match_mask |= STAGE85_XNU_IOKIT_REGISTRY_SERVICE_INTERRUPT_BIT;
    }
    if (string_prop_equals(args, timer_node, "name", "stage85-timer-personality")) {
        contract->personality_name_match_mask |= STAGE85_XNU_IOKIT_REGISTRY_SERVICE_TIMER_BIT;
    }
    if (string_prop_equals(args, cpu_node, "name", "stage85-cpu-personality")) {
        contract->personality_name_match_mask |= STAGE85_XNU_IOKIT_REGISTRY_SERVICE_CPU_BIT;
    }

    if (string_prop_equals(args, platform_node, "CFBundleIdentifier", "com.mi4ios6.stage85.localcatalog")) {
        contract->bundle_identifier_match_mask |= STAGE85_XNU_IOKIT_REGISTRY_SERVICE_PLATFORM_BIT;
    }
    if (string_prop_equals(args, interrupt_node, "CFBundleIdentifier", "com.mi4ios6.stage85.localcatalog")) {
        contract->bundle_identifier_match_mask |= STAGE85_XNU_IOKIT_REGISTRY_SERVICE_INTERRUPT_BIT;
    }
    if (string_prop_equals(args, timer_node, "CFBundleIdentifier", "com.mi4ios6.stage85.localcatalog")) {
        contract->bundle_identifier_match_mask |= STAGE85_XNU_IOKIT_REGISTRY_SERVICE_TIMER_BIT;
    }
    if (string_prop_equals(args, cpu_node, "CFBundleIdentifier", "com.mi4ios6.stage85.localcatalog")) {
        contract->bundle_identifier_match_mask |= STAGE85_XNU_IOKIT_REGISTRY_SERVICE_CPU_BIT;
    }

    if (u32_prop_equals(args, platform_node, "catalog-property-dryrun", 1u) &&
        u32_prop_equals(args, platform_node, "provider-plane-published", 1u)) {
        contract->catalog_marker_match_mask |= STAGE85_XNU_IOKIT_REGISTRY_SERVICE_PLATFORM_BIT;
    }
    if (u32_prop_equals(args, interrupt_node, "catalog-property-dryrun", 1u) &&
        u32_prop_equals(args, interrupt_node, "provider-plane-published", 1u)) {
        contract->catalog_marker_match_mask |= STAGE85_XNU_IOKIT_REGISTRY_SERVICE_INTERRUPT_BIT;
    }
    if (u32_prop_equals(args, timer_node, "catalog-property-dryrun", 1u) &&
        u32_prop_equals(args, timer_node, "provider-plane-published", 1u)) {
        contract->catalog_marker_match_mask |= STAGE85_XNU_IOKIT_REGISTRY_SERVICE_TIMER_BIT;
    }
    if (u32_prop_equals(args, cpu_node, "catalog-property-dryrun", 1u) &&
        u32_prop_equals(args, cpu_node, "provider-plane-published", 1u)) {
        contract->catalog_marker_match_mask |= STAGE85_XNU_IOKIT_REGISTRY_SERVICE_CPU_BIT;
    }

    if (string_prop_equals(args, platform_node, "IOProviderClass", "IOPlatformExpertDevice")) {
        contract->provider_class_match_mask |= STAGE85_XNU_IOKIT_REGISTRY_SERVICE_PLATFORM_BIT;
    }
    if (string_prop_equals(args, interrupt_node, "IOProviderClass", "IOPlatformExpertDevice")) {
        contract->provider_class_match_mask |= STAGE85_XNU_IOKIT_REGISTRY_SERVICE_INTERRUPT_BIT;
    }
    if (string_prop_equals(args, timer_node, "IOProviderClass", "IOPlatformExpertDevice")) {
        contract->provider_class_match_mask |= STAGE85_XNU_IOKIT_REGISTRY_SERVICE_TIMER_BIT;
    }
    if (string_prop_equals(args, cpu_node, "IOProviderClass", "IOPlatformExpertDevice")) {
        contract->provider_class_match_mask |= STAGE85_XNU_IOKIT_REGISTRY_SERVICE_CPU_BIT;
    }

    if (string_prop_equals(args, platform_node, "IOMatchCategory", "Stage84LocalPlatformScaffold")) {
        contract->category_match_mask |= STAGE85_XNU_IOKIT_REGISTRY_SERVICE_PLATFORM_BIT;
    }
    if (string_prop_equals(args, interrupt_node, "IOMatchCategory", "Stage84LocalInterruptService")) {
        contract->category_match_mask |= STAGE85_XNU_IOKIT_REGISTRY_SERVICE_INTERRUPT_BIT;
    }
    if (string_prop_equals(args, timer_node, "IOMatchCategory", "Stage84LocalTimerService")) {
        contract->category_match_mask |= STAGE85_XNU_IOKIT_REGISTRY_SERVICE_TIMER_BIT;
    }
    if (string_prop_equals(args, cpu_node, "IOMatchCategory", "Stage84LocalCPUService")) {
        contract->category_match_mask |= STAGE85_XNU_IOKIT_REGISTRY_SERVICE_CPU_BIT;
    }

    if (string_prop_equals(args, platform_node, "compatible", "qcom,msm8974-cancro-stage85")) {
        contract->compatible_match_mask |= STAGE85_XNU_IOKIT_REGISTRY_SERVICE_PLATFORM_BIT;
    }
    if (string_prop_equals(args, interrupt_node, "compatible", "qcom,msm8974-gic-stage85")) {
        contract->compatible_match_mask |= STAGE85_XNU_IOKIT_REGISTRY_SERVICE_INTERRUPT_BIT;
    }
    if (string_prop_equals(args, timer_node, "compatible", "qcom,msm8974-timer-stage85")) {
        contract->compatible_match_mask |= STAGE85_XNU_IOKIT_REGISTRY_SERVICE_TIMER_BIT;
    }
    if (string_prop_equals(args, cpu_node, "compatible", "qcom,msm8974-cpu-stage85")) {
        contract->compatible_match_mask |= STAGE85_XNU_IOKIT_REGISTRY_SERVICE_CPU_BIT;
    }

    if (u32_prop_equals(args, platform_node, "IOProbeScore", STAGE85_XNU_IOKIT_REGISTRY_SERVICE_PLATFORM_PROBE_SCORE)) {
        contract->probe_score_match_mask |= STAGE85_XNU_IOKIT_REGISTRY_SERVICE_PLATFORM_BIT;
    }
    if (u32_prop_equals(args, interrupt_node, "IOProbeScore", STAGE85_XNU_IOKIT_REGISTRY_SERVICE_INTERRUPT_PROBE_SCORE)) {
        contract->probe_score_match_mask |= STAGE85_XNU_IOKIT_REGISTRY_SERVICE_INTERRUPT_BIT;
    }
    if (u32_prop_equals(args, timer_node, "IOProbeScore", STAGE85_XNU_IOKIT_REGISTRY_SERVICE_TIMER_PROBE_SCORE)) {
        contract->probe_score_match_mask |= STAGE85_XNU_IOKIT_REGISTRY_SERVICE_TIMER_BIT;
    }
    if (u32_prop_equals(args, cpu_node, "IOProbeScore", STAGE85_XNU_IOKIT_REGISTRY_SERVICE_CPU_PROBE_SCORE)) {
        contract->probe_score_match_mask |= STAGE85_XNU_IOKIT_REGISTRY_SERVICE_CPU_BIT;
    }

    if (u32_prop_equals(args, platform_node, "stage-owned-local-only", 1u)) {
        contract->local_only_match_mask |= STAGE85_XNU_IOKIT_REGISTRY_SERVICE_PLATFORM_BIT;
    }
    if (u32_prop_equals(args, interrupt_node, "stage-owned-local-only", 1u)) {
        contract->local_only_match_mask |= STAGE85_XNU_IOKIT_REGISTRY_SERVICE_INTERRUPT_BIT;
    }
    if (u32_prop_equals(args, timer_node, "stage-owned-local-only", 1u)) {
        contract->local_only_match_mask |= STAGE85_XNU_IOKIT_REGISTRY_SERVICE_TIMER_BIT;
    }
    if (u32_prop_equals(args, cpu_node, "stage-owned-local-only", 1u)) {
        contract->local_only_match_mask |= STAGE85_XNU_IOKIT_REGISTRY_SERVICE_CPU_BIT;
    }

    contract->selected_personality_mask = contract->personality_name_match_mask &
        contract->bundle_identifier_match_mask & contract->catalog_marker_match_mask &
        contract->provider_class_match_mask & contract->category_match_mask &
        contract->compatible_match_mask & contract->probe_score_match_mask &
        contract->local_only_match_mask & contract->source_selected_service_mask;
    if (string_prop_equals(args, rejected_node, "name", "stage85-rejected-personality") &&
        string_prop_equals(args, rejected_node, "CFBundleIdentifier", "com.mi4ios6.stage85.localcatalog") &&
        u32_prop_equals(args, rejected_node, "catalog-property-dryrun", 1u) &&
        u32_prop_equals(args, rejected_node, "provider-plane-published", 0u) &&
        u32_prop_equals(args, rejected_node, "rejected-candidate", 1u) &&
        u32_prop_equals(args, rejected_node, "stage-owned-local-only", 1u) &&
        u32_prop_equals(args, rejected_node, "IOProbeScore", 0u) &&
        string_prop_equals(args, rejected_node, "IOProviderClass", "IOUnknownProvider")) {
        contract->rejected_personality_mask |= STAGE85_XNU_IOKIT_REGISTRY_SERVICE_REJECTED_BIT;
    }
    contract->catalog_property_matrix = contract->selected_personality_mask | contract->rejected_personality_mask;

    contract->platform_personality_ordinal = STAGE85_XNU_IOKIT_CATALOG_PROPERTY_PLATFORM_ORDINAL;
    contract->interrupt_personality_ordinal = STAGE85_XNU_IOKIT_CATALOG_PROPERTY_INTERRUPT_ORDINAL;
    contract->timer_personality_ordinal = STAGE85_XNU_IOKIT_CATALOG_PROPERTY_TIMER_ORDINAL;
    contract->cpu_personality_ordinal = STAGE85_XNU_IOKIT_CATALOG_PROPERTY_CPU_ORDINAL;
    contract->rejected_personality_ordinal = STAGE85_XNU_IOKIT_CATALOG_PROPERTY_REJECTED_ORDINAL;
    contract->personality_order_checksum = contract->platform_personality_ordinal ^
        contract->interrupt_personality_ordinal ^ contract->timer_personality_ordinal ^
        contract->cpu_personality_ordinal ^ contract->rejected_personality_ordinal;

    contract->catalog_count = (contract->catalog_root_node_ptr != 0u) ?
        STAGE85_XNU_IOKIT_CATALOG_PROPERTY_CATALOG_COUNT : 0u;
    contract->personality_candidate_count = STAGE85_XNU_IOKIT_CATALOG_PROPERTY_PERSONALITY_COUNT;
    contract->selected_personality_count = (contract->selected_personality_mask ==
                                            STAGE85_XNU_IOKIT_REGISTRY_SERVICE_SELECTED_REQUIRED) ?
        STAGE85_XNU_IOKIT_CATALOG_PROPERTY_SELECTED_COUNT : 0u;
    contract->rejected_personality_count = (contract->rejected_personality_mask ==
                                            STAGE85_XNU_IOKIT_REGISTRY_SERVICE_REJECTED_REQUIRED) ?
        STAGE85_XNU_IOKIT_CATALOG_PROPERTY_REJECTED_COUNT : 0u;
    contract->property_dryrun_count = contract->selected_personality_count;
    contract->attach_deferred_count = contract->selected_personality_count;
    contract->start_deferred_count = contract->selected_personality_count;
    contract->attach_deferred = 1u;
    contract->start_deferred = 1u;
    contract->personality_property_checksum = contract->catalog_version ^ contract->selected_personality_mask ^
        contract->rejected_personality_mask ^ contract->source_selected_service_mask ^ contract->source_rejected_service_mask ^
        contract->source_service_fact_mask ^ contract->source_parent_match_mask ^ contract->source_dependency_mask ^
        contract->source_publish_order_mask ^ contract->personality_order_checksum ^ contract->catalog_count ^
        contract->personality_candidate_count ^ contract->selected_personality_count ^ contract->rejected_personality_count ^
        contract->property_dryrun_count;
    contract->expected_property_checksum = 0xfffffffbu;

    contract->iokit_reference_mask = preflight->xnu_iokit_provider_plane_dryrun_contract.iokit_reference_mask;
    contract->iokit_runtime_blocked_mask = preflight->xnu_iokit_provider_plane_dryrun_contract.iokit_runtime_blocked_mask;
    contract->iokit_reference_count = preflight->xnu_iokit_provider_plane_dryrun_contract.iokit_reference_count;
    contract->iokit_public_compile_count = preflight->xnu_iokit_provider_plane_dryrun_contract.iokit_public_compile_count;
    contract->iokit_public_link_count = preflight->xnu_iokit_provider_plane_dryrun_contract.iokit_public_link_count;
    contract->iokit_reference_only = preflight->xnu_iokit_provider_plane_dryrun_contract.iokit_reference_only;
    contract->no_iokit_runtime_exec = preflight->xnu_iokit_provider_plane_dryrun_contract.no_iokit_runtime_exec;
    contract->no_catalog_runtime_exec = 1u;
    contract->no_provider_runtime_exec = preflight->xnu_iokit_provider_plane_dryrun_contract.no_provider_runtime_exec;
    contract->no_platform_driver_exec = preflight->xnu_iokit_provider_plane_dryrun_contract.no_platform_driver_exec;
    contract->no_public_xnu_exec = preflight->xnu_iokit_provider_plane_dryrun_contract.no_public_xnu_exec;
    contract->no_platform_runtime_exec = preflight->xnu_iokit_provider_plane_dryrun_contract.no_platform_runtime_exec;
    contract->no_public_pmap_exec = preflight->xnu_iokit_provider_plane_dryrun_contract.no_public_pmap_exec;
    contract->no_live_pmap_tables_installed = preflight->xnu_iokit_provider_plane_dryrun_contract.no_live_pmap_tables_installed;
    contract->proposed_workspace_written = preflight->xnu_iokit_provider_plane_dryrun_contract.proposed_workspace_written;
    contract->pmap_ttbr_written = preflight->xnu_iokit_provider_plane_dryrun_contract.pmap_ttbr_written;
    contract->pmap_ttbcr_written = preflight->xnu_iokit_provider_plane_dryrun_contract.pmap_ttbcr_written;
    contract->pmap_dacr_written = preflight->xnu_iokit_provider_plane_dryrun_contract.pmap_dacr_written;
    contract->pmap_sctlr_written = preflight->xnu_iokit_provider_plane_dryrun_contract.pmap_sctlr_written;
    contract->pmap_tlbs_invalidated = preflight->xnu_iokit_provider_plane_dryrun_contract.pmap_tlbs_invalidated;
    contract->caches_changed = preflight->xnu_iokit_provider_plane_dryrun_contract.caches_changed;
    contract->persistent_write_attempted = preflight->xnu_iokit_provider_plane_dryrun_contract.persistent_write_attempted;
    contract->xnu_start_executed = preflight->xnu_iokit_provider_plane_dryrun_contract.xnu_start_executed;
    contract->generated_macho_executed = preflight->xnu_iokit_provider_plane_dryrun_contract.generated_macho_executed;
    contract->proposed_platform_gap_mask = preflight->platform_gap_mask;
    contract->proposed_pexpert_gap_mask = preflight->pexpert_gap_mask;
    contract->local_only = 1u;
    contract->fail_closed = 1u;

    source_ok = (contract->source_provider_status == STAGE85_STATUS_OK &&
                 contract->source_provider_satisfied_mask == STAGE85_XNU_IOKIT_PROVIDER_PLANE_DRYRUN_REQUIRED_MASK &&
                 contract->source_provider_failure_mask == 0u &&
                 contract->source_registry_status == STAGE85_STATUS_OK &&
                 contract->source_registry_satisfied_mask == STAGE85_XNU_IOKIT_REGISTRY_SERVICE_DRYRUN_REQUIRED_MASK &&
                 contract->source_registry_failure_mask == 0u &&
                 contract->source_match_status == STAGE85_STATUS_OK &&
                 contract->source_scaffold_status == STAGE85_STATUS_OK &&
                 contract->source_pexpert_status == STAGE85_STATUS_OK &&
                 contract->source_pmap_transition_status == STAGE85_STATUS_OK &&
                 contract->source_compile_graph_status == STAGE85_STATUS_OK &&
                 contract->source_object_subset_status == STAGE85_STATUS_OK &&
                 contract->source_link_status == STAGE85_STATUS_OK &&
                 contract->source_loader_safety_mask == contract->source_loader_safety_required_mask) ? 1u : 0u;
    if (source_ok) {
        contract->satisfied_mask |= STAGE85_XNU_IOKIT_CATALOG_PROPERTY_DRYRUN_SAT_SOURCE_PROVIDER;
    } else {
        contract->failure_mask |= STAGE85_XNU_IOKIT_CATALOG_PROPERTY_DRYRUN_FAIL_SOURCE;
    }

    if (contract->apple_dt_semantic_mask == STAGE85_DT_READY_REQUIRED) {
        contract->satisfied_mask |= STAGE85_XNU_IOKIT_CATALOG_PROPERTY_DRYRUN_SAT_APPLE_DT;
    } else {
        contract->failure_mask |= STAGE85_XNU_IOKIT_CATALOG_PROPERTY_DRYRUN_FAIL_DT;
    }

    catalog_ok = (contract->catalog_root_node_ptr != 0u && contract->catalog_root_prop_count >= 6u &&
                  contract->catalog_name_present == 1u &&
                  contract->catalog_version == STAGE85_XNU_IOKIT_CATALOG_PROPERTY_CATALOG_VERSION &&
                  contract->catalog_local_only == 1u && contract->catalog_count == 1u) ? 1u : 0u;
    if (catalog_ok) {
        contract->satisfied_mask |= STAGE85_XNU_IOKIT_CATALOG_PROPERTY_DRYRUN_SAT_CATALOG_ROOT;
    } else {
        contract->failure_mask |= STAGE85_XNU_IOKIT_CATALOG_PROPERTY_DRYRUN_FAIL_CATALOG;
    }

    personality_nodes_ok = (contract->platform_personality_node_ptr != 0u && contract->platform_personality_prop_count >= 11u &&
                            contract->interrupt_personality_node_ptr != 0u && contract->interrupt_personality_prop_count >= 11u &&
                            contract->timer_personality_node_ptr != 0u && contract->timer_personality_prop_count >= 11u &&
                            contract->cpu_personality_node_ptr != 0u && contract->cpu_personality_prop_count >= 11u &&
                            contract->rejected_personality_node_ptr != 0u && contract->rejected_personality_prop_count >= 10u) ? 1u : 0u;
    if (personality_nodes_ok) {
        contract->satisfied_mask |= STAGE85_XNU_IOKIT_CATALOG_PROPERTY_DRYRUN_SAT_PERSONALITY_NODE_SET;
    } else {
        contract->failure_mask |= STAGE85_XNU_IOKIT_CATALOG_PROPERTY_DRYRUN_FAIL_PERSONALITY;
    }

    if (contract->personality_name_match_mask == STAGE85_XNU_IOKIT_REGISTRY_SERVICE_SELECTED_REQUIRED) {
        contract->satisfied_mask |= STAGE85_XNU_IOKIT_CATALOG_PROPERTY_DRYRUN_SAT_PERSONALITY_NAME_MATRIX;
    } else {
        contract->failure_mask |= STAGE85_XNU_IOKIT_CATALOG_PROPERTY_DRYRUN_FAIL_PERSONALITY;
    }
    if (contract->bundle_identifier_match_mask == STAGE85_XNU_IOKIT_REGISTRY_SERVICE_SELECTED_REQUIRED) {
        contract->satisfied_mask |= STAGE85_XNU_IOKIT_CATALOG_PROPERTY_DRYRUN_SAT_BUNDLE_IDENTIFIER_MATRIX;
    } else {
        contract->failure_mask |= STAGE85_XNU_IOKIT_CATALOG_PROPERTY_DRYRUN_FAIL_PERSONALITY;
    }
    if (contract->catalog_marker_match_mask == STAGE85_XNU_IOKIT_REGISTRY_SERVICE_SELECTED_REQUIRED) {
        contract->satisfied_mask |= STAGE85_XNU_IOKIT_CATALOG_PROPERTY_DRYRUN_SAT_CATALOG_MARKER_MATRIX;
    } else {
        contract->failure_mask |= STAGE85_XNU_IOKIT_CATALOG_PROPERTY_DRYRUN_FAIL_PERSONALITY;
    }
    if (contract->provider_class_match_mask == STAGE85_XNU_IOKIT_REGISTRY_SERVICE_SELECTED_REQUIRED &&
        contract->source_parent_match_mask == STAGE85_XNU_IOKIT_REGISTRY_SERVICE_SELECTED_REQUIRED) {
        contract->satisfied_mask |= STAGE85_XNU_IOKIT_CATALOG_PROPERTY_DRYRUN_SAT_PROVIDER_MATRIX;
    } else {
        contract->failure_mask |= STAGE85_XNU_IOKIT_CATALOG_PROPERTY_DRYRUN_FAIL_MATCH;
    }
    if (contract->category_match_mask == STAGE85_XNU_IOKIT_REGISTRY_SERVICE_SELECTED_REQUIRED) {
        contract->satisfied_mask |= STAGE85_XNU_IOKIT_CATALOG_PROPERTY_DRYRUN_SAT_CATEGORY_MATRIX;
    } else {
        contract->failure_mask |= STAGE85_XNU_IOKIT_CATALOG_PROPERTY_DRYRUN_FAIL_MATCH;
    }
    if (contract->compatible_match_mask == STAGE85_XNU_IOKIT_REGISTRY_SERVICE_SELECTED_REQUIRED) {
        contract->satisfied_mask |= STAGE85_XNU_IOKIT_CATALOG_PROPERTY_DRYRUN_SAT_COMPATIBLE_MATRIX;
    } else {
        contract->failure_mask |= STAGE85_XNU_IOKIT_CATALOG_PROPERTY_DRYRUN_FAIL_MATCH;
    }
    if (contract->probe_score_match_mask == STAGE85_XNU_IOKIT_REGISTRY_SERVICE_SELECTED_REQUIRED) {
        contract->satisfied_mask |= STAGE85_XNU_IOKIT_CATALOG_PROPERTY_DRYRUN_SAT_PROBE_SCORE_MATRIX;
    } else {
        contract->failure_mask |= STAGE85_XNU_IOKIT_CATALOG_PROPERTY_DRYRUN_FAIL_MATCH;
    }
    if (contract->local_only_match_mask == STAGE85_XNU_IOKIT_REGISTRY_SERVICE_SELECTED_REQUIRED) {
        contract->satisfied_mask |= STAGE85_XNU_IOKIT_CATALOG_PROPERTY_DRYRUN_SAT_LOCAL_ONLY_MATRIX;
    } else {
        contract->failure_mask |= STAGE85_XNU_IOKIT_CATALOG_PROPERTY_DRYRUN_FAIL_PERSONALITY;
    }
    if (contract->source_service_fact_mask == STAGE85_XNU_IOKIT_REGISTRY_SERVICE_SELECTED_REQUIRED &&
        contract->source_dependency_mask == STAGE85_XNU_IOKIT_REGISTRY_SERVICE_SELECTED_REQUIRED &&
        contract->source_publish_order_mask == STAGE85_XNU_IOKIT_REGISTRY_SERVICE_SELECTED_REQUIRED) {
        contract->satisfied_mask |= STAGE85_XNU_IOKIT_CATALOG_PROPERTY_DRYRUN_SAT_SERVICE_FACTS;
    } else {
        contract->failure_mask |= STAGE85_XNU_IOKIT_CATALOG_PROPERTY_DRYRUN_FAIL_FACTS;
    }
    if (contract->rejected_personality_mask == STAGE85_XNU_IOKIT_REGISTRY_SERVICE_REJECTED_REQUIRED &&
        contract->source_rejected_service_mask == STAGE85_XNU_IOKIT_REGISTRY_SERVICE_REJECTED_REQUIRED &&
        contract->rejected_personality_ordinal == STAGE85_XNU_IOKIT_CATALOG_PROPERTY_REJECTED_ORDINAL) {
        contract->satisfied_mask |= STAGE85_XNU_IOKIT_CATALOG_PROPERTY_DRYRUN_SAT_REJECT_PERSONALITY;
    } else {
        contract->failure_mask |= STAGE85_XNU_IOKIT_CATALOG_PROPERTY_DRYRUN_FAIL_PERSONALITY;
    }
    if (contract->catalog_count == STAGE85_XNU_IOKIT_CATALOG_PROPERTY_CATALOG_COUNT &&
        contract->personality_candidate_count == STAGE85_XNU_IOKIT_CATALOG_PROPERTY_PERSONALITY_COUNT &&
        contract->selected_personality_count == STAGE85_XNU_IOKIT_CATALOG_PROPERTY_SELECTED_COUNT &&
        contract->rejected_personality_count == STAGE85_XNU_IOKIT_CATALOG_PROPERTY_REJECTED_COUNT &&
        contract->property_dryrun_count == STAGE85_XNU_IOKIT_CATALOG_PROPERTY_SELECTED_COUNT &&
        contract->catalog_property_matrix == (STAGE85_XNU_IOKIT_REGISTRY_SERVICE_SELECTED_REQUIRED |
                                              STAGE85_XNU_IOKIT_REGISTRY_SERVICE_REJECTED_REQUIRED)) {
        contract->satisfied_mask |= STAGE85_XNU_IOKIT_CATALOG_PROPERTY_DRYRUN_SAT_DRYRUN_COUNTS;
    } else {
        contract->failure_mask |= STAGE85_XNU_IOKIT_CATALOG_PROPERTY_DRYRUN_FAIL_COUNTS;
    }
    if (contract->personality_order_checksum == 0xffffffffu &&
        contract->personality_property_checksum == contract->expected_property_checksum) {
        contract->satisfied_mask |= STAGE85_XNU_IOKIT_CATALOG_PROPERTY_DRYRUN_SAT_PROPERTY_CHECKSUM;
    } else {
        contract->failure_mask |= STAGE85_XNU_IOKIT_CATALOG_PROPERTY_DRYRUN_FAIL_CHECKSUM;
    }
    if (contract->attach_deferred == 1u && contract->start_deferred == 1u &&
        contract->attach_deferred_count == STAGE85_XNU_IOKIT_CATALOG_PROPERTY_SELECTED_COUNT &&
        contract->start_deferred_count == STAGE85_XNU_IOKIT_CATALOG_PROPERTY_SELECTED_COUNT) {
        contract->satisfied_mask |= STAGE85_XNU_IOKIT_CATALOG_PROPERTY_DRYRUN_SAT_ATTACH_START_DEFERRED;
    } else {
        contract->failure_mask |= STAGE85_XNU_IOKIT_CATALOG_PROPERTY_DRYRUN_FAIL_SAFETY_BOUNDARY;
    }

    if (contract->iokit_reference_mask == STAGE85_XNU_IOKIT_REFERENCE_REQUIRED &&
        contract->iokit_runtime_blocked_mask == STAGE85_XNU_IOKIT_REFERENCE_REQUIRED &&
        contract->iokit_reference_count >= 3u && contract->iokit_public_compile_count == 0u &&
        contract->iokit_public_link_count == 0u && contract->iokit_reference_only == 1u) {
        contract->satisfied_mask |= STAGE85_XNU_IOKIT_CATALOG_PROPERTY_DRYRUN_SAT_IOKIT_REFERENCE;
    } else {
        contract->failure_mask |= STAGE85_XNU_IOKIT_CATALOG_PROPERTY_DRYRUN_FAIL_PUBLIC_BOUNDARY;
    }

    public_boundary_ok = (contract->no_public_xnu_exec == 1u && contract->no_platform_runtime_exec == 1u &&
                          contract->no_iokit_runtime_exec == 1u && contract->no_catalog_runtime_exec == 1u &&
                          contract->no_provider_runtime_exec == 1u && contract->no_platform_driver_exec == 1u &&
                          contract->source_object_subset_status == STAGE85_STATUS_OK &&
                          contract->source_link_status == STAGE85_STATUS_OK) ? 1u : 0u;
    if (public_boundary_ok) {
        contract->satisfied_mask |= STAGE85_XNU_IOKIT_CATALOG_PROPERTY_DRYRUN_SAT_PUBLIC_BOUNDARY |
            STAGE85_XNU_IOKIT_CATALOG_PROPERTY_DRYRUN_SAT_NO_IOKIT_RUNTIME_EXEC |
            STAGE85_XNU_IOKIT_CATALOG_PROPERTY_DRYRUN_SAT_NO_CATALOG_RUNTIME_EXEC |
            STAGE85_XNU_IOKIT_CATALOG_PROPERTY_DRYRUN_SAT_NO_PLATFORM_DRIVER_EXEC |
            STAGE85_XNU_IOKIT_CATALOG_PROPERTY_DRYRUN_SAT_NO_PUBLIC_XNU_EXEC;
    } else {
        contract->failure_mask |= STAGE85_XNU_IOKIT_CATALOG_PROPERTY_DRYRUN_FAIL_PUBLIC_BOUNDARY;
    }

    if (contract->no_public_pmap_exec == 1u) {
        contract->satisfied_mask |= STAGE85_XNU_IOKIT_CATALOG_PROPERTY_DRYRUN_SAT_NO_PUBLIC_PMAP_EXEC;
    } else {
        contract->failure_mask |= STAGE85_XNU_IOKIT_CATALOG_PROPERTY_DRYRUN_FAIL_PMAP_BOUNDARY;
    }

    pmap_boundary_ok = (contract->no_live_pmap_tables_installed == 0u &&
                        contract->proposed_workspace_written == 0u &&
                        contract->pmap_ttbr_written == 0u && contract->pmap_ttbcr_written == 0u &&
                        contract->pmap_dacr_written == 0u && contract->pmap_sctlr_written == 0u &&
                        contract->pmap_tlbs_invalidated == 0u && contract->caches_changed == 0u) ? 1u : 0u;
    if (contract->no_live_pmap_tables_installed == 0u) {
        contract->satisfied_mask |= STAGE85_XNU_IOKIT_CATALOG_PROPERTY_DRYRUN_SAT_NO_LIVE_PMAP_INSTALL;
    }
    if (contract->proposed_workspace_written == 0u) {
        contract->satisfied_mask |= STAGE85_XNU_IOKIT_CATALOG_PROPERTY_DRYRUN_SAT_NO_PROPOSED_PMAP_WRITE;
    }
    if (contract->pmap_ttbr_written == 0u && contract->pmap_ttbcr_written == 0u &&
        contract->pmap_dacr_written == 0u && contract->pmap_sctlr_written == 0u) {
        contract->satisfied_mask |= STAGE85_XNU_IOKIT_CATALOG_PROPERTY_DRYRUN_SAT_NO_PMAP_CONTROL_WRITE;
    }
    if (contract->pmap_tlbs_invalidated == 0u && contract->caches_changed == 0u) {
        contract->satisfied_mask |= STAGE85_XNU_IOKIT_CATALOG_PROPERTY_DRYRUN_SAT_NO_TLB_CACHE_CHANGE;
    }
    if (pmap_boundary_ok && contract->source_pmap_transition_status == STAGE85_STATUS_OK) {
        contract->satisfied_mask |= STAGE85_XNU_IOKIT_CATALOG_PROPERTY_DRYRUN_SAT_PMAP_BOUNDARY;
    } else {
        contract->failure_mask |= STAGE85_XNU_IOKIT_CATALOG_PROPERTY_DRYRUN_FAIL_PMAP_BOUNDARY;
    }

    if (contract->xnu_start_executed == 0u && contract->generated_macho_executed == 0u) {
        contract->satisfied_mask |= STAGE85_XNU_IOKIT_CATALOG_PROPERTY_DRYRUN_SAT_NO_XNU_MACHO_EXEC;
    } else {
        contract->failure_mask |= STAGE85_XNU_IOKIT_CATALOG_PROPERTY_DRYRUN_FAIL_SAFETY_BOUNDARY;
    }

    if (contract->local_only == 1u && contract->fail_closed == 1u &&
        contract->persistent_write_attempted == 0u &&
        contract->proposed_platform_gap_mask == (STAGE85_PLATFORM_GAP_REQUIRED_RECORDED & ~STAGE85_PLATFORM_GAP_IOKIT_STACK) &&
        contract->proposed_pexpert_gap_mask == 0u &&
        contract->selected_personality_mask == STAGE85_XNU_IOKIT_REGISTRY_SERVICE_SELECTED_REQUIRED &&
        contract->rejected_personality_mask == STAGE85_XNU_IOKIT_REGISTRY_SERVICE_REJECTED_REQUIRED) {
        contract->satisfied_mask |= STAGE85_XNU_IOKIT_CATALOG_PROPERTY_DRYRUN_SAT_FAIL_CLOSED;
    } else {
        contract->failure_mask |= STAGE85_XNU_IOKIT_CATALOG_PROPERTY_DRYRUN_FAIL_ROLLUP |
            STAGE85_XNU_IOKIT_CATALOG_PROPERTY_DRYRUN_FAIL_SAFETY_BOUNDARY;
    }

    contract->checksum = stage85_iokit_catalog_property_dryrun_checksum(contract);
    if (contract->checksum != stage85_iokit_catalog_property_dryrun_checksum(contract)) {
        contract->failure_mask |= STAGE85_XNU_IOKIT_CATALOG_PROPERTY_DRYRUN_FAIL_CHECKSUM;
    }

    if (contract->satisfied_mask == contract->required_mask && contract->failure_mask == 0u) {
        contract->status = STAGE85_STATUS_OK;
    } else {
        contract->status = STAGE85_STATUS_FAIL(contract->failure_mask);
    }

    stage85_iokit_catalog_property_dryrun_log(contract);
    return contract->status == STAGE85_STATUS_OK;
}

const struct stage85_xnu_iokit_catalog_property_dryrun_contract *
stage85_xnu_iokit_catalog_property_dryrun_contract_result(void)
{
    return &g_stage85_xnu_iokit_catalog_property_dryrun_contract;
}
