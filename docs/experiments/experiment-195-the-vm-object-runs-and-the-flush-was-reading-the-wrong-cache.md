# Experiment 195 — The VM's own object runs, `pmap_bootstrap` is reached with every predicted number, and the flush that carries the evidence was reading the wrong cache

Date: 2026-09-18
Build switches: `STAGE90_ENTRY_REAL_ARM_INIT=1` (entry image), `STAGE90_XNU_ENTRY=1` (payload);
gate flag `--allow-xnu-entry`. All other switches at their defaults, which are exp-159's:
`HANDOFF_MODE = HARD_SKIP`, `PMAP_ATTR_MODE = SO_ONLY`, `CACHE_MODE = NONE`
Device: Xiaomi Mi 4 LTE `cancro`, serial `4a2fe00b`, non-persistent `fastboot boot`
Capture: `/tmp/cancro-last_kmsg.txt`

## The result

```
MI4IOS6_STAGE90_XNU stage90 xnu_entry: jumping to XNU's _start

MI4IOS6_STAGE90_XNU real XNU entry: real arm_init reached a symbol this image does not provide
 xnu_entry_kv_written=0x000001cc
 xnu_entry_kv_in_dram=0x000001cc
 xnu_entry_csselr_before=0x00000002
 xnu_entry_ccsidr_before=0xf0ffe03b
 xnu_entry_ccsidr_l1=0xa007e01a
MI4IOS6_STAGE90_XNU real XNU entry xnu_entry_pmb_next_paddr=0x40400000
 xnu_entry_pmb_cpsr=0x60000093
 xnu_entry_pmb_cpu_ttep=0x00404000
 xnu_entry_pmb_avail_start=0x0040a000
 xnu_entry_pmb_avail_end=0x00a00000
 xnu_entry_pmb_gvirtbase=0x00200000
 xnu_entry_pmb_gphyssize=0x00800000
 xnu_entry_pmb_mem_size=0x00800000
 xnu_entry_pmb_static_mem_end=0x00a00000
 xnu_entry_pmb_end_kern=0x00248000
 xnu_entry_pmb_sane_size=0x005f6000
 xnu_entry_pmb_kernel_slide=0x80200000
 stub_hit=pmap_bootstrap

No errors detected
```

`stage90_xnu_entry_stub_no_exception=0x00000001` earlier in the log, as in every run since 189.

Every one of the twelve values was predicted before the run, and every one is exact:

| | written by | predicted | measured |
| --- | --- | --- | --- |
| `next_paddr` | `(gVirtBase + MEM_SIZE_MAX + 0x3FFFFF) & 0xFFC00000` | `0x40400000` | `0x40400000` |
| `cpu_ttep` | `args->topOfKernelData + 4 pages` | `0x00404000` | `0x00404000` |
| `avail_start` | `cpu_ttep + 6 pages` | `0x0040a000` | `0x0040a000` |
| `avail_end` | `gPhysBase + mem_size` | `0x00a00000` | `0x00a00000` |
| `gVirtBase` | `args->virtBase` | `0x00200000` | `0x00200000` |
| `gPhysSize` | `args->memSize` | `0x00800000` | `0x00800000` |
| `mem_size` | clamp of `args->memSize` | `0x00800000` | `0x00800000` |
| `static_memory_end` | `gVirtBase + mem_size` | `0x00a00000` | `0x00a00000` |
| `end_kern` | `round_page(getlastaddr())` | `0x00248000` | `0x00248000` |
| `sane_size` | `mem_size - (avail_start - gPhysBase)` | `0x005f6000` | `0x005f6000` |
| `vm_kernel_slide` | `gVirtBase - 0x80000000` | `0x80200000` | `0x80200000` |
| `CPSR` | recorded for continuity | — | `0x60000093` |

`end_kern` is the number the Mach-O header exists to make true: the image's `.bss` now ends at
`0x002479c8`, `getlastaddr()` reads that back out of the header, and `round_page` gives
`0x00248000`. It moved 16 KB from exp-194 because this step made the image bigger, which is the
header doing its job rather than the linker script's number being copied somewhere.

`CPSR` is `0x60000093` again — F clear, as in exp-193 and exp-194. Nothing here depends on it.

## What ran

The object is `osfmk_vm_vm_resident.o` — 31692 bytes of text, 104 of data, 5772 of `.bss` and
**113 undefined references**, the largest object in this sequence and the first from the VM proper
rather than the ARM layer. Linking it resolves seven symbols and adds sixty-one:

