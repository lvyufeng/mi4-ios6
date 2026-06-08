#include "stage64.h"

/*
 * Stage64 XNU pmap safe live-table transition prerequisite dry-run contract.
 *
 * This contract deliberately stops at a read-only transition plan.  It imports
 * the Stage64 multi-window page-granular dry-run descriptors, mirrors only the
 * L1 coarse-table descriptor plan into a separate Stage-owned candidate L1
 * scratch buffer, proves representative translations through the existing
 * local L2-bank dry-run tables, and records the TTBR0/TTBCR/DACR/SCTLR values
 * that a later transition would require.  It does not install that candidate
 * L1 table, does not write TTBR/TTBCR/DACR/SCTLR for the candidate, does not
 * invalidate TLBs for the candidate, does not change cache policy, does not
 * write the proposed pmap workspace, and does not execute public VM/pmap code.
 */

static struct stage64_xnu_pmap_transition_dryrun_contract g_stage64_xnu_pmap_transition_dryrun_contract;

static uint32_t g_stage64_pmap_transition_l1[STAGE64_XNU_PMAP_TRANSITION_DRYRUN_L1_ENTRY_COUNT]
    __attribute__((aligned(STAGE64_XNU_PMAP_TRANSITION_DRYRUN_L1_ALIGNMENT)));

static uint32_t stage64_pmap_transition_dryrun_checksum(
    volatile const struct stage64_xnu_pmap_transition_dryrun_contract *contract)
{
    volatile const uint32_t *words = (volatile const uint32_t *)(uintptr_t)contract;
    uint32_t count = (uint32_t)(offsetof(struct stage64_xnu_pmap_transition_dryrun_contract, checksum) /
                                sizeof(uint32_t));
    uint32_t checksum = 0u;

    for (uint32_t i = 0; i < count; i++) {
        checksum ^= words[i];
    }

    return checksum;
}

static uint32_t stage64_pmap_transition_buffer_checksum(const uint32_t *buf, uint32_t count)
{
    uint32_t checksum = 0u;

    for (uint32_t i = 0; i < count; i++) {
        checksum ^= buf[i] + (i * 0x45d9f3bu);
    }

    return checksum;
}

static uint32_t stage64_pmap_transition_add_overflow_u32(uint32_t a, uint32_t b, uint32_t *out)
{
    *out = a + b;
    return *out < a;
}

static uint32_t stage64_pmap_transition_ranges_overlap(uint32_t a_base, uint32_t a_limit,
                                                       uint32_t b_base, uint32_t b_limit)
{
    if (a_limit <= a_base || b_limit <= b_base) {
        return 1u;
    }
    return (a_base < b_limit && b_base < a_limit) ? 1u : 0u;
}

static uint32_t stage64_pmap_transition_l1_index(uint32_t va)
{
    return va >> 20;
}

static uint32_t stage64_pmap_transition_l2_index(uint32_t va)
{
    return (va & STAGE64_XNU_PMAP_TRANSITION_DRYRUN_L2_INDEX_MASK) >>
           STAGE64_XNU_PMAP_TRANSITION_DRYRUN_L2_INDEX_SHIFT;
}

static uint32_t stage64_pmap_transition_make_l1_table_descriptor(uint32_t l2_base)
{
    return (l2_base & STAGE64_XNU_PMAP_TRANSITION_DRYRUN_L1_TABLE_MASK) |
           STAGE64_XNU_PMAP_TRANSITION_DRYRUN_L1_TABLE_TYPE;
}

static uint32_t stage64_pmap_transition_translate(const uint32_t *l1,
                                                  uint32_t local_l2_base,
                                                  uint32_t local_l2_limit,
                                                  uint32_t va,
                                                  uint32_t *pa_out)
{
    uint32_t l1_index = stage64_pmap_transition_l1_index(va);
    uint32_t l2_index = stage64_pmap_transition_l2_index(va);
    uint32_t l1_desc;
    uint32_t l2_base;
    const uint32_t *l2;
    uint32_t pte;

    if (pa_out) {
        *pa_out = 0u;
    }
    if (l1_index >= STAGE64_XNU_PMAP_TRANSITION_DRYRUN_L1_ENTRY_COUNT) {
        return 0u;
    }
    l1_desc = l1[l1_index];
    if ((l1_desc & STAGE64_XNU_PMAP_TRANSITION_DRYRUN_L1_TYPE_MASK) !=
        STAGE64_XNU_PMAP_TRANSITION_DRYRUN_L1_TABLE_TYPE) {
        return 0u;
    }
    l2_base = l1_desc & STAGE64_XNU_PMAP_TRANSITION_DRYRUN_L1_TABLE_MASK;
    if (l2_base < local_l2_base || l2_base >= local_l2_limit ||
        local_l2_limit - l2_base < STAGE64_XNU_PMAP_TRANSITION_DRYRUN_L2_COARSE_BYTES) {
        return 0u;
    }
    l2 = (const uint32_t *)(uintptr_t)l2_base;
    pte = l2[l2_index];
    if ((pte & STAGE64_XNU_PMAP_TRANSITION_DRYRUN_PTE_TYPE_MASK) !=
        STAGE64_XNU_PMAP_TRANSITION_DRYRUN_PTE_TYPE_SMALL) {
        return 0u;
    }
    if (pa_out) {
        *pa_out = (pte & STAGE64_XNU_PMAP_TRANSITION_DRYRUN_PTE_PAGE_MASK) |
                  (va & STAGE64_XNU_PMAP_TRANSITION_DRYRUN_PAGE_OFFSET_MASK);
    }
    return 1u;
}

