# Experiment 231 — the Prediction Holds, `zone_bootstrap` at Last, and Two Source Branches Instead of a Walk

Date: 2026-09-18
Build switches: `STAGE90_ENTRY_REAL_ARM_INIT=1` (entry image), `STAGE90_XNU_ENTRY=1` (payload);
gate flag `--allow-xnu-entry`. All other switches at their defaults, which are exp-159's:
`HANDOFF_MODE = HARD_SKIP`, `PMAP_ATTR_MODE = SO_ONLY`, `CACHE_MODE = NONE`, `STAGE90_XNU_REAL_DT = 0`
Device: Xiaomi Mi 4 LTE `cancro`, serial `4a2fe00b`, non-persistent `fastboot boot`
Capture: `/tmp/cancro-last_kmsg.txt`

## The result

```
MI4IOS6_STAGE90_XNU stage90 xnu_entry: jumping to XNU's _start

MI4IOS6_STAGE90_XNU real XNU entry: real arm_init reached a symbol this image does not provide
 xnu_entry_kv_written=0x00000019
 xnu_entry_kv_in_dram=0x00000019
 xnu_entry_csselr_before=0x00000002
 xnu_entry_ccsidr_before=0xf0ffe03b
 xnu_entry_ccsidr_l1=0xa007e01a
MI4IOS6_STAGE90_XNU real XNU entry stub_hit=zone_bootstrap

No errors detected
```

`stage90_xnu_entry_stub_status=0x90000001`, `failure_mask=0x00000000` in every contract that reports
one, `safety_boundary_preserved=0x00000001`, `mmu_unchanged=0x00000001`,
`persistent_write_attempted=0x00000000` in all 25 contracts that report it, and the device returned
to Android on its own. `kv_written == kv_in_dram == 0x19 = 25 = strlen("zone_bootstrap") + 11`.
No `exception:` line, and one `undef` breadcrumb, at line 3454, before the jump.

**The prediction was `zone_bootstrap` and it held** — the first hit since experiment 227, after four
consecutive misses. Four earlier predictions had aimed at this same symbol and none had arrived.

## Why this one was made differently

The four misses before it are a complete account of what the walk cannot see, so this prediction was
not made by walking. It was made by reading two branches out of the source, both of which the walk
calls guarded and both of which are not taken:

```c
/* osfmk/vm/vm_pageout.c */
boolean_t vm_pressure_events_enabled = FALSE;            /* :4089 ... */

void vm_pressure_response(void)
{
	if (vm_pressure_events_enabled == FALSE)
		return;                                   /* ... so the whole body is skipped */
	...
		if (vm_pressure_thread_running == FALSE) {
			thread_wakeup_prim(...);          /* the one stub inside it, at +0x3e0 */
		}
```

```c
/* bsd/kern/kern_memorystatus.c */
unsigned int memorystatus_available_pages_pressure = 0;  /* :658 */

void memorystatus_pages_update(unsigned int pages_avail)
{
	memorystatus_available_pages = pages_avail;
	vm_pressure_response();
	if (memorystatus_available_pages <= memorystatus_available_pages_pressure) {
		...
			memorystatus_thread_wake();       /* ... which reaches thread_wakeup_prim */
	}
```

`vm_pressure_events_enabled` is `FALSE` at its definition and is set `TRUE` only at line 4572, far
past this call, so `vm_pressure_response` returns on its second instruction. `memorystatus_available_pages`
is a few hundred thousand pages and the threshold is `0`, so the second branch is not taken either.
Both `thread_wakeup_prim` stubs — the one inside `vm_pressure_response` and the one inside
`memorystatus_pages_update` — are therefore unreachable on this boot path, and the prediction names
neither.

Everything from there to `zone_bootstrap` is real: `pmap_startup`'s remaining body,
`vm_page_bootstrap`'s `arm_usimple_lock_init`, `vm_mem_bootstrap`'s `kernel_debug_string_early`. And
`zone_bootstrap` is `vm_mem_bootstrap`'s next call at `+0x2c`, which is the call list experiment 229
quoted and experiment 230's stop three levels short of.

**The `stub_hit=zone_bootstrap` line is the first prediction in five that a reader could have checked
by hand, and that is the point.** The tool now has a rule for the cases it can compute
(`--root <frontier>`), and a rule for the cases it cannot (`--assume-taken CALLER+0xNNN`), and this
step is the first where neither was needed because the function was short enough to read.

## Cost

`out/xnu_kernel_obj/osfmk_vm_vm_pageout.o` (`osfmk/vm/vm_pageout.c`) — **54812 bytes of text, 152 of
data (32 in `.data` and 120 in a `__DATA, __data` section), 1992 of `.bss`, 2764 of
`.rodata.str1.1`, 303 definitions, 219 references**:

