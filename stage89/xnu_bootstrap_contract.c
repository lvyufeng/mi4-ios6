#include "stage89.h"

static struct stage89_xnu_bootstrap_contract g_stage89_xnu_bootstrap_contract;

static uint32_t stage89_xnu_bootstrap_contract_checksum(volatile const struct stage89_xnu_bootstrap_contract *contract)
{
    volatile const uint32_t *words = (volatile const uint32_t *)(uintptr_t)contract;
    uint32_t count = (uint32_t)(offsetof(struct stage89_xnu_bootstrap_contract, checksum) / sizeof(uint32_t));
    uint32_t checksum = 0u;

    for (uint32_t i = 0; i < count; i++) {
        checksum ^= words[i];
    }

    return checksum;
}

static uint32_t stage89_contract_add_overflow_u32(uint32_t a, uint32_t b, uint32_t *out)
{
    *out = a + b;
    return *out < a;
}

static uint32_t stage89_contract_sub_valid_u32(uint32_t base, uint32_t limit, uint32_t *out)
{
    if (limit <= base) {
        *out = 0u;
        return 0u;
    }
    *out = limit - base;
    return 1u;
}

static uint32_t stage89_contract_is_page_aligned(uint32_t value)
{
    return (value & STAGE89_XNU_BOOTSTRAP_CONTRACT_PAGE_ALIGN_MASK) == 0u;
}

static uint32_t stage89_contract_page_count(uint32_t bytes)
{
    if (bytes == 0u) {
        return 0u;
    }
    return (bytes + (STAGE89_XNU_BOOTSTRAP_CONTRACT_PAGE_SIZE - 1u)) >> 12;
}

static uint32_t stage89_contract_ranges_overlap(uint32_t a_base, uint32_t a_limit,
                                                uint32_t b_base, uint32_t b_limit)
{
    if (a_limit <= a_base || b_limit <= b_base) {
        return 1u;
    }
    return (a_base < b_limit && b_base < a_limit) ? 1u : 0u;
}

