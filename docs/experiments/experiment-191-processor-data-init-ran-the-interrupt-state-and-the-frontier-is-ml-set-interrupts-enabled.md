# Experiment 191 — `processor_data_init` ran, the interrupt state at the top of the critical section, and the frontier is `ml_set_interrupts_enabled`

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
MI4IOS6_STAGE90_XNU real XNU entry xnu_entry_mlsie_enable=0x00000000
 xnu_entry_mlsie_cpsr=0x60000093
 xnu_entry_mlsie_master_ptr=0x00236e68
 xnu_entry_mlsie_proc_state=0x00000000
 xnu_entry_mlsie_proc_set=0x00238a38
 xnu_entry_mlsie_pset0_bitmask=0x00000000
 xnu_entry_mlsie_pset0_count=0x00000000
 xnu_entry_mlsie_proc_count=0x00000000
 stub_hit=ml_set_interrupts_enabled

No errors detected
```

Eight values. Seven were predicted; the eighth, `CPSR`, is the first value in this sequence that
comes from the program status register, and it is reported rather than predicted.

## The smallest step in the sequence, and the one that proves the last one ran

`osfmk/kern/processor_data.o` is **72 bytes of text with exactly two references**, `memset` and
`timer_init` — and both were already satisfied in this image (`memset` from the EABI runtime
aliases, `timer_init` from `osfmk/kern/timer.o`, linked since the timer work of exp-177). So the
step's whole effect is:

```
resolved (1): processor_data_init
added:        (nothing)
320 -> 319 undefined
```

That is the object's complete body — four statements, which `timer_init` over the idle, system and
user states and one `DBOP_NONE` store into the debugger state — and the run's evidence that it ran
is that the next symbol along, `ml_set_interrupts_enabled`, was reached at all. There is nothing
between them but `processor->processor_list = NULL`.

## Where this stop is, from its own argument

`ml_set_interrupts_enabled` is called twice by `processor_init`, and the two calls are `splsched()`
and `splx(s)`:

```
242: mov r0, #0                     ; splsched()  - ml_set_interrupts_enabled(FALSE)
244: bl  ml_set_interrupts_enabled
248: mov r8, r0                     ; s = the previous state, kept
     ... lck_spin_lock, bit_set, cpu_set_count++, lck_spin_unlock ...
374: mov r0, r8                     ; splx(s)
378: bl  ml_set_interrupts_enabled
```

`xnu_entry_mlsie_enable = 0` is the argument, so this is the first of the two — `splsched`, not
`splx` — and the value that distinguishes them is the one the probe was *handed* rather than one it
read. For the second call the argument would have been whatever r8 held.

The two counters are 0 here and were 0 at exp-190's stop as well, but for the opposite reason:
exp-190 stopped *before* the locked region, this stops *inside* its first statement, and
`pset->cpu_set_count++` (`processor.c:180`) and `processor_count++` (`processor.c:193`) are both
still ahead. So they are not a check that distinguishes the two stops — the argument is — but they
do say the locked region has not been entered.

`processor->state` is still 0 and `processor->processor_set` is still `0x00238a38 = &pset0`, both
unchanged from exp-190. `master_processor` is still `0x00236e68`, i.e. `BootProcessor`, read
through the global this time rather than received as an argument — a second route to the same
pointer, six instructions apart.

## `CPSR = 0x60000093`

| field | bits | value | meaning |
| --- | --- | --- | --- |
| NZCV | 31:28 | 0x6 | N=0, Z=1, C=1, V=0 |
| E | 9 | 0 | little-endian |
| I | 7 | **1** | IRQs **masked** |
| F | 6 | 0 | FIQs unmasked |
| T | 5 | 0 | ARM state, not Thumb |
| M | 4:0 | 0x13 | SVC mode |

Two things are worth reading out of it. The first is `I = 1`: interrupts are already masked when
`splsched()` is called, so this call is *recording* the state rather than changing it — which is
what `start.s` predicts, since nothing in XNU's `_start` enables interrupts and the payload enters
with them masked. The second is `M = 0x13`, SVC mode: the CPU is in the mode `start.s` set the
stack for (`start.s:310-311`), which is the same fact the exception-vector stack fix in exp-184
turned on. `F = 0` is the one that was not predicted either way — FIQs are enabled, and this is the
only place in the sequence where that register has been looked at, so it is recorded as measured.

The mode and interrupt bits are the useful part; NZCV is whatever the last compare before the call
left, and the compiler will have arranged it.

## Cost, and why `text size` went *down*

`processor_data.o` adds 72 bytes of text, and the image's reported text size fell from 134668 to
134444 — because the probe got smaller by more than the object did. Exp-190's probe made fourteen
`entry_kv` calls with long keys; this one makes eight with shorter ones, and the strings and call
sequences are most of a probe's footprint. The same effect in the other direction is what makes
`image bytes` useless as a per-step number here: it is 215088 in both experiments, byte for byte,
because `.bss` did not move and the `.bin` ends at the first `.bss` byte. `xnu_entry_checks=5` /
`xnu_entry_failures=0` re-checked exp-175's four invariants, with `args` still at +241664.

| | exp-190 | now |
| --- | --- | --- |
| XNU objects linked | 31 | 32 (`osfmk/kern/processor_data.o`) |
| text (includes the probe) | 134668 B | 134444 B |
| image | 215088 B | 215088 B |
| `.bss` | 0x00234550 – 0x00239dc8 (22648 B) | unchanged |
| boot_args offset | +241664 | +241664 |
| `topOfKernelData` | +2097152 | +2097152 |
| undefined | 320 (276 functions, 44 storage) | 319 (275 functions, 44 storage) |
| headroom | 1860152 B | 1860152 B |

## What is next

`ml_set_interrupts_enabled` is `osfmk/arm/machine_routines_common.c:507`, and the object is
**2923 bytes of text, 44 of data and 108 of `.bss` across 18 references**. It is not a register
access: it reads `CPSR`, and on the enable path it consults `get_preemption_level()` and
`current_thread()` and can call `ml_check_interrupts_disabled_duration`, with an
`#if INTERRUPT_MASKED_DEBUG` block in the middle — which is the reason to link the object rather
than stand in for it, the way the timer table was linked rather than copied.

