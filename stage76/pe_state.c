#include "stage76.h"

struct pe_platform_state PE_state_stage76;

static uint32_t reg_value(const void *dt, uint32_t dt_len, const void *node, const char *name, uint32_t word_index, uint32_t fallback)
{
    uint32_t len;
    const uint32_t *v = (const uint32_t *)apple_dt_get_prop(dt, dt_len, node, name, &len);
    if (!v || len < (word_index + 1u) * sizeof(uint32_t)) {
        return fallback;
    }
    return v[word_index];
}

void pe_state_init_from_boot_args(struct boot_args *args)
{
    memset(&PE_state_stage76, 0, sizeof(PE_state_stage76));

    PE_state_stage76.bootArgs = args;
    PE_state_stage76.deviceTreeHead = args ? args->deviceTreeP : 0;
    PE_state_stage76.deviceTreeLength = args ? args->deviceTreeLength : 0;
    PE_state_stage76.machineType = args ? args->machineType : 0;
    PE_state_stage76.vectorBase = (uint32_t)(uintptr_t)stage76_vectors;

    if (!args || !args->deviceTreeP || !args->deviceTreeLength) {
        return;
    }

    const void *dt = args->deviceTreeP;
    uint32_t dt_len = args->deviceTreeLength;

    const void *memory = apple_dt_find_child(dt, dt_len, dt, "memory");
    const void *cpus = apple_dt_find_child(dt, dt_len, dt, "cpus");
    const void *gic = apple_dt_find_child(dt, dt_len, dt, "interrupt-controller");
    const void *timer = apple_dt_find_child(dt, dt_len, dt, "timer");

    if (memory) {
        PE_state_stage76.memoryBase = reg_value(dt, dt_len, memory, "reg", 0, 0);
        PE_state_stage76.memorySize = reg_value(dt, dt_len, memory, "reg", 1, 0);
    }
    if (cpus) {
        PE_state_stage76.cpuCount = apple_dt_node_child_count(dt, dt_len, cpus);
    }
    if (gic) {
        PE_state_stage76.gicDistributorBase = reg_value(dt, dt_len, gic, "reg", 0, 0);
        PE_state_stage76.gicCpuBase = reg_value(dt, dt_len, gic, "reg", 2, 0);
    }
    if (timer) {
        PE_state_stage76.timerBase = reg_value(dt, dt_len, timer, "reg", 0, 0);
        PE_state_stage76.timerFrequency = apple_dt_get_u32_prop(dt, dt_len, timer, "frequency", 0);
    }
}

void pe_state_log(void)
{
    xnu_log_puts("PE_state summary begin\n");
    xnu_log_kv32("PE_bootArgs", (uint32_t)(uintptr_t)PE_state_stage76.bootArgs);
    xnu_log_kv32("PE_deviceTreeHead", (uint32_t)(uintptr_t)PE_state_stage76.deviceTreeHead);
    xnu_log_kv32("PE_deviceTreeLength", PE_state_stage76.deviceTreeLength);
    xnu_log_kv32("PE_memoryBase", PE_state_stage76.memoryBase);
    xnu_log_kv32("PE_memorySize", PE_state_stage76.memorySize);
    xnu_log_kv32("PE_cpuCount", PE_state_stage76.cpuCount);
    xnu_log_kv32("PE_gicDistributorBase", PE_state_stage76.gicDistributorBase);
    xnu_log_kv32("PE_gicCpuBase", PE_state_stage76.gicCpuBase);
    xnu_log_kv32("PE_timerBase", PE_state_stage76.timerBase);
    xnu_log_kv32("PE_timerFrequency", PE_state_stage76.timerFrequency);
    xnu_log_kv32("PE_machineType", PE_state_stage76.machineType);
    xnu_log_kv32("PE_vectorBase", PE_state_stage76.vectorBase);
    xnu_log_puts("PE_state summary end\n");
}

int pe_state_validate(void)
{
    int ok = 1;

    ok &= (PE_state_stage76.bootArgs != 0);
    ok &= (PE_state_stage76.deviceTreeHead != 0);
    ok &= (PE_state_stage76.memoryBase == RAM_PHYS_BASE);
    ok &= (PE_state_stage76.memorySize == (RAM_CONSOLE_BASE - RAM_PHYS_BASE));
    ok &= (PE_state_stage76.cpuCount == 4u);
    ok &= (PE_state_stage76.gicDistributorBase == 0xf9000000u);
    ok &= (PE_state_stage76.gicCpuBase == 0xf9002000u);
    ok &= (PE_state_stage76.timerBase == 0xf9020000u);
    ok &= (PE_state_stage76.timerFrequency == 19200000u);
    ok &= (PE_state_stage76.vectorBase != 0);

    if (ok) {
        xnu_log_puts("PE_state validate ok\n");
    } else {
        xnu_log_puts("PE_state validate failed\n");
    }
    return ok;
}
