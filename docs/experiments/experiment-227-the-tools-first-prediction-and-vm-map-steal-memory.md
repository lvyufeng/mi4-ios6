# Experiment 227 — the Tool's First Prediction, `vm_map_steal_memory`, and the NEON That Waited Two Experiments

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
MI4IOS6_STAGE90_XNU real XNU entry stub_hit=vm_map_steal_memory

No errors detected
```

`stage90_xnu_entry_stub_status=0x90000001`, `failure_mask=0x00000000` in every contract that reports
one, `safety_boundary_preserved=0x00000001`, `mmu_unchanged=0x00000001`,
`persistent_write_attempted=0x00000000` in all 25 contracts that report it, and the device returned
to Android on its own. `kv_written == kv_in_dram == 0x1e = 30 = strlen("vm_map_steal_memory") + 11`.
No `exception:` line, and one `undef` breadcrumb, at line 3454, before the jump.

**The prediction was `stub_hit=vm_map_steal_memory` and it held** - and it is the first prediction in
this sequence that was not made by hand. It came from `tools/xnu_entry_callwalk.py`, run against the
image with `osfmk_vm_vm_init.o` linked, before the device was touched.

## What the prediction now rests on

The tool walks the transitive call graph of the linked ELF from `kernel_bootstrap`, following direct
`bl` and tail `b` and skipping calls inside conditional blocks, and names the first stub it reaches.
For this step that walk is:

```
 kernel_bootstrap
   vm_mem_bootstrap
     vm_page_bootstrap
       vm_page_init_lck_grp
         vm_compressor_init_locks     ; exp-226's stop, now real - 88 bytes, four calls, all real
       lck_mtx_init_ext x3
       PE_parse_boot_argn
       __bzero x4
       kernel_debug_string_early
       vm_map_steal_memory            ; STUB at 0x002468bc - the stop
```

and it is a *stronger* statement than the hand reading it replaces in two ways that are worth
separating, because only one of them is about depth:

- it walks **all 27 of `vm_page_bootstrap`'s calls**, not the five that fitted in a truncated window;
- it reports that **no call on this path is inside a conditional block**, which is what makes the
  answer a prediction rather than an upper bound. The 128 calls the walk does step over are listed,
  and `vm_page_bootstrap` is not among them.

The path itself is the one experiment 226's hand prediction was aiming at: the same
`kernel_bootstrap` → `vm_mem_bootstrap` → `vm_page_bootstrap` chain, one call further along.

## The NEON that waited two experiments executed

Experiment 225's document predicted that `vm_page_bootstrap`'s NEON - `vld1.64 {d16-d17}, [r1 :128]`,
`vdup.32 q11, r0`, `vshl.s32 q12, q8, #3`, and a looping `vst2.32 {d24-d27}, [r1 :64]!` - would run
"on the very next step's path". Experiment 226 stopped before it, inside `vm_page_init_lck_grp`, at
`0x00241330`, which is before the first `vld1.64` at `0x00218c30`. This run stopped at `0x002468bc`,
which is past every one of them, so they executed - and `grep -c 'exception: '` is 0.

That matters for a reason beyond the count: an *aligning* NEON store, `vst2.32 ... :64!`, is the
first instruction in this project whose correctness depends on the *address* it is handed rather than
only on the coprocessor being enabled. It is being handed `0x0026c038 + r7` with `r7` stepping by 64,
which is 8-byte aligned as `:64` requires, and the `:128` load source is the literal table at
`0x00218dc0`, which is 16-byte aligned. Both were read before the run and neither faulted; the log
having no `exception: data abort` is the measurement that says so.

So the two NEON questions this project had open are now closed, one experiment apart:

| | first NEON | this run's NEON |
| --- | --- | --- |
| where | `kernel_debug_string_early`, exp-225 | `vm_page_bootstrap`, exp-227 |
| what it needed | CPACR's CP10/CP11 and `FPEXC.EN`, set by `start.s`'s `join_start` | the same, plus alignment the compiler arranged |
| evidence | no `exception: undefined instruction` | no `exception: data abort` |

## Cost

`out/xnu_kernel_obj/osfmk_vm_vm_compressor.o` (`osfmk/vm/vm_compressor.c`) - **23260 bytes of text,
144 of data, 16248 of `.bss`, 206 definitions, 100 references**:

```
resolved (2):  c_master_lock                  (storage stub)
               vm_compressor_init_locks       (function stub)

added  (36):   clock_get_uptime compaction_swapper_inited hibernate_in_progress_with_pinned_swap
               hibernate_should_abort kernel_memory_depopulate kernel_memory_populate memoryshot
               memorystatus_kill_on_FC_thrashing memorystatus_kill_on_VM_thrashing
               metacompressor metadecompressor proc_set_thread_policy thread_vm_bind_group_add
               vm_compaction_swapper_do_init vm_compressor_algorithm vm_compressor_algorithm_init
               vm_compressor_get_decode_scratch_size vm_compressor_swap_init vm_num_pinned_swap_files
               vm_num_swap_files vm_pageout_internal_start vm_pageout_io_throttle
               vm_pageout_queue_external vm_restricted_to_single_processor
               vm_swap_consider_defragmenting vm_swap_files_pinned vm_swapfile_total_segs_alloced
               vm_swapfile_total_segs_used vm_swap_free vm_swap_get vm_swappin_enabled
               vm_swap_put_failures WKdm_compress_new WKdm_decompress_new zone_map_max_address
               zone_map_min_address

519 -> 553 undefined          (519 + 36 - 2)
```

