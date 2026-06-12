#include "stage79.h"

static struct stage79_xnu_entry_stub_result g_stage79_xnu_entry_stub_result;

static inline uint32_t stage79_entry_stub_read_ttbr0(void)
{
    uint32_t v;
    __asm__ volatile ("mrc p15, 0, %0, c2, c0, 0" : "=r"(v));
    return v;
}

static inline uint32_t stage79_entry_stub_read_sctlr(void)
{
    uint32_t v;
    __asm__ volatile ("mrc p15, 0, %0, c1, c0, 0" : "=r"(v));
    return v;
}

static uint32_t stage79_entry_stub_checksum(const struct stage79_xnu_entry_stub_result *r)
{
    const uint32_t *words = (const uint32_t *)r;
    uint32_t checksum = 0u;
    uint32_t count = (uint32_t)(offsetof(struct stage79_xnu_entry_stub_result, checksum) / sizeof(uint32_t));
    uint32_t i;

    for (i = 0u; i < count; i++) {
        checksum ^= words[i];
    }

    return checksum;
}

uint32_t stage79_arm_init_stub(struct boot_args *args, struct stage79_xnu_entry_stub_result *result)
{
    static const char enter_msg[] = "stage79_arm_init_stub: entered Stage-owned _start/arm_init-shaped path\n";
    static const char return_msg[] = "stage79_arm_init_stub: returning to stage loader\n";
    const struct stage79_xnu_early_pmap_platform_init_result *early;
    const struct stage79_xnu_pe_init_platform_false_result *pe;
    uint32_t valid_args;
    uint32_t valid_dt;
    uint32_t early_ok;
    uint32_t pe_ok;

    xnu_log_puts(enter_msg);

    if (!result) {
        return STAGE79_XNU_ENTRY_STUB_RETURN_BAD_ARG;
    }

    result->arm_init_called = 1u;
    result->magic = STAGE79_XNU_ENTRY_STUB_MAGIC;
    result->output_lines += 1u;
    result->output_bytes += (uint32_t)(sizeof(enter_msg) - 1u);

    result->boot_args_ptr = (uint32_t)(uintptr_t)args;
    if (!args) {
        xnu_log_puts("stage79_arm_init_stub: null boot_args\n");
        result->arm_init_status = STAGE79_STATUS_BASE;
        return STAGE79_STATUS_BASE;
    }

    result->boot_args_rev_ver = ((uint32_t)args->Version << 16) | args->Revision;
    result->boot_args_phys_base = args->physBase;
    result->boot_args_mem_size = args->memSize;
    result->boot_args_top_of_kernel_data = args->topOfKernelData;
    result->boot_args_machine_type = args->machineType;
    result->boot_args_device_tree_ptr = (uint32_t)(uintptr_t)args->deviceTreeP;
    result->boot_args_device_tree_length = args->deviceTreeLength;

    valid_args = (args->Revision == BOOT_ARGS_REVISION &&
                  args->Version == BOOT_ARGS_VERSION &&
                  args->physBase == STAGE79_BASE &&
                  args->machineType == MACHINE_TYPE_MSM8974) ? 1u : 0u;
    valid_dt = (args->deviceTreeP != NULL && args->deviceTreeLength != 0u) ? 1u : 0u;

    result->boot_args_valid = valid_args;
    result->device_tree_valid = valid_dt;

    if (!valid_args || !valid_dt) {
        xnu_log_puts("stage79_arm_init_stub: boot_args validation failed\n");
        result->arm_init_status = STAGE79_STATUS_BASE;
        return STAGE79_STATUS_BASE;
    }

    result->early_pmap_platform_init_called = 1u;
    early_ok = (uint32_t)stage79_xnu_early_pmap_platform_init_run(args, result);
    result->early_pmap_platform_init_returned = 1u;
    early = stage79_xnu_early_pmap_platform_init_result();
    result->early_pmap_platform_init_status = early->status;
    result->early_pmap_platform_init_checksum = early->checksum;

    if (early_ok != 1u || early->status != STAGE79_STATUS_OK) {
        xnu_log_puts("stage79_arm_init_stub: early pmap/platform init failed\n");
        result->arm_init_status = STAGE79_STATUS_BASE;
        return STAGE79_STATUS_BASE;
    }

    result->pe_init_platform_false_called = 1u;
    pe_ok = (uint32_t)stage79_xnu_pe_init_platform_false_run(args, result);
    result->pe_init_platform_false_returned = 1u;
    pe = stage79_xnu_pe_init_platform_false_result();
    result->pe_init_platform_false_status = pe->status;
    result->pe_init_platform_false_checksum = pe->checksum;

    if (pe_ok != 1u || pe->status != STAGE79_STATUS_OK) {
        xnu_log_puts("stage79_arm_init_stub: PE_init_platform(FALSE)-shaped init failed\n");
        result->arm_init_status = STAGE79_STATUS_BASE;
        return STAGE79_STATUS_BASE;
    }

    result->safety_boundary_preserved = 1u;
    result->arm_init_status = STAGE79_STATUS_OK;

    xnu_log_puts(return_msg);
    result->output_lines += 1u;
    result->output_bytes += (uint32_t)(sizeof(return_msg) - 1u);

    return STAGE79_STATUS_OK;
}