```
resolved (7):  vm_cache_geometry_colors  vm_page_create  vm_page_init_local_q  vm_page_locks
               vm_page_wire_count  vm_page_wire_count_initial  vm_set_page_size
added    (61): c_master_lock  compressor_object  consider_buffer_cache_collect  consider_zone_gc
               fillPage  kalloc_map  kernel_memory_allocate  kernel_object  ledger_debit
               memorystatus_pages_update  OSAddAtomic  OSAddAtomic16
               OSKextGetAllocationSiteForCaller  OSKextGetKmodIDForSite
               patch_low_glo_vm_page_info  pmap_clear_noencrypt  pmap_clear_reference
               pmap_clear_refmod  pmap_copy_page  pmap_copy_part_page  pmap_disconnect
               pmap_enter  pmap_enter_options  pmap_free_pages  pmap_next_page
               pmap_next_page_hi  pmap_set_cache_attributes  pmap_set_modify
               pmap_virtual_space  pmap_zero_page  pmap_zero_part_page
               purgeable_nonvolatile_count  purgeable_nonvolatile_queue  purgeable_queues
               token_new_pagecount  vm_compressor_init_locks  vm_config  vm_map_sizes
               vm_map_steal_memory  vm_object_cache_remove  vm_object_lock_avoid
               _vm_object_lock_try  vm_object_lock_try  vm_object_reuse_pages
               vm_object_unlock  vm_page_is_slideable
               vm_pageout_cleaned_commit_reactivated  vm_pageout_cleaned_reactivated
               vm_pageout_steal_laundry  vm_pageout_throttle_up  vm_page_slide
               vm_page_speculative_q_age_ms  vm_page_validate_cs  vm_paging_lock
               vm_purgeable_q_advance_all  vm_purgeable_queue_lock  vm_submap_object
               zcram  zget  zone_free_count  zone_map
324 -> 378 undefined
```

Both directions link, which is exp-190's rule: the probe stands on `pmap_bootstrap`, which
`osfmk/arm/pmap.o` defines — and `pmap.o` is *not* part of this step.

Sixty-one new obligations and not one of them was reached, because the only code in this step that
runs is `vm_set_page_size` — twelve statements, 52 bytes, no calls except a `panic` on a page size
that is not a power of two — and then `arm_vm_init`'s own tail. That is the shape of a step that is
much larger than the part of it that executes, and it is worth saying plainly: **the frontier
method measures the call graph, not the file size.** A hundred and thirteen references and the
device touched one function of them.

What also ran, and had never run before, is the part of `arm_vm_init` between `vm_set_page_size`
and the probe:

```
    1a90:	bl	vm_set_page_size
    1aa0:	bl	set_mmu_ttb
    1aa8:	bl	set_mmu_ttb_alternate
    1aac:	bl	flush_mmu_tlb
    1ab0:	mrc	15, 0, r0, cr13, cr0, {4}     ; current_thread, the __ARM_USER_PROTECT__ block
    ...  428 bytes of stores to this image's own globals
    1c5c:	bl	pmap_bootstrap
```

So XNU's page tables were installed for the first time on this device (`TTBR0` switched to the
`boot_tte` copy at `cpu_ttep`, TLB flushed) and the kernel kept executing, which is the property
the image was built around: `physBase == virtBase` makes `_start`'s tables identity tables and
makes the switch transparent. The `__ARM_USER_PROTECT__` block wrote `thread->machine.uptw_ttb`,
`kptw_ttb` and `uptw_ttc` — three stores at `+1448`, `+1452`, `+1456` from TPIDRPRW, which exp-193
measured as `&init_thread` — inside `init_thread`'s 0x680 bytes, so nothing was written outside the
object. And the V=P clearing loop that precedes it is a **no-op for this image**: `gPhysBase ==
gVirtBase` takes the `else` branch, which sets `tte = &cpu_tte[ttenum(gVirtBase + gPhysSize)]`,
equal to `tte_limit`, so the loop body never runs. The image's own mapping is the boot table's
mapping, which is why the switch is safe.

## A defect in the evidence path, found the hard way

**The first run of this experiment produced this:**

```
MI4IOS6_STAGE90_XNU real XNU entry: real arm_init reached a symbol this image does not provide

No errors detected
```

No name, no values. The message proves `entry_stub_hit` ran — it is the only caller of
`entry_epilogue` that passes that string — so a probe was reached, and the results buffer came
back empty. An empty buffer cannot be told apart from a probe that never ran, and the *only* thing
that could have written it, `entry_kv`, has a guard that could not have failed at
`g_kv_len == 0`. The image itself was checked before anything else was suspected: the
`pmap_bootstrap` probe is in the linked ELF, and `xnu_entry_pmb_next_paddr` is in
`stage90.bin`'s embedded copy of it, so the payload was running the image that was built.

The cause is in `entry_epilogue`'s cache flush, and it is two defects in one loop.

