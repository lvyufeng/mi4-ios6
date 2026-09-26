/* Stage84 Stage-owned full kernel virtual address space pmap.
 * Extends Stage82's minimal L1-only candidate pmap to a complete L1+L2 two-level
 * translation supporting 4KB page granularity and high-virtual kernel mapping at
 * virtBase=0x80000000. Models public ARM XNU arm_vm_init/pmap_bootstrap but remains
 * Stage-owned, fail-closed, and non-persistent.
 *
 * Stage84 builds:
 * - L1 table (4096 entries, 16KB aligned) with mix of 1MB sections and page table ptrs
 * - L2 tables (256 entries each, 1KB aligned) for 4KB page mappings
 * - Kernel image mapped to high VA (0x80000000+) via L2 pages
 * - Physical RAM direct map via L1 sections
 * - Device MMIO via L1 sections (GIC, RAM console, IMEM, PS_HOLD)
 *
 * Stage84 installs the candidate pmap, verifies high-virtual data access (not PC
 * relocation), then restores original TTBR0. It still avoids public XNU/pmap runtime,
 * generated Mach-O execution, cache policy changes, and persistent writes.
 *
 * Public ARM XNU reference (not executed):
 *   external/xnu-4570.1.46/osfmk/arm/arm_vm_init.c
 *   external/xnu-4570.1.46/osfmk/arm/pmap.c
 *   external/xnu-4570.1.46/osfmk/arm/proc_reg.h (ARMv7 descriptor format)
 */

#include "stage90.h"
#include <stddef.h>

/* Stage84 virtual address space layout */
#define STAGE90_VIRT_BASE              0x80000000u  /* Kernel virtual base */
#define STAGE90_PHYS_BASE              0x00008000u  /* Kernel physical base */
/*
 * `STAGE90_HIGH_ALIAS_BASE`, `STAGE90_IMAGE_ALIAS_LIMIT`, `STAGE90_GIC_ALIAS_BASE` and
 * `STAGE90_RAM_CONSOLE_ALIAS_BASE` all live in stage90.h now: mmu.c's `build_identity_table()`
 * builds the same image alias and the same pair of device aliases, and a base defined twice drifts.
 *
 * This file's copy was the last one to move, and it was the one that broke. It kept the RAM-console
 * alias at 0xc0300000 and used *that* as the image-alias loop's limit, so this table's window was
 * 3 MB while mmu.c's was 4 MB - one window, two limits. The RAM console alias used to live at
 * 0xc0100000, which is also the second megabyte of the image alias; it was moved because that
 * collided with the deviceTreeP alias (0xc010c18c would have resolved into the ramoops buffer,
 * presenting as a garbage device tree rather than a fault). The move fixed the collision and left
 * the window silently smaller than the other table's, which is what experiment 425 found: the alias
 * loop stopped at 0xc0300000, `stage90_full_pmap_probe_word` had just moved to PA 0x32C0B4, and the
 * probe read ramoops content through the RAM-console alias at 0xc032c0b4. See stage90.h for the
 * window's size and the build-time check that keeps both aliases outside it.
 */

/* ARMv7 short-descriptor format constants */
#define STAGE90_XNU_TTE_L1_ENTRY_COUNT 4096u
#define STAGE90_XNU_TTE_L2_ENTRY_COUNT 256u
#define STAGE90_XNU_TTE_L2_TABLE_COUNT 128u  /* Pool of L2 tables */

/* L1 descriptor types */
#define L1_DESC_FAULT         0x00000000u
#define L1_DESC_PAGE_TABLE    0x00000001u  /* Points to L2 table */
#define L1_DESC_SECTION       0x00000002u  /* 1MB section */

/* L1 section descriptor (1MB, from Stage81/82) */
#define L1_SECTION_SIZE       0x00100000u
#define L1_SECTION_MASK       0xfff00000u
#define L1_DESC_SECTION_SO     STAGE90_PMAP_DESC_SECTION_SO
#define L1_DESC_SECTION_DRAM   STAGE90_PMAP_DESC_SECTION_DRAM

/* L1 page table pointer (points to L2 table, 1KB aligned) */
#define L1_TABLE_MASK         0xfffffc00u  /* L2 table base address mask */

/* L2 descriptor types (4KB pages) */
#define L2_DESC_FAULT         0x00000000u
#define L2_DESC_SMALL_PAGE    0x00000002u  /* Extended small page (4KB) */

/* L2 small page descriptor (4KB) */
#define L2_PAGE_SIZE          0x00001000u
#define L2_PAGE_MASK          0xfffff000u
#define L2_DESC_PAGE_SO        STAGE90_PMAP_DESC_PAGE_SO     /* Strongly-Ordered, AP=11, XN=0, small page type */
#define L2_DESC_PAGE_DRAM      STAGE90_PMAP_DESC_PAGE_DRAM   /* memory type per STAGE90_PMAP_ATTR_MODE */

/* Index extraction macros */
#define L1_INDEX(va)          (((va) >> 20) & 0xfffu)    /* bits [31:20] */
#define L2_INDEX(va)          (((va) >> 12) & 0xffu)     /* bits [19:12] */

static struct stage90_xnu_arm_vm_init_full_pmap_result g_result;
static volatile uint32_t stage90_full_pmap_probe_word;

