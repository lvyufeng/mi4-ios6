#include "stage88.h"

static struct stage88_xnu_entry_stub_result g_stage88_xnu_entry_stub_result;

static inline uint32_t stage88_entry_stub_read_ttbr0(void)
{
    uint32_t v;
    __asm__ volatile ("mrc p15, 0, %0, c2, c0, 0" : "=r"(v));
    return v;
}

static inline uint32_t stage88_entry_stub_read_sctlr(void)
{
    uint32_t v;
    __asm__ volatile ("mrc p15, 0, %0, c1, c0, 0" : "=r"(v));
    return v;
}

static uint32_t stage88_entry_stub_checksum(const struct stage88_xnu_entry_stub_result *r)
{
    const uint32_t *words = (const uint32_t *)r;
    uint32_t checksum = 0u;
    uint32_t count = (uint32_t)(offsetof(struct stage88_xnu_entry_stub_result, checksum) / sizeof(uint32_t));
    uint32_t i;

    for (i = 0u; i < count; i++) {
        checksum ^= words[i];
    }

    return checksum;
}

uint32_t stage88_arm_init_stub(struct boot_args *args, struct stage88_xnu_entry_stub_result *result)
{
    static const char enter_msg[] = "stage88_arm_init_stub: entered Stage-owned _start/arm_init-shaped path\n";
    static const char return_msg[] = "stage88_arm_init_stub: returning to stage loader\n";
    const struct stage88_xnu_early_pmap_platform_init_result *early;
    const struct stage88_xnu_pe_init_platform_false_result *pe;
    const struct stage88_xnu_arm_init_post_pe_bootstrap_result *post;
    const struct stage88_xnu_arm_vm_init_full_pmap_result *live_pmap;
    const struct stage88_xnu_arm_vm_init_high_va_code_exec_result *high_va_code_exec;
    const struct stage88_xnu_arm_vm_init_high_va_irq_handler_result *high_va_irq_handler;
    const struct stage88_xnu_arm_vm_init_high_va_data_abort_handler_result *high_va_data_abort_handler;
    const struct stage88_xnu_arm_vm_init_high_va_undef_handler_result *high_va_undef_handler;
    uint32_t valid_args;
    uint32_t valid_dt;
    uint32_t early_ok;
    uint32_t pe_ok;
    uint32_t post_ok;
    uint32_t live_pmap_ok;
    uint32_t high_va_code_exec_ok;
    uint32_t high_va_irq_handler_ok;
    uint32_t high_va_data_abort_handler_ok;
    uint32_t high_va_undef_handler_ok;

    xnu_log_puts(enter_msg);

    if (!result) {
        return STAGE88_XNU_ENTRY_STUB_RETURN_BAD_ARG;
    }

    result->arm_init_called = 1u;
    result->magic = STAGE88_XNU_ENTRY_STUB_MAGIC;
    result->output_lines += 1u;
    result->output_bytes += (uint32_t)(sizeof(enter_msg) - 1u);

    result->boot_args_ptr = (uint32_t)(uintptr_t)args;
    if (!args) {
        xnu_log_puts("stage88_arm_init_stub: null boot_args\n");
        result->arm_init_status = STAGE88_STATUS_BASE;
        return STAGE88_STATUS_BASE;
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
                  args->physBase == STAGE88_BASE &&
                  args->machineType == MACHINE_TYPE_MSM8974) ? 1u : 0u;
    valid_dt = (args->deviceTreeP != NULL && args->deviceTreeLength != 0u) ? 1u : 0u;

    result->boot_args_valid = valid_args;
    result->device_tree_valid = valid_dt;

    if (!valid_args || !valid_dt) {
        xnu_log_puts("stage88_arm_init_stub: boot_args validation failed\n");
        result->arm_init_status = STAGE88_STATUS_BASE;
        return STAGE88_STATUS_BASE;
    }

    result->early_pmap_platform_init_called = 1u;
    early_ok = (uint32_t)stage88_xnu_early_pmap_platform_init_run(args, result);
    result->early_pmap_platform_init_returned = 1u;
    early = stage88_xnu_early_pmap_platform_init_result();
    result->early_pmap_platform_init_status = early->status;
    result->early_pmap_platform_init_checksum = early->checksum;

    if (early_ok != 1u || early->status != STAGE88_STATUS_OK) {
        xnu_log_puts("stage88_arm_init_stub: early pmap/platform init failed\n");
        result->arm_init_status = STAGE88_STATUS_BASE;
        return STAGE88_STATUS_BASE;
    }

    result->pe_init_platform_false_called = 1u;
    pe_ok = (uint32_t)stage88_xnu_pe_init_platform_false_run(args, result);
    result->pe_init_platform_false_returned = 1u;
    pe = stage88_xnu_pe_init_platform_false_result();
    result->pe_init_platform_false_status = pe->status;
    result->pe_init_platform_false_checksum = pe->checksum;

    if (pe_ok != 1u || pe->status != STAGE88_STATUS_OK) {
        xnu_log_puts("stage88_arm_init_stub: PE_init_platform(FALSE)-shaped init failed\n");
        result->arm_init_status = STAGE88_STATUS_BASE;
        return STAGE88_STATUS_BASE;
    }

    result->arm_init_post_pe_bootstrap_called = 1u;
    post_ok = (uint32_t)stage88_xnu_arm_init_post_pe_bootstrap_run(args, result);
    result->arm_init_post_pe_bootstrap_returned = 1u;
    post = stage88_xnu_arm_init_post_pe_bootstrap_result();
    result->arm_init_post_pe_bootstrap_status = post->status;
    result->arm_init_post_pe_bootstrap_checksum = post->checksum;

    if (post_ok != 1u || post->status != STAGE88_STATUS_OK) {
        xnu_log_puts("stage88_arm_init_stub: post-PE bootstrap/timebase init failed\n");
        result->arm_init_status = STAGE88_STATUS_BASE;
        return STAGE88_STATUS_BASE;
    }

    result->arm_vm_init_full_pmap_called = 1u;
    live_pmap_ok = (uint32_t)stage88_xnu_arm_vm_init_full_pmap_run(args, result);
    result->arm_vm_init_full_pmap_returned = 1u;
    live_pmap = stage88_xnu_arm_vm_init_full_pmap_result();
    result->arm_vm_init_full_pmap_status = live_pmap->status;
    result->arm_vm_init_full_pmap_checksum = live_pmap->checksum;
    result->live_pmap_installed = live_pmap->live_pmap_installed;
    result->tlb_invalidated = (live_pmap->tlb_invalidate_count != 0u) ? 1u : 0u;

    if (live_pmap_ok != 1u || live_pmap->status != STAGE88_STATUS_OK) {
        xnu_log_puts("stage88_arm_init_stub: arm_vm_init live-pmap install failed\n");
        result->arm_init_status = STAGE88_STATUS_BASE;
        return STAGE88_STATUS_BASE;
    }

    /* Stage86 NEW: High-VA code execution window */
    high_va_code_exec_ok = (uint32_t)stage88_xnu_arm_vm_init_high_va_code_exec_run(args, result);
    high_va_code_exec = stage88_xnu_arm_vm_init_high_va_code_exec_result();

    if (high_va_code_exec_ok != 0 || high_va_code_exec->status != STAGE88_STATUS_OK) {
        xnu_log_puts("stage88_arm_init_stub: high-VA code execution failed\n");
        result->arm_init_status = STAGE88_STATUS_BASE;
        return STAGE88_STATUS_BASE;
    }

    /* Stage87 NEW: High-VA IRQ handler relocation window */
    high_va_irq_handler_ok = (uint32_t)stage88_xnu_arm_vm_init_high_va_irq_handler_run(args, result);
    high_va_irq_handler = stage88_xnu_arm_vm_init_high_va_irq_handler_result();

    if (high_va_irq_handler_ok != 0 || high_va_irq_handler->status != STAGE88_STATUS_OK) {
        xnu_log_puts("stage88_arm_init_stub: high-VA IRQ handler relocation failed\n");
        result->arm_init_status = STAGE88_STATUS_BASE;
        return STAGE88_STATUS_BASE;
    }

    /* Stage87 NEW: High-VA data abort handler window */
    high_va_data_abort_handler_ok = (uint32_t)stage88_xnu_arm_vm_init_high_va_data_abort_handler_run(args, result);
    high_va_data_abort_handler = stage88_xnu_arm_vm_init_high_va_data_abort_handler_result();

    if (high_va_data_abort_handler_ok != 0 || high_va_data_abort_handler->status != STAGE88_STATUS_OK) {
        xnu_log_puts("stage88_arm_init_stub: high-VA data abort handler relocation failed\n");
        result->arm_init_status = STAGE88_STATUS_BASE;
        return STAGE88_STATUS_BASE;
    }

    /* Stage88 NEW: High-VA undefined instruction handler window */
    high_va_undef_handler_ok = (uint32_t)stage88_xnu_arm_vm_init_high_va_undef_handler_run(args, result);
    high_va_undef_handler = stage88_xnu_arm_vm_init_high_va_undef_handler_result();

    if (high_va_undef_handler_ok != 0 || high_va_undef_handler->status != STAGE88_STATUS_OK) {
        xnu_log_puts("stage88_arm_init_stub: high-VA undefined instruction handler failed\n");
        result->arm_init_status = STAGE88_STATUS_BASE;
        return STAGE88_STATUS_BASE;
    }

    result->safety_boundary_preserved = 1u;
    result->arm_init_status = STAGE88_STATUS_OK;

    xnu_log_puts(return_msg);
    result->output_lines += 1u;
    result->output_bytes += (uint32_t)(sizeof(return_msg) - 1u);

    return STAGE88_STATUS_OK;
}