**It read CCSIDR without ever selecting a cache.** `CCSIDR` describes whichever cache `CSSELR`
selects, and `entry_stubs.c` never wrote `CSSELR` — neither does `cache_ops.c`, whose comment
states the same assumption ("which defaults to the L1 data cache"). XNU's own `do_cacheid()`
(`osfmk/arm/cpuid.c:222-265`) selects L1, reads CCSIDR, and then — because this device reports
`Ctype2 == 0x4` — selects **L2** and reads it again, and never selects L1 back. So by the time
anything of this project's runs after `cpu_init`, `CSSELR` points at the L2. The device measured
exactly that:

```
 xnu_entry_csselr_before=0x00000002        ; CSSELR_L2, from cpuid.c's own enum
 xnu_entry_ccsidr_before=0xf0ffe03b        ; 4096 sets x 8 ways x 128 B = 4 MB
 xnu_entry_ccsidr_l1=0xa007e01a            ; 64 sets x 4 ways x 64 B = 16 KB
```

The second value is the L1 data cache, measured on this device for the first time and matching
what a Krait core should have. The first is not an L1 at all.

**And the set/way operand was built with the wrong shift.** The operand is built as
`(way << way_shift) | (set << log2(line))`, and the set field is bits
`[log2(line) + log2(sets) - 1 : log2(line)]` — XNU's own flush increments the set by
`1 << MMU_I7SET` and looks for overflow at `1 << (MMU_NSET + MMU_I7SET)`
(`osfmk/arm/caches_asm.s:245-257`, `start.s:281-288`), which is exactly that. But the way is
**not** above the set: XNU increments it at a fixed high bit, `1 << MMU_I7WAY`, and
`osfmk/arm/proc_reg.h` gives that bit as `30` for a 4-way cache (`MMU_NWAY = 2`), `31` for a
2-way one (`MMU_NWAY = 1`, Cyclone and Typhoon), and `29` for the L2's 8 ways
(`L2_NWAY = 3`) — the same rule in all three, `32 - log2(ways)`, the way right-justified at bit
31. For this device's measured 4-way L1 that is bit 30. The loop used
`log2(line) + log2(ways)` = **bit 8**, which is inside the set field, so it cannot select a way at
all.

Together: the flush enumerated 4096x8 from the L2's description, and drove the L1 with an operand
whose way bits are in the wrong place. It was not a flush. Which lines it reached was therefore an
accident, which is how the identical mechanism delivered `stub_hit=vm_set_page_size` in exp-194 and
lost the buffer here — and it is worth being exact about what that means: the two addresses do not
differ in any way this project can model (`.bss` moved from `0x0023xxxx` to `0x00241xxx`, and both
land in the same set and the same way under the measured geometry), so there is no explanation here
of why one delivered and the other did not. The honest reading is that the flush was never a flush
and the runs before this one were lucky. That is exactly the property that had to go: a measurement
path whose success is not determined by the thing being measured.

This is a defect in the *evidence path*, not in the kernel: it silently decided whether any
experiment's output appeared, and the failures it caused would have looked like "the probe was not
reached" — the most expensive possible misreading for a project that measures by stopping.

## The fix, and what it does not depend on

Four changes, all in `entry_epilogue`:

1. **The results buffer is cleaned by MVA** — `mcr p15, 0, addr, c7, c10, 1` over
   `[&g_kv_len, &g_kv_buf[ENTRY_KV_BUF])` in 32-byte steps — before the sweep. A clean by address
   names the lines directly, cannot miss them, and does not depend on any geometry read; the
   hardware ignores the bits below the line, so a step smaller than the line size is safe.
2. **`CSSELR` is written rather than assumed**: level 1 data (0) before the geometry read, with the
   pre-existing value and both CCSIDR readings kept and printed, because the pair is the evidence.
3. **The sweep's way shift is corrected to `32 - log2(ways)`** — bit 30 here — which is the
   convention XNU's own macros encode, instead of `log2(line) + log2(ways)` = bit 8.
4. **`kv_written` is read from a register before the teardown and printed next to `kv_in_dram`.**
   A value in a callee-saved register cannot be lost by a cache flush, so an empty buffer with a
   non-zero `kv_written` now says "the transfer failed" instead of being indistinguishable from
   "nothing was recorded". This is the change that made the failure diagnosable at all.

The sweep stays as the backstop that covers every other line the image wrote, but it is no longer
load-bearing: the measured run reports `kv_written == kv_in_dram == 0x1cc`, and the run after it —
with the corrected shift as well — reports the same twelve values with the same checksums, so the
results now arrive by construction rather than by geometry.

The same two defects are in `cache_ops.c`'s `cache_clean_invalidate_dcache_all()`, which
reproduces the unselected CCSIDR read and the `log2(line) + log2(ways)` shift. Under the current
`CACHE_MODE = NONE` that function has no callers — its two call sites are both inside
`#if STAGE90_CACHE_MODE == STAGE90_CACHE_MODE_ICACHE_DCACHE` — so it has never caused a failure
here; it is fixed in experiment 196 rather than in this one, because it is a change to the payload
and needs the payload verified on the device.

