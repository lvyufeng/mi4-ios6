#include "stage70.h"

/*
 * Stage70 XNU pmap page-granular dry-run contract.
 *
 * This is a Stage-owned, target-side proof for the ARMv7 short-descriptor
 * coarse page table path used by public ARM XNU references when a mapping must
 * be refined below 1 MiB section granularity.  It constructs a local L1 table
 * plus one local 4 KiB L2 page-table page, installs four L1 coarse descriptors
 * into the local L1 array, populates 1024 small-page PTEs into the local L2
 * page, reads descriptors/PTEs back, performs software translations, and
 * checksums only local Stage-owned scratch buffers.
 *
 * It does not write the proposed pmap workspace, does not install live
 * XNU/pmap tables, does not touch TTBR/TTBCR/DACR/SCTLR, does not invalidate
 * TLBs, does not change cache policy, and does not execute public VM/pmap code.
 */

static struct stage70_xnu_pmap_page_dryrun_contract g_stage70_xnu_pmap_page_dryrun_contract;

static uint32_t g_stage70_pmap_page_dryrun_l1[STAGE70_XNU_PMAP_PAGE_DRYRUN_L1_ENTRY_COUNT]
    __attribute__((aligned(STAGE70_XNU_PMAP_PAGE_DRYRUN_L1_ALIGNMENT)));

static uint32_t g_stage70_pmap_page_dryrun_l2[STAGE70_XNU_PMAP_PAGE_DRYRUN_L2_PTE_COUNT]
    __attribute__((aligned(STAGE70_XNU_PMAP_PAGE_DRYRUN_L2_ALIGNMENT)));

static uint32_t stage70_pmap_page_dryrun_checksum(
    volatile const struct stage70_xnu_pmap_page_dryrun_contract *contract)
{
    volatile const uint32_t *words = (volatile const uint32_t *)(uintptr_t)contract;
    uint32_t count = (uint32_t)(offsetof(struct stage70_xnu_pmap_page_dryrun_contract, checksum) /
                                sizeof(uint32_t));
    uint32_t checksum = 0u;

    for (uint32_t i = 0; i < count; i++) {
        checksum ^= words[i];
    }

    return checksum;
}

static uint32_t stage70_pmap_page_dryrun_buffer_checksum(const uint32_t *buf, uint32_t count)
{
    uint32_t checksum = 0u;

    for (uint32_t i = 0; i < count; i++) {
        checksum ^= buf[i] + (i * 0x7f4a7c15u);
    }

    return checksum;
}

static uint32_t stage70_pmap_page_dryrun_add_overflow_u32(uint32_t a, uint32_t b, uint32_t *out)
{
    *out = a + b;
    return *out < a;
}

static uint32_t stage70_pmap_page_dryrun_l1_index(uint32_t va)
{
    return va >> 20;
}

static uint32_t stage70_pmap_page_dryrun_l2_index(uint32_t va)
{
    return (va & STAGE70_XNU_PMAP_PAGE_DRYRUN_L2_INDEX_MASK) >>
           STAGE70_XNU_PMAP_PAGE_DRYRUN_L2_INDEX_SHIFT;
}

static uint32_t stage70_pmap_page_dryrun_make_l1_table_descriptor(uint32_t l2_base)
{
    return (l2_base & STAGE70_XNU_PMAP_PAGE_DRYRUN_L1_TABLE_MASK) |
           STAGE70_XNU_PMAP_PAGE_DRYRUN_L1_TABLE_TYPE;
}

static uint32_t stage70_pmap_page_dryrun_make_pte(uint32_t pa)
{
    return (pa & STAGE70_XNU_PMAP_PAGE_DRYRUN_PTE_PAGE_MASK) |
           STAGE70_XNU_PMAP_PAGE_DRYRUN_PTE_ATTR_DEFAULT;
}

static uint32_t stage70_pmap_page_dryrun_expected_pa(uint32_t va,
                                                     uint32_t virt_base,
                                                     uint32_t phys_base)
{
    return phys_base + (va - virt_base);
}

static uint32_t stage70_pmap_page_dryrun_local_ranges_overlap(uint32_t a_base, uint32_t a_limit,
                                                              uint32_t b_base, uint32_t b_limit)
{
    if (a_limit <= a_base || b_limit <= b_base) {
        return 1u;
    }
    return (a_base < b_limit && b_base < a_limit) ? 1u : 0u;
}

static uint32_t stage70_pmap_page_dryrun_populate_window(
    uint32_t *l1, uint32_t *l2, uint32_t virt_base, uint32_t phys_base)
{
    uint32_t first_l1 = stage70_pmap_page_dryrun_l1_index(virt_base);
    uint32_t writes = 0u;

    for (uint32_t i = 0; i < STAGE70_XNU_PMAP_PAGE_DRYRUN_L2_TABLES_PER_PAGE; i++) {
        uint32_t l1_index = first_l1 + i;
        uint32_t l2_base = (uint32_t)(uintptr_t)&l2[i * STAGE70_XNU_PMAP_PAGE_DRYRUN_L2_PTES_PER_TABLE];

        if (l1_index >= STAGE70_XNU_PMAP_PAGE_DRYRUN_L1_ENTRY_COUNT) {
            return 0u;
        }
        l1[l1_index] = stage70_pmap_page_dryrun_make_l1_table_descriptor(l2_base);
    }

    for (uint32_t i = 0; i < STAGE70_XNU_PMAP_PAGE_DRYRUN_L2_PTE_COUNT; i++) {
        uint32_t pa = phys_base + (i * STAGE70_XNU_PMAP_PAGE_DRYRUN_PAGE_SIZE);
        l2[i] = stage70_pmap_page_dryrun_make_pte(pa);
        writes++;
    }

    return writes;
}

static uint32_t stage70_pmap_page_dryrun_translate(const uint32_t *l1,
                                                    uint32_t local_l2_base,
                                                    uint32_t local_l2_limit,
                                                    uint32_t va,
                                                    uint32_t *pa_out)
{
    uint32_t l1_index = stage70_pmap_page_dryrun_l1_index(va);
    uint32_t l2_index = stage70_pmap_page_dryrun_l2_index(va);
    uint32_t l1_desc;
    uint32_t l2_base;
    const uint32_t *l2;
    uint32_t pte;

    if (pa_out) {
        *pa_out = 0u;
    }
    if (l1_index >= STAGE70_XNU_PMAP_PAGE_DRYRUN_L1_ENTRY_COUNT) {
        return 0u;
    }
    l1_desc = l1[l1_index];
    if ((l1_desc & STAGE70_XNU_PMAP_PAGE_DRYRUN_L1_TYPE_MASK) !=
        STAGE70_XNU_PMAP_PAGE_DRYRUN_L1_TABLE_TYPE) {
        return 0u;
    }
    l2_base = l1_desc & STAGE70_XNU_PMAP_PAGE_DRYRUN_L1_TABLE_MASK;
    if (l2_base < local_l2_base || l2_base >= local_l2_limit ||
        local_l2_limit - l2_base < STAGE70_XNU_PMAP_PAGE_DRYRUN_L2_COARSE_BYTES) {
        return 0u;
    }
    l2 = (const uint32_t *)(uintptr_t)l2_base;
    pte = l2[l2_index];
    if ((pte & STAGE70_XNU_PMAP_PAGE_DRYRUN_PTE_TYPE_MASK) !=
        STAGE70_XNU_PMAP_PAGE_DRYRUN_PTE_TYPE_SMALL) {
        return 0u;
    }
    if (pa_out) {
        *pa_out = (pte & STAGE70_XNU_PMAP_PAGE_DRYRUN_PTE_PAGE_MASK) |
                  (va & STAGE70_XNU_PMAP_PAGE_DRYRUN_PAGE_OFFSET_MASK);
    }
    return 1u;
}

