#include "stage77.h"

/*
 * Stage77 XNU pmap multi-window page-granular dry-run contract.
 *
 * This is a Stage-owned extension of the prior pmap attribute and
 * page-table arithmetic: it builds three independent 4 MiB coarse/L2 mapping
 * windows in local scratch buffers only.  The windows model a kernel/workspace
 * RAM window, a RAM-console window, and a device/GIC-style window using the
 * exact pmap PTE attribute templates imported from the attr dry-run contract.
 * It does not write the proposed pmap workspace, does not install
 * live pmap tables, does not touch control registers, does not invalidate TLBs,
 * does not change cache policy, and does not execute public VM/pmap code.
 */

static struct stage77_xnu_pmap_multiwindow_dryrun_contract g_stage77_xnu_pmap_multiwindow_dryrun_contract;

static uint32_t g_stage77_pmap_multiwindow_l1[STAGE77_XNU_PMAP_MULTIWINDOW_DRYRUN_L1_ENTRY_COUNT]
    __attribute__((aligned(STAGE77_XNU_PMAP_MULTIWINDOW_DRYRUN_L1_ALIGNMENT)));

static uint32_t g_stage77_pmap_multiwindow_l2_bank[
    STAGE77_XNU_PMAP_MULTIWINDOW_DRYRUN_WINDOW_COUNT * STAGE77_XNU_PMAP_MULTIWINDOW_DRYRUN_L2_PTE_COUNT]
    __attribute__((aligned(STAGE77_XNU_PMAP_MULTIWINDOW_DRYRUN_L2_ALIGNMENT)));

static uint32_t stage77_pmap_multiwindow_dryrun_checksum(
    volatile const struct stage77_xnu_pmap_multiwindow_dryrun_contract *contract)
{
    volatile const uint32_t *words = (volatile const uint32_t *)(uintptr_t)contract;
    uint32_t count = (uint32_t)(offsetof(struct stage77_xnu_pmap_multiwindow_dryrun_contract, checksum) /
                                sizeof(uint32_t));
    uint32_t checksum = 0u;

    for (uint32_t i = 0; i < count; i++) {
        checksum ^= words[i];
    }

    return checksum;
}

static uint32_t stage77_pmap_multiwindow_buffer_checksum(const uint32_t *buf, uint32_t count)
{
    uint32_t checksum = 0u;

    for (uint32_t i = 0; i < count; i++) {
        checksum ^= buf[i] + (i * 0x6d2b79f5u);
    }

    return checksum;
}

static uint32_t stage77_pmap_multiwindow_add_overflow_u32(uint32_t a, uint32_t b, uint32_t *out)
{
    *out = a + b;
    return *out < a;
}

static uint32_t stage77_pmap_multiwindow_local_ranges_overlap(uint32_t a_base, uint32_t a_limit,
                                                              uint32_t b_base, uint32_t b_limit)
{
    if (a_limit <= a_base || b_limit <= b_base) {
        return 1u;
    }
    return (a_base < b_limit && b_base < a_limit) ? 1u : 0u;
}

static uint32_t stage77_pmap_multiwindow_l1_index(uint32_t va)
{
    return va >> 20;
}

static uint32_t stage77_pmap_multiwindow_l2_index(uint32_t va)
{
    return (va & STAGE77_XNU_PMAP_MULTIWINDOW_DRYRUN_L2_INDEX_MASK) >>
           STAGE77_XNU_PMAP_MULTIWINDOW_DRYRUN_L2_INDEX_SHIFT;
}

static uint32_t stage77_pmap_multiwindow_make_l1_table_descriptor(uint32_t l2_base)
{
    return (l2_base & STAGE77_XNU_PMAP_MULTIWINDOW_DRYRUN_L1_TABLE_MASK) |
           STAGE77_XNU_PMAP_MULTIWINDOW_DRYRUN_L1_TABLE_TYPE;
}

static uint32_t stage77_pmap_multiwindow_make_pte(uint32_t pa, uint32_t template_word)
{
    return (pa & STAGE77_XNU_PMAP_MULTIWINDOW_DRYRUN_PTE_PAGE_MASK) |
           (template_word & STAGE77_XNU_PMAP_MULTIWINDOW_DRYRUN_PTE_ATTR_MASK);
}

static uint32_t stage77_pmap_multiwindow_expected_pa(uint32_t va, uint32_t virt_base, uint32_t phys_base)
{
    return phys_base + (va - virt_base);
}

static uint32_t stage77_pmap_multiwindow_populate_window(uint32_t *l1, uint32_t *l2_bank,
                                                         uint32_t window_slot,
                                                         uint32_t virt_base,
                                                         uint32_t phys_base,
                                                         uint32_t template_word)
{
    uint32_t first_l1 = stage77_pmap_multiwindow_l1_index(virt_base);
    uint32_t l2_window_offset = window_slot * STAGE77_XNU_PMAP_MULTIWINDOW_DRYRUN_L2_PTE_COUNT;
    uint32_t writes = 0u;

    if (window_slot >= STAGE77_XNU_PMAP_MULTIWINDOW_DRYRUN_WINDOW_COUNT) {
        return 0u;
    }
    if ((virt_base & (STAGE77_XNU_PMAP_MULTIWINDOW_DRYRUN_WINDOW_ALIGN - 1u)) != 0u ||
        (phys_base & STAGE77_XNU_PMAP_MULTIWINDOW_DRYRUN_PAGE_OFFSET_MASK) != 0u) {
        return 0u;
    }

    for (uint32_t i = 0; i < STAGE77_XNU_PMAP_MULTIWINDOW_DRYRUN_L2_TABLES_PER_PAGE; i++) {
        uint32_t l1_index = first_l1 + i;
        uint32_t l2_base = (uint32_t)(uintptr_t)&l2_bank[l2_window_offset +
            (i * STAGE77_XNU_PMAP_MULTIWINDOW_DRYRUN_L2_PTES_PER_TABLE)];

        if (l1_index >= STAGE77_XNU_PMAP_MULTIWINDOW_DRYRUN_L1_ENTRY_COUNT) {
            return 0u;
        }
        l1[l1_index] = stage77_pmap_multiwindow_make_l1_table_descriptor(l2_base);
    }

    for (uint32_t i = 0; i < STAGE77_XNU_PMAP_MULTIWINDOW_DRYRUN_L2_PTE_COUNT; i++) {
        uint32_t pa = phys_base + (i * STAGE77_XNU_PMAP_MULTIWINDOW_DRYRUN_PAGE_SIZE);
        l2_bank[l2_window_offset + i] = stage77_pmap_multiwindow_make_pte(pa, template_word);
        writes++;
    }

    return writes;
}

static uint32_t stage77_pmap_multiwindow_translate(const uint32_t *l1,
                                                   uint32_t local_l2_base,
                                                   uint32_t local_l2_limit,
                                                   uint32_t va,
                                                   uint32_t *pa_out)
{
    uint32_t l1_index = stage77_pmap_multiwindow_l1_index(va);
    uint32_t l2_index = stage77_pmap_multiwindow_l2_index(va);
    uint32_t l1_desc;
    uint32_t l2_base;
    const uint32_t *l2;
    uint32_t pte;

    if (pa_out) {
        *pa_out = 0u;
    }
    if (l1_index >= STAGE77_XNU_PMAP_MULTIWINDOW_DRYRUN_L1_ENTRY_COUNT) {
        return 0u;
    }
    l1_desc = l1[l1_index];
    if ((l1_desc & STAGE77_XNU_PMAP_MULTIWINDOW_DRYRUN_L1_TYPE_MASK) !=
        STAGE77_XNU_PMAP_MULTIWINDOW_DRYRUN_L1_TABLE_TYPE) {
        return 0u;
    }
    l2_base = l1_desc & STAGE77_XNU_PMAP_MULTIWINDOW_DRYRUN_L1_TABLE_MASK;
    if (l2_base < local_l2_base || l2_base >= local_l2_limit ||
        local_l2_limit - l2_base < STAGE77_XNU_PMAP_MULTIWINDOW_DRYRUN_L2_COARSE_BYTES) {
        return 0u;
    }
    l2 = (const uint32_t *)(uintptr_t)l2_base;
    pte = l2[l2_index];
    if ((pte & STAGE77_XNU_PMAP_MULTIWINDOW_DRYRUN_PTE_TYPE_MASK) !=
        STAGE77_XNU_PMAP_MULTIWINDOW_DRYRUN_PTE_TYPE_SMALL) {
        return 0u;
    }
    if (pa_out) {
        *pa_out = (pte & STAGE77_XNU_PMAP_MULTIWINDOW_DRYRUN_PTE_PAGE_MASK) |
                  (va & STAGE77_XNU_PMAP_MULTIWINDOW_DRYRUN_PAGE_OFFSET_MASK);
    }
    return 1u;
}

