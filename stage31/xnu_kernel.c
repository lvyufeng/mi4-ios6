#include "stage31.h"

int kernel_entry(struct boot_args *args)
{
    xnu_log_puts("kernel_entry(struct boot_args*) entered\n");

    if (!args) {
        xnu_log_puts("kernel_entry bad: null args\n");
        return 0;
    }

    xnu_log_kv32("boot_args_ptr", (uint32_t)(uintptr_t)args);
    xnu_log_kv32("boot_args_rev_ver", ((uint32_t)args->Version << 16) | args->Revision);
    xnu_log_kv32("physBase", args->physBase);
    xnu_log_kv32("memSize", args->memSize);
    xnu_log_kv32("topOfKernelData", args->topOfKernelData);
    xnu_log_kv32("deviceTreeP", (uint32_t)(uintptr_t)args->deviceTreeP);
    xnu_log_kv32("deviceTreeLength", args->deviceTreeLength);

    if (args->Revision != BOOT_ARGS_REVISION || args->Version != BOOT_ARGS_VERSION) {
        xnu_log_puts("kernel_entry bad: boot_args revision/version\n");
        return 0;
    }
    if (!args->deviceTreeP || !args->deviceTreeLength) {
        xnu_log_puts("kernel_entry bad: missing device tree\n");
        return 0;
    }

    if (!apple_dt_selftest_and_log(args->deviceTreeP, args->deviceTreeLength)) {
        xnu_log_puts("kernel_entry bad: apple_dt selftest\n");
        return 0;
    }

    if (!pexpert_discover_and_log(args)) {
        xnu_log_puts("kernel_entry bad: pexpert discovery\n");
        return 0;
    }

    pe_state_init_from_boot_args(args);
    pe_state_log();
    if (!pe_state_validate()) {
        xnu_log_puts("kernel_entry bad: PE_state validation\n");
        return 0;
    }

    gic_readonly_snapshot(PE_state_stage31.gicDistributorBase, PE_state_stage31.gicCpuBase);
    gic_log_snapshot();
    if (!gic_validate_snapshot()) {
        xnu_log_puts("kernel_entry bad: GIC validation\n");
        return 0;
    }

    ml_init_timebase();
    const uint64_t t0 = ml_get_timebase();
    delay_us(2000);
    const uint64_t t1 = ml_get_timebase();
    xnu_log_kv32("ml_timebase_freq_check", ml_get_timebase_frequency());
    xnu_log_kv64("ml_timebase_delta_ticks", t1 - t0);
    xnu_log_kv32("ml_timebase_delta_us", timebase_elapsed_us(t0, t1));

    if (ml_get_timebase_frequency() == 19200000u && timebase_elapsed_us(t0, t1) >= 1900u) {
        xnu_log_puts("ml timebase ok\n");
    } else {
        xnu_log_puts("ml timebase suspicious\n");
        return 0;
    }

    if (!mmu_identity_selftest()) {
        xnu_log_puts("kernel_entry bad: MMU identity selftest\n");
        return 0;
    }

    if (!mmu_high_alias_selftest()) {
        xnu_log_puts("kernel_entry bad: MMU high alias selftest\n");
        return 0;
    }

    if (!mmu_high_call_selftest()) {
        xnu_log_puts("kernel_entry bad: MMU high call selftest\n");
        return 0;
    }

    if (!mmu_high_bootstrap_selftest()) {
        xnu_log_puts("kernel_entry bad: MMU high bootstrap selftest\n");
        return 0;
    }

    if (!gic_sgi_selftest()) {
        xnu_log_puts("kernel_entry bad: SGI IRQ selftest\n");
        return 0;
    }

    if (!gic_timer_selftest()) {
        xnu_log_puts("kernel_entry bad: timer IRQ selftest\n");
        return 0;
    }

    xnu_log_puts("kernel_entry ok\n");
    return 1;
}