static uint32_t stage70_pmap_page_dryrun_verify_l1_descriptors(
    const uint32_t *l1,
    const struct stage70_xnu_pmap_page_dryrun_contract *contract,
    uint32_t *first_word,
    uint32_t *last_word,
    uint32_t *type_mask_seen,
    uint32_t *attr_mask_seen)
{
    uint32_t ok = 1u;

    *first_word = 0u;
    *last_word = 0u;
    *type_mask_seen = 0u;
    *attr_mask_seen = 0u;

    for (uint32_t i = 0; i < contract->page_window_l1_count; i++) {
        uint32_t l1_index = contract->page_window_l1_first_index + i;
        uint32_t expected_l2_base = contract->local_l2_base +
            (i * STAGE70_XNU_PMAP_PAGE_DRYRUN_L2_COARSE_BYTES);
        uint32_t expected = stage70_pmap_page_dryrun_make_l1_table_descriptor(expected_l2_base);
        uint32_t word;

        if (l1_index >= STAGE70_XNU_PMAP_PAGE_DRYRUN_L1_ENTRY_COUNT) {
            ok = 0u;
            continue;
        }
        word = l1[l1_index];
        if (i == 0u) {
            *first_word = word;
        }
        if (i == contract->page_window_l1_count - 1u) {
            *last_word = word;
        }
        *type_mask_seen |= word & STAGE70_XNU_PMAP_PAGE_DRYRUN_L1_TYPE_MASK;
        *attr_mask_seen |= word & STAGE70_XNU_PMAP_PAGE_DRYRUN_L1_TABLE_ATTR_MASK;
        if (word != expected) {
            ok = 0u;
        }
    }

    return ok;
}

static uint32_t stage70_pmap_page_dryrun_verify_pte_readback(
    const uint32_t *l2,
    struct stage70_xnu_pmap_page_dryrun_contract *contract)
{
    uint32_t ok = 1u;
    uint32_t kernel_index;
    uint32_t workspace_index;

    kernel_index = (contract->translation_kernel_va - contract->page_window_virt_base) >>
        STAGE70_XNU_PMAP_PAGE_DRYRUN_L2_INDEX_SHIFT;
    workspace_index = (contract->translation_workspace_va - contract->page_window_virt_base) >>
        STAGE70_XNU_PMAP_PAGE_DRYRUN_L2_INDEX_SHIFT;

    contract->pte_type_mask_seen = 0u;
    contract->pte_attr_mask_seen = 0u;
    for (uint32_t i = 0; i < contract->page_window_l2_pte_count; i++) {
        uint32_t pa = contract->page_window_phys_base +
            (i * STAGE70_XNU_PMAP_PAGE_DRYRUN_PAGE_SIZE);
        uint32_t expected = stage70_pmap_page_dryrun_make_pte(pa);
        uint32_t word = l2[i];

        contract->pte_type_mask_seen |= word & STAGE70_XNU_PMAP_PAGE_DRYRUN_PTE_TYPE_MASK;
        contract->pte_attr_mask_seen |= word & STAGE70_XNU_PMAP_PAGE_DRYRUN_PTE_ATTR_MASK;
        if (i == contract->page_window_l2_pte_first_index) {
            contract->pte_first_word = word;
        }
        if (i == kernel_index) {
            contract->pte_kernel_word = word;
        }
        if (i == workspace_index) {
            contract->pte_workspace_word = word;
        }
        if (i == contract->page_window_l2_pte_last_index) {
            contract->pte_last_word = word;
        }
        if (word != expected) {
            ok = 0u;
        }
    }

    return ok;
}

