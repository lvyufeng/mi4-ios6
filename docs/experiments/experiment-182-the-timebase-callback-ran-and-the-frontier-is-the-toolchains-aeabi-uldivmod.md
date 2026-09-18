# Experiment 182 — The timebase callback ran, and the frontier is the toolchain's `__aeabi_uldivmod`

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
MI4IOS6_STAGE90_XNU real XNU entry stub_hit=__aeabi_uldivmod

No errors detected
```

The probe for this run was on `kernel_early_bootstrap` (`arm_init.c:268`), the statement after
`rtclock_early_init` returns — and it did not fire. So the run stopped *inside* the call, and the
symbol it stopped at says exactly where.

`rtclock_early_init` (`osfmk/arm/rtclock.c:86`) is one statement:

```c
PE_register_timebase_callback(timebase_callback);
```

`PE_register_timebase_callback` is real code (`pexpert/arm/pe_init.c:415`, in the image since
exp-172), and it does not merely store the callback — it calls `PE_call_timebase_callback()`
immediately, which invokes it:

```c
timebase_freq.timebase_num = gPEClockFrequencyInfo.timebase_frequency_hz;
timebase_freq.timebase_den = 1;
if (gTimebaseCallback)
        gTimebaseCallback(&timebase_freq);
```

`timebase_callback` is what this run's object added, and its first act is a guard:

```c
if (freq->timebase_den < 1 || freq->timebase_den > 4 ||
    freq->timebase_num < freq->timebase_den)
        panic("rtclock timebase_callback: invalid constant %ld / %ld", ...);
```

With `den = 1` that is `timebase_num < 1`, so **the panic did not fire and the frequency XNU
computed from this project's device tree is not zero.** That is a real measurement about the tree:
`pe_identify_machine.c:130` sets `timebase_frequency_hz` from a device-tree property, and
`stage90_main.c:692` advertises `timebase-frequency = 19200000` on the cpu nodes.

Then it reaches the reduction loop, and stops:

```c
while (t64_2 != 0) {
        uint64_t temp = t64_2;
        t64_2 = t64_1 % t64_2;      /* <-- 64-bit modulus, on 32-bit ARM */
        t64_1 = temp;
}
```

`uint64_t % uint64_t` on ARMv7 is the EABI helper `__aeabi_uldivmod`, which this image does not
have.

## The first frontier edge that is not XNU's

Every symbol this method has stopped at until now has been an XNU function or an XNU global. This
one is neither: `__aeabi_uldivmod` is the ARM EABI's 64-bit divide-and-remainder helper, and it
comes from libgcc. Measured, not assumed —

```
grep -rl '__aeabi_uldivmod' external/xnu-4570.1.46/       -> nothing
nm --defined-only on all 700+ compiled XNU objects        -> nothing
grep -l 'aeabi' on the same                             -> nothing at all, not one symbol
```

So XNU does not define it, in this tree or in these objects, and there is no XNU implementation for
`entry_arm_rtabi.s` to alias it to the way it aliases `__aeabi_memcpy` to XNU's own `bcopy`. The
image is being asked for a capability the *toolchain* normally provides, and the honest source is
libgcc — which is a different kind of decision from every one this frontier has presented so far,
and is the next experiment's subject.

It also says something about what the image is now: `timebase_callback` is the first function in it
that does 64-bit arithmetic, and it is the first thing to ask for a compiler runtime routine rather
than a kernel one.

## The object

`osfmk/arm/rtclock.o` — 2136 bytes of text, 19 functions, 18 undefined references. Measured by
linking an empty object in its place:

```
resolved:  absolutetime_to_microtime  absolutetime_to_nanoseconds  ClearIdlePop  mach_absolute_time
           nanoseconds_to_absolutetime  rtclock_early_init  rtclock_intr  SetIdlePop