```
resolved (22): memoryshot  vm_page_free_reserve  vm_page_is_slideable  vm_page_slide  vm_pageout
               vm_pageout_internal_start  vm_pageout_steal_laundry  vm_pageout_throttle_up
               vm_paging_map_init  vm_pressure_response  vm_set_restrictions        (11 functions)
               consider_buffer_cache_collect  memorystatus_purge_on_critical
               memorystatus_purge_on_urgent  memorystatus_purge_on_warning  vm_config
               vm_page_speculative_q_age_ms  vm_pageout_cleaned_commit_reactivated
               vm_pageout_cleaned_reactivated  vm_pageout_queue_external  vm_paging_lock
               vm_restricted_to_single_processor                                      (11 storage)

added  (35):   available_for_purge  commpage_set_memory_pressure  consider_machine_adjust
               consider_machine_collect  cs_debug  is_zone_map_nearing_exhaustion  m_drain
               memory_object_data_initialize  memory_object_data_return
               proc_set_thread_policy_with_tid  stack_collect  thread_block_parameter
               thread_yield_internal  upl_offset_to_pagelist
               vm_compressor_get_encode_scratch_size  vm_compressor_pager_count
               vm_compressor_pager_init  vm_compressor_pager_put  vm_fault_page
               vm_object_cache_evict  vm_object_collapse
               vm_object_compressor_pager_create  vm_object_lck_attr  vm_object_lck_grp
               vm_object_lock_request  vm_object_page_grab  vm_object_reaper_init
               vm_object_transpose  vm_object_update  vm_page_sleep
               vm_purgeable_compressed_update  vm_purgeable_object_purge_one
               vm_shared_region_slide_page  vnode_pager_get_isinuse
               vnode_pager_lookup_vnode

653 -> 666 undefined          (653 + 35 - 22)
```

`653 + 35 - 22 = 666` exactly. The 22 resolved are 11 function stubs and 11 storage, so the counters
move `551 - 11 + 30 = 570` functions and `102 - 11 + 5 = 96` storage, and 30 + 5 = 35. **Storage stubs
went down for the first time** — eleven data symbols stopped being four bytes of zero and became the
real objects, including `vm_config` at 0x14, `vm_pageout_queue_external` at 0x20 and `vm_paging_lock`
at 0x8.

| | exp-230 | now |
| --- | --- | --- |
| entry objects linked | 70 | 71 (`osfmk_vm_vm_pageout.o`) |
| entry text | 460593 B | **518801 B** |
| entry image | 569632 B | **618936 B** |
| entry `.bss` | 0x0028ab28–0x002a93c8 (125088 B) | 0x00296b48–0x002b5b88 (**127040 B**) |
| undefined | 653 | **666** |
| stubs | 551 functions, 102 storage | **570 functions, 96 storage** |
| boot_args offset | +700416 | **+749568** |
| headroom below `topOfKernelData` | 1403960 B | **1352824 B** |
| payload text | 1061818 B | **1111122 B** |

## What is next: `zone_bootstrap`, and the prediction is `thread_call_setup`

The frontier is `zone_bootstrap`, defined by `osfmk/kern/zalloc.c` → `out/xnu_kernel_obj/osfmk_kern_zalloc.o`
— **16672 bytes of text, 8 of data, 56280 of `.bss`, 1386 of `.rodata.str1.1`, 128 definitions,
72 references**. The `.bss` is the largest single contribution this link has taken, the zone table,
and it is zeroed by the payload rather than stored, so it costs image bytes and not file bytes.

**The prediction is `stub_hit=thread_call_setup`**, and it comes from both methods agreeing at once:
`--root zone_bootstrap` names it, and so does the plain walk from `kernel_bootstrap`, which is the
first time that has happened since experiment 227.

```
$ python3 tools/xnu_entry_callwalk.py --root zone_bootstrap
walk from zone_bootstrap:
  zone_bootstrap
    thread_call_setup   STUB
```

The two agreeing is itself the signal: the walk only reaches that call because every function between
`kernel_bootstrap` and `zone_bootstrap` is real now, so the lower bound and the rooted answer have
converged. `thread_call_setup` is defined by `osfmk/kern/thread_call.c`, so the step after this one is
`osfmk_kern_thread_call.o`, and `early_random`'s `read_erandom` sits two guards behind it as the named
alternative.

## Reproduce

```bash
# the step: 22 resolved, 35 added, 653 -> 666, stubs 551fn/102st -> 570fn/96st
printf '' > /tmp/empty.c && arm-none-eabi-gcc -mcpu=cortex-a15 -marm -c /tmp/empty.c -o /tmp/empty.o
(cd stages/stage90/xnu_arm_boot && STAGE90_ENTRY_REAL_ARM_INIT=1 \
   STAGE90_ENTRY_OSFMK_VM_VM_PAGEOUT_OBJ=/tmp/empty.o ./build_entry.sh)
cp out/stage90/xnu_arm_entry_undef.txt /tmp/A.txt
(cd stages/stage90/xnu_arm_boot && STAGE90_ENTRY_REAL_ARM_INIT=1 ./build_entry.sh)
cp out/stage90/xnu_arm_entry_undef.txt /tmp/B.txt
comm -23 <(LC_ALL=C sort -u /tmp/A.txt) <(LC_ALL=C sort -u /tmp/B.txt) | wc -l   # 22
comm -13 <(LC_ALL=C sort -u /tmp/A.txt) <(LC_ALL=C sort -u /tmp/B.txt) | wc -l   # 35
wc -l /tmp/A.txt /tmp/B.txt                                                      # 653 and 666

# ... and it ran
./tools/host_entry_macho_check.sh
./tools/host_dt_check.sh | grep -E "tree built|RESULT"
(cd stages/stage90 && STAGE90_EXTRA_CFLAGS='-DSTAGE90_XNU_ENTRY=1' ./build.sh)
(cd stages/stage90 && ./run_and_capture.sh --allow-xnu-entry)
sed -n '/jumping to XNU/,$p' /tmp/cancro-last_kmsg.txt | head -14   # ... stub_hit=zone_bootstrap

# the two branches the prediction rests on
sed -n '4089p;4100p;4572p' external/xnu-4570.1.46/osfmk/vm/vm_pageout.c
sed -n '658p' external/xnu-4570.1.46/bsd/kern/kern_memorystatus.c
```

Nothing was flashed: `persistent_write_attempted=0x00000000` in all 25 contracts that report it,
and the device returned to Android on its own.