/* 16KB-aligned candidate L1 table (4096 entries) */
static uint32_t stage90_candidate_l1[STAGE90_XNU_TTE_L1_ENTRY_COUNT]
    __attribute__((aligned(16384)));

/* L2 tables pool: 128 tables × 256 entries = 32768 entries total (128KB) */
static uint32_t stage90_candidate_l2_pool[STAGE90_XNU_TTE_L2_TABLE_COUNT * STAGE90_XNU_TTE_L2_ENTRY_COUNT]
    __attribute__((aligned(1024)));

/* L2 table allocation cursor */
static uint32_t stage90_l2_table_allocated_count;

static uint32_t stage90_xnu_arm_vm_init_full_pmap_checksum(
    volatile const struct stage90_xnu_arm_vm_init_full_pmap_result *r)
{
    volatile const uint32_t *words = (volatile const uint32_t *)r;
    uint32_t count = (uint32_t)(offsetof(struct stage90_xnu_arm_vm_init_full_pmap_result, checksum) / sizeof(uint32_t));
    uint32_t chk = 0;
    for (uint32_t i = 0; i < count; i++) {
        chk ^= words[i];
    }
    return chk;
}

static void stage90_xnu_arm_vm_init_full_pmap_log(
    volatile const struct stage90_xnu_arm_vm_init_full_pmap_result *r)
{
    xnu_log_puts("stage90_xnu_arm_vm_init_full_pmap result:\n");
    xnu_log_kv32("stage90_xnu_arm_vm_init_full_pmap_version", r->version);
    xnu_log_kv32("stage90_xnu_arm_vm_init_full_pmap_size", r->size);
    xnu_log_kv32("stage90_xnu_arm_vm_init_full_pmap_status", r->status);
    xnu_log_kv32("stage90_xnu_arm_vm_init_full_pmap_required_mask", r->required_mask);
    xnu_log_kv32("stage90_xnu_arm_vm_init_full_pmap_satisfied_mask", r->satisfied_mask);
    xnu_log_kv32("stage90_xnu_arm_vm_init_full_pmap_failure_mask", r->failure_mask);
    xnu_log_kv32("stage90_xnu_arm_vm_init_full_pmap_source_post_pe_status", r->source_post_pe_status);
    xnu_log_kv32("stage90_xnu_arm_vm_init_full_pmap_source_post_pe_checksum", r->source_post_pe_checksum);
    xnu_log_kv32("stage90_xnu_arm_vm_init_full_pmap_boot_args_ptr", r->boot_args_ptr);
    xnu_log_kv32("stage90_xnu_arm_vm_init_full_pmap_boot_args_valid", r->boot_args_valid);
    xnu_log_kv32("stage90_xnu_arm_vm_init_full_pmap_memory_size", r->memory_size);
    xnu_log_kv32("stage90_xnu_arm_vm_init_full_pmap_phys_base", r->phys_base);
    xnu_log_kv32("stage90_xnu_arm_vm_init_full_pmap_candidate_l1_base", r->candidate_l1_base);
    xnu_log_kv32("stage90_xnu_arm_vm_init_full_pmap_candidate_l1_checksum", r->candidate_l1_checksum);
    xnu_log_kv32("stage90_xnu_arm_vm_init_full_pmap_candidate_l2_pool_base", r->candidate_l2_pool_base);
    xnu_log_kv32("stage90_xnu_arm_vm_init_full_pmap_candidate_l2_pool_checksum", r->candidate_l2_pool_checksum);
    xnu_log_kv32("stage90_xnu_arm_vm_init_full_pmap_l2_tables_allocated", r->l2_tables_allocated);
    xnu_log_kv32("stage90_xnu_arm_vm_init_full_pmap_virt_base", r->virt_base);
    xnu_log_kv32("stage90_xnu_arm_vm_init_full_pmap_original_ttbr0", r->original_ttbr0);
    xnu_log_kv32("stage90_xnu_arm_vm_init_full_pmap_original_ttbcr", r->original_ttbcr);
    xnu_log_kv32("stage90_xnu_arm_vm_init_full_pmap_original_dacr", r->original_dacr);
    xnu_log_kv32("stage90_xnu_arm_vm_init_full_pmap_original_sctlr", r->original_sctlr);
    xnu_log_kv32("stage90_xnu_arm_vm_init_full_pmap_live_ttbr0", r->live_ttbr0);
    xnu_log_kv32("stage90_xnu_arm_vm_init_full_pmap_live_ttbcr", r->live_ttbcr);
    xnu_log_kv32("stage90_xnu_arm_vm_init_full_pmap_live_dacr", r->live_dacr);
    xnu_log_kv32("stage90_xnu_arm_vm_init_full_pmap_live_sctlr", r->live_sctlr);
    xnu_log_kv32("stage90_xnu_arm_vm_init_full_pmap_restored_ttbr0", r->restored_ttbr0);
    xnu_log_kv32("stage90_xnu_arm_vm_init_full_pmap_restored_ttbcr", r->restored_ttbcr);
    xnu_log_kv32("stage90_xnu_arm_vm_init_full_pmap_restored_dacr", r->restored_dacr);
    xnu_log_kv32("stage90_xnu_arm_vm_init_full_pmap_restored_sctlr", r->restored_sctlr);
    xnu_log_kv32("stage90_xnu_arm_vm_init_full_pmap_ttbr0_write_count", r->ttbr0_write_count);
    xnu_log_kv32("stage90_xnu_arm_vm_init_full_pmap_tlb_invalidate_count", r->tlb_invalidate_count);
    xnu_log_kv32("stage90_xnu_arm_vm_init_full_pmap_live_pmap_installed", r->live_pmap_installed);
    xnu_log_kv32("stage90_xnu_arm_vm_init_full_pmap_live_pmap_verified", r->live_pmap_verified);
    xnu_log_kv32("stage90_xnu_arm_vm_init_full_pmap_live_pmap_restored", r->live_pmap_restored);
    xnu_log_kv32("stage90_xnu_arm_vm_init_full_pmap_high_alias_verified", r->high_alias_verified);
    xnu_log_kv32("stage90_xnu_arm_vm_init_full_pmap_ram_console_verified", r->ram_console_verified);
    xnu_log_kv32("stage90_xnu_arm_vm_init_full_pmap_gic_verified", r->gic_verified);
    xnu_log_kv32("stage90_xnu_arm_vm_init_full_pmap_high_va_data_verified", r->high_va_data_verified);
    xnu_log_kv32("stage90_xnu_arm_vm_init_full_pmap_public_arm_vm_init_executed", r->public_arm_vm_init_executed);
    xnu_log_kv32("stage90_xnu_arm_vm_init_full_pmap_public_pmap_runtime_executed", r->public_pmap_runtime_executed);
    xnu_log_kv32("stage90_xnu_arm_vm_init_full_pmap_persistent_write_attempted", r->persistent_write_attempted);
    xnu_log_kv32("stage90_xnu_arm_vm_init_full_pmap_safety_boundary_preserved", r->safety_boundary_preserved);
    xnu_log_kv32("stage90_xnu_arm_vm_init_full_pmap_checksum", r->checksum);
}

