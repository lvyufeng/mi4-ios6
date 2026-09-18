# Experiment 181 — The TPIDRPRW round trip held, and the frontier is `rtclock_early_init`

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
MI4IOS6_STAGE90_XNU real XNU entry stub_hit=rtclock_early_init

No errors detected
```

The previous run stopped at `machine_set_current_thread`, the last statement of `thread_bootstrap`.
This one is twelve lines of `arm_init` further on, which means a specific list of things did not
fault:

| `arm_init.c` | what it does | why not faulting is evidence |
| --- | --- | --- |
| `:241` | `thread = current_thread()` | reads TPIDRPRW (`cpu_data.h:52` is `__builtin_arm_mrc(15,0,13,0,4)`) |
| `:247` | `thread->machine.preemption_count = 0` | **writes through that pointer** |
| `:249` | `thread->machine.CpuDatap = &BootCpuData` | writes through it again |
| `:262` | `BootCpuData.cpu_processor->processor_data.kernel_timer = &thread->system_timer` | writes through the pointer `cpu_processor_alloc(TRUE)` returned |
| `:265` | `cpu_bootstrap()` | see below |

`:247` is the load-bearing one. `start.s` writes TPIDRURO (`:78`, `:309`) but **not** TPIDRPRW, so
TPIDRPRW holds whatever the boot path left there — and XNU's own page tables map only
`[physBase, physBase + memSize)` = `[0x00200000, 0x00a00000)`. A write through a zero or residual
TPIDRPRW would land at address ~0, outside that map, and data-abort. It did not. So
`machine_set_current_thread` wrote the register and `current_thread()` read it back.

`:262` is the second one: `cpu_processor` came from `cpu_processor_alloc(TRUE)` in the previous
experiment, and this line writes *into* what it points at. A `NULL` or garbage return would fault
here. It did not.

**`cpu_bootstrap` proves nothing, and it is worth saying so:** `osfmk/arm/cpu.c:86` is

```c
void
cpu_bootstrap(void)
{
}
```

an empty function. It is real code — `osfmk_arm_cpu.o` has been linked since exp-159 — and its
having run is why the run got as far as it did, but it is not evidence about anything. The
neighbouring `cpu_sleep` and `cpu_idle` in the same file are the ones that use `getCpuDatap()`, and
neither has run.

This is inference from an absence — nothing faulted — rather than a measurement, which is the
weakest kind of evidence this project has and the kind its own notes warn about. Turning it into a
measured equality is what the next probe is for: print `current_thread()` and `&init_thread` at the
same point and let the two numbers be compared.

## The object

`out/xnu_asm_obj/machine_routines_asm.o` — Apple's `osfmk/arm/machine_routines_asm.s`, assembled
rather than compiled. 2280 bytes of text, 76 symbols. Measured by linking an empty object in its
place:

```
resolved:  cpu_idle_wfi fiq_context_init get_mmu_control machine_set_current_thread
           ml_get_timebase set_mmu_control timer_grab timer_update wfi_inst
added:     copyin_validate copyout_validate Idle_context kernel_pmap_store
```

287 − 9 + 4 = 282 (245 functions, 37 storage).

Two of the nine resolutions are worth naming. `machine_set_current_thread` is the one the previous
run asked for. `get_mmu_control` and `set_mmu_control` were stubs that had been standing in for the
MMU control register since the image was first assembled — real now, though nothing has called them
yet.

`Idle_context` is the new storage stub: 37 storage symbols now where there were 36, and this is the
one that arrived with the assembly. Nothing on the path taken reaches it.

| | exp-180 | now |
| --- | --- | --- |
| XNU objects linked | 19 | 20 (`osfmk/arm/machine_routines_asm.o`) |
| text | 91268 B | 93380 B |
| image | 165248 B | 165248 B |
| `.bss` | 0x00228390 – 0x0022bfc8 | 0x00228390 – 0x0022c088 |
| boot_args offset | +184320 | +188416 |
| `topOfKernelData` | +2097152 | +2097152 |
| undefined | 287 (251 functions, 36 storage) | 282 (245 functions, 37 storage) |
| headroom | 1916984 B | 1916792 B |

The binary is the same 165248 bytes for the second run in a row: 2112 bytes of text again fit
inside the padding at the end of the text section.

## What is next

`osfmk/arm/rtclock.o` — 2136 bytes of text, 19 functions, 18 undefined references of which three are
new (`clock_timebase_init`, `commpage_set_timestamp`, `timer_intr`). `rtclock_early_init`
(`rtclock.c:86`) is:

```c
PE_register_timebase_callback(timebase_callback);
```

and everything else in it is `#if DEVELOPMENT || DEBUG`. So the object brings the real-time clock
into the image, and the next edge will be whatever `PE_register_timebase_callback` turns out to be —
it is one of the symbols already linked out of `pexpert/gen/pe_gen.o`, so the interesting question is
whether that call reaches `timebase_callback` and what it does with the timebase frequencies the
device tree advertises (19.2 MHz, `stage90_main.c:692`).

Nothing was flashed: `persistent_write_attempted = 0x00000000` in all 25 contracts that report it,
and the device returned to Android on its own.

## Reproduce

```bash
(cd stages/stage90/xnu_arm_boot && STAGE90_ENTRY_REAL_ARM_INIT=1 ./build_entry.sh)
#   ... entry point 0x00200074, text 93380, image 165248, 282 undefined, 245 stubs

# what the object cost: 9 resolved, 4 added
(cd stages/stage90/xnu_arm_boot && STAGE90_ENTRY_REAL_ARM_INIT=1 \
   STAGE90_ENTRY_MACHINE_ROUTINES_ASM_OBJ=/tmp/empty.o ./build_entry.sh)
comm -23 <(sort /tmp/u_no_mra.txt) <(sort out/stage90/xnu_arm_entry_undef.txt)
comm -13 <(sort /tmp/u_no_mra.txt) <(sort out/stage90/xnu_arm_entry_undef.txt)

(cd stages/stage90 && STAGE90_EXTRA_CFLAGS='-DSTAGE90_XNU_ENTRY=1' ./build.sh)
(cd stages/stage90 && ./run_and_capture.sh --allow-xnu-entry)
grep -n 'stub_hit\|No errors detected' /tmp/cancro-last_kmsg.txt | tail -3

# the lines that did not fault, and the empty function among them
sed -n '239,268p' external/xnu-4570.1.46/osfmk/arm/arm_init.c
sed -n '82,90p' external/xnu-4570.1.46/osfmk/arm/cpu.c

# that start.s does not write TPIDRPRW, which is what makes :247 evidence
grep -n 'c13, c0, 4' external/xnu-4570.1.46/osfmk/arm/start.s || echo "(no TPIDRPRW write in start.s)"

# the size of the next step
arm-none-eabi-size out/xnu_kernel_obj/osfmk_arm_rtclock.o
sed -n '86,100p' external/xnu-4570.1.46/osfmk/arm/rtclock.c
```
