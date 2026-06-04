#include "stage18.h"

#define L1_SECTION_COUNT       4096u
#define L1_SECTION_SIZE        0x00100000u
#define L1_SECTION_MASK        0xfff00000u

/* ARMv7 short-descriptor section: domain 0, AP full access, strongly ordered/shareable, executable. */
#define L1_DESC_SECTION_SO     0x00010c02u

#define SECTION_INDEX(addr)    (((uint32_t)(addr)) >> 20)
#define STAGE18_HIGH_ALIAS_BASE        0xc0000000u
#define STAGE18_RAM_CONSOLE_ALIAS_BASE 0xc0100000u
#define STAGE18_GIC_ALIAS_BASE         0xc0200000u

#define STAGE18_BOOTSTRAP_STATUS_OK    0x18000001u
#define STAGE18_BOOTSTRAP_STATUS_BASE  0x18000000u
#define STAGE18_FAIL_REV_VER           0x00000001u
#define STAGE18_FAIL_MACHINE           0x00000002u
#define STAGE18_FAIL_DT                0x00000004u
#define STAGE18_FAIL_MEMORY            0x00000008u
#define STAGE18_FAIL_CPU               0x00000010u
#define STAGE18_FAIL_GIC               0x00000020u
#define STAGE18_FAIL_TIMER             0x00000040u
#define STAGE18_FAIL_VECTOR            0x00000080u
#define STAGE18_FAIL_TIMEBASE_INIT     0x00000100u
#define STAGE18_FAIL_GIC_SUMMARY       0x00000200u
#define STAGE18_FAIL_INIT_STEPS        0x00000400u
#define STAGE18_FAIL_DT_SUMMARY        0x00000800u

#define STAGE18_INIT_STEP_VALIDATE     0x00000001u
#define STAGE18_INIT_STEP_TIMEBASE     0x00000002u
#define STAGE18_INIT_STEP_GIC_SUMMARY  0x00000004u
#define STAGE18_INIT_STEP_COMPLETE     0x00000008u

#define STAGE18_INIT_REQUIRED_STEPS    (STAGE18_INIT_STEP_VALIDATE | \
                                        STAGE18_INIT_STEP_TIMEBASE | \
                                        STAGE18_INIT_STEP_GIC_SUMMARY | \
                                        STAGE18_INIT_STEP_COMPLETE)

#define STAGE18_ROOT_STEP_ENTER        0x00000001u
#define STAGE18_ROOT_STEP_INIT         0x00000002u
#define STAGE18_ROOT_STEP_RESULT       0x00000004u
#define STAGE18_ROOT_STEP_RETURN       0x00000008u
#define STAGE18_ROOT_STEP_DT_SUMMARY   0x00000010u
#define STAGE18_ROOT_REQUIRED_STEPS    (STAGE18_ROOT_STEP_ENTER | \
                                        STAGE18_ROOT_STEP_INIT | \
                                        STAGE18_ROOT_STEP_RESULT | \
                                        STAGE18_ROOT_STEP_RETURN | \
                                        STAGE18_ROOT_STEP_DT_SUMMARY)

static uint32_t stage18_l1_table[L1_SECTION_COUNT] __attribute__((aligned(16384)));
static volatile uint32_t stage18_mmu_probe_word;
static volatile uint32_t stage18_mmu_alias_probe;

struct stage18_alias_state {
    uint32_t magic;
    uint32_t input;
    uint32_t result;
    uint32_t checksum;
};

static struct stage18_alias_state stage18_alias_state_block;

struct stage18_bootstrap_state {
    uint32_t magic;
    uint32_t boot_args_rev_ver;
    uint32_t machine_type;
    uint32_t device_tree_length;
    uint32_t pe_memory_base;
    uint32_t pe_memory_size;
    uint32_t pe_cpu_count;
    uint32_t pe_gic_dist_base;
    uint32_t pe_gic_cpu_base;
    uint32_t pe_timer_base;
    uint32_t pe_timer_frequency;
    uint32_t pe_vector_base;
    uint32_t boot_flags;
    uint32_t validation_mask;
    uint32_t root_steps;
    uint32_t root_status;
    uint32_t init_steps;
    uint32_t init_status;
    uint32_t init_timebase_freq;
    uint32_t init_timebase_delta_us;
    uint32_t init_gic_irq_count;
    uint32_t init_gic_cpu_count;
    uint32_t init_gic_dist_ctlr;
    uint32_t init_gic_cpu_ctlr;
    uint32_t root_boot_args_virt;
    uint32_t root_dt_virt;
    uint32_t root_dt_root_children;
    uint32_t root_dt_memory_base;
    uint32_t root_dt_memory_size;
    uint32_t root_dt_timer_frequency;
    uint32_t root_dt_summary_status;
    uint32_t status;
    uint32_t checksum;
};

static struct stage18_bootstrap_state stage18_bootstrap_state_block;

static uint32_t stage18_high_alias_target(uint32_t input, volatile struct stage18_alias_state *state)
    __attribute__((noinline));

static uint32_t stage18_high_alias_target(uint32_t input, volatile struct stage18_alias_state *state)
{
    uint32_t result = (input ^ 0x12c0ffeeu) + 0x1234u;

    state->magic = 0x12001200u;
    state->input = input;
    state->result = result;
    state->checksum = state->magic ^ state->input ^ state->result;

    return result;
}

static uint32_t stage18_kernel_root(struct boot_args *args,
                                    volatile struct pe_platform_state *pe,
                                    volatile struct stage18_bootstrap_state *state)
    __attribute__((noinline));

