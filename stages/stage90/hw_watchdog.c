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
 * The SoC's own watchdog does not depend on any of it. It is a hardware counter; when
 * it expires the SoC resets, whatever the CPU is doing. It is also the mechanism
 * Android itself relies on: a kernel panic on this device ends in a watchdog bite,
 * and that bite is what produces a readable /proc/last_kmsg afterwards. So the path
 * "watchdog bite -> reset -> last_kmsg" is not speculative here; it is the normal
 * crash path of the phone this payload runs on.
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
 * MMIO only: no storage, and the register state is lost on power cycle. The armed
 * timeout is deliberately far longer than a healthy payload run (~1s), so a good run
 * never sees it. And the failure mode of a mis-timed watchdog is a reset to Android,
 * not a brick.
 */

#include "stage90.h"

/*
 * MSM8974 application-processor watchdog. See the header comment for provenance.
 * The device tree node is 0x1000 bytes; the register offsets below are the driver's.
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
    xnu_log_kv32("hw_watchdog_sts_after", r->sts_after);
    xnu_log_kv32("hw_watchdog_readback_ok", r->readback_ok);
    xnu_log_kv32("hw_watchdog_checksum", r->checksum);
}

/*
 * Start a hardware countdown. `timeout_s` is when the SoC resets itself if nothing
 * else has rebooted it first. Bark and bite are set to the same value on purpose:
 * the bark is an interrupt, and we would rather there be no interrupt at all before
 * the reset than an unexpected one arriving in the middle of the payload.
 */
int stage90_hw_watchdog_arm(uint32_t timeout_s)
{
    struct stage90_hw_watchdog_result *r = &g_result;
    uint32_t ticks;
    uint32_t sts_before;
    uint32_t en_before;

    memset(r, 0, sizeof(*r));
    r->base = MSM8974_WDT_BASE;
    r->timeout_s = timeout_s;

    /* Read before writing anything, so a wrong base shows up as implausible state
     * rather than as our own writes being read back at us. */
    sts_before = hw_wdt_read(MSM8974_WDT_REG_STS);
    en_before = hw_wdt_read(MSM8974_WDT_REG_EN);
    r->sts_before = sts_before;
    r->en_before = en_before;

    xnu_log_puts("stage90 hw_watchdog: arming (independent of GIC, timer and IRQ state)\n");
    xnu_log_kv32("hw_watchdog_base", MSM8974_WDT_BASE);
    xnu_log_kv32("hw_watchdog_hz", MSM8974_WDT_HZ);
    xnu_log_kv32("hw_watchdog_sts_before", sts_before);
    xnu_log_kv32("hw_watchdog_en_before", en_before);

    /* Guard against the tick value wrapping the 32-bit register. */
    if (timeout_s > (0xffffffffu / MSM8974_WDT_HZ)) {
        xnu_log_puts("stage90 hw_watchdog: timeout too large to encode; not arming\n");
        r->readback_ok = 0u;
        return 0;
    }
    ticks = timeout_s * MSM8974_WDT_HZ;
    r->bark_ticks = ticks;
    r->bite_ticks = ticks;

    hw_wdt_write(MSM8974_WDT_REG_BARK, ticks);
    hw_wdt_write(MSM8974_WDT_REG_BITE, ticks);
    hw_wdt_write(MSM8974_WDT_REG_EN, 1u);
    hw_wdt_write(MSM8974_WDT_REG_RST, 1u);

    r->en_after = hw_wdt_read(MSM8974_WDT_REG_EN);
    r->bark_after = hw_wdt_read(MSM8974_WDT_REG_BARK);
    r->bite_after = hw_wdt_read(MSM8974_WDT_REG_BITE);
    r->sts_after = hw_wdt_read(MSM8974_WDT_REG_STS);

    /*
     * Readback is the only check available without a spec: if the registers are not
     * where the device tree says they are, what we wrote will not come back. A
     * mismatch is logged as a plain fact - it does not abort the payload, because
     * failing to arm a safety net is not a reason to stop doing the work.
     */
    r->readback_ok = ((r->en_after & 1u) == 1u &&
                      r->bark_after == ticks &&
                      r->bite_after == ticks) ? 1u : 0u;
    r->enabled = r->readback_ok;

    r->checksum = hw_wdt_checksum(r);
    stage90_hw_watchdog_log(r);

    if (r->readback_ok) {
        xnu_log_puts("stage90 hw_watchdog: armed; the SoC will reset itself if the payload stops\n");
        return 1;
    }

    xnu_log_puts("stage90 hw_watchdog: readback mismatch - NOT armed; no hardware reset net\n");
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
