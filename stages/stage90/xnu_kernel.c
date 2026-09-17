#include "stage90.h"

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

    gic_readonly_snapshot(PE_state_stage90.gicDistributorBase, PE_state_stage90.gicCpuBase);
    gic_log_snapshot();
    if (!gic_validate_snapshot()) {
        xnu_log_puts("kernel_entry bad: GIC validation\n");
        return 0;
    }

    /*
     * Arm the recovery net here: the GIC bases are known-good from the snapshot
     * above, and everything from this point on - the loader preflight, the whole
     * arm_init ladder (full pmap, candidate L1, the high-VA handler windows) and
     * the handoff jump - is the unproven part of the payload, so it is exactly
     * what needs covering. The code above this line has a 90-stage track record;
     * leaving its IRQ behaviour untouched keeps that baseline intact.
     *
     * On the happy path this changes nothing: every exit from the payload ends in
     * platform_reboot() well before the dead-man's budget expires.
     */
    (void)stage90_arm_deadman_reset();

#if STAGE90_EXCLUSIVE_PROBE
    /*
     * Phase 1 baseline: measure LDREX/STREX under the current Strongly-Ordered
     * mapping, before any attribute map change. Changes nothing itself.
     */
    (void)stage90_exclusive_probe_run();
#endif

#if STAGE90_XNU_MSM8974_SHIM
    /*
     * Phase 3: exercise the Stage-owned MSM8974 platform shim - the replacement for XNU's
     * ARM timer/interrupt bring-up. It registers its tbd_ops through a mirror of
     * ml_init_timebase's guard and verifies the registration took, then checks the hardware
     * facts (the EOI pairing, the measured CNTP interrupt number, the validated CNTFRQ).
     *
     * It leaves the timer disarmed: the payload's own timer code owns arming from here.
     */
    if (stage90_xnu_msm8974_shim_run() != 0) {
        /*
         * Non-fatal, and said out loud. This is the next stage's platform layer, not a
         * precondition for the current one, so it must not fail the run - but a reader
         * comparing "the run passed" with "the shim passed" would otherwise have to
         * notice that msm8974_shim_status in the log disagrees with kernel_entry ok.
         */
        xnu_log_puts("kernel_entry: msm8974 platform shim did NOT satisfy its checks - "
                     "see msm8974_shim_failures above; not fatal to this run\n");
    }
#endif

#if STAGE90_XNU_BOOT_ARGS
    /*
     * Phase 2: build and validate a boot_args that conforms to the contract in XNU's
     * own entry code. It is a second object - the ladder's identity-based one is
     * untouched - and nothing consumes it yet, so this only proves the invariants hold
     * on the real image_end the linker produced, which is the one number in the
     * contract that cannot be checked from the host beforehand.
     */
    if (stage90_xnu_boot_args_prepare(args->deviceTreeP, args->deviceTreeLength) != 0) {
        /* Same reasoning as the shim above: a Phase 2 probe, recorded and not fatal. */
        xnu_log_puts("kernel_entry: conforming boot_args did NOT satisfy its contract - "
                     "see xnu_ba_failures above; not fatal to this run\n");
    }
#endif

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

#if STAGE90_EXCLUSIVE_PROBE
    /*
     * Phase 1, phase 2: the same four exclusive tests, re-run now that the MMU is on and a
     * page-table descriptor actually applies to the probe word.
     *
     * Phase 1 above runs before enable_identity_mmu(), i.e. with SCTLR.M clear. ARMv7 treats
     * every access as Strongly-ordered while the MMU is off, so no descriptor is consulted and
     * the attribute mode cannot change the outcome - which is exactly why the SO_ONLY and
     * NORMAL_NC measurements were identical. That comparison was between two builds running
     * the same meaningless configuration, not between two memory types.
     *
     * This is the measurement the attribute-map work was for. It is deliberately a probe and
     * not a check: a failure here is a finding about the platform, not a boot failure.
     */
    (void)stage90_exclusive_probe_run_mmu_on();

    /*
     * Phase 3: the same tests on a cacheable mapping with the D-cache on. Normal-Non-cacheable
     * did not make the monitor track, so the remaining explanation is that Krait only monitors
     * cacheable accesses. Phase 3 adds one cacheable 1 MB section, enables SCTLR.C, measures,
     * and restores both before returning.
     */
    (void)stage90_exclusive_probe_run_dcache();
#endif

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

    if (!mmu_stage90_ttbr0_roundtrip_selftest()) {
        xnu_log_puts("kernel_entry bad: Stage84 TTBR0 roundtrip selftest\n");
        return 0;
    }

    if (!stage90_loader_preflight_run(args)) {
        xnu_log_puts("kernel_entry bad: Mach-O/XNU loader preflight\n");
        return 0;
    }

    xnu_log_puts("kernel_entry ok\n");
    return 1;
}
