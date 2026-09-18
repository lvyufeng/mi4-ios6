# Experiment 188 — The timer table converted into ticks, and the frontier is `do_cpuid`

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
MI4IOS6_STAGE90_XNU real XNU entry xnu_entry_tcoal_src_idle_ns=0x004c4b40
 xnu_entry_tcoal_src_qos0_ns=0x000f4240
 xnu_entry_tcoal_idle_abstime=0x00017700
 xnu_entry_tcoal_resort_abstime=0x000ea600
 xnu_entry_tcoal_bg_shift=0xfffffffb
 xnu_entry_tcoal_qos3_abstime_max=0x0015f900
 xnu_entry_tcoal_qos4_abstime_max=0x0b71b000
 xnu_entry_past_deadline_adj=0x000000c0
 stub_hit=do_cpuid

No errors detected
```

Eight values, all eight predicted before the run from the two source files, and the name of the
next edge — also predicted. It is the first run in this sequence where the check is against a
table's own source rather than against a single constant, and the first whose object is a driver.

## Two objects, four numbers each, and the arithmetic between them

`osfmk/arm/arm_timer.o` contributes one 168-byte table, `tcoal_prio_params_init`
(`arm_timer.c:256`). `osfmk/kern/timer_call.o` — linked since exp-185 — owns `tcoal_prio_params`,
the destination (`timer_call.h:173`), and `timer_call_init_abstime` filled it by running every
threshold in that table through the real `nanoseconds_to_absolutetime`. The four source values and
the four derived ones are on opposite sides of that call, so the pair is a check of the link *and*
of the arithmetic:

| read at | source (`timer_queue.h:114`) | ns | derived (`timer_call.h:150`) | ticks |
| --- | --- | --- | --- | --- |
| ns[0] | `idle_entry_timer_processing_hdeadline_threshold_ns` = `5000 * NSEC_PER_USEC` | 5,000,000 | ab[4] `..._abstime` | **96,000** = 0x00017700 |
| ns[96] | `latency_qos_ns_max[0]` = `1 * NSEC_PER_MSEC` | 1,000,000 | ab[104] `latency_qos_abstime_max[0]` | 19,200 |
| ns[8] | `timer_resort_threshold_ns` = `50 * NSEC_PER_MSEC` | 50,000,000 | ab[12] `..._abstime` | **960,000** = 0x000ea600 |
| ns[16] | `timer_coalesce_bg_shift` | −5 | ab[20] `timer_coalesce_bg_shift` | **−5** = 0xfffffffb |
| ns[120] | `latency_qos_ns_max[3]` = `75 * NSEC_PER_MSEC` | 75,000,000 | ab[128] `abstime_max[3]` | **1,440,000** = 0x0015f900 |
| ns[128] | `latency_qos_ns_max[4]` = `10000 * NSEC_PER_MSEC` | 10,000,000,000 | ab[136] `abstime_max[4]` | **192,000,000** = 0x0b71b000 |

`19200000` is the divisor: `abstime = ns × rtclock_sec_divisor / NSEC_PER_SEC`, with
`rtclock_sec_divisor = 19200000` from the timebase this project's device tree advertises. That
constant was measured from both directions in exp-184 (19200000 ticks → exactly 1,000,000,000 ns)
and exp-186 (1,000,000,000 ns → exactly 19200000 ticks); this run spends it four more times, on
numbers chosen to land on different residues of it.

The eighth value is a separate global: `xnu_entry_past_deadline_adj = 0x000000c0` is
`past_deadline_timer_adjustment` after `nanoseconds_to_absolutetime(PAST_DEADLINE_TIMER_ADJUSTMENT_NS)`
with `PAST_DEADLINE_TIMER_ADJUSTMENT_NS` = 10,000, which the compiled code shows as a bare
`movw r0, #10000` — 10,000 × 19200000 / 10⁹ = 192.

## Why the offsets are measured and not read off the header

This image includes no XNU headers, so the probe cannot name a structure member; it reads words at
offsets. The offsets are taken from **the disassembly of `timer_call_init_abstime` in this image's
own `osfmk_kern_timer_call.o`** — the one function that reads the source struct and writes the
destination struct — so each one is what the compiler used, on both sides:

```
ldr r0, [r4]        -> str r1, [r6, #4]      source 0    dest 4     idle
ldr r0, [r4, #8]    -> str r1, [r6, #12]     source 8    dest 12    resort
add r0, r4, #12     -> add r1, r6, #16       shifts, 5 words, 12 -> 16
ldrd r8, [r4, #32]  -> add r2, r6, #40       rt_ns_max 32 -> 40
ldr r0, [r4, #72]   -> str r0, [r6, #80]     scale 72 -> 80
ldrd r8, [r4, #96]  -> add r2, r6, #104      qos[0] 96 -> 104
ldr r2, [r4, #144]  -> str r2, [r6, #152]    rate_limited 144 -> 152
```

`r4` is `timer_call_get_priority_params()`'s return and `r6` is `tcoal_prio_params`; both are
`movw`/`movt` pairs with relocations, so the compiler is telling us which global each register
holds. The two layouts differ by exactly the one word `powergate_latency_abstime`
(`timer_call.h:151`) that the abstime struct has in front of it, which is why the same member sits
four bytes further along in `r6` than in `r4` — and reading the header instead of the code is
exactly how that four would be missed.

`timer_call_init_abstime` is `static` and no longer exists as a symbol: it was inlined into
`timer_call_init`, which is why the disassembly above is labelled `<timer_call_init>`. Its
`for (i = 0; i < NUM_LATENCY_QOS_TIERS; i++)` is also fully unrolled — six `nanoseconds_to_absolutetime`
calls with `add r2, r6, #104/#112/#120/#128/#136/#144`, which is where the qos offsets come from.

The 168 is a second, independent check on the offsets: `timer_coalescing_priority_params_ns_t` at
those offsets ends at 144 + 24 = 168, and `arm-none-eabi-size` reports `osfmk_arm_arm_timer.o` as
`1044 text, 168 data`. The table linked and the struct read through it are the same shape, and that
is a fact about the object rather than about the probe.

## The call is the check that the object displaced the stub

The probe calls `timer_call_get_priority_params()` — the real one, now linked — and reads the four
source values *through its return value*. A stub returns 0, so if the definition had not been
displaced every one of those reads would fault at address 96, and the data-abort handler added in
exp-184 would have reported `dfar=0x00000060` instead of values. The three probes of exp-185/186/187
confirmed a newly linked object by a value it wrote into a *global*; this one confirms it by the
return value of the function the object exists to provide.

`ab[20]` is the sharpest of the eight for a different reason: the five shift fields are copied as
one 20-byte block by `vld1.32 {d16-d17}` / `vst1.32`, not field by field, and
`timer_coalesce_bg_shift` is −5 — the third word of that block. Zeroed memory, a stale copy, or a
copy of the wrong member all give 0 or a positive number there; only the right block copied to the
right place gives 0xfffffffb.

## What the stop establishes

`do_cpuid` is the first symbol `cpu_init` asks for, and `cpu_init` is the statement in `arm_init`
immediately after `kernel_early_bootstrap()`. So stopping here means the image ran, in order:

- the whole of `timer_call_init_abstime`, every threshold in arm_timer.c's table converted with the
  real `nanoseconds_to_absolutetime` — the first code in this image to do XNU's own arithmetic over
  a table of XNU's own constants;
- the return out of `timer_call_init` and out of `kernel_early_bootstrap` (`startup.c:225`), which
  ends with `timer_call_init()` — **there is nothing left in either of them**;
- `arm_init`'s next statement, `cpu_init()` (`cpu.c:198`), through `getCpuDatap()` and
  `timer_call_queue_init` — the latter wrapping `mpqueue_init` over `timer_call_lck_grp`, the group
  exp-187 read out of the list;
- the `cdp->cpu_type != CPU_TYPE_ARM` test, which was **true**: the run reports `do_cpuid` and not
  `pmap_cpu_data_init`, so this is the boot CPU (`arm_init.c:250` set `current_thread()->machine.CpuDatap`
  to `&BootCpuData`) and its `cpu_type` was still 0.

`kernel_early_bootstrap` is now exhaustively real. The frontier is one function further out in
`arm_init`, not one symbol further along `startup.c`.

## Placing a probe one function out

The last three probes were placed inside the function the frontier lived in. This one could not be:
`timer_call_init_abstime` ends with the QoS loop, `timer_call_init` ends with it, and
`kernel_early_bootstrap` ends with `timer_call_init()` — so the first symbol the run reaches that
nothing defines belongs to the *caller*, and the rule that finds it is the same one:

> read the function the frontier is in, statement by statement, and take the next symbol in it that
> nothing defines — then check every call between the last stop and the probe is defined in an
> object already linked.

The check is what makes it cheap here: between the last stop and `do_cpuid` the image calls
`nanoseconds_to_absolutetime` (rtclock.o, exp-184), `getCpuDatap` (a macro over
`current_thread()`), and `timer_call_queue_init`/`mpqueue_init` (timer_call.o, exp-185). All real.

`cpu_init`'s `if` also has two possible first calls, and only one of them is on the path this run
takes. Both are defined in the probe — `do_cpuid` and `pmap_cpu_data_init` — so that a run that
took the other branch would report it by name instead of silently stopping somewhere further on.
This costs nothing and removes a case where the *absence* of a value would have to be interpreted.

## What the object cost: 10 resolved, nothing added

```
resolved (10): quantum_timer_set_deadline  timer_call_cpu  timer_call_get_priority_params
               timer_call_nosync_cpu  timer_intr  timer_queue_assign  timer_queue_cancel
               timer_queue_cpu  timer_resort_threshold  timer_resync_deadlines
added:         (nothing)
336 -> 326 undefined; text 127692 -> 128716; image 198440 -> 198608
```

Every one of the object's 11 references is already satisfied by an object the image links, and
1044 bytes of text bought ten symbols back — the largest resolution count since exp-187's 18, for
a tenth of the size. `timer_resort_threshold` and `timer_queue_assign` are the interesting two:
they are the *variables* the early timer code reads, so the image now carries the timer driver's
state, not only its code.

The baseline moved from 337 to 336 for a reason worth recording, because it is not the object: the
probe's own definitions changed in two directions. It no longer defines
`timer_call_get_priority_params` (+1 undefined, since `timer_call.o` references it) and it now
defines `do_cpuid` and `pmap_cpu_data_init` (−2). Hence 337 → 336 → 326 rather than 337 → 327.

| | exp-187 | now |
| --- | --- | --- |
| XNU objects linked | 27 | 28 (`osfmk/arm/arm_timer.o`) |
| text | 127692 B | 128716 B |
| image | 198440 B | 198608 B |
| `.bss` | 0x002304a8 – 0x00235a08 | 0x00230550 – 0x00235ac8 (21880 B) |
| boot_args offset | +225280 | +225280 |
| `topOfKernelData` | +2097152 | +2097152 |
| undefined | 337 (291 functions, 46 storage) | 326 (280 functions, 46 storage) |
| headroom | 1877496 B | 1877304 B |

The image grew by exactly the table's 168 bytes: `arm_timer.o`'s 1024 bytes of text went into the
fixed 128 KB text region and did not move the file's size, which is the same effect exp-186
recorded for the strncpy/strnlen pair. `xnu_entry_checks=5` / `xnu_entry_failures=0` re-checked
exp-175's four layout invariants.

## What is next

`do_cpuid` is `osfmk/arm/cpuid.c:85`, and `cpuid.o` is **850 bytes of text**:

```
00000000 T do_cpuid          00000128 T do_debugid
0000012c T arm_debug_info    00000130 T do_mvfpid
00000134 T arm_mvfp_info     00000138 T do_cacheid
00000020 T cpuid_info        0000002c T cpuid_get_cpufamily
000002dc T cache_info
```

Five of those are undefined in this image right now — `do_cacheid`, `do_mvfpid`, `do_debugid`,
`cpuid_info` and `cache_info`; `do_cpuid` is not, because this experiment's probe defines it — and
`cpu_init` calls four of them in a row, then falls through to
`switch (cpu_info_p->arm_info.arm_arch)`, which decides `cdp->cpu_subtype` from `cpuid_info()`.
So the step resolves five for 850 bytes, the best ratio in the sequence so far, and the work it
brings is reading the CPU: `do_cpuid` reads MIDR, `do_cacheid` reads CLIDR/CCSIDR/CSSELR,
`do_mvfpid` reads MVFR0/1.

`cpuid.o`'s own undefined set is nine `machine_*` readers plus `vm_cache_geometry_colors`, and the
nine are all in **`machine_cpuid.o`** — 220 bytes of text, nine functions and two `.bss` structs,
and **no undefined references of its own**:

```
T machine_read_midr  machine_read_clidr  machine_read_ccsidr  machine_write_csselr
  machine_read_isa_feat1  machine_do_debugid  machine_do_mvfpid
  machine_arm_debug_info  machine_arm_mvfp_info
```

So the step is `cpuid.o` + `machine_cpuid.o`, 1070 bytes of text, and its cost was **measured on
the host** by linking both into this image's pass 1: **5 resolved and 1 added**.

```
resolved (5): cache_info  cpuid_info  do_cacheid  do_debugid  do_mvfpid
added (1):    vm_cache_geometry_colors
326 -> 322 undefined, 280 -> 275 functions, 46 -> 47 storage
```

`vm_cache_geometry_colors` is a `.bss` global defined only in `osfmk/vm/vm_resident.o` — a file
this image does not link — so it would be the first storage stub in a while. That measurement is
the only thing in this document taken on the host rather than from the device, and it is the
number exp-189 will be checked against. And this is the step where the frontier stops reading
values XNU computed and starts reading values *the hardware* reports: MIDR, CLIDR, CCSIDR and
MVFR0/1 are the first registers in this sequence that come off the CPU rather than out of a table.

Nothing was flashed: `persistent_write_attempted=0x00000000` in all 25 contracts that report it,
and the device returned to Android on its own.

## Reproduce

```bash
(cd stages/stage90/xnu_arm_boot && STAGE90_ENTRY_REAL_ARM_INIT=1 ./build_entry.sh)
#   ... entry point 0x00200074, text 128716, image 198608, 326 undefined, 280 stubs

# what the object cost: 10 resolved, nothing added
printf '' > /tmp/empty.c && arm-none-eabi-gcc -mcpu=cortex-a15 -marm -c /tmp/empty.c -o /tmp/empty.o
(cd stages/stage90/xnu_arm_boot && STAGE90_ENTRY_REAL_ARM_INIT=1 \
   STAGE90_ENTRY_ARM_TIMER_OBJ=/tmp/empty.o ./build_entry.sh)
comm -13 <(sort out/stage90/xnu_arm_entry_undef.txt) <(sort /tmp/u_without_arm_timer.txt)   # 10
comm -23 <(sort out/stage90/xnu_arm_entry_undef.txt) <(sort /tmp/u_without_arm_timer.txt)   # nothing

(cd stages/stage90 && STAGE90_EXTRA_CFLAGS='-DSTAGE90_XNU_ENTRY=1' ./build.sh)
(cd stages/stage90 && ./run_and_capture.sh --allow-xnu-entry)
grep -n 'tcoal\|past_deadline\|stub_hit\|No errors detected' /tmp/cancro-last_kmsg.txt | tail -10

# the offsets, from the compiled code rather than from the header
arm-none-eabi-objdump -dr out/xnu_kernel_obj/osfmk_kern_timer_call.o | \
   awk '/<timer_call_init>:/{f=1} f{print} f&&/^$/{exit}' | grep -E 'ldr|ldrd|str|add r[12], r6'

# the table, and why it is 168 bytes
arm-none-eabi-objdump -s -j .data out/xnu_kernel_obj/osfmk_arm_arm_timer.o
arm-none-eabi-size out/xnu_kernel_obj/osfmk_arm_arm_timer.o
sed -n '114,130p' external/xnu-4570.1.46/osfmk/kern/timer_queue.h
sed -n '150,172p' external/xnu-4570.1.46/osfmk/kern/timer_call.h
sed -n '256,280p' external/xnu-4570.1.46/osfmk/arm/arm_timer.c

# the size of the next step, and what its dependencies cost
arm-none-eabi-size out/xnu_kernel_obj/osfmk_arm_cpuid.o out/xnu_kernel_obj/osfmk_arm_machine_cpuid.o
arm-none-eabi-nm -u out/xnu_kernel_obj/osfmk_arm_cpuid.o
arm-none-eabi-nm -u out/xnu_kernel_obj/osfmk_arm_machine_cpuid.o      # empty - self-contained
# then add both to LINK_OBJS in build_entry.sh, rebuild pass 1, diff, and take them back out:
#   326 -> 322 undefined, 280 -> 275 functions, 46 -> 47 storage
sed -n '198,230p' external/xnu-4570.1.46/osfmk/arm/cpu.c
```