static void stage89_xnu_bootstrap_contract_log(const struct stage89_xnu_bootstrap_contract *contract)
{
    xnu_log_kv32("stage89_xnu_bootstrap_contract_status", contract->status);
    xnu_log_kv32("stage89_xnu_bootstrap_contract_required_mask", contract->required_mask);
    xnu_log_kv32("stage89_xnu_bootstrap_contract_satisfied_mask", contract->satisfied_mask);
    xnu_log_kv32("stage89_xnu_bootstrap_contract_failure_mask", contract->failure_mask);
    xnu_log_kv32("stage89_xnu_bootstrap_contract_checksum", contract->checksum);
    xnu_log_kv32("stage89_xnu_bootstrap_contract_source_macho_status", contract->source_macho_status);
    xnu_log_kv32("stage89_xnu_bootstrap_contract_source_staging_status", contract->source_staging_status);
    xnu_log_kv32("stage89_xnu_bootstrap_contract_source_materialized_status", contract->source_materialized_status);
    xnu_log_kv32("stage89_xnu_bootstrap_contract_source_tte_dryrun_status", contract->source_tte_dryrun_status);
    xnu_log_kv32("stage89_xnu_bootstrap_contract_source_safe_table_status", contract->source_safe_table_status);
    xnu_log_kv32("stage89_xnu_bootstrap_contract_source_ttbr_roundtrip_status", contract->source_ttbr_roundtrip_status);
    xnu_log_kv32("stage89_xnu_bootstrap_contract_source_ttbr_restored_status", contract->source_ttbr_restored_status);
    xnu_log_kv32("stage89_xnu_bootstrap_contract_source_cache_preserved_status", contract->source_cache_preserved_status);
    xnu_log_kv32("stage89_xnu_bootstrap_contract_source_compile_graph_status", contract->source_compile_graph_status);
    xnu_log_kv32("stage89_xnu_bootstrap_contract_source_object_subset_status", contract->source_object_subset_status);
    xnu_log_kv32("stage89_xnu_bootstrap_contract_source_link_status", contract->source_link_status);
    xnu_log_kv32("stage89_xnu_bootstrap_contract_proposed_virtBase", contract->proposed_virtBase);
    xnu_log_kv32("stage89_xnu_bootstrap_contract_proposed_physBase", contract->proposed_physBase);
    xnu_log_kv32("stage89_xnu_bootstrap_contract_proposed_memSize", contract->proposed_memSize);
    xnu_log_kv32("stage89_xnu_bootstrap_contract_loaded_phys_base", contract->loaded_phys_base);
    xnu_log_kv32("stage89_xnu_bootstrap_contract_loaded_phys_end", contract->loaded_phys_end);
    xnu_log_kv32("stage89_xnu_bootstrap_contract_loaded_virt_base", contract->loaded_virt_base);
    xnu_log_kv32("stage89_xnu_bootstrap_contract_loaded_virt_end", contract->loaded_virt_end);
    xnu_log_kv32("stage89_xnu_bootstrap_contract_loaded_file_end", contract->loaded_file_end);
    xnu_log_kv32("stage89_xnu_bootstrap_contract_proposed_topOfKernelData", contract->proposed_topOfKernelData);
    xnu_log_kv32("stage89_xnu_bootstrap_contract_ttep_workspace_base", contract->ttep_workspace_base);
    xnu_log_kv32("stage89_xnu_bootstrap_contract_ttep_workspace_size", contract->ttep_workspace_size);
    xnu_log_kv32("stage89_xnu_bootstrap_contract_ttep_workspace_limit", contract->ttep_workspace_limit);
    xnu_log_kv32("stage89_xnu_bootstrap_contract_proposed_avail_start", contract->proposed_avail_start);
    xnu_log_kv32("stage89_xnu_bootstrap_contract_proposed_avail_end", contract->proposed_avail_end);
    xnu_log_kv32("stage89_xnu_bootstrap_contract_page_size", contract->page_size);
    xnu_log_kv32("stage89_xnu_bootstrap_contract_page_align_mask", contract->page_align_mask);
    xnu_log_kv32("stage89_xnu_bootstrap_contract_loaded_image_bytes", contract->loaded_image_bytes);
    xnu_log_kv32("stage89_xnu_bootstrap_contract_loaded_image_page_count", contract->loaded_image_page_count);
    xnu_log_kv32("stage89_xnu_bootstrap_contract_loaded_file_page_count", contract->loaded_file_page_count);
    xnu_log_kv32("stage89_xnu_bootstrap_contract_zero_fill_bytes", contract->zero_fill_bytes);
    xnu_log_kv32("stage89_xnu_bootstrap_contract_zero_fill_page_count", contract->zero_fill_page_count);
    xnu_log_kv32("stage89_xnu_bootstrap_contract_workspace_page_count", contract->workspace_page_count);
    xnu_log_kv32("stage89_xnu_bootstrap_contract_identity_l1_first_index", contract->identity_l1_first_index);
    xnu_log_kv32("stage89_xnu_bootstrap_contract_identity_l1_last_index", contract->identity_l1_last_index);
    xnu_log_kv32("stage89_xnu_bootstrap_contract_identity_l1_section_count", contract->identity_l1_section_count);
    xnu_log_kv32("stage89_xnu_bootstrap_contract_highva_l1_first_index", contract->highva_l1_first_index);
    xnu_log_kv32("stage89_xnu_bootstrap_contract_highva_l1_last_index", contract->highva_l1_last_index);
    xnu_log_kv32("stage89_xnu_bootstrap_contract_highva_l1_section_count", contract->highva_l1_section_count);
    xnu_log_kv32("stage89_xnu_bootstrap_contract_page_coverage_mask", contract->page_coverage_mask);
    xnu_log_kv32("stage89_xnu_bootstrap_contract_section_coverage_mask", contract->section_coverage_mask);
    xnu_log_kv32("stage89_xnu_bootstrap_contract_stage_image_base", contract->stage_image_base);
    xnu_log_kv32("stage89_xnu_bootstrap_contract_stage_image_end", contract->stage_image_end);
    xnu_log_kv32("stage89_xnu_bootstrap_contract_device_tree_base", contract->device_tree_base);
    xnu_log_kv32("stage89_xnu_bootstrap_contract_device_tree_end", contract->device_tree_end);
    xnu_log_kv32("stage89_xnu_bootstrap_contract_staging_arena_base", contract->staging_arena_base);
    xnu_log_kv32("stage89_xnu_bootstrap_contract_staging_arena_end", contract->staging_arena_end);
    xnu_log_kv32("stage89_xnu_bootstrap_contract_safe_table_base", contract->safe_table_base);
    xnu_log_kv32("stage89_xnu_bootstrap_contract_safe_table_end", contract->safe_table_end);
    xnu_log_kv32("stage89_xnu_bootstrap_contract_ram_console_base", contract->ram_console_base);
    xnu_log_kv32("stage89_xnu_bootstrap_contract_ram_console_end", contract->ram_console_end);
    xnu_log_kv32("stage89_xnu_bootstrap_contract_no_public_xnu_exec", contract->no_public_xnu_exec);
    xnu_log_kv32("stage89_xnu_bootstrap_contract_no_platform_runtime_exec", contract->no_platform_runtime_exec);
    xnu_log_kv32("stage89_xnu_bootstrap_contract_no_macho_exec", contract->no_macho_exec);
    xnu_log_kv32("stage89_xnu_bootstrap_contract_no_proposed_phys_write", contract->no_proposed_phys_write);
    xnu_log_kv32("stage89_xnu_bootstrap_contract_no_proposed_tte_write", contract->no_proposed_tte_write);
    xnu_log_kv32("stage89_xnu_bootstrap_contract_no_persist_write", contract->no_persist_write);
    xnu_log_kv32("stage89_xnu_bootstrap_contract_no_cache_policy_change", contract->no_cache_policy_change);
    xnu_log_kv32("stage89_xnu_bootstrap_contract_fail_closed", contract->fail_closed);
}