static inline uint32_t read_ttbr0(void)
{
    uint32_t v;
    __asm__ volatile("mrc p15, 0, %0, c2, c0, 0" : "=r"(v));
    return v;
}

static inline uint32_t read_ttbcr(void)
{
    uint32_t v;
    __asm__ volatile("mrc p15, 0, %0, c2, c0, 2" : "=r"(v));
    return v;
}

static inline uint32_t read_dacr(void)
{
    uint32_t v;
    __asm__ volatile("mrc p15, 0, %0, c3, c0, 0" : "=r"(v));
    return v;
}

static inline uint32_t read_sctlr(void)
{
    uint32_t v;
    __asm__ volatile("mrc p15, 0, %0, c1, c0, 0" : "=r"(v));
    return v;
}

static inline void write_ttbr0(uint32_t v)
{
    __asm__ volatile("mcr p15, 0, %0, c2, c0, 0" : : "r"(v));
}

static inline void invalidate_tlbs(void)
{
    __asm__ volatile("mcr p15, 0, %0, c8, c7, 0" : : "r"(0));
}

static inline void dsb_isb(void)
{
    __asm__ volatile("dsb\nisb" : : : "memory");
}

static uint32_t candidate_l1_checksum(const uint32_t *l1, uint32_t count)
{
    uint32_t chk = 0;
    for (uint32_t i = 0; i < count; i++) {
        chk ^= l1[i];
    }
    return chk;
}

/* L1 section mapping helper (1MB granularity, from Stage81/82) */
static void map_l1_section_desc(uint32_t *l1, uint32_t va, uint32_t pa, uint32_t desc)
{
    uint32_t l1_index = L1_INDEX(va);
    l1[l1_index] = (pa & L1_SECTION_MASK) | desc;
}

/* DRAM: the image, its high aliases, the RAM direct map, the RAM console window,
 * and the page tables themselves (they live in .bss). */
static void map_l1_section_dram(uint32_t *l1, uint32_t va, uint32_t pa)
{
    map_l1_section_desc(l1, va, pa, L1_DESC_SECTION_DRAM);
}

/* MMIO: GIC/timer, IMEM, PS_HOLD - Strongly-ordered in every mode. */
static void map_l1_section_mmio(uint32_t *l1, uint32_t va, uint32_t pa)
{
    map_l1_section_desc(l1, va, pa, L1_DESC_SECTION_SO);
}

/* L2 table allocation helper */
static uint32_t *alloc_l2_table(void)
{
    if (stage90_l2_table_allocated_count >= STAGE90_XNU_TTE_L2_TABLE_COUNT) {
        return NULL;  /* L2 pool exhausted */
    }
    uint32_t *l2 = &stage90_candidate_l2_pool[stage90_l2_table_allocated_count * STAGE90_XNU_TTE_L2_ENTRY_COUNT];
    stage90_l2_table_allocated_count++;

    /* Zero the new L2 table */
    for (uint32_t i = 0; i < STAGE90_XNU_TTE_L2_ENTRY_COUNT; i++) {
        l2[i] = L2_DESC_FAULT;
    }
    return l2;
}