static uint32_t stage64_pmap_transition_import_window(uint32_t first_l1,
                                                      uint32_t last_l1,
                                                      uint32_t l2_base,
                                                      uint32_t expected_first_word,
                                                      uint32_t *first_word,
                                                      uint32_t *type_mask_seen,
                                                      uint32_t *attr_mask_seen)
{
    uint32_t ok = 1u;

    if (first_word) {
        *first_word = 0u;
    }
    if (last_l1 < first_l1 || last_l1 >= STAGE64_XNU_PMAP_TRANSITION_DRYRUN_L1_ENTRY_COUNT) {
        return 0u;
    }
    if ((last_l1 - first_l1 + 1u) != STAGE64_XNU_PMAP_MULTIWINDOW_DRYRUN_L2_TABLES_PER_PAGE) {
        ok = 0u;
    }

    for (uint32_t i = 0; i <= last_l1 - first_l1; i++) {
        uint32_t word = stage64_pmap_transition_make_l1_table_descriptor(
            l2_base + (i * STAGE64_XNU_PMAP_TRANSITION_DRYRUN_L2_COARSE_BYTES));
        g_stage64_pmap_transition_l1[first_l1 + i] = word;
        if (i == 0u && first_word) {
            *first_word = word;
        }
        if (type_mask_seen) {
            *type_mask_seen |= word & STAGE64_XNU_PMAP_TRANSITION_DRYRUN_L1_TYPE_MASK;
        }
        if (attr_mask_seen) {
            *attr_mask_seen |= word & STAGE64_XNU_PMAP_TRANSITION_DRYRUN_L1_TABLE_ATTR_MASK;
        }
    }

    if (first_word && *first_word != expected_first_word) {
        ok = 0u;
    }
    return ok;
}