static uint32_t stage77_pmap_multiwindow_verify_l1_window(
    const uint32_t *l1,
    uint32_t first_l1,
    uint32_t l1_count,
    uint32_t l2_window_base,
    uint32_t *first_word,
    uint32_t *last_word,
    uint32_t *type_mask_seen,
    uint32_t *attr_mask_seen)
{
    uint32_t ok = 1u;

    if (first_word) {
        *first_word = 0u;
    }
    if (last_word) {
        *last_word = 0u;
    }

    for (uint32_t i = 0; i < l1_count; i++) {
        uint32_t l1_index = first_l1 + i;
        uint32_t expected_l2_base = l2_window_base +
            (i * STAGE77_XNU_PMAP_MULTIWINDOW_DRYRUN_L2_COARSE_BYTES);
        uint32_t expected = stage77_pmap_multiwindow_make_l1_table_descriptor(expected_l2_base);
        uint32_t word;

        if (l1_index >= STAGE77_XNU_PMAP_MULTIWINDOW_DRYRUN_L1_ENTRY_COUNT) {
            ok = 0u;
            continue;
        }
        word = l1[l1_index];
        if (i == 0u && first_word) {
            *first_word = word;
        }
        if (i == l1_count - 1u && last_word) {
            *last_word = word;
        }
        if (type_mask_seen) {
            *type_mask_seen |= word & STAGE77_XNU_PMAP_MULTIWINDOW_DRYRUN_L1_TYPE_MASK;
        }
        if (attr_mask_seen) {
            *attr_mask_seen |= word & STAGE77_XNU_PMAP_MULTIWINDOW_DRYRUN_L1_TABLE_ATTR_MASK;
        }
        if (word != expected) {
            ok = 0u;
        }
    }

    return ok;
}

static uint32_t stage77_pmap_multiwindow_verify_pte_window(
    const uint32_t *l2_bank,
    uint32_t window_slot,
    uint32_t phys_base,
    uint32_t template_word,
    uint32_t *first_word,
    uint32_t *last_word,
    uint32_t *type_mask_seen,
    uint32_t *attr_mask_seen)
{
    uint32_t ok = 1u;
    uint32_t l2_window_offset = window_slot * STAGE77_XNU_PMAP_MULTIWINDOW_DRYRUN_L2_PTE_COUNT;

    if (first_word) {
        *first_word = 0u;
    }
    if (last_word) {
        *last_word = 0u;
    }

    for (uint32_t i = 0; i < STAGE77_XNU_PMAP_MULTIWINDOW_DRYRUN_L2_PTE_COUNT; i++) {
        uint32_t pa = phys_base + (i * STAGE77_XNU_PMAP_MULTIWINDOW_DRYRUN_PAGE_SIZE);
        uint32_t expected = stage77_pmap_multiwindow_make_pte(pa, template_word);
        uint32_t word = l2_bank[l2_window_offset + i];

        if (type_mask_seen) {
            *type_mask_seen |= word & STAGE77_XNU_PMAP_MULTIWINDOW_DRYRUN_PTE_TYPE_MASK;
        }
        if (attr_mask_seen) {
            *attr_mask_seen |= word & STAGE77_XNU_PMAP_MULTIWINDOW_DRYRUN_PTE_ATTR_MASK;
        }
        if (i == 0u && first_word) {
            *first_word = word;
        }
        if (i == STAGE77_XNU_PMAP_MULTIWINDOW_DRYRUN_L2_PTE_COUNT - 1u && last_word) {
            *last_word = word;
        }
        if (word != expected) {
            ok = 0u;
        }
    }

    return ok;
}

