/* MSM8974 hardware watchdog: the last-resort reset.
 *
 * Why this exists
 * ---------------
 * The software dead-man in stage90_main.c depends on the GIC, the ARM timer, IRQ
 * delivery and the vector table all working, and on IRQs being unmasked. If a hang
 * happens with any of those broken - or with IRQs masked - it cannot fire, and the
 * device stays dark until someone holds the power button. That is exactly the
 * failure the 2026-09-16 run produced.
 *
 * The SoC's own watchdog does not depend on the payload's GIC or timer state. It counts
 * down on its own; when it expires the SoC resets. It is also the mechanism Android itself
 * relies on: a kernel panic on this device ends in a watchdog bite, and that bite is what
 * produces a readable /proc/last_kmsg afterwards. So the path "watchdog bite -> reset ->
 * last_kmsg" is not speculative here; it is the normal crash path of the phone this
 * payload runs on.
 *
 * One caveat the vendor documentation states, and this file should not overstate away.
 * Documentation/devicetree/bindings/arm/msm/msm_watchdog.txt says the bite "is an
 * interrupt in the secure mode, which leads to a reset of the SOC via the secure
 * watchdog". So the reset is mediated by the secure world (TrustZone), not by the
 * non-secure counter alone - it is not literally "no software involvement anywhere". That
 * is fine in practice: the secure world is running (aboot loaded it, and Android's own
 * panic path depends on exactly this), and this device demonstrably resets this way. But
 * it is a dependency, and the accurate claim is "does not depend on the payload's state"
 * rather than "does not depend on any software".
 *
 * Two uses
 * --------
 *   1. ARM: start a hardware countdown at the top of the payload. If nothing has
 *      rebooted by then, the SoC resets itself. This covers every hang, including
 *      ones the software dead-man cannot see.
 *   2. BITE NOW: platform_reboot() calls this after its PS_HOLD write. That makes
 *      the reboot path independent of the PMIC: if PS_HOLD does not take effect -
 *      one of the readings of the 2026-09-16 hang - the watchdog still resets the SoC.
 *
 * Grounding
 * ---------
 * Nothing here is guessed. The base address comes from the cancro device tree -
 *   external/android_kernel_xiaomi_cancro/arch/arm/boot/dts/msm8974.dtsi:
 *     qcom,wdt@f9017000 { compatible = "qcom,msm-watchdog"; reg = <0xf9017000 0x1000>; ... }
 * and the register offsets, the clock rate and the programming order come from that
 * kernel's own driver, arch/arm/mach-msm/msm_watchdog_v2.c:
 *     WDT0_RST 0x04   WDT0_EN 0x08   WDT0_STS 0x0C
 *     WDT0_BARK_TIME 0x10   WDT0_BITE_TIME 0x14
 *     WDT_HZ 32765
 *     timeout = bark_time_ms * WDT_HZ / 1000;
 *     write WDT0_BARK_TIME = timeout;  WDT0_BITE_TIME = timeout + 3*WDT_HZ;
 *     write WDT0_EN = 1;               WDT0_RST = 1;
 * The bite-now sequence is the same one that driver uses to force a bite from its
 * bark handler: set BITE_TIME to 1 tick, then pet.
 *
 * 0xf9017000 is inside the 1 MB section 0xf9000000-0xf90fffff, which both the
 * identity table and the candidate L1 already map as MMIO, so this needs no new
 * mapping and works under either table.
 *
 * Safety
 * ------
 * MMIO only: no storage, and the register state is lost on power cycle. The failure mode of
 * a mis-timed watchdog is a reset to Android, not a brick.
 *
 * The armed timeout is deliberately far longer than a healthy payload run, and that claim
 * was checked rather than asserted. Bounding the payload from its own code, with a
 * pessimistic 500 ns per uncached Strongly-Ordered byte (and every buffer byte touched
 * twice):
 *
 *     explicit delay_us calls (4 sites)                       9 ms
 *     bounded selftest waits, worst case (SGI 20ms, timer 50ms) 70 ms
 *     logging, 275 KB at byte granularity (the largest recorded
 *       Stage log; runtime.c's memcpy/memset are byte loops, so
 *       byte granularity is exact here, not conservative)    138 ms
 *     large buffer zeroing: 16K + 128K + 16K L1/L2 tables,
 *       32K device tree, 64K Mach-O staging arena            262 ms
 *                                                          --------
 *     pessimistic total                                     479 ms
 *
 * against a 30 s bark: a 63x margin. So a healthy run cannot trip it. That matters
 * because a watchdog that fired spuriously would reboot every run and look like a hang -
 * the exact failure this file exists to prevent, reintroduced by its own timeout.
 */

#include "stage90.h"

