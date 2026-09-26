/*
 * Cache maintenance, in one place.
 *
 * Phase 1 turns caches on, and that is the point at which this project stops being able to rely
 * on "everything is immediately visible". The obligations that come with it are small but easy
 * to get subtly wrong, so they live here rather than being open-coded:
 *
 *   - publishing a page table - a write to an L1/L2 entry that a later translation depends on -
 *     needs the entry in memory, because the MMU's table walk does not read the D-cache;
 *   - both caches must be invalidated before they are first enabled, so a line left valid by
 *     whatever ran before this payload cannot be used for one of our addresses;
 *   - anything that must outlive a reboot needs to be out of the D-cache before the reboot.
 *     `ram_console` is mapped non-cacheable under every cacheable attribute mode for exactly
 *     that reason (see STAGE90_PMAP_DESC_SECTION_RAM_CONSOLE), so a crash log never depends on
 *     maintenance happening.
 *
 * Three functions, and they are the three things this payload actually needs. A fourth
 * obligation exists and has no caller yet, so it is deliberately absent rather than present and
 * untested: once something writes *code* at runtime - the Mach-O loader, when its destination
 * becomes mapped - that path must invalidate the I-cache for the region it wrote. The loader's
 * header says so at the point where it would matter.
 *
 * The by-set/way operation reads its geometry from CCSIDR rather than assuming a line size or an
 * associativity, because assuming either is the kind of thing that works on the first board and
 * silently disagrees with the next one. It also *selects* the cache before reading - CCSIDR
 * describes whichever level CSSELR points at, and XNU leaves that pointing at the L2.
 */

#include "stage90.h"

static inline void cache_dsb_isb(void)
{
    __asm__ volatile ("dsb sy\n\tisb" ::: "memory");
}

/* CTR - cache type register. bits[3:0] = log2(words per smallest data cache line). */
static uint32_t cache_line_bytes(void)
{
    uint32_t ctr;
    __asm__ volatile ("mrc p15, 0, %0, c0, c0, 1" : "=r"(ctr));
    return 4u << (ctr & 0xfu);
}

/*
 * CCSIDR for the level CSSELR selects, and CSSELR is therefore selected rather than assumed.
 *
 * It does not default to the L1 data cache, which is what this function used to say. On a core XNU
 * has run `cpu_init` on, `do_cacheid()` (`osfmk/arm/cpuid.c:222-265`) selects L1, reads CCSIDR, and
 * then selects **L2** and reads it again, and never selects L1 back - so anything of ours that runs
 * after XNU inherits CSSELR = 2 and reads the L2's geometry out of it. That is measured, not
 * inferred: experiment 195's entry image read `CSSELR = 2` and this device's L2 description on this
 * device, where the L1 is 64 sets, 4 ways, 64 bytes (16 KB).
 *
 * **The L2's own numbers are corrected by 676, and the correction is the defect this file is
 * named in.** The sentence above used to give that description as "a 4096-set, 8-way, 128-byte
 * description (4 MB)" - and 4096 is not a decode of any register word. The measured word is
 * `0xf0ffe03b`: `LineSize` 3, `Associativity` 7 and `NumSets` 2047, i.e. **2048 sets of 8 ways of
 * 128-byte lines, 2 MiB**, and the nearest misread of the word (bits[27:12]) gives 4095, not 4096.
 * 4096 is the **compile-time** L2 geometry (`__ARM_L2CACHE_SIZE_LOG__=21` with `L2_CLINE=6` is 4096
 * sets of 64-byte lines) - one value with two definitions, one file apart, which is why this
 * comment and `entry_stubs.c`'s were wrong together. XNU's own source corroborates the register:
 * `cpuid.c:275` says "capri has a 2MB L2 cache" and this device is capri. The geometry this
 * function *returns* was never affected - it writes CSSELR, reads CCSIDR and decodes the fields
 * below - so this is a comment about what a reader would have swept with, and the reader who
 * matters is an arm built from the comment's numbers rather than from the register's.
 */
static void cache_dcache_geometry(uint32_t *line_log2, uint32_t *ways, uint32_t *sets)
{
    uint32_t ccsidr;

    __asm__ volatile ("mcr p15, 2, %0, c0, c0, 0" :: "r"(0u) : "memory");   /* level 1 data */
    __asm__ volatile ("mrc p15, 1, %0, c0, c0, 0" : "=r"(ccsidr));

    /* LineSize is log2(words per line) - 2, so bytes per line is 2^(LineSize + 4). */
    *line_log2 = (ccsidr & 0x7u) + 4u;
    *ways = ((ccsidr >> 3) & 0x3ffu) + 1u;
    *sets = ((ccsidr >> 13) & 0x7fffu) + 1u;
}

static uint32_t cache_log2_u32(uint32_t v)
{
    uint32_t n = 0u;
    while (v > 1u) {
        v >>= 1;
        n++;
    }
    return n;
}

/*
 * DCCISW - clean and invalidate the whole D-cache by set and way.
 *
 * "Clean and invalidate" rather than plain invalidate on purpose: an invalidate would discard
 * dirty lines, and before the D-cache is first enabled this runs against a cache that may still
 * hold lines from whatever ran before us. Cleaning first cannot lose data; invalidating first can.
 *
 * Also used by platform_reboot, which is the one path that must not assume anything else worked.
 *
 * The operand's layout is XNU's, not an obvious one. The set field starts at the line size -
 * `1 << MMU_I7SET` in `osfmk/arm/caches_asm.s:245-257` increments exactly that - and the way is
 * right-justified at bit 31, which is what `MMU_I7WAY` is in `osfmk/arm/proc_reg.h`: 30 for a
 * 4-way cache, 31 for 2, 29 for the L2's 8 ways. This loop used to put the way at
 * `line_log2 + log2(ways)` - bit 8 for this device's L1 - which is inside the set field, so it
 * never selected a way and the flush was not a flush. Experiment 195 is where that showed.
 */
void cache_clean_invalidate_dcache_all(void)
{
    uint32_t line_log2, ways, sets, way_shift, way, set;

    cache_dcache_geometry(&line_log2, &ways, &sets);
    way_shift = 32u - cache_log2_u32(ways);

    for (way = 0u; way < ways; way++) {
        for (set = 0u; set < sets; set++) {
            uint32_t val = (way << way_shift) | (set << line_log2);
            __asm__ volatile ("mcr p15, 0, %0, c7, c14, 2" :: "r"(val) : "memory");
        }
    }
    cache_dsb_isb();
}

/* DCCMVAC over a range - clean every line the range touches, so it is in memory. Used to publish
 * page tables: a clean, not an invalidate, because the cache may hold the only up-to-date copy. */
void cache_clean_dcache_range(uint32_t va, uint32_t bytes)
{
    const uint32_t line = cache_line_bytes();
    uint32_t addr = va & ~(line - 1u);
    const uint32_t end = va + bytes;

    for (; addr < end; addr += line) {
        __asm__ volatile ("mcr p15, 0, %0, c7, c10, 1" :: "r"(addr) : "memory");
    }
    cache_dsb_isb();
}

/* ICIALLU - invalidate the whole I-cache, before it is first enabled. */
void cache_invalidate_icache_all(void)
{
    uint32_t zero = 0;
    __asm__ volatile ("mcr p15, 0, %0, c7, c5, 0" :: "r"(zero) : "memory");
    cache_dsb_isb();
}