added:     clock_timebase_init  commpage_set_timestamp  timer_intr
```

281 − 8 + 3 = 276 (239 functions, 37 storage).

Eight resolutions, and the interesting ones are the timebase conversions: `mach_absolute_time`,
`absolutetime_to_nanoseconds`, `nanoseconds_to_absolutetime` and `absolutetime_to_microtime` were
all stubs, and all four are real now. They are the functions that divide by the constants
`timebase_callback` is in the middle of computing — so they are real code that cannot work yet, and
the run shows exactly where the missing piece is.

| | exp-181 | now |
| --- | --- | --- |
| XNU objects linked | 20 | 21 (`osfmk/arm/rtclock.o`) |
| text | 93380 B | 95492 B |
| image | 165248 B | 165248 B |
| `.bss` | 0x00228390 – 0x0022c088 | 0x00228390 – 0x0022c088 |
| boot_args offset | +188416 | +188416 |
| `topOfKernelData` | +2097152 | +2097152 |
| undefined | 282 (245 functions, 37 storage) | 276 (239 functions, 37 storage) |
| headroom | 1916792 B | 1916792 B |

The binary is the same 165248 bytes for the third run in a row, and every derived offset in
`xnu_arm_entry.h` is unchanged. 2112 bytes of text have now gone into the padding at the end of the
text section three times running — which will not last, and exp-175's derivation is what makes it not
matter when it does.

## What the probe will measure next

The probe written for this run is unused, and it is already the right one for the run after the
next. It prints `current_thread()` — the TPIDRPRW round trip exp-181 could only infer from an
absence — and `cpu_number()`, which follows the same register through `getCpuDatap()` and the
pointer `arm_init.c:249` stored, and should come back `0`. It also repeats `ml_get_cpu_count()` from
exp-177 as a control. It did not run because the image stopped before reaching it, which is the
first time a probe has been written and not fired.

Nothing was flashed: `persistent_write_attempted = 0x00000000` in all 25 contracts that report it,
and the device returned to Android on its own.

## Reproduce

```bash
(cd stages/stage90/xnu_arm_boot && STAGE90_ENTRY_REAL_ARM_INIT=1 ./build_entry.sh)
#   ... entry point 0x00200074, text 95492, image 165248, 276 undefined, 239 stubs

# what the object cost: 8 resolved, 3 added
(cd stages/stage90/xnu_arm_boot && STAGE90_ENTRY_REAL_ARM_INIT=1 \
   STAGE90_ENTRY_ARM_RTCLOCK_OBJ=/tmp/empty.o ./build_entry.sh)
comm -23 <(sort /tmp/u_no_rt.txt) <(sort out/stage90/xnu_arm_entry_undef.txt)
comm -13 <(sort /tmp/u_no_rt.txt) <(sort out/stage90/xnu_arm_entry_undef.txt)

(cd stages/stage90 && STAGE90_EXTRA_CFLAGS='-DSTAGE90_XNU_ENTRY=1' ./build.sh)
(cd stages/stage90 && ./run_and_capture.sh --allow-xnu-entry)
grep -n 'stub_hit\|current_thread\|cpu_number\|No errors detected' /tmp/cancro-last_kmsg.txt | tail -4

# the callback, its guard, and the loop it died in
sed -n '86,95p'  external/xnu-4570.1.46/osfmk/arm/rtclock.c
sed -n '103,136p' external/xnu-4570.1.46/osfmk/arm/rtclock.c
sed -n '415,432p' external/xnu-4570.1.46/pexpert/arm/pe_init.c

# that no XNU object defines the symbol the frontier is now at
grep -rl '__aeabi_uldivmod' external/xnu-4570.1.46/ || echo "(XNU does not define it)"
for o in out/xnu_kernel_obj/*.o out/xnu_asm_obj/*.o; do
  arm-none-eabi-nm --defined-only "$o" 2>/dev/null | grep aeabi
done || echo "(no compiled XNU object defines any __aeabi_* symbol)"
```