/* L1 page table pointer helper (points to L2 table) */
static void map_l1_page_table(uint32_t *l1, uint32_t va, uint32_t *l2_pa)
{
    uint32_t l1_index = L1_INDEX(va);
    uint32_t l2_base = (uint32_t)(uintptr_t)l2_pa;
    l1[l1_index] = (l2_base & L1_TABLE_MASK) | L1_DESC_PAGE_TABLE;
}

/* L2 small page mapping helper (4KB granularity).
 * Only used for the high-VA kernel image window, which is DRAM. */
static void map_l2_page(uint32_t *l2, uint32_t va, uint32_t pa)
{
    uint32_t l2_index = L2_INDEX(va);
    l2[l2_index] = (pa & L2_PAGE_MASK) | L2_DESC_PAGE_DRAM;
}

int stage90_xnu_arm_vm_init_full_pmap_run(
    struct boot_args *args,
    struct stage90_xnu_entry_stub_result *entry_result)
{
    struct stage90_xnu_arm_vm_init_full_pmap_result *r = &g_result;
    const struct stage90_xnu_arm_init_post_pe_bootstrap_result *post_pe;
    uint32_t candidate_l1_base;
    uint32_t switched;
    volatile uint32_t *identity_probe;
    volatile uint32_t *alias_probe;
    volatile uint32_t *alias_ram_console;
    volatile uint32_t *identity_ram_console;
    volatile uint32_t *alias_gicd_ctlr;
    volatile uint32_t *identity_gicd_ctlr;

    (void)entry_result;
    memset(r, 0, sizeof(*r));
    r->version = STAGE90_XNU_ARM_VM_INIT_FULL_PMAP_VERSION;
    r->size = sizeof(*r);
    r->status = STAGE90_STATUS_BASE;
    r->required_mask = STAGE90_XNU_ARM_VM_INIT_FULL_PMAP_REQUIRED_MASK;
    switched = 0;

    post_pe = stage90_xnu_arm_init_post_pe_bootstrap_result();
    if (!post_pe || post_pe->status != STAGE90_STATUS_OK) {
        r->failure_mask |= STAGE90_XNU_ARM_VM_INIT_FULL_PMAP_FAIL_SOURCE_POST_PE;
        goto finish;
    }
    r->source_post_pe_status = post_pe->status;
    r->source_post_pe_checksum = post_pe->checksum;
    r->satisfied_mask |= STAGE90_XNU_ARM_VM_INIT_FULL_PMAP_SAT_SOURCE_POST_PE_OK;

    if (!args || args->Revision != BOOT_ARGS_REVISION ||
        args->Version != BOOT_ARGS_VERSION ||
        args->physBase != STAGE90_BASE ||
        args->machineType != MACHINE_TYPE_MSM8974 ||
        args->memSize != (RAM_CONSOLE_BASE - RAM_PHYS_BASE)) {
        r->failure_mask |= STAGE90_XNU_ARM_VM_INIT_FULL_PMAP_FAIL_BOOT_ARGS;
        goto finish;
    }
    r->boot_args_ptr = (uint32_t)(uintptr_t)args;
    r->boot_args_valid = 1;
    r->memory_size = args->memSize;
    r->phys_base = args->physBase;
    r->satisfied_mask |= STAGE90_XNU_ARM_VM_INIT_FULL_PMAP_SAT_BOOT_ARGS_VALID;

    candidate_l1_base = (uint32_t)(uintptr_t)stage90_candidate_l1;
    if ((candidate_l1_base & 0x3fff) != 0) {
        r->failure_mask |= STAGE90_XNU_ARM_VM_INIT_FULL_PMAP_FAIL_CANDIDATE_L1_ALIGN;
        goto finish;
    }
    r->candidate_l1_base = candidate_l1_base;
    r->satisfied_mask |= STAGE90_XNU_ARM_VM_INIT_FULL_PMAP_SAT_CANDIDATE_L1_VALID;

    /* Build complete L1+L2 candidate pmap */
    memset(stage90_candidate_l1, 0, sizeof(stage90_candidate_l1));
    memset(stage90_candidate_l2_pool, 0, sizeof(stage90_candidate_l2_pool));
    stage90_l2_table_allocated_count = 0;

    /*
     * The image's own extent, used by three of the phases below: Phase 1 (the low identity map),
     * Phase 2 (the high page-mapped window) and Phase 3 (which has to stay out of Phase 2's way).
     * Derived here rather than three times, because the three have to agree - the failure that
     * produced this comment was two of them disagreeing.
     */
    const uint32_t image_end = (uint32_t)(uintptr_t)__stage90_image_end;
    uint32_t image_windows = (image_end + L1_SECTION_SIZE - 1u) / L1_SECTION_SIZE;

    if (image_windows == 0u) {
        image_windows = 1u;
    }

    /*
     * Phase 1: Low identity mappings (1MB sections, for current PC safety).
     *
     * A loop over the payload's image, not two named sections - the same correction Phase 5
     * already carries, needed here for the second time and for the same reason. The high-VA data
     * check further down writes and reads `stage90_full_pmap_probe_word` through *both* its own
     * address and `STAGE90_VIRT_BASE +` that address, so the identity half has to be mapped by
     * these sections while the candidate table is installed. Two sections cover PA [0, 2 MB):
     * that was enough while the word sat at 0x001fc0b4, and linking `osfmk_kern_host.o` into the
     * entry image grew the payload's embedded image, moved the word to 0x002000b4 and put it one
     * section past the last one. The check then reported `HIGH_VA_DATA`, which names the high-VA
     * half of the comparison - the half that was working - and says nothing about the identity
     * half that had just been left unmapped.
     */
    for (uint32_t ident_off = 0u; ident_off < image_end; ident_off += L1_SECTION_SIZE) {
        map_l1_section_dram(stage90_candidate_l1, ident_off, ident_off);
    }

