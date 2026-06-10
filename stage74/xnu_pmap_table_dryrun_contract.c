#include "stage74.h"

/*
 * Stage74 XNU pmap table population dry-run contract.
 *
 * This is a Stage-owned, target-side proof that the early pmap L1 section table
 * can be planned, populated, read back, translated, checksummed and bounded in
 * a LOCAL Stage-owned scratch buffer only.  It imports the Stage57/Stage74
 * bootstrap tuple, the pmap/bootstrap allocation contract, and the pmap
 * bootstrap snapshot, then simulates section-descriptor population without
 * writing the proposed pmap workspace, without installing any live XNU/pmap
 * tables, without touching TTBR/TTBCR/DACR/SCTLR, and without invalidating
 * TLBs.  No public arm_vm_init.c/pmap.c runtime is compiled, linked or run.
 */

#define STAGE74_PMAP_DRYRUN_LOWMEM_ATTR_MASK 0x000fffffu

static struct stage74_xnu_pmap_table_dryrun_contract g_stage74_xnu_pmap_table_dryrun_contract;

/* Local Stage-owned simulation buffer: never the proposed pmap workspace. */
static uint32_t g_stage74_pmap_dryrun_l1[STAGE74_XNU_PMAP_TABLE_DRYRUN_L1_ENTRY_COUNT]
    __attribute__((aligned(STAGE74_XNU_PMAP_TABLE_DRYRUN_L1_ALIGNMENT)));

static uint32_t stage74_pmap_dryrun_checksum(volatile const struct stage74_xnu_pmap_table_dryrun_contract *contract)
{
    volatile const uint32_t *words = (volatile const uint32_t *)(uintptr_t)contract;
    uint32_t count = (uint32_t)(offsetof(struct stage74_xnu_pmap_table_dryrun_contract, checksum) / sizeof(uint32_t));
    uint32_t checksum = 0u;

    for (uint32_t i = 0; i < count; i++) {
        checksum ^= words[i];
    }

    return checksum;
}

static uint32_t stage74_pmap_dryrun_buffer_checksum(const uint32_t *buf, uint32_t count)
{
    uint32_t checksum = 0u;

    for (uint32_t i = 0; i < count; i++) {
        checksum ^= buf[i] + (i * 0x9e3779b9u);
    }

    return checksum;
}

static uint32_t stage74_pmap_dryrun_section_index(uint32_t addr)
{
    return addr >> 20;
}

static uint32_t stage74_pmap_dryrun_make_descriptor(uint32_t phys_base)
{
    return (phys_base & STAGE74_XNU_PMAP_TABLE_DRYRUN_DESC_BASE_MASK) |
           STAGE74_XNU_PMAP_TABLE_DRYRUN_DESC_SECTION_SO;
}

static uint32_t stage74_pmap_dryrun_map_range(uint32_t *l1, uint32_t virt_base,
                                              uint32_t phys_base, uint32_t size)
{
    uint32_t mapped = 0u;
    uint32_t offset;

    if (size == 0u) {
        return 0u;
    }
    if ((virt_base & STAGE74_XNU_PMAP_TABLE_DRYRUN_SECTION_OFFSET_MASK) != 0u ||
        (phys_base & STAGE74_XNU_PMAP_TABLE_DRYRUN_SECTION_OFFSET_MASK) != 0u) {
        return 0u;
    }
    for (offset = 0u; offset < size; offset += STAGE74_XNU_PMAP_TABLE_DRYRUN_SECTION_SIZE) {
        uint32_t va = virt_base + offset;
        uint32_t pa = phys_base + offset;
        uint32_t idx = stage74_pmap_dryrun_section_index(va);

        if (idx >= STAGE74_XNU_PMAP_TABLE_DRYRUN_L1_ENTRY_COUNT) {
            return 0u;
        }
        l1[idx] = stage74_pmap_dryrun_make_descriptor(pa);
        mapped++;
    }

    return mapped;
}

static uint32_t stage74_pmap_dryrun_translate(const uint32_t *l1, uint32_t va, uint32_t *pa_out)
{
    uint32_t idx = stage74_pmap_dryrun_section_index(va);
    uint32_t desc;

    if (idx >= STAGE74_XNU_PMAP_TABLE_DRYRUN_L1_ENTRY_COUNT) {
        return 0u;
    }
    desc = l1[idx];
    if ((desc & STAGE74_XNU_PMAP_TABLE_DRYRUN_DESC_TYPE_MASK) !=
        STAGE74_XNU_PMAP_TABLE_DRYRUN_DESC_TYPE_SECTION) {
        return 0u;
    }
    *pa_out = (desc & STAGE74_XNU_PMAP_TABLE_DRYRUN_DESC_BASE_MASK) |
              (va & STAGE74_XNU_PMAP_TABLE_DRYRUN_SECTION_OFFSET_MASK);
    return 1u;
}

static uint32_t stage74_pmap_dryrun_add_overflow_u32(uint32_t a, uint32_t b, uint32_t *out)
{
    *out = a + b;
    return *out < a;
}

static uint32_t stage74_pmap_dryrun_range_indices(uint32_t base, uint32_t size,
                                                  uint32_t *first, uint32_t *last,
                                                  uint32_t *count)
{
    uint32_t limit;

    *first = 0u;
    *last = 0u;
    *count = 0u;
    if (size == 0u || stage74_pmap_dryrun_add_overflow_u32(base, size, &limit)) {
        return 0u;
    }
    *first = stage74_pmap_dryrun_section_index(base);
    *last = stage74_pmap_dryrun_section_index(limit - 1u);
    if (*first > *last || *last >= STAGE74_XNU_PMAP_TABLE_DRYRUN_L1_ENTRY_COUNT) {
        return 0u;
    }
    *count = *last - *first + 1u;
    return 1u;
}

