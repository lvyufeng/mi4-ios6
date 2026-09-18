# Experiment 225 — the First NEON Executes, `kernel_debug_string_early` Runs, and the Frontier Is `vm_mem_bootstrap`

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
 xnu_entry_kv_written=0x0000001b
 xnu_entry_kv_in_dram=0x0000001b
 xnu_entry_csselr_before=0x00000002
 xnu_entry_ccsidr_before=0xf0ffe03b
 xnu_entry_ccsidr_l1=0xa007e01a
MI4IOS6_STAGE90_XNU real XNU entry stub_hit=vm_mem_bootstrap

No errors detected
```

`stage90_xnu_entry_stub_status=0x90000001`, `failure_mask=0x00000000` in every contract that reports
one, `safety_boundary_preserved=0x00000001`, `mmu_unchanged=0x00000001`,
`persistent_write_attempted=0x00000000` in all 25 contracts that report it, and the device returned
to Android on its own. `kv_written == kv_in_dram == 0x1b = 27 = strlen("vm_mem_bootstrap") + 11`.
No `exception:` line, and one `undef` breadcrumb, at line 3454, before the jump.

The prediction was `stub_hit=vm_mem_bootstrap` and it held - the eleventh consecutive prediction made
from the disassembly rather than from a relocation list, and it was the first one that came with a
second, independent statement of itself: the argument the stop received is the string
`"vm_mem_bootstrap"`.

## The first NEON code in this project's history ran, and nothing trapped

This is the part of the run that measured something new rather than confirming something read. With
`kernel_debug_string_early` linked, the run executed the top of its body:

```
     dc8: push  {r4, lr}
     dcc: sub   sp, sp, #16
     dd0: vmov.i32 q8, #0                <-- NEON, unconditional
     dd8: mov   r0, sp
     ddc: vst1.64 {d16-d17}, [r0]        <-- NEON, unconditional
     de4: bl    strlen
     e08: bl    strncpy
     e10: pop   {r4, pc}
```

Two NEON instructions, on the first instructions of the function, with no condition in front of
them - unlike `cc_cmp_safe`'s vector path, which needs a length of 32 and has never been taken. So
experiment 220's open question ("whether CPACR/FPEXC allow it in this CPU state is a question nothing
has asked yet") has an answer, and the answer is the absence of a line: `grep -c 'exception: '` is 0.
An undefined-instruction trap would have reached `fleh_undef` and written `exception: undefined
instruction`, which is exactly the outcome that was on the table.

The evidence for *why* is in the image, and it is worth separating from the evidence that it
happened. `osfmk/arm/start.s`:

```
200354: mrc p15, 0, r2, c1, c0, 2     ; read CPACR
200358: mov r3, #15
20035c: orr r2, r2, r3, lsl #20       ; 0xF << 20: coprocessors 10 and 11 to full access
200360: mcr p15, 0, r2, c1, c0, 2
200368: cmp r1, #0
20036c: beq 2003a0 <join_start_1>     ; taken when invoked from _start
2003a0: vmrs r2, fpexc / orr r2, r2, #0x40000000 / vmsr fpexc, r2
```

`_start` reaches `join_start` by a direct branch (`200070: b 2002e4`), and `arm_init` - which
experiment 222's run demonstrably reached - is called from `start.s` after `join_start` returns. So
the CP10/CP11 grant and `FPEXC.EN` were both in place before `kernel_bootstrap` ran, and the run is
what says so rather than the source alone.

**This is a durable fact about the image, not about this step.** `vm_page_bootstrap`
(`osfmk/vm/vm_resident.c`, 888 bytes at `0x00218b50`) is already linked and already contains NEON -
`vld1.64 {d16-d17}, [r1 :128]`, `vdup.32 q11, r0`, `vshl.s32 q12, q8, #3`, `vst2.32 {d24-d27}, [r1 :64]!`
- including an aligning store in a loop. It is on the very next step's path, and the alignment
requirements are met by construction: the `vld1.64 ... :128` source is the literal table at
`0x00218dc0` (r1 = `pc + 400`, 16-byte aligned), and the `vst2.32 ... :64!` target is
`0x0026c038 + r7` with `r7` stepping by 64, 8-byte aligned. Nothing has to be decided about it; it is
recorded here so that the run which executes it is read correctly.