    /*
     * Phase 2: High kernel image mapping via L2 pages (4KB granularity).
     *
     * The window is derived from the image, not fixed at 1 MB, and that is a correction rather
     * than a preference. The check further down writes and reads `stage90_full_pmap_probe_word`
     * through `STAGE90_VIRT_BASE + its physical address`, so the window has to contain that word:
     * with 256 pages it covered PA [0, 1 MB) and the word sat at 0x000EC0B4, which is why this
     * passed for months. Embedding a larger XNU entry image moved a .bss variable to 0x001000B4 -
     * 148 bytes past the end of the window - and the read came back through the L1 slot for
     * 0x80100000, which maps PA 0x80100000, so the check reported HIGH_VA_DATA and nothing said
     * the mapping was simply too small.
     *
     * `__stage90_image_end` is the same value `boot_args.c` uses for topOfKernelData, so the
     * window follows the payload's own layout: one 1 MB L2 table per megabyte of image, from
     * PA 0 upward, identity-mapped at STAGE90_VIRT_BASE. The pool holds 128 tables, so this
     * stops being viable at a 128 MB payload, which is far past anything the payload can be.
     */
    {
        uint32_t windows = image_windows;

        for (uint32_t w = 0u; w < windows; w++) {
            uint32_t base = w * L1_SECTION_SIZE;
            uint32_t *l2_kernel = alloc_l2_table();

            if (!l2_kernel) {
                r->failure_mask |= STAGE90_XNU_ARM_VM_INIT_FULL_PMAP_FAIL_L2_ALLOC;
                goto finish;
            }
            map_l1_page_table(stage90_candidate_l1, STAGE90_VIRT_BASE + base, l2_kernel);
            for (uint32_t page = 0u; page < STAGE90_XNU_TTE_L2_ENTRY_COUNT; page++) {
                uint32_t off = base + (page * L2_PAGE_SIZE);
                map_l2_page(l2_kernel, STAGE90_VIRT_BASE + off, off);
            }
        }
        xnu_log_kv32("stage90_xnu_arm_vm_init_full_pmap_l2_image_end", image_end);
        xnu_log_kv32("stage90_xnu_arm_vm_init_full_pmap_l2_image_windows", windows);
    }

    /*
     * Phase 3: Physical RAM direct map (1MB sections for efficiency)
     * Map 256MB starting at VA/PA 0x80200000 (example, cancro has ~1.5GB but we map subset)
     *
     * **Except where Phase 2 already claimed the L1 slot.** Phase 2 page-maps the image at
     * `STAGE90_VIRT_BASE + off`, one L1 slot per megabyte of image, and this phase starts at
     * `STAGE90_VIRT_BASE + 0x200000` and writes *sections* - so from the moment the image's
     * window reaches 2 MB the two are writing the same L1 entries, and this phase, running later,
     * wins. The high-VA data check then reads `STAGE90_VIRT_BASE + &stage90_full_pmap_probe_word`
     * and gets PA 0x802000b4 instead of PA 0x002000b4, which is what `HIGH_VA_DATA` reported for a
     * build whose only relevant change was that the probe word crossed 0x00200000. The image's
     * window is the mapping the check exists to test, so the direct map yields to it here.
     */
    for (uint32_t offset = 0; offset < (256 * 1024 * 1024); offset += L1_SECTION_SIZE) {
        uint32_t va = 0x80200000u + offset;
        uint32_t pa = 0x80200000u + offset;

        if (va < STAGE90_VIRT_BASE + (image_windows * L1_SECTION_SIZE)) {
            continue;
        }
        map_l1_section_dram(stage90_candidate_l1, va, pa);
    }

    /* Phase 4: Device MMIO (1MB sections) */
    map_l1_section_dram(stage90_candidate_l1, 0xde500000u, RAM_CONSOLE_BASE);    /* RAM console */
    map_l1_section_dram(stage90_candidate_l1, 0xde600000u, RAM_CONSOLE_BASE + L1_SECTION_SIZE);  /* +1MB */
    map_l1_section_mmio(stage90_candidate_l1, 0xf9000000u, 0xf9000000u);         /* GIC */
    /* **NOT the IMEM, and this comment said it was until 681.** This line maps `0xfa000000`; the
     * project's `MSM_IMEM_BASE_PHYS` is `0x0fa00000` (`stage90.h:17`), a different megabyte, and the
     * reset-reason offset `0x65c` is relative to that one (`stage90.h:18`). Two addresses, one of them
     * written down with the name of the other, is this project's most expensive defect class. What this
     * section is actually for is not established by this step: the entry image's own GIC probe maps
     * `0xf9000000` live, and this VA has no reader in either image. **681 said the Phase 5 line below
     * "is the IMEM" and that this line was the mislabelled one; 682 measured that BOTH were wrong** -
     * `0x0fa00000` is the SoC's *shared RAM*, not the IMEM, and the IMEM is not mapped here at all.
     * See the Phase 5 line's own comment. */
    map_l1_section_mmio(stage90_candidate_l1, 0xfa000000u, 0xfa000000u);
    map_l1_section_mmio(stage90_candidate_l1, 0xfc400000u, 0xfc400000u);         /* PS_HOLD */

