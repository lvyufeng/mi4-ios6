#include "stage79.h"

static struct stage79_xnu_iokit_match_dryrun_contract g_stage79_xnu_iokit_match_dryrun_contract;

static uint32_t stage79_iokit_match_dryrun_checksum(
    volatile const struct stage79_xnu_iokit_match_dryrun_contract *contract)
{
    volatile const uint32_t *words = (volatile const uint32_t *)(uintptr_t)contract;
    uint32_t count = (uint32_t)(offsetof(struct stage79_xnu_iokit_match_dryrun_contract, checksum) /
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

static uint32_t string_prop_equals(const struct boot_args *args, const void *node, const char *name, const char *expected)
{
    uint32_t len;
    const char *value = prop_string(args, node, name, &len);

    if (!value || len == 0u || !expected) {
        return 0u;
    }

    return (strcmp(value, expected) == 0) ? 1u : 0u;
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

static void stage79_iokit_match_dryrun_log(const struct stage79_xnu_iokit_match_dryrun_contract *contract)
{
    xnu_log_kv32("stage79_xnu_iokit_match_dryrun_contract_status", contract->status);
    xnu_log_kv32("stage79_xnu_iokit_match_dryrun_contract_required_mask", contract->required_mask);
    xnu_log_kv32("stage79_xnu_iokit_match_dryrun_contract_satisfied_mask", contract->satisfied_mask);
    xnu_log_kv32("stage79_xnu_iokit_match_dryrun_contract_failure_mask", contract->failure_mask);
    xnu_log_kv32("stage79_xnu_iokit_match_dryrun_contract_checksum", contract->checksum);
    xnu_log_kv32("stage79_xnu_iokit_match_provider_node_props", contract->provider_prop_count);
    xnu_log_kv32("stage79_xnu_iokit_match_driver_node_props", contract->driver_prop_count);
    xnu_log_kv32("stage79_xnu_iokit_match_provider_class_match", contract->provider_class_match);
    xnu_log_kv32("stage79_xnu_iokit_match_provider_path_match", contract->provider_path_match);
    xnu_log_kv32("stage79_xnu_iokit_match_category_match", contract->match_category_match);
    xnu_log_kv32("stage79_xnu_iokit_match_compatible_match", contract->compatible_match);
    xnu_log_kv32("stage79_xnu_iokit_match_driver_probe_score", contract->driver_probe_score);
    xnu_log_kv32("stage79_xnu_iokit_match_dryrun_match_count", contract->dryrun_match_count);
    xnu_log_kv32("stage79_xnu_iokit_match_attach_deferred", contract->attach_deferred);
    xnu_log_kv32("stage79_xnu_iokit_match_start_deferred", contract->start_deferred);
    xnu_log_kv32("stage79_xnu_iokit_match_iokit_reference_mask", contract->iokit_reference_mask);
    xnu_log_kv32("stage79_xnu_iokit_match_iokit_runtime_blocked_mask", contract->iokit_runtime_blocked_mask);
    xnu_log_kv32("stage79_xnu_iokit_match_public_compile_count", contract->iokit_public_compile_count);
    xnu_log_kv32("stage79_xnu_iokit_match_public_link_count", contract->iokit_public_link_count);
    xnu_log_kv32("stage79_xnu_iokit_match_no_iokit_runtime_exec", contract->no_iokit_runtime_exec);
    xnu_log_kv32("stage79_xnu_iokit_match_no_platform_driver_exec", contract->no_platform_driver_exec);
    xnu_log_kv32("stage79_xnu_iokit_match_no_live_pmap_tables_installed", contract->no_live_pmap_tables_installed);
    xnu_log_kv32("stage79_xnu_iokit_match_proposed_workspace_written", contract->proposed_workspace_written);
    xnu_log_kv32("stage79_xnu_iokit_match_pmap_ttbr_written", contract->pmap_ttbr_written);
    xnu_log_kv32("stage79_xnu_iokit_match_pmap_tlbs_invalidated", contract->pmap_tlbs_invalidated);
    xnu_log_kv32("stage79_xnu_iokit_match_caches_changed", contract->caches_changed);
    xnu_log_kv32("stage79_xnu_iokit_match_persistent_write_attempted", contract->persistent_write_attempted);
    xnu_log_kv32("stage79_xnu_iokit_match_platform_gap_mask", contract->proposed_platform_gap_mask);
}

int stage79_xnu_iokit_match_dryrun_contract_selftest(const struct stage79_loader_preflight *preflight)
{
    struct stage79_xnu_iokit_match_dryrun_contract *contract = &g_stage79_xnu_iokit_match_dryrun_contract;
    const struct boot_args *args = preflight ? (const struct boot_args *)(uintptr_t)preflight->boot_args_ptr :
        (const struct boot_args *)0;
    const void *root = args ? args->deviceTreeP : (const void *)0;
    uint32_t source_ok;
    uint32_t provider_ok;
    uint32_t driver_ok;
    uint32_t match_ok;
    uint32_t public_boundary_ok;
    uint32_t pmap_boundary_ok;

    memset(contract, 0, sizeof(*contract));
    contract->version = STAGE79_XNU_IOKIT_MATCH_DRYRUN_CONTRACT_VERSION;
    contract->size = sizeof(*contract);
    contract->status = STAGE79_STATUS_BASE;
    contract->required_mask = STAGE79_XNU_IOKIT_MATCH_DRYRUN_REQUIRED_MASK;

    if (!preflight || !args || !root) {
        contract->failure_mask = STAGE79_XNU_IOKIT_MATCH_DRYRUN_FAIL_SOURCE |
            STAGE79_XNU_IOKIT_MATCH_DRYRUN_FAIL_SAFETY_BOUNDARY;
        contract->checksum = stage79_iokit_match_dryrun_checksum(contract);
        stage79_iokit_match_dryrun_log(contract);
        return 0;
    }

    contract->source_scaffold_status = preflight->xnu_iokit_platform_scaffold_contract_status;
    contract->source_scaffold_satisfied_mask = preflight->xnu_iokit_platform_scaffold_contract_satisfied_mask;
    contract->source_scaffold_failure_mask = preflight->xnu_iokit_platform_scaffold_contract_failure_mask;
    contract->source_scaffold_checksum = preflight->xnu_iokit_platform_scaffold_contract_checksum;
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
    contract->source_loader_safety_required_mask = STAGE79_LOADER_SAFETY_REQUIRED;
    contract->boot_args_ptr = preflight->boot_args_ptr;
    contract->device_tree_ptr = preflight->device_tree_ptr;
    contract->device_tree_length = preflight->device_tree_length;
    contract->apple_dt_semantic_mask = preflight->apple_dt_semantic_mask;

    contract->provider_node_ptr = (uint32_t)(uintptr_t)apple_dt_find_child(args->deviceTreeP, args->deviceTreeLength,
                                                                           root, "iokit-platform-scaffold");
    contract->driver_node_ptr = (uint32_t)(uintptr_t)apple_dt_find_child(args->deviceTreeP, args->deviceTreeLength,
                                                                         root, "msm8974-platform-driver");

    contract->provider_prop_count = apple_dt_node_prop_count(args->deviceTreeP, args->deviceTreeLength,
                                                             (const void *)(uintptr_t)contract->provider_node_ptr);
    contract->provider_compatible_present = prop_present(args, (const void *)(uintptr_t)contract->provider_node_ptr,
                                                         "compatible");
    contract->provider_ioclass_present = prop_present(args, (const void *)(uintptr_t)contract->provider_node_ptr,
                                                      "IOClass");
    contract->provider_device_type_present = prop_present(args, (const void *)(uintptr_t)contract->provider_node_ptr,
                                                          "device_type");
    contract->provider_registry_plane_present = prop_present(args, (const void *)(uintptr_t)contract->provider_node_ptr,
                                                             "registry-plane");
    contract->provider_class_is_platform_device = string_prop_equals(args,
                                                                     (const void *)(uintptr_t)contract->provider_node_ptr,
                                                                     "IOClass", "IOPlatformExpertDevice");
    contract->provider_registry_is_device_tree = string_prop_equals(args,
                                                                    (const void *)(uintptr_t)contract->provider_node_ptr,
                                                                    "registry-plane", "IODeviceTree");

    contract->driver_prop_count = apple_dt_node_prop_count(args->deviceTreeP, args->deviceTreeLength,
                                                           (const void *)(uintptr_t)contract->driver_node_ptr);
    contract->driver_compatible_present = prop_present(args, (const void *)(uintptr_t)contract->driver_node_ptr,
                                                       "compatible");
    contract->driver_ioclass_present = prop_present(args, (const void *)(uintptr_t)contract->driver_node_ptr,
                                                    "IOClass");
    contract->driver_provider_present = prop_present(args, (const void *)(uintptr_t)contract->driver_node_ptr,
                                                     "IOProviderClass");
    contract->driver_match_category_present = prop_present(args, (const void *)(uintptr_t)contract->driver_node_ptr,
                                                           "IOMatchCategory");
    contract->driver_name_match_present = string_prop_equals(args, (const void *)(uintptr_t)contract->driver_node_ptr,
                                                             "name", "msm8974-platform-driver");
    contract->driver_probe_score_present = prop_present(args, (const void *)(uintptr_t)contract->driver_node_ptr,
                                                        "IOProbeScore");
    contract->driver_local_only_present = prop_present(args, (const void *)(uintptr_t)contract->driver_node_ptr,
                                                       "stage-owned-local-only");
    contract->driver_probe_score = apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength,
                                                         (const void *)(uintptr_t)contract->driver_node_ptr,
                                                         "IOProbeScore", 0u);
    contract->provider_class_match = string_prop_equals(args, (const void *)(uintptr_t)contract->driver_node_ptr,
                                                        "IOProviderClass", "IOPlatformExpertDevice");
    contract->provider_path_match = string_prop_equals(args, (const void *)(uintptr_t)contract->provider_node_ptr,
                                                       "IOProviderClass", "IODeviceTree:/");
    contract->match_category_match = string_prop_equals(args, (const void *)(uintptr_t)contract->driver_node_ptr,
                                                        "IOMatchCategory", "Stage79LocalPlatformScaffold");
    contract->compatible_match = string_prop_equals(args, (const void *)(uintptr_t)contract->driver_node_ptr,
                                                    "compatible", "qcom,msm8974-cancro-stage79");
    contract->driver_gic_base = reg_word(args, (const void *)(uintptr_t)contract->driver_node_ptr, 0u);
    contract->driver_timer_base = reg_word(args, (const void *)(uintptr_t)contract->driver_node_ptr, 2u);
    contract->driver_timebase_frequency = apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength,
                                                                (const void *)(uintptr_t)contract->driver_node_ptr,
                                                                "timebase-frequency", 0u);
    contract->driver_cpu_count = apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength,
                                                       (const void *)(uintptr_t)contract->driver_node_ptr,
                                                       "cpu-count", 0u);

    contract->iokit_reference_mask = preflight->xnu_iokit_platform_scaffold_contract.iokit_reference_mask;
    contract->iokit_runtime_blocked_mask = preflight->xnu_iokit_platform_scaffold_contract.iokit_runtime_blocked_mask;
    contract->iokit_reference_count = preflight->xnu_iokit_platform_scaffold_contract.iokit_reference_count;
    contract->iokit_public_compile_count = preflight->xnu_iokit_platform_scaffold_contract.iokit_public_compile_count;
    contract->iokit_public_link_count = preflight->xnu_iokit_platform_scaffold_contract.iokit_public_link_count;
    contract->iokit_reference_only = preflight->xnu_iokit_platform_scaffold_contract.iokit_reference_only;

    contract->provider_candidate_count = 1u;
    contract->driver_candidate_count = 1u;
    contract->selected_driver_count = (contract->provider_class_match == 1u &&
                                       contract->match_category_match == 1u &&
                                       contract->compatible_match == 1u) ? 1u : 0u;
    contract->rejected_driver_count = (contract->selected_driver_count == 1u) ? 0u : 1u;
    contract->dryrun_match_count = contract->selected_driver_count;
    contract->attach_deferred = 1u;
    contract->start_deferred = 1u;

    contract->no_iokit_runtime_exec = preflight->xnu_iokit_platform_scaffold_contract.no_iokit_runtime_exec;
    contract->no_platform_driver_exec = preflight->xnu_iokit_platform_scaffold_contract.no_platform_driver_exec;
    contract->no_public_xnu_exec = preflight->xnu_iokit_platform_scaffold_contract.no_public_xnu_exec;
    contract->no_platform_runtime_exec = preflight->xnu_iokit_platform_scaffold_contract.no_platform_runtime_exec;
    contract->no_public_pmap_exec = preflight->xnu_iokit_platform_scaffold_contract.no_public_pmap_exec;
    contract->no_live_pmap_tables_installed = preflight->xnu_iokit_platform_scaffold_contract.no_live_pmap_tables_installed;
    contract->proposed_workspace_written = preflight->xnu_iokit_platform_scaffold_contract.proposed_workspace_written;
    contract->pmap_ttbr_written = preflight->xnu_iokit_platform_scaffold_contract.pmap_ttbr_written;
    contract->pmap_ttbcr_written = preflight->xnu_iokit_platform_scaffold_contract.pmap_ttbcr_written;
    contract->pmap_dacr_written = preflight->xnu_iokit_platform_scaffold_contract.pmap_dacr_written;
    contract->pmap_sctlr_written = preflight->xnu_iokit_platform_scaffold_contract.pmap_sctlr_written;
    contract->pmap_tlbs_invalidated = preflight->xnu_iokit_platform_scaffold_contract.pmap_tlbs_invalidated;
    contract->caches_changed = preflight->xnu_iokit_platform_scaffold_contract.caches_changed;
    contract->persistent_write_attempted = preflight->xnu_iokit_platform_scaffold_contract.persistent_write_attempted;
    contract->xnu_start_executed = preflight->xnu_iokit_platform_scaffold_contract.xnu_start_executed;
    contract->generated_macho_executed = preflight->xnu_iokit_platform_scaffold_contract.generated_macho_executed;
    contract->proposed_platform_gap_mask = preflight->platform_gap_mask;
    contract->proposed_pexpert_gap_mask = preflight->pexpert_gap_mask;
    contract->local_only = 1u;
    contract->fail_closed = 1u;

    source_ok = (contract->source_scaffold_status == STAGE79_STATUS_OK &&
                 contract->source_scaffold_satisfied_mask == STAGE79_XNU_IOKIT_PLATFORM_SCAFFOLD_REQUIRED_MASK &&
                 contract->source_scaffold_failure_mask == 0u &&
                 contract->source_pexpert_status == STAGE79_STATUS_OK &&
                 contract->source_pexpert_failure_mask == 0u &&
                 contract->source_pmap_transition_status == STAGE79_STATUS_OK &&
                 contract->source_pmap_transition_failure_mask == 0u &&
                 contract->source_compile_graph_status == STAGE79_STATUS_OK &&
                 contract->source_compile_graph_failure_mask == 0u &&
                 contract->source_object_subset_status == STAGE79_STATUS_OK &&
                 contract->source_object_subset_failure_mask == 0u &&
                 contract->source_link_status == STAGE79_STATUS_OK &&
                 contract->source_link_failure_mask == 0u &&
                 contract->source_loader_safety_mask == contract->source_loader_safety_required_mask) ? 1u : 0u;
    if (source_ok) {
        contract->satisfied_mask |= STAGE79_XNU_IOKIT_MATCH_DRYRUN_SAT_SOURCE_SCAFFOLD;
    } else {
        contract->failure_mask |= STAGE79_XNU_IOKIT_MATCH_DRYRUN_FAIL_SOURCE;
    }

    if (contract->apple_dt_semantic_mask == STAGE79_DT_READY_REQUIRED) {
        contract->satisfied_mask |= STAGE79_XNU_IOKIT_MATCH_DRYRUN_SAT_APPLE_DT;
    } else {
        contract->failure_mask |= STAGE79_XNU_IOKIT_MATCH_DRYRUN_FAIL_DT;
    }

    provider_ok = (contract->provider_node_ptr != 0u && contract->provider_prop_count >= 6u &&
                   contract->provider_compatible_present == 1u &&
                   contract->provider_ioclass_present == 1u &&
                   contract->provider_device_type_present == 1u &&
                   contract->provider_registry_plane_present == 1u &&
                   contract->provider_class_is_platform_device == 1u &&
                   contract->provider_registry_is_device_tree == 1u) ? 1u : 0u;
    if (provider_ok) {
        contract->satisfied_mask |= STAGE79_XNU_IOKIT_MATCH_DRYRUN_SAT_PROVIDER_NODE;
    } else {
        contract->failure_mask |= STAGE79_XNU_IOKIT_MATCH_DRYRUN_FAIL_PROVIDER;
    }

    driver_ok = (contract->driver_node_ptr != 0u && contract->driver_prop_count >= 10u &&
                 contract->driver_compatible_present == 1u &&
                 contract->driver_ioclass_present == 1u &&
                 contract->driver_provider_present == 1u &&
                 contract->driver_match_category_present == 1u &&
                 contract->driver_name_match_present == 1u &&
                 contract->driver_probe_score_present == 1u &&
                 contract->driver_local_only_present == 1u) ? 1u : 0u;
    if (driver_ok) {
        contract->satisfied_mask |= STAGE79_XNU_IOKIT_MATCH_DRYRUN_SAT_DRIVER_NODE;
    } else {
        contract->failure_mask |= STAGE79_XNU_IOKIT_MATCH_DRYRUN_FAIL_DRIVER;
    }

    if (contract->provider_class_match == 1u && contract->provider_path_match == 1u) {
        contract->satisfied_mask |= STAGE79_XNU_IOKIT_MATCH_DRYRUN_SAT_PROVIDER_CLASS_MATCH;
    } else {
        contract->failure_mask |= STAGE79_XNU_IOKIT_MATCH_DRYRUN_FAIL_MATCH;
    }
    if (contract->match_category_match == 1u) {
        contract->satisfied_mask |= STAGE79_XNU_IOKIT_MATCH_DRYRUN_SAT_MATCH_CATEGORY;
    } else {
        contract->failure_mask |= STAGE79_XNU_IOKIT_MATCH_DRYRUN_FAIL_MATCH;
    }
    if (contract->compatible_match == 1u) {
        contract->satisfied_mask |= STAGE79_XNU_IOKIT_MATCH_DRYRUN_SAT_COMPATIBLE_MATCH;
    } else {
        contract->failure_mask |= STAGE79_XNU_IOKIT_MATCH_DRYRUN_FAIL_MATCH;
    }

    if (contract->driver_gic_base == STAGE79_XNU_IOKIT_PLATFORM_EXPECTED_GIC_DIST_BASE &&
        contract->driver_timer_base == STAGE79_XNU_IOKIT_PLATFORM_EXPECTED_TIMER_BASE &&
        contract->driver_timebase_frequency == STAGE79_XNU_IOKIT_PLATFORM_EXPECTED_TIMEBASE_FREQ &&
        contract->driver_cpu_count == STAGE79_XNU_IOKIT_PLATFORM_EXPECTED_CPU_COUNT) {
        contract->satisfied_mask |= STAGE79_XNU_IOKIT_MATCH_DRYRUN_SAT_MSM8974_FACTS;
    } else {
        contract->failure_mask |= STAGE79_XNU_IOKIT_MATCH_DRYRUN_FAIL_DRIVER;
    }

    if (contract->iokit_reference_mask == STAGE79_XNU_IOKIT_REFERENCE_REQUIRED &&
        contract->iokit_runtime_blocked_mask == STAGE79_XNU_IOKIT_REFERENCE_REQUIRED &&
        contract->iokit_reference_count >= 3u && contract->iokit_public_compile_count == 0u &&
        contract->iokit_public_link_count == 0u && contract->iokit_reference_only == 1u) {
        contract->satisfied_mask |= STAGE79_XNU_IOKIT_MATCH_DRYRUN_SAT_IOKIT_REFERENCE;
    } else {
        contract->failure_mask |= STAGE79_XNU_IOKIT_MATCH_DRYRUN_FAIL_PUBLIC_BOUNDARY;
    }

    match_ok = (contract->provider_candidate_count == 1u && contract->driver_candidate_count == 1u &&
                contract->selected_driver_count == 1u && contract->rejected_driver_count == 0u &&
                contract->dryrun_match_count == 1u) ? 1u : 0u;
    if (match_ok) {
        contract->satisfied_mask |= STAGE79_XNU_IOKIT_MATCH_DRYRUN_SAT_SERVICE_MATCH_DRYRUN;
    } else {
        contract->failure_mask |= STAGE79_XNU_IOKIT_MATCH_DRYRUN_FAIL_MATCH;
    }

    if (contract->driver_probe_score == STAGE79_XNU_IOKIT_MATCH_EXPECTED_PROBE_SCORE) {
        contract->satisfied_mask |= STAGE79_XNU_IOKIT_MATCH_DRYRUN_SAT_PROBE_SCORE;
    } else {
        contract->failure_mask |= STAGE79_XNU_IOKIT_MATCH_DRYRUN_FAIL_MATCH;
    }

    if (contract->attach_deferred == 1u && contract->start_deferred == 1u) {
        contract->satisfied_mask |= STAGE79_XNU_IOKIT_MATCH_DRYRUN_SAT_ATTACH_START_DEFERRED;
    } else {
        contract->failure_mask |= STAGE79_XNU_IOKIT_MATCH_DRYRUN_FAIL_SAFETY_BOUNDARY;
    }

    public_boundary_ok = (contract->no_public_xnu_exec == 1u && contract->no_platform_runtime_exec == 1u &&
                          contract->no_iokit_runtime_exec == 1u && contract->no_platform_driver_exec == 1u &&
                          contract->source_object_subset_status == STAGE79_STATUS_OK &&
                          contract->source_link_status == STAGE79_STATUS_OK) ? 1u : 0u;
    if (public_boundary_ok) {
        contract->satisfied_mask |= STAGE79_XNU_IOKIT_MATCH_DRYRUN_SAT_PUBLIC_BOUNDARY |
            STAGE79_XNU_IOKIT_MATCH_DRYRUN_SAT_NO_IOKIT_RUNTIME_EXEC |
            STAGE79_XNU_IOKIT_MATCH_DRYRUN_SAT_NO_PLATFORM_DRIVER_EXEC |
            STAGE79_XNU_IOKIT_MATCH_DRYRUN_SAT_NO_PUBLIC_XNU_EXEC;
    } else {
        contract->failure_mask |= STAGE79_XNU_IOKIT_MATCH_DRYRUN_FAIL_PUBLIC_BOUNDARY;
    }

    if (contract->no_public_pmap_exec == 1u) {
        contract->satisfied_mask |= STAGE79_XNU_IOKIT_MATCH_DRYRUN_SAT_NO_PUBLIC_PMAP_EXEC;
    } else {
        contract->failure_mask |= STAGE79_XNU_IOKIT_MATCH_DRYRUN_FAIL_PMAP_BOUNDARY;
    }

    pmap_boundary_ok = (contract->no_live_pmap_tables_installed == 0u &&
                        contract->proposed_workspace_written == 0u &&
                        contract->pmap_ttbr_written == 0u && contract->pmap_ttbcr_written == 0u &&
                        contract->pmap_dacr_written == 0u && contract->pmap_sctlr_written == 0u &&
                        contract->pmap_tlbs_invalidated == 0u && contract->caches_changed == 0u) ? 1u : 0u;
    if (contract->no_live_pmap_tables_installed == 0u) {
        contract->satisfied_mask |= STAGE79_XNU_IOKIT_MATCH_DRYRUN_SAT_NO_LIVE_PMAP_INSTALL;
    }
    if (contract->proposed_workspace_written == 0u) {
        contract->satisfied_mask |= STAGE79_XNU_IOKIT_MATCH_DRYRUN_SAT_NO_PROPOSED_PMAP_WRITE;
    }
    if (contract->pmap_ttbr_written == 0u && contract->pmap_ttbcr_written == 0u &&
        contract->pmap_dacr_written == 0u && contract->pmap_sctlr_written == 0u) {
        contract->satisfied_mask |= STAGE79_XNU_IOKIT_MATCH_DRYRUN_SAT_NO_PMAP_CONTROL_WRITE;
    }
    if (contract->pmap_tlbs_invalidated == 0u && contract->caches_changed == 0u) {
        contract->satisfied_mask |= STAGE79_XNU_IOKIT_MATCH_DRYRUN_SAT_NO_TLB_CACHE_CHANGE;
    }
    if (pmap_boundary_ok && contract->source_pmap_transition_status == STAGE79_STATUS_OK &&
        contract->source_pmap_transition_failure_mask == 0u) {
        contract->satisfied_mask |= STAGE79_XNU_IOKIT_MATCH_DRYRUN_SAT_PMAP_BOUNDARY;
    } else {
        contract->failure_mask |= STAGE79_XNU_IOKIT_MATCH_DRYRUN_FAIL_PMAP_BOUNDARY;
    }

    if (contract->xnu_start_executed == 0u && contract->generated_macho_executed == 0u) {
        contract->satisfied_mask |= STAGE79_XNU_IOKIT_MATCH_DRYRUN_SAT_NO_XNU_MACHO_EXEC;
    } else {
        contract->failure_mask |= STAGE79_XNU_IOKIT_MATCH_DRYRUN_FAIL_SAFETY_BOUNDARY;
    }
    if (contract->persistent_write_attempted == 0u) {
        contract->satisfied_mask |= STAGE79_XNU_IOKIT_MATCH_DRYRUN_SAT_NO_PERSIST_WRITE;
    } else {
        contract->failure_mask |= STAGE79_XNU_IOKIT_MATCH_DRYRUN_FAIL_SAFETY_BOUNDARY;
    }

    if (contract->proposed_platform_gap_mask == (STAGE79_PLATFORM_GAP_REQUIRED_RECORDED & ~STAGE79_PLATFORM_GAP_IOKIT_STACK) &&
        contract->proposed_pexpert_gap_mask == 0u) {
        contract->satisfied_mask |= STAGE79_XNU_IOKIT_MATCH_DRYRUN_SAT_PLATFORM_ROLLUP;
    } else {
        contract->failure_mask |= STAGE79_XNU_IOKIT_MATCH_DRYRUN_FAIL_ROLLUP;
    }

    if (contract->local_only == 1u && contract->fail_closed == 1u) {
        contract->satisfied_mask |= STAGE79_XNU_IOKIT_MATCH_DRYRUN_SAT_FAIL_CLOSED;
    } else {
        contract->failure_mask |= STAGE79_XNU_IOKIT_MATCH_DRYRUN_FAIL_SAFETY_BOUNDARY;
    }

    contract->checksum = stage79_iokit_match_dryrun_checksum(contract);
    if (contract->checksum != stage79_iokit_match_dryrun_checksum(contract)) {
        contract->failure_mask |= STAGE79_XNU_IOKIT_MATCH_DRYRUN_FAIL_CHECKSUM;
    }

    if (contract->satisfied_mask == contract->required_mask && contract->failure_mask == 0u) {
        contract->status = STAGE79_STATUS_OK;
    } else {
        contract->status = STAGE79_STATUS_BASE | contract->failure_mask;
    }

    stage79_iokit_match_dryrun_log(contract);
    return contract->status == STAGE79_STATUS_OK;
}

const struct stage79_xnu_iokit_match_dryrun_contract *stage79_xnu_iokit_match_dryrun_contract_result(void)
{
    return &g_stage79_xnu_iokit_match_dryrun_contract;
}
