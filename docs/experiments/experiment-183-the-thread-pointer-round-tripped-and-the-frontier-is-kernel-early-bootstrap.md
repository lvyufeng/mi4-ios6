# Experiment 183 — The thread pointer round-tripped through TPIDRPRW, and the frontier is `kernel_early_bootstrap`

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
MI4IOS6_STAGE90_XNU real XNU entry xnu_entry_current_thread=0x0022a8d0
 xnu_entry_cpu_number=0x00000000
 xnu_entry_avail_cpus=0x00000004
 stub_hit=kernel_early_bootstrap
```

**`0x0022a8d0` is `init_thread`.** From the same image:

```
0022a250 b thread_template
0022a8d0 b init_thread
```

exp-181 could only say "the write did not fault, therefore the pointer was valid" — evidence from an
absence, the weakest kind this project has. This run prints the pointer, and it is the address of
the variable `thread_bootstrap` copied the template into and handed to `machine_set_current_thread`
at `thread.c:397-398`. The chain, end to end:

1. `thread_bootstrap` fills `thread_template`, copies it to `init_thread`, and calls
   `machine_set_current_thread(&init_thread)`;
2. that is `osfmk/arm/machine_routines_asm.s:38`, `mcr p15, 0, r0, c13, c0, 4` — the TPIDRPRW write;
3. `current_thread()` is `osfmk/arm/cpu_data.h:52`, `__builtin_arm_mrc(15, 0, 13, 0, 4)` — the read;
4. `arm_init.c:241` reads it, `:247` writes through it, and the probe at `:268` prints it —
   **after** `cpu_bootstrap`, `rtclock_early_init` and the whole timebase callback have run in
   between.

`cpu_number() = 0` is the second half, and it is a longer chain than it looks: it is
`getCpuDatap()->cpu_number`, and `getCpuDatap()` is `current_thread()->machine.CpuDatap`
(`cpu_data.h:79`). So the same register is read, the pointer `arm_init.c:249` stored is followed
(`&BootCpuData`), and the field `arm_init.c:222` set from `ml_get_boot_cpu_number()` is returned.
Three real XNU functions and two structure fields, and the answer is the right one.

`avail_cpus = 4` is the control from exp-177, unchanged: this is still the image that counted this
project's `/cpus` children.

## The frontier is now inside the callback's aftermath, and the callback completed

The previous run stopped at `__aeabi_uldivmod`, in the middle of `timebase_callback`'s reduction
loop. This run is past `rtclock_early_init` entirely, which means the loop finished, the constants
were written, and `arm_init` moved on to `kernel_early_bootstrap` (`arm_init.c:268`).

What that leaves unmeasured is the *answer*: `timebase_callback` writes

```c
rtclock_timebase_const.numer = (uint32_t)numer;
rtclock_timebase_const.denom = (uint32_t)denom;
rtclock_sec_divisor = divisor;                       /* freq->timebase_num / freq->timebase_den */
rtclock_usec_divisor = divisor / USEC_PER_SEC;
```

and in this tree both `rtclock_timebase_const` and `rtclock_sec_divisor` are macros onto
`RTClockData` (`osfmk/arm/rtclock.h:74,76`), a struct this file cannot lay out without XNU's
headers. But the values are reachable anyway, because `mach_absolute_time()` and
`absolutetime_to_nanoseconds()` are now real code and divide by exactly those constants. Printing
both at the next edge gives a ratio that can be checked against the frequency the device tree
advertises — `stage90_main.c:692` says 19200000, and `NSEC_PER_SEC / 19200000` is 625/12 — so the
ratio is the measurement, and it is self-checking: a different frequency produces a different
number, and a broken constant produces an impossible one.

## libgcc, and the mistake that had to be made to place it

`__aeabi_uldivmod` is the ARM EABI helper for 64-bit division and comes from libgcc. It is added
with `--start-group "$LIBGCC" --end-group`.

The first attempt added the group to the **final link only**, and it changed nothing: the link
succeeded, the symbol resolved — to the *stub*, at `0x0020d25c`. Pass 1 is what produces the
undefined set the stubs are generated from, and pass 1 did not have libgcc, so a stub for
`__aeabi_uldivmod` was generated; and the linker never looks inside an archive for a symbol that
some object already defines. The build would have run and stopped at the same symbol it stopped at
before, with a successful build log saying otherwise.

With the group in both links:

```
resolved:  __aeabi_uldivmod
added:     (nothing)
276 -> 275 undefined, 239 -> 238 function stubs, text 95492 -> 95916
```

+424 bytes of text, which is `__aeabi_uldivmod` and the `__udivmoddi4` it calls, both from the
archive. Nothing new is undefined: the group is at the end of the link and supplies only what
nothing else did.

libgcc rather than a division routine written here, deliberately — the same reasoning as
`entry_arm_rtabi.s`'s aliases, one step further. Where an XNU implementation exists the project
points the EABI name at it (`__aeabi_memcpy` → XNU's `bcopy`); where none exists, a second
implementation written by hand would be a number with nothing to check it against.

**This is the first run in which the entry image contains code that is neither XNU's nor this
project's.** It is the same image, 165248 bytes, with 424 bytes of someone else's tested arithmetic
in the middle of it.

| | exp-182 | now |
| --- | --- | --- |
| XNU objects linked | 21 | 21 |
| libraries | — | libgcc (2 members) |
| text | 95492 B | 95916 B |
| image | 165248 B | 165248 B |
| `.bss` | 0x00228390 – 0x0022c088 | 0x00228390 – 0x0022c088 |
| undefined | 276 (239 functions, 37 storage) | 275 (238 functions, 37 storage) |
| headroom | 1916792 B | 1916792 B |

The binary is the same 165248 bytes for the fourth run in a row. 2352 bytes have now gone into the
padding at the end of the text section without moving anything.

## What is next

`kernel_early_bootstrap`, which is `osfmk/kern/startup.c`. That file is large — it is where the
kernel's own initialization sequence lives — so this is a step whose size has to be measured before
it is taken, the way `thread.o`'s was in exp-179.

Nothing was flashed: `persistent_write_attempted = 0x00000000` in all 25 contracts that report it,
and the device returned to Android on its own.

## Reproduce

```bash
(cd stages/stage90/xnu_arm_boot && STAGE90_ENTRY_REAL_ARM_INIT=1 ./build_entry.sh)
#   ... entry point 0x00200074, text 95916, image 165248, 275 undefined, 238 stubs

(cd stages/stage90 && STAGE90_EXTRA_CFLAGS='-DSTAGE90_XNU_ENTRY=1' ./build.sh)
(cd stages/stage90 && ./run_and_capture.sh --allow-xnu-entry)
grep -n 'current_thread\|cpu_number\|avail_cpus\|stub_hit\|No errors detected' /tmp/cancro-last_kmsg.txt | tail -5

# the value the probe printed, against the variable it names
arm-none-eabi-nm out/stage90/xnu_arm_entry.elf | grep -w 'init_thread\|thread_template'

# the round trip, from both ends
sed -n '36,44p'  external/xnu-4570.1.46/osfmk/arm/machine_routines_asm.s
sed -n '50,54p'  external/xnu-4570.1.46/osfmk/arm/cpu_data.h

# what the group is for, and that the archive is where it comes from
arm-none-eabi-gcc -print-libgcc-file-name
arm-none-eabi-nm "$(arm-none-eabi-gcc -print-libgcc-file-name)" | grep -w '__aeabi_uldivmod\|__udivmoddi4'

# the constants the next probe will check indirectly
sed -n '128,140p' external/xnu-4570.1.46/osfmk/arm/rtclock.c
grep -n 'timebase-frequency' stages/stage90/stage90_main.c
```
