/*
 * 911c: the Mi 4's RAM bank layout, measured from the SMEM RAM-partition table.
 *
 * ------------------------------------------------------------------------------------------------
 * The four-part shape, the same as `entry_storage.c`, `entry_gic.c` and `entry_usb.c`
 * ------------------------------------------------------------------------------------------------
 *
 *   1. **the mapping, and its refusal is the step.** `entry_mmio_section` installs the SMEM window
 *      under two VAs (below). A result of 0 means the slot at that VA already held a descriptor and
 *      this probe then dereferences **neither** - the mapping it would be reading through is not one
 *      it installed, and a read of an address whose translation it cannot vouch for is a fault, not
 *      a measurement. The four mapper numbers are published under `_smem_*` so they are never read
 *      as the GIC's or the storage block's (693's rule).
 *   2. **the heap table of contents.** `heap_info` at 0xD0, `heap_toc[512]` at 0xE0, 16 B each. The
 *      walk reads every entry's `allocated`/`offset`/`size` and publishes a census, then follows the
 *      allocated entries whose `offset` is inside the window.
 *   3. **the partition table, validated by MAGIC, not by index.** `smem_alloc` scans the TOC for a
 *      size, not an index; so this probe scans the TOC and accepts an entry whose first two words are
 *      `_SMEM_RAM_PTABLE_MAGIC_{1,2}` and whose `len` is within `1..32`. The verdict is a published
 *      reading (`_ptable_found`) so "SMEM is not here" and "SMEM is here but the table is absent" are
 *      different results.
 *   4. **the banks, by the device's OWN rule.** `parts[i].type == SYS_MEMORY (1) && size >= 256 MB`
 *      - exactly `memory_topology.c`'s `meminfo_init(SYS_MEMORY, SZ_256M)` predicate. Each qualifying
 *      partition is published as `_bankN_start`/`_bankN_size`, and their sum as `_total_bytes` - the
 *      device's real total, which is the number the goal's "3 GB" clause is about.
 *
 * ------------------------------------------------------------------------------------------------
 * Two mappings, and the low one pays for itself twice
 * ------------------------------------------------------------------------------------------------
 *
 * The entry image's section installer is `entry_section_install(va >> 20, pa, ...)` - a 1 MB section
 * keyed by the top 12 bits of the VA and **with no VA guard** (911c design). The handed-off kernel
 * runs `arm_vm_init.c`'s tables with `TTBCR.N = 2`, and on this device `TTBR0 == TTBR1`
 * (`xnu_live_ttbr0 == xnu_live_ttbr1` in the live channel), so a section installed for any 1 MB VA
 * resolves. This probe therefore maps SMEM twice:
 *
 *   - the **high alias** `0xe0000000 -> 0x0fa00000` (index `0xe00`), in the hole between the banks;
 *   - the **low identity** `0x0fa00000 -> 0x0fa00000` (index `0x0fa`), which is free in this image.
 *
 * The **alias** is the robust read: the low identity section aliases a coprime quantum, so it
 * behaves exactly as the alias - only its range is nearer the epilogue's faulting store - and if it
 * is refused (slot occupied) the alias has already mapped the same bytes. The **low identity** is
 * mapped for the second reason: `0x0fa0065c`, the epilogue's `RESTART_REASON` store target, lives in
 * this megabyte, and mapping it **un-faults that store** - the owed clean self-end
 * (`docs/experiments/experiment-745:76`). One mapping, two purposes. This file itself only *reads*
 * through either VA; the un-faulting is a side effect the epilogue collects.
 *
 * ------------------------------------------------------------------------------------------------
 * This rung MEASURES; it does not port the 3 GB
 * ------------------------------------------------------------------------------------------------
 *
 * XNU's physmap is single-span (`phystokv(a) = a - gPhysBase + gVirtBase`, `vm_param.h:196`;
 * `vm_first_phys = gPhysBase`, `pmap.c:2859`; tables sized by `atop(mem_size)`), so a low bank below
 * `0x80000000` cannot be linearly mapped from `0x80000000` without a bank/region list the pmap does
 * not have. That port is **911e**. This file's job is to produce the *numbers* 911e will port - the
 * real bank base and size - which is why it must read them off the device and not infer them.
 */
#include <stdint.h>

#include "entry_smem.h"

