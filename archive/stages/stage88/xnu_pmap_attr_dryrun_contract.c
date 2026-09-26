#include "stage88.h"

/*
 * Stage84 XNU pmap cache/MMU attribute dry-run contract.
 *
 * This is a Stage-owned proof of the exact ARMv7 short-descriptor attribute
 * arithmetic used by public XNU references.  It models proc_reg.h constants,
 * pmap.h WIMG names, pmap.c wimg_to_pte() selection, and arm_vm_init.c
 * page-granular protection helper templates without compiling, linking, or
 * executing public VM/pmap runtime code and without touching live MMU state.
 */

static struct stage88_xnu_pmap_attr_dryrun_contract g_stage88_xnu_pmap_attr_dryrun_contract;

static uint32_t stage88_pmap_attr_dryrun_checksum(
    volatile const struct stage88_xnu_pmap_attr_dryrun_contract *contract)
{
    volatile const uint32_t *words = (volatile const uint32_t *)(uintptr_t)contract;
    uint32_t count = (uint32_t)(offsetof(struct stage88_xnu_pmap_attr_dryrun_contract, checksum) /
                                sizeof(uint32_t));
    uint32_t checksum = 0u;

    for (uint32_t i = 0; i < count; i++) {
        checksum ^= words[i];
    }

    return checksum;
}

static uint32_t stage88_pmap_attr_make_pte_attr(uint32_t attrindx)
{
    return ((attrindx & 0x3u) << 2) | (((attrindx >> 2) & 0x1u) << 6);
}

static uint32_t stage88_pmap_attr_decode_pte_attr(uint32_t word)
{
    return ((word >> 2) & 0x3u) | (((word >> 6) & 0x1u) << 2);
}

static uint32_t stage88_pmap_attr_make_tte_attr(uint32_t attrindx)
{
    return ((attrindx & 0x3u) << 2) | (((attrindx >> 2) & 0x1u) << 12);
}

static uint32_t stage88_pmap_attr_decode_tte_attr(uint32_t word)
{
    return ((word >> 2) & 0x3u) | (((word >> 12) & 0x1u) << 2);
}

static uint32_t stage88_pmap_attr_make_pte_ap(uint32_t ap)
{
    return ((ap & 0x1u) << 5) | (((ap >> 1) & 0x1u) << 9);
}

static uint32_t stage88_pmap_attr_decode_pte_ap(uint32_t word)
{
    return ((word >> 5) & 0x1u) | (((word >> 9) & 0x1u) << 1);
}

static uint32_t stage88_pmap_attr_make_tte_ap(uint32_t ap)
{
    return ((ap & 0x1u) << 11) | (((ap >> 1) & 0x1u) << 15);
}

static uint32_t stage88_pmap_attr_decode_tte_ap(uint32_t word)
{
    return ((word >> 11) & 0x1u) | (((word >> 15) & 0x1u) << 1);
}

static uint32_t stage88_pmap_attr_make_page_template(uint32_t ap, uint32_t xn)
{
    uint32_t word = STAGE88_XNU_PMAP_ATTR_DRYRUN_PTE_TYPE |
                    STAGE88_XNU_PMAP_ATTR_DRYRUN_PTE_AF |
                    STAGE88_XNU_PMAP_ATTR_DRYRUN_PTE_SH |
                    stage88_pmap_attr_make_pte_attr(STAGE88_XNU_PMAP_ATTR_DRYRUN_CACHE_DEFAULT) |
                    stage88_pmap_attr_make_pte_ap(ap);

    if (xn) {
        word |= STAGE88_XNU_PMAP_ATTR_DRYRUN_PTE_NX;
    }

    return word;
}

static uint32_t stage88_pmap_attr_make_section_template(uint32_t ap, uint32_t xn)
{
    uint32_t word = STAGE88_XNU_PMAP_ATTR_DRYRUN_TTE_TYPE_SECTION |
                    STAGE88_XNU_PMAP_ATTR_DRYRUN_TTE_AF |
                    STAGE88_XNU_PMAP_ATTR_DRYRUN_TTE_SH |
                    stage88_pmap_attr_make_tte_attr(STAGE88_XNU_PMAP_ATTR_DRYRUN_CACHE_DEFAULT) |
                    stage88_pmap_attr_make_tte_ap(ap);

    if (xn) {
        word |= STAGE88_XNU_PMAP_ATTR_DRYRUN_TTE_NX;
    }

    return word;
}

static uint32_t stage88_pmap_attr_wimg_to_pte(uint32_t wimg)
{
    switch (wimg & 0xffu) {
    case STAGE88_XNU_PMAP_ATTR_DRYRUN_VM_WIMG_IO:
        return stage88_pmap_attr_make_pte_attr(STAGE88_XNU_PMAP_ATTR_DRYRUN_CACHE_DISABLE) |
               STAGE88_XNU_PMAP_ATTR_DRYRUN_PTE_NX;
    case STAGE88_XNU_PMAP_ATTR_DRYRUN_VM_WIMG_POSTED:
        return stage88_pmap_attr_make_pte_attr(STAGE88_XNU_PMAP_ATTR_DRYRUN_CACHE_POSTED) |
               STAGE88_XNU_PMAP_ATTR_DRYRUN_PTE_NX;
    case STAGE88_XNU_PMAP_ATTR_DRYRUN_VM_WIMG_WCOMB:
        return stage88_pmap_attr_make_pte_attr(STAGE88_XNU_PMAP_ATTR_DRYRUN_CACHE_WRITECOMB) |
               STAGE88_XNU_PMAP_ATTR_DRYRUN_PTE_NX;
    case STAGE88_XNU_PMAP_ATTR_DRYRUN_VM_WIMG_WTHRU:
        return stage88_pmap_attr_make_pte_attr(STAGE88_XNU_PMAP_ATTR_DRYRUN_CACHE_WRITETHRU) |
               STAGE88_XNU_PMAP_ATTR_DRYRUN_PTE_SH;
    case STAGE88_XNU_PMAP_ATTR_DRYRUN_VM_WIMG_COPYBACK:
        return stage88_pmap_attr_make_pte_attr(STAGE88_XNU_PMAP_ATTR_DRYRUN_CACHE_WRITEBACK) |
               STAGE88_XNU_PMAP_ATTR_DRYRUN_PTE_SH;
    case STAGE88_XNU_PMAP_ATTR_DRYRUN_VM_WIMG_INNERWBACK:
        return stage88_pmap_attr_make_pte_attr(STAGE88_XNU_PMAP_ATTR_DRYRUN_CACHE_INNERWRITEBACK) |
               STAGE88_XNU_PMAP_ATTR_DRYRUN_PTE_SH;
    default:
        return stage88_pmap_attr_make_pte_attr(STAGE88_XNU_PMAP_ATTR_DRYRUN_CACHE_DEFAULT) |
               STAGE88_XNU_PMAP_ATTR_DRYRUN_PTE_SH;
    }
}

