# Experiment 233 — Three in a Row, `vm_object_bootstrap`, and What the Walk Reaches Now

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
 xnu_entry_kv_written=0x0000001e
 xnu_entry_kv_in_dram=0x0000001e
 xnu_entry_csselr_before=0x00000002
 xnu_entry_ccsidr_before=0xf0ffe03b
 xnu_entry_ccsidr_l1=0xa007e01a
MI4IOS6_STAGE90_XNU real XNU entry stub_hit=vm_object_bootstrap

No errors detected
```

`stage90_xnu_entry_stub_status=0x90000001`, `failure_mask=0x00000000` in every contract that reports
one, `safety_boundary_preserved=0x00000001`, `mmu_unchanged=0x00000001`,
`persistent_write_attempted=0x00000000` in all 25 contracts that report it, and the device returned
to Android on its own. `kv_written == kv_in_dram == 0x1e = 30 = strlen("vm_object_bootstrap") + 11`.
No `exception:` line, and one `undef` breadcrumb, at line 3454, before the jump.

**The prediction was `vm_object_bootstrap` and it held** — three in a row.

## The frontier has moved out of the four functions it has been in since experiment 227

Every stop from experiment 227 to experiment 232 was inside one of four adjacent functions:
`vm_page_bootstrap`, `pmap_startup`, `memorystatus_pages_update` and `zone_bootstrap`. This one is
inside `vm_mem_bootstrap` itself — the caller — which is the first time the frontier has stepped out
of that cluster.

```
 kernel_bootstrap
   vm_mem_bootstrap                     <-- the stop is in this frame
     kernel_debug_string_early   real
     vm_page_bootstrap           real   (exp-227 through exp-231 were all inside this)
     kernel_debug_string_early   real
     zone_bootstrap              real   (exp-232 was inside this)
     kernel_debug_string_early   real
     vm_object_bootstrap         STUB   <-- the stop
```

Seven calls of `vm_mem_bootstrap` now execute in real code, four of them resolved in the last three
experiments, and the frame that used to be three levels above the action is now where the action is.
The `+0x3c` offset in that call list has been quoted in three consecutive documents as the thing
behind the next corner; it is now in front of the run.

## Cost

`out/xnu_kernel_obj/osfmk_kern_thread_call.o` (`osfmk/kern/thread_call.c`) — **12600 bytes of text,
2432 of data, 61769 of `.bss`, 1451 of `.rodata.str1.1`, 77 definitions, 47 references**:

```
resolved (6):  thread_call_allocate  thread_call_cancel  thread_call_enter
               thread_call_enter_delayed  thread_call_initialize  thread_call_setup

added  (5):    absolutetime_to_continuoustime  continuoustime_to_absolutetime
               waitq_assert_wait64  waitq_init  waitq_wakeup64_one

653 -> 652 undefined          (653 + 5 - 6)
```

`653 + 5 - 6 = 652` exactly, and all six resolved and all five added are function stubs, so the
counters move `563 - 6 + 5 = 562` functions and storage does not move at all. This is the smallest
step by undefined count in either direction since experiment 222 — six obligations discharged and
five taken on.

What it costs in bytes is another matter. The object brings **61769 bytes of `.bss`**, the second
largest single contribution after `zalloc`'s, and unlike `zalloc`'s it is not mostly already reserved:

| | exp-232 | now |
| --- | --- | --- |
| entry objects linked | 72 | 73 (`osfmk_kern_thread_call.o`) |
| entry text | 536497 B | **550353 B** |
| entry image | 635328 B | **654144 B** |
| entry `.bss` | 0x0029ab50–0x002b9e08 (127672 B) | 0x0029f4d0–0x002cd8c8 (**189432 B**) |
| undefined | 653 | **652** |
| stubs | 563 functions, 90 storage | **562 functions, 90 storage** |
| boot_args offset | +765952 | **+847872** |
| headroom below `topOfKernelData` | 1335800 B | **1255224 B** |
| payload text | 1127514 B | **1146330 B** |

The `.bss` grew by 61760 bytes, essentially the whole of the object's own, and the boot_args offset
moved 81920 further down. **The headroom is now 1255224 bytes** — it was 1588216 when this stretch of
work began at experiment 224, so seven steps have consumed 333 KB of the 2 MB window. Still 1.2 MB
free, and the trend is worth stating plainly: if the remaining objects average what the last seven
have, roughly twenty-six more steps of this size fit before `topOfKernelData` is reached. Most of the
remaining objects are larger than these, so the real number is smaller, and the point at which that
matters is visible now rather than at the end.

## What is next: `vm_object_bootstrap`, and the prediction is `kmem_alloc_kobject`

The frontier is `vm_object_bootstrap`, defined by `osfmk/vm/vm_object.c` →
`out/xnu_kernel_obj/osfmk_vm_vm_object.o` — **38320 bytes of text, 48 of data (24 in `.data` and 24
in a `__DATA, __data` section), 1568 of `.bss`, 972 of `.rodata.str1.1`, 152 definitions, 149
references**.

**The prediction is `stub_hit=kmem_alloc_kobject`, and it is a correction.** Written before the build,
the prediction was `kmem_init`, taken from `vm_mem_bootstrap`'s own call list — which is the list that
named this experiment's stop correctly, and which reads

```
+0x02c zone_bootstrap        real
+0x03c vm_object_bootstrap   STUB  <- exp-233's stop
+0x05c vm_map_init           real
+0x074 kmem_init             STUB  <- what the call list predicted
```

That list is the *caller's* view, and it is wrong for the same reason experiment 230's prediction was
wrong: the run stops at the frontier, so what happens next is what the frontier's body does. Asking
the tool that question gives a different answer:

```
$ python3 tools/xnu_entry_callwalk.py --root vm_object_bootstrap
walk from vm_object_bootstrap:
  vm_object_bootstrap
    zinit
      kmem_alloc_kobject   STUB