    /* Phase 5: Legacy high-alias mappings from Stage81/82 (extend to cover full image) */
    /*
     * A loop over the payload's image, not a named section count. The comment here said "Stage84
     * image ends at ~0x111000, need at least 2MB alias"; experiment 267's image ends at 0x21a000,
     * and a pair of sections silently mapped only the first 2 MB of it. This table is the one a
     * handed-off kernel would run under, so it needs the whole image too.
     *
     * This table's limit is `STAGE90_IMAGE_ALIAS_LIMIT` - the same expression mmu.c's
     * build_identity_table() stops at, so the two tables can no longer disagree about how big the
     * window is. That is the correction 425 made: this loop's limit used to be *this file's*
     * RAM-console alias, i.e. a constant that exists for another purpose, and when the alias moved
     * from 0xc0100000 to 0xc0300000 to stop colliding with the deviceTreeP alias, the window
     * silently shrank from 4 MB to 3 MB while the other table's stayed at 4.
     */
    for (uint32_t alias_off = 0u; alias_off < (uint32_t)(uintptr_t)__stage90_image_end;
         alias_off += L1_SECTION_SIZE) {
        if (STAGE90_HIGH_ALIAS_BASE + alias_off >= STAGE90_IMAGE_ALIAS_LIMIT) {
            xnu_log_kv32("pmap_image_alias_image_end", (uint32_t)(uintptr_t)__stage90_image_end);
            xnu_log_kv32("pmap_image_alias_limit", STAGE90_IMAGE_ALIAS_LIMIT);
            xnu_log_puts("pmap image alias window is smaller than the image\n");
            break;
        }
        map_l1_section_dram(stage90_candidate_l1, STAGE90_HIGH_ALIAS_BASE + alias_off, alias_off);
    }
    /*
     * Logged unconditionally, not only when the window is too small. 425's whole difficulty was
     * that the run said `FAIL_HIGH_ALIAS` - the probe - while the cause was this window, and the
     * only line that named it was inside the branch that is taken precisely when it is too late.
     */
    xnu_log_kv32("pmap_image_alias_limit", STAGE90_IMAGE_ALIAS_LIMIT);
    xnu_log_kv32("pmap_image_alias_sections",
        ((uint32_t)(uintptr_t)__stage90_image_end + L1_SECTION_SIZE - 1u) / L1_SECTION_SIZE);
    /* RAM-console alias at its own VA, so it cannot shadow the image alias above. */
    map_l1_section_dram(stage90_candidate_l1, STAGE90_RAM_CONSOLE_ALIAS_BASE, RAM_CONSOLE_BASE);
    map_l1_section_mmio(stage90_candidate_l1, STAGE90_GIC_ALIAS_BASE, 0xf9000000u);
    /* **This is the SHARED RAM, not the IMEM - measured, and 681's comment here said the opposite.**
     * `0x0fa00000` is `MSM8974_MSM_SHARED_RAM_PHYS`
     * (`arch/arm/mach-msm/include/mach/msm_iomap-8974.h:26`), consumed as `msm_shared_ram_phys`
     * (`io.c:317`) and mapped by that kernel at the fixed virtual `MSM_SHARED_RAM_BASE`
     * (`msm_iomap.h:92`), size `SZ_2M` (`:122`) - i.e. SMEM. The phone's own device tree says the same
     * thing twice over: `soc/qcom,smem@fa00000/reg` is `0x0FA00000` for `0x200000`, and its first client
     * `soc/qcom,ipc-spinlock@fa00000/reg` is the same base and size. **The IMEM is elsewhere**:
     * `soc/qcom,msm-imem@fe805000/reg` is `0xFE805000` for `0x1000`, and Android's own restart-reason
     * word is `MSM_IMEM_BASE + 0x65C` (`arch/arm/mach-msm/restart.c:48,371`) with `MSM_IMEM_BASE` the
     * virtual `0xFA00A000` (`msm_iomap.h:72`) that `board-dt.c:82` maps onto that DT page - so
     * `0xFE80565C`. **So `RESTART_REASON` (SMEM + `0x65c`) is not Android's word, and the reset path
     * does not depend on this line.** 682 has the measurement and the consequence; the value is NOT
     * changed here, because changing it is a payload *build* and the arm in `out/` is owed a press. */
    map_l1_section_mmio(stage90_candidate_l1, 0x0fa00000u, 0x0fa00000u);

    /* Phase 6: Self-mapping (L1 and L2 pool) */
    uint32_t l2_pool_base = (uint32_t)(uintptr_t)stage90_candidate_l2_pool;
    map_l1_section_dram(stage90_candidate_l1, candidate_l1_base, candidate_l1_base);
    map_l1_section_dram(stage90_candidate_l1, l2_pool_base, l2_pool_base);