static uint32_t stage18_kernel_root(struct boot_args *args,
                                    volatile struct pe_platform_state *pe,
                                    volatile struct stage18_bootstrap_state *state)
{
    volatile uint32_t *gicd_ctlr = (volatile uint32_t *)(uintptr_t)pe->gicDistributorBase;
    volatile uint32_t *gicd_typer = (volatile uint32_t *)(uintptr_t)(pe->gicDistributorBase + 0x004u);
    volatile uint32_t *gicc_ctlr = (volatile uint32_t *)(uintptr_t)pe->gicCpuBase;
    const void *dt_high = (const void *)(uintptr_t)(STAGE18_HIGH_ALIAS_BASE + (uint32_t)(uintptr_t)args->deviceTreeP);
    const void *memory_node;
    const void *timer_node;
    const uint32_t *memory_reg;
    uint32_t memory_reg_len = 0;
    uint32_t rev_ver = ((uint32_t)args->Version << 16) | args->Revision;
    uint32_t failures = 0;
    uint32_t root_steps = 0;
    uint32_t steps = 0;
    uint32_t typer;
    uint64_t t0;
    uint64_t t1;

    xnu_log_puts("high virtual kernel_root entered\n");
    root_steps |= STAGE18_ROOT_STEP_ENTER;
    xnu_log_kv32("high_root_steps", root_steps);
    xnu_log_puts("high root owns init sequence\n");
    xnu_log_puts("high init sequence begin\n");

    state->magic = 0x18001800u;
    state->boot_args_rev_ver = rev_ver;
    state->machine_type = args->machineType;
    state->device_tree_length = args->deviceTreeLength;
    state->pe_memory_base = pe->memoryBase;
    state->pe_memory_size = pe->memorySize;
    state->pe_cpu_count = pe->cpuCount;
    state->pe_gic_dist_base = pe->gicDistributorBase;
    state->pe_gic_cpu_base = pe->gicCpuBase;
    state->pe_timer_base = pe->timerBase;
    state->pe_timer_frequency = pe->timerFrequency;
    state->pe_vector_base = pe->vectorBase;
    state->boot_flags = args->bootFlags;
    state->validation_mask = 0;
    state->root_steps = root_steps;
    state->root_status = STAGE18_BOOTSTRAP_STATUS_BASE;
    state->init_steps = 0;
    state->init_status = STAGE18_BOOTSTRAP_STATUS_BASE;
    state->init_timebase_freq = 0;
    state->init_timebase_delta_us = 0;
    state->init_gic_irq_count = 0;
    state->init_gic_cpu_count = 0;
    state->init_gic_dist_ctlr = 0;
    state->init_gic_cpu_ctlr = 0;
    state->root_boot_args_virt = (uint32_t)(uintptr_t)args;
    state->root_dt_virt = (uint32_t)(uintptr_t)dt_high;
    state->root_dt_root_children = 0;
    state->root_dt_memory_base = 0;
    state->root_dt_memory_size = 0;
    state->root_dt_timer_frequency = 0;
    state->root_dt_summary_status = STAGE18_BOOTSTRAP_STATUS_BASE;
    state->status = STAGE18_BOOTSTRAP_STATUS_BASE;
    state->checksum = 0;

    xnu_log_puts("high init step validate begin\n");
    if (rev_ver != 0x00020002u) failures |= STAGE18_FAIL_REV_VER;
    if (args->machineType != MACHINE_TYPE_MSM8974) failures |= STAGE18_FAIL_MACHINE;
    if (!args->deviceTreeP || args->deviceTreeLength < 0x200u) failures |= STAGE18_FAIL_DT;
    if (pe->memoryBase != RAM_PHYS_BASE || pe->memorySize != (RAM_CONSOLE_BASE - RAM_PHYS_BASE)) failures |= STAGE18_FAIL_MEMORY;
    if (pe->cpuCount != 4u) failures |= STAGE18_FAIL_CPU;
    if (pe->gicDistributorBase != 0xf9000000u || pe->gicCpuBase != 0xf9002000u) failures |= STAGE18_FAIL_GIC;
    if (pe->timerBase != 0xf9020000u || pe->timerFrequency != 19200000u) failures |= STAGE18_FAIL_TIMER;
    if (pe->vectorBase != (uint32_t)(uintptr_t)stage18_vectors) failures |= STAGE18_FAIL_VECTOR;
    if ((failures & 0x000000ffu) == 0u) {
        steps |= STAGE18_INIT_STEP_VALIDATE;
        xnu_log_puts("high init step validate ok\n");
    } else {
        xnu_log_puts("high init step validate failed\n");
    }

    xnu_log_puts("high init step timebase begin\n");
    ml_init_timebase();
    t0 = ml_get_timebase();
    delay_us(1000);
    t1 = ml_get_timebase();
    state->init_timebase_freq = ml_get_timebase_frequency();
    state->init_timebase_delta_us = timebase_elapsed_us(t0, t1);
    if (state->init_timebase_freq == 19200000u && state->init_timebase_delta_us >= 900u) {
        steps |= STAGE18_INIT_STEP_TIMEBASE;
        xnu_log_puts("high init step timebase ok\n");
    } else {
        failures |= STAGE18_FAIL_TIMEBASE_INIT;
        xnu_log_puts("high init step timebase failed\n");
    }

    xnu_log_puts("high init step gic summary begin\n");
    typer = *gicd_typer;
    state->init_gic_dist_ctlr = *gicd_ctlr;
    state->init_gic_cpu_ctlr = *gicc_ctlr;
    state->init_gic_irq_count = ((typer & 0x1fu) + 1u) * 32u;
    state->init_gic_cpu_count = ((typer >> 5) & 0x7u) + 1u;
    if ((state->init_gic_dist_ctlr & 1u) != 0u &&
        (state->init_gic_cpu_ctlr & 1u) != 0u &&
        state->init_gic_irq_count >= 288u &&
        state->init_gic_cpu_count == 4u) {
        steps |= STAGE18_INIT_STEP_GIC_SUMMARY;
        xnu_log_puts("high init step gic summary ok\n");
    } else {
        failures |= STAGE18_FAIL_GIC_SUMMARY;
        xnu_log_puts("high init step gic summary failed\n");
    }

    xnu_log_puts("high root dt summary begin\n");
    state->root_dt_root_children = apple_dt_node_child_count(dt_high, args->deviceTreeLength, dt_high);
    memory_node = apple_dt_find_child(dt_high, args->deviceTreeLength, dt_high, "memory");
    timer_node = apple_dt_find_child(dt_high, args->deviceTreeLength, dt_high, "timer");
    memory_reg = (const uint32_t *)apple_dt_get_prop(dt_high, args->deviceTreeLength, memory_node, "reg", &memory_reg_len);
    if (memory_reg && memory_reg_len >= 8u) {
        state->root_dt_memory_base = memory_reg[0];
        state->root_dt_memory_size = memory_reg[1];
    }
    state->root_dt_timer_frequency = apple_dt_get_u32_prop(dt_high, args->deviceTreeLength, timer_node, "frequency", 0);
    if ((uint32_t)(uintptr_t)args >= STAGE18_HIGH_ALIAS_BASE &&
        state->root_dt_virt >= STAGE18_HIGH_ALIAS_BASE &&
        state->root_dt_root_children == 6u &&
        state->root_dt_memory_base == RAM_PHYS_BASE &&
        state->root_dt_memory_size == (RAM_CONSOLE_BASE - RAM_PHYS_BASE) &&
        state->root_dt_timer_frequency == 19200000u) {
        root_steps |= STAGE18_ROOT_STEP_DT_SUMMARY;
        state->root_dt_summary_status = STAGE18_BOOTSTRAP_STATUS_OK;
        xnu_log_puts("high root dt summary ok\n");
    } else {
        failures |= STAGE18_FAIL_DT_SUMMARY;
        state->root_dt_summary_status = STAGE18_BOOTSTRAP_STATUS_BASE | STAGE18_FAIL_DT_SUMMARY;
        xnu_log_puts("high root dt summary failed\n");
    }

    if ((steps & (STAGE18_INIT_STEP_VALIDATE | STAGE18_INIT_STEP_TIMEBASE | STAGE18_INIT_STEP_GIC_SUMMARY)) ==
        (STAGE18_INIT_STEP_VALIDATE | STAGE18_INIT_STEP_TIMEBASE | STAGE18_INIT_STEP_GIC_SUMMARY)) {
        steps |= STAGE18_INIT_STEP_COMPLETE;
        root_steps |= STAGE18_ROOT_STEP_INIT;
        xnu_log_puts("high init sequence complete\n");
    } else {
        failures |= STAGE18_FAIL_INIT_STEPS;
        xnu_log_puts("high init sequence incomplete\n");
    }

    if (!failures) {
        root_steps |= STAGE18_ROOT_STEP_RESULT;
    }
    root_steps |= STAGE18_ROOT_STEP_RETURN;

    state->validation_mask = failures;
    state->root_steps = root_steps;
    state->root_status = (root_steps == STAGE18_ROOT_REQUIRED_STEPS && !failures) ?
        STAGE18_BOOTSTRAP_STATUS_OK : (STAGE18_BOOTSTRAP_STATUS_BASE | failures | STAGE18_FAIL_INIT_STEPS);
    state->init_steps = steps;
    state->init_status = failures ? (STAGE18_BOOTSTRAP_STATUS_BASE | failures) : STAGE18_BOOTSTRAP_STATUS_OK;
    state->status = state->root_status;
    state->checksum = state->magic ^ state->boot_args_rev_ver ^ state->machine_type ^
        state->device_tree_length ^ state->pe_memory_base ^ state->pe_memory_size ^
        state->pe_cpu_count ^ state->pe_gic_dist_base ^ state->pe_gic_cpu_base ^
        state->pe_timer_base ^ state->pe_timer_frequency ^ state->pe_vector_base ^
        state->boot_flags ^ state->validation_mask ^ state->root_steps ^ state->root_status ^
        state->init_steps ^ state->init_status ^ state->init_timebase_freq ^
        state->init_timebase_delta_us ^ state->init_gic_irq_count ^ state->init_gic_cpu_count ^
        state->init_gic_dist_ctlr ^ state->init_gic_cpu_ctlr ^ state->root_boot_args_virt ^
        state->root_dt_virt ^ state->root_dt_root_children ^ state->root_dt_memory_base ^
        state->root_dt_memory_size ^ state->root_dt_timer_frequency ^
        state->root_dt_summary_status ^ state->status;

    xnu_log_kv32("high_root_steps", state->root_steps);
    xnu_log_kv32("high_root_status", state->root_status);
    xnu_log_kv32("high_bootstrap_validation_mask", state->validation_mask);
    xnu_log_kv32("high_bootstrap_init_steps", state->init_steps);
    xnu_log_kv32("high_bootstrap_init_status", state->init_status);
    xnu_log_kv32("high_bootstrap_timebase_freq", state->init_timebase_freq);
    xnu_log_kv32("high_bootstrap_timebase_delta_us", state->init_timebase_delta_us);
    xnu_log_kv32("high_bootstrap_gic_irq_count", state->init_gic_irq_count);
    xnu_log_kv32("high_bootstrap_gic_cpu_count", state->init_gic_cpu_count);
    xnu_log_kv32("high_root_boot_args_virt", state->root_boot_args_virt);
    xnu_log_kv32("high_root_dt_virt", state->root_dt_virt);
    xnu_log_kv32("high_root_dt_root_children", state->root_dt_root_children);
    xnu_log_kv32("high_root_dt_memory_base", state->root_dt_memory_base);
    xnu_log_kv32("high_root_dt_memory_size", state->root_dt_memory_size);
    xnu_log_kv32("high_root_dt_timer_frequency", state->root_dt_timer_frequency);
    xnu_log_kv32("high_root_dt_summary_status", state->root_dt_summary_status);
    xnu_log_kv32("high_bootstrap_status", state->status);
    xnu_log_kv32("high_bootstrap_checksum", state->checksum);
    xnu_log_puts("high virtual kernel_root leaving\n");

    return state->status;
}