/*
 * MSM8974 application-processor watchdog. See the header comment for provenance.
 * The device tree node is 0x1000 bytes; the register offsets below are the driver's.
 *
 * WDT0_STS is not a status register despite the name: the driver uses it as the live
 * countdown, `(sts >> 1) & 0xFFFFF` ticks, and its pet path compares that count
 * against the programmed bark time to work out its slack. That makes it the one
 * register that can prove the watchdog is actually armed and running, so this driver
 * samples it rather than only trusting a write/read-back.
 */
#define MSM8974_WDT_BASE       0xf9017000u
#define MSM8974_WDT_SIZE       0x00001000u
#define MSM8974_WDT_REG_RST    0x04u
#define MSM8974_WDT_REG_EN     0x08u
#define MSM8974_WDT_REG_STS    0x0cu
#define MSM8974_WDT_REG_BARK   0x10u
#define MSM8974_WDT_REG_BITE   0x14u

/* The watchdog's own clock, in Hz, per msm_watchdog_v2.c (module param WDT_HZ). */
#define MSM8974_WDT_HZ         32765u

/*
 * The vendor driver's own gap between the bark interrupt and the bite, and the width of the
 * two registers. Both are 20 bits: measured on hardware, and consistent with the driver's own
 * `(sts >> 1) & 0xFFFFF` for the live countdown. A value above 0xfffff truncates silently,
 * which is how a 33s bite became a 1.00s one on the first selftest run.
 */
#define MSM8974_WDT_BITE_EXTRA_TICKS ((uint32_t)STAGE90_HW_WATCHDOG_BITE_GAP_S * MSM8974_WDT_HZ)
#define MSM8974_WDT_MAX_TICKS        STAGE90_HW_WATCHDOG_MAX_TICKS

static struct stage90_hw_watchdog_result g_result;

static inline uint32_t hw_wdt_read(uint32_t off)
{
    return *(volatile uint32_t *)(uintptr_t)(MSM8974_WDT_BASE + off);
}

static inline void hw_wdt_write(uint32_t off, uint32_t value)
{
    *(volatile uint32_t *)(uintptr_t)(MSM8974_WDT_BASE + off) = value;
    __asm__ volatile ("dsb sy" ::: "memory");
}

/* Live countdown, in the encoding the vendor driver uses. */
static uint32_t hw_wdt_countdown(void)
{
    return (hw_wdt_read(MSM8974_WDT_REG_STS) >> 1) & 0xfffffu;
}

static uint32_t hw_wdt_checksum(const struct stage90_hw_watchdog_result *r)
{
    const uint32_t *words = (const uint32_t *)r;
    uint32_t count = (uint32_t)(offsetof(struct stage90_hw_watchdog_result, checksum) / sizeof(uint32_t));
    uint32_t chk = 0u;

    for (uint32_t i = 0u; i < count; i++) {
        chk ^= words[i];
    }
    return chk;
}

void stage90_hw_watchdog_log(const struct stage90_hw_watchdog_result *r)
{
    xnu_log_puts("stage90 hw_watchdog result:\n");
    xnu_log_kv32("hw_watchdog_enabled", r->enabled);
    xnu_log_kv32("hw_watchdog_timeout_s", r->timeout_s);
    xnu_log_kv32("hw_watchdog_base", r->base);
    xnu_log_kv32("hw_watchdog_sts_before", r->sts_before);
    xnu_log_kv32("hw_watchdog_en_before", r->en_before);
    xnu_log_kv32("hw_watchdog_bark_ticks_written", r->bark_ticks);
    xnu_log_kv32("hw_watchdog_bite_ticks_written", r->bite_ticks);
    xnu_log_kv32("hw_watchdog_en_after", r->en_after);
    xnu_log_kv32("hw_watchdog_bark_after", r->bark_after);
    xnu_log_kv32("hw_watchdog_bite_after", r->bite_after);
    xnu_log_kv32("hw_watchdog_countdown_first", r->countdown_first);
    xnu_log_kv32("hw_watchdog_countdown_second", r->countdown_second);
    xnu_log_kv32("hw_watchdog_counter_running", r->counter_running);
    xnu_log_kv32("hw_watchdog_countdown_plausible", r->countdown_plausible);
    xnu_log_kv32("hw_watchdog_bite_truncated", r->bite_truncated);
    xnu_log_kv32("hw_watchdog_max_ticks", r->max_ticks);
    xnu_log_kv32("hw_watchdog_readback_ok", r->readback_ok);
    xnu_log_kv32("hw_watchdog_checksum", r->checksum);
}

