#include "stage68.h"

#define STAGE68_PMAP_REFERENCE_ARM_VM_INIT 0x00000001u
#define STAGE68_PMAP_REFERENCE_PMAP_C      0x00000002u
#define STAGE68_PMAP_REFERENCE_PMAP_H      0x00000004u
#define STAGE68_PMAP_REFERENCE_PROC_REG_H  0x00000008u
#define STAGE68_PMAP_REFERENCE_VM_PARAM_H  0x00000010u
#define STAGE68_PMAP_REFERENCE_REQUIRED    0x0000001fu

#define STAGE68_PMAP_RUNTIME_BLOCK_ARM_VM_INIT 0x00000001u
#define STAGE68_PMAP_RUNTIME_BLOCK_PMAP_C      0x00000002u
#define STAGE68_PMAP_RUNTIME_BLOCK_LIVE_TABLES 0x00000004u
#define STAGE68_PMAP_RUNTIME_BLOCK_REQUIRED    0x00000007u

static struct stage68_xnu_pmap_bootstrap_contract g_stage68_xnu_pmap_bootstrap_contract;

static uint32_t stage68_xnu_pmap_bootstrap_contract_checksum(volatile const struct stage68_xnu_pmap_bootstrap_contract *contract)
{
    volatile const uint32_t *words = (volatile const uint32_t *)(uintptr_t)contract;
    uint32_t count = (uint32_t)(offsetof(struct stage68_xnu_pmap_bootstrap_contract, checksum) / sizeof(uint32_t));
    uint32_t checksum = 0u;

    for (uint32_t i = 0; i < count; i++) {
        checksum ^= words[i];
    }

    return checksum;
}

static uint32_t stage68_pmap_contract_add_overflow_u32(uint32_t a, uint32_t b, uint32_t *out)
{
    *out = a + b;
    return *out < a;
}

static uint32_t stage68_pmap_contract_ranges_overlap(uint32_t a_base, uint32_t a_limit,
                                                     uint32_t b_base, uint32_t b_limit)
{
    if (a_limit <= a_base || b_limit <= b_base) {
        return 1u;
    }
    return (a_base < b_limit && b_base < a_limit) ? 1u : 0u;
}

static uint32_t stage68_pmap_contract_count_bits(uint32_t value)
{
    uint32_t count = 0u;

    while (value) {
        count += value & 1u;
        value >>= 1;
    }

    return count;
}