static inline uint32_t read_sctlr(void)
{
    uint32_t v;
    __asm__ volatile ("mrc p15, 0, %0, c1, c0, 0" : "=r"(v));
    return v;
}

static inline uint32_t read_ttbr0(void)
{
    uint32_t v;
    __asm__ volatile ("mrc p15, 0, %0, c2, c0, 0" : "=r"(v));
    return v;
}

static inline uint32_t read_dacr(void)
{
    uint32_t v;
    __asm__ volatile ("mrc p15, 0, %0, c3, c0, 0" : "=r"(v));
    return v;
}

static inline void write_ttbr0(uint32_t v)
{
    __asm__ volatile ("mcr p15, 0, %0, c2, c0, 0" :: "r"(v) : "memory");
}

static inline void write_ttbcr(uint32_t v)
{
    __asm__ volatile ("mcr p15, 0, %0, c2, c0, 2" :: "r"(v) : "memory");
}

static inline void write_dacr(uint32_t v)
{
    __asm__ volatile ("mcr p15, 0, %0, c3, c0, 0" :: "r"(v) : "memory");
}

static inline void invalidate_tlbs(void)
{
    uint32_t zero = 0;
    __asm__ volatile ("mcr p15, 0, %0, c8, c7, 0" :: "r"(zero) : "memory");
}

