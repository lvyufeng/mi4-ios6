# Experiment 98 — Stage90 Phase 1: the D-cache on, and Phase 1 closed

Date: 2026-09-17
Commit under test: `9552842`, plus the changes described below
Build switches: `STAGE90_PMAP_ATTR_MODE = STAGE90_PMAP_ATTR_MODE_NORMAL_WB`,
`STAGE90_CACHE_MODE = STAGE90_CACHE_MODE_ICACHE_DCACHE`; `STAGE90_HANDOFF_MODE = HARD_SKIP`;
both nets armed
Device: Xiaomi Mi 4 LTE `cancro`, serial `4a2fe00b`, non-persistent `fastboot boot`
Captures: `/tmp/kmsg-dcache1.txt` (first build), `/tmp/kmsg-dcache2.txt` (the shipped build,
after two unused helpers were removed from `cache_ops.c` — the run reported here)

Run 1 of the first build was green, and the re-run after the trim was green too, with the same
values. Both are recorded because the image that produced the evidence has to be the image that
ships.

## What was being tested

The last of Phase 1's exit criteria and the one the roadmap put the loudest warning on: **the
D-cache**. `STAGE90_CACHE_MODE = ICACHE_DCACHE` sets `SCTLR.I` and then `SCTLR.C`, the second in
its own write after the MMU is already on, and after a whole-cache clean-and-invalidate. Once it
is on, this payload — like any kernel — has three new obligations:

1. **Publishing a page table.** The MMU's table walk does not read the D-cache, so an L1/L2 entry
   that a later translation depends on must be cleaned to memory first. Cleaned, not invalidated:
   the cache may hold the only up-to-date copy.
2. **Surviving a reboot.** Anything that must be read after the machine goes down has to be out
   of the cache. `ram_console` is the thing that matters, and the roadmap's rule was to add a
   clean to its write path.
3. **Enabling the caches.** Both caches are invalidated before they are first enabled, so a line
   left valid by whatever ran before this payload cannot be used for one of our addresses.

## Obligation 2 was met a different way, and the reason is worth keeping

The roadmap said "fix `ram_console` first: add a cache-clean in the write path, and a
full clean-and-invalidate of the log region before the reboot." The implementation does
neither, deliberately:

**The ram_console window is mapped Normal-Non-cacheable even under `NORMAL_WB`.** Its PA range
(`0xde500000`–`0xde6fffff`) is disjoint from every other PA this payload maps — the attribute map
lists every mapping by PA precisely so this can be checked — so giving it a different type from
the image violates nothing. And a crash log that is *never* in the cache cannot be lost by a
reboot path that failed to clean it, which is a stronger guarantee than one that depends on every
exit remembering to clean. `log_puts()` already ends in `dsb sy; isb`, which is a complete
guarantee for a non-cacheable store and is not one for a cached store.

`platform_reboot()` also cleans and invalidates the whole D-cache, first thing, before its own log
lines. That is belt and braces rather than the mechanism: it is the path every failure ends on,
including the ones where something else is already wrong, so it does not rely on the mapping
being right either.

Obligation 1 is met by three cleans, one per place that publishes a table: `full_pmap`'s
candidate L1 and L2 pool, the TTBR0 roundtrip's recovery L1, and the exclusive probe's phase 3
(which modifies the live L1). The cleans are by MVA over a known range rather than whole-cache,
because each site knows exactly what it wrote.

Obligation 3 is in `enable_identity_mmu()`, and the order is ARM's: MMU first, caches after. The
D-cache is enabled in a second `write_sctlr` once the first has taken effect, because enabling a
data cache while translation is still off would let it hold lines for physical addresses that are
about to be translated differently.

## The run

```
mmu_entry_low          =0x0001140e      DRAM: Normal WBWA, shareable, AP=01
mmu_entry_ram_console  =0xde511c02      ram_console: Normal NON-cacheable, as designed
mmu_sctlr_after        =0x00c5587f      M=1 C=1 I=1   (was 0x00c5487a: M=0 C=0 I=0)
ttbr_rt_cache_bits_*   =0x00001004      both cache bits, unchanged across the TTBR0 roundtrip
ttbr_rt_caches_changed =0x00000000      the ladder changes nothing during its own run
kernel_entry returned success            and no line containing "failed" anywhere in the log
```

with `ram_console_verified=1`, the high-VA IRQ handler delivering timer interrupts
(`irq_count 1 → 3`), and the device back in Android unattended. The log is complete: 3883 marked
lines ending in the full `platform_reboot` sequence, against 3887 for the I-cache-only run, and
the only difference between the two is how many times the exception-handler probes fired inside
their windows. A stale or truncated log is the symptom a cache bug produces here, so that
comparison is the check that matters.

## What this does and does not establish

**Does:** the payload runs with both caches on; the descriptors, the `SCTLR` bits, the log, the
timer interrupt and all twenty-odd pmap contracts agree; and the one path that would have been
silently wrong — a cacheable crash log — is closed by construction.

**Does not:** show a speedup (no timing comparison was made, and the point was configuration, not
performance). Prove the maintenance is sufficient under *load* — this payload is small and mostly
sequential, so it exercises the three cleans but does not stress them. And it does not exercise
I-cache coherency after a code write, because nothing writes code at runtime; the loader's header
records what that path will have to do.

## Phase 1, closed

| Exit criterion | Status |
| --- | --- |
| identity and high-VA mappings with caches on | ✅ this run (`SCTLR.C` and `.I` set, all mappings verified under them) |
| `ram_console` still logging | ✅ `ram_console_verified=1`, and the log this was read from |
| timer IRQ still delivered | ✅ `irq_count 1 → 3`, `vbar_restored=1` |
| a documented attribute map | ✅ `docs/reference/pmap-attribute-map.md`, three modes, decoded not guessed |
| a passing `LDREX`/`STREX` | ✅ `experiment-96` — and it turned out never to have needed the attribute work |

## What changed in response

- **`cache_ops.c`** (new): `cache_clean_invalidate_dcache_all`, `cache_clean_dcache_range`,
  `cache_invalidate_icache_all`. Three functions, and exactly the three this payload needs; the
  fourth obligation — I-cache invalidate after a code write — has no caller and is deliberately
  absent rather than present and untested, with a note where it will matter.
- `stage90.h`: `STAGE90_CACHE_MODE_ICACHE_DCACHE`; `STAGE90_PMAP_DESC_SECTION_RAM_CONSOLE`; and
  the compile-time rule that any enabled cache needs `NORMAL_WB`.
- `mmu.c`: the ram_console windows use the non-cacheable descriptor; `ttbr_section_desc_for_pa()`
  classifies that PA the same way, so the roundtrip's recovery table cannot disagree with the live
  one about it; the D-cache enable and the table clean before the roundtrip's TTBR write.
- `xnu_arm_vm_init_full_pmap.c`: the clean of the candidate L1 and L2 pool before it is published.
- `exclusive_probe.c`: the clean of the L1 entry phase 3 modifies.
- `stage90_main.c`: `platform_reboot()` cleans and invalidates the whole D-cache first.
- `build.sh` / `preflight_boot_check.sh`: `cache_ops.c` in the source list; the cmdline token
  (`no-cache-change` / `icache-enabled` / `icache-dcache-enabled`) follows the build; and
  `--allow-dcache` with a gate section that says what to read in the log and why.