/*
 * Start a hardware countdown. `timeout_s` is when the SoC resets itself if nothing
 * else has rebooted it first.
 *
 * The bark/bite split follows the vendor driver exactly: bark at the timeout, bite
 * three seconds later (msm_watchdog_v2.c: `WDT0_BARK_TIME = timeout; WDT0_BITE_TIME =
 * timeout + 3*WDT_HZ`). Deviating from a sequence that is known to work on this
 * hardware is the kind of invention that costs a hardware run, so it is not deviated
 * from.
 *
 * The device tree gives this block two interrupts - `interrupts = <0 3 0>, <0 4 0>`,
 * i.e. SPI 3 and SPI 4, intid 35 and intid 36 (intid = 32 + SPI). Nothing here handles
 * either, which is harmless: the payload's IRQ handler treats an unexpected intid
 * generically and EOIs it, so a bark costs one counted interrupt and no more - and the
 * bite needs no cooperation from the non-secure world at all.
 */
int stage90_hw_watchdog_arm(uint32_t timeout_s)
{
    struct stage90_hw_watchdog_result *r = &g_result;
    uint32_t bark_ticks;
    uint32_t bite_ticks;
    uint32_t count_first;
    uint32_t count_second;

    memset(r, 0, sizeof(*r));
    r->base = MSM8974_WDT_BASE;
    r->timeout_s = timeout_s;

    /* Read before writing anything, so a wrong base shows up as implausible state
     * rather than as our own writes being read back at us. */
    r->sts_before = hw_wdt_read(MSM8974_WDT_REG_STS);
    r->en_before = hw_wdt_read(MSM8974_WDT_REG_EN);

    xnu_log_puts("stage90 hw_watchdog: arming (independent of GIC, timer and IRQ state)\n");
    xnu_log_kv32("hw_watchdog_base", MSM8974_WDT_BASE);
    xnu_log_kv32("hw_watchdog_hz", MSM8974_WDT_HZ);
    xnu_log_kv32("hw_watchdog_sts_before", r->sts_before);
    xnu_log_kv32("hw_watchdog_en_before", r->en_before);
    xnu_log_kv32("hw_watchdog_countdown_before", hw_wdt_countdown());

    /*
     * Refuse rather than truncate. The registers are 20 bits, so a value above 0xfffff is
     * silently cut down in hardware - which is how a 33s bite became 1.00s on the first
     * selftest run, and why the readback below had to learn to tell truncation from a dead
     * register. STAGE90_HW_WATCHDOG_TIMEOUT_S is compile-time checked, so reaching this at
     * run time means a caller passed its own value.
     */
    r->max_ticks = MSM8974_WDT_MAX_TICKS;
    if (timeout_s > ((MSM8974_WDT_MAX_TICKS - MSM8974_WDT_BITE_EXTRA_TICKS) / MSM8974_WDT_HZ)) {
        xnu_log_puts("stage90 hw_watchdog: timeout does not fit the 20-bit registers; refusing to arm\n");
        xnu_log_kv32("hw_watchdog_timeout_s", timeout_s);
        xnu_log_kv32("hw_watchdog_max_ticks", MSM8974_WDT_MAX_TICKS);
        r->readback_ok = 0u;
        return 0;
    }
    bark_ticks = timeout_s * MSM8974_WDT_HZ;
    bite_ticks = bark_ticks + MSM8974_WDT_BITE_EXTRA_TICKS;
    r->bark_ticks = bark_ticks;
    r->bite_ticks = bite_ticks;

    hw_wdt_write(MSM8974_WDT_REG_BARK, bark_ticks);
    hw_wdt_write(MSM8974_WDT_REG_BITE, bite_ticks);
    hw_wdt_write(MSM8974_WDT_REG_EN, 1u);
    hw_wdt_write(MSM8974_WDT_REG_RST, 1u);

    r->en_after = hw_wdt_read(MSM8974_WDT_REG_EN);
    r->bark_after = hw_wdt_read(MSM8974_WDT_REG_BARK);
    r->bite_after = hw_wdt_read(MSM8974_WDT_REG_BITE);

    /*
     * Liveness, not just read-back. A register that reads back what was written only
     * proves the write landed; it does not prove a counter is running. Sampling the
     * countdown twice and requiring it to have moved does prove that, and it is the
     * cheapest way for one hardware run to settle whether this net is real. The spin
     * is deliberately short - the counter is ticking at 32765 Hz, so a few hundred
     * microseconds is already thousands of ticks.
     */
    count_first = hw_wdt_countdown();
    for (volatile uint32_t spin = 0u; spin < 200000u; spin++) {
        __asm__ volatile ("nop" ::: "memory");
    }
    count_second = hw_wdt_countdown();

    r->countdown_first = count_first;
    r->countdown_second = count_second;
    r->counter_running = (count_second != count_first) ? 1u : 0u;

    /*
     * And the count is the *right* count. A counter merely observed to move could be aboot's
     * own arming still running from before the payload started - aboot programs 20s
     * (msm8974.dtsi: qcom,bark-time = <20000>) - which would make the net look present with a
     * much shorter timeout than intended.
     *
     * The register counts UP from zero. Two hardware runs settled that, and reading the
     * vendor driver afterwards confirms it: its pet path computes
     *
     *     count = (__raw_readl(base + WDT0_STS) >> 1) & 0xFFFFF;
     *     slack = (bark_time * WDT_HZ / 1000) - count;
     *
     * i.e. `slack = bark_ticks - count`, which is only a remaining-time figure if count rises
     * toward bark_ticks. The first version of this checked for a *down*-counter near the bark
     * value and so reported count_first = 0 as implausible - a working watchdog again reported
     * as absent, for the third time in this file's short life.
     *
     * So: just after the reset the count should be near zero and rising. That still rejects
     * aboot's arming, which would be near 655300 ticks (20s) rather than near zero.
     */
    r->countdown_plausible = ((count_first < (2u * MSM8974_WDT_HZ)) &&
                              (count_second > count_first)) ? 1u : 0u;

    /*
     * Mask the comparison to the register width. An unmasked compare reports "not armed" for
     * a watchdog that is demonstrably running whenever a value exceeds 20 bits - which is
     * exactly what happened, and it turned a working net into a reported failure.
     */
    r->bite_truncated = (r->bite_after != bite_ticks) ? 1u : 0u;
    r->readback_ok = ((r->en_after & 1u) == 1u &&
                      (r->bark_after & MSM8974_WDT_MAX_TICKS) == (bark_ticks & MSM8974_WDT_MAX_TICKS) &&
                      (r->bite_after & MSM8974_WDT_MAX_TICKS) == (bite_ticks & MSM8974_WDT_MAX_TICKS)) ? 1u : 0u;
    r->enabled = ((r->en_after & 1u) == 1u && r->counter_running != 0u &&
                  r->countdown_plausible != 0u) ? 1u : 0u;

    r->checksum = hw_wdt_checksum(r);
    stage90_hw_watchdog_log(r);

    if (r->enabled) {
        xnu_log_puts("stage90 hw_watchdog: armed and counting; the SoC will reset itself if the payload stops\n");
        return 1;
    }

    xnu_log_puts("stage90 hw_watchdog: NOT confirmed armed; no hardware reset net\n");
    return 0;
}