static inline void write_sctlr(uint32_t v)
{
    __asm__ volatile ("mcr p15, 0, %0, c1, c0, 0" :: "r"(v) : "memory");
}

static inline void dsb_isb(void)
{
    __asm__ volatile ("dsb sy\n\tisb" ::: "memory");
}

static void map_section(uint32_t va, uint32_t pa)
{
    stage18_l1_table[SECTION_INDEX(va)] = (pa & L1_SECTION_MASK) | L1_DESC_SECTION_SO;
}

static uint32_t table_entry_for(uint32_t va)
{
    return stage18_l1_table[SECTION_INDEX(va)];
}

static void build_identity_table(void)
{
    memset(stage18_l1_table, 0, sizeof(stage18_l1_table));

    /* Low payload/code/data/BSS/stack/VBAR page-table area. */
    map_section(0x00000000u, 0x00000000u);

    /* First controlled XNU-like high aliases for selected low code/data and debug/MMIO windows. */
    map_section(STAGE18_HIGH_ALIAS_BASE, 0x00000000u);
    map_section(STAGE18_RAM_CONSOLE_ALIAS_BASE, RAM_CONSOLE_BASE);
    map_section(STAGE18_GIC_ALIAS_BASE, 0xf9000000u);

    /* IMEM restart reason. */
    map_section(0x0fa00000u, 0x0fa00000u);

    /* Android ram_console persistent RAM window. */
    map_section(RAM_CONSOLE_BASE, RAM_CONSOLE_BASE);
    map_section(RAM_CONSOLE_BASE + L1_SECTION_SIZE, RAM_CONSOLE_BASE + L1_SECTION_SIZE);

    /* MSM8974 GIC + ARM timer MMIO share the 0xf9000000 section in this stage. */
    map_section(0xf9000000u, 0xf9000000u);

    /* MSM8974 PS_HOLD reset register lives in the 0xfc400000 section. */
    map_section(0xfc400000u, 0xfc400000u);
}

static void enable_identity_mmu(void)
{
    uint32_t sctlr;

    dsb_isb();
    write_ttbcr(0);
    write_ttbr0((uint32_t)(uintptr_t)stage18_l1_table);
    write_dacr(0x00000003u);          /* domain 0 manager during first bring-up */
    invalidate_tlbs();
    dsb_isb();

    sctlr = read_sctlr();
    sctlr &= ~((1u << 2) | (1u << 12)); /* keep D-cache/I-cache disabled */
    sctlr |= 1u;                         /* MMU enable */
    write_sctlr(sctlr);
    dsb_isb();
}

int mmu_identity_selftest(void)
{
    volatile uint32_t *gicd_ctlr = (volatile uint32_t *)(uintptr_t)PE_state_stage18.gicDistributorBase;
    volatile uint32_t *gicc_ctlr = (volatile uint32_t *)(uintptr_t)PE_state_stage18.gicCpuBase;
    volatile uint32_t *restart_reason = (volatile uint32_t *)(uintptr_t)RESTART_REASON;
    volatile uint32_t *ram_console_sig = (volatile uint32_t *)(uintptr_t)RAM_CONSOLE_BASE;

    xnu_log_puts("mmu identity selftest begin\n");
    xnu_log_kv32("mmu_sctlr_before", read_sctlr());
    xnu_log_kv32("mmu_ttbr0_before", read_ttbr0());
    xnu_log_kv32("mmu_dacr_before", read_dacr());
    xnu_log_kv32("mmu_l1_table", (uint32_t)(uintptr_t)stage18_l1_table);

    build_identity_table();
    xnu_log_kv32("mmu_entry_low", table_entry_for(0x00008000u));
    xnu_log_kv32("mmu_entry_high_alias", table_entry_for(STAGE18_HIGH_ALIAS_BASE));
    xnu_log_kv32("mmu_entry_high_ram_console", table_entry_for(STAGE18_RAM_CONSOLE_ALIAS_BASE));
    xnu_log_kv32("mmu_entry_high_gic", table_entry_for(STAGE18_GIC_ALIAS_BASE));
    xnu_log_kv32("mmu_entry_ram_console", table_entry_for(RAM_CONSOLE_BASE));
    xnu_log_kv32("mmu_entry_imem", table_entry_for(RESTART_REASON));
    xnu_log_kv32("mmu_entry_gic", table_entry_for(PE_state_stage18.gicDistributorBase));
    xnu_log_kv32("mmu_entry_timer", table_entry_for(PE_state_stage18.timerBase));
    xnu_log_kv32("mmu_entry_pshold", table_entry_for(MSM8974_PSHOLD));

    enable_identity_mmu();

    xnu_log_kv32("mmu_sctlr_after", read_sctlr());
    xnu_log_kv32("mmu_ttbr0_after", read_ttbr0());
    xnu_log_kv32("mmu_dacr_after", read_dacr());

    stage18_mmu_probe_word = 0x10aa55ffu;
    xnu_log_kv32("mmu_probe_word", stage18_mmu_probe_word);
    xnu_log_kv32("mmu_ram_console_sig", *ram_console_sig);
    xnu_log_kv32("mmu_restart_reason_read", *restart_reason);
    xnu_log_kv32("mmu_gicd_ctlr_read", *gicd_ctlr);
    xnu_log_kv32("mmu_gicc_ctlr_read", *gicc_ctlr);
    xnu_log_kv32("mmu_timer_freq_check", timebase_freq_hz());

    if ((read_sctlr() & 1u) == 0u) {
        xnu_log_puts("mmu identity selftest failed: SCTLR.M clear\n");
        return 0;
    }
    if (stage18_mmu_probe_word != 0x10aa55ffu) {
        xnu_log_puts("mmu identity selftest failed: data probe mismatch\n");
        return 0;
    }
    if (*ram_console_sig != RAM_CONSOLE_SIG) {
        xnu_log_puts("mmu identity selftest failed: ram_console sig mismatch\n");
        return 0;
    }
    if ((*gicd_ctlr & 1u) == 0u || (*gicc_ctlr & 1u) == 0u) {
        xnu_log_puts("mmu identity selftest failed: GIC control reads suspicious\n");
        return 0;
    }

    xnu_log_puts("mmu identity selftest ok\n");
    return 1;
}