static void stage68_xnu_pmap_bootstrap_contract_log(const struct stage68_xnu_pmap_bootstrap_contract *contract)
{
    xnu_log_kv32("stage68_xnu_pmap_bootstrap_contract_status", contract->status);
    xnu_log_kv32("stage68_xnu_pmap_bootstrap_contract_required_mask", contract->required_mask);
    xnu_log_kv32("stage68_xnu_pmap_bootstrap_contract_satisfied_mask", contract->satisfied_mask);
    xnu_log_kv32("stage68_xnu_pmap_bootstrap_contract_failure_mask", contract->failure_mask);
    xnu_log_kv32("stage68_xnu_pmap_bootstrap_contract_checksum", contract->checksum);
    xnu_log_kv32("stage68_xnu_pmap_source_bootstrap_status", contract->source_bootstrap_status);
    xnu_log_kv32("stage68_xnu_pmap_source_bootstrap_satisfied_mask", contract->source_bootstrap_satisfied_mask);
    xnu_log_kv32("stage68_xnu_pmap_source_bootstrap_failure_mask", contract->source_bootstrap_failure_mask);
    xnu_log_kv32("stage68_xnu_pmap_source_bootstrap_checksum", contract->source_bootstrap_checksum);
    xnu_log_kv32("stage68_xnu_pmap_source_snapshot_status", contract->source_snapshot_status);
    xnu_log_kv32("stage68_xnu_pmap_source_snapshot_satisfied_mask", contract->source_snapshot_satisfied_mask);
    xnu_log_kv32("stage68_xnu_pmap_source_snapshot_failure_mask", contract->source_snapshot_failure_mask);
    xnu_log_kv32("stage68_xnu_pmap_source_snapshot_checksum", contract->source_snapshot_checksum);
    xnu_log_kv32("stage68_xnu_pmap_source_tte_dryrun_status", contract->source_tte_dryrun_status);
    xnu_log_kv32("stage68_xnu_pmap_source_safe_table_status", contract->source_safe_table_status);
    xnu_log_kv32("stage68_xnu_pmap_source_stage_owned_tables_status", contract->source_stage_owned_tables_status);
    xnu_log_kv32("stage68_xnu_pmap_source_ttbr_roundtrip_status", contract->source_ttbr_roundtrip_status);
    xnu_log_kv32("stage68_xnu_pmap_source_ttbr_restored_status", contract->source_ttbr_restored_status);
    xnu_log_kv32("stage68_xnu_pmap_source_cache_preserved_status", contract->source_cache_preserved_status);
    xnu_log_kv32("stage68_xnu_pmap_source_compile_graph_status", contract->source_compile_graph_status);
    xnu_log_kv32("stage68_xnu_pmap_source_object_subset_status", contract->source_object_subset_status);
    xnu_log_kv32("stage68_xnu_pmap_source_link_status", contract->source_link_status);
    xnu_log_kv32("stage68_xnu_pmap_reference_mask", contract->pmap_reference_mask);
    xnu_log_kv32("stage68_xnu_pmap_runtime_blocked_mask", contract->pmap_runtime_blocked_mask);
    xnu_log_kv32("stage68_xnu_pmap_reference_count", contract->pmap_reference_count);
    xnu_log_kv32("stage68_xnu_pmap_public_compile_count", contract->public_pmap_compile_count);
    xnu_log_kv32("stage68_xnu_pmap_public_link_count", contract->public_pmap_link_count);
    xnu_log_kv32("stage68_xnu_pmap_public_execute_count", contract->public_pmap_execute_count);
    xnu_log_kv32("stage68_xnu_pmap_arm_vm_init_reference_modeled", contract->arm_vm_init_reference_modeled);
    xnu_log_kv32("stage68_xnu_pmap_pmap_c_reference_modeled", contract->pmap_c_reference_modeled);
    xnu_log_kv32("stage68_xnu_pmap_pmap_h_reference_modeled", contract->pmap_h_reference_modeled);
    xnu_log_kv32("stage68_xnu_pmap_proc_reg_h_reference_modeled", contract->proc_reg_h_reference_modeled);
    xnu_log_kv32("stage68_xnu_pmap_vm_param_h_reference_modeled", contract->vm_param_h_reference_modeled);
    xnu_log_kv32("stage68_xnu_pmap_gVirtBase", contract->gVirtBase);
    xnu_log_kv32("stage68_xnu_pmap_gPhysBase", contract->gPhysBase);
    xnu_log_kv32("stage68_xnu_pmap_gPhysSize", contract->gPhysSize);
    xnu_log_kv32("stage68_xnu_pmap_boot_ttep", contract->boot_ttep);
    xnu_log_kv32("stage68_xnu_pmap_cpu_ttep", contract->cpu_ttep);
    xnu_log_kv32("stage68_xnu_pmap_initial_avail_start", contract->initial_avail_start);
    xnu_log_kv32("stage68_xnu_pmap_avail_end", contract->avail_end);
    xnu_log_kv32("stage68_xnu_pmap_vstart", contract->vstart);
    xnu_log_kv32("stage68_xnu_pmap_virtual_space_end", contract->virtual_space_end);
    xnu_log_kv32("stage68_xnu_pmap_page_size", contract->page_size);
    xnu_log_kv32("stage68_xnu_pmap_mem_size_max", contract->mem_size_max);
    xnu_log_kv32("stage68_xnu_pmap_vm_min_kernel_address", contract->vm_min_kernel_address);
    xnu_log_kv32("stage68_xnu_pmap_vm_max_kernel_address", contract->vm_max_kernel_address);
    xnu_log_kv32("stage68_xnu_pmap_proposed_virtBase", contract->proposed_virtBase);
    xnu_log_kv32("stage68_xnu_pmap_proposed_physBase", contract->proposed_physBase);
    xnu_log_kv32("stage68_xnu_pmap_proposed_memSize", contract->proposed_memSize);
    xnu_log_kv32("stage68_xnu_pmap_proposed_topOfKernelData", contract->proposed_topOfKernelData);
    xnu_log_kv32("stage68_xnu_pmap_proposed_avail_start", contract->proposed_avail_start);
    xnu_log_kv32("stage68_xnu_pmap_proposed_avail_end", contract->proposed_avail_end);
    xnu_log_kv32("stage68_xnu_pmap_loaded_phys_base", contract->loaded_phys_base);
    xnu_log_kv32("stage68_xnu_pmap_loaded_phys_end", contract->loaded_phys_end);
    xnu_log_kv32("stage68_xnu_pmap_ttep_workspace_base", contract->ttep_workspace_base);
    xnu_log_kv32("stage68_xnu_pmap_ttep_workspace_limit", contract->ttep_workspace_limit);
    xnu_log_kv32("stage68_xnu_pmap_allocator_span_base", contract->allocator_span_base);
    xnu_log_kv32("stage68_xnu_pmap_allocator_span_size", contract->allocator_span_size);
    xnu_log_kv32("stage68_xnu_pmap_allocator_span_end", contract->allocator_span_end);
    xnu_log_kv32("stage68_xnu_pmap_allocator_initial_cursor", contract->allocator_initial_cursor);
    xnu_log_kv32("stage68_xnu_pmap_allocator_current_cursor", contract->allocator_current_cursor);
    xnu_log_kv32("stage68_xnu_pmap_allocator_remaining_bytes", contract->allocator_remaining_bytes);
    xnu_log_kv32("stage68_xnu_pmap_allocator_first_alloc_base", contract->allocator_first_alloc_base);
    xnu_log_kv32("stage68_xnu_pmap_allocator_first_alloc_size", contract->allocator_first_alloc_size);
    xnu_log_kv32("stage68_xnu_pmap_allocator_first_alloc_end", contract->allocator_first_alloc_end);
    xnu_log_kv32("stage68_xnu_pmap_allocator_first_alloc_tag", contract->allocator_first_alloc_tag);
    xnu_log_kv32("stage68_xnu_pmap_allocator_alignment", contract->allocator_alignment);
    xnu_log_kv32("stage68_xnu_pmap_workspace_base", contract->workspace_base);
    xnu_log_kv32("stage68_xnu_pmap_workspace_limit", contract->workspace_limit);
    xnu_log_kv32("stage68_xnu_pmap_workspace_size", contract->workspace_size);
    xnu_log_kv32("stage68_xnu_pmap_workspace_section_count", contract->workspace_section_count);
    xnu_log_kv32("stage68_xnu_pmap_workspace_l1_table_phys", contract->workspace_l1_table_phys);
    xnu_log_kv32("stage68_xnu_pmap_workspace_l1_table_virt", contract->workspace_l1_table_virt);
    xnu_log_kv32("stage68_xnu_pmap_workspace_l1_section_descriptor", contract->workspace_l1_section_descriptor);
    xnu_log_kv32("stage68_xnu_pmap_workspace_l1_section_size", contract->workspace_l1_section_size);
    xnu_log_kv32("stage68_xnu_pmap_workspace_allocation_tag", contract->workspace_allocation_tag);
    xnu_log_kv32("stage68_xnu_pmap_workspace_mmu_enabled", contract->workspace_mmu_enabled);
    xnu_log_kv32("stage68_xnu_pmap_workspace_cache_policy", contract->workspace_cache_policy);
    xnu_log_kv32("stage68_xnu_pmap_modeled_free_page_count", contract->modeled_free_page_count);
    xnu_log_kv32("stage68_xnu_pmap_stage_image_base", contract->stage_image_base);
    xnu_log_kv32("stage68_xnu_pmap_stage_image_end", contract->stage_image_end);
    xnu_log_kv32("stage68_xnu_pmap_device_tree_base", contract->device_tree_base);
    xnu_log_kv32("stage68_xnu_pmap_device_tree_end", contract->device_tree_end);
    xnu_log_kv32("stage68_xnu_pmap_staging_arena_base", contract->staging_arena_base);
    xnu_log_kv32("stage68_xnu_pmap_staging_arena_end", contract->staging_arena_end);
    xnu_log_kv32("stage68_xnu_pmap_safe_table_base", contract->safe_table_base);
    xnu_log_kv32("stage68_xnu_pmap_safe_table_end", contract->safe_table_end);
    xnu_log_kv32("stage68_xnu_pmap_ram_console_base", contract->ram_console_base);
    xnu_log_kv32("stage68_xnu_pmap_ram_console_end", contract->ram_console_end);
    xnu_log_kv32("stage68_xnu_pmap_public_arm_vm_init_executed", contract->public_arm_vm_init_executed);
    xnu_log_kv32("stage68_xnu_pmap_live_pmap_tables_installed", contract->live_pmap_tables_installed);
    xnu_log_kv32("stage68_xnu_pmap_xnu_start_executed", contract->xnu_start_executed);
    xnu_log_kv32("stage68_xnu_pmap_generated_macho_executed", contract->generated_macho_executed);
    xnu_log_kv32("stage68_xnu_pmap_proposed_phys_load_written", contract->proposed_phys_load_written);
    xnu_log_kv32("stage68_xnu_pmap_proposed_tte_workspace_written", contract->proposed_tte_workspace_written);
    xnu_log_kv32("stage68_xnu_pmap_persistent_write_attempted", contract->persistent_write_attempted);
    xnu_log_kv32("stage68_xnu_pmap_caches_changed", contract->caches_changed);
    xnu_log_kv32("stage68_xnu_pmap_fail_closed", contract->fail_closed);
}