**The probe has to move before this step can be measured.** `machine_routines_common.o` defines
`ml_set_interrupts_enabled`, which is the symbol the current probe defines; linking both is the
multiple definition that produced exp-190's wrong measurement. The next frontier is what the probe
moves to, and it can be named from the source: once `processor_init` returns, `processor_bootstrap`
returns, and `arm_init` (`arm_init.c:275-333`) stores `my_master_proc`, reads `diag`, `maxmem`,
`hw.memsize`, `up_style_idle_exit` and `immediate_NMI` through `PE_parse_boot_argn` — all real —
and then calls **`arm_vm_init(xmaxmem, args)`**, which is undefined here and is
`osfmk/arm/arm_vm_init.c:339`.

So exp-192 is `machine_routines_common.o` plus a probe at `arm_vm_init`: one object, one symbol
named by the source rather than by a run, and the step that takes this image out of `processor.c`
and into the pmap. `arm_vm_init.o` is **8176 bytes of text, 8 of data and 216 of `.bss` across 23
references** — the largest object since `thread.o` in exp-179 — and it should be linked and
measured before the run that consumes it, now that the probe is no longer in its way. It is also
the first thing in this sequence that the original goal statement is about: `arm_vm_init` is where
the kernel starts setting up memory for the drivers above it.

Nothing was flashed: `persistent_write_attempted=0x00000000` in all 25 contracts that report it,
and the device returned to Android on its own.

## Reproduce

```bash
(cd stages/stage90/xnu_arm_boot && STAGE90_ENTRY_REAL_ARM_INIT=1 ./build_entry.sh)
#   ... entry point 0x00200074, text 134444, image 215088, 319 undefined, 275 stubs

# what the object cost: 1 resolved, nothing added
printf '' > /tmp/empty.c && arm-none-eabi-gcc -mcpu=cortex-a15 -marm -c /tmp/empty.c -o /tmp/empty.o
cp out/stage90/xnu_arm_entry_undef.txt /tmp/A.txt
(cd stages/stage90/xnu_arm_boot && STAGE90_ENTRY_REAL_ARM_INIT=1 \
   STAGE90_ENTRY_KERN_PROCESSOR_DATA_OBJ=/tmp/empty.o ./build_entry.sh)
cp out/stage90/xnu_arm_entry_undef.txt /tmp/B.txt
comm -13 <(sort /tmp/A.txt) <(sort /tmp/B.txt)   # processor_data_init
comm -23 <(sort /tmp/A.txt) <(sort /tmp/B.txt)   # nothing

(cd stages/stage90 && STAGE90_EXTRA_CFLAGS='-DSTAGE90_XNU_ENTRY=1' ./build.sh)
(cd stages/stage90 && ./run_and_capture.sh --allow-xnu-entry)
grep -n 'mlsie_\|stub_hit\|No errors detected' /tmp/cancro-last_kmsg.txt | tail -10

# the two call sites and the argument that distinguishes them
arm-none-eabi-objdump -dr out/xnu_kernel_obj/osfmk_kern_processor.o | \
   awk '/<processor_init>:/{f=1} f{print} f&&/^$/{exit}' | grep -B2 -A2 'ml_set_interrupts_enabled'

# the object's body, and the four statements processor_data_init ran
sed -n '36,54p' external/xnu-4570.1.46/osfmk/kern/processor_data.c
sed -n '505,545p' external/xnu-4570.1.46/osfmk/arm/machine_routines_common.c

# the size of the next step
arm-none-eabi-size out/xnu_kernel_obj/osfmk_arm_machine_routines_common.o
arm-none-eabi-size out/xnu_kernel_obj/osfmk_arm_arm_vm_init.o
arm-none-eabi-nm -u out/xnu_kernel_obj/osfmk_arm_arm_vm_init.o
sed -n '275,340p' external/xnu-4570.1.46/osfmk/arm/arm_init.c
```
