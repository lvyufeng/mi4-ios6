# Experiment 196 — The payload's whole-cache flush was reading the L2's geometry and building the operand with the wrong shift, so it never selected a way

Date: 2026-09-18
Build switches: `STAGE90_ENTRY_REAL_ARM_INIT=1` (entry image), `STAGE90_XNU_ENTRY=1` (payload);
gate flag `--allow-xnu-entry`. All other switches at their defaults, which are exp-159's:
`HANDOFF_MODE = HARD_SKIP`, `PMAP_ATTR_MODE = SO_ONLY`, `CACHE_MODE = NONE`
Device: Xiaomi Mi 4 LTE `cancro`, serial `4a2fe00b`, non-persistent `fastboot boot`
Capture: `/tmp/cancro-last_kmsg.txt`

## The result

```
 xnu_entry_kv_written=0x000001cc
 xnu_entry_kv_in_dram=0x000001cc
 xnu_entry_csselr_before=0x00000002
 xnu_entry_ccsidr_before=0xf0ffe03b
 xnu_entry_ccsidr_l1=0xa007e01a
...
 xnu_entry_pmb_next_paddr=0x40400000   ... xnu_entry_pmb_kernel_slide=0x80200000
 stub_hit=pmap_bootstrap
```

The run is unchanged from experiment 195's: the same stop, the same twelve values, the same
`kv_written == kv_in_dram`. That is the expected result and it is the only one this run could give,
because the function this experiment changes **has no callers** in the configuration it was run
under. What the run establishes is that the change is inert for the current mode — the payload
reaches the same place, the entry image still reaches `pmap_bootstrap`, and
`persistent_write_attempted=0x00000000` in all 25 contracts that report it. The change is a
preparation, not a fix that the device could have confirmed today.

## Why the function has no callers, and why it is worth changing anyway

`cache_ops.c`'s `cache_clean_invalidate_dcache_all()` is the payload's one whole-cache flush, and
it has exactly two call sites:

```
stages/stage90/mmu.c:5486          before the MMU and the caches are first enabled
stages/stage90/stage90_main.c:840  platform_reboot, "the path every failure ends on"
```

Both are inside `#if STAGE90_CACHE_MODE == STAGE90_CACHE_MODE_ICACHE_DCACHE`. Every experiment so
far, this one included, runs with `CACHE_MODE = NONE`, which is exp-159's default - so the calls are
preprocessed away and `grep -c 'bl 8614'` against `stage90.elf` is **0**. The function is linked
(each translation unit emits its extern functions) and never reached.

It is dead code today and live the moment the cache mode is turned on, which is where this work is
going: the high-VA windows, the Mach-O loader and the transition to XNU's own pmap all want
cacheable memory. A flush that does not flush is the worst possible thing to discover while
turning caches on, because the symptom is not a fault - it is data that is *sometimes* stale, in
the shape of a crash log with a line missing or a page table entry that a later translation
disagrees with. It is cheap to fix now and expensive to find later, so it is fixed now.

## The two defects, both measured in experiment 195

**`CCSIDR` describes whichever cache `CSSELR` points at, and this code never selected one.** The
comment said the default "is the L1 data cache". It is not, on a core XNU has run `cpu_init` on:
`do_cacheid()` (`osfmk/arm/cpuid.c:222-265`) selects L1, reads CCSIDR, then selects **L2** and
reads it again, and never selects L1 back. Experiment 195's entry image measured the consequence
directly — `CSSELR = 2` and a CCSIDR of `0xf0ffe03b`, which decodes to 4096 sets x 8 ways x 128-byte
lines = 4 MB, where this device's L1 is `0xa007e01a` = 64 sets x 4 ways x 64 bytes = 16 KB.