/*
 * The live channel and the mapper, both defined in `entry_stubs.c`, and the arm switch. **The switch
 * gates BOTH the body and the records**, the way `STAGE90_XNU_USB_PROBE` gates `entry_usb.c`: with it
 * 0 the whole file is a one-line `void entry_smem_probe(void) { }`, so the call site in
 * `entry_trace.c` is unconditional and the ON and OFF arms are the same source file. The declarations
 * are **outside** the `#if` (692's repair in `entry_gic.c`): they are not code, so moving them out
 * changes no object byte.
 */
#ifndef STAGE90_XNU_SMEM_PROBE
#define STAGE90_XNU_SMEM_PROBE 0
#endif

extern void entry_live_write(const char *key, uint32_t value);
extern uint32_t g_live_state;
/* 484's four numbers, read by `entry_mmio_section` at the install - see `entry_gic.c`. */
extern uint32_t g_live_mmio_l1;
extern uint32_t g_live_mmio_l1_moved;
extern uint32_t g_live_mmio_ttbr0;
extern uint32_t g_live_mmio_ttbr1;
extern uint32_t entry_mmio_section(uint32_t va, uint32_t pa, uint32_t *slot_before_out,
                                   uint32_t *desc_out);

/* Published unconditionally (the header declares them `extern`), so a non-SMEM build carries zeros
 * rather than an undefined symbol and a later reader can still name the cells. */
uint32_t g_stage90_smem_ptable_found = 0u;
uint32_t g_stage90_smem_total_bytes  = 0u;
uint32_t g_stage90_smem_banks        = 0u;
uint32_t g_stage90_smem_bank_start[STAGE90_SMEM_MAX_BANKS];
uint32_t g_stage90_smem_bank_size[STAGE90_SMEM_MAX_BANKS];

#if STAGE90_XNU_SMEM_PROBE

#define SMEM_LIVE(key, value) entry_live_write((key), (uint32_t)(value))

/* One `volatile` 32-bit read of Device memory, 4-aligned. Reads have no write buffer to drain, so
 * no `dsb` is needed (the same reasoning `entry_usb.c` records). Every offset passed here is either
 * a compile-time constant or one already bounded against `STAGE90_SMEM_OFF_MAX`. */
static volatile uint32_t *smem_word(uint32_t base, uint32_t off)
{
    return (volatile uint32_t *)(uintptr_t)(base + off);
}