static void stage64_xnu_pmap_transition_dryrun_contract_log(
    const struct stage64_xnu_pmap_transition_dryrun_contract *contract)
{
    xnu_log_kv32("stage64_xnu_pmap_transition_dryrun_contract_status", contract->status);
    xnu_log_kv32("stage64_xnu_pmap_transition_dryrun_contract_required_mask", contract->required_mask);
    xnu_log_kv32("stage64_xnu_pmap_transition_dryrun_contract_satisfied_mask", contract->satisfied_mask);
    xnu_log_kv32("stage64_xnu_pmap_transition_dryrun_contract_failure_mask", contract->failure_mask);
    xnu_log_kv32("stage64_xnu_pmap_transition_dryrun_contract_checksum", contract->checksum);
    xnu_log_kv32("stage64_xnu_pmap_transition_dryrun_source_multiwindow_status", contract->source_multiwindow_status);
    xnu_log_kv32("stage64_xnu_pmap_transition_dryrun_source_multiwindow_checksum", contract->source_multiwindow_checksum);
    xnu_log_kv32("stage64_xnu_pmap_transition_dryrun_source_pmap_bootstrap_status", contract->source_pmap_bootstrap_status);
    xnu_log_kv32("stage64_xnu_pmap_transition_dryrun_source_ttbr_roundtrip_status", contract->source_ttbr_roundtrip_status);
    xnu_log_kv32("stage64_xnu_pmap_transition_dryrun_l1_entry_count", contract->l1_entry_count);
    xnu_log_kv32("stage64_xnu_pmap_transition_dryrun_l1_bytes", contract->l1_bytes);
    xnu_log_kv32("stage64_xnu_pmap_transition_dryrun_l2_bank_base", contract->l2_bank_base);
    xnu_log_kv32("stage64_xnu_pmap_transition_dryrun_l2_bank_limit", contract->l2_bank_limit);
    xnu_log_kv32("stage64_xnu_pmap_transition_dryrun_candidate_l1_base", contract->candidate_l1_base);
    xnu_log_kv32("stage64_xnu_pmap_transition_dryrun_candidate_l1_limit", contract->candidate_l1_limit);
    xnu_log_kv32("stage64_xnu_pmap_transition_dryrun_candidate_l1_zero_checksum", contract->candidate_l1_zero_checksum);
    xnu_log_kv32("stage64_xnu_pmap_transition_dryrun_candidate_l1_populated_checksum", contract->candidate_l1_populated_checksum);
    xnu_log_kv32("stage64_xnu_pmap_transition_dryrun_candidate_l1_write_count", contract->candidate_l1_write_count);
    xnu_log_kv32("stage64_xnu_pmap_transition_dryrun_candidate_kernel_l1_first_index", contract->candidate_kernel_l1_first_index);
    xnu_log_kv32("stage64_xnu_pmap_transition_dryrun_candidate_kernel_l1_last_index", contract->candidate_kernel_l1_last_index);
    xnu_log_kv32("stage64_xnu_pmap_transition_dryrun_candidate_ram_console_l1_first_index", contract->candidate_ram_console_l1_first_index);
    xnu_log_kv32("stage64_xnu_pmap_transition_dryrun_candidate_ram_console_l1_last_index", contract->candidate_ram_console_l1_last_index);
    xnu_log_kv32("stage64_xnu_pmap_transition_dryrun_candidate_device_l1_first_index", contract->candidate_device_l1_first_index);
    xnu_log_kv32("stage64_xnu_pmap_transition_dryrun_candidate_device_l1_last_index", contract->candidate_device_l1_last_index);
    xnu_log_kv32("stage64_xnu_pmap_transition_dryrun_candidate_l1_descriptor_kernel_first_word", contract->candidate_l1_descriptor_kernel_first_word);
    xnu_log_kv32("stage64_xnu_pmap_transition_dryrun_candidate_l1_descriptor_ram_console_first_word", contract->candidate_l1_descriptor_ram_console_first_word);
    xnu_log_kv32("stage64_xnu_pmap_transition_dryrun_candidate_l1_descriptor_device_first_word", contract->candidate_l1_descriptor_device_first_word);
    xnu_log_kv32("stage64_xnu_pmap_transition_dryrun_candidate_l1_descriptor_type_mask_seen", contract->candidate_l1_descriptor_type_mask_seen);
    xnu_log_kv32("stage64_xnu_pmap_transition_dryrun_candidate_l1_descriptor_attr_mask_seen", contract->candidate_l1_descriptor_attr_mask_seen);
    xnu_log_kv32("stage64_xnu_pmap_transition_dryrun_proposed_ttbr0", contract->proposed_ttbr0);
    xnu_log_kv32("stage64_xnu_pmap_transition_dryrun_proposed_ttbcr", contract->proposed_ttbcr);
    xnu_log_kv32("stage64_xnu_pmap_transition_dryrun_proposed_dacr", contract->proposed_dacr);
    xnu_log_kv32("stage64_xnu_pmap_transition_dryrun_proposed_sctlr", contract->proposed_sctlr);
    xnu_log_kv32("stage64_xnu_pmap_transition_dryrun_proposed_ttbr0_alignment", contract->proposed_ttbr0_alignment);
    xnu_log_kv32("stage64_xnu_pmap_transition_dryrun_proposed_sctlr_cache_bits", contract->proposed_sctlr_cache_bits);
    xnu_log_kv32("stage64_xnu_pmap_transition_dryrun_restored_ttbr0", contract->restored_ttbr0);
    xnu_log_kv32("stage64_xnu_pmap_transition_dryrun_restored_ttbcr", contract->restored_ttbcr);
    xnu_log_kv32("stage64_xnu_pmap_transition_dryrun_restored_dacr", contract->restored_dacr);
    xnu_log_kv32("stage64_xnu_pmap_transition_dryrun_restored_sctlr", contract->restored_sctlr);
    xnu_log_kv32("stage64_xnu_pmap_transition_dryrun_restored_cache_bits", contract->restored_cache_bits);
    xnu_log_kv32("stage64_xnu_pmap_transition_dryrun_recovery_l1_base", contract->recovery_l1_base);
    xnu_log_kv32("stage64_xnu_pmap_transition_dryrun_recovery_l1_limit", contract->recovery_l1_limit);
    xnu_log_kv32("stage64_xnu_pmap_transition_dryrun_recovery_l1_distinct", contract->recovery_l1_distinct);
    xnu_log_kv32("stage64_xnu_pmap_transition_dryrun_workspace_base", contract->workspace_base);
    xnu_log_kv32("stage64_xnu_pmap_transition_dryrun_workspace_limit", contract->workspace_limit);
    xnu_log_kv32("stage64_xnu_pmap_transition_dryrun_workspace_l1_table_phys", contract->workspace_l1_table_phys);
    xnu_log_kv32("stage64_xnu_pmap_transition_dryrun_candidate_workspace_distinct", contract->candidate_workspace_distinct);
    xnu_log_kv32("stage64_xnu_pmap_transition_dryrun_translation_kernel_va", contract->translation_kernel_va);
    xnu_log_kv32("stage64_xnu_pmap_transition_dryrun_translation_kernel_pa", contract->translation_kernel_pa);
    xnu_log_kv32("stage64_xnu_pmap_transition_dryrun_translation_workspace_va", contract->translation_workspace_va);
    xnu_log_kv32("stage64_xnu_pmap_transition_dryrun_translation_workspace_pa", contract->translation_workspace_pa);
    xnu_log_kv32("stage64_xnu_pmap_transition_dryrun_translation_ram_console_va", contract->translation_ram_console_va);
    xnu_log_kv32("stage64_xnu_pmap_transition_dryrun_translation_ram_console_pa", contract->translation_ram_console_pa);
    xnu_log_kv32("stage64_xnu_pmap_transition_dryrun_translation_device_va", contract->translation_device_va);
    xnu_log_kv32("stage64_xnu_pmap_transition_dryrun_translation_device_pa", contract->translation_device_pa);
    xnu_log_kv32("stage64_xnu_pmap_transition_dryrun_translation_case_count", contract->translation_case_count);
    xnu_log_kv32("stage64_xnu_pmap_transition_dryrun_public_pmap_compile_count", contract->public_pmap_compile_count);
    xnu_log_kv32("stage64_xnu_pmap_transition_dryrun_public_pmap_link_count", contract->public_pmap_link_count);
    xnu_log_kv32("stage64_xnu_pmap_transition_dryrun_public_pmap_execute_count", contract->public_pmap_execute_count);
    xnu_log_kv32("stage64_xnu_pmap_transition_dryrun_public_arm_vm_init_executed", contract->public_arm_vm_init_executed);
    xnu_log_kv32("stage64_xnu_pmap_transition_dryrun_public_pmap_runtime_executed", contract->public_pmap_runtime_executed);
    xnu_log_kv32("stage64_xnu_pmap_transition_dryrun_proposed_workspace_written", contract->proposed_workspace_written);
    xnu_log_kv32("stage64_xnu_pmap_transition_dryrun_live_pmap_tables_installed", contract->live_pmap_tables_installed);
    xnu_log_kv32("stage64_xnu_pmap_transition_dryrun_ttbr_written", contract->ttbr_written);
    xnu_log_kv32("stage64_xnu_pmap_transition_dryrun_ttbcr_written", contract->ttbcr_written);
    xnu_log_kv32("stage64_xnu_pmap_transition_dryrun_dacr_written", contract->dacr_written);
    xnu_log_kv32("stage64_xnu_pmap_transition_dryrun_sctlr_written", contract->sctlr_written);
    xnu_log_kv32("stage64_xnu_pmap_transition_dryrun_tlbs_invalidated", contract->tlbs_invalidated);
    xnu_log_kv32("stage64_xnu_pmap_transition_dryrun_caches_changed", contract->caches_changed);
    xnu_log_kv32("stage64_xnu_pmap_transition_dryrun_persistent_write_attempted", contract->persistent_write_attempted);
    xnu_log_kv32("stage64_xnu_pmap_transition_dryrun_xnu_start_executed", contract->xnu_start_executed);
    xnu_log_kv32("stage64_xnu_pmap_transition_dryrun_generated_macho_executed", contract->generated_macho_executed);
    xnu_log_kv32("stage64_xnu_pmap_transition_dryrun_local_only", contract->local_only);
    xnu_log_kv32("stage64_xnu_pmap_transition_dryrun_candidate_not_installed", contract->candidate_not_installed);
    xnu_log_kv32("stage64_xnu_pmap_transition_dryrun_control_register_plan_readonly", contract->control_register_plan_readonly);
    xnu_log_kv32("stage64_xnu_pmap_transition_dryrun_tlb_plan_readonly", contract->tlb_plan_readonly);
    xnu_log_kv32("stage64_xnu_pmap_transition_dryrun_fail_closed", contract->fail_closed);
}