static uint32_t stage74_pmap_dryrun_descriptor_attrs_ok(uint32_t word)
{
    if ((word & STAGE74_XNU_PMAP_TABLE_DRYRUN_DESC_TYPE_MASK) !=
        STAGE74_XNU_PMAP_TABLE_DRYRUN_DESC_TYPE_SECTION) {
        return 0u;
    }
    return ((word & STAGE74_XNU_PMAP_TABLE_DRYRUN_DESC_ATTR_MASK) ==
            (STAGE74_XNU_PMAP_TABLE_DRYRUN_DESC_SECTION_SO &
             STAGE74_XNU_PMAP_TABLE_DRYRUN_DESC_ATTR_MASK)) ? 1u : 0u;
}

static void stage74_xnu_pmap_table_dryrun_contract_log(
    const struct stage74_xnu_pmap_table_dryrun_contract *contract)
{
    xnu_log_kv32("stage74_xnu_pmap_table_dryrun_contract_status", contract->status);
    xnu_log_kv32("stage74_xnu_pmap_table_dryrun_contract_required_mask", contract->required_mask);
    xnu_log_kv32("stage74_xnu_pmap_table_dryrun_contract_satisfied_mask", contract->satisfied_mask);
    xnu_log_kv32("stage74_xnu_pmap_table_dryrun_contract_failure_mask", contract->failure_mask);
    xnu_log_kv32("stage74_xnu_pmap_table_dryrun_contract_checksum", contract->checksum);
    xnu_log_kv32("stage74_xnu_pmap_table_dryrun_source_pmap_bootstrap_status", contract->source_pmap_bootstrap_status);
    xnu_log_kv32("stage74_xnu_pmap_table_dryrun_source_pmap_bootstrap_satisfied_mask", contract->source_pmap_bootstrap_satisfied_mask);
    xnu_log_kv32("stage74_xnu_pmap_table_dryrun_source_pmap_bootstrap_failure_mask", contract->source_pmap_bootstrap_failure_mask);
    xnu_log_kv32("stage74_xnu_pmap_table_dryrun_source_snapshot_status", contract->source_snapshot_status);
    xnu_log_kv32("stage74_xnu_pmap_table_dryrun_source_snapshot_satisfied_mask", contract->source_snapshot_satisfied_mask);
    xnu_log_kv32("stage74_xnu_pmap_table_dryrun_source_snapshot_failure_mask", contract->source_snapshot_failure_mask);
    xnu_log_kv32("stage74_xnu_pmap_table_dryrun_l1_entry_count", contract->l1_entry_count);
    xnu_log_kv32("stage74_xnu_pmap_table_dryrun_l1_bytes", contract->l1_bytes);
    xnu_log_kv32("stage74_xnu_pmap_table_dryrun_section_size", contract->section_size);
    xnu_log_kv32("stage74_xnu_pmap_table_dryrun_section_descriptor", contract->section_descriptor);
    xnu_log_kv32("stage74_xnu_pmap_table_dryrun_local_l1_base", contract->local_l1_base);
    xnu_log_kv32("stage74_xnu_pmap_table_dryrun_local_l1_limit", contract->local_l1_limit);
    xnu_log_kv32("stage74_xnu_pmap_table_dryrun_local_l1_zero_checksum", contract->local_l1_zero_checksum);
    xnu_log_kv32("stage74_xnu_pmap_table_dryrun_local_l1_populated_checksum", contract->local_l1_populated_checksum);
    xnu_log_kv32("stage74_xnu_pmap_table_dryrun_local_l1_write_count", contract->local_l1_write_count);
    xnu_log_kv32("stage74_xnu_pmap_table_dryrun_gVirtBase", contract->gVirtBase);
    xnu_log_kv32("stage74_xnu_pmap_table_dryrun_gPhysBase", contract->gPhysBase);
    xnu_log_kv32("stage74_xnu_pmap_table_dryrun_gPhysSize", contract->gPhysSize);
    xnu_log_kv32("stage74_xnu_pmap_table_dryrun_boot_ttep", contract->boot_ttep);
    xnu_log_kv32("stage74_xnu_pmap_table_dryrun_cpu_ttep", contract->cpu_ttep);
    xnu_log_kv32("stage74_xnu_pmap_table_dryrun_initial_avail_start", contract->initial_avail_start);
    xnu_log_kv32("stage74_xnu_pmap_table_dryrun_avail_end", contract->avail_end);
    xnu_log_kv32("stage74_xnu_pmap_table_dryrun_vstart", contract->vstart);
    xnu_log_kv32("stage74_xnu_pmap_table_dryrun_virtual_space_end", contract->virtual_space_end);
    xnu_log_kv32("stage74_xnu_pmap_table_dryrun_lowmem_l1_first_index", contract->lowmem_l1_first_index);
    xnu_log_kv32("stage74_xnu_pmap_table_dryrun_lowmem_l1_last_index", contract->lowmem_l1_last_index);
    xnu_log_kv32("stage74_xnu_pmap_table_dryrun_lowmem_l1_section_count", contract->lowmem_l1_section_count);
    xnu_log_kv32("stage74_xnu_pmap_table_dryrun_kernel_l1_first_index", contract->kernel_l1_first_index);
    xnu_log_kv32("stage74_xnu_pmap_table_dryrun_kernel_l1_last_index", contract->kernel_l1_last_index);
    xnu_log_kv32("stage74_xnu_pmap_table_dryrun_kernel_l1_section_count", contract->kernel_l1_section_count);
    xnu_log_kv32("stage74_xnu_pmap_table_dryrun_workspace_l1_first_index", contract->workspace_l1_first_index);
    xnu_log_kv32("stage74_xnu_pmap_table_dryrun_workspace_l1_last_index", contract->workspace_l1_last_index);
    xnu_log_kv32("stage74_xnu_pmap_table_dryrun_workspace_l1_section_count", contract->workspace_l1_section_count);
    xnu_log_kv32("stage74_xnu_pmap_table_dryrun_ram_console_l1_first_index", contract->ram_console_l1_first_index);
    xnu_log_kv32("stage74_xnu_pmap_table_dryrun_ram_console_l1_last_index", contract->ram_console_l1_last_index);
    xnu_log_kv32("stage74_xnu_pmap_table_dryrun_ram_console_l1_section_count", contract->ram_console_l1_section_count);
    xnu_log_kv32("stage74_xnu_pmap_table_dryrun_descriptor_lowmem_first_word", contract->descriptor_lowmem_first_word);
    xnu_log_kv32("stage74_xnu_pmap_table_dryrun_descriptor_lowmem_last_word", contract->descriptor_lowmem_last_word);
    xnu_log_kv32("stage74_xnu_pmap_table_dryrun_descriptor_kernel_first_word", contract->descriptor_kernel_first_word);
    xnu_log_kv32("stage74_xnu_pmap_table_dryrun_descriptor_kernel_last_word", contract->descriptor_kernel_last_word);
    xnu_log_kv32("stage74_xnu_pmap_table_dryrun_descriptor_workspace_first_word", contract->descriptor_workspace_first_word);
    xnu_log_kv32("stage74_xnu_pmap_table_dryrun_descriptor_workspace_last_word", contract->descriptor_workspace_last_word);
    xnu_log_kv32("stage74_xnu_pmap_table_dryrun_descriptor_ram_console_first_word", contract->descriptor_ram_console_first_word);
    xnu_log_kv32("stage74_xnu_pmap_table_dryrun_descriptor_ram_console_last_word", contract->descriptor_ram_console_last_word);
    xnu_log_kv32("stage74_xnu_pmap_table_dryrun_descriptor_type_mask_seen", contract->descriptor_type_mask_seen);
    xnu_log_kv32("stage74_xnu_pmap_table_dryrun_descriptor_attr_mask_seen", contract->descriptor_attr_mask_seen);
    xnu_log_kv32("stage74_xnu_pmap_table_dryrun_descriptor_expected_attr_mask", contract->descriptor_expected_attr_mask);
    xnu_log_kv32("stage74_xnu_pmap_table_dryrun_translation_kernel_va", contract->translation_kernel_va);
    xnu_log_kv32("stage74_xnu_pmap_table_dryrun_translation_kernel_pa", contract->translation_kernel_pa);
    xnu_log_kv32("stage74_xnu_pmap_table_dryrun_translation_kernel_expected_pa", contract->translation_kernel_expected_pa);
    xnu_log_kv32("stage74_xnu_pmap_table_dryrun_translation_lowmem_va", contract->translation_lowmem_va);
    xnu_log_kv32("stage74_xnu_pmap_table_dryrun_translation_lowmem_pa", contract->translation_lowmem_pa);
    xnu_log_kv32("stage74_xnu_pmap_table_dryrun_translation_workspace_va", contract->translation_workspace_va);
    xnu_log_kv32("stage74_xnu_pmap_table_dryrun_translation_workspace_pa", contract->translation_workspace_pa);
    xnu_log_kv32("stage74_xnu_pmap_table_dryrun_translation_ram_console_va", contract->translation_ram_console_va);
    xnu_log_kv32("stage74_xnu_pmap_table_dryrun_translation_ram_console_pa", contract->translation_ram_console_pa);
    xnu_log_kv32("stage74_xnu_pmap_table_dryrun_translation_case_count", contract->translation_case_count);
    xnu_log_kv32("stage74_xnu_pmap_table_dryrun_workspace_base", contract->workspace_base);
    xnu_log_kv32("stage74_xnu_pmap_table_dryrun_workspace_limit", contract->workspace_limit);
    xnu_log_kv32("stage74_xnu_pmap_table_dryrun_workspace_size", contract->workspace_size);
    xnu_log_kv32("stage74_xnu_pmap_table_dryrun_workspace_l1_table_phys", contract->workspace_l1_table_phys);
    xnu_log_kv32("stage74_xnu_pmap_table_dryrun_workspace_l1_table_virt", contract->workspace_l1_table_virt);
    xnu_log_kv32("stage74_xnu_pmap_table_dryrun_workspace_l1_table_local_distinct", contract->workspace_l1_table_local_distinct);
    xnu_log_kv32("stage74_xnu_pmap_table_dryrun_public_pmap_compile_count", contract->public_pmap_compile_count);
    xnu_log_kv32("stage74_xnu_pmap_table_dryrun_public_pmap_link_count", contract->public_pmap_link_count);
    xnu_log_kv32("stage74_xnu_pmap_table_dryrun_public_pmap_execute_count", contract->public_pmap_execute_count);
    xnu_log_kv32("stage74_xnu_pmap_table_dryrun_proposed_workspace_written", contract->proposed_workspace_written);
    xnu_log_kv32("stage74_xnu_pmap_table_dryrun_live_pmap_tables_installed", contract->live_pmap_tables_installed);
    xnu_log_kv32("stage74_xnu_pmap_table_dryrun_ttbr_written", contract->ttbr_written);
    xnu_log_kv32("stage74_xnu_pmap_table_dryrun_ttbcr_written", contract->ttbcr_written);
    xnu_log_kv32("stage74_xnu_pmap_table_dryrun_dacr_written", contract->dacr_written);
    xnu_log_kv32("stage74_xnu_pmap_table_dryrun_sctlr_written", contract->sctlr_written);
    xnu_log_kv32("stage74_xnu_pmap_table_dryrun_tlbs_invalidated", contract->tlbs_invalidated);
    xnu_log_kv32("stage74_xnu_pmap_table_dryrun_public_arm_vm_init_executed", contract->public_arm_vm_init_executed);
    xnu_log_kv32("stage74_xnu_pmap_table_dryrun_xnu_start_executed", contract->xnu_start_executed);
    xnu_log_kv32("stage74_xnu_pmap_table_dryrun_generated_macho_executed", contract->generated_macho_executed);
    xnu_log_kv32("stage74_xnu_pmap_table_dryrun_persistent_write_attempted", contract->persistent_write_attempted);
    xnu_log_kv32("stage74_xnu_pmap_table_dryrun_caches_changed", contract->caches_changed);
    xnu_log_kv32("stage74_xnu_pmap_table_dryrun_fail_closed", contract->fail_closed);
}