static void stage77_xnu_pmap_multiwindow_dryrun_contract_log(
    const struct stage77_xnu_pmap_multiwindow_dryrun_contract *contract)
{
    xnu_log_kv32("stage77_xnu_pmap_multiwindow_dryrun_contract_status", contract->status);
    xnu_log_kv32("stage77_xnu_pmap_multiwindow_dryrun_contract_required_mask", contract->required_mask);
    xnu_log_kv32("stage77_xnu_pmap_multiwindow_dryrun_contract_satisfied_mask", contract->satisfied_mask);
    xnu_log_kv32("stage77_xnu_pmap_multiwindow_dryrun_contract_failure_mask", contract->failure_mask);
    xnu_log_kv32("stage77_xnu_pmap_multiwindow_dryrun_contract_checksum", contract->checksum);
    xnu_log_kv32("stage77_xnu_pmap_multiwindow_dryrun_source_table_status", contract->source_table_dryrun_status);
    xnu_log_kv32("stage77_xnu_pmap_multiwindow_dryrun_source_page_status", contract->source_page_dryrun_status);
    xnu_log_kv32("stage77_xnu_pmap_multiwindow_dryrun_source_attr_status", contract->source_attr_dryrun_status);
    xnu_log_kv32("stage77_xnu_pmap_multiwindow_dryrun_window_count", contract->window_count);
    xnu_log_kv32("stage77_xnu_pmap_multiwindow_dryrun_window_size", contract->window_size);
    xnu_log_kv32("stage77_xnu_pmap_multiwindow_dryrun_l1_entry_count", contract->l1_entry_count);
    xnu_log_kv32("stage77_xnu_pmap_multiwindow_dryrun_l2_pte_count", contract->l2_pte_count);
    xnu_log_kv32("stage77_xnu_pmap_multiwindow_dryrun_total_l2_pte_count", contract->total_l2_pte_count);
    xnu_log_kv32("stage77_xnu_pmap_multiwindow_dryrun_pte_template_kernel", contract->pte_template_kernel);
    xnu_log_kv32("stage77_xnu_pmap_multiwindow_dryrun_pte_template_workspace", contract->pte_template_workspace);
    xnu_log_kv32("stage77_xnu_pmap_multiwindow_dryrun_pte_template_ram_console", contract->pte_template_ram_console);
    xnu_log_kv32("stage77_xnu_pmap_multiwindow_dryrun_pte_template_device", contract->pte_template_device);
    xnu_log_kv32("stage77_xnu_pmap_multiwindow_dryrun_pte_expected_attr_mask", contract->pte_expected_attr_mask);
    xnu_log_kv32("stage77_xnu_pmap_multiwindow_dryrun_local_l1_base", contract->local_l1_base);
    xnu_log_kv32("stage77_xnu_pmap_multiwindow_dryrun_local_l1_limit", contract->local_l1_limit);
    xnu_log_kv32("stage77_xnu_pmap_multiwindow_dryrun_local_l2_bank_base", contract->local_l2_bank_base);
    xnu_log_kv32("stage77_xnu_pmap_multiwindow_dryrun_local_l2_bank_limit", contract->local_l2_bank_limit);
    xnu_log_kv32("stage77_xnu_pmap_multiwindow_dryrun_local_l1_zero_checksum", contract->local_l1_zero_checksum);
    xnu_log_kv32("stage77_xnu_pmap_multiwindow_dryrun_local_l2_zero_checksum", contract->local_l2_zero_checksum);
    xnu_log_kv32("stage77_xnu_pmap_multiwindow_dryrun_local_l1_populated_checksum", contract->local_l1_populated_checksum);
    xnu_log_kv32("stage77_xnu_pmap_multiwindow_dryrun_local_l2_populated_checksum", contract->local_l2_populated_checksum);
    xnu_log_kv32("stage77_xnu_pmap_multiwindow_dryrun_local_l1_write_count", contract->local_l1_write_count);
    xnu_log_kv32("stage77_xnu_pmap_multiwindow_dryrun_local_l2_pte_write_count", contract->local_l2_pte_write_count);
    xnu_log_kv32("stage77_xnu_pmap_multiwindow_dryrun_gVirtBase", contract->gVirtBase);
    xnu_log_kv32("stage77_xnu_pmap_multiwindow_dryrun_gPhysBase", contract->gPhysBase);
    xnu_log_kv32("stage77_xnu_pmap_multiwindow_dryrun_workspace_base", contract->workspace_base);
    xnu_log_kv32("stage77_xnu_pmap_multiwindow_dryrun_workspace_limit", contract->workspace_limit);
    xnu_log_kv32("stage77_xnu_pmap_multiwindow_dryrun_kernel_window_virt_base", contract->kernel_window_virt_base);
    xnu_log_kv32("stage77_xnu_pmap_multiwindow_dryrun_kernel_window_phys_base", contract->kernel_window_phys_base);
    xnu_log_kv32("stage77_xnu_pmap_multiwindow_dryrun_kernel_window_l1_first_index", contract->kernel_window_l1_first_index);
    xnu_log_kv32("stage77_xnu_pmap_multiwindow_dryrun_kernel_window_l1_last_index", contract->kernel_window_l1_last_index);
    xnu_log_kv32("stage77_xnu_pmap_multiwindow_dryrun_ram_console_window_virt_base", contract->ram_console_window_virt_base);
    xnu_log_kv32("stage77_xnu_pmap_multiwindow_dryrun_ram_console_window_phys_base", contract->ram_console_window_phys_base);
    xnu_log_kv32("stage77_xnu_pmap_multiwindow_dryrun_ram_console_window_l1_first_index", contract->ram_console_window_l1_first_index);
    xnu_log_kv32("stage77_xnu_pmap_multiwindow_dryrun_ram_console_window_l1_last_index", contract->ram_console_window_l1_last_index);
    xnu_log_kv32("stage77_xnu_pmap_multiwindow_dryrun_device_window_virt_base", contract->device_window_virt_base);
    xnu_log_kv32("stage77_xnu_pmap_multiwindow_dryrun_device_window_phys_base", contract->device_window_phys_base);
    xnu_log_kv32("stage77_xnu_pmap_multiwindow_dryrun_device_window_l1_first_index", contract->device_window_l1_first_index);
    xnu_log_kv32("stage77_xnu_pmap_multiwindow_dryrun_device_window_l1_last_index", contract->device_window_l1_last_index);
    xnu_log_kv32("stage77_xnu_pmap_multiwindow_dryrun_l1_descriptor_kernel_first_word", contract->l1_descriptor_kernel_first_word);
    xnu_log_kv32("stage77_xnu_pmap_multiwindow_dryrun_l1_descriptor_ram_console_first_word", contract->l1_descriptor_ram_console_first_word);
    xnu_log_kv32("stage77_xnu_pmap_multiwindow_dryrun_l1_descriptor_device_first_word", contract->l1_descriptor_device_first_word);
    xnu_log_kv32("stage77_xnu_pmap_multiwindow_dryrun_l1_descriptor_type_mask_seen", contract->l1_descriptor_type_mask_seen);
    xnu_log_kv32("stage77_xnu_pmap_multiwindow_dryrun_l1_descriptor_attr_mask_seen", contract->l1_descriptor_attr_mask_seen);
    xnu_log_kv32("stage77_xnu_pmap_multiwindow_dryrun_pte_kernel_first_word", contract->pte_kernel_first_word);
    xnu_log_kv32("stage77_xnu_pmap_multiwindow_dryrun_pte_ram_console_first_word", contract->pte_ram_console_first_word);
    xnu_log_kv32("stage77_xnu_pmap_multiwindow_dryrun_pte_device_first_word", contract->pte_device_first_word);
    xnu_log_kv32("stage77_xnu_pmap_multiwindow_dryrun_pte_type_mask_seen", contract->pte_type_mask_seen);
    xnu_log_kv32("stage77_xnu_pmap_multiwindow_dryrun_pte_attr_mask_seen", contract->pte_attr_mask_seen);
    xnu_log_kv32("stage77_xnu_pmap_multiwindow_dryrun_translation_kernel_va", contract->translation_kernel_va);
    xnu_log_kv32("stage77_xnu_pmap_multiwindow_dryrun_translation_kernel_pa", contract->translation_kernel_pa);
    xnu_log_kv32("stage77_xnu_pmap_multiwindow_dryrun_translation_workspace_va", contract->translation_workspace_va);
    xnu_log_kv32("stage77_xnu_pmap_multiwindow_dryrun_translation_workspace_pa", contract->translation_workspace_pa);
    xnu_log_kv32("stage77_xnu_pmap_multiwindow_dryrun_translation_ram_console_va", contract->translation_ram_console_va);
    xnu_log_kv32("stage77_xnu_pmap_multiwindow_dryrun_translation_ram_console_pa", contract->translation_ram_console_pa);
    xnu_log_kv32("stage77_xnu_pmap_multiwindow_dryrun_translation_device_va", contract->translation_device_va);
    xnu_log_kv32("stage77_xnu_pmap_multiwindow_dryrun_translation_device_pa", contract->translation_device_pa);
    xnu_log_kv32("stage77_xnu_pmap_multiwindow_dryrun_translation_case_count", contract->translation_case_count);
    xnu_log_kv32("stage77_xnu_pmap_multiwindow_dryrun_prior_page_pte_kernel_word", contract->prior_page_pte_kernel_word);
    xnu_log_kv32("stage77_xnu_pmap_multiwindow_dryrun_prior_page_pte_attr_seen", contract->prior_page_pte_attr_seen);
    xnu_log_kv32("stage77_xnu_pmap_multiwindow_dryrun_prior_attr_pte_template_rwx", contract->prior_attr_pte_template_rwx);
    xnu_log_kv32("stage77_xnu_pmap_multiwindow_dryrun_prior_attr_wimg_io_bits", contract->prior_attr_wimg_io_bits);
    xnu_log_kv32("stage77_xnu_pmap_multiwindow_dryrun_public_pmap_compile_count", contract->public_pmap_compile_count);
    xnu_log_kv32("stage77_xnu_pmap_multiwindow_dryrun_public_pmap_link_count", contract->public_pmap_link_count);
    xnu_log_kv32("stage77_xnu_pmap_multiwindow_dryrun_public_pmap_execute_count", contract->public_pmap_execute_count);
    xnu_log_kv32("stage77_xnu_pmap_multiwindow_dryrun_public_arm_vm_init_executed", contract->public_arm_vm_init_executed);
    xnu_log_kv32("stage77_xnu_pmap_multiwindow_dryrun_public_pmap_runtime_executed", contract->public_pmap_runtime_executed);
    xnu_log_kv32("stage77_xnu_pmap_multiwindow_dryrun_proposed_workspace_written", contract->proposed_workspace_written);
    xnu_log_kv32("stage77_xnu_pmap_multiwindow_dryrun_live_pmap_tables_installed", contract->live_pmap_tables_installed);
    xnu_log_kv32("stage77_xnu_pmap_multiwindow_dryrun_ttbr_written", contract->ttbr_written);
    xnu_log_kv32("stage77_xnu_pmap_multiwindow_dryrun_ttbcr_written", contract->ttbcr_written);
    xnu_log_kv32("stage77_xnu_pmap_multiwindow_dryrun_dacr_written", contract->dacr_written);
    xnu_log_kv32("stage77_xnu_pmap_multiwindow_dryrun_sctlr_written", contract->sctlr_written);
    xnu_log_kv32("stage77_xnu_pmap_multiwindow_dryrun_tlbs_invalidated", contract->tlbs_invalidated);
    xnu_log_kv32("stage77_xnu_pmap_multiwindow_dryrun_caches_changed", contract->caches_changed);
    xnu_log_kv32("stage77_xnu_pmap_multiwindow_dryrun_persistent_write_attempted", contract->persistent_write_attempted);
    xnu_log_kv32("stage77_xnu_pmap_multiwindow_dryrun_xnu_start_executed", contract->xnu_start_executed);
    xnu_log_kv32("stage77_xnu_pmap_multiwindow_dryrun_generated_macho_executed", contract->generated_macho_executed);
    xnu_log_kv32("stage77_xnu_pmap_multiwindow_dryrun_local_only", contract->local_only);
    xnu_log_kv32("stage77_xnu_pmap_multiwindow_dryrun_fail_closed", contract->fail_closed);
}

