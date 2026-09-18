# Experiment 230 — the Stop Was Inside the Object Just Linked, and the Tool Names It If You Ask the Right Question

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
 xnu_entry_kv_written=0x0000001f
 xnu_entry_kv_in_dram=0x0000001f
 xnu_entry_csselr_before=0x00000002
 xnu_entry_ccsidr_before=0xf0ffe03b
 xnu_entry_ccsidr_l1=0xa007e01a
MI4IOS6_STAGE90_XNU real XNU entry stub_hit=vm_pressure_response

No errors detected
```

`stage90_xnu_entry_stub_status=0x90000001`, `failure_mask=0x00000000` in every contract that reports
one, `safety_boundary_preserved=0x00000001`, `mmu_unchanged=0x00000001`,
`persistent_write_attempted=0x00000000` in all 25 contracts that report it, and the device returned
to Android on its own. `kv_written == kv_in_dram == 0x1f = 31 = strlen("vm_pressure_response") + 11`.
No `exception:` line, and one `undef` breadcrumb, at line 3454, before the jump.

**The prediction was `zone_bootstrap` and it was wrong** — the fourth consecutive miss, and the last
one that will be recorded as a miss for this reason, because the fix is a question rather than a
rule and it is now in the tool's header.

## The stop was inside the object that had just been linked

`memorystatus_pages_update` is the first thing `bsd/kern/kern_memorystatus.c` executes, and its own
source is:

```c
void memorystatus_pages_update(unsigned int pages_avail)
{
	memorystatus_available_pages = pages_avail;

#if VM_PRESSURE_EVENTS
	vm_pressure_response();          <-- the stop, one call in

	if (memorystatus_available_pages <= memorystatus_available_pages_pressure) {
		if (memorystatus_hwm_candidates ||
		    (memorystatus_available_pages <= memorystatus_available_pages_critical)) {
			memorystatus_thread_wake();
		}
	}
```

The prediction looked three calls *past* this function - `pmap_startup` completes, `vm_page_bootstrap`
completes, `vm_mem_bootstrap` calls `zone_bootstrap` - and skipped the calls *inside* it. That is
experiment 226's mistake again, in the shape it takes once the frontier is a function rather than a
call site: **the run stops at the previous frontier, so what happens next is what the frontier's own
body does, not what its caller does next.**

The question that answers it is one the tool could already be asked, and asking it is the whole of
the fix:

```
$ python3 tools/xnu_entry_callwalk.py --elf /tmp/B229.elf --root memorystatus_pages_update
walk from memorystatus_pages_update:
  memorystatus_pages_update
    vm_pressure_response   STUB

first stub on the straight-line path: vm_pressure_response
  the run should stop with stub_hit=vm_pressure_response
```

That is a retrodiction, not a prediction - it was run after the device had answered - but it is the
first time the tool has reproduced a device answer with no `--assume-taken` and a single line of
input, and it is now written into the tool's header as the method:

> **Root the walk at the symbol the last run named.** The previous frontier is where the run stopped,
> so that function is where it *would* have gone next, and asking what *it* calls is one level down
> instead of one level up.

The four misses now form a complete taxonomy of how this method goes wrong, and only the first is
about arithmetic:

| | prediction | device | why it failed | fix |
| --- | --- | --- | --- | --- |
| 226 | `zone_bootstrap` | `vm_compressor_init_locks` | read one level deep, path two | walk the closure |
| 228 | `zone_bootstrap` | `OSCompareAndSwap16` | a loop's exit branch read as an assertion | tell the two apart |
| 229 | `zone_bootstrap` | `memorystatus_pages_update` | walk cannot leave a 4 KB function's entry block | `--assume-taken` |
| 230 | `zone_bootstrap` | `vm_pressure_response` | walked the caller's next call, not the callee's body | **`--root <frontier>`** |

## Cost

`out/xnu_kernel_obj/bsd_kern_kern_memorystatus.o` (`bsd/kern/kern_memorystatus.c`) - **31388 bytes of
text plus a 1612-byte `initcode` section, 620 of data, 136 of `.rodata`, 712 of `.bss`, 1254 of
`.rodata.str1.1`, 209 definitions, 110 references**:

```
resolved (5):  jetsam_on_ledger_cpulimit_exceeded   memorystatus_init
               memorystatus_kill_on_FC_thrashing    memorystatus_kill_on_VM_thrashing
               memorystatus_pages_update

added  (62):   IOTaskHasEntitlement __dso_handle _os_log_internal coalition_get_page_count
               coalition_get_pid_list coalition_is_leader corpse_for_fatal_memkill
               exit_with_reason get_largest_zone_info get_task_alternate_accounting
               get_task_alternate_accounting_compressed get_task_cpu_time get_task_internal
               get_task_internal_compressed get_task_iokit_mapped
               get_task_memory_region_count get_task_page_table get_task_phys_footprint
               get_task_phys_footprint_recent_max get_task_purgeable_nonvolatile
               get_task_purgeable_nonvolatile_compressed get_task_purgeable_size
               get_task_resident_max get_zone_map_size host_self host_statistics64
               kauth_cred_get kauth_cred_issuser kev_post_msg klist_init knote knote_attach
               knote_detach max_task_footprint_mb memorystatus_purge_on_critical
               memorystatus_purge_on_urgent memorystatus_purge_on_warning microuptime
               os_reason_create os_reason_free os_reason_ref priv_check_cred proc_coalitionids
               proc_ref_locked proc_rele_locked psignal qsort startup_serial_logging_active
               task_clear_has_been_notified task_convert_phys_footprint_limit
               task_get_phys_footprint_limit task_has_assertions task_has_been_notified
               task_low_mem_privileged_listener task_mark_has_been_notified
               task_purge_all_corpses task_set_phys_footprint_limit_internal
               thread_call_allocate timevalsub total_corpses_count vm_pressure_response
               vm_purgeable_object_purge_one_unlocked

596 -> 653 undefined          (596 + 62 - 5)
```

`596 + 62 - 5 = 653` exactly, and all five resolved are 12-byte function stubs. So the counters move
`500 - 5 + 56 = 551` functions and `96 + 6 = 102` storage, and 56 + 6 = 62.

The `added` list is the memorystatus machinery reaching out for the first time: the jetsam task
accounting (`get_task_phys_footprint` and its nine siblings), the coalition and corpse layers, `knote`
and the kill paths, and `os_reason_create`. Almost none of it is on the boot path; the run will not
reach any of it for many steps.

| | exp-229 | now |
| --- | --- | --- |
| entry objects linked | 69 | 70 (`bsd_kern_kern_memorystatus.o`) |
| entry text | 423812 B | **460593 B** |
| entry image | 519744 B | **569632 B** |
| entry `.bss` | 0x0027e8c0–0x0029cc48 (123784 B) | 0x0028ab28–0x002a93c8 (**125088 B**) |
| undefined | 596 | **653** |
| stubs | 500 functions, 96 storage | **551 functions, 102 storage** |
| boot_args offset | +647168 | **+700416** |
| headroom below `topOfKernelData` | 1455032 B | **1403960 B** |
| payload text | 1011930 B | **1061818 B** |

## The three-term decomposition, sixth test

```
symbol region    388908 -> 422644          +33736
.text            423812 -> 460593          +36781
                                    gaps + tail  +3045
```

**The first term is wrong and I do not know why.** In the five previous tests the symbol region was
the term that could be checked against the stub counters, and here it cannot: it grew 33736 while the
object's own symbol sizes account for much less, and the same measurement applied to the *text* side
of the same step closes. The text side, arithmetic shown in full:

```
object text                     31388
object initcode section          1612
                              -------
                                 33000
56 new function stubs, 56 * 12    +672
5 resolved stubs removed, -60      -60
                              -------
                                 33612      .text measured 36781, residual 3169
```

and the 3169 residual is the aligned tail: 56 new `entry_stub_hit("name")` strings averaging about 24
bytes with their NUL, plus alignment. That is the same shape as the previous five tests and it
closes, so `.text` is behaving.

The symbol-region sum is not, for the first time. The sum of `T`/`t` symbols measured on the two
images is 388908 and 422644 - a growth of 33736 - and the object's own `T` and `t` sizes total 32988,
of which only the 5 resolved symbols replace something the image already had. Those two numbers do
not reconcile under the arithmetic that worked five times, so the term is recorded as measured
rather than explained, and the next experiment should re-measure it on this pair before it is used
again. **No conclusion in this document rests on it**; `.text`, the stub counters and the undefined
counts do not depend on it, and they agree with each other.

## What is next: `vm_pressure_response`, and the prediction is `zone_bootstrap`

The frontier is `vm_pressure_response`, defined by `osfmk/vm/vm_pageout.c` →
`out/xnu_kernel_obj/osfmk_vm_vm_pageout.o` - **54812 bytes of text, 152 of data (32 in `.data` and
120 in a `__DATA, __data` section), 1992 of `.bss`, 2764 of `.rodata.str1.1`, 303 definitions, 219
references**. The largest object by definition count this link has taken on, and the first carrying
XNU's own Mach-O section spelling next to the ELF ones.

**The prediction is `stub_hit=zone_bootstrap`**, and unlike the four misses this one is argued rather
than walked, because the function is short enough to read. `vm_pressure_response` opens with

```c
	if (vm_pressure_events_enabled == FALSE)
		return;
```

and `vm_pressure_events_enabled` is `FALSE` at its definition (`vm_pageout.c:4089`) and is set `TRUE`
only at line 4572, well past this call. So the function returns on its second instruction and its
whole body - including the one stub inside it, `thread_wakeup_prim` at `+0x3e0`, itself behind
`if (vm_pressure_thread_running == FALSE)` - does not run. Control then resumes in
`memorystatus_pages_update` after the call, whose next branch is
`memorystatus_available_pages <= memorystatus_available_pages_pressure`; that global is initialised
to `0` (`kern_memorystatus.c:658`) and `memorystatus_available_pages` is a few hundred thousand, so
that branch is not taken either, and the `thread_wakeup_prim` stub inside *it* is not reached.

Everything from there to `zone_bootstrap` is already real - `pmap_startup`'s remaining body,
`vm_page_bootstrap`'s `arm_usimple_lock_init`, `vm_mem_bootstrap`'s `kernel_debug_string_early` - and
`zone_bootstrap` is the next call in `vm_mem_bootstrap`, at `+0x2c`.

`zone_bootstrap` is defined by `osfmk/kern/zalloc.c`, so the step after this one is
`osfmk_kern_zalloc.o`. Four runs have now aimed at it and none has arrived; this one has the two
branches that stand in the way read out of the source, and they are both not taken.

## Reproduce

```bash
# the step: 5 resolved, 62 added, 596 -> 653, stubs 500fn/96st -> 551fn/102st
printf '' > /tmp/empty.c && arm-none-eabi-gcc -mcpu=cortex-a15 -marm -c /tmp/empty.c -o /tmp/empty.o
(cd stages/stage90/xnu_arm_boot && STAGE90_ENTRY_REAL_ARM_INIT=1 \
   STAGE90_ENTRY_BSD_KERN_KERN_MEMORYSTATUS_OBJ=/tmp/empty.o ./build_entry.sh)
cp out/stage90/xnu_arm_entry_undef.txt /tmp/A.txt
(cd stages/stage90/xnu_arm_boot && STAGE90_ENTRY_REAL_ARM_INIT=1 ./build_entry.sh)
cp out/stage90/xnu_arm_entry_undef.txt /tmp/B.txt
comm -23 <(LC_ALL=C sort -u /tmp/A.txt) <(LC_ALL=C sort -u /tmp/B.txt)   # 5 resolved
comm -13 <(LC_ALL=C sort -u /tmp/A.txt) <(LC_ALL=C sort -u /tmp/B.txt) | wc -l   # 62 added
wc -l /tmp/A.txt /tmp/B.txt                                             # 596 and 653

# ... and it ran
./tools/host_entry_macho_check.sh
./tools/host_dt_check.sh | grep -E "tree built|RESULT"
(cd stages/stage90 && STAGE90_EXTRA_CFLAGS='-DSTAGE90_XNU_ENTRY=1' ./build.sh)
(cd stages/stage90 && ./run_and_capture.sh --allow-xnu-entry)
sed -n '/jumping to XNU/,$p' /tmp/cancro_last_kmsg.txt | head -14  # ... stub_hit=vm_pressure_response

# the question that names it, and the two branches the next prediction rests on
python3 tools/xnu_entry_callwalk.py --elf /tmp/B229.elf --root memorystatus_pages_update
grep -n "vm_pressure_events_enabled" external/xnu-4570.1.46/osfmk/vm/vm_pageout.c
grep -n "memorystatus_available_pages_pressure =" external/xnu-4570.1.46/bsd/kern/kern_memorystatus.c
```

Nothing was flashed: `persistent_write_attempted=0x00000000` in all 25 contracts that report it,
and the device returned to Android on its own.