## The collision the object list did not predict

`bsd_kern_kdebug.o` defines `kdebug_enable`, and `entry_stubs.c` has defined its own stand-in since
before `arm_init` was real - it is there because `osfmk/arm/start.s` references `_kdebug_enable` by
name, long before anything in the image could care what the variable *is*. Nothing in the previous
three hundred experiments had linked the object that owns the name, so nothing had ever said so. The
link said it at once:

```
arm-none-eabi-ld: bsd_kern_kdebug.o:(.bss+0x0): multiple definition of `kdebug_enable';
  xnu_arm_entry_stubs.o:entry_stubs.c:(.bss+0x440): first defined here
```

The stand-in is now under `STAGE90_ENTRY_REAL_KDEBUG_ENABLE`, compiled out with the real objects, the
same way `EntropyData`, `panic` and `_consume_kprintf_args` are.

**Unlike `EntropyData`, the replacement is equivalent, and that is worth stating rather than
implying.** `bsd/kern/kdebug.c:310` is `unsigned int kdebug_enable = 0;`, `nm -S -P` reports it
`B 0 4`, and the stand-in is a zero-initialized `uint32_t`. Same size, same type, same value.
`bsd/sys/kdebug.h:1034` declares the same `unsigned int`, and `osfmk/kern/debug.c:609` reads it. So
there is no measurement here that the stand-in could have got wrong, and none was lost by retiring
it.

## Cost

`out/xnu_kernel_obj/bsd_kern_kdebug.o` (`bsd/kern/kdebug.c`) - **21729 bytes of text, 312 of data,
136 of `.bss`, 112 definitions, 104 references**, the largest object this link has taken on:

```
resolved (9):  kdbg_dump_trace_to_file  kdbg_trace_data  kdbg_trace_string  kdebug_debugid_enabled
               kdebug_free_early_buf    kdebug_init      kernel_debug       kernel_debug_string_early
                                                                          (8 function stubs)
               kdebug_enable                                              (1 storage stub)

added  (43):   clock_get_calendar_microtime commpage_update_kdebug_state current_map fp_drop fp_lookup
               get_bsdtask_info host_info IOSleep kperf_kdebug_handler ktrace_assert_lock_held
               ktrace_configure ktrace_end_single_threaded ktrace_get_owning_pid ktrace_kernel_configure
               ktrace_lock ktrace_read_check ktrace_reset ktrace_start_single_threaded ktrace_unlock
               mach_continuous_time mach_to_bsd_errno mach_vm_deallocate proc_best_name proc_fdlock
               proc_fdunlock proc_rele snprintf strtoul sync sysctl_handle_quad task_act_iterate_wth_args
               task_reference thread_clear_eager_preempt thread_set_eager_preempt vfs_context_kernel
               vfs_context_proc vfs_context_ucred vnode_close vnode_getwithref vnode_open VNOP_FSYNC
               vn_rdwr                                                       (42 function stubs)
               kperf_kdebug_active                                           (1 storage stub)