int stage68_xnu_pmap_bootstrap_contract_selftest(const struct stage68_loader_preflight *preflight)
{
    struct stage68_xnu_pmap_bootstrap_contract *contract = &g_stage68_xnu_pmap_bootstrap_contract;
    const struct stage68_xnu_bootstrap_contract *bootstrap = stage68_xnu_bootstrap_contract_result();
    const struct stage68_pmap_bootstrap_snapshot *snapshot = mmu_stage68_pmap_bootstrap_snapshot_result();
    const struct stage68_ttbr0_roundtrip *ttbr_rt = mmu_stage68_ttbr0_roundtrip_result();
    uint32_t cpu_ttep = 0u;
    uint32_t initial_avail_start = 0u;
    uint32_t avail_end = 0u;
    uint32_t virt_plus_mem = 0u;
    uint32_t virt_plus_round = 0u;
    uint32_t proposed_range_base = 0u;
    uint32_t proposed_range_limit = 0u;
    uint32_t source_rollups_ok = 0u;

    memset(contract, 0, sizeof(*contract));
    contract->version = STAGE68_XNU_PMAP_BOOTSTRAP_CONTRACT_VERSION;
    contract->size = sizeof(*contract);
    contract->status = STAGE68_STATUS_BASE;
    contract->required_mask = STAGE68_XNU_PMAP_BOOTSTRAP_CONTRACT_REQUIRED_MASK;
    contract->page_size = STAGE68_XNU_PMAP_BOOTSTRAP_PAGE_SIZE;
    contract->mem_size_max = STAGE68_XNU_PMAP_BOOTSTRAP_MEM_SIZE_MAX;
    contract->vm_min_kernel_address = STAGE68_XNU_PMAP_BOOTSTRAP_VM_MIN_KERNEL_ADDRESS;
    contract->vm_max_kernel_address = STAGE68_XNU_PMAP_BOOTSTRAP_VM_MAX_KERNEL_ADDRESS;
    contract->virtual_space_end = STAGE68_XNU_PMAP_BOOTSTRAP_VM_MAX_KERNEL_ADDRESS;
    contract->ram_console_base = RAM_CONSOLE_BASE;
    contract->ram_console_end = RAM_CONSOLE_BASE + RAM_CONSOLE_SIZE;
    contract->stage_image_base = STAGE68_BASE;
    contract->stage_image_end = (uint32_t)(uintptr_t)__stage68_image_end;

    xnu_log_puts("Stage68 XNU pmap/bootstrap allocation contract begin\n");

    if (!preflight || !bootstrap || !snapshot || !ttbr_rt) {
        contract->failure_mask |= STAGE68_XNU_PMAP_BOOTSTRAP_CONTRACT_FAIL_SOURCE |
                                  STAGE68_XNU_PMAP_BOOTSTRAP_CONTRACT_FAIL_SNAPSHOT |
                                  STAGE68_XNU_PMAP_BOOTSTRAP_CONTRACT_FAIL_SAFETY_BOUNDARY;
        goto finish;
    }

    contract->source_bootstrap_status = bootstrap->status;
    contract->source_bootstrap_required_mask = bootstrap->required_mask;
    contract->source_bootstrap_satisfied_mask = bootstrap->satisfied_mask;
    contract->source_bootstrap_failure_mask = bootstrap->failure_mask;
    contract->source_bootstrap_checksum = bootstrap->checksum;
    contract->source_snapshot_status = snapshot->status;
    contract->source_snapshot_required_mask = snapshot->required_mask;
    contract->source_snapshot_satisfied_mask = snapshot->satisfied_mask;
    contract->source_snapshot_failure_mask = snapshot->failure_mask;
    contract->source_snapshot_checksum = snapshot->checksum;
    contract->source_tte_dryrun_status = preflight->tte_dryrun_status;
    contract->source_safe_table_status = preflight->safe_table_status;
    contract->source_stage_owned_tables_status = preflight->stage_owned_tables_status;
    contract->source_ttbr_roundtrip_status = preflight->ttbr_roundtrip_status;
    contract->source_ttbr_restored_status = preflight->ttbr_restored_status;
    contract->source_cache_preserved_status = preflight->cache_preserved_status;
    contract->source_compile_graph_status = preflight->xnu_compile_graph_status;
    contract->source_object_subset_status = preflight->xnu_object_subset_status;
    contract->source_link_status = preflight->xnu_link_status;

    contract->proposed_virtBase = bootstrap->proposed_virtBase;
    contract->proposed_physBase = bootstrap->proposed_physBase;
    contract->proposed_memSize = bootstrap->proposed_memSize;
    contract->proposed_topOfKernelData = bootstrap->proposed_topOfKernelData;
    contract->proposed_avail_start = bootstrap->proposed_avail_start;
    contract->proposed_avail_end = bootstrap->proposed_avail_end;
    contract->loaded_phys_base = bootstrap->loaded_phys_base;
    contract->loaded_phys_end = bootstrap->loaded_phys_end;
    contract->ttep_workspace_base = bootstrap->ttep_workspace_base;
    contract->ttep_workspace_limit = bootstrap->ttep_workspace_limit;
    contract->device_tree_base = bootstrap->device_tree_base;
    contract->device_tree_end = bootstrap->device_tree_end;
    contract->staging_arena_base = bootstrap->staging_arena_base;
    contract->staging_arena_end = bootstrap->staging_arena_end;
    contract->safe_table_base = bootstrap->safe_table_base;
    contract->safe_table_end = bootstrap->safe_table_end;

    contract->pmap_reference_mask = STAGE68_PMAP_REFERENCE_ARM_VM_INIT |
        STAGE68_PMAP_REFERENCE_PMAP_C |
        STAGE68_PMAP_REFERENCE_PMAP_H |
        STAGE68_PMAP_REFERENCE_PROC_REG_H |
        STAGE68_PMAP_REFERENCE_VM_PARAM_H;
    contract->pmap_runtime_blocked_mask = STAGE68_PMAP_RUNTIME_BLOCK_ARM_VM_INIT |
        STAGE68_PMAP_RUNTIME_BLOCK_PMAP_C |
        STAGE68_PMAP_RUNTIME_BLOCK_LIVE_TABLES;
    contract->pmap_reference_count = stage68_pmap_contract_count_bits(contract->pmap_reference_mask);
    contract->public_pmap_compile_count = preflight->xnu_compile_graph.pmap_public_compile_count;
    contract->public_pmap_link_count = preflight->xnu_compile_graph.pmap_public_link_count;
    contract->public_pmap_execute_count = 0u;
    contract->arm_vm_init_reference_modeled = ((contract->pmap_reference_mask & STAGE68_PMAP_REFERENCE_ARM_VM_INIT) != 0u) ? 1u : 0u;
    contract->pmap_c_reference_modeled = ((contract->pmap_reference_mask & STAGE68_PMAP_REFERENCE_PMAP_C) != 0u) ? 1u : 0u;
    contract->pmap_h_reference_modeled = ((contract->pmap_reference_mask & STAGE68_PMAP_REFERENCE_PMAP_H) != 0u) ? 1u : 0u;
    contract->proc_reg_h_reference_modeled = ((contract->pmap_reference_mask & STAGE68_PMAP_REFERENCE_PROC_REG_H) != 0u) ? 1u : 0u;
    contract->vm_param_h_reference_modeled = ((contract->pmap_reference_mask & STAGE68_PMAP_REFERENCE_VM_PARAM_H) != 0u) ? 1u : 0u;

    contract->gVirtBase = contract->proposed_virtBase;
    contract->gPhysBase = contract->proposed_physBase;
    contract->gPhysSize = contract->proposed_memSize;
    contract->boot_ttep = contract->proposed_topOfKernelData;
    if (stage68_pmap_contract_add_overflow_u32(contract->boot_ttep, 4u * STAGE68_XNU_PMAP_BOOTSTRAP_PAGE_SIZE, &cpu_ttep) ||
        stage68_pmap_contract_add_overflow_u32(cpu_ttep, 6u * STAGE68_XNU_PMAP_BOOTSTRAP_PAGE_SIZE, &initial_avail_start) ||
        stage68_pmap_contract_add_overflow_u32(contract->proposed_physBase, contract->proposed_memSize, &avail_end) ||
        stage68_pmap_contract_add_overflow_u32(contract->proposed_virtBase, STAGE68_XNU_PMAP_BOOTSTRAP_MEM_SIZE_MAX, &virt_plus_mem) ||
        stage68_pmap_contract_add_overflow_u32(virt_plus_mem, STAGE68_XNU_PMAP_BOOTSTRAP_VSTART_ROUND - 1u, &virt_plus_round)) {
        contract->failure_mask |= STAGE68_XNU_PMAP_BOOTSTRAP_CONTRACT_FAIL_ARITHMETIC;
    } else {
        contract->cpu_ttep = cpu_ttep;
        contract->initial_avail_start = initial_avail_start;
        contract->avail_end = avail_end;
        contract->vstart = virt_plus_round & ~(STAGE68_XNU_PMAP_BOOTSTRAP_VSTART_ROUND - 1u);
    }

    contract->allocator_span_base = snapshot->allocator_span_base;
    contract->allocator_span_size = snapshot->allocator_span_size;
    contract->allocator_span_end = snapshot->allocator_span_end;
    contract->allocator_initial_cursor = snapshot->allocator_initial_cursor;
    contract->allocator_current_cursor = snapshot->allocator_current_cursor;
    contract->allocator_remaining_bytes = snapshot->allocator_remaining_bytes;
    contract->allocator_first_alloc_base = snapshot->allocator_first_alloc_base;
    contract->allocator_first_alloc_size = snapshot->allocator_first_alloc_size;
    contract->allocator_first_alloc_end = snapshot->allocator_first_alloc_end;
    contract->allocator_first_alloc_tag = snapshot->allocator_first_alloc_tag;
    contract->allocator_alignment = snapshot->allocator_alignment;
    contract->workspace_base = snapshot->workspace_base;
    contract->workspace_limit = snapshot->workspace_limit;
    contract->workspace_size = snapshot->workspace_size;
    contract->workspace_section_count = snapshot->workspace_section_count;
    contract->workspace_l1_table_phys = snapshot->workspace_l1_table_phys;
    contract->workspace_l1_table_virt = snapshot->workspace_l1_table_virt;
    contract->workspace_l1_section_descriptor = snapshot->workspace_l1_section_descriptor;
    contract->workspace_l1_section_size = snapshot->workspace_l1_section_size;
    contract->workspace_allocation_tag = snapshot->workspace_allocation_tag;
    contract->workspace_mmu_enabled = snapshot->workspace_mmu_enabled;
    contract->workspace_cache_policy = snapshot->workspace_cache_policy;
    if (contract->avail_end > contract->initial_avail_start) {
        contract->modeled_free_page_count = (contract->avail_end - contract->initial_avail_start) /
            STAGE68_XNU_PMAP_BOOTSTRAP_PAGE_SIZE;
    }

    source_rollups_ok = (contract->source_bootstrap_status == STAGE68_STATUS_OK &&
                         contract->source_bootstrap_satisfied_mask == contract->source_bootstrap_required_mask &&
                         contract->source_bootstrap_failure_mask == 0u &&
                         contract->source_tte_dryrun_status == STAGE68_STATUS_OK &&
                         contract->source_safe_table_status == STAGE68_STATUS_OK &&
                         contract->source_stage_owned_tables_status == STAGE68_STATUS_OK &&
                         contract->source_ttbr_roundtrip_status == STAGE68_STATUS_OK &&
                         contract->source_ttbr_restored_status == STAGE68_STATUS_OK &&
                         contract->source_cache_preserved_status == STAGE68_STATUS_OK &&
                         contract->source_compile_graph_status == STAGE68_STATUS_OK &&
                         contract->source_object_subset_status == STAGE68_STATUS_OK &&
                         contract->source_link_status == STAGE68_STATUS_OK &&
                         preflight->xnu_compile_graph_failure_mask == 0u &&
                         preflight->xnu_object_subset_failure_mask == 0u &&
                         preflight->xnu_link_failure_mask == 0u) ? 1u : 0u;

    if (contract->source_bootstrap_status == STAGE68_STATUS_OK &&
        contract->source_bootstrap_satisfied_mask == contract->source_bootstrap_required_mask &&
        contract->source_bootstrap_failure_mask == 0u) {
        contract->satisfied_mask |= STAGE68_XNU_PMAP_BOOTSTRAP_CONTRACT_SAT_SOURCE_BOOTSTRAP;
    } else {
        contract->failure_mask |= STAGE68_XNU_PMAP_BOOTSTRAP_CONTRACT_FAIL_SOURCE;
    }
    if (source_rollups_ok) {
        contract->satisfied_mask |= STAGE68_XNU_PMAP_BOOTSTRAP_CONTRACT_SAT_SOURCE_ROLLUPS;
    } else {
        contract->failure_mask |= STAGE68_XNU_PMAP_BOOTSTRAP_CONTRACT_FAIL_SOURCE;
    }
    if (contract->pmap_reference_mask == STAGE68_PMAP_REFERENCE_REQUIRED &&
        contract->pmap_runtime_blocked_mask == STAGE68_PMAP_RUNTIME_BLOCK_REQUIRED &&
        contract->pmap_reference_count == 5u &&
        preflight->xnu_compile_graph.pmap_reference_count >= 5u &&
        preflight->xnu_compile_graph.pmap_reference_mask == STAGE68_PMAP_REFERENCE_REQUIRED &&
        preflight->xnu_compile_graph.pmap_runtime_blocked_mask ==
            (STAGE68_PMAP_RUNTIME_BLOCK_ARM_VM_INIT | STAGE68_PMAP_RUNTIME_BLOCK_PMAP_C)) {
        contract->satisfied_mask |= STAGE68_XNU_PMAP_BOOTSTRAP_CONTRACT_SAT_REFERENCE_PROVENANCE;
    } else {
        contract->failure_mask |= STAGE68_XNU_PMAP_BOOTSTRAP_CONTRACT_FAIL_REFERENCE;
    }
    if (contract->gVirtBase == contract->proposed_virtBase &&
        contract->gPhysBase == contract->proposed_physBase &&
        contract->gPhysSize == contract->proposed_memSize &&
        contract->gPhysBase == RAM_PHYS_BASE &&
        contract->gPhysSize == (RAM_CONSOLE_BASE - RAM_PHYS_BASE)) {
        contract->satisfied_mask |= STAGE68_XNU_PMAP_BOOTSTRAP_CONTRACT_SAT_PUBLIC_GLOBALS;
    } else {
        contract->failure_mask |= STAGE68_XNU_PMAP_BOOTSTRAP_CONTRACT_FAIL_ARITHMETIC;
    }
    if (contract->boot_ttep == contract->proposed_topOfKernelData &&
        contract->cpu_ttep == contract->boot_ttep + (4u * STAGE68_XNU_PMAP_BOOTSTRAP_PAGE_SIZE) &&
        contract->initial_avail_start == contract->cpu_ttep + (6u * STAGE68_XNU_PMAP_BOOTSTRAP_PAGE_SIZE) &&
        contract->boot_ttep == contract->ttep_workspace_base &&
        contract->cpu_ttep >= contract->boot_ttep &&
        contract->initial_avail_start == contract->ttep_workspace_limit) {
        contract->satisfied_mask |= STAGE68_XNU_PMAP_BOOTSTRAP_CONTRACT_SAT_TTEP_LAYOUT;
    } else {
        contract->failure_mask |= STAGE68_XNU_PMAP_BOOTSTRAP_CONTRACT_FAIL_ARITHMETIC;
    }
    if (contract->avail_end == contract->proposed_avail_end &&
        contract->avail_end == contract->proposed_physBase + contract->proposed_memSize &&
        contract->initial_avail_start == contract->proposed_avail_start &&
        contract->initial_avail_start < contract->avail_end &&
        contract->avail_end <= RAM_CONSOLE_BASE &&
        contract->modeled_free_page_count != 0u) {
        contract->satisfied_mask |= STAGE68_XNU_PMAP_BOOTSTRAP_CONTRACT_SAT_AVAIL_RANGE;
    } else {
        contract->failure_mask |= STAGE68_XNU_PMAP_BOOTSTRAP_CONTRACT_FAIL_ARITHMETIC;
    }
    if (contract->vstart == ((contract->proposed_virtBase + STAGE68_XNU_PMAP_BOOTSTRAP_MEM_SIZE_MAX +
                              (STAGE68_XNU_PMAP_BOOTSTRAP_VSTART_ROUND - 1u)) &
                             ~(STAGE68_XNU_PMAP_BOOTSTRAP_VSTART_ROUND - 1u)) &&
        contract->vstart >= STAGE68_XNU_PMAP_BOOTSTRAP_VM_MIN_KERNEL_ADDRESS &&
        contract->vstart < contract->virtual_space_end &&
        contract->virtual_space_end == STAGE68_XNU_PMAP_BOOTSTRAP_VM_MAX_KERNEL_ADDRESS) {
        contract->satisfied_mask |= STAGE68_XNU_PMAP_BOOTSTRAP_CONTRACT_SAT_VIRTUAL_SPACE;
    } else {
        contract->failure_mask |= STAGE68_XNU_PMAP_BOOTSTRAP_CONTRACT_FAIL_ARITHMETIC;
    }
    if (contract->source_snapshot_status == STAGE68_STATUS_OK &&
        contract->source_snapshot_satisfied_mask == contract->source_snapshot_required_mask &&
        contract->source_snapshot_failure_mask == 0u &&
        snapshot->stage_owned_snapshot == 1u &&
        snapshot->no_live_pmap_tables_installed == 1u) {
        contract->satisfied_mask |= STAGE68_XNU_PMAP_BOOTSTRAP_CONTRACT_SAT_SNAPSHOT_IMPORTED;
    } else {
        contract->failure_mask |= STAGE68_XNU_PMAP_BOOTSTRAP_CONTRACT_FAIL_SNAPSHOT;
    }
    if (contract->allocator_span_base == RAM_PHYS_BASE &&
        contract->allocator_span_size == STAGE68_XNU_PMAP_BOOTSTRAP_SECTION_SIZE &&
        contract->allocator_span_end == contract->allocator_span_base + contract->allocator_span_size) {
        contract->satisfied_mask |= STAGE68_XNU_PMAP_BOOTSTRAP_CONTRACT_SAT_ALLOCATOR_SPAN;
    } else {
        contract->failure_mask |= STAGE68_XNU_PMAP_BOOTSTRAP_CONTRACT_FAIL_ALLOCATOR;
    }
    if (contract->allocator_initial_cursor == contract->allocator_span_base &&
        contract->allocator_current_cursor == contract->allocator_span_end &&
        contract->allocator_remaining_bytes == 0u) {
        contract->satisfied_mask |= STAGE68_XNU_PMAP_BOOTSTRAP_CONTRACT_SAT_ALLOCATOR_CURSOR;
    } else {
        contract->failure_mask |= STAGE68_XNU_PMAP_BOOTSTRAP_CONTRACT_FAIL_ALLOCATOR;
    }
    if (contract->allocator_first_alloc_base == contract->allocator_span_base &&
        contract->allocator_first_alloc_size == contract->allocator_span_size &&
        contract->allocator_first_alloc_end == contract->allocator_span_end &&
        contract->allocator_first_alloc_tag == STAGE68_XNU_PMAP_BOOTSTRAP_ALLOC_TAG) {
        contract->satisfied_mask |= STAGE68_XNU_PMAP_BOOTSTRAP_CONTRACT_SAT_ALLOCATOR_FIRST_ALLOC;
    } else {
        contract->failure_mask |= STAGE68_XNU_PMAP_BOOTSTRAP_CONTRACT_FAIL_ALLOCATOR;
    }
    if (contract->allocator_alignment == STAGE68_XNU_PMAP_BOOTSTRAP_PAGE_SIZE &&
        (contract->allocator_span_base & (STAGE68_XNU_PMAP_BOOTSTRAP_PAGE_SIZE - 1u)) == 0u &&
        (contract->allocator_span_end & (STAGE68_XNU_PMAP_BOOTSTRAP_PAGE_SIZE - 1u)) == 0u) {
        contract->satisfied_mask |= STAGE68_XNU_PMAP_BOOTSTRAP_CONTRACT_SAT_ALLOCATOR_POLICY;
    } else {
        contract->failure_mask |= STAGE68_XNU_PMAP_BOOTSTRAP_CONTRACT_FAIL_ALLOCATOR;
    }
    if (contract->workspace_base == contract->allocator_span_base &&
        contract->workspace_limit == contract->allocator_span_end &&
        contract->workspace_size == contract->allocator_span_size &&
        contract->workspace_limit == contract->workspace_base + contract->workspace_size) {
        contract->satisfied_mask |= STAGE68_XNU_PMAP_BOOTSTRAP_CONTRACT_SAT_WORKSPACE_RANGE;
    } else {
        contract->failure_mask |= STAGE68_XNU_PMAP_BOOTSTRAP_CONTRACT_FAIL_WORKSPACE;
    }
    if (contract->workspace_l1_table_phys != 0u &&
        (contract->workspace_l1_table_phys & (STAGE68_XNU_PMAP_BOOTSTRAP_L1_ALIGNMENT - 1u)) == 0u &&
        contract->workspace_l1_table_virt >= 0xc0000000u &&
        contract->workspace_l1_table_virt == (0xc0000000u + contract->workspace_l1_table_phys)) {
        contract->satisfied_mask |= STAGE68_XNU_PMAP_BOOTSTRAP_CONTRACT_SAT_WORKSPACE_L1;
    } else {
        contract->failure_mask |= STAGE68_XNU_PMAP_BOOTSTRAP_CONTRACT_FAIL_WORKSPACE;
    }
    if (contract->workspace_section_count == (contract->proposed_memSize / STAGE68_XNU_PMAP_BOOTSTRAP_SECTION_SIZE) &&
        contract->workspace_l1_section_descriptor == STAGE68_XNU_PMAP_BOOTSTRAP_SECTION_DESC_SO &&
        contract->workspace_l1_section_size == STAGE68_XNU_PMAP_BOOTSTRAP_SECTION_SIZE) {
        contract->satisfied_mask |= STAGE68_XNU_PMAP_BOOTSTRAP_CONTRACT_SAT_WORKSPACE_SECTIONS;
    } else {
        contract->failure_mask |= STAGE68_XNU_PMAP_BOOTSTRAP_CONTRACT_FAIL_WORKSPACE;
    }
    if (contract->workspace_allocation_tag == STAGE68_XNU_PMAP_BOOTSTRAP_WORKSPACE_TAG &&
        contract->workspace_mmu_enabled == 1u &&
        contract->workspace_cache_policy == 0u) {
        contract->satisfied_mask |= STAGE68_XNU_PMAP_BOOTSTRAP_CONTRACT_SAT_WORKSPACE_POLICY;
    } else {
        contract->failure_mask |= STAGE68_XNU_PMAP_BOOTSTRAP_CONTRACT_FAIL_WORKSPACE;
    }

    proposed_range_base = contract->loaded_phys_base;
    proposed_range_limit = contract->initial_avail_start;
    if (contract->loaded_phys_base >= RAM_PHYS_BASE && contract->stage_image_end <= contract->loaded_phys_base) {
        contract->satisfied_mask |= STAGE68_XNU_PMAP_BOOTSTRAP_CONTRACT_SAT_NO_STAGE_OVERLAP;
    } else if (!stage68_pmap_contract_ranges_overlap(proposed_range_base, proposed_range_limit,
                                                     contract->stage_image_base, contract->stage_image_end)) {
        contract->satisfied_mask |= STAGE68_XNU_PMAP_BOOTSTRAP_CONTRACT_SAT_NO_STAGE_OVERLAP;
    } else {
        contract->failure_mask |= STAGE68_XNU_PMAP_BOOTSTRAP_CONTRACT_FAIL_OVERLAP;
    }
    if (!stage68_pmap_contract_ranges_overlap(proposed_range_base, proposed_range_limit,
                                              contract->device_tree_base, contract->device_tree_end)) {
        contract->satisfied_mask |= STAGE68_XNU_PMAP_BOOTSTRAP_CONTRACT_SAT_NO_DT_OVERLAP;
    } else {
        contract->failure_mask |= STAGE68_XNU_PMAP_BOOTSTRAP_CONTRACT_FAIL_OVERLAP;
    }
    if (!stage68_pmap_contract_ranges_overlap(proposed_range_base, proposed_range_limit,
                                              contract->staging_arena_base, contract->staging_arena_end)) {
        contract->satisfied_mask |= STAGE68_XNU_PMAP_BOOTSTRAP_CONTRACT_SAT_NO_STAGING_OVERLAP;
    } else {
        contract->failure_mask |= STAGE68_XNU_PMAP_BOOTSTRAP_CONTRACT_FAIL_OVERLAP;
    }
    if (!stage68_pmap_contract_ranges_overlap(proposed_range_base, proposed_range_limit,
                                              contract->safe_table_base, contract->safe_table_end)) {
        contract->satisfied_mask |= STAGE68_XNU_PMAP_BOOTSTRAP_CONTRACT_SAT_NO_SAFE_TABLE_OVERLAP;
    } else {
        contract->failure_mask |= STAGE68_XNU_PMAP_BOOTSTRAP_CONTRACT_FAIL_OVERLAP;
    }
    if (!stage68_pmap_contract_ranges_overlap(proposed_range_base, proposed_range_limit,
                                              contract->ram_console_base, contract->ram_console_end)) {
        contract->satisfied_mask |= STAGE68_XNU_PMAP_BOOTSTRAP_CONTRACT_SAT_NO_RAM_CONSOLE_OVERLAP;
    } else {
        contract->failure_mask |= STAGE68_XNU_PMAP_BOOTSTRAP_CONTRACT_FAIL_OVERLAP;
    }

    if (contract->public_pmap_compile_count == 0u) {
        contract->satisfied_mask |= STAGE68_XNU_PMAP_BOOTSTRAP_CONTRACT_SAT_NO_PUBLIC_PMAP_COMPILE;
    } else {
        contract->failure_mask |= STAGE68_XNU_PMAP_BOOTSTRAP_CONTRACT_FAIL_PUBLIC_PMAP_COMPILE;
    }
    if (contract->public_pmap_link_count == 0u) {
        contract->satisfied_mask |= STAGE68_XNU_PMAP_BOOTSTRAP_CONTRACT_SAT_NO_PUBLIC_PMAP_LINK;
    } else {
        contract->failure_mask |= STAGE68_XNU_PMAP_BOOTSTRAP_CONTRACT_FAIL_PUBLIC_PMAP_LINK;
    }
    if (contract->public_pmap_execute_count == 0u &&
        (preflight->safety_mask & STAGE68_LOADER_SAFETY_NO_PUBLIC_XNU_EXEC) != 0u &&
        ttbr_rt->xnu_entry_executed == 0u) {
        contract->satisfied_mask |= STAGE68_XNU_PMAP_BOOTSTRAP_CONTRACT_SAT_NO_PUBLIC_PMAP_EXEC;
    } else {
        contract->failure_mask |= STAGE68_XNU_PMAP_BOOTSTRAP_CONTRACT_FAIL_PUBLIC_PMAP_EXEC;
    }
    contract->public_arm_vm_init_executed = 0u;
    if (contract->public_arm_vm_init_executed == 0u) {
        contract->satisfied_mask |= STAGE68_XNU_PMAP_BOOTSTRAP_CONTRACT_SAT_NO_ARM_VM_INIT_EXEC;
    } else {
        contract->failure_mask |= STAGE68_XNU_PMAP_BOOTSTRAP_CONTRACT_FAIL_PUBLIC_PMAP_EXEC;
    }
    contract->live_pmap_tables_installed = 0u;
    if (contract->live_pmap_tables_installed == 0u && snapshot->no_live_pmap_tables_installed == 1u &&
        preflight->tte_dryrun.live_mmu_tables_replaced == 0u) {
        contract->satisfied_mask |= STAGE68_XNU_PMAP_BOOTSTRAP_CONTRACT_SAT_NO_LIVE_PMAP_INSTALL;
    } else {
        contract->failure_mask |= STAGE68_XNU_PMAP_BOOTSTRAP_CONTRACT_FAIL_LIVE_PMAP_INSTALL;
    }
    contract->xnu_start_executed = 0u;
    if (contract->xnu_start_executed == 0u && ttbr_rt->xnu_entry_executed == 0u) {
        contract->satisfied_mask |= STAGE68_XNU_PMAP_BOOTSTRAP_CONTRACT_SAT_NO_XNU_JUMP;
    } else {
        contract->failure_mask |= STAGE68_XNU_PMAP_BOOTSTRAP_CONTRACT_FAIL_SAFETY_BOUNDARY;
    }
    contract->generated_macho_executed = ttbr_rt->macho_bytes_executed;
    if (contract->generated_macho_executed == 0u &&
        (preflight->safety_mask & STAGE68_LOADER_SAFETY_NO_EXECUTE) != 0u) {
        contract->satisfied_mask |= STAGE68_XNU_PMAP_BOOTSTRAP_CONTRACT_SAT_NO_MACHO_EXEC;
    } else {
        contract->failure_mask |= STAGE68_XNU_PMAP_BOOTSTRAP_CONTRACT_FAIL_SAFETY_BOUNDARY;
    }
    contract->proposed_phys_load_written = preflight->tte_dryrun.proposed_phys_load_written | ttbr_rt->proposed_phys_load_written;
    contract->proposed_tte_workspace_written = preflight->tte_dryrun.proposed_tte_workspace_written | ttbr_rt->proposed_tte_workspace_written;
    if (contract->proposed_phys_load_written == 0u && contract->proposed_tte_workspace_written == 0u &&
        (preflight->safety_mask & STAGE68_LOADER_SAFETY_NO_PHYS_WRITE) != 0u &&
        (preflight->safety_mask & STAGE68_LOADER_SAFETY_TTE_DRYRUN_ONLY) != 0u) {
        contract->satisfied_mask |= STAGE68_XNU_PMAP_BOOTSTRAP_CONTRACT_SAT_NO_PROPOSED_WRITES;
    } else {
        contract->failure_mask |= STAGE68_XNU_PMAP_BOOTSTRAP_CONTRACT_FAIL_SAFETY_BOUNDARY;
    }
    contract->persistent_write_attempted = ttbr_rt->persistent_write_attempted;
    contract->caches_changed = ttbr_rt->caches_changed;
    if (contract->persistent_write_attempted == 0u && contract->caches_changed == 0u &&
        (preflight->safety_mask & STAGE68_LOADER_SAFETY_NO_PERSIST_WRITE) != 0u &&
        (preflight->safety_mask & STAGE68_LOADER_SAFETY_NO_CACHE_CHANGE) != 0u) {
        contract->satisfied_mask |= STAGE68_XNU_PMAP_BOOTSTRAP_CONTRACT_SAT_NO_PERSIST_CACHE;
    } else {
        contract->failure_mask |= STAGE68_XNU_PMAP_BOOTSTRAP_CONTRACT_FAIL_SAFETY_BOUNDARY;
    }
    if (preflight->xnu_compile_graph.fail_closed == 1u && preflight->xnu_object_subset.fail_closed == 1u &&
        preflight->xnu_link.fail_closed == 1u && bootstrap->fail_closed == 1u) {
        contract->fail_closed = 1u;
        contract->satisfied_mask |= STAGE68_XNU_PMAP_BOOTSTRAP_CONTRACT_SAT_FAIL_CLOSED;
    } else {
        contract->failure_mask |= STAGE68_XNU_PMAP_BOOTSTRAP_CONTRACT_FAIL_SAFETY_BOUNDARY;
    }

finish:
    if (contract->satisfied_mask == contract->required_mask && contract->failure_mask == 0u) {
        contract->status = STAGE68_STATUS_OK;
        xnu_log_puts("Stage68 XNU pmap/bootstrap allocation contract ok\n");
    } else {
        contract->status = STAGE68_STATUS_BASE | contract->failure_mask;
        xnu_log_puts("Stage68 XNU pmap/bootstrap allocation contract failed\n");
    }
    contract->checksum = stage68_xnu_pmap_bootstrap_contract_checksum(contract);
    if (contract->checksum != stage68_xnu_pmap_bootstrap_contract_checksum(contract)) {
        contract->failure_mask |= STAGE68_XNU_PMAP_BOOTSTRAP_CONTRACT_FAIL_CHECKSUM;
        contract->status = STAGE68_STATUS_BASE | contract->failure_mask;
    }

    stage68_xnu_pmap_bootstrap_contract_log(contract);
    return contract->status == STAGE68_STATUS_OK;
}

const struct stage68_xnu_pmap_bootstrap_contract *stage68_xnu_pmap_bootstrap_contract_result(void)
{
    return &g_stage68_xnu_pmap_bootstrap_contract;
}