**The set/way operand's way bits were in the wrong place.** The operand is
`(way << way_shift) | (set << log2(line))`. The set field starting at `log2(line)` is right — XNU's
own flush increments the set by `1 << MMU_I7SET` and tests overflow at
`1 << (MMU_NSET + MMU_I7SET)` (`osfmk/arm/caches_asm.s:245-257`, `start.s:281-288`). The way is not
above the set: XNU increments it at a fixed high bit, and `osfmk/arm/proc_reg.h` gives `MMU_I7WAY`
as 30 for a 4-way cache, 31 for a 2-way one (Cyclone, Typhoon) and 29 for the L2's 8 ways — the
same rule in all three, `32 - log2(ways)`, the way right-justified at bit 31. For this device's
4-way L1 that is bit 30. The code used `log2(line) + log2(ways)` = **bit 8**, which is inside the
set field, so no way was ever selected and the flush could not flush.

Both were also in the entry image's epilogue, unconditionally, and experiment 195 is where they
bit: a run that reached its probe and produced an empty log. The payload's copy is the same code
with the same two defects and, until now, no caller.

## The fix

```c
static void cache_dcache_geometry(uint32_t *line_log2, uint32_t *ways, uint32_t *sets)
{
    __asm__ volatile ("mcr p15, 2, %0, c0, c0, 0" :: "r"(0u) : "memory");   /* level 1 data */
    __asm__ volatile ("mrc p15, 1, %0, c0, c0, 0" : "=r"(ccsidr));
    ...
}

    way_shift = 32u - cache_log2_u32(ways);
```

Two lines, and neither of them is a guess: the geometry is now the geometry of a cache the code
named, and the shift is the one XNU's own macro table encodes for three different cache
configurations.

What is *not* changed: `cache_clean_dcache_range()` and the I-cache invalidate. The range clean
takes its line size from `CTR` (`cache_line_bytes()`), which is geometry-independent and has always
been right — it is what publishes page tables, and it is why the payload's memory-management work
has been correct while the whole-cache flush was not.

## What is next: `pmap.o`, then the cache mode

The frontier is unchanged from experiment 195: `pmap_bootstrap` is `osfmk/arm/pmap.c` and the object
is `osfmk_arm_pmap.o`, 45052 bytes of text and 93 undefined references.

The cache mode is the other thing on the list, and it is now the first thing that will exercise
this fix. When `STAGE90_CACHE_MODE` moves off `NONE`, `cache_clean_invalidate_dcache_all()` gets its
first caller in this project's history, and the correct thing to do is verify it then - with the
same discipline as everything else here, on the device, before anything depends on it.

## Reproduce

```bash
(cd stages/stage90 && STAGE90_EXTRA_CFLAGS='-DSTAGE90_XNU_ENTRY=1' ./build.sh)
(cd stages/stage90 && ./run_and_capture.sh --allow-xnu-entry)
sed -n '/jumping to XNU/,$p' /tmp/cancro-last_kmsg.txt | head -20
#   ... identical to exp-195

# the function this changes has no callers under CACHE_MODE = NONE
ADDR=$(arm-none-eabi-nm out/stage90/stage90.elf | awk '$3=="cache_clean_invalidate_dcache_all"{print $1}')
echo "cache_clean_invalidate_dcache_all at 0x$ADDR"
arm-none-eabi-objdump -d out/stage90/stage90.elf | grep -cE "bl\s+$ADDR\b"        # 0
grep -n 'STAGE90_CACHE_MODE_ICACHE_DCACHE' stages/stage90/mmu.c stages/stage90/stage90_main.c

# the measurement this came from
sed -n '/jumping to XNU/,$p' /tmp/cancro-last_kmsg.txt | sed -n '4,6p'
sed -n '222,268p' external/xnu-4570.1.46/osfmk/arm/cpuid.c
sed -n '303,330p' external/xnu-4570.1.46/osfmk/arm/proc_reg.h
```

Nothing was flashed: `persistent_write_attempted=0x00000000` in all 25 contracts that report it,
and the device returned to Android on its own.
