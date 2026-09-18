# Experiment 232 — Two in a Row, `thread_call_setup`, and the Walk and the Root Agreeing

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
 xnu_entry_kv_written=0x0000001c
 xnu_entry_kv_in_dram=0x0000001c
 xnu_entry_csselr_before=0x00000002
 xnu_entry_ccsidr_before=0xf0ffe03b
 xnu_entry_ccsidr_l1=0xa007e01a
MI4IOS6_STAGE90_XNU real XNU entry stub_hit=thread_call_setup

No errors detected
```

`stage90_xnu_entry_stub_status=0x90000001`, `failure_mask=0x00000000` in every contract that reports
one, `safety_boundary_preserved=0x00000001`, `mmu_unchanged=0x00000001`,
`persistent_write_attempted=0x00000000` in all 25 contracts that report it, and the device returned
to Android on its own. `kv_written == kv_in_dram == 0x1c = 28 = strlen("thread_call_setup") + 11`.
No `exception:` line, and one `undef` breadcrumb, at line 3454, before the jump.

**The prediction was `thread_call_setup` and it held** — two in a row, after four misses.

## Why this one is different from experiment 231

Experiment 231's prediction was made by reading source branches because the walk could not reach
them. This one needed no such reading, and that is the substantive change:

```
$ python3 tools/xnu_entry_callwalk.py --root zone_bootstrap
walk from zone_bootstrap:
  zone_bootstrap
    thread_call_setup   STUB

$ python3 tools/xnu_entry_callwalk.py               # the plain walk from kernel_bootstrap
first stub on the straight-line path: thread_call_setup
```

**Both methods now give the same answer, and that has not happened since experiment 227.** The plain
walk reaches `zone_bootstrap` because every function between `kernel_bootstrap` and it is real, and
that is what the last five steps bought: `vm_mem_bootstrap` calls `kernel_debug_string_early`,
`vm_page_bootstrap`, `kernel_debug_string_early`, `zone_bootstrap`, and four of those five are now
real code rather than twelve-byte stubs. The lower bound and the rooted answer converge as the
straight-line path is filled in, and they converged here.

The remaining named alternative is `early_random`'s `read_erandom`, which is two guards behind
`zone_bootstrap`'s path and which a taken branch could reach first.

## Cost

`out/xnu_kernel_obj/osfmk_kern_zalloc.o` (`osfmk/kern/zalloc.c`) — **16672 bytes of text, 8 of data,
56280 of `.bss`, 1386 of `.rodata.str1.1`, 128 definitions, 72 references**. The `.bss` is the largest
single contribution this link has taken on, and it is the zone table itself.

```
resolved (23):
  functions (15): consider_zone_gc  get_largest_zone_info  get_zone_map_size
                  is_zone_map_nearing_exhaustion  zalloc  zalloc_canblock  zcram  zfree  zget
                  zinit  zone_bootstrap  zone_change  zone_free_count  zone_init
                  zone_prio_refill_configure
  storage   (8):  num_zones  panic_include_zprint  panic_kext_memory_info  panic_kext_memory_size
                  zone_array (0xd800)  zone_map  zone_map_max_address  zone_map_min_address

added  (10):      OSBacktrace  btlog_add_entry  btlog_create  btlog_remove_entries_for_element
                  kfree_nop_count  kmem_alloc_kobject  thread_call_enter
                  thread_yield_to_preemption  trace_backtrace  vm_object_zone

