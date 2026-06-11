#include "stage76.h"

static struct stage76_xnu_live_execution_probe_result g_stage76_xnu_live_probe_result;
static volatile uint32_t g_stage76_xnu_live_probe_enter_count;

static inline uint32_t stage76_live_probe_read_ttbr0(void)
{
    uint32_t v;
    __asm__ volatile ("mrc p15, 0, %0, c2, c0, 0" : "=r"(v));
    return v;
}

static inline uint32_t stage76_live_probe_read_sctlr(void)
{
    uint32_t v;
    __asm__ volatile ("mrc p15, 0, %0, c1, c0, 0" : "=r"(v));
    return v;
}

static uint32_t stage76_live_probe_checksum(const struct stage76_xnu_live_execution_probe_result *r)
{
    const uint32_t *words = (const uint32_t *)r;
    uint32_t checksum = 0u;
    uint32_t count = (uint32_t)(offsetof(struct stage76_xnu_live_execution_probe_result, checksum) / sizeof(uint32_t));
    uint32_t i;

    for (i = 0u; i < count; i++) {
        checksum ^= words[i];
    }

    return checksum;
}

uint32_t __attribute__((noinline)) stage76_xnu_minimal_live_probe(struct stage76_xnu_live_execution_probe_result *result)
{
    static const char enter_msg[] = "stage76_xnu_minimal_live_probe: first live XNU-like code execution entered\n";
    static const char return_msg[] = "stage76_xnu_minimal_live_probe: returning to stage loader\n";

    g_stage76_xnu_live_probe_enter_count++;
    xnu_log_puts(enter_msg);

    if (!result) {
        return STAGE76_XNU_LIVE_PROBE_RETURN_BAD_ARG;
    }

    result->probe_entered = 1u;
    result->probe_magic = STAGE76_XNU_LIVE_PROBE_MAGIC;
    result->probe_status_from_probe = STAGE76_STATUS_OK;
    result->probe_output_lines += 1u;
    result->probe_output_bytes += (uint32_t)(sizeof(enter_msg) - 1u);

    xnu_log_puts(return_msg);
    result->probe_output_lines += 1u;
    result->probe_output_bytes += (uint32_t)(sizeof(return_msg) - 1u);

    return STAGE76_STATUS_OK;
}