```

`vm_object_bootstrap` calls `zinit` on its second statement, and `zinit` allocates through
`kmem_alloc_kobject`. So the front is one level in and one level further down, and **the prediction is
`kmem_alloc_kobject`** — which the plain walk from `kernel_bootstrap` also names, the first time the
plain walk has been able to agree with the rooted one for two steps running.

The correction is recorded rather than quietly made because it is the fifth time this method has been
wrong in the same direction, and the first time the wrong answer was caught *before* the device was
touched. That is the difference the `build_entry.sh` comment is written to make: an argument made in
prose that names what would falsify it, checked against the tool, and corrected in place if it is.

`kmem_alloc_kobject` is defined by `osfmk/vm/vm_kern.c`, so if the prediction holds the step after
this one is `osfmk_vm_vm_kern.o`.

## Reproduce

```bash
# the step: 6 resolved, 5 added, 653 -> 652, stubs 563fn/90st -> 562fn/90st
printf '' > /tmp/empty.c && arm-none-eabi-gcc -mcpu=cortex-a15 -marm -c /tmp/empty.c -o /tmp/empty.o
(cd stages/stage90/xnu_arm_boot && STAGE90_ENTRY_REAL_ARM_INIT=1 \
   STAGE90_ENTRY_OSFMK_KERN_THREAD_CALL_OBJ=/tmp/empty.o ./build_entry.sh)
cp out/stage90/xnu_arm_entry_undef.txt /tmp/A.txt
(cd stages/stage90/xnu_arm_boot && STAGE90_ENTRY_REAL_ARM_INIT=1 ./build_entry.sh)
cp out/stage90/xnu_arm_entry_undef.txt /tmp/B.txt
comm -23 <(LC_ALL=C sort -u /tmp/A.txt) <(LC_ALL=C sort -u /tmp/B.txt) | wc -l   # 6
comm -13 <(LC_ALL=C sort -u /tmp/A.txt) <(LC_ALL=C sort -u /tmp/B.txt) | wc -l   # 5
wc -l /tmp/A.txt /tmp/B.txt                                                      # 653 and 652

# ... and it ran
./tools/host_entry_macho_check.sh
./tools/host_dt_check.sh | grep -E "tree built|RESULT"
(cd stages/stage90 && STAGE90_EXTRA_CFLAGS='-DSTAGE90_XNU_ENTRY=1' ./build.sh)
(cd stages/stage90 && ./run_and_capture.sh --allow-xnu-entry)
sed -n '/jumping to XNU/,$p' /tmp/cancro-last_kmsg.txt | head -14   # ... stub_hit=vm_object_bootstrap

# the frame the stop is in, and the call the next prediction names
python3 tools/xnu_entry_callwalk.py --root thread_call_setup
python3 tools/xnu_entry_callwalk.py
python3 tools/xnu_entry_callwalk.py --root vm_object_bootstrap
```

Nothing was flashed: `persistent_write_attempted=0x00000000` in all 25 contracts that report it,
and the device returned to Android on its own.
