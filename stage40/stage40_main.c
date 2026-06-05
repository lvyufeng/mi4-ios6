#include "stage40.h"

static struct boot_args g_boot_args;
static uint8_t g_apple_dt[4096] __attribute__((aligned(4)));

static void build_stage40_apple_dt(struct apple_dt_builder *b)
{
    static const uint32_t memory_reg[] = {
        RAM_PHYS_BASE, RAM_CONSOLE_BASE - RAM_PHYS_BASE,
    };
    static const uint32_t gic_reg[] = {
        0xf9000000u, 0x00001000u,
        0xf9002000u, 0x00001000u,
    };
    static const uint32_t timer_reg[] = {
        0xf9020000u, 0x00001000u,
        0xf9021000u, 0x00001000u,
        0xf9022000u, 0x00001000u,
    };
    static const uint32_t ram_console_reg[] = {
        RAM_CONSOLE_BASE, RAM_CONSOLE_SIZE,
    };
    static const uint32_t io_ranges[] = {
        0x00000000u, 0xf9000000u, 0x07000000u,
    };

    apple_dt_begin(b, g_apple_dt, sizeof(g_apple_dt));

    /* root: 4 properties, 6 children */
    apple_dt_node_begin(b, 4, 6);
    apple_dt_prop_str(b, "name", "/");
    apple_dt_prop_str(b, "compatible", "qcom,msm8974-xnu-stage40");
    apple_dt_prop_str(b, "model", "Xiaomi Mi 4 cancro Stage40");
    apple_dt_prop_str(b, "target-type", "cancro");

    /* /chosen */
    apple_dt_node_begin(b, 4, 0);
    apple_dt_prop_str(b, "name", "chosen");
    apple_dt_prop_str(b, "boot-args", "debug=0x144 serial=0x1 mi4ios6.stage=25 msm8974=cancro startup-entry=selftest");
    apple_dt_prop_str(b, "stdout-path", "ram-console");
    apple_dt_prop_u32_array(b, "ram-console-reg", ram_console_reg, ARRAY_SIZE(ram_console_reg));

    /* /memory */
    apple_dt_node_begin(b, 4, 0);
    apple_dt_prop_str(b, "name", "memory");
    apple_dt_prop_str(b, "device_type", "memory");
    apple_dt_prop_u32_array(b, "reg", memory_reg, ARRAY_SIZE(memory_reg));
    apple_dt_prop_u32(b, "reserved-top", RAM_CONSOLE_SIZE);

    /* /cpus with four Krait CPU children. */
    apple_dt_node_begin(b, 3, 4);
    apple_dt_prop_str(b, "name", "cpus");
    apple_dt_prop_u32(b, "#address-cells", 1);
    apple_dt_prop_u32(b, "#size-cells", 0);

    for (uint32_t cpu = 0; cpu < 4; cpu++) {
        char name[] = "cpu@0";
        name[4] = (char)('0' + cpu);
        apple_dt_node_begin(b, 6, 0);
        apple_dt_prop_str(b, "name", name);
        apple_dt_prop_str(b, "device_type", "cpu");
        apple_dt_prop_str(b, "compatible", "qcom,krait");
        apple_dt_prop_u32(b, "reg", cpu);
        apple_dt_prop_u32(b, "clock-frequency", 2265600000u);
        apple_dt_prop_u32(b, "timebase-frequency", 19200000u);
    }

    /* /msm8974-io: Apple-XNU-like container for platform MMIO. */
    apple_dt_node_begin(b, 5, 0);
    apple_dt_prop_str(b, "name", "msm8974-io");
    apple_dt_prop_str(b, "device_type", "soc");
    apple_dt_prop_str(b, "compatible", "qcom,msm8974");
    apple_dt_prop_u32_array(b, "ranges", io_ranges, ARRAY_SIZE(io_ranges));
    apple_dt_prop_u32(b, "chip-revision", 0);

    /* /interrupt-controller: MSM QGIC2 / GICv2. */
    apple_dt_node_begin(b, 5, 0);
    apple_dt_prop_str(b, "name", "interrupt-controller");
    apple_dt_prop_str(b, "compatible", "qcom,msm-qgic2");
    apple_dt_prop_u32_array(b, "reg", gic_reg, ARRAY_SIZE(gic_reg));
    apple_dt_prop_u32(b, "#interrupt-cells", 3);
    apple_dt_prop_u32(b, "interrupt-controller", 1);

    /* /timer: MSM/ARM 19.2 MHz timer facts from Linux DT. */
    apple_dt_node_begin(b, 5, 0);
    apple_dt_prop_str(b, "name", "timer");
    apple_dt_prop_str(b, "compatible", "qcom,msm-timer");
    apple_dt_prop_u32(b, "frequency", 19200000u);
    apple_dt_prop_u32_array(b, "reg", timer_reg, ARRAY_SIZE(timer_reg));
    apple_dt_prop_str(b, "use", "early-timebase");
}