void entry_smem_probe(void)
{
    static uint32_t g_smem_probed = 0u;   /* idempotent: one pass does the walk */
    uint32_t alias_mapped, ident_mapped, read_base;
    uint32_t slot_before, desc;
    uint32_t i, n_alloc, n_part, banks, total;
    uint32_t ptable_off, ptable_len;

    if (g_smem_probed != 0u)
        return;                            /* one pass is enough; the layout is fixed */
    g_smem_probed = 1u;

    SMEM_LIVE("xnu_live_smem_live_state", g_live_state);
    SMEM_LIVE("xnu_live_smem_phys_base", STAGE90_SMEM_PHYS_BASE);
    SMEM_LIVE("xnu_live_smem_alias_base", STAGE90_SMEM_ALIAS_BASE);
    SMEM_LIVE("xnu_live_smem_alias_section", (uint32_t)(STAGE90_SMEM_ALIAS_BASE >> 20));
    SMEM_LIVE("xnu_live_smem_ident_section", (uint32_t)(STAGE90_SMEM_PHYS_BASE >> 20));
    SMEM_LIVE("xnu_live_smem_part_stride", STAGE90_SMEM_PART_STRIDE);

    /*
     * **(1) THE MAPPINGS, BEFORE ANYTHING IS READ.** The alias first, because it is the read the
     * rest of the probe uses; then the low identity, whose failure is NOT fatal (the alias already
     * covers the bytes) but whose success is recorded because it also un-faults the epilogue's store.
     */
    alias_mapped = entry_mmio_section(STAGE90_SMEM_ALIAS_BASE, STAGE90_SMEM_PHYS_BASE,
                                      &slot_before, &desc);
    SMEM_LIVE("xnu_live_smem_alias_map", alias_mapped);
    SMEM_LIVE("xnu_live_smem_alias_slot_before", slot_before);
    SMEM_LIVE("xnu_live_smem_alias_desc", desc);
    SMEM_LIVE("xnu_live_smem_ttbr0", g_live_mmio_ttbr0);
    SMEM_LIVE("xnu_live_smem_ttbr1", g_live_mmio_ttbr1);
    SMEM_LIVE("xnu_live_smem_l1_moved", g_live_mmio_l1_moved);

    ident_mapped = entry_mmio_section(STAGE90_SMEM_PHYS_BASE, STAGE90_SMEM_PHYS_BASE,
                                      &slot_before, &desc);
    SMEM_LIVE("xnu_live_smem_ident_map", ident_mapped);
    SMEM_LIVE("xnu_live_smem_ident_slot_before", slot_before);
    SMEM_LIVE("xnu_live_smem_ident_desc", desc);

    /* The **alias** is the mapping the walk uses. If it was refused, the low identity may still
     * carry the bytes (both alias the same PA); prefer whichever installed. If neither did, the
     * probe dereferences nothing and says so. */
    if (alias_mapped != 0u) {
        read_base = STAGE90_SMEM_ALIAS_BASE;
    } else if (ident_mapped != 0u) {
        read_base = STAGE90_SMEM_PHYS_BASE;
    } else {
        SMEM_LIVE("xnu_live_smem_mapped", 0u);
        SMEM_LIVE("xnu_live_smem_ptable_found", 0u);
        SMEM_LIVE("xnu_live_smem_banks", 0u);
        SMEM_LIVE("xnu_live_smem_total_bytes", 0u);
        return;                            /* neither section installed: no read is a read */
    }
    SMEM_LIVE("xnu_live_smem_mapped", 1u);
    SMEM_LIVE("xnu_live_smem_read_base", read_base);

    /*
     * **(2) THE HEAP TOC, AND THE CENSUS.** `heap_info`'s first word is `initialized`; a zero there
     * is not an error to stop on (some bootloaders leave it zero) but it is worth publishing, because
     * `free_offset`/`heap_remaining` next to it are only meaningful if the heap was initialised.
     */
    SMEM_LIVE("xnu_live_smem_heap_initialized",
              *smem_word(read_base, STAGE90_SMEM_HEAP_INFO_OFF + 0x0u));
    SMEM_LIVE("xnu_live_smem_heap_free_offset",
              *smem_word(read_base, STAGE90_SMEM_HEAP_INFO_OFF + 0x4u));
    SMEM_LIVE("xnu_live_smem_heap_remaining",
              *smem_word(read_base, STAGE90_SMEM_HEAP_INFO_OFF + 0x8u));

    /*
     * **(3) THE SCAN.** Every allocated TOC entry whose size could hold a partition table is a
     * candidate. A candidate is accepted only if its first two words are the magic pair AND its `len`
     * is within `1..STAGE90_SMEM_PTABLE_PART_N` - two independent checks, so a coincidental first
     * word with a wild `len` is not read as the table.
     */
    n_alloc = 0u;
    n_part = 0u;
    ptable_off = 0u;
    ptable_len = 0u;
    for (i = 0u; i < STAGE90_SMEM_HEAP_TOC_N; i++) {
        uint32_t e = STAGE90_SMEM_HEAP_TOC_OFF + i * STAGE90_SMEM_TOC_STRIDE;
        uint32_t allocated = *smem_word(read_base, e + STAGE90_SMEM_TOC_ALLOCATED);
        uint32_t off, sz;
        if (allocated == 0u)
            continue;
        n_alloc++;
        if (ptable_off != 0u)
            continue;                      /* already found it; just keep counting */
        off = *smem_word(read_base, e + STAGE90_SMEM_TOC_OFFSET);
        sz  = *smem_word(read_base, e + STAGE90_SMEM_TOC_SIZE);
        /* The offset is bounded BEFORE the magic read, so a wild offset is skipped, not
         * dereferenced ([[mi4-a-device-address-can-be-right-and-undereferenceable]]). The table
         * needs at least magic(8) + len(4) + one partition(56) = 0x10 + 56 bytes. */
        if (off < STAGE90_SMEM_OFF_MAX &&
            sz >= (STAGE90_SMEM_PTABLE_PARTS_OFF + STAGE90_SMEM_PART_STRIDE)) {
            uint32_t m0 = *smem_word(read_base, off + 0x0u);
            uint32_t m1 = *smem_word(read_base, off + 0x4u);
            uint32_t len = *smem_word(read_base, off + STAGE90_SMEM_PTABLE_LEN_OFF);
            if (m0 == STAGE90_SMEM_PTABLE_MAGIC0 &&
                m1 == STAGE90_SMEM_PTABLE_MAGIC1 &&
                len >= 1u && len <= STAGE90_SMEM_PTABLE_PART_N) {
                ptable_off = off;
                ptable_len = len;
                SMEM_LIVE("xnu_live_smem_ptable_toc_slot", i);
                SMEM_LIVE("xnu_live_smem_ptable_toc_size", sz);
            }
        }
    }
    SMEM_LIVE("xnu_live_smem_toc_allocated", n_alloc);

    if (ptable_off == 0u) {
        /* SMEM is mapped but no partition table was found. This is a real reading: publish the
         * verdict and the zero total, and stop - the walk has nothing to walk. */
        SMEM_LIVE("xnu_live_smem_ptable_found", 0u);
        SMEM_LIVE("xnu_live_smem_ptable_off", 0u);
        SMEM_LIVE("xnu_live_smem_part_seen", 0u);
        SMEM_LIVE("xnu_live_smem_banks", 0u);
        SMEM_LIVE("xnu_live_smem_total_bytes", 0u);
        g_stage90_smem_ptable_found = 0u;
        return;
    }

    SMEM_LIVE("xnu_live_smem_ptable_found", 1u);
    SMEM_LIVE("xnu_live_smem_ptable_off", ptable_off);
    SMEM_LIVE("xnu_live_smem_ptable_len", ptable_len);
    SMEM_LIVE("xnu_live_smem_ptable_version", *smem_word(read_base, ptable_off + 0x4u));
    g_stage90_smem_ptable_found = 1u;

    /*
     * **(4) THE BANKS.** The device's own rule: `type == SYS_MEMORY (1) && size >= 256 MB`
     * (`memory_topology.c`'s `meminfo_init(SYS_MEMORY, SZ_256M)`). Every OTHER partition is counted
     * (`_part_seen`) but not published as a bank - a reader that wants the modem/IMEM carve-outs has
     * the count, and a bank list that included them would not sum to the RAM total.
     */
    banks = 0u;
    total = 0u;
    for (i = 0u; i < ptable_len && i < STAGE90_SMEM_PTABLE_PART_N; i++) {
        uint32_t pbase = ptable_off + STAGE90_SMEM_PTABLE_PARTS_OFF + i * STAGE90_SMEM_PART_STRIDE;
        uint32_t start = *smem_word(read_base, pbase + STAGE90_SMEM_PART_START_OFF);
        uint32_t size  = *smem_word(read_base, pbase + STAGE90_SMEM_PART_SIZE_OFF);
        uint32_t type  = *smem_word(read_base, pbase + STAGE90_SMEM_PART_TYPE_OFF);
        n_part++;
        if (type == STAGE90_SMEM_SYS_MEMORY && size >= STAGE90_SMEM_MIN_BANK_SIZE) {
            if (banks < STAGE90_SMEM_MAX_BANKS) {
                g_stage90_smem_bank_start[banks] = start;
                g_stage90_smem_bank_size[banks]  = size;
            }
            banks++;
            /* The total counts EVERY qualifying bank even past MAX_BANKS, so a table with more than
             * MAX_BANKS system partitions reports the true total (the count is capped, the sum is
             * not). */
            total += size;
        }
    }
    /* The per-bank cells are published explicitly, one name per index, so the log never has two
     * banks at one key and a reader adds nothing up by hand (`_total_bytes` is already the sum).
     * A bank index past MAX_BANKS is counted in `_banks`/`_total_bytes` but has no cell - and since
     * the Mi 4 has two banks, that branch is headroom, not a path this device takes. */
    SMEM_LIVE("xnu_live_smem_bank0_start", g_stage90_smem_bank_start[0]);
    SMEM_LIVE("xnu_live_smem_bank0_size",  g_stage90_smem_bank_size[0]);
    SMEM_LIVE("xnu_live_smem_bank1_start", g_stage90_smem_bank_start[1]);
    SMEM_LIVE("xnu_live_smem_bank1_size",  g_stage90_smem_bank_size[1]);
    SMEM_LIVE("xnu_live_smem_bank2_start", g_stage90_smem_bank_start[2]);
    SMEM_LIVE("xnu_live_smem_bank2_size",  g_stage90_smem_bank_size[2]);
    SMEM_LIVE("xnu_live_smem_bank3_start", g_stage90_smem_bank_start[3]);
    SMEM_LIVE("xnu_live_smem_bank3_size",  g_stage90_smem_bank_size[3]);
    SMEM_LIVE("xnu_live_smem_part_seen", n_part);
    SMEM_LIVE("xnu_live_smem_banks", banks);
    SMEM_LIVE("xnu_live_smem_total_bytes", total);
    g_stage90_smem_banks = banks;
    g_stage90_smem_total_bytes = total;
}

#else  /* !STAGE90_XNU_SMEM_PROBE */

void entry_smem_probe(void) { }

#endif /* STAGE90_XNU_SMEM_PROBE */