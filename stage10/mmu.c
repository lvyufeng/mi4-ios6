#include "stage10.h"

#define L1_SECTION_COUNT       4096u
#define L1_SECTION_SIZE        0x00100000u
#define L1_SECTION_MASK        0xfff00000u

/* ARMv7 short-descriptor section: domain 0, AP full access, strongly ordered/shareable, executable. */
#define L1_DESC_SECTION_SO     0x00010c02u

#define SECTION_INDEX(addr)    (((uint32_t)(addr)) >> 20)

static uint32_t stage10_l1_table[L1_SECTION_COUNT] __attribute__((aligned(16384)));
static volatile uint32_t stage10_mmu_probe_word;

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
    stage10_l1_table[SECTION_INDEX(va)] = (pa & L1_SECTION_MASK) | L1_DESC_SECTION_SO;
}

static uint32_t table_entry_for(uint32_t va)
{
    return stage10_l1_table[SECTION_INDEX(va)];
}

static void build_identity_table(void)
{
    memset(stage10_l1_table, 0, sizeof(stage10_l1_table));

    /* Low payload/code/data/BSS/stack/VBAR page-table area. */
    map_section(0x00000000u, 0x00000000u);

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
    write_ttbr0((uint32_t)(uintptr_t)stage10_l1_table);
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
    volatile uint32_t *gicd_ctlr = (volatile uint32_t *)(uintptr_t)PE_state_stage10.gicDistributorBase;
    volatile uint32_t *gicc_ctlr = (volatile uint32_t *)(uintptr_t)PE_state_stage10.gicCpuBase;
    volatile uint32_t *restart_reason = (volatile uint32_t *)(uintptr_t)RESTART_REASON;
    volatile uint32_t *ram_console_sig = (volatile uint32_t *)(uintptr_t)RAM_CONSOLE_BASE;

    xnu_log_puts("mmu identity selftest begin\n");
    xnu_log_kv32("mmu_sctlr_before", read_sctlr());
    xnu_log_kv32("mmu_ttbr0_before", read_ttbr0());
    xnu_log_kv32("mmu_dacr_before", read_dacr());
    xnu_log_kv32("mmu_l1_table", (uint32_t)(uintptr_t)stage10_l1_table);

    build_identity_table();
    xnu_log_kv32("mmu_entry_low", table_entry_for(0x00008000u));
    xnu_log_kv32("mmu_entry_ram_console", table_entry_for(RAM_CONSOLE_BASE));
    xnu_log_kv32("mmu_entry_imem", table_entry_for(RESTART_REASON));
    xnu_log_kv32("mmu_entry_gic", table_entry_for(PE_state_stage10.gicDistributorBase));
    xnu_log_kv32("mmu_entry_timer", table_entry_for(PE_state_stage10.timerBase));
    xnu_log_kv32("mmu_entry_pshold", table_entry_for(MSM8974_PSHOLD));

    enable_identity_mmu();

    xnu_log_kv32("mmu_sctlr_after", read_sctlr());
    xnu_log_kv32("mmu_ttbr0_after", read_ttbr0());
    xnu_log_kv32("mmu_dacr_after", read_dacr());

    stage10_mmu_probe_word = 0x10aa55ffu;
    xnu_log_kv32("mmu_probe_word", stage10_mmu_probe_word);
    xnu_log_kv32("mmu_ram_console_sig", *ram_console_sig);
    xnu_log_kv32("mmu_restart_reason_read", *restart_reason);
    xnu_log_kv32("mmu_gicd_ctlr_read", *gicd_ctlr);
    xnu_log_kv32("mmu_gicc_ctlr_read", *gicc_ctlr);
    xnu_log_kv32("mmu_timer_freq_check", timebase_freq_hz());

    if ((read_sctlr() & 1u) == 0u) {
        xnu_log_puts("mmu identity selftest failed: SCTLR.M clear\n");
        return 0;
    }
    if (stage10_mmu_probe_word != 0x10aa55ffu) {
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
