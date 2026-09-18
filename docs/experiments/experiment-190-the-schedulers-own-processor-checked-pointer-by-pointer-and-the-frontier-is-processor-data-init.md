# Experiment 190 — The scheduler's own processor, checked pointer by pointer, and the frontier is `processor_data_init`

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
MI4IOS6_STAGE90_XNU real XNU entry xnu_entry_proc_ptr=0x00236e68
 xnu_entry_master_processor=0x00236e68
 xnu_entry_pset0_addr=0x00238a38
 xnu_entry_master_cpu=0x00000000
 xnu_entry_proc_cpu_id=0x00000000
 xnu_entry_proc_state=0x00000000
 xnu_entry_proc_set=0x00238a38
 xnu_entry_proc_primary=0x00236e68
 xnu_entry_proc_is_recommended=0x00000001
 xnu_entry_proc_quantum_end_lo=0xffffffff
 xnu_entry_proc_quantum_end_hi=0xffffffff
 xnu_entry_proc_deadline_lo=0xffffffff
 xnu_entry_pset0_cpu_set_count=0x00000000
 xnu_entry_processor_count=0x00000000
 stub_hit=processor_data_init

No errors detected
```

Fourteen values, all fourteen predicted before the run. This is the first probe in the sequence
that receives a pointer *from the kernel* and can check it, rather than reading a global the image
already had or a register the hardware reports.

## The pointer that round-tripped, seven stages after it was allocated

`xnu_entry_proc_ptr` is the argument to `processor_data_init`, and `xnu_entry_master_processor` is
the global `processor_bootstrap` wrote one statement earlier. They are the same address,
`0x00236e68`, and that address is not a `processor_array` element:

```
$ arm-none-eabi-nm -S out/stage90/xnu_arm_entry.elf | grep BootProcessor
00236e68 000006a8 B BootProcessor
```

It is `osfmk/arm/cpu_common.c:64`'s `struct processor BootProcessor`, the object
`cpu_processor_alloc(TRUE)` returns for the boot CPU (`cpu_common.c:472-478`). So one address
carries the whole chain, and every link of it was a separate experiment:

| step | where | experiment |
| --- | --- | --- |
| `arm_init` calls `cpu_processor_alloc(TRUE)` into `BootCpuData.cpu_processor` | `arm_init.c:234` | 178 |
| `cpu_processor_alloc` returns `&BootProcessor` | `cpu_common.c:477` | 178 |
| `cpu_to_processor(master_cpu)` returns `cpu_datap(0)->cpu_processor` | `cpu_common.c:501` | this one |
| `processor_bootstrap` stores it in `master_processor` | `processor.c:133` | this one |
| `processor_init` is called with it | `processor.c:135` | this one |
| `processor_init` passes it to `processor_data_init` | `processor.c:169` | this one |

`cpu_to_processor` had never been called before this run — it is `cpu_datap(cpu)->cpu_processor`,
two dereferences through `CpuDataEntries`, and the value it produced is the same object exp-178
allocated. Nothing about that is implied by the run continuing: a stub `processor_bootstrap` would
have left `master_processor` zero and the argument would have been 0.

## Two values that must *not* have happened yet

`processor_count = 0` and `pset0.cpu_set_count = 0` are the only two values here whose correctness
is that they are still zero, and both are that way for a stated reason: `processor_init` increments
`pset->cpu_set_count` (`processor.c:180-181`) and `processor_count` (`processor.c:193`) *after* the
call the probe intercepts. They are a check on the stop's position rather than on the code before
it — the one place in the probe where a non-zero answer would have meant "the frontier is not where
this file says it is".

The rest could not come from zeroed memory, which is what makes them measurements. `.bss` is
zeroed by the payload before XNU starts, so a field a stub left alone reads 0, and these do not:

- `processor_set = 0x00238a38` is `&pset0` — the same address the probe read from the symbol, so
  `processor_bootstrap`'s `pset_init(&pset0, &pset_node0)` and `processor_init`'s store of its
  third argument agree, and neither is zero;
- `quantum_end` and `deadline` are `0xffffffff:0xffffffff` — `UINT64_MAX`, twice. These are the
  first XNU timer fields in this image to hold a deadline, and they arrive from
  `processor_init`'s explicit store, not from `timer_call_setup` (which is called one statement
  earlier, on the `quantum_timer` at offset 64, and only initializes the lock and the call entry);
- `processor_primary = 0x00236e68` is the processor's own address: `processor->processor_primary =
  processor`, the self-pointer a non-SMT processor gets;
- `is_recommended = 1` is a bit test, `(pset->recommended_bitmask & (1ULL << cpu_id)) ? TRUE :
  FALSE`, and it is 1 because `pset_init` ends with `pset->recommended_bitmask = ~0ULL`
  (`processor.c:341`). All 64 bits set, so any cpu_id gives 1 — which is why this one is predicted
  from the source in one line and why it says nothing about the device;
- `master_cpu = 0` and `cpu_id = 0` agree, which is what `cpu_to_processor` indexes with.

## What the stop establishes

`processor_data_init` is `osfmk/kern/processor_data.c:39`, and it is the first symbol
`processor_init` reaches that nothing defines. Reading that function statement by statement, the
call is the fourth thing it does:

```c
	if (processor != master_processor)          /* skipped - processor_bootstrap passed the master */
		SCHED(processor_init)(processor);
	processor->state = PROCESSOR_OFF_LINE;      /* and the ~20 field stores after it */
	processor_state_update_idle(processor);     /* in processor.o */
	timer_call_setup(&processor->quantum_timer, thread_quantum_expire, processor);  /* timer_call.o */
	processor_data_init(processor);             /* processor_data.c -> the stop */