int mmu_high_alias_selftest(void)
{
    volatile uint32_t *alias_probe;
    volatile uint32_t *identity_probe = &stage18_mmu_alias_probe;
    volatile uint32_t *alias_vector;
    volatile uint32_t *identity_vector = (volatile uint32_t *)(uintptr_t)stage18_vectors;
    volatile uint32_t *alias_ram_console;
    volatile uint32_t *identity_ram_console = (volatile uint32_t *)(uintptr_t)RAM_CONSOLE_BASE;
    volatile uint32_t *alias_gicd_ctlr;
    volatile uint32_t *identity_gicd_ctlr = (volatile uint32_t *)(uintptr_t)PE_state_stage18.gicDistributorBase;
    uint32_t probe_phys = (uint32_t)(uintptr_t)&stage18_mmu_alias_probe;
    uint32_t vector_phys = (uint32_t)(uintptr_t)stage18_vectors;

    xnu_log_puts("mmu high alias selftest begin\n");
    xnu_log_kv32("mmu_alias_base", STAGE18_HIGH_ALIAS_BASE);
    xnu_log_kv32("mmu_alias_entry", table_entry_for(STAGE18_HIGH_ALIAS_BASE));
    xnu_log_kv32("mmu_alias_probe_phys", probe_phys);
    xnu_log_kv32("mmu_alias_vector_phys", vector_phys);

    if ((read_sctlr() & 1u) == 0u) {
        xnu_log_puts("mmu high alias selftest failed: MMU disabled\n");
        return 0;
    }

    alias_probe = (volatile uint32_t *)(uintptr_t)(STAGE18_HIGH_ALIAS_BASE + probe_phys);
    alias_vector = (volatile uint32_t *)(uintptr_t)(STAGE18_HIGH_ALIAS_BASE + vector_phys);
    alias_ram_console = (volatile uint32_t *)(uintptr_t)STAGE18_RAM_CONSOLE_ALIAS_BASE;
    alias_gicd_ctlr = (volatile uint32_t *)(uintptr_t)(STAGE18_GIC_ALIAS_BASE + (PE_state_stage18.gicDistributorBase - 0xf9000000u));

    *identity_probe = 0x11112222u;
    xnu_log_kv32("mmu_alias_read_after_identity_write", *alias_probe);
    if (*alias_probe != 0x11112222u) {
        xnu_log_puts("mmu high alias selftest failed: alias read mismatch\n");
        return 0;
    }

    *alias_probe = 0x33334444u;
    xnu_log_kv32("mmu_identity_read_after_alias_write", *identity_probe);
    if (*identity_probe != 0x33334444u) {
        xnu_log_puts("mmu high alias selftest failed: identity read mismatch\n");
        return 0;
    }

    xnu_log_kv32("mmu_alias_vector_word", *alias_vector);
    xnu_log_kv32("mmu_identity_vector_word", *identity_vector);
    if (*alias_vector != *identity_vector) {
        xnu_log_puts("mmu high alias selftest failed: vector alias mismatch\n");
        return 0;
    }

    xnu_log_kv32("mmu_alias_ram_console_sig", *alias_ram_console);
    xnu_log_kv32("mmu_identity_ram_console_sig", *identity_ram_console);
    if (*alias_ram_console != RAM_CONSOLE_SIG) {
        xnu_log_puts("mmu high alias selftest failed: ram_console alias mismatch\n");
        return 0;
    }

    xnu_log_kv32("mmu_alias_gicd_ctlr", *alias_gicd_ctlr);
    xnu_log_kv32("mmu_identity_gicd_ctlr", *identity_gicd_ctlr);
    if ((*alias_gicd_ctlr & 1u) == 0u || *alias_gicd_ctlr != *identity_gicd_ctlr) {
        xnu_log_puts("mmu high alias selftest failed: GIC alias mismatch\n");
        return 0;
    }

    xnu_log_puts("mmu high alias selftest ok\n");
    return 1;
}