/*
 * Force an immediate bite: set the bite time to one tick and pet. This is what
 * msm_watchdog_v2.c's bark handler does to kill the machine, so it is the device's
 * own idiom rather than an invention.
 *
 * Returns only if the bite did not happen, which is itself worth logging.
 */
void stage90_hw_watchdog_bite_now(void)
{
    xnu_log_puts("stage90 hw_watchdog: forcing immediate bite (independent of PS_HOLD)\n");
    xnu_log_kv32("hw_watchdog_bite_sts_before", hw_wdt_read(MSM8974_WDT_REG_STS));

    hw_wdt_write(MSM8974_WDT_REG_BITE, 1u);
    hw_wdt_write(MSM8974_WDT_REG_RST, 1u);

    /* Give the counter time to reach 1 and the reset to begin. */
    for (volatile uint32_t spin = 0u; spin < 10000000u; spin++) {
        __asm__ volatile ("nop" ::: "memory");
    }

    xnu_log_puts("stage90 hw_watchdog: bite did not take effect\n");
    xnu_log_kv32("hw_watchdog_bite_sts_after", hw_wdt_read(MSM8974_WDT_REG_STS));
    xnu_log_kv32("hw_watchdog_bite_en_after", hw_wdt_read(MSM8974_WDT_REG_EN));
}

const struct stage90_hw_watchdog_result *stage90_hw_watchdog_result(void)
{
    return &g_result;
}

/*
 * The watchdog's enable bit, read live, for a caller that is about to give up the recovery nets it
 * *can* have and wants the record to say what is left.
 *
 * `stage90_disarm_deadman_timer` calls this immediately before the jump into the entry image. The
 * software dead-man cannot cover that jump - it needs the payload's vector table and GIC, both of
 * which the entry image replaces - so the hardware watchdog is the only net on the far side, and
 * the step that removes the dead-man's interrupt source is exactly the step that must show the
 * other net is still armed. Reading the SoC's own register rather than the cached
 * `stage90_hw_watchdog_result()` is the point: a shadow copy would say what this project wrote,
 * and the register says what the watchdog is.
 */
uint32_t stage90_hw_watchdog_enabled_readback(void)
{
    return hw_wdt_read(MSM8974_WDT_REG_EN);
}