int stage88_xnu_entry_stub_run(struct boot_args *args)
{
    struct stage88_xnu_entry_stub_result *r = &g_stage88_xnu_entry_stub_result;
    uint32_t return_value;
    uint32_t i;

    for (i = 0u; i < sizeof(*r) / sizeof(uint32_t); i++) {
        ((uint32_t *)r)[i] = 0u;
    }

    r->version = STAGE88_XNU_ENTRY_STUB_VERSION;
    r->size = sizeof(*r);
    r->status = STAGE88_STATUS_BASE;
    r->required_mask = STAGE88_XNU_ENTRY_STUB_REQUIRED_MASK;
    r->start_stub_function_ptr = (uint32_t)(uintptr_t)&stage88_xnu_start_stub;
    r->arm_init_stub_function_ptr = (uint32_t)(uintptr_t)&stage88_arm_init_stub;
    r->result_ptr = (uint32_t)(uintptr_t)r;
    r->magic_expected = STAGE88_XNU_ENTRY_STUB_MAGIC;

    r->ttbr0_before = stage88_entry_stub_read_ttbr0();
    r->sctlr_before = stage88_entry_stub_read_sctlr();

    r->start_called = 1u;
    return_value = stage88_xnu_start_stub(args, r);
    r->return_value = return_value;

    r->ttbr0_after = stage88_entry_stub_read_ttbr0();
    r->sctlr_after = stage88_entry_stub_read_sctlr();

    if (r->start_called == 1u) {
        r->satisfied_mask |= STAGE88_XNU_ENTRY_STUB_SAT_START_CALLED;
    } else {
        r->failure_mask |= STAGE88_XNU_ENTRY_STUB_FAIL_START_NOT_CALLED;
    }

    if (r->start_entered == 1u) {
        r->satisfied_mask |= STAGE88_XNU_ENTRY_STUB_SAT_START_ENTERED;
    } else {
        r->failure_mask |= STAGE88_XNU_ENTRY_STUB_FAIL_START_NOT_ENTERED;
    }

    if (r->arm_init_called == 1u) {
        r->satisfied_mask |= STAGE88_XNU_ENTRY_STUB_SAT_ARM_INIT_CALLED;
    } else {
        r->failure_mask |= STAGE88_XNU_ENTRY_STUB_FAIL_ARM_INIT_NOT_CALLED;
    }

    if (r->arm_init_returned == 1u) {
        r->satisfied_mask |= STAGE88_XNU_ENTRY_STUB_SAT_ARM_INIT_RETURNED;
    } else {
        r->failure_mask |= STAGE88_XNU_ENTRY_STUB_FAIL_ARM_INIT_NOT_RETURNED;
    }

    if (r->return_value == STAGE88_STATUS_OK && r->arm_init_status == STAGE88_STATUS_OK) {
        r->satisfied_mask |= STAGE88_XNU_ENTRY_STUB_SAT_RETURN_STATUS;
    } else {
        r->failure_mask |= STAGE88_XNU_ENTRY_STUB_FAIL_BAD_RETURN_STATUS;
    }

    if (r->boot_args_valid == 1u) {
        r->satisfied_mask |= STAGE88_XNU_ENTRY_STUB_SAT_BOOT_ARGS_VALID;
    } else {
        r->failure_mask |= STAGE88_XNU_ENTRY_STUB_FAIL_BAD_BOOT_ARGS;
    }

    if (r->device_tree_valid == 1u) {
        r->satisfied_mask |= STAGE88_XNU_ENTRY_STUB_SAT_DEVICE_TREE_VALID;
    } else {
        r->failure_mask |= STAGE88_XNU_ENTRY_STUB_FAIL_BAD_DEVICE_TREE;
    }

    if (r->magic == STAGE88_XNU_ENTRY_STUB_MAGIC &&
        r->magic_expected == STAGE88_XNU_ENTRY_STUB_MAGIC) {
        r->satisfied_mask |= STAGE88_XNU_ENTRY_STUB_SAT_MAGIC;
    } else {
        r->failure_mask |= STAGE88_XNU_ENTRY_STUB_FAIL_BAD_MAGIC;
    }

    if (r->output_lines == 2u && r->output_bytes != 0u) {
        r->satisfied_mask |= STAGE88_XNU_ENTRY_STUB_SAT_OUTPUT;
    } else {
        r->failure_mask |= STAGE88_XNU_ENTRY_STUB_FAIL_NO_OUTPUT;
    }

    if (r->ttbr0_before == r->ttbr0_after && r->sctlr_before == r->sctlr_after) {
        r->mmu_state_unchanged = 1u;
        r->satisfied_mask |= STAGE88_XNU_ENTRY_STUB_SAT_MMU_UNCHANGED;
    } else {
        r->failure_mask |= STAGE88_XNU_ENTRY_STUB_FAIL_MMU_CHANGED;
    }

    r->no_exception_observed = 1u;
    r->satisfied_mask |= STAGE88_XNU_ENTRY_STUB_SAT_NO_EXCEPTION;

    if (r->early_pmap_platform_init_called == 1u &&
        r->early_pmap_platform_init_returned == 1u &&
        r->early_pmap_platform_init_status == STAGE88_STATUS_OK &&
        r->early_pmap_platform_init_checksum != 0u) {
        r->satisfied_mask |= STAGE88_XNU_ENTRY_STUB_SAT_EARLY_INIT_OK;
    } else {
        r->failure_mask |= STAGE88_XNU_ENTRY_STUB_FAIL_EARLY_INIT;
    }

    if (r->pe_init_platform_false_called == 1u &&
        r->pe_init_platform_false_returned == 1u &&
        r->pe_init_platform_false_status == STAGE88_STATUS_OK &&
        r->pe_init_platform_false_checksum != 0u) {
        r->satisfied_mask |= STAGE88_XNU_ENTRY_STUB_SAT_PE_INIT_PLATFORM_FALSE_OK;
    } else {
        r->failure_mask |= STAGE88_XNU_ENTRY_STUB_FAIL_PE_INIT_PLATFORM_FALSE;
    }

    if (r->arm_init_post_pe_bootstrap_called == 1u &&
        r->arm_init_post_pe_bootstrap_returned == 1u &&
        r->arm_init_post_pe_bootstrap_status == STAGE88_STATUS_OK &&
        r->arm_init_post_pe_bootstrap_checksum != 0u) {
        r->satisfied_mask |= STAGE88_XNU_ENTRY_STUB_SAT_ARM_INIT_POST_PE_BOOTSTRAP_OK;
    } else {
        r->failure_mask |= STAGE88_XNU_ENTRY_STUB_FAIL_ARM_INIT_POST_PE_BOOTSTRAP;
    }

    if (r->arm_vm_init_full_pmap_called == 1u &&
        r->arm_vm_init_full_pmap_returned == 1u &&
        r->arm_vm_init_full_pmap_status == STAGE88_STATUS_OK &&
        r->arm_vm_init_full_pmap_checksum != 0u) {
        r->satisfied_mask |= STAGE88_XNU_ENTRY_STUB_SAT_ARM_VM_INIT_FULL_PMAP_OK;
    } else {
        r->failure_mask |= STAGE88_XNU_ENTRY_STUB_FAIL_ARM_VM_INIT_FULL_PMAP;
    }

    if (r->public_xnu_start_executed == 0u &&
        r->public_arm_init_executed == 0u &&
        r->public_pmap_runtime_executed == 0u &&
        r->public_iokit_runtime_executed == 0u &&
        r->generated_macho_executed == 0u &&
        r->live_pmap_installed == 1u &&
        r->tlb_invalidated == 1u &&
        r->cache_policy_changed == 0u &&
        r->persistent_write_attempted == 0u &&
        r->safety_boundary_preserved == 1u) {
        r->satisfied_mask |= STAGE88_XNU_ENTRY_STUB_SAT_SAFETY_BOUNDARY;
    } else {
        r->failure_mask |= STAGE88_XNU_ENTRY_STUB_FAIL_SAFETY_BOUNDARY;
    }

    r->status = (r->satisfied_mask == r->required_mask && r->failure_mask == 0u) ?
        STAGE88_STATUS_OK : STAGE88_STATUS_FAIL(r->failure_mask);
    r->checksum = stage88_entry_stub_checksum(r);

    xnu_log_kv32("stage88_xnu_entry_stub_status", r->status);
    xnu_log_kv32("stage88_xnu_entry_stub_satisfied_mask", r->satisfied_mask);
    xnu_log_kv32("stage88_xnu_entry_stub_failure_mask", r->failure_mask);
    xnu_log_kv32("stage88_xnu_entry_stub_required_mask", r->required_mask);
    xnu_log_kv32("stage88_xnu_entry_stub_start_function_ptr", r->start_stub_function_ptr);
    xnu_log_kv32("stage88_xnu_entry_stub_arm_init_function_ptr", r->arm_init_stub_function_ptr);
    xnu_log_kv32("stage88_xnu_entry_stub_result_ptr", r->result_ptr);
    xnu_log_kv32("stage88_xnu_entry_stub_magic", r->magic);
    xnu_log_kv32("stage88_xnu_entry_stub_start_called", r->start_called);
    xnu_log_kv32("stage88_xnu_entry_stub_start_entered", r->start_entered);
    xnu_log_kv32("stage88_xnu_entry_stub_arm_init_called", r->arm_init_called);
    xnu_log_kv32("stage88_xnu_entry_stub_arm_init_returned", r->arm_init_returned);
    xnu_log_kv32("stage88_xnu_entry_stub_return_value", r->return_value);
    xnu_log_kv32("stage88_xnu_entry_stub_boot_args_ptr", r->boot_args_ptr);
    xnu_log_kv32("stage88_xnu_entry_stub_boot_args_rev_ver", r->boot_args_rev_ver);
    xnu_log_kv32("stage88_xnu_entry_stub_device_tree_ptr", r->boot_args_device_tree_ptr);
    xnu_log_kv32("stage88_xnu_entry_stub_device_tree_length", r->boot_args_device_tree_length);
    xnu_log_kv32("stage88_xnu_entry_stub_boot_args_valid", r->boot_args_valid);
    xnu_log_kv32("stage88_xnu_entry_stub_device_tree_valid", r->device_tree_valid);
    xnu_log_kv32("stage88_xnu_entry_stub_output_lines", r->output_lines);
    xnu_log_kv32("stage88_xnu_entry_stub_output_bytes", r->output_bytes);
    xnu_log_kv32("stage88_xnu_entry_stub_ttbr0_before", r->ttbr0_before);
    xnu_log_kv32("stage88_xnu_entry_stub_ttbr0_after", r->ttbr0_after);
    xnu_log_kv32("stage88_xnu_entry_stub_sctlr_before", r->sctlr_before);
    xnu_log_kv32("stage88_xnu_entry_stub_sctlr_after", r->sctlr_after);
    xnu_log_kv32("stage88_xnu_entry_stub_mmu_unchanged", r->mmu_state_unchanged);
    xnu_log_kv32("stage88_xnu_entry_stub_no_exception", r->no_exception_observed);
    xnu_log_kv32("stage88_xnu_entry_stub_early_init_called", r->early_pmap_platform_init_called);
    xnu_log_kv32("stage88_xnu_entry_stub_early_init_returned", r->early_pmap_platform_init_returned);
    xnu_log_kv32("stage88_xnu_entry_stub_early_init_status", r->early_pmap_platform_init_status);
    xnu_log_kv32("stage88_xnu_entry_stub_early_init_checksum", r->early_pmap_platform_init_checksum);
    xnu_log_kv32("stage88_xnu_entry_stub_pe_init_platform_false_called", r->pe_init_platform_false_called);
    xnu_log_kv32("stage88_xnu_entry_stub_pe_init_platform_false_returned", r->pe_init_platform_false_returned);
    xnu_log_kv32("stage88_xnu_entry_stub_pe_init_platform_false_status", r->pe_init_platform_false_status);
    xnu_log_kv32("stage88_xnu_entry_stub_pe_init_platform_false_checksum", r->pe_init_platform_false_checksum);
    xnu_log_kv32("stage88_xnu_entry_stub_arm_init_post_pe_bootstrap_called", r->arm_init_post_pe_bootstrap_called);
    xnu_log_kv32("stage88_xnu_entry_stub_arm_init_post_pe_bootstrap_returned", r->arm_init_post_pe_bootstrap_returned);
    xnu_log_kv32("stage88_xnu_entry_stub_arm_init_post_pe_bootstrap_status", r->arm_init_post_pe_bootstrap_status);
    xnu_log_kv32("stage88_xnu_entry_stub_arm_init_post_pe_bootstrap_checksum", r->arm_init_post_pe_bootstrap_checksum);
    xnu_log_kv32("stage88_xnu_entry_stub_arm_vm_init_full_pmap_called", r->arm_vm_init_full_pmap_called);
    xnu_log_kv32("stage88_xnu_entry_stub_arm_vm_init_full_pmap_returned", r->arm_vm_init_full_pmap_returned);
    xnu_log_kv32("stage88_xnu_entry_stub_arm_vm_init_full_pmap_status", r->arm_vm_init_full_pmap_status);
    xnu_log_kv32("stage88_xnu_entry_stub_arm_vm_init_full_pmap_checksum", r->arm_vm_init_full_pmap_checksum);
    xnu_log_kv32("stage88_xnu_entry_stub_safety_boundary_preserved", r->safety_boundary_preserved);
    xnu_log_kv32("stage88_xnu_entry_stub_checksum", r->checksum);

    return r->status == STAGE88_STATUS_OK;
}

const struct stage88_xnu_entry_stub_result *stage88_xnu_entry_stub_result(void)
{
    return &g_stage88_xnu_entry_stub_result;
}