int mmu_high_call_selftest(void)
{
    typedef uint32_t (*alias_fn_t)(uint32_t, volatile struct stage18_alias_state *);

    const uint32_t input = 0x55667788u;
    const uint32_t expected = (input ^ 0x12c0ffeeu) + 0x1234u;
    uint32_t fn_phys = (uint32_t)(uintptr_t)stage18_high_alias_target;
    uint32_t state_phys = (uint32_t)(uintptr_t)&stage18_alias_state_block;
    alias_fn_t alias_fn = (alias_fn_t)(uintptr_t)(STAGE18_HIGH_ALIAS_BASE + fn_phys);
    volatile struct stage18_alias_state *alias_state =
        (volatile struct stage18_alias_state *)(uintptr_t)(STAGE18_HIGH_ALIAS_BASE + state_phys);
    uint32_t result;

    xnu_log_puts("mmu high call selftest begin\n");
    xnu_log_kv32("mmu_high_call_fn_phys", fn_phys);
    xnu_log_kv32("mmu_high_call_fn_virt", (uint32_t)(uintptr_t)alias_fn);
    xnu_log_kv32("mmu_high_call_state_phys", state_phys);
    xnu_log_kv32("mmu_high_call_state_virt", (uint32_t)(uintptr_t)alias_state);
    xnu_log_kv32("mmu_high_call_input", input);
    xnu_log_kv32("mmu_high_call_expected", expected);

    memset(&stage18_alias_state_block, 0, sizeof(stage18_alias_state_block));

    if ((read_sctlr() & 1u) == 0u) {
        xnu_log_puts("mmu high call selftest failed: MMU disabled\n");
        return 0;
    }

    result = alias_fn(input, alias_state);

    xnu_log_kv32("mmu_high_call_result", result);
    xnu_log_kv32("mmu_high_call_magic_id", stage18_alias_state_block.magic);
    xnu_log_kv32("mmu_high_call_input_id", stage18_alias_state_block.input);
    xnu_log_kv32("mmu_high_call_result_id", stage18_alias_state_block.result);
    xnu_log_kv32("mmu_high_call_checksum_id", stage18_alias_state_block.checksum);
    xnu_log_kv32("mmu_high_call_magic_alias", alias_state->magic);
    xnu_log_kv32("mmu_high_call_checksum_alias", alias_state->checksum);

    if (result != expected || stage18_alias_state_block.result != expected) {
        xnu_log_puts("mmu high call selftest failed: result mismatch\n");
        return 0;
    }
    if (stage18_alias_state_block.magic != 0x12001200u || stage18_alias_state_block.input != input) {
        xnu_log_puts("mmu high call selftest failed: state mismatch\n");
        return 0;
    }
    if (stage18_alias_state_block.checksum != (stage18_alias_state_block.magic ^ input ^ expected)) {
        xnu_log_puts("mmu high call selftest failed: checksum mismatch\n");
        return 0;
    }
    if (alias_state->checksum != stage18_alias_state_block.checksum) {
        xnu_log_puts("mmu high call selftest failed: alias state mismatch\n");
        return 0;
    }

    xnu_log_puts("mmu high call selftest ok\n");
    return 1;
}