## Cost

| | exp-194 | now |
| --- | --- | --- |
| entry objects linked | 35 | 36 (`osfmk/vm/vm_resident.o`) |
| entry text | 147308 B | 180876 B |
| entry image | 215144 B | 264400 B |
| entry `.bss` | 0x00234588 – 0x00239f08 (22912 B) | 0x002405c0 – 0x002479c8 (29704 B) |
| undefined | 324 | 378 |
| stubs | 282 functions, 42 storage | 323 functions, 55 storage |
| boot_args offset | +241664 | +299008 |
| headroom below `topOfKernelData` | — | 1803832 B |
| payload text | 706958 B | 756214 B |

The image is 49 KB bigger and still fits the same window with 1.8 MB to spare; `.bss` ends 1.5 MB
below `topOfKernelData`, the page tables and the device-tree buffer. `xnu_entry_checks=5` /
`xnu_entry_failures=0` re-checks exp-175's four invariants with `args` at +299008.

## What is next: `pmap.o`, and a cache defect fixed before it had a caller

The frontier is exactly where exp-194 predicted it. `pmap_bootstrap` is `osfmk/arm/pmap.c`, and the
object is `osfmk_arm_pmap.o`: **45052 bytes of text, 72 of data, 1032 of `.bss`, 93 undefined
references** — the pmap proper, and the point at which the kernel starts building page tables for
memory that is not this image. `arm_vm_prot_init(args)` and the `pmap_init_pte_page` loop follow it
in the same function, so the step after that is still inside `arm_vm_init`.

`cache_ops.c` carries the same two defects, and it is the payload's one whole-cache flush, used
before the D-cache is first enabled (`mmu.c:5486`) and by `platform_reboot`
(`stage90_main.c:840`). Both call sites are inside
`#if STAGE90_CACHE_MODE == STAGE90_CACHE_MODE_ICACHE_DCACHE`, and this project runs
`CACHE_MODE = NONE`, so the function is linked and never called: **zero `bl` to it in
`stage90.elf`**. It is dead code today and becomes live the moment the cache mode is turned on,
which is the direction the work is going.

## Reproduce

```bash
(cd stages/stage90/xnu_arm_boot && STAGE90_ENTRY_REAL_ARM_INIT=1 ./build_entry.sh)
#   ... entry point 0x00200074, text 180876, image 264400, 378 undefined, 323 stubs
./tools/host_entry_macho_check.sh
#   ... getlastaddr() -> 0x002479c8, every field matches the linker's own symbols

# what the object costs: 7 resolved, 61 added. The probe stands on `pmap_bootstrap`, which
# vm_resident.o does not define, so both directions link.
printf '' > /tmp/empty.c && arm-none-eabi-gcc -mcpu=cortex-a15 -marm -c /tmp/empty.c -o /tmp/empty.o
cp out/stage90/xnu_arm_entry_undef.txt /tmp/A.txt
(cd stages/stage90/xnu_arm_boot && STAGE90_ENTRY_REAL_ARM_INIT=1 \
   STAGE90_ENTRY_VM_RESIDENT_OBJ=/tmp/empty.o ./build_entry.sh)
cp out/stage90/xnu_arm_entry_undef.txt /tmp/B.txt
comm -13 <(sort /tmp/A.txt) <(sort /tmp/B.txt)   # 7 resolved
comm -23 <(sort /tmp/A.txt) <(sort /tmp/B.txt)   # 61 added

(cd stages/stage90 && STAGE90_EXTRA_CFLAGS='-DSTAGE90_XNU_ENTRY=1' ./build.sh)
(cd stages/stage90 && ./run_and_capture.sh --allow-xnu-entry)
sed -n '/jumping to XNU/,$p' /tmp/cancro-last_kmsg.txt | head -20

# the flush defect, in XNU's own words
sed -n '222,268p' external/xnu-4570.1.46/osfmk/arm/cpuid.c
sed -n '240,262p' external/xnu-4570.1.46/osfmk/arm/caches_asm.s
grep -n "MMU_I7SET\|MMU_I7WAY\|MMU_NSET" external/xnu-4570.1.46/osfmk/arm/*.h | head

# the size of the next step
arm-none-eabi-size out/xnu_kernel_obj/osfmk_arm_pmap.o
arm-none-eabi-nm -u out/xnu_kernel_obj/osfmk_arm_pmap.o | wc -l
```

Nothing was flashed: `persistent_write_attempted=0x00000000` in all 25 contracts that report it,
and the device returned to Android on its own.