472 -> 506 undefined          <-- 471 + 1 for the retired stand-in, then + 43 - 9
```

The four counts agree, as in experiment 224: 8 function stubs resolved and 42 added give
`392 - 8 + 42 = 426` functions; 1 storage stub resolved and 1 added give `80 - 1 + 1 = 80`; and
`8 + 42 = 50` function stubs and `1 + 1 = 2` storage stubs make 52 of the 43 + 9 = 52 symbols that
moved. The empty-object build reports 472 rather than experiment 224's 471 **only** because
`kdebug_enable`'s stand-in is gone - the +1 is the retirement, not the object, and it is the one
number in this table that would be misread as a regression.

| | exp-224 | now |
| --- | --- | --- |
| entry objects linked | 64 | 65 (`bsd_kern_kdebug.o`) |
| entry text | 296600 B | **319332 B** |
| entry image | 404464 B | **421240 B** |
| entry `.bss` | 0x00262730–0x0027c408 (105688 B) | 0x00266888–0x00280648 (**105920 B**) |
| undefined | 471 | **506** |
| stubs | 392 functions, 79 storage | **426 functions, 80 storage** |
| boot_args offset | +516096 | **+532480** |
| headroom below `topOfKernelData` | 1588216 B | **1571256 B** |
| payload text | 896650 B | **913426 B** |

## The three-term decomposition, second test, and it still closes

```
symbol region    267988 -> 289816          +21828
.text            296600 -> 319332          +22732
                                    gaps + tail  +904
```

and the symbol region itself, every term measured:

```
8 stubs -> real:

  kdbg_dump_trace_to_file      0xc -> 0x1c8     +456
  kdbg_trace_data              0xc -> 0x3c       +48
  kdbg_trace_string            0xc -> 0x90      +132
  kdebug_debugid_enabled       0xc -> 0xf4      +232
  kdebug_free_early_buf        0xc -> 0x10        +4
  kdebug_init                  0xc -> 0x2c       +32
  kernel_debug                 0xc -> 0x4c       +64
  kernel_debug_string_early    0xc -> 0x4c       +64
  kdebug_enable                0x4 -> 0x4          0     (storage, unchanged type and size)
                                              ------
                                               +1020   net of the 8 x 12-byte stubs

52 new real functions defined by the object             +20304
42 new 12-byte function stubs      42 * 12              +504
                                                     ------
                                                     +21828
```

The 42 and the 8 are the same numbers the stub counters produced by an independent route, and they
agree to the byte for the second experiment running.

## What is next: `vm_mem_bootstrap`, and the prediction is `zone_bootstrap`

The frontier is `vm_mem_bootstrap`, defined by `out/xnu_kernel_obj/osfmk_vm_vm_init.o`
(`osfmk/vm/vm_init.c`) - **994 bytes of text, no data, 24 of `.bss`, 29 definitions, 24 references**.
`vm_mem_bootstrap` itself is 280 bytes and is a straight run of initialisation calls, each preceded by
`kernel_debug_string_early` of its name, which is now real:

```
  10: bl kernel_debug_string_early
  1c: bl vm_page_bootstrap(sp+12, sp+8)     <-- real: 888 bytes at 0x00218b50
  28: bl kernel_debug_string_early
  2c: bl zone_bootstrap                     <-- ABSENT from the image, and not referenced by it
  ...  (vm_object_bootstrap, vm_map_init, kmem_init, vm_allocate_kernel, pmap_init, kext_alloc_init,
        zone_init, vm_page_module_init, kalloc_init, vm_fault_init, memory_manager_default_init,
        memory_object_control_bootstrap, device_pager_bootstrap, vm_paging_map_init)