```

The compiled `processor_init` has exactly two direct calls before it — `bl timer_call_setup`
(`timer_call.o`, real since exp-185) and the `blx` through `sched_multiq_dispatch`'s dispatch
table, which the `cmp r0, r4; beq` above it skips precisely because the argument *is* the master.
Every field store between them is in `processor.o`, which this run is what linked. So stopping here
means real `processor_bootstrap` and real `processor_init` ran to that point, and the offsets the
probe reads are the ones from their own compiled code:

```
str r9, [r4, #8]      offset    8 = state          r9 = 0  -> PROCESSOR_OFF_LINE
str r0, [r4, #16]     offset   16 = is_recommended
str r6, [r4, #32]     offset   32 = processor_set  r6 = r2 = processor_init's third argument (&pset0)
str fp, [r4, #56]     offset   56 = cpu_id         fp = r1 = the cpu_id argument
str r0, [r4, #128]    offset  128 = quantum_end    r0 = -1, and [r4, #132] = -1
str r0, [r4, #144]    offset  144 = deadline       r0 = -1
str r4, [r4, #1236]   offset 1236 = processor_primary, the processor into itself
```

`add r0, r4, #64` on the `timer_call_setup` argument is what fixes 128 as the end of
`quantum_timer`; without it, 128 and 144 would be two 8-byte fields somewhere after it, and the
struct's layout does depend on which `#if` blocks the configuration selected.

## The measurement, and how the first attempt at it was wrong

The step is `osfmk/kern/processor.o`: 4784 bytes of text, 96 of data, 1616 of `.bss`, 42
references. Measured by linking it and then an empty object in its place, the same way as every
step since exp-185:

```
resolved (13): master_cpu  master_processor  processor_bootstrap  processor_count
               processor_init  processor_pset  processor_set_primary
               processor_state_update_explicit  pset0  sched_stats_active
               tasks_threads_lock  threads  threads_count
added (10):    convert_task_to_port  hz_tick_interval  ipc_processor_enable
               ipc_processor_init  mac_task_check_expose_task  processor_shutdown
               realhost  sched_load_average  sched_mach_factor  thread_quantum_expire
323 -> 320 undefined
```

Thirteen resolutions for ten obligations, and the thirteen include the whole of the scheduler's
own state — `pset0`, `master_processor`, `master_cpu`, `processor_count`, `processor_init`,
`processor_bootstrap`, `processor_pset`, `processor_set_primary`,
`processor_state_update_explicit`, `tasks_threads_lock`, `threads`, `threads_count` and
`sched_stats_active` — nearly all of which the image had been carrying as generated storage stubs
with sizes taken from the object rather than from any understanding of what they hold. Those stop
being stand-ins here.

`processor_data_init` appears in neither list, because the *probe* defines it: it is the frontier,
so its own cost is measured by the run, not by the link. That is the same situation
`vm_cache_geometry_colors` was in at exp-189, and it is why a step's link cost and the symbol the
run names are two different numbers.

**The first attempt at this measurement was wrong, and the way it was wrong is worth recording.**
It was taken with the exp-189 probe still in place, which defined `processor_bootstrap` — the
symbol `processor.o` also defines. The link therefore failed with
`multiple definition of 'processor_bootstrap'`, and what a failed link leaves behind is not an
undefined-reference list but a partial one: `ld` reports errors until it gives up, and the set it
had accumulated is neither the with-object set nor the without-object set. It produced a tidy,
believable, entirely wrong split of 11 and 11. Two things make this class of error dangerous rather
than merely annoying: the number looks like all the others, and the failure that caused it is
printed in the middle of a wall of "uses 32-bit enums" warnings.

The rule that avoids it: **a measurement link must succeed in both directions.** If the probe
defines the symbol the object under measurement defines, the object cannot be measured until the
probe has moved — which is the order this experiment was finally done in, writing the exp-190 probe
first and measuring the step second. The same trap had already appeared once, in exp-185, where the
probe's definition of `lck_mod_init` made `locks.o` measure as 0 resolved and 0 added.

A second, smaller artifact was caught while checking the first: `grep -qx "$sym" out/stage90/…`
run from the wrong directory returns false for every symbol, so a status check reported that all
seven symbols in question were "in the image" when the file had not been read at all. Both are the
same class as the seven recorded in memory — a number that is an artifact of how it was taken.

Re-measured cleanly for comparison, exp-189's step (`cpuid.o` + `machine_cpuid.o`) against
*this* probe's reference set is 6 resolved and 1 added, where against exp-189's own probe it was 8
and nothing. Both are right: `arm_mvfp_info` and `cpuid_get_cpufamily` were resolved twice over
because that probe called them, and `vm_cache_geometry_colors` was undefined either way because
that probe read it. An object's "cost" is relative to what the image references, and the probe is
part of the image.

## Cost

| | exp-189 | now |
| --- | --- | --- |
| XNU objects linked | 30 | 31 (`osfmk/kern/processor.o`) |
| text | 129836 B | 134668 B |
| image | 198608 B | 215088 B |
| `.bss` | 0x00230550 – 0x00235b48 (22008 B) | 0x00234550 – 0x00239dc8 (22648 B) |
| boot_args offset | +225280 | +241664 |
| `topOfKernelData` | +2097152 | +2097152 |
| undefined | 320 (273 functions, 47 storage) | 320 (276 functions, 44 storage) |
| headroom | 1877176 B | 1860152 B |

The image grew by 16480 bytes and the real growth is 6752 of them: 5952 of text, 96 of data
(`processor.o`'s) and 704 of `.bss`. The other 9728 are alignment — `.text` crossed a 0x4000
boundary, so `.data` had to start on the next one, and the `.bin` is everything up to the first
`.bss` byte. The same effect was recorded in the other direction in exp-186 and exp-188, where two
objects' worth of text went into existing padding and the file did not move at all. The
boot_args offset moved with the end of `.bss`, which is why the payload reads it from the generated
header rather than repeating it; `xnu_entry_checks=5` / `xnu_entry_failures=0` re-checked exp-175's
four invariants with the new numbers.

## What is next

Two objects, and the first of them is nearly free. `processor_data_init`'s body is four statements
(`processor_data.c:39-53`): a `memset` of `processor->processor_data`, three `timer_init` calls, and
a `DBOP_NONE` store. `osfmk_kern_processor_data.o` is **72 bytes of text with exactly two
references, `memset` and `timer_init`**, and both are already satisfied in this image — so linking
it resolves `processor_data_init` and adds no obligations at all. It may be the smallest non-empty
step in the sequence.

The second is what the step after it will need. `processor_init` continues past the call with
`ml_set_interrupts_enabled` (`bl ml_set_interrupts_enabled`, `processor_init+0x104`), which is
undefined here and is defined in **`osfmk/arm/machine_routines_common.o`** — 2923 bytes of text,
44 of data and 108 of `.bss` across 18 references, where the ones that matter are
`get_preemption_level`, `ml_get_hwclock`, `mach_absolutetime_asleep`, `mt_fixed_counts`,
`mt_perfcontrol`, `arm_user_protect_begin`/`_end` and two storage symbols, `BootArgs` and
`RTClockData`, that this image already has. So the step is `processor_data.o` plus
`machine_routines_common.o`, and its link cost should be measured with the probe moved to
`ml_set_interrupts_enabled` first — which is the ordering this experiment's measurement defect
argues for.

After `processor_init` returns, `processor_bootstrap` returns, and `arm_init` (`arm_init.c:275`)
sets `my_master_proc = master_processor` and then reads `diag`, `maxmem` and `up_style_idle_exit`
through `PE_parse_boot_argn` — real since exp-170. What follows in `arm_init` is
`PE_init_platform`, `cpu_data_init` and the transition to `machine_startup`, which is where the
image stops being a list of statements in one function and starts being the kernel's own thread.

Nothing was flashed: `persistent_write_attempted=0x00000000` in all 25 contracts that report it,
and the device returned to Android on its own.

## Reproduce

```bash
(cd stages/stage90/xnu_arm_boot && STAGE90_ENTRY_REAL_ARM_INIT=1 ./build_entry.sh)
#   ... entry point 0x00200074, text 134668, image 215088, 320 undefined, 276 stubs

# what the object cost: 13 resolved, 10 added. Both builds must link - the probe must not define
# anything the object under measurement defines, which is why the probe moved first.
printf '' > /tmp/empty.c && arm-none-eabi-gcc -mcpu=cortex-a15 -marm -c /tmp/empty.c -o /tmp/empty.o
cp out/stage90/xnu_arm_entry_undef.txt /tmp/A.txt
(cd stages/stage90/xnu_arm_boot && STAGE90_ENTRY_REAL_ARM_INIT=1 \
   STAGE90_ENTRY_KERN_PROCESSOR_OBJ=/tmp/empty.o ./build_entry.sh)
cp out/stage90/xnu_arm_entry_undef.txt /tmp/B.txt
comm -13 <(sort /tmp/A.txt) <(sort /tmp/B.txt)   # 13 resolved
comm -23 <(sort /tmp/A.txt) <(sort /tmp/B.txt)   # 10 added

(cd stages/stage90 && STAGE90_EXTRA_CFLAGS='-DSTAGE90_XNU_ENTRY=1' ./build.sh)
(cd stages/stage90 && ./run_and_capture.sh --allow-xnu-entry)
grep -n 'proc_\|pset0\|processor_count\|stub_hit\|No errors detected' /tmp/cancro-last_kmsg.txt | tail -18

# the pointer, and that it is BootProcessor rather than a processor_array element
arm-none-eabi-nm -S out/stage90/xnu_arm_entry.elf | grep -E ' (BootProcessor|pset0|master_processor)$'
sed -n '60,68p;468,508p' external/xnu-4570.1.46/osfmk/arm/cpu_common.c

# the field offsets, from processor_init's own compiled code
arm-none-eabi-objdump -dr out/xnu_kernel_obj/osfmk_kern_processor.o | \
   awk '/<processor_init>:/{f=1} f{print} f&&/^$/{exit}' | grep -E 'str r[0-9a-f]+, \[r4|R_ARM_CALL|add r0, r4, #64'

# the recommended bitmask the 1 in is_recommended comes from
sed -n '321,345p' external/xnu-4570.1.46/osfmk/kern/processor.c

# the size of the next step
arm-none-eabi-size out/xnu_kernel_obj/osfmk_kern_processor_data.o
arm-none-eabi-size out/xnu_kernel_obj/osfmk_arm_machine_routines_common.o
arm-none-eabi-nm -u out/xnu_kernel_obj/osfmk_arm_machine_routines_common.o
sed -n '36,54p' external/xnu-4570.1.46/osfmk/kern/processor_data.c
```