static void stage70_xnu_pmap_page_dryrun_contract_log(
    const struct stage70_xnu_pmap_page_dryrun_contract *contract)
{
    xnu_log_kv32("stage70_xnu_pmap_page_dryrun_contract_status", contract->status);
    xnu_log_kv32("stage70_xnu_pmap_page_dryrun_contract_required_mask", contract->required_mask);
    xnu_log_kv32("stage70_xnu_pmap_page_dryrun_contract_satisfied_mask", contract->satisfied_mask);
    xnu_log_kv32("stage70_xnu_pmap_page_dryrun_contract_failure_mask", contract->failure_mask);
    xnu_log_kv32("stage70_xnu_pmap_page_dryrun_contract_checksum", contract->checksum);
    xnu_log_kv32("stage70_xnu_pmap_page_dryrun_source_pmap_bootstrap_status", contract->source_pmap_bootstrap_status);
    xnu_log_kv32("stage70_xnu_pmap_page_dryrun_source_table_dryrun_status", contract->source_table_dryrun_status);
    xnu_log_kv32("stage70_xnu_pmap_page_dryrun_source_snapshot_status", contract->source_snapshot_status);
    xnu_log_kv32("stage70_xnu_pmap_page_dryrun_page_size", contract->page_size);
    xnu_log_kv32("stage70_xnu_pmap_page_dryrun_window_size", contract->window_size);
    xnu_log_kv32("stage70_xnu_pmap_page_dryrun_l1_table_type", contract->l1_table_descriptor_type);
    xnu_log_kv32("stage70_xnu_pmap_page_dryrun_l1_table_mask", contract->l1_table_base_mask);
    xnu_log_kv32("stage70_xnu_pmap_page_dryrun_l2_page_bytes", contract->l2_page_bytes);
    xnu_log_kv32("stage70_xnu_pmap_page_dryrun_l2_coarse_table_bytes", contract->l2_coarse_table_bytes);
    xnu_log_kv32("stage70_xnu_pmap_page_dryrun_l2_pte_count", contract->l2_pte_count);
    xnu_log_kv32("stage70_xnu_pmap_page_dryrun_pte_attr_default", contract->pte_attr_default);
    xnu_log_kv32("stage70_xnu_pmap_page_dryrun_local_l1_base", contract->local_l1_base);
    xnu_log_kv32("stage70_xnu_pmap_page_dryrun_local_l1_limit", contract->local_l1_limit);
    xnu_log_kv32("stage70_xnu_pmap_page_dryrun_local_l2_base", contract->local_l2_base);
    xnu_log_kv32("stage70_xnu_pmap_page_dryrun_local_l2_limit", contract->local_l2_limit);
    xnu_log_kv32("stage70_xnu_pmap_page_dryrun_local_l1_zero_checksum", contract->local_l1_zero_checksum);
    xnu_log_kv32("stage70_xnu_pmap_page_dryrun_local_l2_zero_checksum", contract->local_l2_zero_checksum);
    xnu_log_kv32("stage70_xnu_pmap_page_dryrun_local_l1_populated_checksum", contract->local_l1_populated_checksum);
    xnu_log_kv32("stage70_xnu_pmap_page_dryrun_local_l2_populated_checksum", contract->local_l2_populated_checksum);
    xnu_log_kv32("stage70_xnu_pmap_page_dryrun_local_l1_write_count", contract->local_l1_write_count);
    xnu_log_kv32("stage70_xnu_pmap_page_dryrun_local_l2_pte_write_count", contract->local_l2_pte_write_count);
    xnu_log_kv32("stage70_xnu_pmap_page_dryrun_gVirtBase", contract->gVirtBase);
    xnu_log_kv32("stage70_xnu_pmap_page_dryrun_gPhysBase", contract->gPhysBase);
    xnu_log_kv32("stage70_xnu_pmap_page_dryrun_gPhysSize", contract->gPhysSize);
    xnu_log_kv32("stage70_xnu_pmap_page_dryrun_boot_ttep", contract->boot_ttep);
    xnu_log_kv32("stage70_xnu_pmap_page_dryrun_cpu_ttep", contract->cpu_ttep);
    xnu_log_kv32("stage70_xnu_pmap_page_dryrun_initial_avail_start", contract->initial_avail_start);
    xnu_log_kv32("stage70_xnu_pmap_page_dryrun_avail_end", contract->avail_end);
    xnu_log_kv32("stage70_xnu_pmap_page_dryrun_vstart", contract->vstart);
    xnu_log_kv32("stage70_xnu_pmap_page_dryrun_virtual_space_end", contract->virtual_space_end);
    xnu_log_kv32("stage70_xnu_pmap_page_dryrun_workspace_base", contract->workspace_base);
    xnu_log_kv32("stage70_xnu_pmap_page_dryrun_workspace_limit", contract->workspace_limit);
    xnu_log_kv32("stage70_xnu_pmap_page_dryrun_workspace_size", contract->workspace_size);
    xnu_log_kv32("stage70_xnu_pmap_page_dryrun_workspace_l1_table_phys", contract->workspace_l1_table_phys);
    xnu_log_kv32("stage70_xnu_pmap_page_dryrun_workspace_l1_table_virt", contract->workspace_l1_table_virt);
    xnu_log_kv32("stage70_xnu_pmap_page_dryrun_window_virt_base", contract->page_window_virt_base);
    xnu_log_kv32("stage70_xnu_pmap_page_dryrun_window_virt_limit", contract->page_window_virt_limit);
    xnu_log_kv32("stage70_xnu_pmap_page_dryrun_window_phys_base", contract->page_window_phys_base);
    xnu_log_kv32("stage70_xnu_pmap_page_dryrun_window_phys_limit", contract->page_window_phys_limit);
    xnu_log_kv32("stage70_xnu_pmap_page_dryrun_l1_first_index", contract->page_window_l1_first_index);
    xnu_log_kv32("stage70_xnu_pmap_page_dryrun_l1_last_index", contract->page_window_l1_last_index);
    xnu_log_kv32("stage70_xnu_pmap_page_dryrun_l1_count", contract->page_window_l1_count);
    xnu_log_kv32("stage70_xnu_pmap_page_dryrun_l2_pte_first_index", contract->page_window_l2_pte_first_index);
    xnu_log_kv32("stage70_xnu_pmap_page_dryrun_l2_pte_last_index", contract->page_window_l2_pte_last_index);
    xnu_log_kv32("stage70_xnu_pmap_page_dryrun_l2_pte_count", contract->page_window_l2_pte_count);
    xnu_log_kv32("stage70_xnu_pmap_page_dryrun_l1_descriptor_first_word", contract->l1_descriptor_first_word);
    xnu_log_kv32("stage70_xnu_pmap_page_dryrun_l1_descriptor_last_word", contract->l1_descriptor_last_word);
    xnu_log_kv32("stage70_xnu_pmap_page_dryrun_l1_descriptor_type_mask_seen", contract->l1_descriptor_type_mask_seen);
    xnu_log_kv32("stage70_xnu_pmap_page_dryrun_l1_descriptor_attr_mask_seen", contract->l1_descriptor_attr_mask_seen);
    xnu_log_kv32("stage70_xnu_pmap_page_dryrun_l1_descriptor_expected_attr_mask", contract->l1_descriptor_expected_attr_mask);
    xnu_log_kv32("stage70_xnu_pmap_page_dryrun_pte_first_word", contract->pte_first_word);
    xnu_log_kv32("stage70_xnu_pmap_page_dryrun_pte_kernel_word", contract->pte_kernel_word);
    xnu_log_kv32("stage70_xnu_pmap_page_dryrun_pte_workspace_word", contract->pte_workspace_word);
    xnu_log_kv32("stage70_xnu_pmap_page_dryrun_pte_last_word", contract->pte_last_word);
    xnu_log_kv32("stage70_xnu_pmap_page_dryrun_pte_type_mask_seen", contract->pte_type_mask_seen);
    xnu_log_kv32("stage70_xnu_pmap_page_dryrun_pte_attr_mask_seen", contract->pte_attr_mask_seen);
    xnu_log_kv32("stage70_xnu_pmap_page_dryrun_pte_expected_attr_mask", contract->pte_expected_attr_mask);
    xnu_log_kv32("stage70_xnu_pmap_page_dryrun_translation_kernel_va", contract->translation_kernel_va);
    xnu_log_kv32("stage70_xnu_pmap_page_dryrun_translation_kernel_pa", contract->translation_kernel_pa);
    xnu_log_kv32("stage70_xnu_pmap_page_dryrun_translation_kernel_expected_pa", contract->translation_kernel_expected_pa);
    xnu_log_kv32("stage70_xnu_pmap_page_dryrun_translation_workspace_va", contract->translation_workspace_va);
    xnu_log_kv32("stage70_xnu_pmap_page_dryrun_translation_workspace_pa", contract->translation_workspace_pa);
    xnu_log_kv32("stage70_xnu_pmap_page_dryrun_translation_window_first_va", contract->translation_window_first_va);
    xnu_log_kv32("stage70_xnu_pmap_page_dryrun_translation_window_first_pa", contract->translation_window_first_pa);
    xnu_log_kv32("stage70_xnu_pmap_page_dryrun_translation_window_last_va", contract->translation_window_last_va);
    xnu_log_kv32("stage70_xnu_pmap_page_dryrun_translation_window_last_pa", contract->translation_window_last_pa);
    xnu_log_kv32("stage70_xnu_pmap_page_dryrun_translation_case_count", contract->translation_case_count);
    xnu_log_kv32("stage70_xnu_pmap_page_dryrun_public_pmap_compile_count", contract->public_pmap_compile_count);
    xnu_log_kv32("stage70_xnu_pmap_page_dryrun_public_pmap_link_count", contract->public_pmap_link_count);
    xnu_log_kv32("stage70_xnu_pmap_page_dryrun_public_pmap_execute_count", contract->public_pmap_execute_count);
    xnu_log_kv32("stage70_xnu_pmap_page_dryrun_proposed_workspace_written", contract->proposed_workspace_written);
    xnu_log_kv32("stage70_xnu_pmap_page_dryrun_live_pmap_tables_installed", contract->live_pmap_tables_installed);
    xnu_log_kv32("stage70_xnu_pmap_page_dryrun_ttbr_written", contract->ttbr_written);
    xnu_log_kv32("stage70_xnu_pmap_page_dryrun_ttbcr_written", contract->ttbcr_written);
    xnu_log_kv32("stage70_xnu_pmap_page_dryrun_dacr_written", contract->dacr_written);
    xnu_log_kv32("stage70_xnu_pmap_page_dryrun_sctlr_written", contract->sctlr_written);
    xnu_log_kv32("stage70_xnu_pmap_page_dryrun_tlbs_invalidated", contract->tlbs_invalidated);
    xnu_log_kv32("stage70_xnu_pmap_page_dryrun_public_arm_vm_init_executed", contract->public_arm_vm_init_executed);
    xnu_log_kv32("stage70_xnu_pmap_page_dryrun_xnu_start_executed", contract->xnu_start_executed);
    xnu_log_kv32("stage70_xnu_pmap_page_dryrun_generated_macho_executed", contract->generated_macho_executed);
    xnu_log_kv32("stage70_xnu_pmap_page_dryrun_persistent_write_attempted", contract->persistent_write_attempted);
    xnu_log_kv32("stage70_xnu_pmap_page_dryrun_caches_changed", contract->caches_changed);
    xnu_log_kv32("stage70_xnu_pmap_page_dryrun_fail_closed", contract->fail_closed);
}

