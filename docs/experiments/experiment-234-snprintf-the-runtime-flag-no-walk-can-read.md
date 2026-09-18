# Experiment 234 — `snprintf`, a Runtime Flag No Walk Can Read, and the Guard Rule Wrong in the Other Direction

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
 xnu_entry_kv_written=0x00000013
 xnu_entry_kv_in_dram=0x00000013
 xnu_entry_csselr_before=0x00000002
 xnu_entry_ccsidr_before=0xf0ffe03b
 xnu_entry_ccsidr_l1=0xa007e01a
MI4IOS6_STAGE90_XNU real XNU entry stub_hit=snprintf
```

`stage90_xnu_entry_stub_status=0x90000001`, `failure_mask=0x00000000` in every contract that reports
one, `safety_boundary_preserved=0x00000001`, `mmu_unchanged=0x00000001`,
`persistent_write_attempted=0x00000000` in all 25 contracts that report it, and the device returned
to Android on its own. `kv_written == kv_in_dram == 0x13 = 19 = strlen("snprintf") + 11`. No `exception:` line, and one `undef` breadcrumb, at
line 3454, before the jump.

**The prediction was `kmem_alloc_kobject` and it was wrong — the fifth miss, and the first one that
is not a mistake in the walking.**

## The stop is one call after the prediction, in the same function

`vm_object_bootstrap` opens by calling `zinit`, and `zinit` is where both symbols live:

```
$ python3 tools/xnu_entry_callwalk.py --root vm_object_bootstrap
walk from vm_object_bootstrap:
  vm_object_bootstrap
    zinit
      kmem_alloc_kobject   STUB       <- the prediction, and it is wrong
