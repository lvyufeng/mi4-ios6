# Experiment 180 — `thread_bootstrap` ran to its last statement, and the frontier is `machine_set_current_thread`

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
MI4IOS6_STAGE90_XNU real XNU entry stub_hit=machine_set_current_thread

No errors detected
```

**`thread_bootstrap` ran to completion.** It is `osfmk/kern/thread.c:232-399`: a hundred-odd field
assignments to the global `thread_template`, three `timer_init()` calls, a `bzero` of the overrides
block, `init_thread = thread_template;`, and then — its last statement — `machine_set_current_thread(&init_thread)`.
The previous run stopped at the first `timer_init`; this one is past all three, past everything in
between, and at the final call. Nothing between the two runs could have been reached any other way.

`machine_set_current_thread` is in `osfmk/arm/machine_routines_asm.s:36`, and it is where the
thread really becomes current:

```asm
        mcr     p15, 0, r0, c13, c0, 4        // Write TPIDRPRW
        ldr     r1, [r0, TH_CTH_SELF]
        mrc     p15, 0, r2, c13, c0, 3        // Read TPIDRURO
        and     r2, r2, #3                    // Extract cpu number
        orr     r1, r1, r2
        mcr     p15, 0, r1, c13, c0, 3        // Write TPIDRURO
```

That is the other half of `current_thread()`, which `osfmk/arm/cpu_data.h:52` defines as
`__builtin_arm_mrc(15, 0, 13, 0, 4)` — a read of the same TPIDRPRW. So once this object is in the
image, `arm_init.c:241`'s `thread = current_thread()` will return `&init_thread`, and `getCpuDatap()`
— which is `current_thread()->machine.CpuDatap` — starts working. `arm_init.c:247`'s
`thread->machine.preemption_count = 0` writes through it one line later.

Nothing was flashed: `persistent_write_attempted = 0x00000000` in all 25 contracts that report it,
and the device returned to Android on its own.

## A small object, as measured before the run

`osfmk/kern/timer.o` was sized before it was linked, and the answer is why this step is cheap:

```
resolved:  timer_init  timer_switch
added:     timer_update
```

288 − 2 + 1 = 287 (251 functions, 36 storage). `timer_init` itself is four field assignments to a
`timer_t`:

```c
timer->tstamp = 0;
timer->low_bits = 0;
timer->high_bits = 0;
timer->high_bits_check = 0;
```

`timer_switch` and `timer_update` came along because they are in the same 320-byte object, and
because the linker resolves an object's references whether or not the referencing function runs —
the same property that made exp-179's 135-symbol step. Here it costs one.

| | exp-179 | now |
| --- | --- | --- |
| XNU objects linked | 18 | 19 (`osfmk/kern/timer.o`) |
| text | 90980 B | 91268 B |
| image | 165248 B | 165248 B |
| `.bss` | 0x00228390 – 0x0022bfc8 | 0x00228390 – 0x0022bfc8 |
| boot_args offset | +184320 | +184320 |
| `topOfKernelData` | +2097152 | +2097152 |
| undefined | 288 (252 functions, 36 storage) | 287 (251 functions, 36 storage) |
| headroom | 1916984 B | 1916984 B |

Everything below `.text` is unchanged: the object's 288 bytes of text fit inside the alignment
padding at the end of the text section, so the binary is the same 165248 bytes and every derived
offset in `xnu_arm_entry.h` is the same number. exp-175's derivation makes that a non-event instead
of a set of constants to re-check.

## What this pair of runs establishes

exp-179 and exp-180 are the same object-link step, one apart, and together they show the frontier
mechanism doing the thing it is for. The device names one symbol per run; each run's name is the
one the previous run's linked object asks for next. `thread.o` → `timer_init` (`:313`) →
`machine_set_current_thread` (`:398`). Three runs, three names, all of them read off the source
before the run and all of them matching.

The scheduler and the zone allocator arrived in the first of the three — 135 symbols of them — and
not one of them has been reached. That is the closure growing ahead of the execution, which is what
the method says it does and is not the same thing as progress.

## What is next

`out/xnu_asm_obj/machine_routines_asm.o` — 2280 bytes of text, assembled by
`tools/assemble_arm_layer.sh` rather than compiled, because it is Apple's `machine_routines_asm.s`.
It has 12 undefined references, of which four are new to the image:

```
copyin_validate  copyout_validate  Idle_context  kernel_pmap_store
```

The other eight — `EntropyData`, `ExceptionVectorsBase`, `fiqstack_top`, `gPhysBase`, `gPhysSize`,
`gVirtBase`, `intstack_top`, `kdebug_enable` — this image already carries, the last five as the
real definitions `data.o` and `entry_stubs.c` provide. So the step is small again, and it is the one
that makes the TPIDRPRW round trip real.

## Reproduce

```bash
(cd stages/stage90/xnu_arm_boot && STAGE90_ENTRY_REAL_ARM_INIT=1 ./build_entry.sh)
#   ... entry point 0x00200074, text 91268, image 165248, 287 undefined, 251 stubs

# what the object cost: 2 resolved, 1 added
(cd stages/stage90/xnu_arm_boot && STAGE90_ENTRY_REAL_ARM_INIT=1 \
   STAGE90_ENTRY_KERN_TIMER_OBJ=/tmp/empty.o ./build_entry.sh)
comm -23 <(sort /tmp/u_no_ti.txt) <(sort out/stage90/xnu_arm_entry_undef.txt)   # timer_init timer_switch
comm -13 <(sort /tmp/u_no_ti.txt) <(sort out/stage90/xnu_arm_entry_undef.txt)   # timer_update

(cd stages/stage90 && STAGE90_EXTRA_CFLAGS='-DSTAGE90_XNU_ENTRY=1' ./build.sh)
(cd stages/stage90 && ./run_and_capture.sh --allow-xnu-entry)
grep -n 'stub_hit\|No errors detected' /tmp/cancro-last_kmsg.txt | tail -3

# the last statement of thread_bootstrap, and the object that defines it
sed -n '394,400p' external/xnu-4570.1.46/osfmk/kern/thread.c
sed -n '34,46p' external/xnu-4570.1.46/osfmk/arm/machine_routines_asm.s
sed -n '50,62p' external/xnu-4570.1.46/osfmk/arm/cpu_data.h

# the size of the next step
arm-none-eabi-size out/xnu_asm_obj/machine_routines_asm.o
arm-none-eabi-nm -u out/xnu_asm_obj/machine_routines_asm.o
```