int stage74_xnu_pmap_table_dryrun_contract_selftest(const struct stage74_loader_preflight *preflight)
{
    struct stage74_xnu_pmap_table_dryrun_contract *contract = &g_stage74_xnu_pmap_table_dryrun_contract;
    const struct stage74_xnu_pmap_bootstrap_contract *pmap = stage74_xnu_pmap_bootstrap_contract_result();
    const struct stage74_pmap_bootstrap_snapshot *snapshot = mmu_stage74_pmap_bootstrap_snapshot_result();
    const struct stage74_ttbr0_roundtrip *ttbr_rt = mmu_stage74_ttbr0_roundtrip_result();
    uint32_t local_l1_limit = 0u;
    uint32_t lowmem_writes = 0u;
    uint32_t kernel_writes = 0u;
    uint32_t workspace_writes = 0u;
    uint32_t ram_console_writes = 0u;
    uint32_t kernel_base_va = 0u;
    uint32_t kernel_base_pa = 0u;
    uint32_t source_rollups_ok = 0u;
    uint32_t translated = 0u;

    memset(contract, 0, sizeof(*contract));
    memset(g_stage74_pmap_dryrun_l1, 0, sizeof(g_stage74_pmap_dryrun_l1));

    contract->version = STAGE74_XNU_PMAP_TABLE_DRYRUN_CONTRACT_VERSION;
    contract->size = sizeof(*contract);
    contract->status = STAGE74_STATUS_BASE;
    contract->required_mask = STAGE74_XNU_PMAP_TABLE_DRYRUN_REQUIRED_MASK;
    contract->page_size = STAGE74_XNU_PMAP_BOOTSTRAP_PAGE_SIZE;
    contract->l1_entry_count = STAGE74_XNU_PMAP_TABLE_DRYRUN_L1_ENTRY_COUNT;
    contract->l1_bytes = STAGE74_XNU_PMAP_TABLE_DRYRUN_L1_BYTES;
    contract->l1_alignment = STAGE74_XNU_PMAP_TABLE_DRYRUN_L1_ALIGNMENT;
    contract->section_size = STAGE74_XNU_PMAP_TABLE_DRYRUN_SECTION_SIZE;
    contract->section_descriptor = STAGE74_XNU_PMAP_TABLE_DRYRUN_DESC_SECTION_SO;
    contract->desc_type_mask = STAGE74_XNU_PMAP_TABLE_DRYRUN_DESC_TYPE_MASK;
    contract->desc_attr_mask = STAGE74_XNU_PMAP_TABLE_DRYRUN_DESC_ATTR_MASK;
    contract->desc_base_mask = STAGE74_XNU_PMAP_TABLE_DRYRUN_DESC_BASE_MASK;
    contract->descriptor_expected_attr_mask = STAGE74_XNU_PMAP_TABLE_DRYRUN_DESC_SECTION_SO &
        STAGE74_XNU_PMAP_TABLE_DRYRUN_DESC_ATTR_MASK;
    contract->local_l1_base = (uint32_t)(uintptr_t)g_stage74_pmap_dryrun_l1;
    contract->local_l1_bytes = sizeof(g_stage74_pmap_dryrun_l1);
    contract->local_l1_alignment = contract->local_l1_base & (STAGE74_XNU_PMAP_TABLE_DRYRUN_L1_ALIGNMENT - 1u);
    contract->local_l1_zero_checksum = stage74_pmap_dryrun_buffer_checksum(
        g_stage74_pmap_dryrun_l1, STAGE74_XNU_PMAP_TABLE_DRYRUN_L1_ENTRY_COUNT);

    xnu_log_puts("Stage74 XNU pmap table population dry-run contract begin\n");

    if (!preflight || !pmap || !snapshot || !ttbr_rt) {
        contract->failure_mask |= STAGE74_XNU_PMAP_TABLE_DRYRUN_FAIL_SOURCE |
                                  STAGE74_XNU_PMAP_TABLE_DRYRUN_FAIL_SAFETY_BOUNDARY;
        goto finish;
    }

    contract->source_pmap_bootstrap_status = pmap->status;
    contract->source_pmap_bootstrap_required_mask = pmap->required_mask;
    contract->source_pmap_bootstrap_satisfied_mask = pmap->satisfied_mask;
    contract->source_pmap_bootstrap_failure_mask = pmap->failure_mask;
    contract->source_pmap_bootstrap_checksum = pmap->checksum;
    contract->source_snapshot_status = snapshot->status;
    contract->source_snapshot_required_mask = snapshot->required_mask;
    contract->source_snapshot_satisfied_mask = snapshot->satisfied_mask;
    contract->source_snapshot_failure_mask = snapshot->failure_mask;
    contract->source_snapshot_checksum = snapshot->checksum;
    contract->source_bootstrap_status = preflight->xnu_bootstrap_contract_status;
    contract->source_tte_dryrun_status = preflight->tte_dryrun_status;
    contract->source_safe_table_status = preflight->safe_table_status;
    contract->source_stage_owned_tables_status = preflight->stage_owned_tables_status;
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

    source_rollups_ok = (contract->source_pmap_bootstrap_status == STAGE74_STATUS_OK &&
                         contract->source_pmap_bootstrap_satisfied_mask == contract->source_pmap_bootstrap_required_mask &&
                         contract->source_pmap_bootstrap_failure_mask == 0u &&
                         contract->source_snapshot_status == STAGE74_STATUS_OK &&
                         contract->source_snapshot_satisfied_mask == contract->source_snapshot_required_mask &&
                         contract->source_snapshot_failure_mask == 0u &&
                         contract->source_bootstrap_status == STAGE74_STATUS_OK &&
                         contract->source_tte_dryrun_status == STAGE74_STATUS_OK &&
                         contract->source_safe_table_status == STAGE74_STATUS_OK &&
                         contract->source_stage_owned_tables_status == STAGE74_STATUS_OK &&
                         contract->source_ttbr_roundtrip_status == STAGE74_STATUS_OK &&
                         contract->source_ttbr_restored_status == STAGE74_STATUS_OK &&
                         contract->source_cache_preserved_status == STAGE74_STATUS_OK &&
                         contract->source_compile_graph_status == STAGE74_STATUS_OK &&
                         contract->source_object_subset_status == STAGE74_STATUS_OK &&
                         contract->source_link_status == STAGE74_STATUS_OK &&
                         preflight->xnu_compile_graph_failure_mask == 0u &&
                         preflight->xnu_object_subset_failure_mask == 0u &&
                         preflight->xnu_link_failure_mask == 0u) ? 1u : 0u;

    if (contract->source_pmap_bootstrap_status == STAGE74_STATUS_OK &&
        contract->source_pmap_bootstrap_satisfied_mask == contract->source_pmap_bootstrap_required_mask &&
        contract->source_pmap_bootstrap_failure_mask == 0u) {
        contract->satisfied_mask |= STAGE74_XNU_PMAP_TABLE_DRYRUN_SAT_SOURCE_PMAP_BOOTSTRAP;
    } else {
        contract->failure_mask |= STAGE74_XNU_PMAP_TABLE_DRYRUN_FAIL_SOURCE;
    }
    if (contract->source_snapshot_status == STAGE74_STATUS_OK &&
        contract->source_snapshot_satisfied_mask == contract->source_snapshot_required_mask &&
        contract->source_snapshot_failure_mask == 0u &&
        snapshot->stage_owned_snapshot == 1u && snapshot->no_live_pmap_tables_installed == 1u) {
        contract->satisfied_mask |= STAGE74_XNU_PMAP_TABLE_DRYRUN_SAT_SOURCE_SNAPSHOT;
    } else {
        contract->failure_mask |= STAGE74_XNU_PMAP_TABLE_DRYRUN_FAIL_SOURCE;
    }
    if (source_rollups_ok) {
        contract->satisfied_mask |= STAGE74_XNU_PMAP_TABLE_DRYRUN_SAT_SOURCE_ROLLUPS;
    } else {
        contract->failure_mask |= STAGE74_XNU_PMAP_TABLE_DRYRUN_FAIL_SOURCE;
    }

    if (contract->page_size == 4096u &&
        contract->l1_entry_count == 4096u &&
        contract->l1_bytes == 0x00004000u &&
        contract->l1_alignment == 0x00004000u &&
        contract->section_size == 0x00100000u &&
        contract->section_descriptor == 0x00010c02u &&
        contract->desc_type_mask == 0x00000003u &&
        contract->desc_base_mask == 0xfff00000u &&
        contract->gPhysBase == RAM_PHYS_BASE &&
        contract->gPhysSize == (RAM_CONSOLE_BASE - RAM_PHYS_BASE) &&
        contract->avail_end == RAM_CONSOLE_BASE &&
        contract->virtual_space_end == STAGE74_XNU_PMAP_BOOTSTRAP_VM_MAX_KERNEL_ADDRESS) {
        contract->satisfied_mask |= STAGE74_XNU_PMAP_TABLE_DRYRUN_SAT_CONSTANTS;
    } else {
        contract->failure_mask |= STAGE74_XNU_PMAP_TABLE_DRYRUN_FAIL_CONSTANTS;
    }

    if (!stage74_pmap_dryrun_add_overflow_u32(contract->local_l1_base, contract->local_l1_bytes, &local_l1_limit)) {
        contract->local_l1_limit = local_l1_limit;
    }
    if (contract->local_l1_base != 0u &&
        contract->local_l1_limit > contract->local_l1_base &&
        contract->local_l1_bytes == STAGE74_XNU_PMAP_TABLE_DRYRUN_L1_BYTES &&
        contract->local_l1_alignment == 0u &&
        contract->local_l1_base != contract->workspace_l1_table_phys &&
        contract->local_l1_base != contract->workspace_base &&
        contract->local_l1_limit <= RAM_PHYS_BASE) {
        contract->workspace_l1_table_local_distinct = 1u;
        contract->satisfied_mask |= STAGE74_XNU_PMAP_TABLE_DRYRUN_SAT_LOCAL_BUFFER;
    } else {
        contract->failure_mask |= STAGE74_XNU_PMAP_TABLE_DRYRUN_FAIL_LOCAL_BUFFER;
    }
    if (contract->local_l1_zero_checksum == stage74_pmap_dryrun_buffer_checksum(
            g_stage74_pmap_dryrun_l1, STAGE74_XNU_PMAP_TABLE_DRYRUN_L1_ENTRY_COUNT)) {
        contract->satisfied_mask |= STAGE74_XNU_PMAP_TABLE_DRYRUN_SAT_LOCAL_ZERO;
    } else {
        contract->failure_mask |= STAGE74_XNU_PMAP_TABLE_DRYRUN_FAIL_LOCAL_BUFFER;
    }

    if (stage74_pmap_dryrun_range_indices(contract->gPhysBase, contract->gPhysSize,
                                          &contract->lowmem_l1_first_index,
                                          &contract->lowmem_l1_last_index,
                                          &contract->lowmem_l1_section_count)) {
        lowmem_writes = stage74_pmap_dryrun_map_range(g_stage74_pmap_dryrun_l1,
                                                      contract->gPhysBase,
                                                      contract->gPhysBase,
                                                      contract->gPhysSize);
        if (lowmem_writes == contract->lowmem_l1_section_count) {
            contract->satisfied_mask |= STAGE74_XNU_PMAP_TABLE_DRYRUN_SAT_LOW_MEMORY_RANGE;
        } else {
            contract->failure_mask |= STAGE74_XNU_PMAP_TABLE_DRYRUN_FAIL_RANGE;
        }
    } else {
        contract->failure_mask |= STAGE74_XNU_PMAP_TABLE_DRYRUN_FAIL_RANGE;
    }

    kernel_base_va = contract->gVirtBase & STAGE74_XNU_PMAP_TABLE_DRYRUN_DESC_BASE_MASK;
    kernel_base_pa = contract->gPhysBase & STAGE74_XNU_PMAP_TABLE_DRYRUN_DESC_BASE_MASK;
    if (stage74_pmap_dryrun_range_indices(kernel_base_va, STAGE74_XNU_PMAP_TABLE_DRYRUN_SECTION_SIZE,
                                          &contract->kernel_l1_first_index,
                                          &contract->kernel_l1_last_index,
                                          &contract->kernel_l1_section_count)) {
        kernel_writes = stage74_pmap_dryrun_map_range(g_stage74_pmap_dryrun_l1,
                                                      kernel_base_va,
                                                      kernel_base_pa,
                                                      STAGE74_XNU_PMAP_TABLE_DRYRUN_SECTION_SIZE);
        if (kernel_writes == contract->kernel_l1_section_count) {
            contract->satisfied_mask |= STAGE74_XNU_PMAP_TABLE_DRYRUN_SAT_KERNEL_RANGE;
        } else {
            contract->failure_mask |= STAGE74_XNU_PMAP_TABLE_DRYRUN_FAIL_RANGE;
        }
    } else {
        contract->failure_mask |= STAGE74_XNU_PMAP_TABLE_DRYRUN_FAIL_RANGE;
    }

    if (stage74_pmap_dryrun_range_indices(contract->workspace_base, contract->workspace_size,
                                          &contract->workspace_l1_first_index,
                                          &contract->workspace_l1_last_index,
                                          &contract->workspace_l1_section_count)) {
        workspace_writes = stage74_pmap_dryrun_map_range(g_stage74_pmap_dryrun_l1,
                                                         contract->workspace_base,
                                                         contract->workspace_base,
                                                         contract->workspace_size);
        if (workspace_writes == contract->workspace_l1_section_count &&
            contract->workspace_base == RAM_PHYS_BASE &&
            contract->workspace_size == STAGE74_XNU_PMAP_TABLE_DRYRUN_SECTION_SIZE) {
            contract->satisfied_mask |= STAGE74_XNU_PMAP_TABLE_DRYRUN_SAT_WORKSPACE_RANGE;
        } else {
            contract->failure_mask |= STAGE74_XNU_PMAP_TABLE_DRYRUN_FAIL_RANGE;
        }
    } else {
        contract->failure_mask |= STAGE74_XNU_PMAP_TABLE_DRYRUN_FAIL_RANGE;
    }

    if (stage74_pmap_dryrun_range_indices(RAM_CONSOLE_BASE, RAM_CONSOLE_SIZE,
                                          &contract->ram_console_l1_first_index,
                                          &contract->ram_console_l1_last_index,
                                          &contract->ram_console_l1_section_count)) {
        ram_console_writes = stage74_pmap_dryrun_map_range(g_stage74_pmap_dryrun_l1,
                                                           RAM_CONSOLE_BASE,
                                                           RAM_CONSOLE_BASE,
                                                           RAM_CONSOLE_SIZE);
        if (ram_console_writes == contract->ram_console_l1_section_count) {
            contract->satisfied_mask |= STAGE74_XNU_PMAP_TABLE_DRYRUN_SAT_RAM_CONSOLE_RANGE;
        } else {
            contract->failure_mask |= STAGE74_XNU_PMAP_TABLE_DRYRUN_FAIL_RANGE;
        }
    } else {
        contract->failure_mask |= STAGE74_XNU_PMAP_TABLE_DRYRUN_FAIL_RANGE;
    }

    contract->local_l1_write_count = lowmem_writes + kernel_writes + workspace_writes + ram_console_writes;

    contract->descriptor_lowmem_first_word = g_stage74_pmap_dryrun_l1[contract->lowmem_l1_first_index];
    contract->descriptor_lowmem_last_word = g_stage74_pmap_dryrun_l1[contract->lowmem_l1_last_index];
    contract->descriptor_kernel_first_word = g_stage74_pmap_dryrun_l1[contract->kernel_l1_first_index];
    contract->descriptor_kernel_last_word = g_stage74_pmap_dryrun_l1[contract->kernel_l1_last_index];
    contract->descriptor_workspace_first_word = g_stage74_pmap_dryrun_l1[contract->workspace_l1_first_index];
    contract->descriptor_workspace_last_word = g_stage74_pmap_dryrun_l1[contract->workspace_l1_last_index];
    contract->descriptor_ram_console_first_word = g_stage74_pmap_dryrun_l1[contract->ram_console_l1_first_index];
    contract->descriptor_ram_console_last_word = g_stage74_pmap_dryrun_l1[contract->ram_console_l1_last_index];
    contract->descriptor_type_mask_seen =
        (contract->descriptor_lowmem_first_word & STAGE74_XNU_PMAP_TABLE_DRYRUN_DESC_TYPE_MASK) |
        (contract->descriptor_lowmem_last_word & STAGE74_XNU_PMAP_TABLE_DRYRUN_DESC_TYPE_MASK) |
        (contract->descriptor_kernel_first_word & STAGE74_XNU_PMAP_TABLE_DRYRUN_DESC_TYPE_MASK) |
        (contract->descriptor_kernel_last_word & STAGE74_XNU_PMAP_TABLE_DRYRUN_DESC_TYPE_MASK) |
        (contract->descriptor_workspace_first_word & STAGE74_XNU_PMAP_TABLE_DRYRUN_DESC_TYPE_MASK) |
        (contract->descriptor_workspace_last_word & STAGE74_XNU_PMAP_TABLE_DRYRUN_DESC_TYPE_MASK) |
        (contract->descriptor_ram_console_first_word & STAGE74_XNU_PMAP_TABLE_DRYRUN_DESC_TYPE_MASK) |
        (contract->descriptor_ram_console_last_word & STAGE74_XNU_PMAP_TABLE_DRYRUN_DESC_TYPE_MASK);
    contract->descriptor_attr_mask_seen =
        (contract->descriptor_lowmem_first_word & STAGE74_XNU_PMAP_TABLE_DRYRUN_DESC_ATTR_MASK) |
        (contract->descriptor_lowmem_last_word & STAGE74_XNU_PMAP_TABLE_DRYRUN_DESC_ATTR_MASK) |
        (contract->descriptor_kernel_first_word & STAGE74_XNU_PMAP_TABLE_DRYRUN_DESC_ATTR_MASK) |
        (contract->descriptor_kernel_last_word & STAGE74_XNU_PMAP_TABLE_DRYRUN_DESC_ATTR_MASK) |
        (contract->descriptor_workspace_first_word & STAGE74_XNU_PMAP_TABLE_DRYRUN_DESC_ATTR_MASK) |
        (contract->descriptor_workspace_last_word & STAGE74_XNU_PMAP_TABLE_DRYRUN_DESC_ATTR_MASK) |
        (contract->descriptor_ram_console_first_word & STAGE74_XNU_PMAP_TABLE_DRYRUN_DESC_ATTR_MASK) |
        (contract->descriptor_ram_console_last_word & STAGE74_XNU_PMAP_TABLE_DRYRUN_DESC_ATTR_MASK);

    if (stage74_pmap_dryrun_descriptor_attrs_ok(contract->descriptor_lowmem_first_word) &&
        stage74_pmap_dryrun_descriptor_attrs_ok(contract->descriptor_lowmem_last_word) &&
        stage74_pmap_dryrun_descriptor_attrs_ok(contract->descriptor_kernel_first_word) &&
        stage74_pmap_dryrun_descriptor_attrs_ok(contract->descriptor_kernel_last_word) &&
        stage74_pmap_dryrun_descriptor_attrs_ok(contract->descriptor_workspace_first_word) &&
        stage74_pmap_dryrun_descriptor_attrs_ok(contract->descriptor_workspace_last_word) &&
        stage74_pmap_dryrun_descriptor_attrs_ok(contract->descriptor_ram_console_first_word) &&
        stage74_pmap_dryrun_descriptor_attrs_ok(contract->descriptor_ram_console_last_word)) {
        contract->satisfied_mask |= STAGE74_XNU_PMAP_TABLE_DRYRUN_SAT_DESCRIPTOR_READBACK;
    } else {
        contract->failure_mask |= STAGE74_XNU_PMAP_TABLE_DRYRUN_FAIL_DESCRIPTOR;
    }
    if (contract->descriptor_type_mask_seen == STAGE74_XNU_PMAP_TABLE_DRYRUN_DESC_TYPE_SECTION &&
        contract->descriptor_attr_mask_seen == contract->descriptor_expected_attr_mask) {
        contract->satisfied_mask |= STAGE74_XNU_PMAP_TABLE_DRYRUN_SAT_DESCRIPTOR_ATTRS;
    } else {
        contract->failure_mask |= STAGE74_XNU_PMAP_TABLE_DRYRUN_FAIL_DESCRIPTOR;
    }

    contract->translation_kernel_va = contract->gVirtBase;
    contract->translation_kernel_expected_pa = contract->gPhysBase |
        (contract->gVirtBase & STAGE74_XNU_PMAP_TABLE_DRYRUN_SECTION_OFFSET_MASK);
    if (stage74_pmap_dryrun_translate(g_stage74_pmap_dryrun_l1, contract->translation_kernel_va, &translated) &&
        translated == contract->translation_kernel_expected_pa) {
        contract->translation_kernel_pa = translated;
        contract->translation_case_count++;
        contract->satisfied_mask |= STAGE74_XNU_PMAP_TABLE_DRYRUN_SAT_TRANSLATION_KERNEL;
    } else {
        contract->failure_mask |= STAGE74_XNU_PMAP_TABLE_DRYRUN_FAIL_TRANSLATION;
    }

    contract->translation_lowmem_va = contract->gPhysBase;
    if (stage74_pmap_dryrun_translate(g_stage74_pmap_dryrun_l1, contract->translation_lowmem_va, &translated) &&
        translated == contract->gPhysBase) {
        contract->translation_lowmem_pa = translated;
        contract->translation_case_count++;
        contract->satisfied_mask |= STAGE74_XNU_PMAP_TABLE_DRYRUN_SAT_TRANSLATION_LOWMEM;
    } else {
        contract->failure_mask |= STAGE74_XNU_PMAP_TABLE_DRYRUN_FAIL_TRANSLATION;
    }

    contract->translation_workspace_va = contract->workspace_base;
    if (stage74_pmap_dryrun_translate(g_stage74_pmap_dryrun_l1, contract->translation_workspace_va, &translated) &&
        translated == contract->workspace_base) {
        contract->translation_workspace_pa = translated;
        contract->translation_case_count++;
        contract->satisfied_mask |= STAGE74_XNU_PMAP_TABLE_DRYRUN_SAT_TRANSLATION_WORKSPACE;
    } else {
        contract->failure_mask |= STAGE74_XNU_PMAP_TABLE_DRYRUN_FAIL_TRANSLATION;
    }

    contract->translation_ram_console_va = RAM_CONSOLE_BASE;
    if (stage74_pmap_dryrun_translate(g_stage74_pmap_dryrun_l1, contract->translation_ram_console_va, &translated) &&
        translated == RAM_CONSOLE_BASE) {
        contract->translation_ram_console_pa = translated;
        contract->translation_case_count++;
        contract->satisfied_mask |= STAGE74_XNU_PMAP_TABLE_DRYRUN_SAT_TRANSLATION_RAM_CONSOLE;
    } else {
        contract->failure_mask |= STAGE74_XNU_PMAP_TABLE_DRYRUN_FAIL_TRANSLATION;
    }

    contract->local_l1_populated_checksum = stage74_pmap_dryrun_buffer_checksum(
        g_stage74_pmap_dryrun_l1, STAGE74_XNU_PMAP_TABLE_DRYRUN_L1_ENTRY_COUNT);
    if (contract->local_l1_populated_checksum != contract->local_l1_zero_checksum &&
        contract->local_l1_write_count != 0u &&
        contract->workspace_l1_table_local_distinct == 1u &&
        contract->proposed_workspace_written == 0u) {
        contract->satisfied_mask |= STAGE74_XNU_PMAP_TABLE_DRYRUN_SAT_LOCAL_ONLY_CHECKSUM;
    } else {
        contract->failure_mask |= STAGE74_XNU_PMAP_TABLE_DRYRUN_FAIL_CHECKSUM;
    }

    contract->proposed_workspace_written = 0u;
    if (contract->proposed_workspace_written == 0u &&
        pmap->proposed_tte_workspace_written == 0u &&
        preflight->tte_dryrun.proposed_tte_workspace_written == 0u) {
        contract->satisfied_mask |= STAGE74_XNU_PMAP_TABLE_DRYRUN_SAT_NO_PROPOSED_WORKSPACE_WRITE;
    } else {
        contract->failure_mask |= STAGE74_XNU_PMAP_TABLE_DRYRUN_FAIL_SAFETY_BOUNDARY;
    }

    contract->live_pmap_tables_installed = 0u;
    if (contract->live_pmap_tables_installed == 0u &&
        pmap->live_pmap_tables_installed == 0u &&
        snapshot->no_live_pmap_tables_installed == 1u &&
        preflight->tte_dryrun.live_mmu_tables_replaced == 0u) {
        contract->satisfied_mask |= STAGE74_XNU_PMAP_TABLE_DRYRUN_SAT_NO_LIVE_PMAP_INSTALL;
    } else {
        contract->failure_mask |= STAGE74_XNU_PMAP_TABLE_DRYRUN_FAIL_SAFETY_BOUNDARY;
    }

    contract->ttbr_written = 0u;
    contract->ttbcr_written = 0u;
    contract->dacr_written = 0u;
    contract->sctlr_written = 0u;
    if (contract->ttbr_written == 0u && contract->ttbcr_written == 0u &&
        contract->dacr_written == 0u && contract->sctlr_written == 0u) {
        contract->satisfied_mask |= STAGE74_XNU_PMAP_TABLE_DRYRUN_SAT_NO_CONTROL_REG_WRITE;
    } else {
        contract->failure_mask |= STAGE74_XNU_PMAP_TABLE_DRYRUN_FAIL_SAFETY_BOUNDARY;
    }

    contract->tlbs_invalidated = 0u;
    if (contract->tlbs_invalidated == 0u) {
        contract->satisfied_mask |= STAGE74_XNU_PMAP_TABLE_DRYRUN_SAT_NO_TLB_INVALIDATE;
    } else {
        contract->failure_mask |= STAGE74_XNU_PMAP_TABLE_DRYRUN_FAIL_SAFETY_BOUNDARY;
    }

    contract->public_arm_vm_init_executed = 0u;
    if (contract->public_pmap_compile_count == 0u && contract->public_pmap_link_count == 0u &&
        contract->public_pmap_execute_count == 0u && contract->public_arm_vm_init_executed == 0u &&
        (preflight->safety_mask & STAGE74_LOADER_SAFETY_NO_PUBLIC_XNU_EXEC) != 0u) {
        contract->satisfied_mask |= STAGE74_XNU_PMAP_TABLE_DRYRUN_SAT_NO_PUBLIC_PMAP_EXEC;
    } else {
        contract->failure_mask |= STAGE74_XNU_PMAP_TABLE_DRYRUN_FAIL_SAFETY_BOUNDARY;
    }

    contract->xnu_start_executed = 0u;
    contract->generated_macho_executed = ttbr_rt->macho_bytes_executed;
    if (contract->xnu_start_executed == 0u && contract->generated_macho_executed == 0u &&
        ttbr_rt->xnu_entry_executed == 0u &&
        (preflight->safety_mask & STAGE74_LOADER_SAFETY_NO_EXECUTE) != 0u) {
        contract->satisfied_mask |= STAGE74_XNU_PMAP_TABLE_DRYRUN_SAT_NO_XNU_MACHO_EXEC;
    } else {
        contract->failure_mask |= STAGE74_XNU_PMAP_TABLE_DRYRUN_FAIL_SAFETY_BOUNDARY;
    }

    contract->persistent_write_attempted = ttbr_rt->persistent_write_attempted;
    contract->caches_changed = ttbr_rt->caches_changed;
    if (contract->persistent_write_attempted == 0u && contract->caches_changed == 0u &&
        (preflight->safety_mask & STAGE74_LOADER_SAFETY_NO_PERSIST_WRITE) != 0u &&
        (preflight->safety_mask & STAGE74_LOADER_SAFETY_NO_CACHE_CHANGE) != 0u) {
        contract->satisfied_mask |= STAGE74_XNU_PMAP_TABLE_DRYRUN_SAT_NO_PERSIST_CACHE;
    } else {
        contract->failure_mask |= STAGE74_XNU_PMAP_TABLE_DRYRUN_FAIL_SAFETY_BOUNDARY;
    }

    if (preflight->xnu_compile_graph.fail_closed == 1u &&
        preflight->xnu_object_subset.fail_closed == 1u &&
        preflight->xnu_link.fail_closed == 1u &&
        pmap->fail_closed == 1u) {
        contract->fail_closed = 1u;
        contract->satisfied_mask |= STAGE74_XNU_PMAP_TABLE_DRYRUN_SAT_FAIL_CLOSED;
    } else {
        contract->failure_mask |= STAGE74_XNU_PMAP_TABLE_DRYRUN_FAIL_SAFETY_BOUNDARY;
    }

finish:
    if (contract->satisfied_mask == contract->required_mask && contract->failure_mask == 0u) {
        contract->status = STAGE74_STATUS_OK;
        xnu_log_puts("Stage74 XNU pmap table population dry-run contract ok\n");
    } else {
        contract->status = STAGE74_STATUS_BASE | contract->failure_mask;
        xnu_log_puts("Stage74 XNU pmap table population dry-run contract failed\n");
    }
    contract->checksum = stage74_pmap_dryrun_checksum(contract);
    if (contract->checksum != stage74_pmap_dryrun_checksum(contract)) {
        contract->failure_mask |= STAGE74_XNU_PMAP_TABLE_DRYRUN_FAIL_CHECKSUM;
        contract->status = STAGE74_STATUS_BASE | contract->failure_mask;
        contract->checksum = stage74_pmap_dryrun_checksum(contract);
    }

    stage74_xnu_pmap_table_dryrun_contract_log(contract);
    return contract->status == STAGE74_STATUS_OK;
}

const struct stage74_xnu_pmap_table_dryrun_contract *stage74_xnu_pmap_table_dryrun_contract_result(void)
{
    return &g_stage74_xnu_pmap_table_dryrun_contract;
}