int stage89_xnu_bootstrap_contract_selftest(const struct stage89_loader_preflight *preflight)
{
    struct stage89_xnu_bootstrap_contract *contract = &g_stage89_xnu_bootstrap_contract;
    const struct stage89_ttbr0_roundtrip *ttbr_rt = mmu_stage89_ttbr0_roundtrip_result();
    uint32_t proposed_mapping_base = 0u;
    uint32_t proposed_mapping_limit = 0u;
    uint32_t loaded_file_bytes = 0u;
    uint32_t source_rollups_ok = 0u;

    memset(contract, 0, sizeof(*contract));
    contract->version = STAGE89_XNU_BOOTSTRAP_CONTRACT_VERSION;
    contract->size = sizeof(*contract);
    contract->status = STAGE89_STATUS_BASE;
    contract->required_mask = STAGE89_XNU_BOOTSTRAP_CONTRACT_REQUIRED_MASK;
    contract->page_size = STAGE89_XNU_BOOTSTRAP_CONTRACT_PAGE_SIZE;
    contract->page_align_mask = STAGE89_XNU_BOOTSTRAP_CONTRACT_PAGE_ALIGN_MASK;
    contract->ram_console_base = RAM_CONSOLE_BASE;
    contract->ram_console_end = RAM_CONSOLE_BASE + RAM_CONSOLE_SIZE;
    contract->stage_image_base = STAGE89_BASE;
    contract->stage_image_end = (uint32_t)(uintptr_t)__stage89_image_end;

    xnu_log_puts("Stage84 XNU bootstrap mapping contract begin\n");

    if (!preflight || !ttbr_rt) {
        contract->failure_mask |= STAGE89_XNU_BOOTSTRAP_CONTRACT_FAIL_SOURCE_ROLLUP |
                                  STAGE89_XNU_BOOTSTRAP_CONTRACT_FAIL_SAFETY_BOUNDARY;
        goto finish;
    }

    contract->source_macho_status = preflight->macho.status;
    contract->source_staging_status = preflight->staging.status;
    contract->source_materialized_status = preflight->materialized_status;
    contract->source_tte_dryrun_status = preflight->tte_dryrun_status;
    contract->source_safe_table_status = preflight->safe_table_status;
    contract->source_ttbr_roundtrip_status = preflight->ttbr_roundtrip_status;
    contract->source_ttbr_restored_status = preflight->ttbr_restored_status;
    contract->source_cache_preserved_status = preflight->cache_preserved_status;
    contract->source_compile_graph_status = preflight->xnu_compile_graph_status;
    contract->source_object_subset_status = preflight->xnu_object_subset_status;
    contract->source_link_status = preflight->xnu_link_status;

    contract->proposed_virtBase = preflight->proposed_virtBase;
    contract->proposed_physBase = preflight->proposed_physBase;
    contract->proposed_memSize = preflight->proposed_memSize;
    contract->loaded_phys_base = preflight->proposed_loaded_phys_base;
    contract->loaded_phys_end = preflight->proposed_loaded_phys_end;
    contract->loaded_virt_base = preflight->macho.load_vm_base;
    contract->loaded_virt_end = preflight->macho.load_vm_end;
    contract->loaded_file_end = preflight->proposed_loaded_file_end;
    contract->proposed_topOfKernelData = preflight->proposed_topOfKernelData;
    contract->ttep_workspace_base = preflight->proposed_ttep_workspace_base;
    contract->ttep_workspace_size = preflight->proposed_ttep_workspace_size;
    contract->ttep_workspace_limit = preflight->proposed_ttep_workspace_limit;
    contract->proposed_avail_start = preflight->proposed_avail_start;
    contract->zero_fill_bytes = preflight->materialized_zero_bytes;
    contract->workspace_page_count = STAGE89_XNU_TTE_WORKSPACE_PAGES;
    contract->identity_l1_first_index = preflight->tte_dryrun.kernel_l1_first_index;
    contract->identity_l1_last_index = preflight->tte_dryrun.kernel_l1_last_index;
    contract->identity_l1_section_count = preflight->tte_dryrun.kernel_l1_section_count;
    contract->highva_l1_first_index = preflight->tte_dryrun.highva_l1_first_index;
    contract->highva_l1_last_index = preflight->tte_dryrun.highva_l1_last_index;
    contract->highva_l1_section_count = preflight->tte_dryrun.highva_l1_section_count;
    contract->device_tree_base = preflight->device_tree_ptr;
    if (stage89_contract_add_overflow_u32(contract->device_tree_base, preflight->device_tree_length,
                                          &contract->device_tree_end)) {
        contract->failure_mask |= STAGE89_XNU_BOOTSTRAP_CONTRACT_FAIL_DEVICE_TREE_OVERLAP;
    }
    contract->staging_arena_base = preflight->staging.arena_base;
    contract->staging_arena_end = preflight->staging.arena_end;
    contract->safe_table_base = preflight->tte_dryrun.safe_table.local_base;
    contract->safe_table_end = preflight->tte_dryrun.safe_table.local_limit;

    if (stage89_contract_add_overflow_u32(contract->proposed_physBase, contract->proposed_memSize,
                                          &contract->proposed_avail_end)) {
        contract->failure_mask |= STAGE89_XNU_BOOTSTRAP_CONTRACT_FAIL_AVAIL_RANGE;
    }
    if (stage89_contract_sub_valid_u32(contract->loaded_phys_base, contract->loaded_phys_end,
                                       &contract->loaded_image_bytes)) {
        contract->loaded_image_page_count = stage89_contract_page_count(contract->loaded_image_bytes);
        contract->page_coverage_mask |= STAGE89_XNU_BOOTSTRAP_CONTRACT_PAGE_COVER_LOADED_IMAGE;
    }
    if (stage89_contract_sub_valid_u32(preflight->macho.load_file_base, preflight->macho.load_file_end,
                                       &loaded_file_bytes)) {
        contract->loaded_file_page_count = stage89_contract_page_count(loaded_file_bytes);
        contract->page_coverage_mask |= STAGE89_XNU_BOOTSTRAP_CONTRACT_PAGE_COVER_FILE_BYTES;
    } else if (contract->loaded_file_end != 0u) {
        contract->loaded_file_page_count = stage89_contract_page_count(contract->loaded_file_end);
        contract->page_coverage_mask |= STAGE89_XNU_BOOTSTRAP_CONTRACT_PAGE_COVER_FILE_BYTES;
    }
    contract->zero_fill_page_count = stage89_contract_page_count(contract->zero_fill_bytes);
    if (contract->zero_fill_page_count != 0u || contract->source_staging_status == STAGE89_STATUS_OK) {
        contract->page_coverage_mask |= STAGE89_XNU_BOOTSTRAP_CONTRACT_PAGE_COVER_ZERO_FILL;
    }
    if (contract->workspace_page_count == STAGE89_XNU_TTE_WORKSPACE_PAGES &&
        contract->ttep_workspace_size == STAGE89_XNU_TTE_WORKSPACE_BYTES) {
        contract->page_coverage_mask |= STAGE89_XNU_BOOTSTRAP_CONTRACT_PAGE_COVER_WORKSPACE;
    }
    if (contract->highva_l1_section_count != 0u && contract->identity_l1_section_count != 0u) {
        contract->page_coverage_mask |= STAGE89_XNU_BOOTSTRAP_CONTRACT_PAGE_COVER_L1_SECTIONS;
    }

    if ((preflight->tte_dryrun.satisfied_mask & STAGE89_TTE_SAT_TRANSLATION_LOW_RAM) != 0u) {
        contract->section_coverage_mask |= STAGE89_XNU_BOOTSTRAP_CONTRACT_SECTION_COVER_IDENTITY_LOW;
    }
    if ((preflight->tte_dryrun.satisfied_mask & STAGE89_TTE_SAT_TRANSLATION_KERNEL) != 0u) {
        contract->section_coverage_mask |= STAGE89_XNU_BOOTSTRAP_CONTRACT_SECTION_COVER_IDENTITY_KERNEL;
    }
    if ((preflight->tte_dryrun.satisfied_mask & STAGE89_TTE_SAT_TRANSLATION_CONSOLE) != 0u) {
        contract->section_coverage_mask |= STAGE89_XNU_BOOTSTRAP_CONTRACT_SECTION_COVER_RAM_CONSOLE;
    }
    if ((preflight->tte_dryrun.satisfied_mask & STAGE89_TTE_SAT_TRANSLATION_DEVICE) != 0u) {
        contract->section_coverage_mask |= STAGE89_XNU_BOOTSTRAP_CONTRACT_SECTION_COVER_GIC;
    }
    if ((preflight->tte_dryrun.satisfied_mask & (STAGE89_TTE_SAT_HIGHVA_MAPPING |
                                                STAGE89_TTE_SAT_HIGHVA_TRANSLATION)) ==
        (STAGE89_TTE_SAT_HIGHVA_MAPPING | STAGE89_TTE_SAT_HIGHVA_TRANSLATION)) {
        contract->section_coverage_mask |= STAGE89_XNU_BOOTSTRAP_CONTRACT_SECTION_COVER_HIGHVA;
    }

    source_rollups_ok = (contract->source_macho_status == STAGE89_STATUS_OK &&
                         contract->source_staging_status == STAGE89_STATUS_OK &&
                         contract->source_materialized_status == STAGE89_STATUS_OK &&
                         contract->source_tte_dryrun_status == STAGE89_STATUS_OK &&
                         contract->source_safe_table_status == STAGE89_STATUS_OK &&
                         contract->source_ttbr_roundtrip_status == STAGE89_STATUS_OK &&
                         contract->source_ttbr_restored_status == STAGE89_STATUS_OK &&
                         contract->source_cache_preserved_status == STAGE89_STATUS_OK &&
                         contract->source_compile_graph_status == STAGE89_STATUS_OK &&
                         contract->source_object_subset_status == STAGE89_STATUS_OK &&
                         contract->source_link_status == STAGE89_STATUS_OK &&
                         preflight->macho.failure_mask == 0u && preflight->staging.failure_mask == 0u &&
                         preflight->tte_dryrun_failure_mask == 0u &&
                         preflight->ttbr_roundtrip_failure_mask == 0u &&
                         preflight->xnu_compile_graph_failure_mask == 0u &&
                         preflight->xnu_object_subset_failure_mask == 0u &&
                         preflight->xnu_link_failure_mask == 0u) ? 1u : 0u;
    if (source_rollups_ok) {
        contract->satisfied_mask |= STAGE89_XNU_BOOTSTRAP_CONTRACT_SAT_SOURCE_ROLLUPS;
    } else {
        contract->failure_mask |= STAGE89_XNU_BOOTSTRAP_CONTRACT_FAIL_SOURCE_ROLLUP;
    }
    if (contract->source_macho_status == STAGE89_STATUS_OK) {
        contract->satisfied_mask |= STAGE89_XNU_BOOTSTRAP_CONTRACT_SAT_MACHO_IMPORTED;
    }
    if (contract->source_staging_status == STAGE89_STATUS_OK && contract->source_materialized_status == STAGE89_STATUS_OK) {
        contract->satisfied_mask |= STAGE89_XNU_BOOTSTRAP_CONTRACT_SAT_STAGING_IMPORTED;
    }
    if (contract->source_tte_dryrun_status == STAGE89_STATUS_OK) {
        contract->satisfied_mask |= STAGE89_XNU_BOOTSTRAP_CONTRACT_SAT_TTE_IMPORTED;
    }
    if (preflight->highva_dryrun_status == STAGE89_STATUS_OK && contract->highva_l1_section_count != 0u) {
        contract->satisfied_mask |= STAGE89_XNU_BOOTSTRAP_CONTRACT_SAT_HIGHVA_IMPORTED;
    }
    if (contract->source_safe_table_status == STAGE89_STATUS_OK && preflight->stage_owned_tables_status == STAGE89_STATUS_OK &&
        preflight->tte_dryrun.stage_owned_tables_only == 1u && preflight->tte_dryrun.proposed_tte_workspace_written == 0u) {
        contract->satisfied_mask |= STAGE89_XNU_BOOTSTRAP_CONTRACT_SAT_SAFE_TABLE_LOCAL_ONLY;
    } else {
        contract->failure_mask |= STAGE89_XNU_BOOTSTRAP_CONTRACT_FAIL_LOCAL_ARENA_OVERLAP;
    }
    if (contract->source_ttbr_restored_status == STAGE89_STATUS_OK && ttbr_rt->ttbr0_write_count != 0u &&
        (ttbr_rt->satisfied_mask & STAGE89_TTBR_RT_SAT_ORIGINAL_RESTORED) != 0u) {
        contract->satisfied_mask |= STAGE89_XNU_BOOTSTRAP_CONTRACT_SAT_TTBR_RESTORED;
    } else {
        contract->failure_mask |= STAGE89_XNU_BOOTSTRAP_CONTRACT_FAIL_TTBR_RESTORE;
    }
    if (contract->source_cache_preserved_status == STAGE89_STATUS_OK && ttbr_rt->caches_changed == 0u) {
        contract->satisfied_mask |= STAGE89_XNU_BOOTSTRAP_CONTRACT_SAT_CACHES_PRESERVED;
    } else {
        contract->failure_mask |= STAGE89_XNU_BOOTSTRAP_CONTRACT_FAIL_CACHE_POLICY;
    }

    if (stage89_contract_is_page_aligned(contract->proposed_virtBase)) {
        contract->satisfied_mask |= STAGE89_XNU_BOOTSTRAP_CONTRACT_SAT_VIRTBASE_ALIGNED;
    } else {
        contract->failure_mask |= STAGE89_XNU_BOOTSTRAP_CONTRACT_FAIL_ALIGNMENT;
    }
    if (stage89_contract_is_page_aligned(contract->proposed_physBase)) {
        contract->satisfied_mask |= STAGE89_XNU_BOOTSTRAP_CONTRACT_SAT_PHYSBASE_ALIGNED;
    } else {
        contract->failure_mask |= STAGE89_XNU_BOOTSTRAP_CONTRACT_FAIL_ALIGNMENT;
    }
    if (stage89_contract_is_page_aligned(contract->proposed_topOfKernelData)) {
        contract->satisfied_mask |= STAGE89_XNU_BOOTSTRAP_CONTRACT_SAT_TOPOFKERNELDATA_ALIGNED;
    } else {
        contract->failure_mask |= STAGE89_XNU_BOOTSTRAP_CONTRACT_FAIL_ALIGNMENT;
    }
    if (stage89_contract_is_page_aligned(contract->ttep_workspace_base) &&
        stage89_contract_is_page_aligned(contract->ttep_workspace_limit)) {
        contract->satisfied_mask |= STAGE89_XNU_BOOTSTRAP_CONTRACT_SAT_WORKSPACE_ALIGNED;
    } else {
        contract->failure_mask |= STAGE89_XNU_BOOTSTRAP_CONTRACT_FAIL_ALIGNMENT;
    }
    if (stage89_contract_is_page_aligned(contract->proposed_avail_start)) {
        contract->satisfied_mask |= STAGE89_XNU_BOOTSTRAP_CONTRACT_SAT_AVAIL_START_ALIGNED;
    } else {
        contract->failure_mask |= STAGE89_XNU_BOOTSTRAP_CONTRACT_FAIL_ALIGNMENT;
    }

    if (contract->ttep_workspace_base == contract->proposed_topOfKernelData &&
        contract->ttep_workspace_base >= contract->loaded_phys_end &&
        contract->loaded_phys_end > contract->loaded_phys_base) {
        contract->satisfied_mask |= STAGE89_XNU_BOOTSTRAP_CONTRACT_SAT_WORKSPACE_AFTER_IMAGE;
    } else {
        contract->failure_mask |= STAGE89_XNU_BOOTSTRAP_CONTRACT_FAIL_WORKSPACE_ORDER;
    }
    if (contract->proposed_avail_start == contract->ttep_workspace_limit &&
        contract->ttep_workspace_limit > contract->ttep_workspace_base) {
        contract->satisfied_mask |= STAGE89_XNU_BOOTSTRAP_CONTRACT_SAT_AVAIL_START_AFTER_WORKSPACE;
    } else {
        contract->failure_mask |= STAGE89_XNU_BOOTSTRAP_CONTRACT_FAIL_WORKSPACE_ORDER;
    }
    if (contract->proposed_avail_end > contract->proposed_avail_start &&
        contract->proposed_avail_end <= RAM_CONSOLE_BASE &&
        contract->proposed_physBase == RAM_PHYS_BASE) {
        contract->satisfied_mask |= STAGE89_XNU_BOOTSTRAP_CONTRACT_SAT_AVAIL_END_INSIDE_RAM;
    } else {
        contract->failure_mask |= STAGE89_XNU_BOOTSTRAP_CONTRACT_FAIL_AVAIL_RANGE;
    }

    proposed_mapping_base = contract->loaded_phys_base;
    proposed_mapping_limit = contract->proposed_avail_start;
    if (contract->loaded_phys_base >= RAM_PHYS_BASE && contract->stage_image_end <= contract->loaded_phys_base) {
        contract->satisfied_mask |= STAGE89_XNU_BOOTSTRAP_CONTRACT_SAT_NO_STAGE_IMAGE_OVERLAP;
    } else if (!stage89_contract_ranges_overlap(proposed_mapping_base, proposed_mapping_limit,
                                                contract->stage_image_base, contract->stage_image_end)) {
        contract->satisfied_mask |= STAGE89_XNU_BOOTSTRAP_CONTRACT_SAT_NO_STAGE_IMAGE_OVERLAP;
    } else {
        contract->failure_mask |= STAGE89_XNU_BOOTSTRAP_CONTRACT_FAIL_STAGE_IMAGE_OVERLAP;
    }
    if (!stage89_contract_ranges_overlap(proposed_mapping_base, proposed_mapping_limit,
                                         contract->ram_console_base, contract->ram_console_end)) {
        contract->satisfied_mask |= STAGE89_XNU_BOOTSTRAP_CONTRACT_SAT_NO_RAM_CONSOLE_OVERLAP;
    } else {
        contract->failure_mask |= STAGE89_XNU_BOOTSTRAP_CONTRACT_FAIL_RAM_CONSOLE_OVERLAP;
    }
    if (!stage89_contract_ranges_overlap(proposed_mapping_base, proposed_mapping_limit,
                                         contract->device_tree_base, contract->device_tree_end)) {
        contract->satisfied_mask |= STAGE89_XNU_BOOTSTRAP_CONTRACT_SAT_NO_DEVICE_TREE_OVERLAP;
    } else {
        contract->failure_mask |= STAGE89_XNU_BOOTSTRAP_CONTRACT_FAIL_DEVICE_TREE_OVERLAP;
    }
    if (!stage89_contract_ranges_overlap(proposed_mapping_base, proposed_mapping_limit,
                                         contract->staging_arena_base, contract->staging_arena_end)) {
        contract->satisfied_mask |= STAGE89_XNU_BOOTSTRAP_CONTRACT_SAT_NO_STAGING_ARENA_OVERLAP;
    } else {
        contract->failure_mask |= STAGE89_XNU_BOOTSTRAP_CONTRACT_FAIL_LOCAL_ARENA_OVERLAP;
    }
    if (!stage89_contract_ranges_overlap(proposed_mapping_base, proposed_mapping_limit,
                                         contract->safe_table_base, contract->safe_table_end)) {
        contract->satisfied_mask |= STAGE89_XNU_BOOTSTRAP_CONTRACT_SAT_NO_SAFE_TABLE_OVERLAP;
    } else {
        contract->failure_mask |= STAGE89_XNU_BOOTSTRAP_CONTRACT_FAIL_SAFE_TABLE_OVERLAP;
    }

    if (contract->page_coverage_mask == STAGE89_XNU_BOOTSTRAP_CONTRACT_PAGE_COVER_REQUIRED) {
        contract->satisfied_mask |= STAGE89_XNU_BOOTSTRAP_CONTRACT_SAT_PAGE_COVERAGE;
    } else {
        contract->failure_mask |= STAGE89_XNU_BOOTSTRAP_CONTRACT_FAIL_PAGE_COVERAGE;
    }
    if (contract->section_coverage_mask == STAGE89_XNU_BOOTSTRAP_CONTRACT_SECTION_COVER_REQUIRED) {
        contract->satisfied_mask |= STAGE89_XNU_BOOTSTRAP_CONTRACT_SAT_SECTION_COVERAGE;
    } else {
        contract->failure_mask |= STAGE89_XNU_BOOTSTRAP_CONTRACT_FAIL_SECTION_COVERAGE;
    }

    if ((preflight->safety_mask & STAGE89_LOADER_SAFETY_NO_PUBLIC_XNU_EXEC) != 0u &&
        ttbr_rt->xnu_entry_executed == 0u) {
        contract->no_public_xnu_exec = 1u;
        contract->satisfied_mask |= STAGE89_XNU_BOOTSTRAP_CONTRACT_SAT_NO_PUBLIC_XNU_EXEC;
    } else {
        contract->failure_mask |= STAGE89_XNU_BOOTSTRAP_CONTRACT_FAIL_SAFETY_BOUNDARY;
    }
    if ((preflight->safety_mask & STAGE89_LOADER_SAFETY_NO_PLATFORM_RUNTIME_EXEC) != 0u) {
        contract->no_platform_runtime_exec = 1u;
        contract->satisfied_mask |= STAGE89_XNU_BOOTSTRAP_CONTRACT_SAT_NO_PLATFORM_RUNTIME_EXEC;
    } else {
        contract->failure_mask |= STAGE89_XNU_BOOTSTRAP_CONTRACT_FAIL_SAFETY_BOUNDARY;
    }
    if ((preflight->safety_mask & STAGE89_LOADER_SAFETY_NO_EXECUTE) != 0u && ttbr_rt->macho_bytes_executed == 0u) {
        contract->no_macho_exec = 1u;
        contract->satisfied_mask |= STAGE89_XNU_BOOTSTRAP_CONTRACT_SAT_NO_MACHO_EXEC;
    } else {
        contract->failure_mask |= STAGE89_XNU_BOOTSTRAP_CONTRACT_FAIL_SAFETY_BOUNDARY;
    }
    if ((preflight->safety_mask & STAGE89_LOADER_SAFETY_NO_PHYS_WRITE) != 0u &&
        preflight->tte_dryrun.proposed_phys_load_written == 0u && ttbr_rt->proposed_phys_load_written == 0u) {
        contract->no_proposed_phys_write = 1u;
        contract->satisfied_mask |= STAGE89_XNU_BOOTSTRAP_CONTRACT_SAT_NO_PROPOSED_PHYS_WRITE;
    } else {
        contract->failure_mask |= STAGE89_XNU_BOOTSTRAP_CONTRACT_FAIL_SAFETY_BOUNDARY;
    }
    if ((preflight->safety_mask & STAGE89_LOADER_SAFETY_TTE_DRYRUN_ONLY) != 0u &&
        preflight->tte_dryrun.proposed_tte_workspace_written == 0u && ttbr_rt->proposed_tte_workspace_written == 0u) {
        contract->no_proposed_tte_write = 1u;
        contract->satisfied_mask |= STAGE89_XNU_BOOTSTRAP_CONTRACT_SAT_NO_PROPOSED_TTE_WRITE;
    } else {
        contract->failure_mask |= STAGE89_XNU_BOOTSTRAP_CONTRACT_FAIL_SAFETY_BOUNDARY;
    }
    if ((preflight->safety_mask & STAGE89_LOADER_SAFETY_NO_PERSIST_WRITE) != 0u &&
        ttbr_rt->persistent_write_attempted == 0u) {
        contract->no_persist_write = 1u;
        contract->satisfied_mask |= STAGE89_XNU_BOOTSTRAP_CONTRACT_SAT_NO_PERSIST_WRITE;
    } else {
        contract->failure_mask |= STAGE89_XNU_BOOTSTRAP_CONTRACT_FAIL_SAFETY_BOUNDARY;
    }
    if ((preflight->safety_mask & STAGE89_LOADER_SAFETY_NO_CACHE_CHANGE) != 0u &&
        preflight->tte_dryrun.caches_enabled == 0u && ttbr_rt->caches_changed == 0u) {
        contract->no_cache_policy_change = 1u;
        contract->satisfied_mask |= STAGE89_XNU_BOOTSTRAP_CONTRACT_SAT_NO_CACHE_POLICY_CHANGE;
    } else {
        contract->failure_mask |= STAGE89_XNU_BOOTSTRAP_CONTRACT_FAIL_CACHE_POLICY;
    }
    if (preflight->xnu_compile_graph.fail_closed == 1u && preflight->xnu_object_subset.fail_closed == 1u &&
        preflight->xnu_link.fail_closed == 1u) {
        contract->fail_closed = 1u;
        contract->satisfied_mask |= STAGE89_XNU_BOOTSTRAP_CONTRACT_SAT_FAIL_CLOSED;
    } else {
        contract->failure_mask |= STAGE89_XNU_BOOTSTRAP_CONTRACT_FAIL_SAFETY_BOUNDARY;
    }

finish:
    if (contract->satisfied_mask == contract->required_mask && contract->failure_mask == 0u) {
        contract->status = STAGE89_STATUS_OK;
        xnu_log_puts("Stage84 XNU bootstrap mapping contract ok\n");
    } else {
        contract->status = STAGE89_STATUS_FAIL(contract->failure_mask);
        xnu_log_puts("Stage84 XNU bootstrap mapping contract failed\n");
    }
    contract->checksum = stage89_xnu_bootstrap_contract_checksum(contract);

    stage89_xnu_bootstrap_contract_log(contract);
    return contract->status == STAGE89_STATUS_OK;
}

const struct stage89_xnu_bootstrap_contract *stage89_xnu_bootstrap_contract_result(void)
{
    return &g_stage89_xnu_bootstrap_contract;
}
