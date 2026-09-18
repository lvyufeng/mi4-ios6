# Experiment 184 — The timebase came out as the tree's 19.2 MHz, and a hand-written prototype wrote to address 0

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
MI4IOS6_STAGE90_XNU real XNU entry xnu_entry_mach_absolute_time_lo=0x038fc2ee
 xnu_entry_mach_absolute_time_hi=0x00000000
 xnu_entry_ns_per_tree_timebase_lo=0x3b9aca00
 xnu_entry_ns_per_tree_timebase_hi=0x00000000
 xnu_entry_uptime_secs=0x00000003
 xnu_entry_uptime_usecs=0x0001baae
 stub_hit=lck_mod_init

No errors detected
```

## `0x3b9aca00` is 1000000000, and it is the number that was predicted

`rtclock_early_init` completed in exp-183, which means `timebase_callback` ran to the end and wrote
the timebase constants — but it writes them into `RTClockData`, and both `rtclock_timebase_const`
and `rtclock_sec_divisor` are macros onto that struct (`osfmk/arm/rtclock.h:74,76`), which
`entry_stubs.c` cannot lay out without XNU's headers. So exp-183 ended with "the constants were
written" as an inference and no number.

The numbers are reachable anyway, because the functions that *divide by* those constants are real
code now. `absolutetime_to_nanoseconds` (`osfmk/arm/rtclock.c:433`) is

```c
*result = (t64 = abstime / rtclock_sec_divisor) * NSEC_PER_SEC;
abstime -= (t64 * rtclock_sec_divisor);
*result += (abstime * NSEC_PER_SEC) / rtclock_sec_divisor;
```

so asking it what 19200000 ticks are in nanoseconds is asking it whether `rtclock_sec_divisor` is
19200000 — the frequency this project's device tree advertises (`stage90_main.c:692`). If the
constants had been derived from any other frequency the answer would be a different number, and the
arithmetic cannot produce 1000000000 from a wrong divisor by accident. **It came out exactly
1000000000.** The timebase constants are built from the tree.

The other three numbers agree with it, and the agreement is of a kind that has to be checked rather
than assumed:

- `mach_absolute_time() = 0x038fc2ee` = 59753198 ticks ≈ 3.11 s. Non-zero and of the right
  magnitude, which is a fact about the hardware: CNTPCT is running and `ml_get_timebase`
  (`osfmk/arm/machine_routines_asm.s:976`) read it — the real assembly, which reaches the counter
  through TPIDRPRW, then `[r12, ACT_CPUDATAP]`, then `BootCpuData`'s base-timebase offsets.
- `uptime_secs = 3` and `uptime_usecs = 0x1baae` = 113326. `absolutetime_to_microtime` is
  `secs = abstime / rtclock_sec_divisor` then `usecs = rest / rtclock_usec_divisor`, and
  `rtclock_usec_divisor` is `divisor / USEC_PER_SEC` = 19200000/1000000 = **19**, not 19.2. So
  `(59753198 − 3×19200000) / 19` = 2153198/19 = 113326, while the true elapsed fraction is
  112146 µs. The 1180 µs between those two is XNU's own truncation, visible here because two paths
  through the same constants disagree in the fourth digit — and it is a *confirmation* rather than
  an anomaly: the printed `usecs` is only reachable if `sec_divisor` is 19200000 and the divisor
  for microseconds is 19.

This is the check exp-183 asked for, and it is the first measurement in this project that XNU's own
arithmetic produced from a value this project supplied.

## What the run before this one did, and why it printed nothing

The first attempt at this probe ended at

```
MI4IOS6_STAGE90_XNU stage90 xnu_entry: jumping to XNU's _start