24 of the 36 are function stubs and 12 are storage - `519 + 36 - 2 = 553`, `438 - 1 + 24 = 461`
functions and `81 - 1 + 12 = 92` storage. The object defines 206 symbols and the image had heard of
only two of them, so almost all of this step is text and `.bss` that arrived and will not be reached:
the whole WKdm compressor, the swapfile accounting, the memorystatus kill paths.

| | exp-226 | now |
| --- | --- | --- |
| entry objects linked | 66 | 67 (`osfmk_vm_vm_compressor.o`) |
| entry text | 320708 B | **344804 B** |
| entry image | 421240 B | **454152 B** |
| entry `.bss` | 0x00266888–0x00280688 (105984 B) | 0x0026e8b8–0x0028c988 (**123088 B**) |
| undefined | 519 | **553** |
| stubs | 438 functions, 81 storage | **461 functions, 92 storage** |
| boot_args offset | +532480 | **+581632** |
| headroom below `topOfKernelData` | 1571192 B | **1521272 B** |
| payload text | 913426 B | **946338 B** |

`.bss` grew by 17104 bytes - the object's own 16248 plus the storage stubs that arrive with it - and
the boot_args offset moved a further 48 KB down. The headroom is still 1.45 MB, so nothing is close
to a limit, but this is the second consecutive step in which the image's window moved.

## The three-term decomposition, third test

```
symbol region    290612 -> 312536          +21924
.text            320708 -> 344804          +24096
                                    gaps + tail  +2172
```

```
vm_compressor_init_locks     0xc -> 0x58        +76     (the 12-byte stub, minus 12)

48 new real functions defined by the object   +21560
24 new 12-byte function stubs    24 * 12       +288
                                             ------
                                             +21924
```

The 24 and the one are the same numbers the stub counters produced independently, for the third
experiment running.

## What is next: `vm_map_steal_memory`, and the prediction is `zone_bootstrap`

The frontier is `vm_map_steal_memory`, defined by `out/xnu_kernel_obj/osfmk_vm_vm_map.o`
(`osfmk/vm/vm_map.c`) - **76439 bytes of text, 52 of data, 416 of `.bss`, 202 definitions, 162
references**, nearly three times the largest object linked before it. The function itself is 120
bytes and its three calls are all `pmap_steal_memory`, which is already real, so it completes.

**The prediction is `stub_hit=zone_bootstrap`**, and it is the tool's answer again. It is also,
exactly, what experiment 226's hand reading predicted - arrived at the long way round, by walking
`vm_page_bootstrap`'s 27 calls and every call inside every callee. The reading was not wrong about
where the path goes; it was wrong about how much of the path it had looked at.

`zone_bootstrap` is defined by `osfmk/kern/zalloc.c`, so the step after this one is
`osfmk_kern_zalloc.o`.

## Reproduce

```bash
# the step: 2 resolved, 36 added, 519 -> 553, stubs 438fn/81st -> 461fn/92st
printf '' > /tmp/empty.c && arm-none-eabi-gcc -mcpu=cortex-a15 -marm -c /tmp/empty.c -o /tmp/empty.o
(cd stages/stage90/xnu_arm_boot && STAGE90_ENTRY_REAL_ARM_INIT=1 \
   STAGE90_ENTRY_OSFMK_VM_VM_COMPRESSOR_OBJ=/tmp/empty.o ./build_entry.sh)
cp out/stage90/xnu_arm_entry_undef.txt /tmp/A.txt
(cd stages/stage90/xnu_arm_boot && STAGE90_ENTRY_REAL_ARM_INIT=1 ./build_entry.sh)
cp out/stage90/xnu_arm_entry_undef.txt /tmp/B.txt
comm -23 <(sort /tmp/A.txt) <(sort /tmp/B.txt)   # 2 resolved
comm -13 <(sort /tmp/A.txt) <(sort /tmp/B.txt)   # 36 added
wc -l /tmp/A.txt /tmp/B.txt                      # 519 and 553

# ... and it ran
./tools/host_entry_macho_check.sh
./tools/host_dt_check.sh | grep -E "tree built|RESULT"
(cd stages/stage90 && STAGE90_EXTRA_CFLAGS='-DSTAGE90_XNU_ENTRY=1' ./build.sh)
(cd stages/stage90 && ./run_and_capture.sh --allow-xnu-entry)
sed -n '/jumping to XNU/,$p' /tmp/cancro-last_kmsg.txt | head -14   # ... stub_hit=vm_map_steal_memory
grep -c 'exception: ' /tmp/cancro-last_kmsg.txt                     # 0: the aligning NEON did not fault

# the prediction, and the path it was taken from
python3 tools/xnu_entry_callwalk.py                                  # zone_bootstrap (exp-228)
python3 tools/xnu_entry_callwalk.py --elf /tmp/A.elf                 # vm_compressor_init_locks (exp-226)
arm-none-eabi-objdump -d --start-address=0x00218c20 --stop-address=0x00218c40 out/stage90/xnu_arm_entry.elf
```

Nothing was flashed: `persistent_write_attempted=0x00000000` in all 25 contracts that report it,
and the device returned to Android on its own.