int test_kernel_entry(struct boot_args *args)
{
    log_puts("MI4IOS6_STAGE40 handoff -> test_kernel_entry(boot_args*)\n");

    if (!args) {
        log_puts("MI4IOS6_STAGE40 handoff bad: null args\n");
        return 0;
    }

    log_kv32("boot_args_ptr", (uint32_t)(uintptr_t)args);
    log_kv32("boot_args_rev_ver", ((uint32_t)args->Version << 16) | args->Revision);
    log_kv32("boot_args_physBase", args->physBase);
    log_kv32("boot_args_memSize", args->memSize);
    log_kv32("boot_args_topOfKernelData", args->topOfKernelData);
    log_kv32("boot_args_deviceTreeP", (uint32_t)(uintptr_t)args->deviceTreeP);
    log_kv32("boot_args_deviceTreeLength", args->deviceTreeLength);

    if (args->Revision != BOOT_ARGS_REVISION || args->Version != BOOT_ARGS_VERSION) {
        log_puts("MI4IOS6_STAGE40 handoff bad: rev/version\n");
        return 0;
    }
    if (!args->deviceTreeP || !args->deviceTreeLength) {
        log_puts("MI4IOS6_STAGE40 handoff bad: no dt\n");
        return 0;
    }

    if (!apple_dt_selftest_and_log(args->deviceTreeP, args->deviceTreeLength)) {
        log_puts("MI4IOS6_STAGE40 handoff bad: dt selftest\n");
        return 0;
    }

    log_puts("MI4IOS6_STAGE40 handoff ok: boot_args + fuller apple_dt validated\n");
    return 1;
}

void platform_reboot(void)
{
    volatile uint32_t *restart_reason = (volatile uint32_t *)RESTART_REASON;
    volatile uint32_t *ps_hold = (volatile uint32_t *)MSM8974_PSHOLD;

    *restart_reason = RESTART_NORMAL;
    __asm__ volatile ("dsb sy" ::: "memory");

    log_puts("MI4IOS6_STAGE40 attempting MSM8974 PS_HOLD reset\n");
    *ps_hold = 0;
    __asm__ volatile ("dsb sy" ::: "memory");

    for (;;) {
        __asm__ volatile ("wfe");
    }
}

void stage40_main(void)
{
    struct apple_dt_builder b;
    uint32_t dt_len;

    log_init();
    log_puts("MI4IOS6_STAGE40 v1 entered; C runtime active; ram_console live\n");
    log_kv32("stage40_image_end", (uint32_t)(uintptr_t)__stage40_image_end);

    build_stage40_apple_dt(&b);
    dt_len = apple_dt_finish(&b);
    log_kv32("built_apple_dt_len", dt_len);

    if (!dt_len) {
        log_puts("MI4IOS6_STAGE40 apple_dt build failed\n");
        platform_reboot();
    }

    build_boot_args(&g_boot_args, g_apple_dt, dt_len);
    log_puts("MI4IOS6_STAGE40 boot_args built (rev2); fuller apple_dt attached\n");

    log_puts("MI4IOS6_STAGE40 vector base installed at ");
    log_hex32((uint32_t)(uintptr_t)stage40_vectors);
    log_puts("\n");

    log_puts("MI4IOS6_STAGE40 boot-wrapper handoff -> kernel_entry(boot_args*)\n");
    if (kernel_entry(&g_boot_args)) {
        log_puts("MI4IOS6_STAGE40 kernel_entry returned success\n");
    } else {
        log_puts("MI4IOS6_STAGE40 kernel_entry returned failure\n");
        platform_reboot();
    }

    platform_reboot();
}