int stage77_xnu_pmap_multiwindow_dryrun_contract_selftest(const struct stage77_loader_preflight *preflight)
{
    struct stage77_xnu_pmap_multiwindow_dryrun_contract *contract =
        &g_stage77_xnu_pmap_multiwindow_dryrun_contract;
    const struct stage77_xnu_pmap_table_dryrun_contract *table = stage77_xnu_pmap_table_dryrun_contract_result();
    const struct stage77_xnu_pmap_page_dryrun_contract *page = stage77_xnu_pmap_page_dryrun_contract_result();
    const struct stage77_xnu_pmap_attr_dryrun_contract *attr = stage77_xnu_pmap_attr_dryrun_contract_result();
    const struct stage77_xnu_pmap_bootstrap_contract *pmap = stage77_xnu_pmap_bootstrap_contract_result();
    const struct stage77_pmap_bootstrap_snapshot *snapshot = mmu_stage77_pmap_bootstrap_snapshot_result();
    const struct stage77_ttbr0_roundtrip *ttbr_rt = mmu_stage77_ttbr0_roundtrip_result();
    uint32_t local_l1_limit = 0u;
    uint32_t local_l2_bank_limit = 0u;
    uint32_t translated = 0u;
    uint32_t source_rollups_ok;
    uint32_t l1_readback_ok;
    uint32_t pte_readback_ok;
    uint32_t kernel_l2_base;
    uint32_t ram_console_l2_base;
    uint32_t device_l2_base;

    memset(contract, 0, sizeof(*contract));
    memset(g_stage77_pmap_multiwindow_l1, 0, sizeof(g_stage77_pmap_multiwindow_l1));
    memset(g_stage77_pmap_multiwindow_l2_bank, 0, sizeof(g_stage77_pmap_multiwindow_l2_bank));

    contract->version = STAGE77_XNU_PMAP_MULTIWINDOW_DRYRUN_CONTRACT_VERSION;
    contract->size = sizeof(*contract);
    contract->status = STAGE77_STATUS_BASE;
    contract->required_mask = STAGE77_XNU_PMAP_MULTIWINDOW_DRYRUN_REQUIRED_MASK;
    contract->page_size = STAGE77_XNU_PMAP_MULTIWINDOW_DRYRUN_PAGE_SIZE;
    contract->page_offset_mask = STAGE77_XNU_PMAP_MULTIWINDOW_DRYRUN_PAGE_OFFSET_MASK;
    contract->window_count = STAGE77_XNU_PMAP_MULTIWINDOW_DRYRUN_WINDOW_COUNT;
    contract->window_size = STAGE77_XNU_PMAP_MULTIWINDOW_DRYRUN_WINDOW_SIZE;
    contract->window_alignment = STAGE77_XNU_PMAP_MULTIWINDOW_DRYRUN_WINDOW_ALIGN;
    contract->l1_entry_count = STAGE77_XNU_PMAP_MULTIWINDOW_DRYRUN_L1_ENTRY_COUNT;
    contract->l1_bytes = STAGE77_XNU_PMAP_MULTIWINDOW_DRYRUN_L1_BYTES;
    contract->l1_alignment = STAGE77_XNU_PMAP_MULTIWINDOW_DRYRUN_L1_ALIGNMENT;
    contract->l1_table_descriptor_type = STAGE77_XNU_PMAP_MULTIWINDOW_DRYRUN_L1_TABLE_TYPE;
    contract->l1_table_type_mask = STAGE77_XNU_PMAP_MULTIWINDOW_DRYRUN_L1_TYPE_MASK;
    contract->l1_table_base_mask = STAGE77_XNU_PMAP_MULTIWINDOW_DRYRUN_L1_TABLE_MASK;
    contract->l1_table_attr_mask = STAGE77_XNU_PMAP_MULTIWINDOW_DRYRUN_L1_TABLE_ATTR_MASK;
    contract->l2_page_bytes = STAGE77_XNU_PMAP_MULTIWINDOW_DRYRUN_L2_PAGE_BYTES;
    contract->l2_alignment = STAGE77_XNU_PMAP_MULTIWINDOW_DRYRUN_L2_ALIGNMENT;
    contract->l2_coarse_table_bytes = STAGE77_XNU_PMAP_MULTIWINDOW_DRYRUN_L2_COARSE_BYTES;
    contract->l2_tables_per_page = STAGE77_XNU_PMAP_MULTIWINDOW_DRYRUN_L2_TABLES_PER_PAGE;
    contract->l2_ptes_per_table = STAGE77_XNU_PMAP_MULTIWINDOW_DRYRUN_L2_PTES_PER_TABLE;
    contract->l2_pte_count = STAGE77_XNU_PMAP_MULTIWINDOW_DRYRUN_L2_PTE_COUNT;
    contract->total_l2_pte_count = STAGE77_XNU_PMAP_MULTIWINDOW_DRYRUN_WINDOW_COUNT *
        STAGE77_XNU_PMAP_MULTIWINDOW_DRYRUN_L2_PTE_COUNT;
    contract->pte_type_mask = STAGE77_XNU_PMAP_MULTIWINDOW_DRYRUN_PTE_TYPE_MASK;
    contract->pte_type_small = STAGE77_XNU_PMAP_MULTIWINDOW_DRYRUN_PTE_TYPE_SMALL;
    contract->pte_page_mask = STAGE77_XNU_PMAP_MULTIWINDOW_DRYRUN_PTE_PAGE_MASK;
    contract->pte_attr_mask = STAGE77_XNU_PMAP_MULTIWINDOW_DRYRUN_PTE_ATTR_MASK;
    contract->l1_descriptor_expected_attr_mask = STAGE77_XNU_PMAP_MULTIWINDOW_DRYRUN_L1_TABLE_TYPE;
    contract->local_l1_base = (uint32_t)(uintptr_t)g_stage77_pmap_multiwindow_l1;
    contract->local_l1_bytes = sizeof(g_stage77_pmap_multiwindow_l1);
    contract->local_l1_alignment = contract->local_l1_base &
        (STAGE77_XNU_PMAP_MULTIWINDOW_DRYRUN_L1_ALIGNMENT - 1u);
    contract->local_l2_bank_base = (uint32_t)(uintptr_t)g_stage77_pmap_multiwindow_l2_bank;
    contract->local_l2_bank_bytes = sizeof(g_stage77_pmap_multiwindow_l2_bank);
    contract->local_l2_bank_alignment = contract->local_l2_bank_base &
        (STAGE77_XNU_PMAP_MULTIWINDOW_DRYRUN_L2_ALIGNMENT - 1u);
    contract->local_l1_zero_checksum = stage77_pmap_multiwindow_buffer_checksum(
        g_stage77_pmap_multiwindow_l1, STAGE77_XNU_PMAP_MULTIWINDOW_DRYRUN_L1_ENTRY_COUNT);
    contract->local_l2_zero_checksum = stage77_pmap_multiwindow_buffer_checksum(
        g_stage77_pmap_multiwindow_l2_bank, contract->total_l2_pte_count);

    xnu_log_puts("Stage77 XNU pmap multi-window page-granular dry-run contract begin\n");

    if (!preflight || !table || !page || !attr || !pmap || !snapshot || !ttbr_rt) {
        contract->failure_mask |= STAGE77_XNU_PMAP_MULTIWINDOW_DRYRUN_FAIL_SOURCE |
                                  STAGE77_XNU_PMAP_MULTIWINDOW_DRYRUN_FAIL_SAFETY_BOUNDARY;
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
    contract->source_attr_dryrun_status = attr->status;
    contract->source_attr_dryrun_required_mask = attr->required_mask;
    contract->source_attr_dryrun_satisfied_mask = attr->satisfied_mask;
    contract->source_attr_dryrun_failure_mask = attr->failure_mask;
    contract->source_attr_dryrun_checksum = attr->checksum;
    contract->source_pmap_bootstrap_status = pmap->status;
    contract->source_snapshot_status = snapshot->status;
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
    contract->public_pmap_compile_count = attr->public_pmap_compile_count;
    contract->public_pmap_link_count = attr->public_pmap_link_count;
    contract->public_pmap_execute_count = attr->public_pmap_execute_count;

    source_rollups_ok = (contract->source_table_dryrun_status == STAGE77_STATUS_OK &&
                         contract->source_table_dryrun_satisfied_mask == contract->source_table_dryrun_required_mask &&
                         contract->source_table_dryrun_failure_mask == 0u &&
                         contract->source_page_dryrun_status == STAGE77_STATUS_OK &&
                         contract->source_page_dryrun_satisfied_mask == contract->source_page_dryrun_required_mask &&
                         contract->source_page_dryrun_failure_mask == 0u &&
                         contract->source_attr_dryrun_status == STAGE77_STATUS_OK &&
                         contract->source_attr_dryrun_satisfied_mask == contract->source_attr_dryrun_required_mask &&
                         contract->source_attr_dryrun_failure_mask == 0u &&
                         contract->source_pmap_bootstrap_status == STAGE77_STATUS_OK &&
                         contract->source_snapshot_status == STAGE77_STATUS_OK &&
                         contract->source_bootstrap_status == STAGE77_STATUS_OK &&
                         contract->source_tte_dryrun_status == STAGE77_STATUS_OK &&
                         contract->source_ttbr_roundtrip_status == STAGE77_STATUS_OK &&
                         contract->source_ttbr_restored_status == STAGE77_STATUS_OK &&
                         contract->source_cache_preserved_status == STAGE77_STATUS_OK &&
                         contract->source_compile_graph_status == STAGE77_STATUS_OK &&
                         contract->source_object_subset_status == STAGE77_STATUS_OK &&
                         contract->source_link_status == STAGE77_STATUS_OK &&
                         preflight->xnu_compile_graph_failure_mask == 0u &&
                         preflight->xnu_object_subset_failure_mask == 0u &&
                         preflight->xnu_link_failure_mask == 0u) ? 1u : 0u;

    if (contract->source_table_dryrun_status == STAGE77_STATUS_OK &&
        contract->source_table_dryrun_satisfied_mask == contract->source_table_dryrun_required_mask &&
        contract->source_table_dryrun_failure_mask == 0u) {
        contract->satisfied_mask |= STAGE77_XNU_PMAP_MULTIWINDOW_DRYRUN_SAT_SOURCE_TABLE_DRYRUN;
    } else {
        contract->failure_mask |= STAGE77_XNU_PMAP_MULTIWINDOW_DRYRUN_FAIL_SOURCE;
    }
    if (contract->source_page_dryrun_status == STAGE77_STATUS_OK &&
        contract->source_page_dryrun_satisfied_mask == contract->source_page_dryrun_required_mask &&
        contract->source_page_dryrun_failure_mask == 0u) {
        contract->satisfied_mask |= STAGE77_XNU_PMAP_MULTIWINDOW_DRYRUN_SAT_SOURCE_PAGE_DRYRUN;
    } else {
        contract->failure_mask |= STAGE77_XNU_PMAP_MULTIWINDOW_DRYRUN_FAIL_SOURCE;
    }
    if (contract->source_attr_dryrun_status == STAGE77_STATUS_OK &&
        contract->source_attr_dryrun_satisfied_mask == contract->source_attr_dryrun_required_mask &&
        contract->source_attr_dryrun_failure_mask == 0u) {
        contract->satisfied_mask |= STAGE77_XNU_PMAP_MULTIWINDOW_DRYRUN_SAT_SOURCE_ATTR_DRYRUN;
    } else {
        contract->failure_mask |= STAGE77_XNU_PMAP_MULTIWINDOW_DRYRUN_FAIL_SOURCE;
    }
    if (source_rollups_ok) {
        contract->satisfied_mask |= STAGE77_XNU_PMAP_MULTIWINDOW_DRYRUN_SAT_SOURCE_ROLLUPS;
    } else {
        contract->failure_mask |= STAGE77_XNU_PMAP_MULTIWINDOW_DRYRUN_FAIL_SOURCE;
    }

    contract->pte_template_kernel = attr->pte_template_rwx_word;
    contract->pte_template_workspace = attr->pte_template_rwx_word;
    contract->pte_template_ram_console = attr->pte_template_rwnx_word;
    contract->pte_template_device = STAGE77_XNU_PMAP_ATTR_DRYRUN_PTE_TYPE |
        STAGE77_XNU_PMAP_ATTR_DRYRUN_PTE_AF | attr->pte_ap_rwna_word | attr->wimg_io_pte_bits;
    contract->pte_expected_attr_mask = (contract->pte_template_kernel |
                                        contract->pte_template_ram_console |
                                        contract->pte_template_device) &
        STAGE77_XNU_PMAP_MULTIWINDOW_DRYRUN_PTE_ATTR_MASK;

    if (contract->page_size == 0x00001000u &&
        contract->page_offset_mask == 0x00000fffu &&
        contract->window_count == 3u &&
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
        contract->total_l2_pte_count == 3072u &&
        contract->pte_type_small == 0x00000002u &&
        contract->pte_page_mask == 0xfffff000u &&
        contract->pte_template_kernel == 0x00000412u &&
        contract->pte_template_workspace == 0x00000412u &&
        contract->pte_template_ram_console == 0x00000413u &&
        contract->pte_template_device == 0x0000001fu &&
        contract->pte_expected_attr_mask == 0x0000041fu &&
        contract->gPhysBase == RAM_PHYS_BASE &&
        contract->avail_end == RAM_CONSOLE_BASE) {
        contract->satisfied_mask |= STAGE77_XNU_PMAP_MULTIWINDOW_DRYRUN_SAT_CONSTANTS;
    } else {
        contract->failure_mask |= STAGE77_XNU_PMAP_MULTIWINDOW_DRYRUN_FAIL_CONSTANTS |
                                  STAGE77_XNU_PMAP_MULTIWINDOW_DRYRUN_FAIL_ATTR;
    }

    if (!stage77_pmap_multiwindow_add_overflow_u32(contract->local_l1_base,
                                                   contract->local_l1_bytes,
                                                   &local_l1_limit)) {
        contract->local_l1_limit = local_l1_limit;
    }
    if (!stage77_pmap_multiwindow_add_overflow_u32(contract->local_l2_bank_base,
                                                   contract->local_l2_bank_bytes,
                                                   &local_l2_bank_limit)) {
        contract->local_l2_bank_limit = local_l2_bank_limit;
    }

    if (contract->local_l1_base != 0u && contract->local_l1_limit > contract->local_l1_base &&
        contract->local_l1_bytes == STAGE77_XNU_PMAP_MULTIWINDOW_DRYRUN_L1_BYTES &&
        contract->local_l1_alignment == 0u && contract->local_l1_limit <= RAM_PHYS_BASE &&
        contract->local_l1_base != contract->workspace_l1_table_phys &&
        !stage77_pmap_multiwindow_local_ranges_overlap(contract->local_l1_base, contract->local_l1_limit,
                                                       contract->workspace_base, contract->workspace_limit)) {
        contract->satisfied_mask |= STAGE77_XNU_PMAP_MULTIWINDOW_DRYRUN_SAT_LOCAL_L1_BUFFER;
    } else {
        contract->failure_mask |= STAGE77_XNU_PMAP_MULTIWINDOW_DRYRUN_FAIL_LOCAL_BUFFER;
    }
    if (contract->local_l2_bank_base != 0u && contract->local_l2_bank_limit > contract->local_l2_bank_base &&
        contract->local_l2_bank_bytes == (STAGE77_XNU_PMAP_MULTIWINDOW_DRYRUN_WINDOW_COUNT *
                                          STAGE77_XNU_PMAP_MULTIWINDOW_DRYRUN_L2_PAGE_BYTES) &&
        contract->local_l2_bank_alignment == 0u && contract->local_l2_bank_limit <= RAM_PHYS_BASE &&
        !stage77_pmap_multiwindow_local_ranges_overlap(contract->local_l2_bank_base, contract->local_l2_bank_limit,
                                                       contract->local_l1_base, contract->local_l1_limit) &&
        !stage77_pmap_multiwindow_local_ranges_overlap(contract->local_l2_bank_base, contract->local_l2_bank_limit,
                                                       contract->workspace_base, contract->workspace_limit)) {
        contract->satisfied_mask |= STAGE77_XNU_PMAP_MULTIWINDOW_DRYRUN_SAT_LOCAL_L2_BANK;
    } else {
        contract->failure_mask |= STAGE77_XNU_PMAP_MULTIWINDOW_DRYRUN_FAIL_LOCAL_BUFFER;
    }
    if (contract->local_l1_zero_checksum == stage77_pmap_multiwindow_buffer_checksum(
            g_stage77_pmap_multiwindow_l1, STAGE77_XNU_PMAP_MULTIWINDOW_DRYRUN_L1_ENTRY_COUNT) &&
        contract->local_l2_zero_checksum == stage77_pmap_multiwindow_buffer_checksum(
            g_stage77_pmap_multiwindow_l2_bank, contract->total_l2_pte_count)) {
        contract->satisfied_mask |= STAGE77_XNU_PMAP_MULTIWINDOW_DRYRUN_SAT_LOCAL_ZERO;
    } else {
        contract->failure_mask |= STAGE77_XNU_PMAP_MULTIWINDOW_DRYRUN_FAIL_LOCAL_BUFFER;
    }

    contract->kernel_window_virt_base = contract->gVirtBase & ~(STAGE77_XNU_PMAP_MULTIWINDOW_DRYRUN_WINDOW_ALIGN - 1u);
    contract->kernel_window_phys_base = contract->gPhysBase & ~(STAGE77_XNU_PMAP_MULTIWINDOW_DRYRUN_WINDOW_ALIGN - 1u);
    if (!stage77_pmap_multiwindow_add_overflow_u32(contract->kernel_window_virt_base,
                                                   contract->window_size,
                                                   &contract->kernel_window_virt_limit) &&
        !stage77_pmap_multiwindow_add_overflow_u32(contract->kernel_window_phys_base,
                                                   contract->window_size,
                                                   &contract->kernel_window_phys_limit)) {
        contract->kernel_window_l1_first_index = stage77_pmap_multiwindow_l1_index(contract->kernel_window_virt_base);
        contract->kernel_window_l1_last_index = stage77_pmap_multiwindow_l1_index(contract->kernel_window_virt_limit - 1u);
        contract->kernel_window_l1_count = contract->kernel_window_l1_last_index -
            contract->kernel_window_l1_first_index + 1u;
        if (contract->kernel_window_l1_count == STAGE77_XNU_PMAP_MULTIWINDOW_DRYRUN_L2_TABLES_PER_PAGE &&
            contract->kernel_window_l1_last_index < STAGE77_XNU_PMAP_MULTIWINDOW_DRYRUN_L1_ENTRY_COUNT &&
            contract->gVirtBase >= contract->kernel_window_virt_base &&
            contract->gVirtBase < contract->kernel_window_virt_limit &&
            contract->workspace_base >= contract->kernel_window_virt_base &&
            contract->workspace_base < contract->kernel_window_virt_limit &&
            contract->kernel_window_phys_limit <= contract->avail_end) {
            contract->satisfied_mask |= STAGE77_XNU_PMAP_MULTIWINDOW_DRYRUN_SAT_WINDOW_KERNEL;
        } else {
            contract->failure_mask |= STAGE77_XNU_PMAP_MULTIWINDOW_DRYRUN_FAIL_WINDOW;
        }
    } else {
        contract->failure_mask |= STAGE77_XNU_PMAP_MULTIWINDOW_DRYRUN_FAIL_WINDOW;
    }

    contract->ram_console_window_virt_base = RAM_CONSOLE_BASE & ~(STAGE77_XNU_PMAP_MULTIWINDOW_DRYRUN_WINDOW_ALIGN - 1u);
    contract->ram_console_window_phys_base = contract->ram_console_window_virt_base;
    if (!stage77_pmap_multiwindow_add_overflow_u32(contract->ram_console_window_virt_base,
                                                   contract->window_size,
                                                   &contract->ram_console_window_virt_limit) &&
        !stage77_pmap_multiwindow_add_overflow_u32(contract->ram_console_window_phys_base,
                                                   contract->window_size,
                                                   &contract->ram_console_window_phys_limit)) {
        contract->ram_console_window_l1_first_index = stage77_pmap_multiwindow_l1_index(
            contract->ram_console_window_virt_base);
        contract->ram_console_window_l1_last_index = stage77_pmap_multiwindow_l1_index(
            contract->ram_console_window_virt_limit - 1u);
        contract->ram_console_window_l1_count = contract->ram_console_window_l1_last_index -
            contract->ram_console_window_l1_first_index + 1u;
        if (contract->ram_console_window_l1_count == STAGE77_XNU_PMAP_MULTIWINDOW_DRYRUN_L2_TABLES_PER_PAGE &&
            contract->ram_console_window_l1_last_index < STAGE77_XNU_PMAP_MULTIWINDOW_DRYRUN_L1_ENTRY_COUNT &&
            RAM_CONSOLE_BASE >= contract->ram_console_window_virt_base &&
            RAM_CONSOLE_BASE < contract->ram_console_window_virt_limit) {
            contract->satisfied_mask |= STAGE77_XNU_PMAP_MULTIWINDOW_DRYRUN_SAT_WINDOW_RAM_CONSOLE;
        } else {
            contract->failure_mask |= STAGE77_XNU_PMAP_MULTIWINDOW_DRYRUN_FAIL_WINDOW;
        }
    } else {
        contract->failure_mask |= STAGE77_XNU_PMAP_MULTIWINDOW_DRYRUN_FAIL_WINDOW;
    }

    contract->device_window_virt_base = snapshot->kernel_map_base;
    contract->device_window_phys_base = STAGE77_XNU_PMAP_MULTIWINDOW_DRYRUN_GIC_PHYS_BASE;
    if (!stage77_pmap_multiwindow_add_overflow_u32(contract->device_window_virt_base,
                                                   contract->window_size,
                                                   &contract->device_window_virt_limit) &&
        !stage77_pmap_multiwindow_add_overflow_u32(contract->device_window_phys_base,
                                                   contract->window_size,
                                                   &contract->device_window_phys_limit)) {
        contract->device_window_l1_first_index = stage77_pmap_multiwindow_l1_index(contract->device_window_virt_base);
        contract->device_window_l1_last_index = stage77_pmap_multiwindow_l1_index(contract->device_window_virt_limit - 1u);
        contract->device_window_l1_count = contract->device_window_l1_last_index -
            contract->device_window_l1_first_index + 1u;
        if (contract->device_window_l1_count == STAGE77_XNU_PMAP_MULTIWINDOW_DRYRUN_L2_TABLES_PER_PAGE &&
            contract->device_window_l1_last_index < STAGE77_XNU_PMAP_MULTIWINDOW_DRYRUN_L1_ENTRY_COUNT &&
            (contract->device_window_virt_base & (STAGE77_XNU_PMAP_MULTIWINDOW_DRYRUN_WINDOW_ALIGN - 1u)) == 0u &&
            (contract->device_window_phys_base & (STAGE77_XNU_PMAP_MULTIWINDOW_DRYRUN_WINDOW_ALIGN - 1u)) == 0u) {
            contract->satisfied_mask |= STAGE77_XNU_PMAP_MULTIWINDOW_DRYRUN_SAT_WINDOW_DEVICE;
        } else {
            contract->failure_mask |= STAGE77_XNU_PMAP_MULTIWINDOW_DRYRUN_FAIL_WINDOW;
        }
    } else {
        contract->failure_mask |= STAGE77_XNU_PMAP_MULTIWINDOW_DRYRUN_FAIL_WINDOW;
    }

    if ((contract->failure_mask & (STAGE77_XNU_PMAP_MULTIWINDOW_DRYRUN_FAIL_LOCAL_BUFFER |
                                   STAGE77_XNU_PMAP_MULTIWINDOW_DRYRUN_FAIL_WINDOW |
                                   STAGE77_XNU_PMAP_MULTIWINDOW_DRYRUN_FAIL_CONSTANTS)) == 0u) {
        contract->kernel_l2_pte_write_count = stage77_pmap_multiwindow_populate_window(
            g_stage77_pmap_multiwindow_l1, g_stage77_pmap_multiwindow_l2_bank, 0u,
            contract->kernel_window_virt_base, contract->kernel_window_phys_base,
            contract->pte_template_kernel);
        contract->kernel_l1_write_count = STAGE77_XNU_PMAP_MULTIWINDOW_DRYRUN_L2_TABLES_PER_PAGE;
        contract->ram_console_l2_pte_write_count = stage77_pmap_multiwindow_populate_window(
            g_stage77_pmap_multiwindow_l1, g_stage77_pmap_multiwindow_l2_bank, 1u,
            contract->ram_console_window_virt_base, contract->ram_console_window_phys_base,
            contract->pte_template_ram_console);
        contract->ram_console_l1_write_count = STAGE77_XNU_PMAP_MULTIWINDOW_DRYRUN_L2_TABLES_PER_PAGE;
        contract->device_l2_pte_write_count = stage77_pmap_multiwindow_populate_window(
            g_stage77_pmap_multiwindow_l1, g_stage77_pmap_multiwindow_l2_bank, 2u,
            contract->device_window_virt_base, contract->device_window_phys_base,
            contract->pte_template_device);
        contract->device_l1_write_count = STAGE77_XNU_PMAP_MULTIWINDOW_DRYRUN_L2_TABLES_PER_PAGE;
    }

    contract->local_l1_write_count = contract->kernel_l1_write_count +
        contract->ram_console_l1_write_count + contract->device_l1_write_count;
    contract->local_l2_pte_write_count = contract->kernel_l2_pte_write_count +
        contract->ram_console_l2_pte_write_count + contract->device_l2_pte_write_count;

    kernel_l2_base = contract->local_l2_bank_base;
    ram_console_l2_base = contract->local_l2_bank_base + STAGE77_XNU_PMAP_MULTIWINDOW_DRYRUN_L2_PAGE_BYTES;
    device_l2_base = contract->local_l2_bank_base + (2u * STAGE77_XNU_PMAP_MULTIWINDOW_DRYRUN_L2_PAGE_BYTES);
    l1_readback_ok = stage77_pmap_multiwindow_verify_l1_window(
            g_stage77_pmap_multiwindow_l1,
            contract->kernel_window_l1_first_index,
            contract->kernel_window_l1_count,
            kernel_l2_base,
            &contract->l1_descriptor_kernel_first_word,
            &contract->l1_descriptor_kernel_last_word,
            &contract->l1_descriptor_type_mask_seen,
            &contract->l1_descriptor_attr_mask_seen) &&
        stage77_pmap_multiwindow_verify_l1_window(
            g_stage77_pmap_multiwindow_l1,
            contract->ram_console_window_l1_first_index,
            contract->ram_console_window_l1_count,
            ram_console_l2_base,
            &contract->l1_descriptor_ram_console_first_word,
            &contract->l1_descriptor_ram_console_last_word,
            &contract->l1_descriptor_type_mask_seen,
            &contract->l1_descriptor_attr_mask_seen) &&
        stage77_pmap_multiwindow_verify_l1_window(
            g_stage77_pmap_multiwindow_l1,
            contract->device_window_l1_first_index,
            contract->device_window_l1_count,
            device_l2_base,
            &contract->l1_descriptor_device_first_word,
            &contract->l1_descriptor_device_last_word,
            &contract->l1_descriptor_type_mask_seen,
            &contract->l1_descriptor_attr_mask_seen);
    if (l1_readback_ok &&
        contract->local_l1_write_count ==
            (STAGE77_XNU_PMAP_MULTIWINDOW_DRYRUN_WINDOW_COUNT *
             STAGE77_XNU_PMAP_MULTIWINDOW_DRYRUN_L2_TABLES_PER_PAGE)) {
        contract->satisfied_mask |= STAGE77_XNU_PMAP_MULTIWINDOW_DRYRUN_SAT_L1_COARSE_DESCRIPTORS;
    } else {
        contract->failure_mask |= STAGE77_XNU_PMAP_MULTIWINDOW_DRYRUN_FAIL_L1_DESCRIPTOR;
    }

    pte_readback_ok = stage77_pmap_multiwindow_verify_pte_window(
            g_stage77_pmap_multiwindow_l2_bank, 0u,
            contract->kernel_window_phys_base, contract->pte_template_kernel,
            &contract->pte_kernel_first_word,
            &contract->pte_kernel_last_word,
            &contract->pte_type_mask_seen,
            &contract->pte_attr_mask_seen) &&
        stage77_pmap_multiwindow_verify_pte_window(
            g_stage77_pmap_multiwindow_l2_bank, 1u,
            contract->ram_console_window_phys_base, contract->pte_template_ram_console,
            &contract->pte_ram_console_first_word,
            &contract->pte_ram_console_last_word,
            &contract->pte_type_mask_seen,
            &contract->pte_attr_mask_seen) &&
        stage77_pmap_multiwindow_verify_pte_window(
            g_stage77_pmap_multiwindow_l2_bank, 2u,
            contract->device_window_phys_base, contract->pte_template_device,
            &contract->pte_device_first_word,
            &contract->pte_device_last_word,
            &contract->pte_type_mask_seen,
            &contract->pte_attr_mask_seen);
    if (contract->local_l2_pte_write_count == contract->total_l2_pte_count) {
        contract->satisfied_mask |= STAGE77_XNU_PMAP_MULTIWINDOW_DRYRUN_SAT_PTE_POPULATION;
    } else {
        contract->failure_mask |= STAGE77_XNU_PMAP_MULTIWINDOW_DRYRUN_FAIL_PTE;
    }
    if (pte_readback_ok) {
        contract->satisfied_mask |= STAGE77_XNU_PMAP_MULTIWINDOW_DRYRUN_SAT_PTE_READBACK;
    } else {
        contract->failure_mask |= STAGE77_XNU_PMAP_MULTIWINDOW_DRYRUN_FAIL_PTE;
    }
    if (contract->l1_descriptor_type_mask_seen == STAGE77_XNU_PMAP_MULTIWINDOW_DRYRUN_L1_TABLE_TYPE &&
        contract->l1_descriptor_attr_mask_seen == contract->l1_descriptor_expected_attr_mask &&
        contract->pte_type_mask_seen == STAGE77_XNU_PMAP_MULTIWINDOW_DRYRUN_PTE_TYPE_SMALL &&
        contract->pte_attr_mask_seen == contract->pte_expected_attr_mask) {
        contract->satisfied_mask |= STAGE77_XNU_PMAP_MULTIWINDOW_DRYRUN_SAT_ATTRS;
    } else {
        contract->failure_mask |= STAGE77_XNU_PMAP_MULTIWINDOW_DRYRUN_FAIL_ATTR;
    }

    contract->translation_kernel_va = contract->gVirtBase;
    contract->translation_kernel_expected_pa = stage77_pmap_multiwindow_expected_pa(
        contract->translation_kernel_va, contract->kernel_window_virt_base, contract->kernel_window_phys_base);
    if (stage77_pmap_multiwindow_translate(g_stage77_pmap_multiwindow_l1,
                                           contract->local_l2_bank_base,
                                           contract->local_l2_bank_limit,
                                           contract->translation_kernel_va,
                                           &translated) &&
        translated == contract->translation_kernel_expected_pa) {
        contract->translation_kernel_pa = translated;
        contract->translation_case_count++;
        contract->satisfied_mask |= STAGE77_XNU_PMAP_MULTIWINDOW_DRYRUN_SAT_TRANSLATION_KERNEL;
    } else {
        contract->failure_mask |= STAGE77_XNU_PMAP_MULTIWINDOW_DRYRUN_FAIL_TRANSLATION;
    }

    contract->translation_workspace_va = contract->workspace_base;
    contract->translation_workspace_expected_pa = stage77_pmap_multiwindow_expected_pa(
        contract->translation_workspace_va, contract->kernel_window_virt_base, contract->kernel_window_phys_base);
    if (stage77_pmap_multiwindow_translate(g_stage77_pmap_multiwindow_l1,
                                           contract->local_l2_bank_base,
                                           contract->local_l2_bank_limit,
                                           contract->translation_workspace_va,
                                           &translated) &&
        translated == contract->translation_workspace_expected_pa) {
        contract->translation_workspace_pa = translated;
        contract->translation_case_count++;
        contract->satisfied_mask |= STAGE77_XNU_PMAP_MULTIWINDOW_DRYRUN_SAT_TRANSLATION_WORKSPACE;
    } else {
        contract->failure_mask |= STAGE77_XNU_PMAP_MULTIWINDOW_DRYRUN_FAIL_TRANSLATION;
    }

    contract->translation_ram_console_va = RAM_CONSOLE_BASE;
    contract->translation_ram_console_expected_pa = stage77_pmap_multiwindow_expected_pa(
        contract->translation_ram_console_va,
        contract->ram_console_window_virt_base,
        contract->ram_console_window_phys_base);
    if (stage77_pmap_multiwindow_translate(g_stage77_pmap_multiwindow_l1,
                                           contract->local_l2_bank_base,
                                           contract->local_l2_bank_limit,
                                           contract->translation_ram_console_va,
                                           &translated) &&
        translated == contract->translation_ram_console_expected_pa) {
        contract->translation_ram_console_pa = translated;
        contract->translation_case_count++;
        contract->satisfied_mask |= STAGE77_XNU_PMAP_MULTIWINDOW_DRYRUN_SAT_TRANSLATION_RAM_CONSOLE;
    } else {
        contract->failure_mask |= STAGE77_XNU_PMAP_MULTIWINDOW_DRYRUN_FAIL_TRANSLATION;
    }

    contract->translation_device_va = contract->device_window_virt_base;
    contract->translation_device_expected_pa = STAGE77_XNU_PMAP_MULTIWINDOW_DRYRUN_GIC_PHYS_BASE;
    if (stage77_pmap_multiwindow_translate(g_stage77_pmap_multiwindow_l1,
                                           contract->local_l2_bank_base,
                                           contract->local_l2_bank_limit,
                                           contract->translation_device_va,
                                           &translated) &&
        translated == contract->translation_device_expected_pa) {
        contract->translation_device_pa = translated;
        contract->translation_case_count++;
        contract->satisfied_mask |= STAGE77_XNU_PMAP_MULTIWINDOW_DRYRUN_SAT_TRANSLATION_DEVICE;
    } else {
        contract->failure_mask |= STAGE77_XNU_PMAP_MULTIWINDOW_DRYRUN_FAIL_TRANSLATION;
    }

    contract->prior_page_window_virt_base = page->page_window_virt_base;
    contract->prior_page_window_phys_base = page->page_window_phys_base;
    contract->prior_page_pte_kernel_word = page->pte_kernel_word;
    contract->prior_page_pte_attr_seen = page->pte_attr_mask_seen;
    contract->prior_attr_pte_template_rwx = attr->pte_template_rwx_word;
    contract->prior_attr_pte_template_rwnx = attr->pte_template_rwnx_word;
    contract->prior_attr_wimg_io_bits = attr->wimg_io_pte_bits;
    if (contract->prior_page_window_virt_base == contract->kernel_window_virt_base &&
        contract->prior_page_window_phys_base == contract->kernel_window_phys_base &&
        contract->prior_page_pte_kernel_word == stage77_pmap_multiwindow_make_pte(
            contract->translation_kernel_expected_pa, contract->pte_template_kernel) &&
        contract->prior_page_pte_attr_seen == contract->pte_template_kernel &&
        contract->prior_attr_pte_template_rwx == 0x00000412u &&
        contract->prior_attr_pte_template_rwnx == 0x00000413u &&
        contract->prior_attr_wimg_io_bits == 0x0000000du) {
        /* Attribute and prior page readbacks are folded into SAT_ATTRS. */
    } else {
        contract->failure_mask |= STAGE77_XNU_PMAP_MULTIWINDOW_DRYRUN_FAIL_ATTR;
    }

    contract->proposed_workspace_written = attr->proposed_workspace_written |
        page->proposed_workspace_written | table->proposed_workspace_written;
    contract->live_pmap_tables_installed = attr->live_pmap_tables_installed |
        page->live_pmap_tables_installed | table->live_pmap_tables_installed;
    contract->ttbr_written = attr->ttbr_written | page->ttbr_written | table->ttbr_written;
    contract->ttbcr_written = attr->ttbcr_written | page->ttbcr_written | table->ttbcr_written;
    contract->dacr_written = attr->dacr_written | page->dacr_written | table->dacr_written;
    contract->sctlr_written = attr->sctlr_written | page->sctlr_written | table->sctlr_written;
    contract->tlbs_invalidated = attr->tlbs_invalidated | page->tlbs_invalidated | table->tlbs_invalidated;
    contract->caches_changed = attr->caches_changed | page->caches_changed | table->caches_changed |
        ttbr_rt->caches_changed;
    contract->persistent_write_attempted = attr->persistent_write_attempted | page->persistent_write_attempted |
        table->persistent_write_attempted | ttbr_rt->persistent_write_attempted;
    contract->xnu_start_executed = attr->xnu_start_executed | page->xnu_start_executed |
        table->xnu_start_executed | ttbr_rt->xnu_entry_executed;
    contract->generated_macho_executed = attr->generated_macho_executed | page->generated_macho_executed |
        table->generated_macho_executed | ttbr_rt->macho_bytes_executed;

    contract->local_l1_populated_checksum = stage77_pmap_multiwindow_buffer_checksum(
        g_stage77_pmap_multiwindow_l1, STAGE77_XNU_PMAP_MULTIWINDOW_DRYRUN_L1_ENTRY_COUNT);
    contract->local_l2_populated_checksum = stage77_pmap_multiwindow_buffer_checksum(
        g_stage77_pmap_multiwindow_l2_bank, contract->total_l2_pte_count);
    if (contract->local_l1_populated_checksum != contract->local_l1_zero_checksum &&
        contract->local_l2_populated_checksum != contract->local_l2_zero_checksum &&
        contract->local_l1_write_count == (STAGE77_XNU_PMAP_MULTIWINDOW_DRYRUN_WINDOW_COUNT *
                                           STAGE77_XNU_PMAP_MULTIWINDOW_DRYRUN_L2_TABLES_PER_PAGE) &&
        contract->local_l2_pte_write_count == contract->total_l2_pte_count &&
        contract->proposed_workspace_written == 0u) {
        contract->satisfied_mask |= STAGE77_XNU_PMAP_MULTIWINDOW_DRYRUN_SAT_LOCAL_ONLY_CHECKSUM;
    } else {
        contract->failure_mask |= STAGE77_XNU_PMAP_MULTIWINDOW_DRYRUN_FAIL_CHECKSUM;
    }

    contract->local_only = 1u;
    if (contract->public_pmap_compile_count == 0u && contract->public_pmap_link_count == 0u &&
        contract->public_pmap_execute_count == 0u && attr->public_arm_vm_init_executed == 0u &&
        attr->public_pmap_runtime_executed == 0u) {
        contract->public_arm_vm_init_executed = 0u;
        contract->public_pmap_runtime_executed = 0u;
        contract->satisfied_mask |= STAGE77_XNU_PMAP_MULTIWINDOW_DRYRUN_SAT_NO_PUBLIC_PMAP_EXEC;
    } else {
        contract->failure_mask |= STAGE77_XNU_PMAP_MULTIWINDOW_DRYRUN_FAIL_SAFETY_BOUNDARY;
    }
    if (contract->proposed_workspace_written == 0u) {
        contract->satisfied_mask |= STAGE77_XNU_PMAP_MULTIWINDOW_DRYRUN_SAT_NO_PROPOSED_WORKSPACE_WRITE;
    } else {
        contract->failure_mask |= STAGE77_XNU_PMAP_MULTIWINDOW_DRYRUN_FAIL_SAFETY_BOUNDARY;
    }
    if (contract->live_pmap_tables_installed == 0u && snapshot->no_live_pmap_tables_installed == 1u) {
        contract->satisfied_mask |= STAGE77_XNU_PMAP_MULTIWINDOW_DRYRUN_SAT_NO_LIVE_PMAP_INSTALL;
    } else {
        contract->failure_mask |= STAGE77_XNU_PMAP_MULTIWINDOW_DRYRUN_FAIL_SAFETY_BOUNDARY;
    }
    if (contract->ttbr_written == 0u && contract->ttbcr_written == 0u &&
        contract->dacr_written == 0u && contract->sctlr_written == 0u) {
        contract->satisfied_mask |= STAGE77_XNU_PMAP_MULTIWINDOW_DRYRUN_SAT_NO_CONTROL_REG_WRITE;
    } else {
        contract->failure_mask |= STAGE77_XNU_PMAP_MULTIWINDOW_DRYRUN_FAIL_SAFETY_BOUNDARY;
    }
    if (contract->tlbs_invalidated == 0u) {
        contract->satisfied_mask |= STAGE77_XNU_PMAP_MULTIWINDOW_DRYRUN_SAT_NO_TLB_INVALIDATE;
    } else {
        contract->failure_mask |= STAGE77_XNU_PMAP_MULTIWINDOW_DRYRUN_FAIL_SAFETY_BOUNDARY;
    }
    if (contract->caches_changed == 0u &&
        (preflight->safety_mask & STAGE77_LOADER_SAFETY_NO_CACHE_CHANGE) != 0u) {
        contract->satisfied_mask |= STAGE77_XNU_PMAP_MULTIWINDOW_DRYRUN_SAT_NO_CACHE_CHANGE;
    } else {
        contract->failure_mask |= STAGE77_XNU_PMAP_MULTIWINDOW_DRYRUN_FAIL_SAFETY_BOUNDARY;
    }
    if (contract->xnu_start_executed == 0u && contract->generated_macho_executed == 0u &&
        (preflight->safety_mask & STAGE77_LOADER_SAFETY_NO_EXECUTE) != 0u) {
        contract->satisfied_mask |= STAGE77_XNU_PMAP_MULTIWINDOW_DRYRUN_SAT_NO_XNU_MACHO_EXEC;
    } else {
        contract->failure_mask |= STAGE77_XNU_PMAP_MULTIWINDOW_DRYRUN_FAIL_SAFETY_BOUNDARY;
    }
    if (contract->persistent_write_attempted == 0u &&
        (preflight->safety_mask & STAGE77_LOADER_SAFETY_NO_PERSIST_WRITE) != 0u) {
        contract->satisfied_mask |= STAGE77_XNU_PMAP_MULTIWINDOW_DRYRUN_SAT_NO_PERSIST_WRITE;
    } else {
        contract->failure_mask |= STAGE77_XNU_PMAP_MULTIWINDOW_DRYRUN_FAIL_SAFETY_BOUNDARY;
    }

    if (preflight->xnu_compile_graph.fail_closed == 1u &&
        preflight->xnu_object_subset.fail_closed == 1u &&
        preflight->xnu_link.fail_closed == 1u &&
        table->fail_closed == 1u && page->fail_closed == 1u && attr->fail_closed == 1u) {
        contract->fail_closed = 1u;
        contract->satisfied_mask |= STAGE77_XNU_PMAP_MULTIWINDOW_DRYRUN_SAT_FAIL_CLOSED;
    } else {
        contract->failure_mask |= STAGE77_XNU_PMAP_MULTIWINDOW_DRYRUN_FAIL_SAFETY_BOUNDARY;
    }

finish:
    if (contract->satisfied_mask == contract->required_mask && contract->failure_mask == 0u) {
        contract->status = STAGE77_STATUS_OK;
        xnu_log_puts("Stage77 XNU pmap multi-window page-granular dry-run contract ok\n");
    } else {
        contract->status = STAGE77_STATUS_BASE | contract->failure_mask;
        xnu_log_puts("Stage77 XNU pmap multi-window page-granular dry-run contract failed\n");
    }
    contract->checksum = stage77_pmap_multiwindow_dryrun_checksum(contract);
    if (contract->checksum != stage77_pmap_multiwindow_dryrun_checksum(contract)) {
        contract->failure_mask |= STAGE77_XNU_PMAP_MULTIWINDOW_DRYRUN_FAIL_CHECKSUM;
        contract->status = STAGE77_STATUS_BASE | contract->failure_mask;
        contract->checksum = stage77_pmap_multiwindow_dryrun_checksum(contract);
    }

    stage77_xnu_pmap_multiwindow_dryrun_contract_log(contract);
    return contract->status == STAGE77_STATUS_OK;
}

const struct stage77_xnu_pmap_multiwindow_dryrun_contract *stage77_xnu_pmap_multiwindow_dryrun_contract_result(void)
{
    return &g_stage77_xnu_pmap_multiwindow_dryrun_contract;
}