int mmu_high_bootstrap_selftest(void)
{
    typedef uint32_t (*bootstrap_fn_t)(struct boot_args *,
                                       volatile struct pe_platform_state *,
                                       volatile struct stage18_bootstrap_state *);

    uint32_t fn_phys = (uint32_t)(uintptr_t)stage18_kernel_root;
    uint32_t args_phys = (uint32_t)(uintptr_t)PE_state_stage18.bootArgs;
    uint32_t pe_phys = (uint32_t)(uintptr_t)&PE_state_stage18;
    uint32_t state_phys = (uint32_t)(uintptr_t)&stage18_bootstrap_state_block;
    bootstrap_fn_t bootstrap_fn = (bootstrap_fn_t)(uintptr_t)(STAGE18_HIGH_ALIAS_BASE + fn_phys);
    struct boot_args *alias_args = (struct boot_args *)(uintptr_t)(STAGE18_HIGH_ALIAS_BASE + args_phys);
    volatile struct pe_platform_state *alias_pe =
        (volatile struct pe_platform_state *)(uintptr_t)(STAGE18_HIGH_ALIAS_BASE + pe_phys);
    volatile struct stage18_bootstrap_state *alias_state =
        (volatile struct stage18_bootstrap_state *)(uintptr_t)(STAGE18_HIGH_ALIAS_BASE + state_phys);
    uint32_t expected;
    uint32_t result;

    xnu_log_puts("mmu high bootstrap selftest begin\n");
    xnu_log_kv32("mmu_high_bootstrap_fn_phys", fn_phys);
    xnu_log_kv32("mmu_high_bootstrap_fn_virt", (uint32_t)(uintptr_t)bootstrap_fn);
    xnu_log_kv32("mmu_high_bootstrap_args_phys", args_phys);
    xnu_log_kv32("mmu_high_bootstrap_args_virt", (uint32_t)(uintptr_t)alias_args);
    xnu_log_kv32("mmu_high_bootstrap_pe_phys", pe_phys);
    xnu_log_kv32("mmu_high_bootstrap_pe_virt", (uint32_t)(uintptr_t)alias_pe);
    xnu_log_kv32("mmu_high_bootstrap_state_phys", state_phys);
    xnu_log_kv32("mmu_high_bootstrap_state_virt", (uint32_t)(uintptr_t)alias_state);

    memset(&stage18_bootstrap_state_block, 0, sizeof(stage18_bootstrap_state_block));

    if ((read_sctlr() & 1u) == 0u) {
        xnu_log_puts("mmu high bootstrap selftest failed: MMU disabled\n");
        return 0;
    }

    result = bootstrap_fn(alias_args, alias_pe, alias_state);

    expected = stage18_bootstrap_state_block.magic ^ stage18_bootstrap_state_block.boot_args_rev_ver ^
        stage18_bootstrap_state_block.machine_type ^ stage18_bootstrap_state_block.device_tree_length ^
        stage18_bootstrap_state_block.pe_memory_base ^ stage18_bootstrap_state_block.pe_memory_size ^
        stage18_bootstrap_state_block.pe_cpu_count ^ stage18_bootstrap_state_block.pe_gic_dist_base ^
        stage18_bootstrap_state_block.pe_gic_cpu_base ^ stage18_bootstrap_state_block.pe_timer_base ^
        stage18_bootstrap_state_block.pe_timer_frequency ^ stage18_bootstrap_state_block.pe_vector_base ^
        stage18_bootstrap_state_block.boot_flags ^ stage18_bootstrap_state_block.validation_mask ^
        stage18_bootstrap_state_block.root_steps ^ stage18_bootstrap_state_block.root_status ^
        stage18_bootstrap_state_block.init_steps ^ stage18_bootstrap_state_block.init_status ^
        stage18_bootstrap_state_block.init_timebase_freq ^ stage18_bootstrap_state_block.init_timebase_delta_us ^
        stage18_bootstrap_state_block.init_gic_irq_count ^ stage18_bootstrap_state_block.init_gic_cpu_count ^
        stage18_bootstrap_state_block.init_gic_dist_ctlr ^ stage18_bootstrap_state_block.init_gic_cpu_ctlr ^
        stage18_bootstrap_state_block.root_boot_args_virt ^ stage18_bootstrap_state_block.root_dt_virt ^
        stage18_bootstrap_state_block.root_dt_root_children ^ stage18_bootstrap_state_block.root_dt_memory_base ^
        stage18_bootstrap_state_block.root_dt_memory_size ^ stage18_bootstrap_state_block.root_dt_timer_frequency ^
        stage18_bootstrap_state_block.root_dt_summary_status ^ stage18_bootstrap_state_block.status;

    xnu_log_kv32("mmu_high_bootstrap_result", result);
    xnu_log_kv32("mmu_high_bootstrap_expected_checksum", expected);
    xnu_log_kv32("mmu_high_bootstrap_magic_id", stage18_bootstrap_state_block.magic);
    xnu_log_kv32("mmu_high_bootstrap_rev_ver_id", stage18_bootstrap_state_block.boot_args_rev_ver);
    xnu_log_kv32("mmu_high_bootstrap_machine_id", stage18_bootstrap_state_block.machine_type);
    xnu_log_kv32("mmu_high_bootstrap_dt_len_id", stage18_bootstrap_state_block.device_tree_length);
    xnu_log_kv32("mmu_high_bootstrap_mem_base_id", stage18_bootstrap_state_block.pe_memory_base);
    xnu_log_kv32("mmu_high_bootstrap_mem_size_id", stage18_bootstrap_state_block.pe_memory_size);
    xnu_log_kv32("mmu_high_bootstrap_cpu_count_id", stage18_bootstrap_state_block.pe_cpu_count);
    xnu_log_kv32("mmu_high_bootstrap_gic_dist_id", stage18_bootstrap_state_block.pe_gic_dist_base);
    xnu_log_kv32("mmu_high_bootstrap_gic_cpu_id", stage18_bootstrap_state_block.pe_gic_cpu_base);
    xnu_log_kv32("mmu_high_bootstrap_timer_base_id", stage18_bootstrap_state_block.pe_timer_base);
    xnu_log_kv32("mmu_high_bootstrap_timer_freq_id", stage18_bootstrap_state_block.pe_timer_frequency);
    xnu_log_kv32("mmu_high_bootstrap_vector_id", stage18_bootstrap_state_block.pe_vector_base);
    xnu_log_kv32("mmu_high_bootstrap_boot_flags_id", stage18_bootstrap_state_block.boot_flags);
    xnu_log_kv32("mmu_high_bootstrap_validation_id", stage18_bootstrap_state_block.validation_mask);
    xnu_log_kv32("mmu_high_bootstrap_root_steps_id", stage18_bootstrap_state_block.root_steps);
    xnu_log_kv32("mmu_high_bootstrap_root_status_id", stage18_bootstrap_state_block.root_status);
    xnu_log_kv32("mmu_high_bootstrap_init_steps_id", stage18_bootstrap_state_block.init_steps);
    xnu_log_kv32("mmu_high_bootstrap_init_status_id", stage18_bootstrap_state_block.init_status);
    xnu_log_kv32("mmu_high_bootstrap_timebase_freq_id", stage18_bootstrap_state_block.init_timebase_freq);
    xnu_log_kv32("mmu_high_bootstrap_timebase_delta_us_id", stage18_bootstrap_state_block.init_timebase_delta_us);
    xnu_log_kv32("mmu_high_bootstrap_gic_irq_count_id", stage18_bootstrap_state_block.init_gic_irq_count);
    xnu_log_kv32("mmu_high_bootstrap_gic_cpu_count_id", stage18_bootstrap_state_block.init_gic_cpu_count);
    xnu_log_kv32("mmu_high_bootstrap_gic_dist_ctlr_id", stage18_bootstrap_state_block.init_gic_dist_ctlr);
    xnu_log_kv32("mmu_high_bootstrap_gic_cpu_ctlr_id", stage18_bootstrap_state_block.init_gic_cpu_ctlr);
    xnu_log_kv32("mmu_high_bootstrap_root_boot_args_virt_id", stage18_bootstrap_state_block.root_boot_args_virt);
    xnu_log_kv32("mmu_high_bootstrap_root_dt_virt_id", stage18_bootstrap_state_block.root_dt_virt);
    xnu_log_kv32("mmu_high_bootstrap_root_dt_children_id", stage18_bootstrap_state_block.root_dt_root_children);
    xnu_log_kv32("mmu_high_bootstrap_root_dt_mem_base_id", stage18_bootstrap_state_block.root_dt_memory_base);
    xnu_log_kv32("mmu_high_bootstrap_root_dt_mem_size_id", stage18_bootstrap_state_block.root_dt_memory_size);
    xnu_log_kv32("mmu_high_bootstrap_root_dt_timer_freq_id", stage18_bootstrap_state_block.root_dt_timer_frequency);
    xnu_log_kv32("mmu_high_bootstrap_root_dt_summary_status_id", stage18_bootstrap_state_block.root_dt_summary_status);
    xnu_log_kv32("mmu_high_bootstrap_status_id", stage18_bootstrap_state_block.status);
    xnu_log_kv32("mmu_high_bootstrap_checksum_id", stage18_bootstrap_state_block.checksum);
    xnu_log_kv32("mmu_high_bootstrap_magic_alias", alias_state->magic);
    xnu_log_kv32("mmu_high_bootstrap_root_steps_alias", alias_state->root_steps);
    xnu_log_kv32("mmu_high_bootstrap_root_status_alias", alias_state->root_status);
    xnu_log_kv32("mmu_high_bootstrap_root_dt_virt_alias", alias_state->root_dt_virt);
    xnu_log_kv32("mmu_high_bootstrap_root_dt_summary_status_alias", alias_state->root_dt_summary_status);
    xnu_log_kv32("mmu_high_bootstrap_init_steps_alias", alias_state->init_steps);
    xnu_log_kv32("mmu_high_bootstrap_status_alias", alias_state->status);
    xnu_log_kv32("mmu_high_bootstrap_checksum_alias", alias_state->checksum);

    if (result != STAGE18_BOOTSTRAP_STATUS_OK ||
        stage18_bootstrap_state_block.status != STAGE18_BOOTSTRAP_STATUS_OK ||
        stage18_bootstrap_state_block.validation_mask != 0u) {
        xnu_log_puts("mmu high bootstrap selftest failed: validation status\n");
        return 0;
    }
    if (stage18_bootstrap_state_block.checksum != expected) {
        xnu_log_puts("mmu high bootstrap selftest failed: checksum mismatch\n");
        return 0;
    }
    if (stage18_bootstrap_state_block.magic != 0x18001800u ||
        stage18_bootstrap_state_block.boot_args_rev_ver != 0x00020002u ||
        stage18_bootstrap_state_block.machine_type != MACHINE_TYPE_MSM8974) {
        xnu_log_puts("mmu high bootstrap selftest failed: boot args mismatch\n");
        return 0;
    }
    if (stage18_bootstrap_state_block.root_steps != STAGE18_ROOT_REQUIRED_STEPS ||
        stage18_bootstrap_state_block.root_status != STAGE18_BOOTSTRAP_STATUS_OK) {
        xnu_log_puts("mmu high bootstrap selftest failed: high root mismatch\n");
        return 0;
    }
    if (stage18_bootstrap_state_block.root_boot_args_virt != (uint32_t)(uintptr_t)alias_args ||
        stage18_bootstrap_state_block.root_dt_virt < STAGE18_HIGH_ALIAS_BASE ||
        stage18_bootstrap_state_block.root_dt_root_children != 6u ||
        stage18_bootstrap_state_block.root_dt_memory_base != RAM_PHYS_BASE ||
        stage18_bootstrap_state_block.root_dt_memory_size != (RAM_CONSOLE_BASE - RAM_PHYS_BASE) ||
        stage18_bootstrap_state_block.root_dt_timer_frequency != 19200000u ||
        stage18_bootstrap_state_block.root_dt_summary_status != STAGE18_BOOTSTRAP_STATUS_OK) {
        xnu_log_puts("mmu high bootstrap selftest failed: high DT summary mismatch\n");
        return 0;
    }
    if (stage18_bootstrap_state_block.init_steps != STAGE18_INIT_REQUIRED_STEPS ||
        stage18_bootstrap_state_block.init_status != STAGE18_BOOTSTRAP_STATUS_OK ||
        stage18_bootstrap_state_block.init_timebase_freq != 19200000u ||
        stage18_bootstrap_state_block.init_timebase_delta_us < 900u ||
        stage18_bootstrap_state_block.init_gic_irq_count < 288u ||
        stage18_bootstrap_state_block.init_gic_cpu_count != 4u ||
        (stage18_bootstrap_state_block.init_gic_dist_ctlr & 1u) == 0u ||
        (stage18_bootstrap_state_block.init_gic_cpu_ctlr & 1u) == 0u) {
        xnu_log_puts("mmu high bootstrap selftest failed: high init sequence mismatch\n");
        return 0;
    }
    if (stage18_bootstrap_state_block.pe_memory_base != PE_state_stage18.memoryBase ||
        stage18_bootstrap_state_block.pe_memory_size != PE_state_stage18.memorySize ||
        stage18_bootstrap_state_block.pe_cpu_count != PE_state_stage18.cpuCount ||
        stage18_bootstrap_state_block.pe_gic_dist_base != PE_state_stage18.gicDistributorBase ||
        stage18_bootstrap_state_block.pe_gic_cpu_base != PE_state_stage18.gicCpuBase ||
        stage18_bootstrap_state_block.pe_timer_base != PE_state_stage18.timerBase ||
        stage18_bootstrap_state_block.pe_timer_frequency != 19200000u ||
        stage18_bootstrap_state_block.pe_vector_base != (uint32_t)(uintptr_t)stage18_vectors) {
        xnu_log_puts("mmu high bootstrap selftest failed: PE state mismatch\n");
        return 0;
    }
    if (alias_state->checksum != stage18_bootstrap_state_block.checksum ||
        alias_state->status != STAGE18_BOOTSTRAP_STATUS_OK ||
        alias_state->root_status != STAGE18_BOOTSTRAP_STATUS_OK ||
        alias_state->root_steps != STAGE18_ROOT_REQUIRED_STEPS ||
        alias_state->root_dt_summary_status != STAGE18_BOOTSTRAP_STATUS_OK ||
        alias_state->root_dt_memory_base != RAM_PHYS_BASE ||
        alias_state->root_dt_timer_frequency != 19200000u ||
        alias_state->init_status != STAGE18_BOOTSTRAP_STATUS_OK ||
        alias_state->init_steps != STAGE18_INIT_REQUIRED_STEPS ||
        alias_state->validation_mask != 0u) {
        xnu_log_puts("mmu high bootstrap selftest failed: alias state mismatch\n");
        return 0;
    }

    xnu_log_puts("mmu high bootstrap selftest ok\n");
    return 1;
}