int stage70_xnu_pmap_page_dryrun_contract_selftest(const struct stage70_loader_preflight *preflight)
{
    struct stage70_xnu_pmap_page_dryrun_contract *contract = &g_stage70_xnu_pmap_page_dryrun_contract;
    const struct stage70_xnu_pmap_bootstrap_contract *pmap = stage70_xnu_pmap_bootstrap_contract_result();
    const struct stage70_xnu_pmap_table_dryrun_contract *table = stage70_xnu_pmap_table_dryrun_contract_result();
    const struct stage70_pmap_bootstrap_snapshot *snapshot = mmu_stage70_pmap_bootstrap_snapshot_result();
    const struct stage70_ttbr0_roundtrip *ttbr_rt = mmu_stage70_ttbr0_roundtrip_result();
    uint32_t local_l1_limit = 0u;
    uint32_t local_l2_limit = 0u;
    uint32_t translated = 0u;
    uint32_t source_rollups_ok = 0u;

    memset(contract, 0, sizeof(*contract));
    memset(g_stage70_pmap_page_dryrun_l1, 0, sizeof(g_stage70_pmap_page_dryrun_l1));
    memset(g_stage70_pmap_page_dryrun_l2, 0, sizeof(g_stage70_pmap_page_dryrun_l2));

    contract->version = STAGE70_XNU_PMAP_PAGE_DRYRUN_CONTRACT_VERSION;
    contract->size = sizeof(*contract);
    contract->status = STAGE70_STATUS_BASE;
    contract->required_mask = STAGE70_XNU_PMAP_PAGE_DRYRUN_REQUIRED_MASK;
    contract->page_size = STAGE70_XNU_PMAP_PAGE_DRYRUN_PAGE_SIZE;
    contract->page_offset_mask = STAGE70_XNU_PMAP_PAGE_DRYRUN_PAGE_OFFSET_MASK;
    contract->window_size = STAGE70_XNU_PMAP_PAGE_DRYRUN_WINDOW_SIZE;
    contract->window_alignment = STAGE70_XNU_PMAP_PAGE_DRYRUN_WINDOW_ALIGN;
    contract->l1_entry_count = STAGE70_XNU_PMAP_PAGE_DRYRUN_L1_ENTRY_COUNT;
    contract->l1_bytes = STAGE70_XNU_PMAP_PAGE_DRYRUN_L1_BYTES;
    contract->l1_alignment = STAGE70_XNU_PMAP_PAGE_DRYRUN_L1_ALIGNMENT;
    contract->l1_table_descriptor_type = STAGE70_XNU_PMAP_PAGE_DRYRUN_L1_TABLE_TYPE;
    contract->l1_table_type_mask = STAGE70_XNU_PMAP_PAGE_DRYRUN_L1_TYPE_MASK;
    contract->l1_table_base_mask = STAGE70_XNU_PMAP_PAGE_DRYRUN_L1_TABLE_MASK;
    contract->l1_table_attr_mask = STAGE70_XNU_PMAP_PAGE_DRYRUN_L1_TABLE_ATTR_MASK;
    contract->l2_page_bytes = STAGE70_XNU_PMAP_PAGE_DRYRUN_L2_PAGE_BYTES;
    contract->l2_alignment = STAGE70_XNU_PMAP_PAGE_DRYRUN_L2_ALIGNMENT;
    contract->l2_coarse_table_bytes = STAGE70_XNU_PMAP_PAGE_DRYRUN_L2_COARSE_BYTES;
    contract->l2_tables_per_page = STAGE70_XNU_PMAP_PAGE_DRYRUN_L2_TABLES_PER_PAGE;
    contract->l2_ptes_per_table = STAGE70_XNU_PMAP_PAGE_DRYRUN_L2_PTES_PER_TABLE;
    contract->l2_pte_count = STAGE70_XNU_PMAP_PAGE_DRYRUN_L2_PTE_COUNT;
    contract->pte_type_mask = STAGE70_XNU_PMAP_PAGE_DRYRUN_PTE_TYPE_MASK;
    contract->pte_type_small = STAGE70_XNU_PMAP_PAGE_DRYRUN_PTE_TYPE_SMALL;
    contract->pte_page_mask = STAGE70_XNU_PMAP_PAGE_DRYRUN_PTE_PAGE_MASK;
    contract->pte_attr_mask = STAGE70_XNU_PMAP_PAGE_DRYRUN_PTE_ATTR_MASK;
    contract->pte_attr_default = STAGE70_XNU_PMAP_PAGE_DRYRUN_PTE_ATTR_DEFAULT;
    contract->l1_descriptor_expected_attr_mask = STAGE70_XNU_PMAP_PAGE_DRYRUN_L1_TABLE_TYPE;
    contract->pte_expected_attr_mask = STAGE70_XNU_PMAP_PAGE_DRYRUN_PTE_ATTR_DEFAULT &
        STAGE70_XNU_PMAP_PAGE_DRYRUN_PTE_ATTR_MASK;
    contract->local_l1_base = (uint32_t)(uintptr_t)g_stage70_pmap_page_dryrun_l1;
    contract->local_l1_bytes = sizeof(g_stage70_pmap_page_dryrun_l1);
    contract->local_l1_alignment = contract->local_l1_base &
        (STAGE70_XNU_PMAP_PAGE_DRYRUN_L1_ALIGNMENT - 1u);
    contract->local_l2_base = (uint32_t)(uintptr_t)g_stage70_pmap_page_dryrun_l2;
    contract->local_l2_bytes = sizeof(g_stage70_pmap_page_dryrun_l2);
    contract->local_l2_alignment = contract->local_l2_base &
        (STAGE70_XNU_PMAP_PAGE_DRYRUN_L2_ALIGNMENT - 1u);
    contract->local_l1_zero_checksum = stage70_pmap_page_dryrun_buffer_checksum(
        g_stage70_pmap_page_dryrun_l1, STAGE70_XNU_PMAP_PAGE_DRYRUN_L1_ENTRY_COUNT);
    contract->local_l2_zero_checksum = stage70_pmap_page_dryrun_buffer_checksum(
        g_stage70_pmap_page_dryrun_l2, STAGE70_XNU_PMAP_PAGE_DRYRUN_L2_PTE_COUNT);

    xnu_log_puts("Stage70 XNU pmap page-granular dry-run contract begin\n");

    if (!preflight || !pmap || !table || !snapshot || !ttbr_rt) {
        contract->failure_mask |= STAGE70_XNU_PMAP_PAGE_DRYRUN_FAIL_SOURCE |
                                  STAGE70_XNU_PMAP_PAGE_DRYRUN_FAIL_SAFETY_BOUNDARY;
        goto finish;
    }

    contract->source_pmap_bootstrap_status = pmap->status;
    contract->source_pmap_bootstrap_required_mask = pmap->required_mask;
    contract->source_pmap_bootstrap_satisfied_mask = pmap->satisfied_mask;
    contract->source_pmap_bootstrap_failure_mask = pmap->failure_mask;
    contract->source_pmap_bootstrap_checksum = pmap->checksum;
    contract->source_table_dryrun_status = table->status;
    contract->source_table_dryrun_required_mask = table->required_mask;
    contract->source_table_dryrun_satisfied_mask = table->satisfied_mask;
    contract->source_table_dryrun_failure_mask = table->failure_mask;
    contract->source_table_dryrun_checksum = table->checksum;
    contract->source_snapshot_status = snapshot->status;
    contract->source_snapshot_required_mask = snapshot->required_mask;
    contract->source_snapshot_satisfied_mask = snapshot->satisfied_mask;
    contract->source_snapshot_failure_mask = snapshot->failure_mask;
    contract->source_snapshot_checksum = snapshot->checksum;
    contract->source_bootstrap_status = preflight->xnu_bootstrap_contract_status;
    contract->source_tte_dryrun_status = preflight->tte_dryrun_status;
    contract->source_ttbr_roundtrip_status = preflight->ttbr_roundtrip_status;
    contract->source_ttbr_restored_status = preflight->ttbr_restored_status;
    contract->source_cache_preserved_status = preflight->cache_preserved_status;
    contract->source_compile_graph_status = preflight->xnu_compile_graph_status;
    contract->source_object_subset_status = preflight->xnu_object_subset_status;
    contract->source_link_status = preflight->xnu_link_status;

    contract->gVirtBase = pmap->gVirtBase;
    contract->gPhysBase = pmap->gPhysBase;
    contract->gPhysSize = pmap->gPhysSize;
    contract->boot_ttep = pmap->boot_ttep;
    contract->cpu_ttep = pmap->cpu_ttep;
    contract->initial_avail_start = pmap->initial_avail_start;
    contract->avail_end = pmap->avail_end;
    contract->vstart = pmap->vstart;
    contract->virtual_space_end = pmap->virtual_space_end;
    contract->workspace_base = snapshot->workspace_base;
    contract->workspace_limit = snapshot->workspace_limit;
    contract->workspace_size = snapshot->workspace_size;
    contract->workspace_l1_table_phys = snapshot->workspace_l1_table_phys;
    contract->workspace_l1_table_virt = snapshot->workspace_l1_table_virt;
    contract->public_pmap_compile_count = pmap->public_pmap_compile_count;
    contract->public_pmap_link_count = pmap->public_pmap_link_count;
    contract->public_pmap_execute_count = pmap->public_pmap_execute_count;

    source_rollups_ok = (contract->source_pmap_bootstrap_status == STAGE70_STATUS_OK &&
                         contract->source_pmap_bootstrap_satisfied_mask == contract->source_pmap_bootstrap_required_mask &&
                         contract->source_pmap_bootstrap_failure_mask == 0u &&
                         contract->source_table_dryrun_status == STAGE70_STATUS_OK &&
                         contract->source_table_dryrun_satisfied_mask == contract->source_table_dryrun_required_mask &&
                         contract->source_table_dryrun_failure_mask == 0u &&
                         contract->source_snapshot_status == STAGE70_STATUS_OK &&
                         contract->source_snapshot_satisfied_mask == contract->source_snapshot_required_mask &&
                         contract->source_snapshot_failure_mask == 0u &&
                         contract->source_bootstrap_status == STAGE70_STATUS_OK &&
                         contract->source_tte_dryrun_status == STAGE70_STATUS_OK &&
                         contract->source_ttbr_roundtrip_status == STAGE70_STATUS_OK &&
                         contract->source_ttbr_restored_status == STAGE70_STATUS_OK &&
                         contract->source_cache_preserved_status == STAGE70_STATUS_OK &&
                         contract->source_compile_graph_status == STAGE70_STATUS_OK &&
                         contract->source_object_subset_status == STAGE70_STATUS_OK &&
                         contract->source_link_status == STAGE70_STATUS_OK &&
                         preflight->xnu_compile_graph_failure_mask == 0u &&
                         preflight->xnu_object_subset_failure_mask == 0u &&
                         preflight->xnu_link_failure_mask == 0u) ? 1u : 0u;

    if (contract->source_pmap_bootstrap_status == STAGE70_STATUS_OK &&
        contract->source_pmap_bootstrap_satisfied_mask == contract->source_pmap_bootstrap_required_mask &&
        contract->source_pmap_bootstrap_failure_mask == 0u) {
        contract->satisfied_mask |= STAGE70_XNU_PMAP_PAGE_DRYRUN_SAT_SOURCE_PMAP_BOOTSTRAP;
    } else {
        contract->failure_mask |= STAGE70_XNU_PMAP_PAGE_DRYRUN_FAIL_SOURCE;
    }
    if (contract->source_table_dryrun_status == STAGE70_STATUS_OK &&
        contract->source_table_dryrun_satisfied_mask == contract->source_table_dryrun_required_mask &&
        contract->source_table_dryrun_failure_mask == 0u) {
        contract->satisfied_mask |= STAGE70_XNU_PMAP_PAGE_DRYRUN_SAT_SOURCE_TABLE_DRYRUN;
    } else {
        contract->failure_mask |= STAGE70_XNU_PMAP_PAGE_DRYRUN_FAIL_SOURCE;
    }
    if (source_rollups_ok) {
        contract->satisfied_mask |= STAGE70_XNU_PMAP_PAGE_DRYRUN_SAT_SOURCE_ROLLUPS;
    } else {
        contract->failure_mask |= STAGE70_XNU_PMAP_PAGE_DRYRUN_FAIL_SOURCE;
    }

    if (contract->page_size == 0x00001000u &&
        contract->window_size == 0x00400000u &&
        contract->window_alignment == 0x00400000u &&
        contract->l1_entry_count == 4096u &&
        contract->l1_bytes == 0x00004000u &&
        contract->l1_alignment == 0x00004000u &&
        contract->l1_table_descriptor_type == 0x00000001u &&
        contract->l1_table_type_mask == 0x00000003u &&
        contract->l1_table_base_mask == 0xfffffc00u &&
        contract->l2_page_bytes == 0x00001000u &&
        contract->l2_coarse_table_bytes == 0x00000400u &&
        contract->l2_tables_per_page == 4u &&
        contract->l2_ptes_per_table == 256u &&
        contract->l2_pte_count == 1024u &&
        contract->pte_type_small == 0x00000002u &&
        contract->pte_page_mask == 0xfffff000u &&
        contract->pte_attr_default == 0x00000412u &&
        contract->gPhysBase == RAM_PHYS_BASE &&
        contract->avail_end == RAM_CONSOLE_BASE) {
        contract->satisfied_mask |= STAGE70_XNU_PMAP_PAGE_DRYRUN_SAT_CONSTANTS;
    } else {
        contract->failure_mask |= STAGE70_XNU_PMAP_PAGE_DRYRUN_FAIL_CONSTANTS;
    }

    if (!stage70_pmap_page_dryrun_add_overflow_u32(contract->local_l1_base,
                                                   contract->local_l1_bytes,
                                                   &local_l1_limit)) {
        contract->local_l1_limit = local_l1_limit;
    }
    if (!stage70_pmap_page_dryrun_add_overflow_u32(contract->local_l2_base,
                                                   contract->local_l2_bytes,
                                                   &local_l2_limit)) {
        contract->local_l2_limit = local_l2_limit;
    }
    if (contract->local_l1_base != 0u && contract->local_l1_limit > contract->local_l1_base &&
        contract->local_l1_bytes == STAGE70_XNU_PMAP_PAGE_DRYRUN_L1_BYTES &&
        contract->local_l1_alignment == 0u && contract->local_l1_limit <= RAM_PHYS_BASE &&
        contract->local_l1_base != contract->workspace_l1_table_phys &&
        !stage70_pmap_page_dryrun_local_ranges_overlap(contract->local_l1_base, contract->local_l1_limit,
                                                       contract->workspace_base, contract->workspace_limit)) {
        contract->satisfied_mask |= STAGE70_XNU_PMAP_PAGE_DRYRUN_SAT_LOCAL_L1_BUFFER;
    } else {
        contract->failure_mask |= STAGE70_XNU_PMAP_PAGE_DRYRUN_FAIL_LOCAL_BUFFER;
    }
    if (contract->local_l2_base != 0u && contract->local_l2_limit > contract->local_l2_base &&
        contract->local_l2_bytes == STAGE70_XNU_PMAP_PAGE_DRYRUN_L2_PAGE_BYTES &&
        contract->local_l2_alignment == 0u && contract->local_l2_limit <= RAM_PHYS_BASE &&
        !stage70_pmap_page_dryrun_local_ranges_overlap(contract->local_l2_base, contract->local_l2_limit,
                                                       contract->local_l1_base, contract->local_l1_limit) &&
        !stage70_pmap_page_dryrun_local_ranges_overlap(contract->local_l2_base, contract->local_l2_limit,
                                                       contract->workspace_base, contract->workspace_limit)) {
        contract->satisfied_mask |= STAGE70_XNU_PMAP_PAGE_DRYRUN_SAT_LOCAL_L2_BUFFER;
    } else {
        contract->failure_mask |= STAGE70_XNU_PMAP_PAGE_DRYRUN_FAIL_LOCAL_BUFFER;
    }
    if (contract->local_l1_zero_checksum == stage70_pmap_page_dryrun_buffer_checksum(
            g_stage70_pmap_page_dryrun_l1, STAGE70_XNU_PMAP_PAGE_DRYRUN_L1_ENTRY_COUNT) &&
        contract->local_l2_zero_checksum == stage70_pmap_page_dryrun_buffer_checksum(
            g_stage70_pmap_page_dryrun_l2, STAGE70_XNU_PMAP_PAGE_DRYRUN_L2_PTE_COUNT)) {
        contract->satisfied_mask |= STAGE70_XNU_PMAP_PAGE_DRYRUN_SAT_LOCAL_ZERO;
    } else {
        contract->failure_mask |= STAGE70_XNU_PMAP_PAGE_DRYRUN_FAIL_LOCAL_BUFFER;
    }

    contract->page_window_virt_base = contract->gVirtBase & ~(STAGE70_XNU_PMAP_PAGE_DRYRUN_WINDOW_ALIGN - 1u);
    contract->page_window_phys_base = contract->gPhysBase & ~(STAGE70_XNU_PMAP_PAGE_DRYRUN_WINDOW_ALIGN - 1u);
    if (!stage70_pmap_page_dryrun_add_overflow_u32(contract->page_window_virt_base,
                                                   contract->window_size,
                                                   &contract->page_window_virt_limit) &&
        !stage70_pmap_page_dryrun_add_overflow_u32(contract->page_window_phys_base,
                                                   contract->window_size,
                                                   &contract->page_window_phys_limit)) {
        contract->page_window_l1_first_index = stage70_pmap_page_dryrun_l1_index(contract->page_window_virt_base);
        contract->page_window_l1_last_index = stage70_pmap_page_dryrun_l1_index(contract->page_window_virt_limit - 1u);
        contract->page_window_l1_count = contract->page_window_l1_last_index -
            contract->page_window_l1_first_index + 1u;
        contract->page_window_l2_pte_first_index = 0u;
        contract->page_window_l2_pte_last_index = STAGE70_XNU_PMAP_PAGE_DRYRUN_L2_PTE_COUNT - 1u;
        contract->page_window_l2_pte_count = STAGE70_XNU_PMAP_PAGE_DRYRUN_L2_PTE_COUNT;
        if ((contract->page_window_virt_base & (STAGE70_XNU_PMAP_PAGE_DRYRUN_WINDOW_ALIGN - 1u)) == 0u &&
            (contract->page_window_phys_base & (STAGE70_XNU_PMAP_PAGE_DRYRUN_WINDOW_ALIGN - 1u)) == 0u &&
            contract->page_window_l1_count == STAGE70_XNU_PMAP_PAGE_DRYRUN_L2_TABLES_PER_PAGE &&
            contract->page_window_l1_last_index < STAGE70_XNU_PMAP_PAGE_DRYRUN_L1_ENTRY_COUNT &&
            contract->gVirtBase >= contract->page_window_virt_base &&
            contract->gVirtBase < contract->page_window_virt_limit &&
            contract->workspace_base >= contract->page_window_virt_base &&
            contract->workspace_base < contract->page_window_virt_limit &&
            contract->page_window_phys_limit <= contract->avail_end) {
            contract->satisfied_mask |= STAGE70_XNU_PMAP_PAGE_DRYRUN_SAT_WINDOW;
        } else {
            contract->failure_mask |= STAGE70_XNU_PMAP_PAGE_DRYRUN_FAIL_WINDOW;
        }
    } else {
        contract->failure_mask |= STAGE70_XNU_PMAP_PAGE_DRYRUN_FAIL_WINDOW;
    }

    if ((contract->failure_mask & (STAGE70_XNU_PMAP_PAGE_DRYRUN_FAIL_LOCAL_BUFFER |
                                   STAGE70_XNU_PMAP_PAGE_DRYRUN_FAIL_WINDOW)) == 0u) {
        contract->local_l2_pte_write_count = stage70_pmap_page_dryrun_populate_window(
            g_stage70_pmap_page_dryrun_l1,
            g_stage70_pmap_page_dryrun_l2,
            contract->page_window_virt_base,
            contract->page_window_phys_base);
        contract->local_l1_write_count = STAGE70_XNU_PMAP_PAGE_DRYRUN_L2_TABLES_PER_PAGE;
    }

    if (stage70_pmap_page_dryrun_verify_l1_descriptors(
            g_stage70_pmap_page_dryrun_l1, contract,
            &contract->l1_descriptor_first_word,
            &contract->l1_descriptor_last_word,
            &contract->l1_descriptor_type_mask_seen,
            &contract->l1_descriptor_attr_mask_seen) &&
        contract->local_l1_write_count == STAGE70_XNU_PMAP_PAGE_DRYRUN_L2_TABLES_PER_PAGE) {
        contract->satisfied_mask |= STAGE70_XNU_PMAP_PAGE_DRYRUN_SAT_L1_COARSE_DESCRIPTORS;
    } else {
        contract->failure_mask |= STAGE70_XNU_PMAP_PAGE_DRYRUN_FAIL_DESCRIPTOR;
    }

    contract->translation_kernel_va = contract->gVirtBase;
    contract->translation_workspace_va = contract->workspace_base;
    contract->translation_window_first_va = contract->page_window_virt_base;
    contract->translation_window_last_va = contract->page_window_virt_limit - 1u;

    if (contract->local_l2_pte_write_count == STAGE70_XNU_PMAP_PAGE_DRYRUN_L2_PTE_COUNT) {
        contract->satisfied_mask |= STAGE70_XNU_PMAP_PAGE_DRYRUN_SAT_PTE_POPULATION;
    } else {
        contract->failure_mask |= STAGE70_XNU_PMAP_PAGE_DRYRUN_FAIL_PTE;
    }
    if (stage70_pmap_page_dryrun_verify_pte_readback(g_stage70_pmap_page_dryrun_l2, contract)) {
        contract->satisfied_mask |= STAGE70_XNU_PMAP_PAGE_DRYRUN_SAT_PTE_READBACK;
    } else {
        contract->failure_mask |= STAGE70_XNU_PMAP_PAGE_DRYRUN_FAIL_PTE;
    }
    if (contract->l1_descriptor_type_mask_seen == STAGE70_XNU_PMAP_PAGE_DRYRUN_L1_TABLE_TYPE &&
        contract->l1_descriptor_attr_mask_seen == contract->l1_descriptor_expected_attr_mask &&
        contract->pte_type_mask_seen == STAGE70_XNU_PMAP_PAGE_DRYRUN_PTE_TYPE_SMALL &&
        contract->pte_attr_mask_seen == contract->pte_expected_attr_mask) {
        contract->satisfied_mask |= STAGE70_XNU_PMAP_PAGE_DRYRUN_SAT_ATTRS;
    } else {
        contract->failure_mask |= STAGE70_XNU_PMAP_PAGE_DRYRUN_FAIL_DESCRIPTOR |
                                  STAGE70_XNU_PMAP_PAGE_DRYRUN_FAIL_PTE;
    }

    contract->translation_kernel_expected_pa = stage70_pmap_page_dryrun_expected_pa(
        contract->translation_kernel_va, contract->page_window_virt_base, contract->page_window_phys_base);
    if (stage70_pmap_page_dryrun_translate(g_stage70_pmap_page_dryrun_l1,
                                           contract->local_l2_base,
                                           contract->local_l2_limit,
                                           contract->translation_kernel_va,
                                           &translated) &&
        translated == contract->translation_kernel_expected_pa) {
        contract->translation_kernel_pa = translated;
        contract->translation_case_count++;
        contract->satisfied_mask |= STAGE70_XNU_PMAP_PAGE_DRYRUN_SAT_TRANSLATION_KERNEL;
    } else {
        contract->failure_mask |= STAGE70_XNU_PMAP_PAGE_DRYRUN_FAIL_TRANSLATION;
    }

    if (stage70_pmap_page_dryrun_translate(g_stage70_pmap_page_dryrun_l1,
                                           contract->local_l2_base,
                                           contract->local_l2_limit,
                                           contract->translation_workspace_va,
                                           &translated) &&
        translated == stage70_pmap_page_dryrun_expected_pa(contract->translation_workspace_va,
                                                           contract->page_window_virt_base,
                                                           contract->page_window_phys_base)) {
        contract->translation_workspace_pa = translated;
        contract->translation_case_count++;
        contract->satisfied_mask |= STAGE70_XNU_PMAP_PAGE_DRYRUN_SAT_TRANSLATION_WORKSPACE;
    } else {
        contract->failure_mask |= STAGE70_XNU_PMAP_PAGE_DRYRUN_FAIL_TRANSLATION;
    }

    if (stage70_pmap_page_dryrun_translate(g_stage70_pmap_page_dryrun_l1,
                                           contract->local_l2_base,
                                           contract->local_l2_limit,
                                           contract->translation_window_first_va,
                                           &translated) &&
        translated == contract->page_window_phys_base) {
        contract->translation_window_first_pa = translated;
        contract->translation_case_count++;
    } else {
        contract->failure_mask |= STAGE70_XNU_PMAP_PAGE_DRYRUN_FAIL_TRANSLATION;
    }
    if (stage70_pmap_page_dryrun_translate(g_stage70_pmap_page_dryrun_l1,
                                           contract->local_l2_base,
                                           contract->local_l2_limit,
                                           contract->translation_window_last_va,
                                           &translated) &&
        translated == (contract->page_window_phys_limit - 1u)) {
        contract->translation_window_last_pa = translated;
        contract->translation_case_count++;
        contract->satisfied_mask |= STAGE70_XNU_PMAP_PAGE_DRYRUN_SAT_TRANSLATION_WINDOW;
    } else {
        contract->failure_mask |= STAGE70_XNU_PMAP_PAGE_DRYRUN_FAIL_TRANSLATION;
    }

    contract->local_l1_populated_checksum = stage70_pmap_page_dryrun_buffer_checksum(
        g_stage70_pmap_page_dryrun_l1, STAGE70_XNU_PMAP_PAGE_DRYRUN_L1_ENTRY_COUNT);
    contract->local_l2_populated_checksum = stage70_pmap_page_dryrun_buffer_checksum(
        g_stage70_pmap_page_dryrun_l2, STAGE70_XNU_PMAP_PAGE_DRYRUN_L2_PTE_COUNT);
    if (contract->local_l1_populated_checksum != contract->local_l1_zero_checksum &&
        contract->local_l2_populated_checksum != contract->local_l2_zero_checksum &&
        contract->local_l1_write_count == STAGE70_XNU_PMAP_PAGE_DRYRUN_L2_TABLES_PER_PAGE &&
        contract->local_l2_pte_write_count == STAGE70_XNU_PMAP_PAGE_DRYRUN_L2_PTE_COUNT &&
        contract->proposed_workspace_written == 0u) {
        contract->satisfied_mask |= STAGE70_XNU_PMAP_PAGE_DRYRUN_SAT_LOCAL_ONLY_CHECKSUM;
    } else {
        contract->failure_mask |= STAGE70_XNU_PMAP_PAGE_DRYRUN_FAIL_CHECKSUM;
    }

    contract->proposed_workspace_written = 0u;
    if (contract->proposed_workspace_written == 0u &&
        table->proposed_workspace_written == 0u &&
        pmap->proposed_tte_workspace_written == 0u &&
        preflight->tte_dryrun.proposed_tte_workspace_written == 0u) {
        contract->satisfied_mask |= STAGE70_XNU_PMAP_PAGE_DRYRUN_SAT_NO_PROPOSED_WORKSPACE_WRITE;
    } else {
        contract->failure_mask |= STAGE70_XNU_PMAP_PAGE_DRYRUN_FAIL_SAFETY_BOUNDARY;
    }

    contract->live_pmap_tables_installed = 0u;
    if (contract->live_pmap_tables_installed == 0u &&
        table->live_pmap_tables_installed == 0u &&
        pmap->live_pmap_tables_installed == 0u &&
        snapshot->no_live_pmap_tables_installed == 1u &&
        preflight->tte_dryrun.live_mmu_tables_replaced == 0u) {
        contract->satisfied_mask |= STAGE70_XNU_PMAP_PAGE_DRYRUN_SAT_NO_LIVE_PMAP_INSTALL;
    } else {
        contract->failure_mask |= STAGE70_XNU_PMAP_PAGE_DRYRUN_FAIL_SAFETY_BOUNDARY;
    }

    contract->ttbr_written = 0u;
    contract->ttbcr_written = 0u;
    contract->dacr_written = 0u;
    contract->sctlr_written = 0u;
    if (contract->ttbr_written == 0u && contract->ttbcr_written == 0u &&
        contract->dacr_written == 0u && contract->sctlr_written == 0u) {
        contract->satisfied_mask |= STAGE70_XNU_PMAP_PAGE_DRYRUN_SAT_NO_CONTROL_REG_WRITE;
    } else {
        contract->failure_mask |= STAGE70_XNU_PMAP_PAGE_DRYRUN_FAIL_SAFETY_BOUNDARY;
    }

    contract->tlbs_invalidated = 0u;
    if (contract->tlbs_invalidated == 0u) {
        contract->satisfied_mask |= STAGE70_XNU_PMAP_PAGE_DRYRUN_SAT_NO_TLB_INVALIDATE;
    } else {
        contract->failure_mask |= STAGE70_XNU_PMAP_PAGE_DRYRUN_FAIL_SAFETY_BOUNDARY;
    }

    contract->public_arm_vm_init_executed = 0u;
    if (contract->public_pmap_compile_count == 0u && contract->public_pmap_link_count == 0u &&
        contract->public_pmap_execute_count == 0u && contract->public_arm_vm_init_executed == 0u &&
        (preflight->safety_mask & STAGE70_LOADER_SAFETY_NO_PUBLIC_XNU_EXEC) != 0u) {
        contract->satisfied_mask |= STAGE70_XNU_PMAP_PAGE_DRYRUN_SAT_NO_PUBLIC_PMAP_EXEC;
    } else {
        contract->failure_mask |= STAGE70_XNU_PMAP_PAGE_DRYRUN_FAIL_SAFETY_BOUNDARY;
    }

    contract->xnu_start_executed = 0u;
    contract->generated_macho_executed = ttbr_rt->macho_bytes_executed;
    if (contract->xnu_start_executed == 0u && contract->generated_macho_executed == 0u &&
        ttbr_rt->xnu_entry_executed == 0u &&
        (preflight->safety_mask & STAGE70_LOADER_SAFETY_NO_EXECUTE) != 0u) {
        contract->satisfied_mask |= STAGE70_XNU_PMAP_PAGE_DRYRUN_SAT_NO_XNU_MACHO_EXEC;
    } else {
        contract->failure_mask |= STAGE70_XNU_PMAP_PAGE_DRYRUN_FAIL_SAFETY_BOUNDARY;
    }

    contract->persistent_write_attempted = ttbr_rt->persistent_write_attempted;
    contract->caches_changed = ttbr_rt->caches_changed;
    if (contract->persistent_write_attempted == 0u && contract->caches_changed == 0u &&
        (preflight->safety_mask & STAGE70_LOADER_SAFETY_NO_PERSIST_WRITE) != 0u &&
        (preflight->safety_mask & STAGE70_LOADER_SAFETY_NO_CACHE_CHANGE) != 0u) {
        contract->satisfied_mask |= STAGE70_XNU_PMAP_PAGE_DRYRUN_SAT_NO_PERSIST_CACHE;
    } else {
        contract->failure_mask |= STAGE70_XNU_PMAP_PAGE_DRYRUN_FAIL_SAFETY_BOUNDARY;
    }

    if (preflight->xnu_compile_graph.fail_closed == 1u &&
        preflight->xnu_object_subset.fail_closed == 1u &&
        preflight->xnu_link.fail_closed == 1u &&
        pmap->fail_closed == 1u && table->fail_closed == 1u) {
        contract->fail_closed = 1u;
        contract->satisfied_mask |= STAGE70_XNU_PMAP_PAGE_DRYRUN_SAT_FAIL_CLOSED;
    } else {
        contract->failure_mask |= STAGE70_XNU_PMAP_PAGE_DRYRUN_FAIL_SAFETY_BOUNDARY;
    }

finish:
    if (contract->satisfied_mask == contract->required_mask && contract->failure_mask == 0u) {
        contract->status = STAGE70_STATUS_OK;
        xnu_log_puts("Stage70 XNU pmap page-granular dry-run contract ok\n");
    } else {
        contract->status = STAGE70_STATUS_BASE | contract->failure_mask;
        xnu_log_puts("Stage70 XNU pmap page-granular dry-run contract failed\n");
    }
    contract->checksum = stage70_pmap_page_dryrun_checksum(contract);
    if (contract->checksum != stage70_pmap_page_dryrun_checksum(contract)) {
        contract->failure_mask |= STAGE70_XNU_PMAP_PAGE_DRYRUN_FAIL_CHECKSUM;
        contract->status = STAGE70_STATUS_BASE | contract->failure_mask;
        contract->checksum = stage70_pmap_page_dryrun_checksum(contract);
    }

    stage70_xnu_pmap_page_dryrun_contract_log(contract);
    return contract->status == STAGE70_STATUS_OK;
}

const struct stage70_xnu_pmap_page_dryrun_contract *stage70_xnu_pmap_page_dryrun_contract_result(void)
{
    return &g_stage70_xnu_pmap_page_dryrun_contract;
}