int stage64_xnu_pmap_transition_dryrun_contract_selftest(const struct stage64_loader_preflight *preflight)
{
    struct stage64_xnu_pmap_transition_dryrun_contract *contract =
        &g_stage64_xnu_pmap_transition_dryrun_contract;
    const struct stage64_xnu_pmap_multiwindow_dryrun_contract *multi =
        stage64_xnu_pmap_multiwindow_dryrun_contract_result();
    const struct stage64_pmap_bootstrap_snapshot *snapshot = mmu_stage64_pmap_bootstrap_snapshot_result();
    const struct stage64_ttbr0_roundtrip *ttbr_rt = mmu_stage64_ttbr0_roundtrip_result();
    uint32_t candidate_l1_limit = 0u;
    uint32_t translated = 0u;
    uint32_t source_rollups_ok;
    uint32_t descriptor_ok = 1u;

    memset(contract, 0, sizeof(*contract));
    memset(g_stage64_pmap_transition_l1, 0, sizeof(g_stage64_pmap_transition_l1));

    contract->version = STAGE64_XNU_PMAP_TRANSITION_DRYRUN_CONTRACT_VERSION;
    contract->size = sizeof(*contract);
    contract->status = STAGE64_STATUS_BASE;
    contract->required_mask = STAGE64_XNU_PMAP_TRANSITION_DRYRUN_REQUIRED_MASK;
    contract->l1_entry_count = STAGE64_XNU_PMAP_TRANSITION_DRYRUN_L1_ENTRY_COUNT;
    contract->l1_bytes = STAGE64_XNU_PMAP_TRANSITION_DRYRUN_L1_BYTES;
    contract->l1_alignment = STAGE64_XNU_PMAP_TRANSITION_DRYRUN_L1_ALIGNMENT;
    contract->l1_table_descriptor_type = STAGE64_XNU_PMAP_TRANSITION_DRYRUN_L1_TABLE_TYPE;
    contract->l1_table_type_mask = STAGE64_XNU_PMAP_TRANSITION_DRYRUN_L1_TYPE_MASK;
    contract->l1_table_base_mask = STAGE64_XNU_PMAP_TRANSITION_DRYRUN_L1_TABLE_MASK;
    contract->l1_table_attr_mask = STAGE64_XNU_PMAP_TRANSITION_DRYRUN_L1_TABLE_ATTR_MASK;
    contract->l2_coarse_table_bytes = STAGE64_XNU_PMAP_TRANSITION_DRYRUN_L2_COARSE_BYTES;
    contract->l2_page_bytes = STAGE64_XNU_PMAP_TRANSITION_DRYRUN_L2_PAGE_BYTES;
    contract->l2_pte_count = STAGE64_XNU_PMAP_TRANSITION_DRYRUN_L2_PTE_COUNT;
    contract->pte_type_mask = STAGE64_XNU_PMAP_TRANSITION_DRYRUN_PTE_TYPE_MASK;
    contract->pte_type_small = STAGE64_XNU_PMAP_TRANSITION_DRYRUN_PTE_TYPE_SMALL;
    contract->pte_page_mask = STAGE64_XNU_PMAP_TRANSITION_DRYRUN_PTE_PAGE_MASK;
    contract->page_offset_mask = STAGE64_XNU_PMAP_TRANSITION_DRYRUN_PAGE_OFFSET_MASK;
    contract->candidate_l1_base = (uint32_t)(uintptr_t)g_stage64_pmap_transition_l1;
    contract->candidate_l1_bytes = sizeof(g_stage64_pmap_transition_l1);
    contract->candidate_l1_alignment = contract->candidate_l1_base &
        (STAGE64_XNU_PMAP_TRANSITION_DRYRUN_L1_ALIGNMENT - 1u);
    contract->candidate_l1_zero_checksum = stage64_pmap_transition_buffer_checksum(
        g_stage64_pmap_transition_l1, STAGE64_XNU_PMAP_TRANSITION_DRYRUN_L1_ENTRY_COUNT);

    xnu_log_puts("Stage64 XNU pmap transition prerequisite dry-run contract begin\n");

    if (!preflight || !multi || !snapshot || !ttbr_rt) {
        contract->failure_mask |= STAGE64_XNU_PMAP_TRANSITION_DRYRUN_FAIL_SOURCE |
                                  STAGE64_XNU_PMAP_TRANSITION_DRYRUN_FAIL_SAFETY_BOUNDARY;
        goto finish;
    }

    contract->source_multiwindow_status = multi->status;
    contract->source_multiwindow_required_mask = multi->required_mask;
    contract->source_multiwindow_satisfied_mask = multi->satisfied_mask;
    contract->source_multiwindow_failure_mask = multi->failure_mask;
    contract->source_multiwindow_checksum = multi->checksum;
    contract->source_pmap_bootstrap_status = preflight->xnu_pmap_bootstrap_contract_status;
    contract->source_ttbr_roundtrip_status = preflight->ttbr_roundtrip_status;
    contract->source_ttbr_restored_status = preflight->ttbr_restored_status;
    contract->source_cache_preserved_status = preflight->cache_preserved_status;
    contract->source_compile_graph_status = preflight->xnu_compile_graph_status;
    contract->source_object_subset_status = preflight->xnu_object_subset_status;
    contract->source_link_status = preflight->xnu_link_status;
    contract->l2_bank_base = multi->local_l2_bank_base;
    contract->l2_bank_limit = multi->local_l2_bank_limit;
    contract->l2_bank_bytes = multi->local_l2_bank_bytes;
    contract->workspace_base = multi->workspace_base;
    contract->workspace_limit = multi->workspace_limit;
    contract->workspace_l1_table_phys = multi->workspace_l1_table_phys;
    contract->workspace_l1_table_virt = multi->workspace_l1_table_virt;
    contract->recovery_l1_base = ttbr_rt->stage_l1_base;
    contract->recovery_l1_limit = ttbr_rt->stage_l1_limit;
    contract->restored_ttbr0 = ttbr_rt->restored_ttbr0;
    contract->restored_ttbcr = ttbr_rt->restored_ttbcr;
    contract->restored_dacr = ttbr_rt->restored_dacr;
    contract->restored_sctlr = ttbr_rt->restored_sctlr;
    contract->restored_cache_bits = ttbr_rt->cache_bits_after;

    source_rollups_ok = (contract->source_multiwindow_status == STAGE64_STATUS_OK &&
                         contract->source_multiwindow_satisfied_mask == contract->source_multiwindow_required_mask &&
                         contract->source_multiwindow_failure_mask == 0u &&
                         contract->source_pmap_bootstrap_status == STAGE64_STATUS_OK &&
                         contract->source_ttbr_roundtrip_status == STAGE64_STATUS_OK &&
                         contract->source_ttbr_restored_status == STAGE64_STATUS_OK &&
                         contract->source_cache_preserved_status == STAGE64_STATUS_OK &&
                         contract->source_compile_graph_status == STAGE64_STATUS_OK &&
                         contract->source_object_subset_status == STAGE64_STATUS_OK &&
                         contract->source_link_status == STAGE64_STATUS_OK &&
                         preflight->xnu_pmap_multiwindow_dryrun_contract_status_rollup == STAGE64_STATUS_OK &&
                         preflight->xnu_pmap_bootstrap_contract_failure_mask == 0u &&
                         preflight->xnu_pmap_multiwindow_dryrun_contract_failure_mask == 0u &&
                         preflight->xnu_compile_graph_failure_mask == 0u &&
                         preflight->xnu_object_subset_failure_mask == 0u &&
                         preflight->xnu_link_failure_mask == 0u) ? 1u : 0u;

    if (contract->source_multiwindow_status == STAGE64_STATUS_OK &&
        contract->source_multiwindow_satisfied_mask == contract->source_multiwindow_required_mask &&
        contract->source_multiwindow_failure_mask == 0u) {
        contract->satisfied_mask |= STAGE64_XNU_PMAP_TRANSITION_DRYRUN_SAT_SOURCE_MULTIWINDOW;
    } else {
        contract->failure_mask |= STAGE64_XNU_PMAP_TRANSITION_DRYRUN_FAIL_SOURCE;
    }
    if (source_rollups_ok) {
        contract->satisfied_mask |= STAGE64_XNU_PMAP_TRANSITION_DRYRUN_SAT_SOURCE_ROLLUPS;
    } else {
        contract->failure_mask |= STAGE64_XNU_PMAP_TRANSITION_DRYRUN_FAIL_SOURCE;
    }

    if (contract->l1_entry_count != 4096u || contract->l1_bytes != 0x00004000u ||
        contract->l1_alignment != 0x00004000u || contract->l1_table_descriptor_type != 0x00000001u ||
        contract->l1_table_type_mask != 0x00000003u || contract->l1_table_base_mask != 0xfffffc00u ||
        contract->l2_bank_bytes != (STAGE64_XNU_PMAP_MULTIWINDOW_DRYRUN_WINDOW_COUNT *
                                    STAGE64_XNU_PMAP_MULTIWINDOW_DRYRUN_L2_PAGE_BYTES) ||
        contract->l2_coarse_table_bytes != 0x00000400u || contract->l2_page_bytes != 0x00001000u ||
        contract->l2_pte_count != 1024u || contract->pte_type_small != 0x00000002u ||
        contract->pte_page_mask != 0xfffff000u || contract->page_offset_mask != 0x00000fffu) {
        contract->failure_mask |= STAGE64_XNU_PMAP_TRANSITION_DRYRUN_FAIL_DESCRIPTOR;
    }

    if (!stage64_pmap_transition_add_overflow_u32(contract->candidate_l1_base,
                                                  contract->candidate_l1_bytes,
                                                  &candidate_l1_limit)) {
        contract->candidate_l1_limit = candidate_l1_limit;
    }
    contract->candidate_workspace_distinct =
        !stage64_pmap_transition_ranges_overlap(contract->candidate_l1_base, contract->candidate_l1_limit,
                                                contract->workspace_base, contract->workspace_limit) ? 1u : 0u;
    contract->recovery_l1_distinct =
        !stage64_pmap_transition_ranges_overlap(contract->candidate_l1_base, contract->candidate_l1_limit,
                                                contract->recovery_l1_base, contract->recovery_l1_limit) ? 1u : 0u;

    if (contract->candidate_l1_base != 0u && contract->candidate_l1_limit > contract->candidate_l1_base &&
        contract->candidate_l1_bytes == STAGE64_XNU_PMAP_TRANSITION_DRYRUN_L1_BYTES &&
        contract->candidate_l1_alignment == 0u && contract->candidate_l1_limit <= RAM_PHYS_BASE &&
        contract->candidate_workspace_distinct == 1u && contract->recovery_l1_distinct == 1u &&
        !stage64_pmap_transition_ranges_overlap(contract->candidate_l1_base, contract->candidate_l1_limit,
                                                multi->local_l1_base, multi->local_l1_limit) &&
        !stage64_pmap_transition_ranges_overlap(contract->candidate_l1_base, contract->candidate_l1_limit,
                                                contract->l2_bank_base, contract->l2_bank_limit)) {
        contract->satisfied_mask |= STAGE64_XNU_PMAP_TRANSITION_DRYRUN_SAT_CANDIDATE_L1_BUFFER;
    } else {
        contract->failure_mask |= STAGE64_XNU_PMAP_TRANSITION_DRYRUN_FAIL_LOCAL_BUFFER;
    }
    if (contract->candidate_l1_zero_checksum == stage64_pmap_transition_buffer_checksum(
            g_stage64_pmap_transition_l1, STAGE64_XNU_PMAP_TRANSITION_DRYRUN_L1_ENTRY_COUNT)) {
        contract->satisfied_mask |= STAGE64_XNU_PMAP_TRANSITION_DRYRUN_SAT_CANDIDATE_ZERO;
    } else {
        contract->failure_mask |= STAGE64_XNU_PMAP_TRANSITION_DRYRUN_FAIL_LOCAL_BUFFER;
    }

    contract->candidate_kernel_l1_first_index = multi->kernel_window_l1_first_index;
    contract->candidate_kernel_l1_last_index = multi->kernel_window_l1_last_index;
    contract->candidate_ram_console_l1_first_index = multi->ram_console_window_l1_first_index;
    contract->candidate_ram_console_l1_last_index = multi->ram_console_window_l1_last_index;
    contract->candidate_device_l1_first_index = multi->device_window_l1_first_index;
    contract->candidate_device_l1_last_index = multi->device_window_l1_last_index;

    descriptor_ok &= stage64_pmap_transition_import_window(
        contract->candidate_kernel_l1_first_index,
        contract->candidate_kernel_l1_last_index,
        contract->l2_bank_base,
        multi->l1_descriptor_kernel_first_word,
        &contract->candidate_l1_descriptor_kernel_first_word,
        &contract->candidate_l1_descriptor_type_mask_seen,
        &contract->candidate_l1_descriptor_attr_mask_seen);
    descriptor_ok &= stage64_pmap_transition_import_window(
        contract->candidate_ram_console_l1_first_index,
        contract->candidate_ram_console_l1_last_index,
        contract->l2_bank_base + STAGE64_XNU_PMAP_TRANSITION_DRYRUN_L2_PAGE_BYTES,
        multi->l1_descriptor_ram_console_first_word,
        &contract->candidate_l1_descriptor_ram_console_first_word,
        &contract->candidate_l1_descriptor_type_mask_seen,
        &contract->candidate_l1_descriptor_attr_mask_seen);
    descriptor_ok &= stage64_pmap_transition_import_window(
        contract->candidate_device_l1_first_index,
        contract->candidate_device_l1_last_index,
        contract->l2_bank_base + (2u * STAGE64_XNU_PMAP_TRANSITION_DRYRUN_L2_PAGE_BYTES),
        multi->l1_descriptor_device_first_word,
        &contract->candidate_l1_descriptor_device_first_word,
        &contract->candidate_l1_descriptor_type_mask_seen,
        &contract->candidate_l1_descriptor_attr_mask_seen);
    contract->candidate_l1_write_count = STAGE64_XNU_PMAP_MULTIWINDOW_DRYRUN_WINDOW_COUNT *
        STAGE64_XNU_PMAP_MULTIWINDOW_DRYRUN_L2_TABLES_PER_PAGE;

    if (descriptor_ok &&
        contract->candidate_l1_descriptor_type_mask_seen == STAGE64_XNU_PMAP_TRANSITION_DRYRUN_L1_TABLE_TYPE &&
        contract->candidate_l1_descriptor_attr_mask_seen == STAGE64_XNU_PMAP_TRANSITION_DRYRUN_L1_TABLE_TYPE &&
        contract->l2_bank_base == multi->local_l2_bank_base &&
        contract->l2_bank_limit == multi->local_l2_bank_limit) {
        contract->satisfied_mask |= STAGE64_XNU_PMAP_TRANSITION_DRYRUN_SAT_DESCRIPTOR_IMPORT;
    } else {
        contract->failure_mask |= STAGE64_XNU_PMAP_TRANSITION_DRYRUN_FAIL_DESCRIPTOR;
    }

    contract->translation_kernel_va = multi->translation_kernel_va;
    contract->translation_kernel_expected_pa = multi->translation_kernel_expected_pa;
    if (stage64_pmap_transition_translate(g_stage64_pmap_transition_l1,
                                          contract->l2_bank_base,
                                          contract->l2_bank_limit,
                                          contract->translation_kernel_va,
                                          &translated) &&
        translated == contract->translation_kernel_expected_pa) {
        contract->translation_kernel_pa = translated;
        contract->translation_case_count++;
        contract->satisfied_mask |= STAGE64_XNU_PMAP_TRANSITION_DRYRUN_SAT_TRANSLATION_KERNEL;
    } else {
        contract->failure_mask |= STAGE64_XNU_PMAP_TRANSITION_DRYRUN_FAIL_TRANSLATION;
    }

    contract->translation_workspace_va = multi->translation_workspace_va;
    contract->translation_workspace_expected_pa = multi->translation_workspace_expected_pa;
    if (stage64_pmap_transition_translate(g_stage64_pmap_transition_l1,
                                          contract->l2_bank_base,
                                          contract->l2_bank_limit,
                                          contract->translation_workspace_va,
                                          &translated) &&
        translated == contract->translation_workspace_expected_pa) {
        contract->translation_workspace_pa = translated;
        contract->translation_case_count++;
        contract->satisfied_mask |= STAGE64_XNU_PMAP_TRANSITION_DRYRUN_SAT_TRANSLATION_WORKSPACE;
    } else {
        contract->failure_mask |= STAGE64_XNU_PMAP_TRANSITION_DRYRUN_FAIL_TRANSLATION;
    }

    contract->translation_ram_console_va = multi->translation_ram_console_va;
    contract->translation_ram_console_expected_pa = multi->translation_ram_console_expected_pa;
    if (stage64_pmap_transition_translate(g_stage64_pmap_transition_l1,
                                          contract->l2_bank_base,
                                          contract->l2_bank_limit,
                                          contract->translation_ram_console_va,
                                          &translated) &&
        translated == contract->translation_ram_console_expected_pa) {
        contract->translation_ram_console_pa = translated;
        contract->translation_case_count++;
        contract->satisfied_mask |= STAGE64_XNU_PMAP_TRANSITION_DRYRUN_SAT_TRANSLATION_RAM_CONSOLE;
    } else {
        contract->failure_mask |= STAGE64_XNU_PMAP_TRANSITION_DRYRUN_FAIL_TRANSLATION;
    }

    contract->translation_device_va = multi->translation_device_va;
    contract->translation_device_expected_pa = multi->translation_device_expected_pa;
    if (stage64_pmap_transition_translate(g_stage64_pmap_transition_l1,
                                          contract->l2_bank_base,
                                          contract->l2_bank_limit,
                                          contract->translation_device_va,
                                          &translated) &&
        translated == contract->translation_device_expected_pa) {
        contract->translation_device_pa = translated;
        contract->translation_case_count++;
        contract->satisfied_mask |= STAGE64_XNU_PMAP_TRANSITION_DRYRUN_SAT_TRANSLATION_DEVICE;
    } else {
        contract->failure_mask |= STAGE64_XNU_PMAP_TRANSITION_DRYRUN_FAIL_TRANSLATION;
    }

    contract->proposed_ttbr0 = contract->candidate_l1_base & STAGE64_XNU_PMAP_TRANSITION_DRYRUN_TTBR0_BASE_MASK;
    contract->proposed_ttbcr = ttbr_rt->original_ttbcr;
    contract->proposed_dacr = ttbr_rt->original_dacr;
    contract->proposed_sctlr = ttbr_rt->original_sctlr;
    contract->proposed_ttbr0_alignment = contract->candidate_l1_base &
        (STAGE64_XNU_PMAP_TRANSITION_DRYRUN_L1_ALIGNMENT - 1u);
    contract->proposed_sctlr_cache_bits = contract->proposed_sctlr &
        STAGE64_XNU_PMAP_TRANSITION_DRYRUN_CONTROL_CACHE_MASK;

    if (contract->proposed_ttbr0 == contract->candidate_l1_base &&
        contract->proposed_ttbr0_alignment == 0u &&
        contract->proposed_ttbr0 != (ttbr_rt->original_ttbr0 & STAGE64_XNU_PMAP_TRANSITION_DRYRUN_TTBR0_BASE_MASK)) {
        contract->satisfied_mask |= STAGE64_XNU_PMAP_TRANSITION_DRYRUN_SAT_PROPOSED_TTBR0_PLAN;
    } else {
        contract->failure_mask |= STAGE64_XNU_PMAP_TRANSITION_DRYRUN_FAIL_CONTROL_PLAN;
    }
    if (contract->proposed_ttbcr == ttbr_rt->restored_ttbcr && contract->proposed_ttbcr == ttbr_rt->original_ttbcr) {
        contract->satisfied_mask |= STAGE64_XNU_PMAP_TRANSITION_DRYRUN_SAT_PROPOSED_TTBCR_PLAN;
    } else {
        contract->failure_mask |= STAGE64_XNU_PMAP_TRANSITION_DRYRUN_FAIL_CONTROL_PLAN;
    }
    if (contract->proposed_dacr == ttbr_rt->restored_dacr && contract->proposed_dacr == ttbr_rt->original_dacr) {
        contract->satisfied_mask |= STAGE64_XNU_PMAP_TRANSITION_DRYRUN_SAT_PROPOSED_DACR_PLAN;
    } else {
        contract->failure_mask |= STAGE64_XNU_PMAP_TRANSITION_DRYRUN_FAIL_CONTROL_PLAN;
    }
    if (contract->proposed_sctlr == ttbr_rt->restored_sctlr &&
        contract->proposed_sctlr_cache_bits == ttbr_rt->cache_bits_before &&
        contract->proposed_sctlr_cache_bits == ttbr_rt->cache_bits_after && ttbr_rt->caches_changed == 0u) {
        contract->satisfied_mask |= STAGE64_XNU_PMAP_TRANSITION_DRYRUN_SAT_PROPOSED_SCTLR_PLAN;
    } else {
        contract->failure_mask |= STAGE64_XNU_PMAP_TRANSITION_DRYRUN_FAIL_CONTROL_PLAN;
    }

    if (contract->recovery_l1_distinct == 1u && contract->candidate_workspace_distinct == 1u &&
        snapshot->status == STAGE64_STATUS_OK && snapshot->no_live_pmap_tables_installed == 1u &&
        (ttbr_rt->restored_ttbr0 & STAGE64_XNU_PMAP_TRANSITION_DRYRUN_TTBR0_BASE_MASK) ==
            (ttbr_rt->original_ttbr0 & STAGE64_XNU_PMAP_TRANSITION_DRYRUN_TTBR0_BASE_MASK) &&
        ttbr_rt->restored_ttbcr == ttbr_rt->original_ttbcr &&
        ttbr_rt->restored_dacr == ttbr_rt->original_dacr &&
        ttbr_rt->cache_bits_after == ttbr_rt->cache_bits_before) {
        contract->satisfied_mask |= STAGE64_XNU_PMAP_TRANSITION_DRYRUN_SAT_RECOVERY_CONTINUITY;
    } else {
        contract->failure_mask |= STAGE64_XNU_PMAP_TRANSITION_DRYRUN_FAIL_RECOVERY;
    }

    contract->public_pmap_compile_count = multi->public_pmap_compile_count;
    contract->public_pmap_link_count = multi->public_pmap_link_count;
    contract->public_pmap_execute_count = multi->public_pmap_execute_count;
    contract->public_arm_vm_init_executed = multi->public_arm_vm_init_executed;
    contract->public_pmap_runtime_executed = multi->public_pmap_runtime_executed;
    contract->proposed_workspace_written = multi->proposed_workspace_written;
    contract->live_pmap_tables_installed = multi->live_pmap_tables_installed;
    contract->ttbr_written = multi->ttbr_written;
    contract->ttbcr_written = multi->ttbcr_written;
    contract->dacr_written = multi->dacr_written;
    contract->sctlr_written = multi->sctlr_written;
    contract->tlbs_invalidated = multi->tlbs_invalidated;
    contract->caches_changed = multi->caches_changed | ttbr_rt->caches_changed;
    contract->persistent_write_attempted = multi->persistent_write_attempted | ttbr_rt->persistent_write_attempted;
    contract->xnu_start_executed = multi->xnu_start_executed | ttbr_rt->xnu_entry_executed;
    contract->generated_macho_executed = multi->generated_macho_executed | ttbr_rt->macho_bytes_executed;
    contract->local_only = 1u;
    contract->candidate_not_installed = 1u;
    contract->control_register_plan_readonly = 1u;
    contract->tlb_plan_readonly = 1u;

    contract->candidate_l1_populated_checksum = stage64_pmap_transition_buffer_checksum(
        g_stage64_pmap_transition_l1, STAGE64_XNU_PMAP_TRANSITION_DRYRUN_L1_ENTRY_COUNT);
    if (contract->candidate_l1_populated_checksum != contract->candidate_l1_zero_checksum &&
        contract->candidate_l1_write_count == (STAGE64_XNU_PMAP_MULTIWINDOW_DRYRUN_WINDOW_COUNT *
                                               STAGE64_XNU_PMAP_MULTIWINDOW_DRYRUN_L2_TABLES_PER_PAGE) &&
        contract->proposed_workspace_written == 0u && contract->candidate_not_installed == 1u) {
        contract->satisfied_mask |= STAGE64_XNU_PMAP_TRANSITION_DRYRUN_SAT_LOCAL_ONLY_CHECKSUM;
    } else {
        contract->failure_mask |= STAGE64_XNU_PMAP_TRANSITION_DRYRUN_FAIL_CHECKSUM;
    }

    if (contract->public_pmap_compile_count == 0u && contract->public_pmap_link_count == 0u &&
        contract->public_pmap_execute_count == 0u && contract->public_arm_vm_init_executed == 0u &&
        contract->public_pmap_runtime_executed == 0u) {
        contract->satisfied_mask |= STAGE64_XNU_PMAP_TRANSITION_DRYRUN_SAT_NO_PUBLIC_PMAP_EXEC;
    } else {
        contract->failure_mask |= STAGE64_XNU_PMAP_TRANSITION_DRYRUN_FAIL_SAFETY_BOUNDARY;
    }
    if (contract->proposed_workspace_written == 0u) {
        contract->satisfied_mask |= STAGE64_XNU_PMAP_TRANSITION_DRYRUN_SAT_NO_PROPOSED_WORKSPACE_WRITE;
    } else {
        contract->failure_mask |= STAGE64_XNU_PMAP_TRANSITION_DRYRUN_FAIL_SAFETY_BOUNDARY;
    }
    if (contract->live_pmap_tables_installed == 0u && contract->candidate_not_installed == 1u) {
        contract->satisfied_mask |= STAGE64_XNU_PMAP_TRANSITION_DRYRUN_SAT_NO_LIVE_PMAP_INSTALL;
    } else {
        contract->failure_mask |= STAGE64_XNU_PMAP_TRANSITION_DRYRUN_FAIL_SAFETY_BOUNDARY;
    }
    if (contract->ttbr_written == 0u && contract->ttbcr_written == 0u && contract->dacr_written == 0u &&
        contract->sctlr_written == 0u && contract->control_register_plan_readonly == 1u) {
        contract->satisfied_mask |= STAGE64_XNU_PMAP_TRANSITION_DRYRUN_SAT_NO_CONTROL_REG_WRITE;
    } else {
        contract->failure_mask |= STAGE64_XNU_PMAP_TRANSITION_DRYRUN_FAIL_SAFETY_BOUNDARY;
    }
    if (contract->tlbs_invalidated == 0u && contract->tlb_plan_readonly == 1u) {
        contract->satisfied_mask |= STAGE64_XNU_PMAP_TRANSITION_DRYRUN_SAT_NO_TLB_INVALIDATE;
    } else {
        contract->failure_mask |= STAGE64_XNU_PMAP_TRANSITION_DRYRUN_FAIL_SAFETY_BOUNDARY;
    }
    if (contract->caches_changed == 0u &&
        (preflight->safety_mask & STAGE64_LOADER_SAFETY_NO_CACHE_CHANGE) != 0u) {
        contract->satisfied_mask |= STAGE64_XNU_PMAP_TRANSITION_DRYRUN_SAT_NO_CACHE_CHANGE;
    } else {
        contract->failure_mask |= STAGE64_XNU_PMAP_TRANSITION_DRYRUN_FAIL_SAFETY_BOUNDARY;
    }
    if (contract->xnu_start_executed == 0u && contract->generated_macho_executed == 0u &&
        (preflight->safety_mask & STAGE64_LOADER_SAFETY_NO_EXECUTE) != 0u) {
        contract->satisfied_mask |= STAGE64_XNU_PMAP_TRANSITION_DRYRUN_SAT_NO_XNU_MACHO_EXEC;
    } else {
        contract->failure_mask |= STAGE64_XNU_PMAP_TRANSITION_DRYRUN_FAIL_SAFETY_BOUNDARY;
    }
    if (contract->persistent_write_attempted == 0u &&
        (preflight->safety_mask & STAGE64_LOADER_SAFETY_NO_PERSIST_WRITE) != 0u) {
        contract->satisfied_mask |= STAGE64_XNU_PMAP_TRANSITION_DRYRUN_SAT_NO_PERSIST_WRITE;
    } else {
        contract->failure_mask |= STAGE64_XNU_PMAP_TRANSITION_DRYRUN_FAIL_SAFETY_BOUNDARY;
    }

    if (multi->fail_closed == 1u && preflight->xnu_compile_graph.fail_closed == 1u &&
        preflight->xnu_object_subset.fail_closed == 1u && preflight->xnu_link.fail_closed == 1u &&
        contract->local_only == 1u && contract->candidate_not_installed == 1u &&
        contract->control_register_plan_readonly == 1u && contract->tlb_plan_readonly == 1u) {
        contract->fail_closed = 1u;
        contract->satisfied_mask |= STAGE64_XNU_PMAP_TRANSITION_DRYRUN_SAT_FAIL_CLOSED;
    } else {
        contract->failure_mask |= STAGE64_XNU_PMAP_TRANSITION_DRYRUN_FAIL_SAFETY_BOUNDARY;
    }

finish:
    if (contract->satisfied_mask == contract->required_mask && contract->failure_mask == 0u) {
        contract->status = STAGE64_STATUS_OK;
        xnu_log_puts("Stage64 XNU pmap transition prerequisite dry-run contract ok\n");
    } else {
        contract->status = STAGE64_STATUS_BASE | contract->failure_mask;
        xnu_log_puts("Stage64 XNU pmap transition prerequisite dry-run contract failed\n");
    }
    contract->checksum = stage64_pmap_transition_dryrun_checksum(contract);
    if (contract->checksum != stage64_pmap_transition_dryrun_checksum(contract)) {
        contract->failure_mask |= STAGE64_XNU_PMAP_TRANSITION_DRYRUN_FAIL_CHECKSUM;
        contract->status = STAGE64_STATUS_BASE | contract->failure_mask;
        contract->checksum = stage64_pmap_transition_dryrun_checksum(contract);
    }

    stage64_xnu_pmap_transition_dryrun_contract_log(contract);
    return contract->status == STAGE64_STATUS_OK;
}

const struct stage64_xnu_pmap_transition_dryrun_contract *stage64_xnu_pmap_transition_dryrun_contract_result(void)
{
    return &g_stage64_xnu_pmap_transition_dryrun_contract;
}
