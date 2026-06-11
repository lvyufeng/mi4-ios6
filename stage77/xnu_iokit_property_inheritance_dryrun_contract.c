#include "stage77.h"

static struct stage77_xnu_iokit_property_inheritance_dryrun_contract
    g_stage77_xnu_iokit_property_inheritance_dryrun_contract;

static uint32_t stage77_iokit_property_inheritance_dryrun_checksum(
    volatile const struct stage77_xnu_iokit_property_inheritance_dryrun_contract *contract)
{
    volatile const uint32_t *words = (volatile const uint32_t *)(uintptr_t)contract;
    uint32_t count = (uint32_t)(offsetof(struct stage77_xnu_iokit_property_inheritance_dryrun_contract,
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

static void stage77_iokit_property_inheritance_dryrun_log(
    const struct stage77_xnu_iokit_property_inheritance_dryrun_contract *contract)
{
    xnu_log_kv32("stage77_xnu_iokit_property_inheritance_dryrun_contract_status", contract->status);
    xnu_log_kv32("stage77_xnu_iokit_property_inheritance_dryrun_contract_required_mask", contract->required_mask);
    xnu_log_kv32("stage77_xnu_iokit_property_inheritance_dryrun_contract_satisfied_mask", contract->satisfied_mask);
    xnu_log_kv32("stage77_xnu_iokit_property_inheritance_dryrun_contract_failure_mask", contract->failure_mask);
    xnu_log_kv32("stage77_xnu_iokit_property_inheritance_dryrun_contract_checksum", contract->checksum);
    xnu_log_kv32("stage77_xnu_iokit_property_inheritance_source_catalog_status", contract->source_catalog_status);
    xnu_log_kv32("stage77_xnu_iokit_property_inheritance_source_catalog_satisfied_mask", contract->source_catalog_satisfied_mask);
    xnu_log_kv32("stage77_xnu_iokit_property_inheritance_source_catalog_failure_mask", contract->source_catalog_failure_mask);
    xnu_log_kv32("stage77_xnu_iokit_property_inheritance_source_catalog_property_checksum", contract->source_catalog_property_checksum);
    xnu_log_kv32("stage77_xnu_iokit_property_inheritance_apple_dt_semantic_mask", contract->apple_dt_semantic_mask);
    xnu_log_kv32("stage77_xnu_iokit_property_inheritance_platform_props", contract->platform_personality_prop_count);
    xnu_log_kv32("stage77_xnu_iokit_property_inheritance_interrupt_props", contract->interrupt_personality_prop_count);
    xnu_log_kv32("stage77_xnu_iokit_property_inheritance_timer_props", contract->timer_personality_prop_count);
    xnu_log_kv32("stage77_xnu_iokit_property_inheritance_cpu_props", contract->cpu_personality_prop_count);
    xnu_log_kv32("stage77_xnu_iokit_property_inheritance_rejected_props", contract->rejected_personality_prop_count);
    xnu_log_kv32("stage77_xnu_iokit_property_inheritance_source_selected_mask", contract->source_selected_personality_mask);
    xnu_log_kv32("stage77_xnu_iokit_property_inheritance_source_rejected_mask", contract->source_rejected_personality_mask);
    xnu_log_kv32("stage77_xnu_iokit_property_inheritance_registry_entry_marker_mask", contract->registry_entry_marker_mask);
    xnu_log_kv32("stage77_xnu_iokit_property_inheritance_marker_mask", contract->property_inheritance_marker_mask);
    xnu_log_kv32("stage77_xnu_iokit_property_inheritance_registry_entry_ordinal_mask", contract->registry_entry_ordinal_mask);
    xnu_log_kv32("stage77_xnu_iokit_property_inheritance_registry_plane_match_mask", contract->registry_plane_match_mask);
    xnu_log_kv32("stage77_xnu_iokit_property_inheritance_ioclass_mask", contract->inherited_ioclass_mask);
    xnu_log_kv32("stage77_xnu_iokit_property_inheritance_provider_class_mask", contract->inherited_provider_class_mask);
    xnu_log_kv32("stage77_xnu_iokit_property_inheritance_category_mask", contract->inherited_category_mask);
    xnu_log_kv32("stage77_xnu_iokit_property_inheritance_compatible_mask", contract->inherited_compatible_mask);
    xnu_log_kv32("stage77_xnu_iokit_property_inheritance_probe_score_mask", contract->inherited_probe_score_mask);
    xnu_log_kv32("stage77_xnu_iokit_property_inheritance_bundle_mask", contract->inherited_bundle_identifier_mask);
    xnu_log_kv32("stage77_xnu_iokit_property_inheritance_inherited_property_mask", contract->inherited_property_mask);
    xnu_log_kv32("stage77_xnu_iokit_property_inheritance_instance_source_bit_mask", contract->instance_source_bit_mask);
    xnu_log_kv32("stage77_xnu_iokit_property_inheritance_instance_publication_mask", contract->instance_publication_mask);
    xnu_log_kv32("stage77_xnu_iokit_property_inheritance_instance_local_only_mask", contract->instance_local_only_mask);
    xnu_log_kv32("stage77_xnu_iokit_property_inheritance_instance_property_mask", contract->instance_property_mask);
    xnu_log_kv32("stage77_xnu_iokit_property_inheritance_attach_deferred_mask", contract->attach_deferred_mask);
    xnu_log_kv32("stage77_xnu_iokit_property_inheritance_start_deferred_mask", contract->start_deferred_mask);
    xnu_log_kv32("stage77_xnu_iokit_property_inheritance_selected_registry_entry_mask", contract->selected_registry_entry_mask);
    xnu_log_kv32("stage77_xnu_iokit_property_inheritance_rejected_unattached_mask", contract->rejected_unattached_mask);
    xnu_log_kv32("stage77_xnu_iokit_property_inheritance_matrix", contract->property_inheritance_matrix);
    xnu_log_kv32("stage77_xnu_iokit_property_inheritance_platform_ordinal", contract->platform_registry_entry_ordinal);
    xnu_log_kv32("stage77_xnu_iokit_property_inheritance_interrupt_ordinal", contract->interrupt_registry_entry_ordinal);
    xnu_log_kv32("stage77_xnu_iokit_property_inheritance_timer_ordinal", contract->timer_registry_entry_ordinal);
    xnu_log_kv32("stage77_xnu_iokit_property_inheritance_cpu_ordinal", contract->cpu_registry_entry_ordinal);
    xnu_log_kv32("stage77_xnu_iokit_property_inheritance_rejected_ordinal", contract->rejected_registry_entry_ordinal);
    xnu_log_kv32("stage77_xnu_iokit_property_inheritance_order_checksum", contract->registry_entry_order_checksum);
    xnu_log_kv32("stage77_xnu_iokit_property_inheritance_registry_plane_hash", contract->registry_plane_hash);
    xnu_log_kv32("stage77_xnu_iokit_property_inheritance_checksum", contract->property_inheritance_checksum);
    xnu_log_kv32("stage77_xnu_iokit_property_inheritance_expected_checksum", contract->expected_property_inheritance_checksum);
    xnu_log_kv32("stage77_xnu_iokit_property_inheritance_candidate_count", contract->registry_entry_candidate_count);
    xnu_log_kv32("stage77_xnu_iokit_property_inheritance_selected_entry_count", contract->selected_entry_count);
    xnu_log_kv32("stage77_xnu_iokit_property_inheritance_rejected_entry_count", contract->rejected_entry_count);
    xnu_log_kv32("stage77_xnu_iokit_property_inheritance_dryrun_count", contract->registry_entry_dryrun_count);
    xnu_log_kv32("stage77_xnu_iokit_property_inheritance_inherited_property_dryrun_count", contract->inherited_property_dryrun_count);
    xnu_log_kv32("stage77_xnu_iokit_property_inheritance_instance_property_dryrun_count", contract->instance_property_dryrun_count);
    xnu_log_kv32("stage77_xnu_iokit_property_inheritance_attach_deferred_count", contract->attach_deferred_count);
    xnu_log_kv32("stage77_xnu_iokit_property_inheritance_start_deferred_count", contract->start_deferred_count);
    xnu_log_kv32("stage77_xnu_iokit_property_inheritance_rejected_unattached_count", contract->rejected_unattached_count);
    xnu_log_kv32("stage77_xnu_iokit_property_inheritance_public_compile_count", contract->iokit_public_compile_count);
    xnu_log_kv32("stage77_xnu_iokit_property_inheritance_public_link_count", contract->iokit_public_link_count);
    xnu_log_kv32("stage77_xnu_iokit_property_inheritance_no_iokit_runtime_exec", contract->no_iokit_runtime_exec);
    xnu_log_kv32("stage77_xnu_iokit_property_inheritance_no_registry_entry_runtime_exec", contract->no_registry_entry_runtime_exec);
    xnu_log_kv32("stage77_xnu_iokit_property_inheritance_no_property_runtime_exec", contract->no_property_runtime_exec);
    xnu_log_kv32("stage77_xnu_iokit_property_inheritance_no_attach_runtime_exec", contract->no_attach_runtime_exec);
    xnu_log_kv32("stage77_xnu_iokit_property_inheritance_no_start_runtime_exec", contract->no_start_runtime_exec);
    xnu_log_kv32("stage77_xnu_iokit_property_inheritance_no_provider_runtime_exec", contract->no_provider_runtime_exec);
    xnu_log_kv32("stage77_xnu_iokit_property_inheritance_no_platform_driver_exec", contract->no_platform_driver_exec);
    xnu_log_kv32("stage77_xnu_iokit_property_inheritance_no_live_pmap_tables_installed", contract->no_live_pmap_tables_installed);
    xnu_log_kv32("stage77_xnu_iokit_property_inheritance_proposed_workspace_written", contract->proposed_workspace_written);
    xnu_log_kv32("stage77_xnu_iokit_property_inheritance_pmap_ttbr_written", contract->pmap_ttbr_written);
    xnu_log_kv32("stage77_xnu_iokit_property_inheritance_pmap_tlbs_invalidated", contract->pmap_tlbs_invalidated);
    xnu_log_kv32("stage77_xnu_iokit_property_inheritance_caches_changed", contract->caches_changed);
    xnu_log_kv32("stage77_xnu_iokit_property_inheritance_persistent_write_attempted", contract->persistent_write_attempted);
}

int stage77_xnu_iokit_property_inheritance_dryrun_contract_selftest(
    const struct stage77_loader_preflight *preflight)
{
    struct stage77_xnu_iokit_property_inheritance_dryrun_contract *contract =
        &g_stage77_xnu_iokit_property_inheritance_dryrun_contract;
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
    contract->version = STAGE77_XNU_IOKIT_PROPERTY_INHERITANCE_DRYRUN_CONTRACT_VERSION;
    contract->size = sizeof(*contract);
    contract->status = STAGE77_STATUS_BASE;
    contract->required_mask = STAGE77_XNU_IOKIT_PROPERTY_INHERITANCE_DRYRUN_REQUIRED_MASK;

    if (!preflight || !args || !root) {
        contract->failure_mask = STAGE77_XNU_IOKIT_PROPERTY_INHERITANCE_DRYRUN_FAIL_SOURCE |
            STAGE77_XNU_IOKIT_PROPERTY_INHERITANCE_DRYRUN_FAIL_SAFETY_BOUNDARY;
        contract->checksum = stage77_iokit_property_inheritance_dryrun_checksum(contract);
        stage77_iokit_property_inheritance_dryrun_log(contract);
        return 0;
    }

    contract->source_catalog_status = preflight->xnu_iokit_catalog_property_dryrun_contract_status;
    contract->source_catalog_satisfied_mask = preflight->xnu_iokit_catalog_property_dryrun_contract_satisfied_mask;
    contract->source_catalog_failure_mask = preflight->xnu_iokit_catalog_property_dryrun_contract_failure_mask;
    contract->source_catalog_checksum = preflight->xnu_iokit_catalog_property_dryrun_contract_checksum;
    contract->source_catalog_property_checksum = preflight->xnu_iokit_catalog_property_dryrun_contract.personality_property_checksum;
    contract->source_provider_status = preflight->xnu_iokit_provider_plane_dryrun_contract_status;
    contract->source_provider_satisfied_mask = preflight->xnu_iokit_provider_plane_dryrun_contract_satisfied_mask;
    contract->source_provider_failure_mask = preflight->xnu_iokit_provider_plane_dryrun_contract_failure_mask;
    contract->source_registry_status = preflight->xnu_iokit_registry_service_dryrun_contract_status;
    contract->source_registry_satisfied_mask = preflight->xnu_iokit_registry_service_dryrun_contract_satisfied_mask;
    contract->source_registry_failure_mask = preflight->xnu_iokit_registry_service_dryrun_contract_failure_mask;
    contract->source_match_status = preflight->xnu_iokit_match_dryrun_contract_status;
    contract->source_scaffold_status = preflight->xnu_iokit_platform_scaffold_contract_status;
    contract->source_pexpert_status = preflight->xnu_pexpert_hook_readiness_contract_status;
    contract->source_pmap_transition_status = preflight->xnu_pmap_transition_dryrun_contract_status;
    contract->source_compile_graph_status = preflight->xnu_compile_graph_status;
    contract->source_object_subset_status = preflight->xnu_object_subset_status;
    contract->source_link_status = preflight->xnu_link_status;
    contract->source_loader_safety_mask = preflight->safety_mask;
    contract->source_loader_safety_required_mask = STAGE77_LOADER_SAFETY_REQUIRED;
    contract->boot_args_ptr = preflight->boot_args_ptr;
    contract->device_tree_ptr = preflight->device_tree_ptr;
    contract->device_tree_length = preflight->device_tree_length;
    contract->apple_dt_semantic_mask = preflight->apple_dt_semantic_mask;

    contract->platform_personality_node_ptr = (uint32_t)(uintptr_t)apple_dt_find_child(args->deviceTreeP,
                                                                                       args->deviceTreeLength,
                                                                                       root,
                                                                                       "stage77-platform-personality");
    contract->interrupt_personality_node_ptr = (uint32_t)(uintptr_t)apple_dt_find_child(args->deviceTreeP,
                                                                                        args->deviceTreeLength,
                                                                                        root,
                                                                                        "stage77-interrupt-personality");
    contract->timer_personality_node_ptr = (uint32_t)(uintptr_t)apple_dt_find_child(args->deviceTreeP,
                                                                                    args->deviceTreeLength,
                                                                                    root,
                                                                                    "stage77-timer-personality");
    contract->cpu_personality_node_ptr = (uint32_t)(uintptr_t)apple_dt_find_child(args->deviceTreeP,
                                                                                  args->deviceTreeLength,
                                                                                  root,
                                                                                  "stage77-cpu-personality");
    contract->rejected_personality_node_ptr = (uint32_t)(uintptr_t)apple_dt_find_child(args->deviceTreeP,
                                                                                       args->deviceTreeLength,
                                                                                       root,
                                                                                       "stage77-rejected-personality");

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

    contract->source_selected_personality_mask = preflight->xnu_iokit_catalog_property_dryrun_contract.selected_personality_mask;
    contract->source_rejected_personality_mask = preflight->xnu_iokit_catalog_property_dryrun_contract.rejected_personality_mask;
    contract->source_catalog_property_matrix = preflight->xnu_iokit_catalog_property_dryrun_contract.catalog_property_matrix;

    if (u32_prop_equals(args, platform_node, "registry-entry-dryrun", 1u)) {
        contract->registry_entry_marker_mask |= STAGE77_XNU_IOKIT_REGISTRY_SERVICE_PLATFORM_BIT;
    }
    if (u32_prop_equals(args, interrupt_node, "registry-entry-dryrun", 1u)) {
        contract->registry_entry_marker_mask |= STAGE77_XNU_IOKIT_REGISTRY_SERVICE_INTERRUPT_BIT;
    }
    if (u32_prop_equals(args, timer_node, "registry-entry-dryrun", 1u)) {
        contract->registry_entry_marker_mask |= STAGE77_XNU_IOKIT_REGISTRY_SERVICE_TIMER_BIT;
    }
    if (u32_prop_equals(args, cpu_node, "registry-entry-dryrun", 1u)) {
        contract->registry_entry_marker_mask |= STAGE77_XNU_IOKIT_REGISTRY_SERVICE_CPU_BIT;
    }

    if (u32_prop_equals(args, platform_node, "property-inheritance-dryrun", 1u)) {
        contract->property_inheritance_marker_mask |= STAGE77_XNU_IOKIT_REGISTRY_SERVICE_PLATFORM_BIT;
    }
    if (u32_prop_equals(args, interrupt_node, "property-inheritance-dryrun", 1u)) {
        contract->property_inheritance_marker_mask |= STAGE77_XNU_IOKIT_REGISTRY_SERVICE_INTERRUPT_BIT;
    }
    if (u32_prop_equals(args, timer_node, "property-inheritance-dryrun", 1u)) {
        contract->property_inheritance_marker_mask |= STAGE77_XNU_IOKIT_REGISTRY_SERVICE_TIMER_BIT;
    }
    if (u32_prop_equals(args, cpu_node, "property-inheritance-dryrun", 1u)) {
        contract->property_inheritance_marker_mask |= STAGE77_XNU_IOKIT_REGISTRY_SERVICE_CPU_BIT;
    }
    if (u32_prop_equals(args, rejected_node, "property-inheritance-dryrun", 1u)) {
        contract->property_inheritance_marker_mask |= STAGE77_XNU_IOKIT_REGISTRY_SERVICE_REJECTED_BIT;
    }

    if (u32_prop_equals(args, platform_node, "registry-entry-ordinal",
                        STAGE77_XNU_IOKIT_PROPERTY_INHERITANCE_PLATFORM_ORDINAL)) {
        contract->registry_entry_ordinal_mask |= STAGE77_XNU_IOKIT_REGISTRY_SERVICE_PLATFORM_BIT;
    }
    if (u32_prop_equals(args, interrupt_node, "registry-entry-ordinal",
                        STAGE77_XNU_IOKIT_PROPERTY_INHERITANCE_INTERRUPT_ORDINAL)) {
        contract->registry_entry_ordinal_mask |= STAGE77_XNU_IOKIT_REGISTRY_SERVICE_INTERRUPT_BIT;
    }
    if (u32_prop_equals(args, timer_node, "registry-entry-ordinal",
                        STAGE77_XNU_IOKIT_PROPERTY_INHERITANCE_TIMER_ORDINAL)) {
        contract->registry_entry_ordinal_mask |= STAGE77_XNU_IOKIT_REGISTRY_SERVICE_TIMER_BIT;
    }
    if (u32_prop_equals(args, cpu_node, "registry-entry-ordinal",
                        STAGE77_XNU_IOKIT_PROPERTY_INHERITANCE_CPU_ORDINAL)) {
        contract->registry_entry_ordinal_mask |= STAGE77_XNU_IOKIT_REGISTRY_SERVICE_CPU_BIT;
    }

    if (string_prop_equals(args, platform_node, "registry-plane", "IODeviceTree")) {
        contract->registry_plane_match_mask |= STAGE77_XNU_IOKIT_REGISTRY_SERVICE_PLATFORM_BIT;
    }
    if (string_prop_equals(args, interrupt_node, "registry-plane", "IODeviceTree")) {
        contract->registry_plane_match_mask |= STAGE77_XNU_IOKIT_REGISTRY_SERVICE_INTERRUPT_BIT;
    }
    if (string_prop_equals(args, timer_node, "registry-plane", "IODeviceTree")) {
        contract->registry_plane_match_mask |= STAGE77_XNU_IOKIT_REGISTRY_SERVICE_TIMER_BIT;
    }
    if (string_prop_equals(args, cpu_node, "registry-plane", "IODeviceTree")) {
        contract->registry_plane_match_mask |= STAGE77_XNU_IOKIT_REGISTRY_SERVICE_CPU_BIT;
    }

    if (string_prop_equals(args, platform_node, "IOClass", "MSM8974PlatformExpert")) {
        contract->inherited_ioclass_mask |= STAGE77_XNU_IOKIT_REGISTRY_SERVICE_PLATFORM_BIT;
    }
    if (string_prop_equals(args, interrupt_node, "IOClass", "MSM8974InterruptController")) {
        contract->inherited_ioclass_mask |= STAGE77_XNU_IOKIT_REGISTRY_SERVICE_INTERRUPT_BIT;
    }
    if (string_prop_equals(args, timer_node, "IOClass", "MSM8974Timer")) {
        contract->inherited_ioclass_mask |= STAGE77_XNU_IOKIT_REGISTRY_SERVICE_TIMER_BIT;
    }
    if (string_prop_equals(args, cpu_node, "IOClass", "IOCPU")) {
        contract->inherited_ioclass_mask |= STAGE77_XNU_IOKIT_REGISTRY_SERVICE_CPU_BIT;
    }

    if (string_prop_equals(args, platform_node, "IOProviderClass", "IOPlatformExpertDevice")) {
        contract->inherited_provider_class_mask |= STAGE77_XNU_IOKIT_REGISTRY_SERVICE_PLATFORM_BIT;
    }
    if (string_prop_equals(args, interrupt_node, "IOProviderClass", "IOPlatformExpertDevice")) {
        contract->inherited_provider_class_mask |= STAGE77_XNU_IOKIT_REGISTRY_SERVICE_INTERRUPT_BIT;
    }
    if (string_prop_equals(args, timer_node, "IOProviderClass", "IOPlatformExpertDevice")) {
        contract->inherited_provider_class_mask |= STAGE77_XNU_IOKIT_REGISTRY_SERVICE_TIMER_BIT;
    }
    if (string_prop_equals(args, cpu_node, "IOProviderClass", "IOPlatformExpertDevice")) {
        contract->inherited_provider_class_mask |= STAGE77_XNU_IOKIT_REGISTRY_SERVICE_CPU_BIT;
    }

    if (string_prop_equals(args, platform_node, "IOMatchCategory", "Stage77LocalPlatformScaffold")) {
        contract->inherited_category_mask |= STAGE77_XNU_IOKIT_REGISTRY_SERVICE_PLATFORM_BIT;
    }
    if (string_prop_equals(args, interrupt_node, "IOMatchCategory", "Stage77LocalInterruptService")) {
        contract->inherited_category_mask |= STAGE77_XNU_IOKIT_REGISTRY_SERVICE_INTERRUPT_BIT;
    }
    if (string_prop_equals(args, timer_node, "IOMatchCategory", "Stage77LocalTimerService")) {
        contract->inherited_category_mask |= STAGE77_XNU_IOKIT_REGISTRY_SERVICE_TIMER_BIT;
    }
    if (string_prop_equals(args, cpu_node, "IOMatchCategory", "Stage77LocalCPUService")) {
        contract->inherited_category_mask |= STAGE77_XNU_IOKIT_REGISTRY_SERVICE_CPU_BIT;
    }

    if (string_prop_equals(args, platform_node, "compatible", "qcom,msm8974-cancro-stage77")) {
        contract->inherited_compatible_mask |= STAGE77_XNU_IOKIT_REGISTRY_SERVICE_PLATFORM_BIT;
    }
    if (string_prop_equals(args, interrupt_node, "compatible", "qcom,msm8974-gic-stage77")) {
        contract->inherited_compatible_mask |= STAGE77_XNU_IOKIT_REGISTRY_SERVICE_INTERRUPT_BIT;
    }
    if (string_prop_equals(args, timer_node, "compatible", "qcom,msm8974-timer-stage77")) {
        contract->inherited_compatible_mask |= STAGE77_XNU_IOKIT_REGISTRY_SERVICE_TIMER_BIT;
    }
    if (string_prop_equals(args, cpu_node, "compatible", "qcom,msm8974-cpu-stage77")) {
        contract->inherited_compatible_mask |= STAGE77_XNU_IOKIT_REGISTRY_SERVICE_CPU_BIT;
    }

    if (u32_prop_equals(args, platform_node, "IOProbeScore", STAGE77_XNU_IOKIT_REGISTRY_SERVICE_PLATFORM_PROBE_SCORE)) {
        contract->inherited_probe_score_mask |= STAGE77_XNU_IOKIT_REGISTRY_SERVICE_PLATFORM_BIT;
    }
    if (u32_prop_equals(args, interrupt_node, "IOProbeScore", STAGE77_XNU_IOKIT_REGISTRY_SERVICE_INTERRUPT_PROBE_SCORE)) {
        contract->inherited_probe_score_mask |= STAGE77_XNU_IOKIT_REGISTRY_SERVICE_INTERRUPT_BIT;
    }
    if (u32_prop_equals(args, timer_node, "IOProbeScore", STAGE77_XNU_IOKIT_REGISTRY_SERVICE_TIMER_PROBE_SCORE)) {
        contract->inherited_probe_score_mask |= STAGE77_XNU_IOKIT_REGISTRY_SERVICE_TIMER_BIT;
    }
    if (u32_prop_equals(args, cpu_node, "IOProbeScore", STAGE77_XNU_IOKIT_REGISTRY_SERVICE_CPU_PROBE_SCORE)) {
        contract->inherited_probe_score_mask |= STAGE77_XNU_IOKIT_REGISTRY_SERVICE_CPU_BIT;
    }

    if (string_prop_equals(args, platform_node, "CFBundleIdentifier", "com.mi4ios6.stage77.localcatalog")) {
        contract->inherited_bundle_identifier_mask |= STAGE77_XNU_IOKIT_REGISTRY_SERVICE_PLATFORM_BIT;
    }
    if (string_prop_equals(args, interrupt_node, "CFBundleIdentifier", "com.mi4ios6.stage77.localcatalog")) {
        contract->inherited_bundle_identifier_mask |= STAGE77_XNU_IOKIT_REGISTRY_SERVICE_INTERRUPT_BIT;
    }
    if (string_prop_equals(args, timer_node, "CFBundleIdentifier", "com.mi4ios6.stage77.localcatalog")) {
        contract->inherited_bundle_identifier_mask |= STAGE77_XNU_IOKIT_REGISTRY_SERVICE_TIMER_BIT;
    }
    if (string_prop_equals(args, cpu_node, "CFBundleIdentifier", "com.mi4ios6.stage77.localcatalog")) {
        contract->inherited_bundle_identifier_mask |= STAGE77_XNU_IOKIT_REGISTRY_SERVICE_CPU_BIT;
    }

    contract->inherited_property_mask = contract->inherited_ioclass_mask &
        contract->inherited_provider_class_mask & contract->inherited_category_mask &
        contract->inherited_compatible_mask & contract->inherited_probe_score_mask &
        contract->inherited_bundle_identifier_mask;

    if (u32_prop_equals(args, platform_node, "source-service-bit", STAGE77_XNU_IOKIT_REGISTRY_SERVICE_PLATFORM_BIT)) {
        contract->instance_source_bit_mask |= STAGE77_XNU_IOKIT_REGISTRY_SERVICE_PLATFORM_BIT;
    }
    if (u32_prop_equals(args, interrupt_node, "source-service-bit", STAGE77_XNU_IOKIT_REGISTRY_SERVICE_INTERRUPT_BIT)) {
        contract->instance_source_bit_mask |= STAGE77_XNU_IOKIT_REGISTRY_SERVICE_INTERRUPT_BIT;
    }
    if (u32_prop_equals(args, timer_node, "source-service-bit", STAGE77_XNU_IOKIT_REGISTRY_SERVICE_TIMER_BIT)) {
        contract->instance_source_bit_mask |= STAGE77_XNU_IOKIT_REGISTRY_SERVICE_TIMER_BIT;
    }
    if (u32_prop_equals(args, cpu_node, "source-service-bit", STAGE77_XNU_IOKIT_REGISTRY_SERVICE_CPU_BIT)) {
        contract->instance_source_bit_mask |= STAGE77_XNU_IOKIT_REGISTRY_SERVICE_CPU_BIT;
    }

    if (u32_prop_equals(args, platform_node, "provider-plane-published", 1u)) {
        contract->instance_publication_mask |= STAGE77_XNU_IOKIT_REGISTRY_SERVICE_PLATFORM_BIT;
    }
    if (u32_prop_equals(args, interrupt_node, "provider-plane-published", 1u)) {
        contract->instance_publication_mask |= STAGE77_XNU_IOKIT_REGISTRY_SERVICE_INTERRUPT_BIT;
    }
    if (u32_prop_equals(args, timer_node, "provider-plane-published", 1u)) {
        contract->instance_publication_mask |= STAGE77_XNU_IOKIT_REGISTRY_SERVICE_TIMER_BIT;
    }
    if (u32_prop_equals(args, cpu_node, "provider-plane-published", 1u)) {
        contract->instance_publication_mask |= STAGE77_XNU_IOKIT_REGISTRY_SERVICE_CPU_BIT;
    }

    if (u32_prop_equals(args, platform_node, "stage-owned-local-only", 1u) &&
        u32_prop_equals(args, platform_node, "catalog-property-dryrun", 1u)) {
        contract->instance_local_only_mask |= STAGE77_XNU_IOKIT_REGISTRY_SERVICE_PLATFORM_BIT;
    }
    if (u32_prop_equals(args, interrupt_node, "stage-owned-local-only", 1u) &&
        u32_prop_equals(args, interrupt_node, "catalog-property-dryrun", 1u)) {
        contract->instance_local_only_mask |= STAGE77_XNU_IOKIT_REGISTRY_SERVICE_INTERRUPT_BIT;
    }
    if (u32_prop_equals(args, timer_node, "stage-owned-local-only", 1u) &&
        u32_prop_equals(args, timer_node, "catalog-property-dryrun", 1u)) {
        contract->instance_local_only_mask |= STAGE77_XNU_IOKIT_REGISTRY_SERVICE_TIMER_BIT;
    }
    if (u32_prop_equals(args, cpu_node, "stage-owned-local-only", 1u) &&
        u32_prop_equals(args, cpu_node, "catalog-property-dryrun", 1u)) {
        contract->instance_local_only_mask |= STAGE77_XNU_IOKIT_REGISTRY_SERVICE_CPU_BIT;
    }

    contract->instance_property_mask = contract->registry_entry_marker_mask &
        contract->registry_entry_ordinal_mask & contract->registry_plane_match_mask &
        contract->instance_source_bit_mask & contract->instance_publication_mask &
        contract->instance_local_only_mask;

    if (u32_prop_equals(args, platform_node, "attach-deferred", 1u)) {
        contract->attach_deferred_mask |= STAGE77_XNU_IOKIT_REGISTRY_SERVICE_PLATFORM_BIT;
    }
    if (u32_prop_equals(args, interrupt_node, "attach-deferred", 1u)) {
        contract->attach_deferred_mask |= STAGE77_XNU_IOKIT_REGISTRY_SERVICE_INTERRUPT_BIT;
    }
    if (u32_prop_equals(args, timer_node, "attach-deferred", 1u)) {
        contract->attach_deferred_mask |= STAGE77_XNU_IOKIT_REGISTRY_SERVICE_TIMER_BIT;
    }
    if (u32_prop_equals(args, cpu_node, "attach-deferred", 1u)) {
        contract->attach_deferred_mask |= STAGE77_XNU_IOKIT_REGISTRY_SERVICE_CPU_BIT;
    }
    if (u32_prop_equals(args, platform_node, "start-deferred", 1u)) {
        contract->start_deferred_mask |= STAGE77_XNU_IOKIT_REGISTRY_SERVICE_PLATFORM_BIT;
    }
    if (u32_prop_equals(args, interrupt_node, "start-deferred", 1u)) {
        contract->start_deferred_mask |= STAGE77_XNU_IOKIT_REGISTRY_SERVICE_INTERRUPT_BIT;
    }
    if (u32_prop_equals(args, timer_node, "start-deferred", 1u)) {
        contract->start_deferred_mask |= STAGE77_XNU_IOKIT_REGISTRY_SERVICE_TIMER_BIT;
    }
    if (u32_prop_equals(args, cpu_node, "start-deferred", 1u)) {
        contract->start_deferred_mask |= STAGE77_XNU_IOKIT_REGISTRY_SERVICE_CPU_BIT;
    }

    contract->selected_registry_entry_mask = contract->inherited_property_mask & contract->instance_property_mask &
        contract->attach_deferred_mask & contract->start_deferred_mask & contract->source_selected_personality_mask;

    if (string_prop_equals(args, rejected_node, "name", "stage77-rejected-personality") &&
        u32_prop_equals(args, rejected_node, "registry-entry-dryrun", 0u) &&
        u32_prop_equals(args, rejected_node, "property-inheritance-dryrun", 1u) &&
        u32_prop_equals(args, rejected_node, "registry-entry-ordinal",
                        STAGE77_XNU_IOKIT_PROPERTY_INHERITANCE_REJECTED_ORDINAL) &&
        u32_prop_equals(args, rejected_node, "registry-entry-attached", 0u) &&
        u32_prop_equals(args, rejected_node, "provider-plane-published", 0u) &&
        u32_prop_equals(args, rejected_node, "rejected-candidate", 1u) &&
        u32_prop_equals(args, rejected_node, "stage-owned-local-only", 1u)) {
        contract->rejected_unattached_mask |= STAGE77_XNU_IOKIT_REGISTRY_SERVICE_REJECTED_BIT;
    }

    contract->property_inheritance_matrix = contract->selected_registry_entry_mask | contract->rejected_unattached_mask;
    contract->platform_registry_entry_ordinal = STAGE77_XNU_IOKIT_PROPERTY_INHERITANCE_PLATFORM_ORDINAL;
    contract->interrupt_registry_entry_ordinal = STAGE77_XNU_IOKIT_PROPERTY_INHERITANCE_INTERRUPT_ORDINAL;
    contract->timer_registry_entry_ordinal = STAGE77_XNU_IOKIT_PROPERTY_INHERITANCE_TIMER_ORDINAL;
    contract->cpu_registry_entry_ordinal = STAGE77_XNU_IOKIT_PROPERTY_INHERITANCE_CPU_ORDINAL;
    contract->rejected_registry_entry_ordinal = STAGE77_XNU_IOKIT_PROPERTY_INHERITANCE_REJECTED_ORDINAL;
    contract->registry_entry_order_checksum = contract->platform_registry_entry_ordinal ^
        contract->interrupt_registry_entry_ordinal ^ contract->timer_registry_entry_ordinal ^
        contract->cpu_registry_entry_ordinal ^ contract->rejected_registry_entry_ordinal;
    contract->registry_plane_hash = STAGE77_XNU_IOKIT_PROPERTY_INHERITANCE_REGISTRY_PLANE_HASH;

    contract->registry_entry_candidate_count = STAGE77_XNU_IOKIT_PROPERTY_INHERITANCE_CANDIDATE_COUNT;
    contract->selected_entry_count = (contract->selected_registry_entry_mask ==
                                      STAGE77_XNU_IOKIT_REGISTRY_SERVICE_SELECTED_REQUIRED) ?
        STAGE77_XNU_IOKIT_PROPERTY_INHERITANCE_ENTRY_COUNT : 0u;
    contract->rejected_entry_count = (contract->rejected_unattached_mask ==
                                      STAGE77_XNU_IOKIT_REGISTRY_SERVICE_REJECTED_REQUIRED) ?
        STAGE77_XNU_IOKIT_PROPERTY_INHERITANCE_REJECTED_COUNT : 0u;
    contract->registry_entry_dryrun_count = contract->selected_entry_count;
    contract->inherited_property_dryrun_count = contract->selected_entry_count;
    contract->instance_property_dryrun_count = contract->selected_entry_count;
    contract->attach_deferred_count = (contract->attach_deferred_mask ==
                                       STAGE77_XNU_IOKIT_REGISTRY_SERVICE_SELECTED_REQUIRED) ?
        STAGE77_XNU_IOKIT_PROPERTY_INHERITANCE_ENTRY_COUNT : 0u;
    contract->start_deferred_count = (contract->start_deferred_mask ==
                                      STAGE77_XNU_IOKIT_REGISTRY_SERVICE_SELECTED_REQUIRED) ?
        STAGE77_XNU_IOKIT_PROPERTY_INHERITANCE_ENTRY_COUNT : 0u;
    contract->rejected_unattached_count = contract->rejected_entry_count;
    contract->property_inheritance_checksum = contract->source_catalog_property_checksum ^
        contract->source_selected_personality_mask ^ contract->source_rejected_personality_mask ^
        contract->selected_registry_entry_mask ^ contract->rejected_unattached_mask ^
        contract->inherited_property_mask ^ contract->instance_property_mask ^ contract->attach_deferred_mask ^
        contract->start_deferred_mask ^ contract->registry_entry_order_checksum ^
        contract->registry_entry_candidate_count ^ contract->selected_entry_count ^
        contract->rejected_entry_count ^ contract->registry_entry_dryrun_count ^
        contract->inherited_property_dryrun_count ^ contract->instance_property_dryrun_count ^
        contract->rejected_unattached_count ^ contract->registry_plane_hash;
    contract->expected_property_inheritance_checksum = STAGE77_XNU_IOKIT_PROPERTY_INHERITANCE_EXPECTED_PROPERTY_CHECKSUM;

    contract->iokit_reference_mask = preflight->xnu_iokit_catalog_property_dryrun_contract.iokit_reference_mask;
    contract->iokit_runtime_blocked_mask = preflight->xnu_iokit_catalog_property_dryrun_contract.iokit_runtime_blocked_mask;
    contract->iokit_reference_count = preflight->xnu_iokit_catalog_property_dryrun_contract.iokit_reference_count;
    contract->iokit_public_compile_count = preflight->xnu_iokit_catalog_property_dryrun_contract.iokit_public_compile_count;
    contract->iokit_public_link_count = preflight->xnu_iokit_catalog_property_dryrun_contract.iokit_public_link_count;
    contract->iokit_reference_only = preflight->xnu_iokit_catalog_property_dryrun_contract.iokit_reference_only;
    contract->no_iokit_runtime_exec = preflight->xnu_iokit_catalog_property_dryrun_contract.no_iokit_runtime_exec;
    contract->no_catalog_runtime_exec = preflight->xnu_iokit_catalog_property_dryrun_contract.no_catalog_runtime_exec;
    contract->no_provider_runtime_exec = preflight->xnu_iokit_catalog_property_dryrun_contract.no_provider_runtime_exec;
    contract->no_registry_entry_runtime_exec = 1u;
    contract->no_property_runtime_exec = 1u;
    contract->no_attach_runtime_exec = 1u;
    contract->no_start_runtime_exec = 1u;
    contract->no_platform_driver_exec = preflight->xnu_iokit_catalog_property_dryrun_contract.no_platform_driver_exec;
    contract->no_public_xnu_exec = preflight->xnu_iokit_catalog_property_dryrun_contract.no_public_xnu_exec;
    contract->no_platform_runtime_exec = preflight->xnu_iokit_catalog_property_dryrun_contract.no_platform_runtime_exec;
    contract->no_public_pmap_exec = preflight->xnu_iokit_catalog_property_dryrun_contract.no_public_pmap_exec;
    contract->no_live_pmap_tables_installed = preflight->xnu_iokit_catalog_property_dryrun_contract.no_live_pmap_tables_installed;
    contract->proposed_workspace_written = preflight->xnu_iokit_catalog_property_dryrun_contract.proposed_workspace_written;
    contract->pmap_ttbr_written = preflight->xnu_iokit_catalog_property_dryrun_contract.pmap_ttbr_written;
    contract->pmap_ttbcr_written = preflight->xnu_iokit_catalog_property_dryrun_contract.pmap_ttbcr_written;
    contract->pmap_dacr_written = preflight->xnu_iokit_catalog_property_dryrun_contract.pmap_dacr_written;
    contract->pmap_sctlr_written = preflight->xnu_iokit_catalog_property_dryrun_contract.pmap_sctlr_written;
    contract->pmap_tlbs_invalidated = preflight->xnu_iokit_catalog_property_dryrun_contract.pmap_tlbs_invalidated;
    contract->caches_changed = preflight->xnu_iokit_catalog_property_dryrun_contract.caches_changed;
    contract->persistent_write_attempted = preflight->xnu_iokit_catalog_property_dryrun_contract.persistent_write_attempted;
    contract->xnu_start_executed = preflight->xnu_iokit_catalog_property_dryrun_contract.xnu_start_executed;
    contract->generated_macho_executed = preflight->xnu_iokit_catalog_property_dryrun_contract.generated_macho_executed;
    contract->proposed_platform_gap_mask = preflight->platform_gap_mask;
    contract->proposed_pexpert_gap_mask = preflight->pexpert_gap_mask;
    contract->local_only = 1u;
    contract->fail_closed = 1u;

    source_ok = (contract->source_catalog_status == STAGE77_STATUS_OK &&
                 contract->source_catalog_satisfied_mask == STAGE77_XNU_IOKIT_CATALOG_PROPERTY_DRYRUN_REQUIRED_MASK &&
                 contract->source_catalog_failure_mask == 0u &&
                 contract->source_provider_status == STAGE77_STATUS_OK &&
                 contract->source_provider_satisfied_mask == STAGE77_XNU_IOKIT_PROVIDER_PLANE_DRYRUN_REQUIRED_MASK &&
                 contract->source_provider_failure_mask == 0u &&
                 contract->source_registry_status == STAGE77_STATUS_OK &&
                 contract->source_registry_satisfied_mask == STAGE77_XNU_IOKIT_REGISTRY_SERVICE_DRYRUN_REQUIRED_MASK &&
                 contract->source_registry_failure_mask == 0u &&
                 contract->source_match_status == STAGE77_STATUS_OK &&
                 contract->source_scaffold_status == STAGE77_STATUS_OK &&
                 contract->source_pexpert_status == STAGE77_STATUS_OK &&
                 contract->source_pmap_transition_status == STAGE77_STATUS_OK &&
                 contract->source_compile_graph_status == STAGE77_STATUS_OK &&
                 contract->source_object_subset_status == STAGE77_STATUS_OK &&
                 contract->source_link_status == STAGE77_STATUS_OK &&
                 contract->source_loader_safety_mask == contract->source_loader_safety_required_mask) ? 1u : 0u;
    if (source_ok) {
        contract->satisfied_mask |= STAGE77_XNU_IOKIT_PROPERTY_INHERITANCE_DRYRUN_SAT_SOURCE_CATALOG;
    } else {
        contract->failure_mask |= STAGE77_XNU_IOKIT_PROPERTY_INHERITANCE_DRYRUN_FAIL_SOURCE;
    }

    if (contract->apple_dt_semantic_mask == STAGE77_DT_READY_REQUIRED) {
        contract->satisfied_mask |= STAGE77_XNU_IOKIT_PROPERTY_INHERITANCE_DRYRUN_SAT_APPLE_DT;
    } else {
        contract->failure_mask |= STAGE77_XNU_IOKIT_PROPERTY_INHERITANCE_DRYRUN_FAIL_DT;
    }

    personality_nodes_ok = (contract->platform_personality_node_ptr != 0u && contract->platform_personality_prop_count >= 17u &&
                            contract->interrupt_personality_node_ptr != 0u && contract->interrupt_personality_prop_count >= 17u &&
                            contract->timer_personality_node_ptr != 0u && contract->timer_personality_prop_count >= 17u &&
                            contract->cpu_personality_node_ptr != 0u && contract->cpu_personality_prop_count >= 17u &&
                            contract->rejected_personality_node_ptr != 0u && contract->rejected_personality_prop_count >= 15u) ? 1u : 0u;
    if (personality_nodes_ok) {
        contract->satisfied_mask |= STAGE77_XNU_IOKIT_PROPERTY_INHERITANCE_DRYRUN_SAT_PERSONALITY_NODE_SET;
    } else {
        contract->failure_mask |= STAGE77_XNU_IOKIT_PROPERTY_INHERITANCE_DRYRUN_FAIL_PERSONALITY;
    }

    if (contract->registry_entry_marker_mask == STAGE77_XNU_IOKIT_REGISTRY_SERVICE_SELECTED_REQUIRED &&
        contract->property_inheritance_marker_mask == (STAGE77_XNU_IOKIT_REGISTRY_SERVICE_SELECTED_REQUIRED |
                                                       STAGE77_XNU_IOKIT_REGISTRY_SERVICE_REJECTED_REQUIRED)) {
        contract->satisfied_mask |= STAGE77_XNU_IOKIT_PROPERTY_INHERITANCE_DRYRUN_SAT_REGISTRY_ENTRY_MARKERS;
    } else {
        contract->failure_mask |= STAGE77_XNU_IOKIT_PROPERTY_INHERITANCE_DRYRUN_FAIL_REGISTRY_ENTRY;
    }
    if (contract->registry_entry_ordinal_mask == STAGE77_XNU_IOKIT_REGISTRY_SERVICE_SELECTED_REQUIRED &&
        contract->registry_entry_order_checksum == 0xffffffffu) {
        contract->satisfied_mask |= STAGE77_XNU_IOKIT_PROPERTY_INHERITANCE_DRYRUN_SAT_ENTRY_ORDINAL_MATRIX;
    } else {
        contract->failure_mask |= STAGE77_XNU_IOKIT_PROPERTY_INHERITANCE_DRYRUN_FAIL_REGISTRY_ENTRY;
    }
    if (contract->registry_plane_match_mask == STAGE77_XNU_IOKIT_REGISTRY_SERVICE_SELECTED_REQUIRED &&
        contract->registry_plane_hash == STAGE77_XNU_IOKIT_PROPERTY_INHERITANCE_REGISTRY_PLANE_HASH) {
        contract->satisfied_mask |= STAGE77_XNU_IOKIT_PROPERTY_INHERITANCE_DRYRUN_SAT_REGISTRY_PLANE_MATRIX;
    } else {
        contract->failure_mask |= STAGE77_XNU_IOKIT_PROPERTY_INHERITANCE_DRYRUN_FAIL_REGISTRY_ENTRY;
    }

    if (contract->inherited_ioclass_mask == STAGE77_XNU_IOKIT_REGISTRY_SERVICE_SELECTED_REQUIRED) {
        contract->satisfied_mask |= STAGE77_XNU_IOKIT_PROPERTY_INHERITANCE_DRYRUN_SAT_INHERITED_CLASS_MATRIX;
    } else {
        contract->failure_mask |= STAGE77_XNU_IOKIT_PROPERTY_INHERITANCE_DRYRUN_FAIL_INHERITANCE;
    }
    if (contract->inherited_provider_class_mask == STAGE77_XNU_IOKIT_REGISTRY_SERVICE_SELECTED_REQUIRED) {
        contract->satisfied_mask |= STAGE77_XNU_IOKIT_PROPERTY_INHERITANCE_DRYRUN_SAT_INHERITED_PROVIDER_MATRIX;
    } else {
        contract->failure_mask |= STAGE77_XNU_IOKIT_PROPERTY_INHERITANCE_DRYRUN_FAIL_INHERITANCE;
    }
    if (contract->inherited_category_mask == STAGE77_XNU_IOKIT_REGISTRY_SERVICE_SELECTED_REQUIRED) {
        contract->satisfied_mask |= STAGE77_XNU_IOKIT_PROPERTY_INHERITANCE_DRYRUN_SAT_INHERITED_CATEGORY_MATRIX;
    } else {
        contract->failure_mask |= STAGE77_XNU_IOKIT_PROPERTY_INHERITANCE_DRYRUN_FAIL_INHERITANCE;
    }
    if (contract->inherited_compatible_mask == STAGE77_XNU_IOKIT_REGISTRY_SERVICE_SELECTED_REQUIRED) {
        contract->satisfied_mask |= STAGE77_XNU_IOKIT_PROPERTY_INHERITANCE_DRYRUN_SAT_INHERITED_COMPATIBLE_MATRIX;
    } else {
        contract->failure_mask |= STAGE77_XNU_IOKIT_PROPERTY_INHERITANCE_DRYRUN_FAIL_INHERITANCE;
    }
    if (contract->inherited_probe_score_mask == STAGE77_XNU_IOKIT_REGISTRY_SERVICE_SELECTED_REQUIRED) {
        contract->satisfied_mask |= STAGE77_XNU_IOKIT_PROPERTY_INHERITANCE_DRYRUN_SAT_INHERITED_PROBE_SCORE_MATRIX;
    } else {
        contract->failure_mask |= STAGE77_XNU_IOKIT_PROPERTY_INHERITANCE_DRYRUN_FAIL_INHERITANCE;
    }
    if (contract->inherited_bundle_identifier_mask == STAGE77_XNU_IOKIT_REGISTRY_SERVICE_SELECTED_REQUIRED) {
        contract->satisfied_mask |= STAGE77_XNU_IOKIT_PROPERTY_INHERITANCE_DRYRUN_SAT_INHERITED_BUNDLE_MATRIX;
    } else {
        contract->failure_mask |= STAGE77_XNU_IOKIT_PROPERTY_INHERITANCE_DRYRUN_FAIL_INHERITANCE;
    }

    if (contract->instance_source_bit_mask == STAGE77_XNU_IOKIT_REGISTRY_SERVICE_SELECTED_REQUIRED) {
        contract->satisfied_mask |= STAGE77_XNU_IOKIT_PROPERTY_INHERITANCE_DRYRUN_SAT_INSTANCE_SOURCE_BIT_MATRIX;
    } else {
        contract->failure_mask |= STAGE77_XNU_IOKIT_PROPERTY_INHERITANCE_DRYRUN_FAIL_INSTANCE;
    }
    if (contract->instance_publication_mask == STAGE77_XNU_IOKIT_REGISTRY_SERVICE_SELECTED_REQUIRED) {
        contract->satisfied_mask |= STAGE77_XNU_IOKIT_PROPERTY_INHERITANCE_DRYRUN_SAT_INSTANCE_PUBLICATION_MATRIX;
    } else {
        contract->failure_mask |= STAGE77_XNU_IOKIT_PROPERTY_INHERITANCE_DRYRUN_FAIL_INSTANCE;
    }
    if (contract->instance_local_only_mask == STAGE77_XNU_IOKIT_REGISTRY_SERVICE_SELECTED_REQUIRED) {
        contract->satisfied_mask |= STAGE77_XNU_IOKIT_PROPERTY_INHERITANCE_DRYRUN_SAT_INSTANCE_LOCAL_ONLY_MATRIX;
    } else {
        contract->failure_mask |= STAGE77_XNU_IOKIT_PROPERTY_INHERITANCE_DRYRUN_FAIL_INSTANCE;
    }

    if (contract->attach_deferred_mask == STAGE77_XNU_IOKIT_REGISTRY_SERVICE_SELECTED_REQUIRED &&
        contract->start_deferred_mask == STAGE77_XNU_IOKIT_REGISTRY_SERVICE_SELECTED_REQUIRED &&
        contract->attach_deferred_count == STAGE77_XNU_IOKIT_PROPERTY_INHERITANCE_ENTRY_COUNT &&
        contract->start_deferred_count == STAGE77_XNU_IOKIT_PROPERTY_INHERITANCE_ENTRY_COUNT) {
        contract->satisfied_mask |= STAGE77_XNU_IOKIT_PROPERTY_INHERITANCE_DRYRUN_SAT_ATTACH_START_DEFERRED;
    } else {
        contract->failure_mask |= STAGE77_XNU_IOKIT_PROPERTY_INHERITANCE_DRYRUN_FAIL_SAFETY_BOUNDARY;
    }

    if (contract->rejected_unattached_mask == STAGE77_XNU_IOKIT_REGISTRY_SERVICE_REJECTED_REQUIRED &&
        contract->source_rejected_personality_mask == STAGE77_XNU_IOKIT_REGISTRY_SERVICE_REJECTED_REQUIRED &&
        contract->rejected_registry_entry_ordinal == STAGE77_XNU_IOKIT_PROPERTY_INHERITANCE_REJECTED_ORDINAL &&
        contract->rejected_unattached_count == STAGE77_XNU_IOKIT_PROPERTY_INHERITANCE_REJECTED_COUNT) {
        contract->satisfied_mask |= STAGE77_XNU_IOKIT_PROPERTY_INHERITANCE_DRYRUN_SAT_REJECT_UNATTACHED;
    } else {
        contract->failure_mask |= STAGE77_XNU_IOKIT_PROPERTY_INHERITANCE_DRYRUN_FAIL_REJECTED;
    }

    if (contract->registry_entry_candidate_count == STAGE77_XNU_IOKIT_PROPERTY_INHERITANCE_CANDIDATE_COUNT &&
        contract->selected_entry_count == STAGE77_XNU_IOKIT_PROPERTY_INHERITANCE_ENTRY_COUNT &&
        contract->rejected_entry_count == STAGE77_XNU_IOKIT_PROPERTY_INHERITANCE_REJECTED_COUNT &&
        contract->registry_entry_dryrun_count == STAGE77_XNU_IOKIT_PROPERTY_INHERITANCE_ENTRY_COUNT &&
        contract->inherited_property_dryrun_count == STAGE77_XNU_IOKIT_PROPERTY_INHERITANCE_ENTRY_COUNT &&
        contract->instance_property_dryrun_count == STAGE77_XNU_IOKIT_PROPERTY_INHERITANCE_ENTRY_COUNT &&
        contract->property_inheritance_matrix == (STAGE77_XNU_IOKIT_REGISTRY_SERVICE_SELECTED_REQUIRED |
                                                  STAGE77_XNU_IOKIT_REGISTRY_SERVICE_REJECTED_REQUIRED)) {
        contract->satisfied_mask |= STAGE77_XNU_IOKIT_PROPERTY_INHERITANCE_DRYRUN_SAT_DRYRUN_COUNTS;
    } else {
        contract->failure_mask |= STAGE77_XNU_IOKIT_PROPERTY_INHERITANCE_DRYRUN_FAIL_COUNTS;
    }
    if (contract->property_inheritance_checksum == contract->expected_property_inheritance_checksum) {
        contract->satisfied_mask |= STAGE77_XNU_IOKIT_PROPERTY_INHERITANCE_DRYRUN_SAT_PROPERTY_CHECKSUM;
    } else {
        contract->failure_mask |= STAGE77_XNU_IOKIT_PROPERTY_INHERITANCE_DRYRUN_FAIL_CHECKSUM;
    }

    if (contract->iokit_reference_mask == STAGE77_XNU_IOKIT_REFERENCE_REQUIRED &&
        contract->iokit_runtime_blocked_mask == STAGE77_XNU_IOKIT_REFERENCE_REQUIRED &&
        contract->iokit_reference_count >= 3u && contract->iokit_public_compile_count == 0u &&
        contract->iokit_public_link_count == 0u && contract->iokit_reference_only == 1u) {
        contract->satisfied_mask |= STAGE77_XNU_IOKIT_PROPERTY_INHERITANCE_DRYRUN_SAT_IOKIT_REFERENCE;
    } else {
        contract->failure_mask |= STAGE77_XNU_IOKIT_PROPERTY_INHERITANCE_DRYRUN_FAIL_PUBLIC_BOUNDARY;
    }

    public_boundary_ok = (contract->no_public_xnu_exec == 1u && contract->no_platform_runtime_exec == 1u &&
                          contract->no_iokit_runtime_exec == 1u && contract->no_catalog_runtime_exec == 1u &&
                          contract->no_provider_runtime_exec == 1u &&
                          contract->no_registry_entry_runtime_exec == 1u && contract->no_property_runtime_exec == 1u &&
                          contract->no_attach_runtime_exec == 1u && contract->no_start_runtime_exec == 1u &&
                          contract->no_platform_driver_exec == 1u &&
                          contract->source_object_subset_status == STAGE77_STATUS_OK &&
                          contract->source_link_status == STAGE77_STATUS_OK) ? 1u : 0u;
    if (public_boundary_ok) {
        contract->satisfied_mask |= STAGE77_XNU_IOKIT_PROPERTY_INHERITANCE_DRYRUN_SAT_PUBLIC_BOUNDARY |
            STAGE77_XNU_IOKIT_PROPERTY_INHERITANCE_DRYRUN_SAT_NO_IOKIT_RUNTIME_EXEC |
            STAGE77_XNU_IOKIT_PROPERTY_INHERITANCE_DRYRUN_SAT_NO_REGISTRY_ENTRY_RUNTIME_EXEC |
            STAGE77_XNU_IOKIT_PROPERTY_INHERITANCE_DRYRUN_SAT_NO_PROPERTY_RUNTIME_EXEC |
            STAGE77_XNU_IOKIT_PROPERTY_INHERITANCE_DRYRUN_SAT_NO_ATTACH_START_RUNTIME_EXEC |
            STAGE77_XNU_IOKIT_PROPERTY_INHERITANCE_DRYRUN_SAT_NO_PUBLIC_XNU_EXEC;
    } else {
        contract->failure_mask |= STAGE77_XNU_IOKIT_PROPERTY_INHERITANCE_DRYRUN_FAIL_PUBLIC_BOUNDARY;
    }

    if (contract->no_public_pmap_exec == 1u) {
        contract->satisfied_mask |= STAGE77_XNU_IOKIT_PROPERTY_INHERITANCE_DRYRUN_SAT_NO_PUBLIC_PMAP_EXEC;
    } else {
        contract->failure_mask |= STAGE77_XNU_IOKIT_PROPERTY_INHERITANCE_DRYRUN_FAIL_PMAP_BOUNDARY;
    }

    pmap_boundary_ok = (contract->no_live_pmap_tables_installed == 0u &&
                        contract->proposed_workspace_written == 0u &&
                        contract->pmap_ttbr_written == 0u && contract->pmap_ttbcr_written == 0u &&
                        contract->pmap_dacr_written == 0u && contract->pmap_sctlr_written == 0u &&
                        contract->pmap_tlbs_invalidated == 0u && contract->caches_changed == 0u) ? 1u : 0u;
    if (contract->no_live_pmap_tables_installed == 0u) {
        contract->satisfied_mask |= STAGE77_XNU_IOKIT_PROPERTY_INHERITANCE_DRYRUN_SAT_NO_LIVE_PMAP_INSTALL;
    }
    if (contract->pmap_tlbs_invalidated == 0u && contract->caches_changed == 0u) {
        contract->satisfied_mask |= STAGE77_XNU_IOKIT_PROPERTY_INHERITANCE_DRYRUN_SAT_NO_TLB_CACHE_CHANGE;
    }
    if (pmap_boundary_ok && contract->source_pmap_transition_status == STAGE77_STATUS_OK) {
        contract->satisfied_mask |= STAGE77_XNU_IOKIT_PROPERTY_INHERITANCE_DRYRUN_SAT_PMAP_BOUNDARY;
    } else {
        contract->failure_mask |= STAGE77_XNU_IOKIT_PROPERTY_INHERITANCE_DRYRUN_FAIL_PMAP_BOUNDARY;
    }

    if (contract->local_only == 1u && contract->fail_closed == 1u &&
        contract->persistent_write_attempted == 0u && contract->xnu_start_executed == 0u &&
        contract->generated_macho_executed == 0u &&
        contract->proposed_platform_gap_mask == (STAGE77_PLATFORM_GAP_REQUIRED_RECORDED & ~STAGE77_PLATFORM_GAP_IOKIT_STACK) &&
        contract->proposed_pexpert_gap_mask == 0u &&
        contract->selected_registry_entry_mask == STAGE77_XNU_IOKIT_REGISTRY_SERVICE_SELECTED_REQUIRED &&
        contract->rejected_unattached_mask == STAGE77_XNU_IOKIT_REGISTRY_SERVICE_REJECTED_REQUIRED) {
        contract->satisfied_mask |= STAGE77_XNU_IOKIT_PROPERTY_INHERITANCE_DRYRUN_SAT_FAIL_CLOSED;
    } else {
        contract->failure_mask |= STAGE77_XNU_IOKIT_PROPERTY_INHERITANCE_DRYRUN_FAIL_ROLLUP |
            STAGE77_XNU_IOKIT_PROPERTY_INHERITANCE_DRYRUN_FAIL_SAFETY_BOUNDARY;
    }

    contract->checksum = stage77_iokit_property_inheritance_dryrun_checksum(contract);
    if (contract->checksum != stage77_iokit_property_inheritance_dryrun_checksum(contract)) {
        contract->failure_mask |= STAGE77_XNU_IOKIT_PROPERTY_INHERITANCE_DRYRUN_FAIL_CHECKSUM;
    }

    if (contract->satisfied_mask == contract->required_mask && contract->failure_mask == 0u) {
        contract->status = STAGE77_STATUS_OK;
    } else {
        contract->status = STAGE77_STATUS_BASE | contract->failure_mask;
    }

    stage77_iokit_property_inheritance_dryrun_log(contract);
    return contract->status == STAGE77_STATUS_OK;
}

const struct stage77_xnu_iokit_property_inheritance_dryrun_contract *
stage77_xnu_iokit_property_inheritance_dryrun_contract_result(void)
{
    return &g_stage77_xnu_iokit_property_inheritance_dryrun_contract;
}