```

**The prediction is `stub_hit=zone_bootstrap`**, and the reasoning is one step deeper than usual
because `vm_page_bootstrap` is already in the image: the prediction is not "the first call in
`vm_mem_bootstrap` that is missing" but "the first call *anywhere on the executed path* that is
missing". Two facts settle it:

1. **`vm_page_bootstrap` has no stubbed dependency at all.** Its whole body is five calls -
   `__bzero`, `vm_page_init_lck_grp`, and `lck_mtx_init_ext` three times - every one of them already
   real in this image, and it contains **no indirect call** (`grep -c 'blx\|ldr pc'` is 0). Its only
   branches are one unconditional `b` and one `bne`, both of which form the NEON loop above and
   neither of which skips a call. So it runs to completion and returns.

2. **`zone_bootstrap` is absent from the image *and* currently unreferenced by it** - which is a
   stronger statement than "undefined", because `vm_mem_bootstrap` is a generated stub today, so the
   object that calls `zone_bootstrap` is not linked at all. Linking `osfmk_vm_vm_init.o` creates the
   reference, the stub generator creates the stub, and the run stops on it. The same is true of
   `vm_object_bootstrap`, `vm_map_init`, `kmem_init`, `vm_allocate_kernel`, `kext_alloc_init`,
   `zone_init`, `kalloc_init`, `vm_fault_init`, `memory_manager_default_init`,
   `memory_object_control_bootstrap`, `device_pager_bootstrap` and `vm_paging_map_init`.

The contrast with experiment 221 is worth recording, because it is the same shape of prediction and
it is the reason the method is written down: there, the prediction was a *list-walk* through the
caller's own calls. Here it is a list-walk through a function that the caller calls, which requires
that function to be disassembled rather than assumed. `vm_page_bootstrap`'s word size said 888 bytes
and 888 bytes is a lot of places for a stub to hide.

## Reproduce

```bash
# the step: 9 resolved, 43 added, 472 -> 506 (471 + 1 for the retired stand-in)
printf '' > /tmp/empty.c && arm-none-eabi-gcc -mcpu=cortex-a15 -marm -c /tmp/empty.c -o /tmp/empty.o
(cd stages/stage90/xnu_arm_boot && STAGE90_ENTRY_REAL_ARM_INIT=1 \
   STAGE90_ENTRY_BSD_KERN_KDEBUG_OBJ=/tmp/empty.o ./build_entry.sh)
cp out/stage90/xnu_arm_entry_undef.txt /tmp/A.txt
(cd stages/stage90/xnu_arm_boot && STAGE90_ENTRY_REAL_ARM_INIT=1 ./build_entry.sh)
cp out/stage90/xnu_arm_entry_undef.txt /tmp/B.txt
comm -23 <(sort /tmp/A.txt) <(sort /tmp/B.txt)   # 9 resolved
comm -13 <(sort /tmp/A.txt) <(sort /tmp/B.txt)   # 43 added
wc -l /tmp/A.txt /tmp/B.txt                      # 472 and 506

# ... and it ran
./tools/host_entry_macho_check.sh
./tools/host_dt_check.sh | grep -E "tree built|RESULT"
(cd stages/stage90 && STAGE90_EXTRA_CFLAGS='-DSTAGE90_XNU_ENTRY=1' ./build.sh)
(cd stages/stage90 && ./run_and_capture.sh --allow-xnu-entry)
sed -n '/jumping to XNU/,$p' /tmp/cancro-last_kmsg.txt | head -14   # ... stub_hit=vm_mem_bootstrap
grep -c 'exception: ' /tmp/cancro-last_kmsg.txt                     # 0: the NEON did not trap

# the collision, and why the replacement is equivalent
arm-none-eabi-nm -S -P out/xnu_kernel_obj/bsd_kern_kdebug.o | grep -w kdebug_enable   # B 0 4
grep -n 'kdebug_enable' external/xnu-4570.1.46/bsd/kern/kdebug.c | head -2            # :310

# why the next stop is zone_bootstrap, one function deeper than the caller
arm-none-eabi-size out/xnu_kernel_obj/osfmk_vm_vm_init.o
arm-none-eabi-nm -u out/xnu_kernel_obj/osfmk_vm_vm_init.o | grep -cw zone_bootstrap   # 0: this object does not define it
arm-none-eabi-objdump -d --start-address=0x00218b50 --stop-address=0x00218ec8 out/stage90/xnu_arm_entry.elf \
  | awk '{print $3}' | grep -E '^b' | sort | uniq -c      # 5 bl, 1 b, 1 bne - no indirect call
```

Nothing was flashed: `persistent_write_attempted=0x00000000` in all 25 contracts that report it,
and the device returned to Android on its own.