No errors detected
```

with no `real XNU entry` line, no `stub_hit`, and no exception report — the only run in ten that
produced no output at all. Two separate defects produced that, and both are worth their own
paragraph.

### The prototype was not the real one, and a wrong prototype is a memory write

`osfmk/kern/clock.h:174,244`:

```c
extern void  absolutetime_to_microtime(uint64_t abstime, clock_sec_t *secs, clock_usec_t *microsecs);
extern void  absolutetime_to_nanoseconds(uint64_t abstime, uint64_t *result);
```

Both are `void` and write through out-parameters; `clock_sec_t` and `clock_usec_t` are `uint32_t`
(`clock.h:56-57`). The probe declared them as `uint64_t`-returning, and that is not a mistake any
tool in this build could see — there is no header in this image to compare the declaration against,
and a link checks names, not types. What the compiler emitted for `absolutetime_to_nanoseconds(
19200000ull)`:

```
20231c:  mov  r1, #0            ; the high half of the 64-bit constant
202320:  bl   absolutetime_to_nanoseconds
```

AAPCS passes a 64-bit first argument in r0:r1, so the *compiler* is right about `abstime`. But the
*callee* wants its second parameter — the result pointer — in r2, and the caller never set r2. So
the store `*result = ...` went through whatever was in r2, and r2 was left by the call immediately
before it, `mach_absolute_time`. That function is `ml_get_timebase`, whose last instructions are

```
20ca58:  ldr  r2, [r3, #72]     ; CPU_BASE_TIMEBASE_LOW
20ca5c:  adds r0, r0, r2
20ca60:  ldr  r2, [r3, #76]     ; CPU_BASE_TIMEBASE_HIGH
20ca64:  adc  r1, r1, r2
```

`BootCpuData.base_timebase_high` is never written by anything and is zero (the tick count this run
printed is 3.11 s since boot, which is what a zero base looks like), so **r2 was 0 and the probe
stored to address 0.**

### And the exception path could not report it

Address 0 is not mapped. `start.s` invalidates every L1 entry (`:166`) and then fills in exactly
three things: the 1 MB section around the current PC (`:186`), the kernel range
`[physBase, physBase + memSize)` = `[0x00200000, 0x00a00000)` (`:193`), and `HIGH_EXC_VECTORS`
(`:241`). So the store took a translation fault — and a fault is the one event `entry_stubs.c`'s
`fleh_*` handlers exist to report. They did not report it. The reason is the stack:

`start.s:310-311` sets SP once, in SVC mode, to `intstack_top - SS_SIZE`. It never writes a banked
stack pointer, and an exception does not keep the SVC stack — it banks to the mode's own. Whatever
is in SP_abt when `_start` finishes is what the payload left there, and the payload is explicit
about it (`stages/stage90/start.S:35-43`):

```
    cps     #0x17                    /* ABT */
    ldr     sp, =stage90_abt_stack_top
```

and that symbol is at **0x000b4400** (`arm-none-eabi-nm`), inside a payload that ends at
`__stage90_image_end = 0x0014e000`. Both are below XNU's `physBase` of 0x00200000, so under XNU's
page tables the handler's stack is unmapped. Its first push data-aborts, the abort is taken again
in the same mode on the same pointer, and the CPU recurses on itself until the watchdog resets the
device: a fault with no message, indistinguishable from a hang, which is exactly what the log
showed.

### The fix, in two parts

The probe now declares the real signatures and passes real out-parameters — the run above is its
output. And `entry_vectors.s` no longer lets a handler run on an inherited stack: each trampoline
loads SP from a literal before branching,

```
00201f40 <vec_tramp_4>:
  201f40:  ldr  sp, [pc]   ; 201f48 <vec_tramp_4_stack>
  201f44:  ldr  pc, [pc]   ; 201f4c <vec_tramp_4_handler>
  201f48:  .word 0x0022d580      ; entry_vectors_stack_top
  201f4c:  .word 0x00202470      ; fleh_dataabt
```

with the stack itself defined in `entry_vectors.s` — in this file rather than in `entry_stubs.c`
because the literal needs the stack top's address, and a C variable holding it would be one
dereference away; here the symbol *is* the address, so `.word` resolves it at link time. It is
`.bss`, inside the window, which is the only other thing XNU's page tables map.

The two abort handlers now also record the fault registers, which are valid only there: DFAR and
DFSR for data (`c6/c0/0`, `c5/c0/0`), IFAR and IFSR for prefetch (`c6/c0/2`, `c5/c0/1`). Which
vector fired says a fault happened; `dfar=0x00000000` says the image stored through a pointer it
set to zero, and `dfar=0x0020xxxx` says it was jumped to rather than reached. That distinction is
the whole diagnosis, and until this run the image could not make it.

The second fix is the more important one, because the first only removed the fault that found it.
Anything `arm_init` reaches from here that faults would have been silently reset again.

## The object

`osfmk/kern/startup.o` — 2728 bytes of text, 7 functions, 98 undefined references. Measured by
linking an empty object in its place:

```
resolved:  kernel_bootstrap  kernel_early_bootstrap  slave_main  vm_kernel_addrperm
added:     71, including bsd_init  bsd_early_init  ipc_bootstrap  thread_call_initialize
           timer_call_init  sched_init  vm_mem_bootstrap  vm_page_init_local_q  console_init
           kdp_init  oslog_init  machine_load_context  max_mem  serverperfmode  version
```

276 − 4 + 71 = 343 (299 functions, 44 storage).

Four resolutions for 71 new obligations: the same shape as `thread.o` in exp-179, and for the same
reason — `startup.c` is where the kernel's own initialization sequence lives, so its object is the
closure of everything that sequence calls. What the run *reached* is much smaller: one
`PE_parse_boot_argn` and then the probe. The other seventy are the closure growing ahead of the
execution, which is what the method says it does and is not progress.

| | exp-183 | now |
| --- | --- | --- |
| XNU objects linked | 21 | 22 (`osfmk/kern/startup.o`) |
| text | 95916 B | 100972 B |
| image | 165248 B | 181632 B |
| `.bss` | 0x00228390 – 0x0022c088 | 0x0022c390 – 0x002312c8 |
| boot_args offset | +188416 | +208896 |
| `topOfKernelData` | +2097152 | +2097152 |
| undefined | 275 (238 functions, 37 storage) | 343 (299 functions, 44 storage) |
| headroom | 1916792 B | 1895736 B |

The image is no longer the same binary twice: the 4 KB vector stack and the probe's own text pushed
it past the padding at the end of `.text` for the first time, and every derived offset in
`xnu_arm_entry.h` moved by a page as a result. exp-175's derivation is what makes that a rebuild
rather than a set of constants to re-check by hand.

One small thing worth recording because it looks like a check and is not one: the payload's
`xnu_entry_checksum` XOR-folds its own result struct, and it printed the same `0x9060249d` in this
run and the silent one before it — while `bss_end` and `args_pa` both moved by +0x1000, whose two
deltas cancel under XOR exactly. The layout checks that mean something are `checks=5`,
`failures=0`; the checksum is a transcription check, not a layout check.

## What is next

`timer_call_init`, which is `osfmk/kern/startup.c:238` and the statement after the probe's own
symbol in `kernel_early_bootstrap`. It is the second of the two calls that function makes:

```c
	lck_mod_init();
	timer_call_init();
```

and it is the first edge that lands in the allocator and the lock world — `timer_call_setup`,
`timer_init`, `lck_mtx_*`, `zinit`/`zalloc` are what the added list shows behind it. None of them
has ever run in this image. The step should be sized before it is taken, the way `timer.o` was in
exp-180 and `startup.o` was in this one.

Nothing was flashed: `persistent_write_attempted = 0x00000000` in all 25 contracts that report it,
and the device returned to Android on its own.

## Reproduce

```bash
(cd stages/stage90/xnu_arm_boot && STAGE90_ENTRY_REAL_ARM_INIT=1 ./build_entry.sh)
#   ... entry point 0x00200074, text 100972, image 181632, 343 undefined, 299 stubs

# what the object cost: 4 resolved, 71 added
printf '' > /tmp/empty.c && arm-none-eabi-gcc -mcpu=cortex-a15 -marm -c /tmp/empty.c -o /tmp/empty.o
(cd stages/stage90/xnu_arm_boot && STAGE90_ENTRY_REAL_ARM_INIT=1 \
   STAGE90_ENTRY_KERN_STARTUP_OBJ=/tmp/empty.o ./build_entry.sh)
comm -23 <(sort out/stage90/xnu_arm_entry_undef.txt) <(sort /tmp/u_with_startup.txt)   # resolved
comm -13 <(sort out/stage90/xnu_arm_entry_undef.txt) <(sort /tmp/u_with_startup.txt) | wc -l   # 71

(cd stages/stage90 && STAGE90_EXTRA_CFLAGS='-DSTAGE90_XNU_ENTRY=1' ./build.sh)
(cd stages/stage90 && ./run_and_capture.sh --allow-xnu-entry)
grep -n 'stub_hit\|mach_absolute_time\|timebase\|uptime\|No errors detected' /tmp/cancro-last_kmsg.txt | tail -8

# the signatures the probe has to match, and the constants the answers come from
sed -n '52,58p;170,178p;240,248p' external/xnu-4570.1.46/osfmk/kern/clock.h
sed -n '433,441p' external/xnu-4570.1.46/osfmk/arm/rtclock.c

# what the wrong prototype passed in r2: the last write to it before the call
arm-none-eabi-objdump -d out/stage90/xnu_arm_entry.elf --disassemble='ml_get_timebase'

# the stack the handler inherits, and that it is outside XNU's map
sed -n '35,43p'   stages/stage90/start.S           # the payload sets every banked stack
arm-none-eabi-nm out/stage90/stage90.elf | grep -Ew 'stage90_abt_stack_top|__stage90_image_end'
sed -n '308,313p' external/xnu-4570.1.46/osfmk/arm/start.s   # _start sets only the SVC one

# the fix, from both ends
arm-none-eabi-objdump -d out/stage90/xnu_arm_entry.elf --start-address=0x00201f40 --stop-address=0x00201f60
arm-none-eabi-nm out/stage90/xnu_arm_entry.elf | grep entry_vectors_stack

# the size of the next step
sed -n '226,245p' external/xnu-4570.1.46/osfmk/kern/startup.c
arm-none-eabi-nm -u out/xnu_kernel_obj/osfmk_kern_startup.o | grep -c timer_call_init
```