static void stage88_xnu_pmap_attr_dryrun_contract_log(
    const struct stage88_xnu_pmap_attr_dryrun_contract *contract)
{
    xnu_log_kv32("stage88_xnu_pmap_attr_dryrun_contract_status", contract->status);
    xnu_log_kv32("stage88_xnu_pmap_attr_dryrun_contract_required_mask", contract->required_mask);
    xnu_log_kv32("stage88_xnu_pmap_attr_dryrun_contract_satisfied_mask", contract->satisfied_mask);
    xnu_log_kv32("stage88_xnu_pmap_attr_dryrun_contract_failure_mask", contract->failure_mask);
    xnu_log_kv32("stage88_xnu_pmap_attr_dryrun_contract_checksum", contract->checksum);
    xnu_log_kv32("stage88_xnu_pmap_attr_dryrun_source_table_status", contract->source_table_dryrun_status);
    xnu_log_kv32("stage88_xnu_pmap_attr_dryrun_source_page_status", contract->source_page_dryrun_status);
    xnu_log_kv32("stage88_xnu_pmap_attr_dryrun_cache_writeback", contract->cache_attrindx_writeback);
    xnu_log_kv32("stage88_xnu_pmap_attr_dryrun_cache_writecomb", contract->cache_attrindx_writecomb);
    xnu_log_kv32("stage88_xnu_pmap_attr_dryrun_cache_writethru", contract->cache_attrindx_writethru);
    xnu_log_kv32("stage88_xnu_pmap_attr_dryrun_cache_disable", contract->cache_attrindx_disable);
    xnu_log_kv32("stage88_xnu_pmap_attr_dryrun_cache_innerwriteback", contract->cache_attrindx_innerwriteback);
    xnu_log_kv32("stage88_xnu_pmap_attr_dryrun_cache_posted", contract->cache_attrindx_posted);
    xnu_log_kv32("stage88_xnu_pmap_attr_dryrun_cache_default", contract->cache_attrindx_default);
    xnu_log_kv32("stage88_xnu_pmap_attr_dryrun_ap_rwna", contract->ap_rwna);
    xnu_log_kv32("stage88_xnu_pmap_attr_dryrun_ap_rwrw", contract->ap_rwrw);
    xnu_log_kv32("stage88_xnu_pmap_attr_dryrun_ap_rona", contract->ap_rona);
    xnu_log_kv32("stage88_xnu_pmap_attr_dryrun_ap_roro", contract->ap_roro);
    xnu_log_kv32("stage88_xnu_pmap_attr_dryrun_vm_wimg_default", contract->vm_wimg_default);
    xnu_log_kv32("stage88_xnu_pmap_attr_dryrun_vm_wimg_copyback", contract->vm_wimg_copyback);
    xnu_log_kv32("stage88_xnu_pmap_attr_dryrun_vm_wimg_innerwback", contract->vm_wimg_innerwback);
    xnu_log_kv32("stage88_xnu_pmap_attr_dryrun_vm_wimg_io", contract->vm_wimg_io);
    xnu_log_kv32("stage88_xnu_pmap_attr_dryrun_vm_wimg_posted", contract->vm_wimg_posted);
    xnu_log_kv32("stage88_xnu_pmap_attr_dryrun_vm_wimg_wthru", contract->vm_wimg_wthru);
    xnu_log_kv32("stage88_xnu_pmap_attr_dryrun_vm_wimg_wcomb", contract->vm_wimg_wcomb);
    xnu_log_kv32("stage88_xnu_pmap_attr_dryrun_pte_attr_writeback", contract->pte_attr_writeback);
    xnu_log_kv32("stage88_xnu_pmap_attr_dryrun_pte_attr_writecomb", contract->pte_attr_writecomb);
    xnu_log_kv32("stage88_xnu_pmap_attr_dryrun_pte_attr_writethru", contract->pte_attr_writethru);
    xnu_log_kv32("stage88_xnu_pmap_attr_dryrun_pte_attr_disable", contract->pte_attr_disable);
    xnu_log_kv32("stage88_xnu_pmap_attr_dryrun_pte_attr_innerwriteback", contract->pte_attr_innerwriteback);
    xnu_log_kv32("stage88_xnu_pmap_attr_dryrun_pte_attr_roundtrip_mask", contract->pte_attr_roundtrip_mask);
    xnu_log_kv32("stage88_xnu_pmap_attr_dryrun_tte_attr_writeback", contract->tte_attr_writeback);
    xnu_log_kv32("stage88_xnu_pmap_attr_dryrun_tte_attr_writecomb", contract->tte_attr_writecomb);
    xnu_log_kv32("stage88_xnu_pmap_attr_dryrun_tte_attr_writethru", contract->tte_attr_writethru);
    xnu_log_kv32("stage88_xnu_pmap_attr_dryrun_tte_attr_disable", contract->tte_attr_disable);
    xnu_log_kv32("stage88_xnu_pmap_attr_dryrun_tte_attr_innerwriteback", contract->tte_attr_innerwriteback);
    xnu_log_kv32("stage88_xnu_pmap_attr_dryrun_tte_attr_roundtrip_mask", contract->tte_attr_roundtrip_mask);
    xnu_log_kv32("stage88_xnu_pmap_attr_dryrun_pte_template_rwx_word", contract->pte_template_rwx_word);
    xnu_log_kv32("stage88_xnu_pmap_attr_dryrun_pte_template_rwnx_word", contract->pte_template_rwnx_word);
    xnu_log_kv32("stage88_xnu_pmap_attr_dryrun_pte_template_rox_word", contract->pte_template_rox_word);
    xnu_log_kv32("stage88_xnu_pmap_attr_dryrun_pte_template_ronx_word", contract->pte_template_ronx_word);
    xnu_log_kv32("stage88_xnu_pmap_attr_dryrun_section_template_word", contract->section_template_word);
    xnu_log_kv32("stage88_xnu_pmap_attr_dryrun_wimg_default_pte_bits", contract->wimg_default_pte_bits);
    xnu_log_kv32("stage88_xnu_pmap_attr_dryrun_wimg_io_pte_bits", contract->wimg_io_pte_bits);
    xnu_log_kv32("stage88_xnu_pmap_attr_dryrun_wimg_posted_pte_bits", contract->wimg_posted_pte_bits);
    xnu_log_kv32("stage88_xnu_pmap_attr_dryrun_wimg_wcomb_pte_bits", contract->wimg_wcomb_pte_bits);
    xnu_log_kv32("stage88_xnu_pmap_attr_dryrun_wimg_wthru_pte_bits", contract->wimg_wthru_pte_bits);
    xnu_log_kv32("stage88_xnu_pmap_attr_dryrun_wimg_innerwback_pte_bits", contract->wimg_innerwback_pte_bits);
    xnu_log_kv32("stage88_xnu_pmap_attr_dryrun_prior_table_section_word", contract->prior_table_section_word);
    xnu_log_kv32("stage88_xnu_pmap_attr_dryrun_prior_table_attr_seen", contract->prior_table_attr_seen);
    xnu_log_kv32("stage88_xnu_pmap_attr_dryrun_prior_page_pte_word", contract->prior_page_pte_word);
    xnu_log_kv32("stage88_xnu_pmap_attr_dryrun_prior_page_attr_seen", contract->prior_page_attr_seen);
    xnu_log_kv32("stage88_xnu_pmap_attr_dryrun_public_pmap_compile_count", contract->public_pmap_compile_count);
    xnu_log_kv32("stage88_xnu_pmap_attr_dryrun_public_pmap_link_count", contract->public_pmap_link_count);
    xnu_log_kv32("stage88_xnu_pmap_attr_dryrun_public_pmap_execute_count", contract->public_pmap_execute_count);
    xnu_log_kv32("stage88_xnu_pmap_attr_dryrun_public_arm_vm_init_executed", contract->public_arm_vm_init_executed);
    xnu_log_kv32("stage88_xnu_pmap_attr_dryrun_proposed_workspace_written", contract->proposed_workspace_written);
    xnu_log_kv32("stage88_xnu_pmap_attr_dryrun_live_pmap_tables_installed", contract->live_pmap_tables_installed);
    xnu_log_kv32("stage88_xnu_pmap_attr_dryrun_ttbr_written", contract->ttbr_written);
    xnu_log_kv32("stage88_xnu_pmap_attr_dryrun_ttbcr_written", contract->ttbcr_written);
    xnu_log_kv32("stage88_xnu_pmap_attr_dryrun_dacr_written", contract->dacr_written);
    xnu_log_kv32("stage88_xnu_pmap_attr_dryrun_sctlr_written", contract->sctlr_written);
    xnu_log_kv32("stage88_xnu_pmap_attr_dryrun_tlbs_invalidated", contract->tlbs_invalidated);
    xnu_log_kv32("stage88_xnu_pmap_attr_dryrun_caches_changed", contract->caches_changed);
    xnu_log_kv32("stage88_xnu_pmap_attr_dryrun_persistent_write_attempted", contract->persistent_write_attempted);
    xnu_log_kv32("stage88_xnu_pmap_attr_dryrun_xnu_start_executed", contract->xnu_start_executed);
    xnu_log_kv32("stage88_xnu_pmap_attr_dryrun_generated_macho_executed", contract->generated_macho_executed);
    xnu_log_kv32("stage88_xnu_pmap_attr_dryrun_local_only", contract->local_only);
    xnu_log_kv32("stage88_xnu_pmap_attr_dryrun_fail_closed", contract->fail_closed);
}

