# Experiment 178 — The two halves of `CpuDataEntries` agree, and the frontier is `thread_bootstrap`

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
MI4IOS6_STAGE90_XNU real XNU entry xnu_entry_cpu_data_vaddr=0x0021e000
 xnu_entry_cpu_data_paddr=0x0021e000
 xnu_entry_boot_cpu_data_va=0x0021e000
 xnu_entry_boot_processor=0x00225b68
 stub_hit=thread_bootstrap

No errors detected
```

Three addresses, and the fact that the first three are **the same number** is the measurement.

`arm_init` writes the boot CPU's entry twice (`osfmk/arm/arm_init.c:236-237`):

```c
CpuDataEntries[master_cpu].cpu_data_vaddr = &BootCpuData;
CpuDataEntries[master_cpu].cpu_data_paddr = (void *)((uintptr_t)(args->physBase)
                                          + ((uintptr_t)&BootCpuData
                                          - (uintptr_t)(args->virtBase)));
```

With `physBase == virtBase` — the identity `xnu_entry_jump.c` exists to arrange, and the reason
`_start` can convert its own addresses — the second expression collapses to the first. All three
printed words are `0x0021e000`, so: line 236 ran, line 237 ran, and **the identity held**. Had they
differed, the difference would have been exactly `physBase − virtBase`, which is the number this
whole arrangement exists to make zero.

`0x0021e000` is `BootCpuData`'s link-time address, from the same image:

```
0021d000 D CpuDataEntries
0021e000 D BootCpuData
00225b68 B BootProcessor
```

and `xnu_entry_boot_processor=0x00225b68` is `cpu_common.o`'s own `B 0x18` — the thing
`cpu_processor_alloc(TRUE)` returns without allocating anything (`cpu_common.c:472` is
`if (is_boot_cpu) return &BootProcessor;`). So the run also shows where the newly linked object's
bss landed: inside the image's bss range, `0x00224378 – 0x002267c8`.

This is the first run whose probe prints a *relation* rather than a value, and it is the check that
was worth having: `cpu_processor_alloc` returning a pointer cannot fail, but the physBase/virtBase
identity is a claim this project makes about a boot_args it builds itself, and until now the only
evidence for it was that things did not fault.

## One object, and the two words that read it

`osfmk/arm/cpu_common.o` — the object exp-177's run named. 2428 bytes of text, 36 symbols, 31
undefined references. Measured by linking an empty object in its place and diffing:

```
resolved:  BootProcessor cpu_idle_tickle cpu_machine_init cpu_number cpu_processor_alloc
           cpu_processor_free cpu_signal cpu_signal_cancel cpu_signal_deferred cpu_signal_handler
           cpu_signal_handler_internal current_processor idle_enable processor_to_cpu_datap
           real_ncpus wake_abstime
added:     17  (ast_check cache_xcall_handler chudxnu_cpu_signal_handler clear_wait DebuggerXCall
           hw_atomic_and hw_atomic_and_noret hw_atomic_or kperf_signal_handler OSCompareAndSwap
           PE_cpu_machine_init PE_cpu_signal PE_cpu_signal_cancel PE_cpu_signal_deferred
           platform_cache_init rtclock_intr sched_stats_active)