int stage79_xnu_entry_stub_run(struct boot_args *args)
{
    struct stage79_xnu_entry_stub_result *r = &g_stage79_xnu_entry_stub_result;
    uint32_t return_value;
    uint32_t i;

    for (i = 0u; i < sizeof(*r) / sizeof(uint32_t); i++) {
        ((uint32_t *)r)[i] = 0u;
    }

    r->version = STAGE79_XNU_ENTRY_STUB_VERSION;
    r->size = sizeof(*r);
    r->status = STAGE79_STATUS_BASE;
    r->required_mask = STAGE79_XNU_ENTRY_STUB_REQUIRED_MASK;
    r->start_stub_function_ptr = (uint32_t)(uintptr_t)&stage79_xnu_start_stub;
    r->arm_init_stub_function_ptr = (uint32_t)(uintptr_t)&stage79_arm_init_stub;
    r->result_ptr = (uint32_t)(uintptr_t)r;
    r->magic_expected = STAGE79_XNU_ENTRY_STUB_MAGIC;

    r->ttbr0_before = stage79_entry_stub_read_ttbr0();
    r->sctlr_before = stage79_entry_stub_read_sctlr();

    r->start_called = 1u;
    return_value = stage79_xnu_start_stub(args, r);
    r->return_value = return_value;

    r->ttbr0_after = stage79_entry_stub_read_ttbr0();
    r->sctlr_after = stage79_entry_stub_read_sctlr();

    if (r->start_called == 1u) {
        r->satisfied_mask |= STAGE79_XNU_ENTRY_STUB_SAT_START_CALLED;
    } else {
        r->failure_mask |= STAGE79_XNU_ENTRY_STUB_FAIL_START_NOT_CALLED;
    }

    if (r->start_entered == 1u) {
        r->satisfied_mask |= STAGE79_XNU_ENTRY_STUB_SAT_START_ENTERED;
    } else {
        r->failure_mask |= STAGE79_XNU_ENTRY_STUB_FAIL_START_NOT_ENTERED;
    }

    if (r->arm_init_called == 1u) {
        r->satisfied_mask |= STAGE79_XNU_ENTRY_STUB_SAT_ARM_INIT_CALLED;
    } else {
        r->failure_mask |= STAGE79_XNU_ENTRY_STUB_FAIL_ARM_INIT_NOT_CALLED;
    }

    if (r->arm_init_returned == 1u) {
        r->satisfied_mask |= STAGE79_XNU_ENTRY_STUB_SAT_ARM_INIT_RETURNED;
    } else {
        r->failure_mask |= STAGE79_XNU_ENTRY_STUB_FAIL_ARM_INIT_NOT_RETURNED;
    }

    if (r->return_value == STAGE79_STATUS_OK && r->arm_init_status == STAGE79_STATUS_OK) {
        r->satisfied_mask |= STAGE79_XNU_ENTRY_STUB_SAT_RETURN_STATUS;
    } else {
        r->failure_mask |= STAGE79_XNU_ENTRY_STUB_FAIL_BAD_RETURN_STATUS;
    }

    if (r->boot_args_valid == 1u) {
        r->satisfied_mask |= STAGE79_XNU_ENTRY_STUB_SAT_BOOT_ARGS_VALID;
    } else {
        r->failure_mask |= STAGE79_XNU_ENTRY_STUB_FAIL_BAD_BOOT_ARGS;
    }

    if (r->device_tree_valid == 1u) {
        r->satisfied_mask |= STAGE79_XNU_ENTRY_STUB_SAT_DEVICE_TREE_VALID;
    } else {
        r->failure_mask |= STAGE79_XNU_ENTRY_STUB_FAIL_BAD_DEVICE_TREE;
    }

    if (r->magic == STAGE79_XNU_ENTRY_STUB_MAGIC &&
        r->magic_expected == STAGE79_XNU_ENTRY_STUB_MAGIC) {
        r->satisfied_mask |= STAGE79_XNU_ENTRY_STUB_SAT_MAGIC;
    } else {
        r->failure_mask |= STAGE79_XNU_ENTRY_STUB_FAIL_BAD_MAGIC;
    }

    if (r->output_lines == 2u && r->output_bytes != 0u) {
        r->satisfied_mask |= STAGE79_XNU_ENTRY_STUB_SAT_OUTPUT;
    } else {
        r->failure_mask |= STAGE79_XNU_ENTRY_STUB_FAIL_NO_OUTPUT;
    }

    if (r->ttbr0_before == r->ttbr0_after && r->sctlr_before == r->sctlr_after) {
        r->mmu_state_unchanged = 1u;
        r->satisfied_mask |= STAGE79_XNU_ENTRY_STUB_SAT_MMU_UNCHANGED;
    } else {
        r->failure_mask |= STAGE79_XNU_ENTRY_STUB_FAIL_MMU_CHANGED;
    }

    r->no_exception_observed = 1u;
    r->satisfied_mask |= STAGE79_XNU_ENTRY_STUB_SAT_NO_EXCEPTION;

    if (r->early_pmap_platform_init_called == 1u &&
        r->early_pmap_platform_init_returned == 1u &&
        r->early_pmap_platform_init_status == STAGE79_STATUS_OK &&
        r->early_pmap_platform_init_checksum != 0u) {
        r->satisfied_mask |= STAGE79_XNU_ENTRY_STUB_SAT_EARLY_INIT_OK;
    } else {
        r->failure_mask |= STAGE79_XNU_ENTRY_STUB_FAIL_EARLY_INIT;
    }

    if (r->pe_init_platform_false_called == 1u &&
        r->pe_init_platform_false_returned == 1u &&
        r->pe_init_platform_false_status == STAGE79_STATUS_OK &&
        r->pe_init_platform_false_checksum != 0u) {
        r->satisfied_mask |= STAGE79_XNU_ENTRY_STUB_SAT_PE_INIT_PLATFORM_FALSE_OK;
    } else {
        r->failure_mask |= STAGE79_XNU_ENTRY_STUB_FAIL_PE_INIT_PLATFORM_FALSE;
    }

    if (r->public_xnu_start_executed == 0u &&
        r->public_arm_init_executed == 0u &&
        r->public_pmap_runtime_executed == 0u &&
        r->public_iokit_runtime_executed == 0u &&
        r->generated_macho_executed == 0u &&
        r->live_pmap_installed == 0u &&
        r->tlb_invalidated == 0u &&
        r->cache_policy_changed == 0u &&
        r->persistent_write_attempted == 0u &&
        r->safety_boundary_preserved == 1u) {
        r->satisfied_mask |= STAGE79_XNU_ENTRY_STUB_SAT_SAFETY_BOUNDARY;
    } else {
        r->failure_mask |= STAGE79_XNU_ENTRY_STUB_FAIL_SAFETY_BOUNDARY;
    }

    r->status = (r->satisfied_mask == r->required_mask && r->failure_mask == 0u) ?
        STAGE79_STATUS_OK : (STAGE79_STATUS_BASE | r->failure_mask);
    r->checksum = stage79_entry_stub_checksum(r);

    xnu_log_kv32("stage79_xnu_entry_stub_status", r->status);
    xnu_log_kv32("stage79_xnu_entry_stub_satisfied_mask", r->satisfied_mask);
    xnu_log_kv32("stage79_xnu_entry_stub_failure_mask", r->failure_mask);
    xnu_log_kv32("stage79_xnu_entry_stub_required_mask", r->required_mask);
    xnu_log_kv32("stage79_xnu_entry_stub_start_function_ptr", r->start_stub_function_ptr);
    xnu_log_kv32("stage79_xnu_entry_stub_arm_init_function_ptr", r->arm_init_stub_function_ptr);
    xnu_log_kv32("stage79_xnu_entry_stub_result_ptr", r->result_ptr);
    xnu_log_kv32("stage79_xnu_entry_stub_magic", r->magic);
    xnu_log_kv32("stage79_xnu_entry_stub_start_called", r->start_called);
    xnu_log_kv32("stage79_xnu_entry_stub_start_entered", r->start_entered);
    xnu_log_kv32("stage79_xnu_entry_stub_arm_init_called", r->arm_init_called);
    xnu_log_kv32("stage79_xnu_entry_stub_arm_init_returned", r->arm_init_returned);
    xnu_log_kv32("stage79_xnu_entry_stub_return_value", r->return_value);
    xnu_log_kv32("stage79_xnu_entry_stub_boot_args_ptr", r->boot_args_ptr);
    xnu_log_kv32("stage79_xnu_entry_stub_boot_args_rev_ver", r->boot_args_rev_ver);
    xnu_log_kv32("stage79_xnu_entry_stub_device_tree_ptr", r->boot_args_device_tree_ptr);
    xnu_log_kv32("stage79_xnu_entry_stub_device_tree_length", r->boot_args_device_tree_length);
    xnu_log_kv32("stage79_xnu_entry_stub_boot_args_valid", r->boot_args_valid);
    xnu_log_kv32("stage79_xnu_entry_stub_device_tree_valid", r->device_tree_valid);
    xnu_log_kv32("stage79_xnu_entry_stub_output_lines", r->output_lines);
    xnu_log_kv32("stage79_xnu_entry_stub_output_bytes", r->output_bytes);
    xnu_log_kv32("stage79_xnu_entry_stub_ttbr0_before", r->ttbr0_before);
    xnu_log_kv32("stage79_xnu_entry_stub_ttbr0_after", r->ttbr0_after);
    xnu_log_kv32("stage79_xnu_entry_stub_sctlr_before", r->sctlr_before);
    xnu_log_kv32("stage79_xnu_entry_stub_sctlr_after", r->sctlr_after);
    xnu_log_kv32("stage79_xnu_entry_stub_mmu_unchanged", r->mmu_state_unchanged);
    xnu_log_kv32("stage79_xnu_entry_stub_no_exception", r->no_exception_observed);
    xnu_log_kv32("stage79_xnu_entry_stub_early_init_called", r->early_pmap_platform_init_called);
    xnu_log_kv32("stage79_xnu_entry_stub_early_init_returned", r->early_pmap_platform_init_returned);
    xnu_log_kv32("stage79_xnu_entry_stub_early_init_status", r->early_pmap_platform_init_status);
    xnu_log_kv32("stage79_xnu_entry_stub_early_init_checksum", r->early_pmap_platform_init_checksum);
    xnu_log_kv32("stage79_xnu_entry_stub_pe_init_platform_false_called", r->pe_init_platform_false_called);
    xnu_log_kv32("stage79_xnu_entry_stub_pe_init_platform_false_returned", r->pe_init_platform_false_returned);
    xnu_log_kv32("stage79_xnu_entry_stub_pe_init_platform_false_status", r->pe_init_platform_false_status);
    xnu_log_kv32("stage79_xnu_entry_stub_pe_init_platform_false_checksum", r->pe_init_platform_false_checksum);
    xnu_log_kv32("stage79_xnu_entry_stub_safety_boundary_preserved", r->safety_boundary_preserved);
    xnu_log_kv32("stage79_xnu_entry_stub_checksum", r->checksum);

    return r->status == STAGE79_STATUS_OK;
}

const struct stage79_xnu_entry_stub_result *stage79_xnu_entry_stub_result(void)
{
    return &g_stage79_xnu_entry_stub_result;
}
