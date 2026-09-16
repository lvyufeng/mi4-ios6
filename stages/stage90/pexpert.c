#include "stage90.h"

static int require_node(const struct boot_args *args, const char *name, const void **out)
{
    const void *node = apple_dt_find_child(args->deviceTreeP, args->deviceTreeLength, args->deviceTreeP, name);
    if (!node) {
        xnu_log_puts("pexpert missing /");
        log_puts(name);
        log_puts("\n");
        return 0;
    }

    xnu_log_puts("pexpert found /");
    log_puts(name);
    log_puts("\n");
    *out = node;
    return 1;
}

static void log_reg_pair(const char *prefix, const uint32_t *reg, uint32_t count)
{
    if (count >= 2) {
        xnu_log_puts(prefix);
        log_puts(" base=");
        log_hex32(reg[0]);
        log_puts(" size=");
        log_hex32(reg[1]);
        log_puts("\n");
    }
}

int pexpert_discover_and_log(struct boot_args *args)
{
    const void *chosen;
    const void *memory;
    const void *cpus;
    const void *gic;
    const void *timer;
    const void *iokit_platform;
    const void *platform_driver;
    uint32_t len;

    xnu_log_puts("pexpert discovery begin\n");
    xnu_log_kv32("dt_root_props", apple_dt_node_prop_count(args->deviceTreeP, args->deviceTreeLength, args->deviceTreeP));
    xnu_log_kv32("dt_root_children", apple_dt_node_child_count(args->deviceTreeP, args->deviceTreeLength, args->deviceTreeP));

    if (!require_node(args, "chosen", &chosen) ||
        !require_node(args, "memory", &memory) ||
        !require_node(args, "cpus", &cpus) ||
        !require_node(args, "interrupt-controller", &gic) ||
        !require_node(args, "timer", &timer) ||
        !require_node(args, "iokit-platform-scaffold", &iokit_platform) ||
        !require_node(args, "msm8974-platform-driver", &platform_driver)) {
        xnu_log_puts("pexpert discovery failed\n");
        return 0;
    }

    const char *boot_args = (const char *)apple_dt_get_prop(args->deviceTreeP, args->deviceTreeLength, chosen, "boot-args", &len);
    if (boot_args) {
        xnu_log_puts("chosen boot-args: ");
        log_puts(boot_args);
        log_puts("\n");
    }

    const uint32_t *mem_reg = (const uint32_t *)apple_dt_get_prop(args->deviceTreeP, args->deviceTreeLength, memory, "reg", &len);
    if (mem_reg && len >= 8) {
        log_reg_pair("memory.reg", mem_reg, len / 4u);
        if (mem_reg[0] == RAM_PHYS_BASE && mem_reg[1] == args->memSize) {
            xnu_log_puts("memory matches boot_args\n");
        }
    }

    xnu_log_kv32("cpu_count", apple_dt_node_child_count(args->deviceTreeP, args->deviceTreeLength, cpus));

    const uint32_t *gic_reg = (const uint32_t *)apple_dt_get_prop(args->deviceTreeP, args->deviceTreeLength, gic, "reg", &len);
    if (gic_reg && len >= 16) {
        log_reg_pair("gicd.reg", &gic_reg[0], 2);
        log_reg_pair("gicc.reg", &gic_reg[2], 2);
    }
    xnu_log_kv32("gic_interrupt_cells", apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, gic, "#interrupt-cells", 0));

    const uint32_t timer_freq = apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, timer, "frequency", 0);
    xnu_log_kv32("timer_frequency", timer_freq);
    const uint32_t *timer_reg = (const uint32_t *)apple_dt_get_prop(args->deviceTreeP, args->deviceTreeLength, timer, "reg", &len);
    if (timer_reg && len >= 8) {
        log_reg_pair("timer.reg0", &timer_reg[0], 2);
    }

    xnu_log_kv32("iokit_platform_props", apple_dt_node_prop_count(args->deviceTreeP, args->deviceTreeLength, iokit_platform));
    xnu_log_kv32("platform_driver_props", apple_dt_node_prop_count(args->deviceTreeP, args->deviceTreeLength, platform_driver));
    xnu_log_kv32("platform_driver_cpu_count",
                 apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, platform_driver, "cpu-count", 0));
    xnu_log_kv32("platform_driver_timebase_frequency",
                 apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, platform_driver, "timebase-frequency", 0));
    const uint32_t *platform_driver_reg = (const uint32_t *)apple_dt_get_prop(args->deviceTreeP, args->deviceTreeLength,
                                                                              platform_driver, "reg", &len);
    if (platform_driver_reg && len >= 16) {
        log_reg_pair("platform-driver.gic", &platform_driver_reg[0], 2);
        log_reg_pair("platform-driver.timer", &platform_driver_reg[2], 2);
    }

    xnu_log_puts("pexpert discovery ok\n");
    return 1;
}