```

152 − 16 + 17 = 153 (133 functions, 20 storage). Three storage stubs became real definitions with
no deletion: `idle_enable`, `real_ncpus` and `wake_abstime` were stand-ins sized from `nm -S`, and
this object defines all three.

| | exp-177 | now |
| --- | --- | --- |
| XNU objects linked | 16 | 17 (`osfmk/arm/cpu_common.o`) |
| text | 67588 B | 70276 B |
| image | 148768 B | 148792 B |
| `.bss` | 0x00224378 – 0x00226188 | 0x00224378 – 0x002267c8 |
| undefined | 151 (129 functions, 22 storage) | 153 (133 functions, 20 storage) |

## The probe that reads it, and why it is declared the way it is

`cpu_data_entry_t` (`osfmk/arm/cpu_data_internal.h:80`) is `cpu_data_paddr`, then `cpu_data_vaddr`,
then two `uint32_t` on arm32 — two pointers followed by two words. The probe declares

```c
extern uint32_t CpuDataEntries[2];
```

and reads words 0 and 1, which takes both halves without reproducing a struct. The extent is 2
rather than `MAX_CPUS` deliberately: the real declaration is `CpuDataEntries[MAX_CPUS]`
(`cpu_data_internal.h:285`), and writing that here would be a second copy of `MAX_CPUS` to keep in
step — the defect class exp-175 was spent removing. Two is all this reads.

An earlier draft declared `uint32_t CpuDataEntries[];` and the build refused it: `-Werror` with
`-fno-common` means an unsized file-scope array is a *definition* of one element, not a
declaration, so it would have collided with `data.o`'s real 4 KB one. `extern uint32_t
CpuDataEntries[2];` is a declaration and the object supplies the extent.

## The next object is where the step size changes

The device named `thread_bootstrap`, which is `osfmk/kern/thread.c:232`. That object is not another
`cpu_common.o`:

| | `machine_routines.o` (exp-177) | `cpu_common.o` (exp-178) | `kern/thread.o` (next) |
| --- | --- | --- | --- |
| text | 4135 B | 2428 B | **16944 B** |
| functions | 71 | 36 | **77** |
| undefined refs | 79 | 31 | **161** |
| new to the image | 56 | 17 | **135** |

135 new obligations, and the list is not a frontier any more — it is the scheduler (`sched_tick`,
`thread_setrun`, `pset0`, `sched_multiq_dispatch`), the zone allocator (`zalloc`, `zfree`, `zinit`,
`zone_change`), the stack allocator, IPC vouchers, ledgers, task and thread policy, and
`machine_thread_create`. `thread_bootstrap` itself is a long sequence of assignments to the global
`thread_template` and then three calls to `timer_init()`.

That is the question exp-176 raised and exp-177 answered "still yes" for a 71-function object. This
one is four times the size with two and a half times the obligations, and whether the answer is
still yes is the next experiment's subject — measured, not assumed, and not started here.

Nothing was flashed: `persistent_write_attempted = 0x00000000` in all 25 contracts that report it,
and the device returned to Android on its own.

## Reproduce

```bash
(cd stages/stage90/xnu_arm_boot && STAGE90_ENTRY_REAL_ARM_INIT=1 ./build_entry.sh)
#   ... entry point 0x00200074, text 70276, image 148792, 153 undefined, 133 stubs

# the object, and the measurement of what it costs
arm-none-eabi-size out/xnu_kernel_obj/osfmk_arm_cpu_common.o
(cd stages/stage90/xnu_arm_boot && STAGE90_ENTRY_REAL_ARM_INIT=1 \
   STAGE90_ENTRY_CPU_COMMON_OBJ=/tmp/empty.o ./build_entry.sh)
comm -23 <(sort /tmp/u_no_cc.txt) <(sort out/stage90/xnu_arm_entry_undef.txt)   # resolved: 16
comm -13 <(sort /tmp/u_no_cc.txt) <(sort out/stage90/xnu_arm_entry_undef.txt)   # added: 17

(cd stages/stage90 && STAGE90_EXTRA_CFLAGS='-DSTAGE90_XNU_ENTRY=1' ./build.sh)
(cd stages/stage90 && ./run_and_capture.sh --allow-xnu-entry)
grep -n 'cpu_data_v\|boot_processor\|stub_hit\|No errors detected' /tmp/cancro-last_kmsg.txt | tail -5

# the three addresses, against the image that produced them
arm-none-eabi-nm out/stage90/xnu_arm_entry.elf | grep -w 'BootCpuData\|CpuDataEntries\|BootProcessor'

# the two lines the relation is about
sed -n '234,238p' external/xnu-4570.1.46/osfmk/arm/arm_init.c
sed -n '80,90p' external/xnu-4570.1.46/osfmk/arm/cpu_data_internal.h

# the size of the next step
arm-none-eabi-size out/xnu_kernel_obj/osfmk_kern_thread.o
```