```

```
zinit+0x5e4  bl kmem_alloc_kobject    <- predicted
zinit+0x6c8  bl snprintf              <- what the device printed
```

The two are 228 bytes apart and both inside `osfmk/kern/zalloc.c:2230-2255`:

```c
	if (kmem_alloc_ready) {
		size_t len = MIN(strlen(name)+1, MACH_ZONE_NAME_MAX_LEN);

		if (zone_names_start == 0 || ((zone_names_next - zone_names_start) + len) > PAGE_SIZE) {
			printf("zalloc: allocating memory for zone names buffer\n");
			kern_return_t retval = kmem_alloc_kobject(kernel_map, &zone_names_start,
					PAGE_SIZE, VM_KERN_MEMORY_OSFMK);      /* zinit+0x5e4 */
			...
		}

		strlcpy((char *)zone_names_next, name, len);
		z->zone_name = (char *)zone_names_next;
		zone_names_next += len;
	} else {
		z->zone_name = name;
	}

	if (num_zones_logged < max_num_zones_to_log) {
		...
		while (i <= max_num_zones_to_log) {
			snprintf(zlog_name, MAX_ZONE_NAME, "zlog%d", i);    /* zinit+0x6c8 */
```

**`kmem_alloc_kobject` is behind `if (kmem_alloc_ready)`, and `kmem_alloc_ready` is false when
`zinit` runs.** Not "probably false" — exactly three sites in the image build the address
`0x002b6570`, two of them in `zinit` and both loads, and the one store in the whole image is later in
the boot sequence than the call:

```
$ arm-none-eabi-nm -S -P out/stage90/xnu_arm_entry.elf | grep -w vm_mem_bootstrap
vm_mem_bootstrap T 23fe5c 280

  vm_mem_bootstrap+0x02c  bl zone_bootstrap
  vm_mem_bootstrap+0x03c  bl vm_object_bootstrap     <- zinit runs here, flag still 0
  vm_mem_bootstrap+0x05c  bl vm_map_init
  vm_mem_bootstrap+0x074  bl kmem_init               <- a stub; XNU sets the flag in here
  vm_mem_bootstrap+0x134  bl pmap_init
  vm_mem_bootstrap+0x148  str r1, [0x002b6570]      <- kmem_alloc_ready = 1, *after* both
```

So on this boot the `else` branch runs, `z->zone_name` gets the caller's pointer instead of a copy,
control falls past the whole `if` into the zlog block, and the first unprovided symbol it meets is
`snprintf`. The walk could not have known this: **`kmem_alloc_ready` is a value, and the walk has no
values.** Every previous miss was a wrong picture of the control flow. This one is a right picture of
the control flow and a wrong guess about which way the branch went.

## The tool was also wrong about the control flow, and that part is a bug

Knowing the flag explains the device; it does not explain why the walk called a call inside
`if (kmem_alloc_ready)` a *straight-line* call. It should have said guarded — and if it had, the walk
would have skipped it and the answer would have been `snprintf`.

It did not, and the cause is the fix for experiment 228. `by_branch` was introduced so that a loop
body entered by an explicit `b` would not be called guarded merely because the loop's exit test spans
it. The flag was then propagated along the **entire straight-line run from one unconditional `b` to
the next**, and a long run crosses conditional branches:

```
$ grep -E '^  [0-9a-f]+:\s+[0-9a-f]+\s+b\s'  zinit.dis
  zinit+0x24c  b  zinit+0x254      <- marks everything from +0x254 as "reached by a branch"
  ...
  zinit+0x564  beq zinit+0x63c     <- guard on kmem_alloc_ready, 0x80 bytes before the call
  zinit+0x5e4  bl kmem_alloc_kobject
  zinit+0x638  b  zinit+0x650      <- the run finally ends here
```

`zinit+0x5e4` sits inside the span of `beq zinit+0x63c`, so the span rule would have called it
guarded — but `_is_guarded` checks `addr not in by_branch` first, and the `b` at `+0x24c` had already
put it in `by_branch`. **The propagation now stops at the first conditional branch crossed.** After a
conditional branch the span rule is the right question again; a loop body is unaffected because its
head-to-back-edge run is straight.

Both documented retrodictions still hold after the change, and exp-234's is now exact:

```
$ python3 tools/xnu_entry_callwalk.py --elf /tmp/B228.elf --root pmap_steal_memory \
    --assume-taken pmap_steal_memory+0x130 --assume-taken pmap_enter_options+0x1054
first stub on the straight-line path: OSCompareAndSwap16        # exp-228, unchanged

$ python3 tools/xnu_entry_callwalk.py --elf /tmp/B229.elf --root memorystatus_pages_update
first stub on the straight-line path: vm_pressure_response      # exp-230, unchanged

$ python3 tools/xnu_entry_callwalk.py --elf /tmp/B230.elf --root vm_mem_bootstrap
first stub on the straight-line path: zone_bootstrap            # exp-231, unchanged

$ # on the exp-234 image, where snprintf was still unprovided:
  vm_object_bootstrap -> zinit -> snprintf                      # exp-234, now right
```

The change widens the guarded set: **778 of the current image's call sites move from unguarded to
guarded, and not one moves the other way.** That is a large number and it is recorded rather than
celebrated, because a rule that only ever adds guards is a rule that can only ever make the walk
answer "no stub" more often. On the three archived images that experiments 228 to 231 were predicted
from it changes no answer at all; on the current image it changes the plain walk's answer from
`zinit`'s `kmem_alloc_kobject` — which experiment 234 has just falsified — to `vm_mem_bootstrap`'s
`kmem_init`, which is the next unprovided symbol on that path and is consistent with the run passing
through `zinit` without stopping.

## Cost

`out/xnu_kernel_obj/osfmk_vm_vm_object.o` (`osfmk/vm/vm_object.c`) — **38320 bytes of text, 48 of
data (24 in `.data` and 24 in a `__DATA, __data` section), 1568 of `.bss`, 972 of `.rodata.str1.1`,
152 definitions, 149 references**:

```
resolved (39):
  functions (33): _vm_object_allocate  _vm_object_lock_try  vm_object_allocate
                  vm_object_bootstrap  vm_object_cache_evict  vm_object_cache_remove
                  vm_object_change_wimg_mode  vm_object_coalesce  vm_object_collapse
                  vm_object_compressor_pager_create  vm_object_copy_delayed
                  vm_object_copy_quickly  vm_object_copy_slowly  vm_object_copy_strategically
                  vm_object_deactivate_pages  vm_object_deallocate  vm_object_init
                  vm_object_lock  vm_object_lock_avoid  vm_object_lock_request
                  vm_object_lock_shared  vm_object_lock_try  vm_object_lock_yield_shared
                  vm_object_page_grab  vm_object_pmap_protect  vm_object_pmap_protect_options
                  vm_object_purgable_control  vm_object_reaper_init  vm_object_reuse_pages
                  vm_object_shadow  vm_object_transpose  vm_object_unlock  vm_page_sleep
  storage   (6):  compressor_object (D)  kernel_object (B)  vm_counters (B)
                  vm_object_lck_attr (B)  vm_object_lck_grp (B)  vm_object_zone (B)

added  (26):
  functions (25): compressor_memory_object_create  memory_object_control_allocate
                  memory_object_control_collapse  memory_object_control_disable
                  memory_object_init  memory_object_last_unmap  memory_object_reference
                  memory_object_terminate  vm_compressor_pager_next_compressed
                  vm_compressor_pager_reap_pages  vm_compressor_pager_state_clr
                  vm_compressor_pager_transfer  vm_fault_cleanup  vm_purgeable_accounting
                  vm_purgeable_nonvolatile_dequeue  vm_purgeable_object_add
                  vm_purgeable_object_remove  vm_purgeable_token_add
                  vm_purgeable_token_delete_first  vm_purgeable_token_delete_last
                  vnode_pager_get_isSSD  vnode_pager_get_object_devvp
                  vnode_pager_get_object_size  vnode_pager_get_throttle_io_limit
                  vnode_pager_issue_reprioritize_io
  storage   (1):  speculative_reads_disabled

652 -> 639 undefined          (652 + 26 - 39)
```

`652 + 26 - 39 = 639` exactly — and this is the second time the undefined count has gone **down**,
by 13 again, the same margin as experiment 232's 23 resolved against 10 added. The counters move
`562 - 33 + 25 = 554` functions and `90 - 6 + 1 = 85` storage, and 25 + 1 = 26. `speculative_reads_disabled`
was `U` in the object, which is why it arrives as a storage stub; the rest of the object's 149
references point at symbols the image already had.

| | exp-233 | now |
| --- | --- | --- |
| entry objects linked | 73 | 74 (`osfmk_vm_vm_object.o`) |
| entry text | 550353 B | **589329 B** |
| entry image | 654144 B | **686960 B** |
| entry `.bss` | 0x0029f4d0–0x002cd8c8 (189432 B) | 0x002a74e8–0x002d5d08 (**190496 B**) |
| undefined | 652 | **639** |
| stubs | 562 functions, 85 storage | **554 functions, 85 storage** |
| boot_args offset | +847872 | **+880640** |
| headroom below `topOfKernelData` | 1255224 B | **1221368 B** |
| payload text | 1146330 B | **1179146 B** |

## What is next: `snprintf`, and it is not in `vm_object.c`

`snprintf` is defined by `bsd/kern/subr_prf.c` → `out/xnu_kernel_obj/bsd_kern_subr_prf.o`, so the
step is `bsd_kern_subr_prf.o` and this is the first time the frontier has been pulled out of the VM
subsystem by a *printf*. The prediction recorded before the build was `kmem_alloc_kobject`, "the call
the last run stopped twenty bytes before", corrected in `build_entry.sh` to the deferred call. It was
never tested, because the run stopped one call short of it at `snprintf`, which is where `subr_prf.c`
comes in — the correction is that the frontier was never `kmem_alloc_kobject` at all.

## Reproduce

```bash
# the step: 39 resolved, 26 added, 652 -> 639, stubs 562fn/90st -> 554fn/85st
printf '' > /tmp/empty.c && arm-none-eabi-gcc -mcpu=cortex-a15 -marm -c /tmp/empty.c -o /tmp/empty.o
(cd stages/stage90/xnu_arm_boot && STAGE90_ENTRY_REAL_ARM_INIT=1 \
   STAGE90_ENTRY_OSFMK_VM_VM_OBJECT_OBJ=/tmp/empty.o ./build_entry.sh)
cp out/stage90/xnu_arm_entry_undef.txt /tmp/VOA.txt
(cd stages/stage90/xnu_arm_boot && STAGE90_ENTRY_REAL_ARM_INIT=1 ./build_entry.sh)
cp out/stage90/xnu_arm_entry_undef.txt /tmp/VOB.txt
comm -23 <(LC_ALL=C sort -u /tmp/VOA.txt) <(LC_ALL=C sort -u /tmp/VOB.txt) | wc -l   # 39
comm -13 <(LC_ALL=C sort -u /tmp/VOA.txt) <(LC_ALL=C sort -u /tmp/VOB.txt) | wc -l   # 26
wc -l /tmp/VOA.txt /tmp/VOB.txt                                                      # 652 and 639

# ... and it ran
./tools/host_entry_macho_check.sh
./tools/host_dt_check.sh | grep -E "tree built|RESULT"
(cd stages/stage90 && STAGE90_EXTRA_CFLAGS='-DSTAGE90_XNU_ENTRY=1' ./build.sh)
(cd stages/stage90 && ./run_and_capture.sh --allow-xnu-entry)
sed -n '/jumping to XNU/,$p' /tmp/cancro-last_kmsg.txt | head -14   # ... stub_hit=snprintf

# the flag that decides it, and its only writer
python3 tools/xnu_entry_callwalk.py --root vm_object_bootstrap        # was kmem_alloc_kobject
arm-none-eabi-nm -S -P out/stage90/xnu_arm_entry.elf | grep -w vm_mem_bootstrap
arm-none-eabi-objdump -d out/stage90/xnu_arm_entry.elf | grep -A3 -B3 '25968' | grep -B4 'str	r1, \[r0\]'
sed -n '2223,2258p' external/xnu-4570.1.46/osfmk/kern/zalloc.c
```

Nothing was flashed: `persistent_write_attempted=0x00000000` in all 25 contracts that report it,
and the device returned to Android on its own.