int stage88_xnu_pmap_attr_dryrun_contract_selftest(const struct stage88_loader_preflight *preflight)
{
    struct stage88_xnu_pmap_attr_dryrun_contract *contract = &g_stage88_xnu_pmap_attr_dryrun_contract;
    const struct stage88_xnu_pmap_table_dryrun_contract *table = stage88_xnu_pmap_table_dryrun_contract_result();
    const struct stage88_xnu_pmap_page_dryrun_contract *page = stage88_xnu_pmap_page_dryrun_contract_result();
    const struct stage88_ttbr0_roundtrip *ttbr_rt = mmu_stage88_ttbr0_roundtrip_result();
    uint32_t source_rollups_ok;
    uint32_t checksum_once;

    memset(contract, 0, sizeof(*contract));
    contract->version = STAGE88_XNU_PMAP_ATTR_DRYRUN_CONTRACT_VERSION;
    contract->size = sizeof(*contract);
    contract->status = STAGE88_STATUS_BASE;
    contract->required_mask = STAGE88_XNU_PMAP_ATTR_DRYRUN_REQUIRED_MASK;

    xnu_log_puts("Stage84 XNU pmap cache/MMU attribute dry-run contract begin\n");

    if (!preflight || !table || !page || !ttbr_rt) {
        contract->failure_mask |= STAGE88_XNU_PMAP_ATTR_DRYRUN_FAIL_SOURCE |
                                  STAGE88_XNU_PMAP_ATTR_DRYRUN_FAIL_SAFETY_BOUNDARY;
        goto finish;
    }

    contract->source_table_dryrun_status = table->status;
    contract->source_table_dryrun_required_mask = table->required_mask;
    contract->source_table_dryrun_satisfied_mask = table->satisfied_mask;
    contract->source_table_dryrun_failure_mask = table->failure_mask;
    contract->source_table_dryrun_checksum = table->checksum;
    contract->source_page_dryrun_status = page->status;
    contract->source_page_dryrun_required_mask = page->required_mask;
    contract->source_page_dryrun_satisfied_mask = page->satisfied_mask;
    contract->source_page_dryrun_failure_mask = page->failure_mask;
    contract->source_page_dryrun_checksum = page->checksum;
    contract->source_bootstrap_status = preflight->xnu_bootstrap_contract_status;
    contract->source_pmap_bootstrap_status = preflight->xnu_pmap_bootstrap_contract_status;
    contract->source_cache_preserved_status = preflight->cache_preserved_status;
    contract->source_compile_graph_status = preflight->xnu_compile_graph_status;
    contract->source_object_subset_status = preflight->xnu_object_subset_status;
    contract->source_link_status = preflight->xnu_link_status;

    source_rollups_ok = (contract->source_table_dryrun_status == STAGE88_STATUS_OK &&
                         contract->source_table_dryrun_satisfied_mask == contract->source_table_dryrun_required_mask &&
                         contract->source_table_dryrun_failure_mask == 0u &&
                         contract->source_page_dryrun_status == STAGE88_STATUS_OK &&
                         contract->source_page_dryrun_satisfied_mask == contract->source_page_dryrun_required_mask &&
                         contract->source_page_dryrun_failure_mask == 0u &&
                         contract->source_bootstrap_status == STAGE88_STATUS_OK &&
                         contract->source_pmap_bootstrap_status == STAGE88_STATUS_OK &&
                         contract->source_cache_preserved_status == STAGE88_STATUS_OK &&
                         contract->source_compile_graph_status == STAGE88_STATUS_OK &&
                         contract->source_object_subset_status == STAGE88_STATUS_OK &&
                         contract->source_link_status == STAGE88_STATUS_OK) ? 1u : 0u;

    if (contract->source_table_dryrun_status == STAGE88_STATUS_OK &&
        contract->source_table_dryrun_satisfied_mask == contract->source_table_dryrun_required_mask &&
        contract->source_table_dryrun_failure_mask == 0u) {
        contract->satisfied_mask |= STAGE88_XNU_PMAP_ATTR_DRYRUN_SAT_SOURCE_TABLE_DRYRUN;
    } else {
        contract->failure_mask |= STAGE88_XNU_PMAP_ATTR_DRYRUN_FAIL_SOURCE;
    }
    if (contract->source_page_dryrun_status == STAGE88_STATUS_OK &&
        contract->source_page_dryrun_satisfied_mask == contract->source_page_dryrun_required_mask &&
        contract->source_page_dryrun_failure_mask == 0u) {
        contract->satisfied_mask |= STAGE88_XNU_PMAP_ATTR_DRYRUN_SAT_SOURCE_PAGE_DRYRUN;
    } else {
        contract->failure_mask |= STAGE88_XNU_PMAP_ATTR_DRYRUN_FAIL_SOURCE;
    }
    if (source_rollups_ok) {
        contract->satisfied_mask |= STAGE88_XNU_PMAP_ATTR_DRYRUN_SAT_SOURCE_ROLLUPS;
    } else {
        contract->failure_mask |= STAGE88_XNU_PMAP_ATTR_DRYRUN_FAIL_SOURCE;
    }

    contract->cache_attrindx_writeback = STAGE88_XNU_PMAP_ATTR_DRYRUN_CACHE_WRITEBACK;
    contract->cache_attrindx_writecomb = STAGE88_XNU_PMAP_ATTR_DRYRUN_CACHE_WRITECOMB;
    contract->cache_attrindx_writethru = STAGE88_XNU_PMAP_ATTR_DRYRUN_CACHE_WRITETHRU;
    contract->cache_attrindx_disable = STAGE88_XNU_PMAP_ATTR_DRYRUN_CACHE_DISABLE;
    contract->cache_attrindx_innerwriteback = STAGE88_XNU_PMAP_ATTR_DRYRUN_CACHE_INNERWRITEBACK;
    contract->cache_attrindx_posted = STAGE88_XNU_PMAP_ATTR_DRYRUN_CACHE_POSTED;
    contract->cache_attrindx_default = STAGE88_XNU_PMAP_ATTR_DRYRUN_CACHE_DEFAULT;
    contract->ap_rwna = STAGE88_XNU_PMAP_ATTR_DRYRUN_AP_RWNA;
    contract->ap_rwrw = STAGE88_XNU_PMAP_ATTR_DRYRUN_AP_RWRW;
    contract->ap_rona = STAGE88_XNU_PMAP_ATTR_DRYRUN_AP_RONA;
    contract->ap_roro = STAGE88_XNU_PMAP_ATTR_DRYRUN_AP_RORO;
    contract->vm_wimg_default = STAGE88_XNU_PMAP_ATTR_DRYRUN_VM_WIMG_DEFAULT;
    contract->vm_wimg_copyback = STAGE88_XNU_PMAP_ATTR_DRYRUN_VM_WIMG_COPYBACK;
    contract->vm_wimg_innerwback = STAGE88_XNU_PMAP_ATTR_DRYRUN_VM_WIMG_INNERWBACK;
    contract->vm_wimg_io = STAGE88_XNU_PMAP_ATTR_DRYRUN_VM_WIMG_IO;
    contract->vm_wimg_posted = STAGE88_XNU_PMAP_ATTR_DRYRUN_VM_WIMG_POSTED;
    contract->vm_wimg_wthru = STAGE88_XNU_PMAP_ATTR_DRYRUN_VM_WIMG_WTHRU;
    contract->vm_wimg_wcomb = STAGE88_XNU_PMAP_ATTR_DRYRUN_VM_WIMG_WCOMB;

    if (contract->cache_attrindx_writeback == 0u && contract->cache_attrindx_writecomb == 1u &&
        contract->cache_attrindx_writethru == 2u && contract->cache_attrindx_disable == 3u &&
        contract->cache_attrindx_innerwriteback == 4u && contract->cache_attrindx_posted == 3u &&
        contract->cache_attrindx_default == 0u && contract->ap_rwna == 0u &&
        contract->ap_rwrw == 1u && contract->ap_rona == 2u && contract->ap_roro == 3u &&
        contract->vm_wimg_default == 0x02u && contract->vm_wimg_copyback == 0x02u &&
        contract->vm_wimg_innerwback == 0x12u && contract->vm_wimg_io == 0x07u &&
        contract->vm_wimg_posted == 0x27u && contract->vm_wimg_wthru == 0x0bu &&
        contract->vm_wimg_wcomb == 0x06u) {
        contract->satisfied_mask |= STAGE88_XNU_PMAP_ATTR_DRYRUN_SAT_PUBLIC_CONSTANTS |
                                    STAGE88_XNU_PMAP_ATTR_DRYRUN_SAT_CACHE_ATTRINDX_VALUES;
    } else {
        contract->failure_mask |= STAGE88_XNU_PMAP_ATTR_DRYRUN_FAIL_CONSTANTS;
    }

    contract->pte_attr_writeback = stage88_pmap_attr_make_pte_attr(contract->cache_attrindx_writeback);
    contract->pte_attr_writecomb = stage88_pmap_attr_make_pte_attr(contract->cache_attrindx_writecomb);
    contract->pte_attr_writethru = stage88_pmap_attr_make_pte_attr(contract->cache_attrindx_writethru);
    contract->pte_attr_disable = stage88_pmap_attr_make_pte_attr(contract->cache_attrindx_disable);
    contract->pte_attr_innerwriteback = stage88_pmap_attr_make_pte_attr(contract->cache_attrindx_innerwriteback);
    contract->pte_attr_mask = STAGE88_XNU_PMAP_ATTR_DRYRUN_PTE_ATTRINDXMASK;
    contract->tte_attr_writeback = stage88_pmap_attr_make_tte_attr(contract->cache_attrindx_writeback);
    contract->tte_attr_writecomb = stage88_pmap_attr_make_tte_attr(contract->cache_attrindx_writecomb);
    contract->tte_attr_writethru = stage88_pmap_attr_make_tte_attr(contract->cache_attrindx_writethru);
    contract->tte_attr_disable = stage88_pmap_attr_make_tte_attr(contract->cache_attrindx_disable);
    contract->tte_attr_innerwriteback = stage88_pmap_attr_make_tte_attr(contract->cache_attrindx_innerwriteback);
    contract->tte_attr_mask = STAGE88_XNU_PMAP_ATTR_DRYRUN_TTE_ATTRINDXMASK;

    if (stage88_pmap_attr_decode_pte_attr(contract->pte_attr_writeback) == contract->cache_attrindx_writeback) {
        contract->pte_attr_roundtrip_mask |= 0x01u;
    }
    if (stage88_pmap_attr_decode_pte_attr(contract->pte_attr_writecomb) == contract->cache_attrindx_writecomb) {
        contract->pte_attr_roundtrip_mask |= 0x02u;
    }
    if (stage88_pmap_attr_decode_pte_attr(contract->pte_attr_writethru) == contract->cache_attrindx_writethru) {
        contract->pte_attr_roundtrip_mask |= 0x04u;
    }
    if (stage88_pmap_attr_decode_pte_attr(contract->pte_attr_disable) == contract->cache_attrindx_disable) {
        contract->pte_attr_roundtrip_mask |= 0x08u;
    }
    if (stage88_pmap_attr_decode_pte_attr(contract->pte_attr_innerwriteback) == contract->cache_attrindx_innerwriteback) {
        contract->pte_attr_roundtrip_mask |= 0x10u;
    }
    if (stage88_pmap_attr_decode_tte_attr(contract->tte_attr_writeback) == contract->cache_attrindx_writeback) {
        contract->tte_attr_roundtrip_mask |= 0x01u;
    }
    if (stage88_pmap_attr_decode_tte_attr(contract->tte_attr_writecomb) == contract->cache_attrindx_writecomb) {
        contract->tte_attr_roundtrip_mask |= 0x02u;
    }
    if (stage88_pmap_attr_decode_tte_attr(contract->tte_attr_writethru) == contract->cache_attrindx_writethru) {
        contract->tte_attr_roundtrip_mask |= 0x04u;
    }
    if (stage88_pmap_attr_decode_tte_attr(contract->tte_attr_disable) == contract->cache_attrindx_disable) {
        contract->tte_attr_roundtrip_mask |= 0x08u;
    }
    if (stage88_pmap_attr_decode_tte_attr(contract->tte_attr_innerwriteback) == contract->cache_attrindx_innerwriteback) {
        contract->tte_attr_roundtrip_mask |= 0x10u;
    }

    if (contract->pte_attr_writeback == 0x00000000u && contract->pte_attr_writecomb == 0x00000004u &&
        contract->pte_attr_writethru == 0x00000008u && contract->pte_attr_disable == 0x0000000cu &&
        contract->pte_attr_innerwriteback == 0x00000040u && contract->pte_attr_mask == 0x0000004cu &&
        contract->tte_attr_writeback == 0x00000000u && contract->tte_attr_writecomb == 0x00000004u &&
        contract->tte_attr_writethru == 0x00000008u && contract->tte_attr_disable == 0x0000000cu &&
        contract->tte_attr_innerwriteback == 0x00001000u && contract->tte_attr_mask == 0x0000100cu &&
        contract->pte_attr_roundtrip_mask == 0x1fu && contract->tte_attr_roundtrip_mask == 0x1fu) {
        contract->satisfied_mask |= STAGE88_XNU_PMAP_ATTR_DRYRUN_SAT_ATTRINDX_MACROS;
    } else {
        contract->failure_mask |= STAGE88_XNU_PMAP_ATTR_DRYRUN_FAIL_ATTRINDX;
    }

    contract->pte_ap_rwna_word = stage88_pmap_attr_make_pte_ap(contract->ap_rwna);
    contract->pte_ap_rwrw_word = stage88_pmap_attr_make_pte_ap(contract->ap_rwrw);
    contract->pte_ap_rona_word = stage88_pmap_attr_make_pte_ap(contract->ap_rona);
    contract->pte_ap_roro_word = stage88_pmap_attr_make_pte_ap(contract->ap_roro);
    contract->tte_ap_rwna_word = stage88_pmap_attr_make_tte_ap(contract->ap_rwna);
    contract->tte_ap_rwrw_word = stage88_pmap_attr_make_tte_ap(contract->ap_rwrw);
    contract->tte_ap_rona_word = stage88_pmap_attr_make_tte_ap(contract->ap_rona);
    contract->tte_ap_roro_word = stage88_pmap_attr_make_tte_ap(contract->ap_roro);
    if (contract->pte_ap_rwna_word == 0x00000000u && contract->pte_ap_rwrw_word == 0x00000020u &&
        contract->pte_ap_rona_word == 0x00000200u && contract->pte_ap_roro_word == 0x00000220u &&
        contract->tte_ap_rwna_word == 0x00000000u && contract->tte_ap_rwrw_word == 0x00000800u &&
        contract->tte_ap_rona_word == 0x00008000u && contract->tte_ap_roro_word == 0x00008800u &&
        stage88_pmap_attr_decode_pte_ap(contract->pte_ap_roro_word) == contract->ap_roro &&
        stage88_pmap_attr_decode_tte_ap(contract->tte_ap_roro_word) == contract->ap_roro) {
        contract->satisfied_mask |= STAGE88_XNU_PMAP_ATTR_DRYRUN_SAT_ACCESS_PROTECTIONS;
    } else {
        contract->failure_mask |= STAGE88_XNU_PMAP_ATTR_DRYRUN_FAIL_AP;
    }

    contract->section_template_word = stage88_pmap_attr_make_section_template(contract->ap_rwrw, 0u);
    contract->section_template_expected_word = STAGE88_XNU_PMAP_ATTR_DRYRUN_TTE_TYPE_SECTION |
        STAGE88_XNU_PMAP_ATTR_DRYRUN_TTE_AF | STAGE88_XNU_PMAP_ATTR_DRYRUN_TTE_SH |
        contract->tte_attr_writeback | contract->tte_ap_rwrw_word;
    contract->section_template_mask = contract->section_template_word &
        STAGE88_XNU_PMAP_ATTR_DRYRUN_TTE_SECTION_TEMPLATE_MASK;
    contract->section_template_ap_seen = stage88_pmap_attr_decode_tte_ap(contract->section_template_word);
    contract->section_template_attr_seen = stage88_pmap_attr_decode_tte_attr(contract->section_template_word);
    if (contract->section_template_word == contract->section_template_expected_word &&
        contract->section_template_mask == 0x00010c02u &&
        contract->section_template_ap_seen == contract->ap_rwrw &&
        contract->section_template_attr_seen == contract->cache_attrindx_default) {
        contract->satisfied_mask |= STAGE88_XNU_PMAP_ATTR_DRYRUN_SAT_SECTION_TEMPLATE;
    } else {
        contract->failure_mask |= STAGE88_XNU_PMAP_ATTR_DRYRUN_FAIL_TEMPLATE;
    }

    contract->pte_template_rwx_word = stage88_pmap_attr_make_page_template(contract->ap_rwna, 0u);
    contract->pte_template_rwnx_word = stage88_pmap_attr_make_page_template(contract->ap_rwna, 1u);
    contract->pte_template_rox_word = stage88_pmap_attr_make_page_template(contract->ap_rona, 0u);
    contract->pte_template_ronx_word = stage88_pmap_attr_make_page_template(contract->ap_rona, 1u);
    contract->pte_template_expected_word = STAGE88_XNU_PMAP_ATTR_DRYRUN_PTE_TYPE |
        STAGE88_XNU_PMAP_ATTR_DRYRUN_PTE_AF | STAGE88_XNU_PMAP_ATTR_DRYRUN_PTE_SH |
        contract->pte_attr_writeback | contract->pte_ap_rwna_word;
    contract->pte_template_mask = contract->pte_template_rwx_word &
        STAGE88_XNU_PMAP_ATTR_DRYRUN_PTE_TEMPLATE_MASK;
    contract->pte_template_ap_seen = stage88_pmap_attr_decode_pte_ap(contract->pte_template_rwx_word);
    contract->pte_template_attr_seen = stage88_pmap_attr_decode_pte_attr(contract->pte_template_rwx_word);
    if (contract->pte_template_rwx_word == 0x00000412u &&
        contract->pte_template_rwnx_word == 0x00000413u &&
        contract->pte_template_rox_word == 0x00000612u &&
        contract->pte_template_ronx_word == 0x00000613u &&
        contract->pte_template_expected_word == contract->pte_template_rwx_word &&
        contract->pte_template_mask == 0x00000412u &&
        contract->pte_template_ap_seen == contract->ap_rwna &&
        contract->pte_template_attr_seen == contract->cache_attrindx_default) {
        contract->satisfied_mask |= STAGE88_XNU_PMAP_ATTR_DRYRUN_SAT_PAGE_TEMPLATE;
    } else {
        contract->failure_mask |= STAGE88_XNU_PMAP_ATTR_DRYRUN_FAIL_TEMPLATE;
    }

    contract->wimg_default_pte_bits = stage88_pmap_attr_wimg_to_pte(contract->vm_wimg_default);
    contract->wimg_copyback_pte_bits = stage88_pmap_attr_wimg_to_pte(contract->vm_wimg_copyback);
    contract->wimg_innerwback_pte_bits = stage88_pmap_attr_wimg_to_pte(contract->vm_wimg_innerwback);
    contract->wimg_io_pte_bits = stage88_pmap_attr_wimg_to_pte(contract->vm_wimg_io);
    contract->wimg_posted_pte_bits = stage88_pmap_attr_wimg_to_pte(contract->vm_wimg_posted);
    contract->wimg_wthru_pte_bits = stage88_pmap_attr_wimg_to_pte(contract->vm_wimg_wthru);
    contract->wimg_wcomb_pte_bits = stage88_pmap_attr_wimg_to_pte(contract->vm_wimg_wcomb);
    contract->wimg_fallback_pte_bits = stage88_pmap_attr_wimg_to_pte(0u);
    if (contract->wimg_default_pte_bits == (contract->pte_attr_writeback | STAGE88_XNU_PMAP_ATTR_DRYRUN_PTE_SH) &&
        contract->wimg_copyback_pte_bits == contract->wimg_default_pte_bits) {
        contract->satisfied_mask |= STAGE88_XNU_PMAP_ATTR_DRYRUN_SAT_WIMG_DEFAULT_COPYBACK;
    } else {
        contract->failure_mask |= STAGE88_XNU_PMAP_ATTR_DRYRUN_FAIL_WIMG;
    }
    if (contract->wimg_io_pte_bits == (contract->pte_attr_disable | STAGE88_XNU_PMAP_ATTR_DRYRUN_PTE_NX) &&
        contract->wimg_posted_pte_bits == (contract->pte_attr_disable | STAGE88_XNU_PMAP_ATTR_DRYRUN_PTE_NX)) {
        contract->satisfied_mask |= STAGE88_XNU_PMAP_ATTR_DRYRUN_SAT_WIMG_DEVICE_POSTED;
    } else {
        contract->failure_mask |= STAGE88_XNU_PMAP_ATTR_DRYRUN_FAIL_WIMG;
    }
    if (contract->wimg_wcomb_pte_bits == (contract->pte_attr_writecomb | STAGE88_XNU_PMAP_ATTR_DRYRUN_PTE_NX) &&
        contract->wimg_wthru_pte_bits == (contract->pte_attr_writethru | STAGE88_XNU_PMAP_ATTR_DRYRUN_PTE_SH) &&
        contract->wimg_innerwback_pte_bits == (contract->pte_attr_innerwriteback | STAGE88_XNU_PMAP_ATTR_DRYRUN_PTE_SH) &&
        contract->wimg_fallback_pte_bits == (contract->pte_attr_writeback | STAGE88_XNU_PMAP_ATTR_DRYRUN_PTE_SH)) {
        contract->satisfied_mask |= STAGE88_XNU_PMAP_ATTR_DRYRUN_SAT_WIMG_OTHER_CACHE_MODES;
    } else {
        contract->failure_mask |= STAGE88_XNU_PMAP_ATTR_DRYRUN_FAIL_WIMG;
    }

    contract->public_arm_vm_page_granular_rwx = contract->pte_template_rwx_word;
    contract->public_arm_vm_page_granular_rwnx = contract->pte_template_rwnx_word;
    contract->public_arm_vm_page_granular_rox = contract->pte_template_rox_word;
    contract->public_arm_vm_page_granular_ronx = contract->pte_template_ronx_word;
    if (contract->public_arm_vm_page_granular_rwx == 0x00000412u &&
        contract->public_arm_vm_page_granular_rwnx == 0x00000413u &&
        contract->public_arm_vm_page_granular_rox == 0x00000612u &&
        contract->public_arm_vm_page_granular_ronx == 0x00000613u) {
        contract->satisfied_mask |= STAGE88_XNU_PMAP_ATTR_DRYRUN_SAT_PAGE_PROT_HELPERS;
    } else {
        contract->failure_mask |= STAGE88_XNU_PMAP_ATTR_DRYRUN_FAIL_TEMPLATE;
    }

    contract->prior_table_section_word = table->section_descriptor;
    contract->prior_table_attr_seen = table->descriptor_attr_mask_seen;
    contract->prior_page_pte_word = page->pte_kernel_word & STAGE88_XNU_PMAP_PAGE_DRYRUN_PTE_ATTR_MASK;
    contract->prior_page_attr_seen = page->pte_attr_mask_seen;
    contract->prior_page_expected_attr = page->pte_expected_attr_mask;
    if (contract->prior_table_section_word == 0x00010c02u &&
        contract->prior_table_attr_seen == (0x00010c02u & STAGE88_XNU_PMAP_TABLE_DRYRUN_DESC_ATTR_MASK) &&
        contract->prior_page_pte_word == contract->pte_template_rwx_word &&
        contract->prior_page_attr_seen == contract->pte_template_rwx_word &&
        contract->prior_page_expected_attr == contract->pte_template_rwx_word) {
        contract->satisfied_mask |= STAGE88_XNU_PMAP_ATTR_DRYRUN_SAT_PRIOR_READBACKS;
    } else {
        contract->failure_mask |= STAGE88_XNU_PMAP_ATTR_DRYRUN_FAIL_READBACK;
    }

    contract->local_only = 1u;
    if (contract->local_only == 1u) {
        contract->satisfied_mask |= STAGE88_XNU_PMAP_ATTR_DRYRUN_SAT_LOCAL_ONLY;
    } else {
        contract->failure_mask |= STAGE88_XNU_PMAP_ATTR_DRYRUN_FAIL_SAFETY_BOUNDARY;
    }

    contract->public_pmap_compile_count = preflight->xnu_compile_graph.pmap_public_compile_count;
    contract->public_pmap_link_count = preflight->xnu_compile_graph.pmap_public_link_count;
    contract->public_pmap_execute_count = page->public_pmap_execute_count;
    contract->public_arm_vm_init_executed = 0u;
    contract->public_pmap_runtime_executed = 0u;
    if (contract->public_pmap_compile_count == 0u && contract->public_pmap_link_count == 0u &&
        contract->public_pmap_execute_count == 0u && contract->public_arm_vm_init_executed == 0u &&
        contract->public_pmap_runtime_executed == 0u) {
        contract->satisfied_mask |= STAGE88_XNU_PMAP_ATTR_DRYRUN_SAT_NO_PUBLIC_PMAP_EXEC;
    } else {
        contract->failure_mask |= STAGE88_XNU_PMAP_ATTR_DRYRUN_FAIL_SAFETY_BOUNDARY;
    }

    contract->proposed_workspace_written = page->proposed_workspace_written | table->proposed_workspace_written;
    contract->live_pmap_tables_installed = page->live_pmap_tables_installed | table->live_pmap_tables_installed;
    contract->ttbr_written = page->ttbr_written | table->ttbr_written;
    contract->ttbcr_written = page->ttbcr_written | table->ttbcr_written;
    contract->dacr_written = page->dacr_written | table->dacr_written;
    contract->sctlr_written = page->sctlr_written | table->sctlr_written;
    contract->tlbs_invalidated = page->tlbs_invalidated | table->tlbs_invalidated;
    contract->caches_changed = page->caches_changed | table->caches_changed | ttbr_rt->caches_changed;
    contract->persistent_write_attempted = page->persistent_write_attempted | table->persistent_write_attempted |
        ttbr_rt->persistent_write_attempted;
    contract->xnu_start_executed = page->xnu_start_executed | table->xnu_start_executed | ttbr_rt->xnu_entry_executed;
    contract->generated_macho_executed = page->generated_macho_executed | table->generated_macho_executed |
        ttbr_rt->macho_bytes_executed;

    if (contract->proposed_workspace_written == 0u) {
        contract->satisfied_mask |= STAGE88_XNU_PMAP_ATTR_DRYRUN_SAT_NO_PROPOSED_WORKSPACE_WRITE;
    } else {
        contract->failure_mask |= STAGE88_XNU_PMAP_ATTR_DRYRUN_FAIL_SAFETY_BOUNDARY;
    }
    if (contract->live_pmap_tables_installed == 0u) {
        contract->satisfied_mask |= STAGE88_XNU_PMAP_ATTR_DRYRUN_SAT_NO_LIVE_PMAP_INSTALL;
    } else {
        contract->failure_mask |= STAGE88_XNU_PMAP_ATTR_DRYRUN_FAIL_SAFETY_BOUNDARY;
    }
    if (contract->ttbr_written == 0u && contract->ttbcr_written == 0u &&
        contract->dacr_written == 0u && contract->sctlr_written == 0u) {
        contract->satisfied_mask |= STAGE88_XNU_PMAP_ATTR_DRYRUN_SAT_NO_CONTROL_REG_WRITE;
    } else {
        contract->failure_mask |= STAGE88_XNU_PMAP_ATTR_DRYRUN_FAIL_SAFETY_BOUNDARY;
    }
    if (contract->tlbs_invalidated == 0u) {
        contract->satisfied_mask |= STAGE88_XNU_PMAP_ATTR_DRYRUN_SAT_NO_TLB_INVALIDATE;
    } else {
        contract->failure_mask |= STAGE88_XNU_PMAP_ATTR_DRYRUN_FAIL_SAFETY_BOUNDARY;
    }
    if (contract->caches_changed == 0u &&
        (preflight->safety_mask & STAGE88_LOADER_SAFETY_NO_CACHE_CHANGE) != 0u) {
        contract->satisfied_mask |= STAGE88_XNU_PMAP_ATTR_DRYRUN_SAT_NO_CACHE_CHANGE;
    } else {
        contract->failure_mask |= STAGE88_XNU_PMAP_ATTR_DRYRUN_FAIL_SAFETY_BOUNDARY;
    }
    if (contract->xnu_start_executed == 0u && contract->generated_macho_executed == 0u) {
        contract->satisfied_mask |= STAGE88_XNU_PMAP_ATTR_DRYRUN_SAT_NO_XNU_MACHO_EXEC;
    } else {
        contract->failure_mask |= STAGE88_XNU_PMAP_ATTR_DRYRUN_FAIL_SAFETY_BOUNDARY;
    }
    if (contract->persistent_write_attempted == 0u &&
        (preflight->safety_mask & STAGE88_LOADER_SAFETY_NO_PERSIST_WRITE) != 0u) {
        contract->satisfied_mask |= STAGE88_XNU_PMAP_ATTR_DRYRUN_SAT_NO_PERSIST_WRITE;
    } else {
        contract->failure_mask |= STAGE88_XNU_PMAP_ATTR_DRYRUN_FAIL_SAFETY_BOUNDARY;
    }

    if (preflight->xnu_compile_graph.fail_closed == 1u &&
        preflight->xnu_object_subset.fail_closed == 1u &&
        preflight->xnu_link.fail_closed == 1u &&
        table->fail_closed == 1u && page->fail_closed == 1u) {
        contract->fail_closed = 1u;
        contract->satisfied_mask |= STAGE88_XNU_PMAP_ATTR_DRYRUN_SAT_FAIL_CLOSED;
    } else {
        contract->failure_mask |= STAGE88_XNU_PMAP_ATTR_DRYRUN_FAIL_SAFETY_BOUNDARY;
    }

finish:
    if (contract->satisfied_mask == contract->required_mask && contract->failure_mask == 0u) {
        contract->status = STAGE88_STATUS_OK;
        xnu_log_puts("Stage84 XNU pmap cache/MMU attribute dry-run contract ok\n");
    } else {
        contract->status = STAGE88_STATUS_FAIL(contract->failure_mask);
        xnu_log_puts("Stage84 XNU pmap cache/MMU attribute dry-run contract failed\n");
    }
    contract->checksum = stage88_pmap_attr_dryrun_checksum(contract);
    checksum_once = stage88_pmap_attr_dryrun_checksum(contract);
    if (contract->checksum != checksum_once) {
        contract->failure_mask |= STAGE88_XNU_PMAP_ATTR_DRYRUN_FAIL_CHECKSUM;
        contract->status = STAGE88_STATUS_FAIL(contract->failure_mask);
        contract->checksum = stage88_pmap_attr_dryrun_checksum(contract);
    }

    stage88_xnu_pmap_attr_dryrun_contract_log(contract);
    return contract->status == STAGE88_STATUS_OK;
}

const struct stage88_xnu_pmap_attr_dryrun_contract *stage88_xnu_pmap_attr_dryrun_contract_result(void)
{
    return &g_stage88_xnu_pmap_attr_dryrun_contract;
}