    r->candidate_l1_checksum = candidate_l1_checksum(stage90_candidate_l1, STAGE90_XNU_TTE_L1_ENTRY_COUNT);
    r->satisfied_mask |= STAGE90_XNU_ARM_VM_INIT_FULL_PMAP_SAT_CANDIDATE_L1_POPULATED;

    /* Record L2 table info */
    r->candidate_l2_pool_base = (uint32_t)(uintptr_t)stage90_candidate_l2_pool;
    r->candidate_l2_pool_checksum = candidate_l1_checksum(stage90_candidate_l2_pool,
        STAGE90_XNU_TTE_L2_TABLE_COUNT * STAGE90_XNU_TTE_L2_ENTRY_COUNT);
    r->l2_tables_allocated = stage90_l2_table_allocated_count;
    r->virt_base = STAGE90_VIRT_BASE;
    r->satisfied_mask |= STAGE90_XNU_ARM_VM_INIT_FULL_PMAP_SAT_L2_TABLES_POPULATED;

    r->original_ttbr0 = read_ttbr0();
    r->original_ttbcr = read_ttbcr();
    r->original_dacr = read_dacr();
    r->original_sctlr = read_sctlr();
    if ((r->original_sctlr & 1u) == 0u) {
        r->failure_mask |= STAGE90_XNU_ARM_VM_INIT_FULL_PMAP_FAIL_MMU_DISABLED;
        goto finish;
    }
    r->satisfied_mask |= STAGE90_XNU_ARM_VM_INIT_FULL_PMAP_SAT_CONTROL_REGS_SAVED;

    identity_probe = (volatile uint32_t *)(uintptr_t)&stage90_full_pmap_probe_word;
    alias_probe = (volatile uint32_t *)(uintptr_t)(STAGE90_HIGH_ALIAS_BASE +
        (uint32_t)(uintptr_t)&stage90_full_pmap_probe_word);
    identity_ram_console = (volatile uint32_t *)(uintptr_t)RAM_CONSOLE_BASE;
    alias_ram_console = (volatile uint32_t *)(uintptr_t)STAGE90_RAM_CONSOLE_ALIAS_BASE;
    identity_gicd_ctlr = (volatile uint32_t *)(uintptr_t)PE_state_stage90.gicDistributorBase;
    alias_gicd_ctlr = (volatile uint32_t *)(uintptr_t)(STAGE90_GIC_ALIAS_BASE + (PE_state_stage90.gicDistributorBase - 0xf9000000u));

    dsb_isb();
    /*
     * Publish both tables before TTBR0 points at them. The MMU's table walk does not read the
     * D-cache, so entries still sitting in dirty lines would be invisible to it and the
     * translations this window verifies would come from stale memory instead. The L2 pool is
     * cleaned whole rather than per allocated table because it is small and one range is
     * obviously complete.
     */
    cache_clean_dcache_range(candidate_l1_base,
                             STAGE90_XNU_TTE_L1_ENTRY_COUNT * sizeof(uint32_t));
    cache_clean_dcache_range((uint32_t)(uintptr_t)stage90_candidate_l2_pool,
                             sizeof(stage90_candidate_l2_pool));
    write_ttbr0(candidate_l1_base);
    r->ttbr0_write_count++;
    switched = 1;
    invalidate_tlbs();
    r->tlb_invalidate_count++;
    dsb_isb();

    r->live_ttbr0 = read_ttbr0();
    r->live_ttbcr = read_ttbcr();
    r->live_dacr = read_dacr();
    r->live_sctlr = read_sctlr();
    r->live_pmap_installed = 1;
    r->satisfied_mask |= STAGE90_XNU_ARM_VM_INIT_FULL_PMAP_SAT_LIVE_PMAP_INSTALLED;

    if ((r->live_ttbr0 & 0xffffc000u) != candidate_l1_base) {
        r->failure_mask |= STAGE90_XNU_ARM_VM_INIT_FULL_PMAP_FAIL_TTBR0_MISMATCH;
    }
    if (r->live_ttbcr != r->original_ttbcr || r->live_dacr != r->original_dacr) {
        r->failure_mask |= STAGE90_XNU_ARM_VM_INIT_FULL_PMAP_FAIL_CONTROL_REG_CHANGED;
    }
    if (r->failure_mask == 0) {
        r->satisfied_mask |= STAGE90_XNU_ARM_VM_INIT_FULL_PMAP_SAT_LIVE_PMAP_VERIFIED;
        r->live_pmap_verified = 1;
    }

    *identity_probe = 0x11223344u;
    if (*alias_probe == 0x11223344u) {
        r->high_alias_verified = 1;
        r->satisfied_mask |= STAGE90_XNU_ARM_VM_INIT_FULL_PMAP_SAT_HIGH_ALIAS_VERIFIED;
    } else {
        r->failure_mask |= STAGE90_XNU_ARM_VM_INIT_FULL_PMAP_FAIL_HIGH_ALIAS;
    }

    if (*alias_ram_console == RAM_CONSOLE_SIG && *alias_ram_console == *identity_ram_console) {
        r->ram_console_verified = 1;
        r->satisfied_mask |= STAGE90_XNU_ARM_VM_INIT_FULL_PMAP_SAT_RAM_CONSOLE_VERIFIED;
    } else {
        r->failure_mask |= STAGE90_XNU_ARM_VM_INIT_FULL_PMAP_FAIL_RAM_CONSOLE;
    }