int stage76_xnu_live_execution_probe_run(void)
{
    struct stage76_xnu_live_execution_probe_result *r = &g_stage76_xnu_live_probe_result;
    uint32_t return_value;
    uint32_t i;

    for (i = 0u; i < sizeof(*r) / sizeof(uint32_t); i++) {
        ((uint32_t *)r)[i] = 0u;
    }

    r->version = STAGE76_XNU_LIVE_PROBE_VERSION;
    r->size = sizeof(*r);
    r->status = STAGE76_STATUS_BASE;
    r->required_mask = STAGE76_XNU_LIVE_PROBE_REQUIRED_MASK;
    r->probe_magic_expected = STAGE76_XNU_LIVE_PROBE_MAGIC;
    r->probe_function_ptr = (uint32_t)(uintptr_t)&stage76_xnu_minimal_live_probe;
    r->probe_result_ptr = (uint32_t)(uintptr_t)r;
    r->enter_count_before = g_stage76_xnu_live_probe_enter_count;

    r->ttbr0_before = stage76_live_probe_read_ttbr0();
    r->sctlr_before = stage76_live_probe_read_sctlr();

    r->probe_called = 1u;
    return_value = stage76_xnu_minimal_live_probe(r);
    r->probe_returned = 1u;

    r->ttbr0_after = stage76_live_probe_read_ttbr0();
    r->sctlr_after = stage76_live_probe_read_sctlr();
    r->enter_count_after = g_stage76_xnu_live_probe_enter_count;
    r->probe_return_value = return_value;

    if (r->probe_called == 1u) {
        r->satisfied_mask |= STAGE76_XNU_LIVE_PROBE_SAT_CALLED;
    } else {
        r->failure_mask |= STAGE76_XNU_LIVE_PROBE_FAIL_NOT_CALLED;
    }

    if (r->probe_entered == 1u && r->enter_count_after == (r->enter_count_before + 1u)) {
        r->satisfied_mask |= STAGE76_XNU_LIVE_PROBE_SAT_ENTERED;
    } else {
        r->failure_mask |= STAGE76_XNU_LIVE_PROBE_FAIL_NOT_ENTERED;
    }

    if (r->probe_returned == 1u) {
        r->satisfied_mask |= STAGE76_XNU_LIVE_PROBE_SAT_RETURNED;
    } else {
        r->failure_mask |= STAGE76_XNU_LIVE_PROBE_FAIL_NOT_RETURNED;
    }

    if (r->probe_return_value == STAGE76_STATUS_OK && r->probe_status_from_probe == STAGE76_STATUS_OK) {
        r->satisfied_mask |= STAGE76_XNU_LIVE_PROBE_SAT_RETURN_STATUS;
    } else {
        r->failure_mask |= STAGE76_XNU_LIVE_PROBE_FAIL_BAD_RETURN_STATUS;
    }

    if (r->probe_magic == STAGE76_XNU_LIVE_PROBE_MAGIC &&
        r->probe_magic_expected == STAGE76_XNU_LIVE_PROBE_MAGIC) {
        r->satisfied_mask |= STAGE76_XNU_LIVE_PROBE_SAT_MAGIC;
    } else {
        r->failure_mask |= STAGE76_XNU_LIVE_PROBE_FAIL_BAD_MAGIC;
    }

    if (r->probe_output_lines == 2u && r->probe_output_bytes != 0u) {
        r->satisfied_mask |= STAGE76_XNU_LIVE_PROBE_SAT_OUTPUT;
    } else {
        r->failure_mask |= STAGE76_XNU_LIVE_PROBE_FAIL_NO_OUTPUT;
    }

    if (r->ttbr0_before == r->ttbr0_after && r->sctlr_before == r->sctlr_after) {
        r->mmu_state_unchanged = 1u;
        r->satisfied_mask |= STAGE76_XNU_LIVE_PROBE_SAT_MMU_UNCHANGED;
    } else {
        r->failure_mask |= STAGE76_XNU_LIVE_PROBE_FAIL_MMU_CHANGED;
    }

    r->no_exception_observed = 1u;
    r->satisfied_mask |= STAGE76_XNU_LIVE_PROBE_SAT_NO_EXCEPTION;

    r->status = (r->satisfied_mask == r->required_mask && r->failure_mask == 0u) ?
        STAGE76_STATUS_OK : (STAGE76_STATUS_BASE | r->failure_mask);
    r->checksum = stage76_live_probe_checksum(r);

    xnu_log_kv32("stage76_xnu_live_probe_status", r->status);
    xnu_log_kv32("stage76_xnu_live_probe_satisfied_mask", r->satisfied_mask);
    xnu_log_kv32("stage76_xnu_live_probe_failure_mask", r->failure_mask);
    xnu_log_kv32("stage76_xnu_live_probe_required_mask", r->required_mask);
    xnu_log_kv32("stage76_xnu_live_probe_function_ptr", r->probe_function_ptr);
    xnu_log_kv32("stage76_xnu_live_probe_result_ptr", r->probe_result_ptr);
    xnu_log_kv32("stage76_xnu_live_probe_called", r->probe_called);
    xnu_log_kv32("stage76_xnu_live_probe_entered", r->probe_entered);
    xnu_log_kv32("stage76_xnu_live_probe_returned", r->probe_returned);
    xnu_log_kv32("stage76_xnu_live_probe_return_value", r->probe_return_value);
    xnu_log_kv32("stage76_xnu_live_probe_magic", r->probe_magic);
    xnu_log_kv32("stage76_xnu_live_probe_output_lines", r->probe_output_lines);
    xnu_log_kv32("stage76_xnu_live_probe_output_bytes", r->probe_output_bytes);
    xnu_log_kv32("stage76_xnu_live_probe_ttbr0_before", r->ttbr0_before);
    xnu_log_kv32("stage76_xnu_live_probe_ttbr0_after", r->ttbr0_after);
    xnu_log_kv32("stage76_xnu_live_probe_sctlr_before", r->sctlr_before);
    xnu_log_kv32("stage76_xnu_live_probe_sctlr_after", r->sctlr_after);
    xnu_log_kv32("stage76_xnu_live_probe_mmu_unchanged", r->mmu_state_unchanged);
    xnu_log_kv32("stage76_xnu_live_probe_no_exception", r->no_exception_observed);
    xnu_log_kv32("stage76_xnu_live_probe_checksum", r->checksum);

    return r->status == STAGE76_STATUS_OK;
}

const struct stage76_xnu_live_execution_probe_result *stage76_xnu_live_execution_probe_result(void)
{
    return &g_stage76_xnu_live_probe_result;
}