666 -> 653 undefined          (666 + 10 - 23)
```

`666 + 10 - 23 = 653` exactly — **the undefined count went down**, which has happened only twice
before in this sequence and never by this much. `zone_array` is the interesting one among the
resolved: a 0xd800-byte storage stub, 55296 bytes of zero, replaced by the real array.

The counters move `570 - 15 + 8 = 563` functions and `96 - 8 + 2 = 90` storage, and 8 + 2 = 10. So
ten new obligations arrived for twenty-three discharged, and eight of the ten are the zone
machinery's own: `OSBacktrace`, `btlog_add_entry`, `btlog_remove_entries_for_element`,
`trace_backtrace`, `kfree_nop_count`, `kmem_alloc_kobject`, `thread_call_enter`,
`thread_yield_to_preemption`.

| | exp-231 | now |
| --- | --- | --- |
| entry objects linked | 71 | 72 (`osfmk_kern_zalloc.o`) |
| entry text | 518801 B | **536497 B** |
| entry image | 618936 B | **635328 B** |
| entry `.bss` | 0x00296b48–0x002b5b88 (127040 B) | 0x0029ab50–0x002b9e08 (**127672 B**) |
| undefined | 666 | **653** |
| stubs | 570 functions, 96 storage | **563 functions, 90 storage** |
| boot_args offset | +749568 | **+765952** |
| headroom below `topOfKernelData` | 1352824 B | **1335800 B** |
| payload text | 1111122 B | **1127514 B** |

The `.bss` grew by only 632 bytes for a 56280-byte contribution, because `zone_array` and most of the
rest were already reserved as stubs. The image grew by 16392 bytes.

## What is next: `thread_call_setup`, and the prediction is `vm_object_bootstrap`

The frontier is `thread_call_setup`, defined by `osfmk/kern/thread_call.c` →
`out/xnu_kernel_obj/osfmk_kern_thread_call.o` — **12600 bytes of text, 2432 of data, 61769 of `.bss`,
1451 of `.rodata.str1.1`, 77 definitions, 47 references**. The second-largest `.bss` contribution
after `zalloc`'s, and like it, zeroed by the payload rather than stored.

**The prediction is `stub_hit=vm_object_bootstrap`**, and it is the plain walk's answer, not the
rooted one. `--root thread_call_setup` finds no stub — the six symbols this object resolves are the
whole of `thread_call_setup`'s straight-line reach — so the run resumes in `zone_bootstrap` after the
call, `zone_bootstrap` returns, and `vm_mem_bootstrap` calls `kernel_debug_string_early` (real) and
then `vm_object_bootstrap` at `+0x3c`, which is a stub. The two methods are not agreeing here because
only one of them has anything to say; where they agree is the useful signal, and this step it is
absent in a way that is itself informative.

`vm_object_bootstrap` is defined by `osfmk/vm/vm_object.c`, so the step after this one is
`osfmk_vm_vm_object.o`.

## Reproduce

```bash
# the step: 23 resolved, 10 added, 666 -> 653, stubs 570fn/96st -> 563fn/90st
printf '' > /tmp/empty.c && arm-none-eabi-gcc -mcpu=cortex-a15 -marm -c /tmp/empty.c -o /tmp/empty.o
(cd stages/stage90/xnu_arm_boot && STAGE90_ENTRY_REAL_ARM_INIT=1 \
   STAGE90_ENTRY_OSFMK_KERN_ZALLOC_OBJ=/tmp/empty.o ./build_entry.sh)
cp out/stage90/xnu_arm_entry_undef.txt /tmp/A.txt
(cd stages/stage90/xnu_arm_boot && STAGE90_ENTRY_REAL_ARM_INIT=1 ./build_entry.sh)
cp out/stage90/xnu_arm_entry_undef.txt /tmp/B.txt
comm -23 <(LC_ALL=C sort -u /tmp/A.txt) <(LC_ALL=C sort -u /tmp/B.txt) | wc -l   # 23
comm -13 <(LC_ALL=C sort -u /tmp/A.txt) <(LC_ALL=C sort -u /tmp/B.txt) | wc -l   # 10
wc -l /tmp/A.txt /tmp/B.txt                                                      # 666 and 653

# ... and it ran
./tools/host_entry_macho_check.sh
./tools/host_dt_check.sh | grep -E "tree built|RESULT"
(cd stages/stage90 && STAGE90_EXTRA_CFLAGS='-DSTAGE90_XNU_ENTRY=1' ./build.sh)
(cd stages/stage90 && ./run_and_capture.sh --allow-xnu-entry)
sed -n '/jumping to XNU/,$p' /tmp/cancro-last_kmsg.txt | head -14   # ... stub_hit=thread_call_setup

# the two methods agreeing
python3 tools/xnu_entry_callwalk.py --root zone_bootstrap
python3 tools/xnu_entry_callwalk.py
arm-none-eabi-nm -S -P out/xnu_kernel_obj/osfmk_kern_zalloc.o | grep -w zone_array
```

Nothing was flashed: `persistent_write_attempted=0x00000000` in all 25 contracts that report it,
and the device returned to Android on its own.