    if ((*alias_gicd_ctlr & 1u) != 0u && *alias_gicd_ctlr == *identity_gicd_ctlr) {
        r->gic_verified = 1;
        r->satisfied_mask |= STAGE90_XNU_ARM_VM_INIT_FULL_PMAP_SAT_GIC_VERIFIED;
    } else {
        r->failure_mask |= STAGE90_XNU_ARM_VM_INIT_FULL_PMAP_FAIL_GIC;
    }

    /* Stage84 NEW: High-virtual data access verification via L2 pages */
    /* Test writing/reading through high-virtual address (virtBase + offset).
     * The candidate L2 maps VA 0x80000000+N -> PA 0x00000000+N (identity offset),
     * so the high-VA for a physical address X is STAGE90_VIRT_BASE + X. */
    volatile uint32_t *highva_probe_identity = (volatile uint32_t *)(uintptr_t)&stage90_full_pmap_probe_word;
    volatile uint32_t *highva_probe = (volatile uint32_t *)(uintptr_t)(
        STAGE90_VIRT_BASE + (uint32_t)(uintptr_t)&stage90_full_pmap_probe_word);

    *highva_probe_identity = 0xaabbccdd;
    if (*highva_probe == 0xaabbccdd && *highva_probe == *highva_probe_identity) {
        r->high_va_data_verified = 1;
        r->satisfied_mask |= STAGE90_XNU_ARM_VM_INIT_FULL_PMAP_SAT_HIGH_VA_DATA_VERIFIED;
    } else {
        r->failure_mask |= STAGE90_XNU_ARM_VM_INIT_FULL_PMAP_FAIL_HIGH_VA_DATA;
    }

    dsb_isb();
    write_ttbr0(r->original_ttbr0);
    r->ttbr0_write_count++;
    invalidate_tlbs();
    r->tlb_invalidate_count++;
    dsb_isb();
    switched = 0;

    r->restored_ttbr0 = read_ttbr0();
    r->restored_ttbcr = read_ttbcr();
    r->restored_dacr = read_dacr();
    r->restored_sctlr = read_sctlr();
    r->live_pmap_restored = 1;
    r->satisfied_mask |= STAGE90_XNU_ARM_VM_INIT_FULL_PMAP_SAT_LIVE_PMAP_RESTORED;

    if ((r->restored_ttbr0 & 0xffffc000u) == (r->original_ttbr0 & 0xffffc000u) &&
        r->restored_ttbcr == r->original_ttbcr &&
        r->restored_dacr == r->original_dacr &&
        r->restored_sctlr == r->original_sctlr) {
        r->satisfied_mask |= STAGE90_XNU_ARM_VM_INIT_FULL_PMAP_SAT_ORIGINAL_RESTORED;
    } else {
        r->failure_mask |= STAGE90_XNU_ARM_VM_INIT_FULL_PMAP_FAIL_RESTORE;
    }

finish:
    if (switched) {
        dsb_isb();
        write_ttbr0(r->original_ttbr0);
        r->ttbr0_write_count++;
        invalidate_tlbs();
        r->tlb_invalidate_count++;
        dsb_isb();
        r->restored_ttbr0 = read_ttbr0();
        r->restored_ttbcr = read_ttbcr();
        r->restored_dacr = read_dacr();
        r->restored_sctlr = read_sctlr();
        r->live_pmap_restored = 1;
    }

    r->public_arm_vm_init_executed = 0;
    r->public_pmap_runtime_executed = 0;
    r->persistent_write_attempted = 0;
    r->satisfied_mask |= STAGE90_XNU_ARM_VM_INIT_FULL_PMAP_SAT_NO_PUBLIC_ARM_VM_INIT |
                         STAGE90_XNU_ARM_VM_INIT_FULL_PMAP_SAT_NO_PUBLIC_PMAP_RUNTIME |
                         STAGE90_XNU_ARM_VM_INIT_FULL_PMAP_SAT_NO_PERSISTENT_WRITE;

    if ((r->satisfied_mask | STAGE90_XNU_ARM_VM_INIT_FULL_PMAP_SAT_SAFETY_BOUNDARY) ==
            r->required_mask &&
        r->failure_mask == 0) {
        r->safety_boundary_preserved = 1;
        r->satisfied_mask |= STAGE90_XNU_ARM_VM_INIT_FULL_PMAP_SAT_SAFETY_BOUNDARY;
    } else {
        r->failure_mask |= STAGE90_XNU_ARM_VM_INIT_FULL_PMAP_FAIL_SAFETY_BOUNDARY;
    }

    r->status = (r->satisfied_mask == r->required_mask && r->failure_mask == 0) ?
                STAGE90_STATUS_OK : STAGE90_STATUS_FAIL(r->failure_mask);
    r->checksum = stage90_xnu_arm_vm_init_full_pmap_checksum(r);

    stage90_xnu_arm_vm_init_full_pmap_log(r);
    return (r->status == STAGE90_STATUS_OK) ? 1 : 0;
}

const struct stage90_xnu_arm_vm_init_full_pmap_result *
stage90_xnu_arm_vm_init_full_pmap_result(void)
{
    return &g_result;
}
