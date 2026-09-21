# Experiment 515 — the branch moved, and the interrupt taken inside the window reads the same null `cpu_data`

**One line.** 514 named the wall and the boot argument that selects the other side of it. 515 **passes the
argument** — `up_style_idle_exit=1`, in both copies of the command line, with a `_Static_assert` that makes
an over-long string a build failure rather than a silently dropped tail — and wraps **both ends** of
`platform_cache_idle_enter` so the branch is *counted* instead of inferred from a fault that did not happen.
**The repair took, and the log says so twice.** The enter is entered once, from `cpu_idle+248`, and the
wrapper reads the kernel's own globals with the cache **on**: `xnu_live_pce_up = 1`,
`xnu_live_pce_ncpu = 1`, `xnu_live_pce_datap = 0x8051a000`. And the store to `0x130` that both of 514's runs
died on is **gone from both of 515's logs** — `sleh_far = 0x130` and `sleh_pc = 0x80045290` do not occur,
because the `else` branch does not run: the kernel's own test at `caches.c:414`, taken with the cache off,
now takes the `CleanPoU_Dcache()` arm. The boot then goes **one layer deeper** and stops there: neither the
`wfi` wrapper nor the exit wrapper is ever entered, so the fault is *inside* the window — an interrupt taken
while the D-cache is off, whose handler is Apple's own `locore_fleh_irq` prologue, which reloads TPIDRPRW,
calls `ml_get_timebase()` to stir the entropy pool (`locore.s:1421-1423`), and faults on
`ldr r2, [r3, #88]` with `r3 = 0` — the **same NULL `getCpuDatap()`**, now read asynchronously, at
`sleh_far = 0x58`, `sleh_pc = 0x80017058`, with the abort handler's own first load then faulting at
`0x8` and the storm running to its cap of 64. The window's state is also visible in the payload's *own*
counters, which **recede**: `xnu_live_sleh_seen` runs `1..8` and then `4..64` in **both** runs of this image,
where 514's ran `1..8` and then `1..64`.

## What the step was for

514 closed with a named wall and one boot argument:

    platform_cache_disable();                                 /* SCTLR.C = 0, isb */
    if (up_style_idle_exit && (real_ncpus == 1))
            CleanPoU_Dcache();                                /* reads nothing */
    else {
            cpu_data_t *cpu_data_ptr = getCpuDatap();         /* reads with C = 0  <-- 514's fault */
            FlushPoU_Dcache();
            cpu_data_ptr->cpu_CLW_active = 0;                 /* the store to 0x130 */
            ...

(`caches.c:402-430`.) `real_ncpus` is 1 because this port registers only the boot CPU, and
`up_style_idle_exit` was 0 because the payload's `CommandLine` did not carry it — so a uniprocessor was
being driven down the SMP arm of Apple's own idle-cache path, and that arm reads `getCpuDatap()` *after*
the cache has been turned off. 514 asked for two things: supply the argument, and **measure the read
instead of eliminating it**, with the branch *counted at both of its ends* rather than inferred from which
fault did not happen. That is what this step is.

## What the step changed

**The boot argument, twice, with a build failure where the old behaviour was a silent truncation.**
`stages/stage90/boot_args.c`'s `cmd[]` gains one token and loses three:

    debug=0x144 rd=md0 mi4ios6.stage=83 xnu-pe-init-false xnu-postpe cpu-topo bootcpu rtclock
    xnu-armvm prevm-pexpert dtinit-facts peid-machine no-pub-peinit no-pub-dtinit no-pub-peid
    no-pub-thread no-pub-cpuboot no-pub-rtclock up_style_idle_exit=1

— 244 characters, `sizeof` 245, against a `CommandLine` of `BOOT_LINE_LENGTH` = 256. Three facts decide
the spelling, and each is a defect that would have been silent:

* **It cannot be a bare word.** `PE_parse_boot_argn_internal` matches a bare token only when it begins with
  `-` (`pexpert/gen/bootargs.c:98-115`); every other token in this string is a flag of that form, which is
  why the port reads those with its own matcher. `name=value` goes through `getval`, and `max_len = 4` makes
  `argnumcpy` an `int32_t` — so the token is `up_style_idle_exit=1`, and `arm_init.c:287-289` sets the
  `boolean_t` at `arm_init.c:103` from it.
* **The field had 255 of its 256 bytes used.** So the string cannot simply grow. The three tokens dropped —
  `live-pmap`, `tlb-live`, `pmap-restore` — were checked before being dropped: their only readers are
  stage 85-88 sources, and none of them is in stage90's code pool.
* **`build_boot_args` copies the smaller of the two sizes and drops the *tail* silently.** That is the
  path by which exactly this token — the last one in the string — would have gone missing with no error
  anywhere, so the `_Static_assert(sizeof(cmd) <= BOOT_LINE_LENGTH, ...)` is the part of the change that
  matters more than the token.

The device tree's `/chosen` `boot-args` copy (`stage90_main.c`) is kept in agreement, as it has been since
459: it is not what XNU's parser reads, but this port's own contracts read it
(`xnu_pe_init_platform_false.c:410-413`, `xnu_pexpert_hook_readiness_contract.c:163-164`), and two copies of
one decision that are not compared is this project's most-repeated defect.

**The two wraps, and why their counters are the ones that can be trusted.** `build_entry.sh` gains
`--wrap=platform_cache_idle_enter` and `--wrap=platform_cache_idle_exit` beside 514's `cpu_idle_wfi`
(the census is now **75** wraps: 63 reached by a branch, 1 same-object-only, 1 never called, 10 by address).
The enter wrapper takes its reading **before** the real call and the exit wrapper takes its **after** the
real call — i.e. **both are taken with the cache on**, because `platform_cache_disable()` (`caches.c:406`)
is the first thing the real enter does and `platform_cache_idle_exit` ends by setting `SCTLR_DCACHE`
again (`caches.c:490-495`). That is not tidiness; it is the property that makes a *count* here a reading,
and this step's own log now shows why: 514's `g_wfi_calls` is incremented *inside* the window, and 515's
run exhibits precisely the hazard that puts it in doubt (see **The counter that recedes**, below).

**A payload-side check, in the payload build.** The entry build runs first, so a check there that reads
`out/stage90/boot_args.o` would be reading the previous build's object — 460's defect class. The two
command-line copies are therefore checked in `stages/stage90/build.sh`, which has both artifacts of one
build in hand: the argument's *name* is taken from the entry image's own literal (`strings` over
`.text`), and both `boot_args.o` and `stage90_main.o` must carry ` up_style_idle_exit=1` and be within the
field. The three readers in that block are written so that they do not kill their producer on a closed
pipe: the first version used `awk ... exit` and `head -1` against `strings` on a 5.5 MB image, died with
status **141** and printed nothing, which is the same silent death the entry build's own clause warns
about.

## What the build checks

`build_entry.sh`'s `xnu_entry_515` clause asserts, from the linked image rather than from the sources:

* the name `arm_init`'s parse site passes to `PE_parse_boot_argn` occurs **exactly once** as a literal in
  the image's `.text` (`objcopy -O binary --only-section=.text`, then `strings` — the whole-ELF count is 2
  because `.debug_str` and `.strtab` each carry a copy that is not in the image, and `--only-section=.rodata`
  yields 0 bytes because this image has no `.rodata`), and `PE_parse_boot_argn` is not in pass 1's undefined
  set, i.e. the parser is the real one;
* `up_style_idle_exit` (`0x8054d104`) is one 32-bit `.bss` word and `real_ncpus` (`0x80520378`) one 32-bit
  `.data` word whose value **read out of the image with `od` is 1**;
* inside `platform_cache_idle_enter` (`0x80046238..0x800462d4`) each of the two operands is materialised by
  a `movw`/`movt` pair, the `real_ncpus` one is `cmp`'d against `1`, the up arm calls `CleanPoU_Dcache` and
  the else arm `FlushPoU_Dcache`, `getCpuDatap()` appears as `mrc cr13,cr0,{4}` followed by
  `ldr [r?, #1484]` (this configuration's `ACT_CPUDATAP`, read from `out/xnu_assym/STAGE90_XNU/assym.s`),
  and the store to `[r?, #304]` that 514's runs faulted on is there;
* that function is wrapped at both ends, each wrapper is entered from inside `cpu_idle` **only** (1 site
  each, none outside), and the wrapper's own `getCpuDatap()` window is a three-instruction
  `mrc` + conditional load — requiring the load to be the literal next instruction was this check's first
  failure, because gcc compiles the null-guarded read to `ldrne`.

Payload side: `up_style_idle_exit=1` in the entry image's literal, ` up_style_idle_exit=1` in both
`boot_args.o` and `stage90_main.o`, and each copy under 256 bytes. 129 fixture mutations refused;
`xnu_entry_failures = 0`.

## The readings

Both runs of this image are **identical in every reading below**; run 1's log is 572179 bytes and run 2's
571845 (`/tmp/515-run1-kmsg.txt`, `/tmp/515-run2-kmsg.txt`).

    xnu_live_pce_seq     = 1          the enter wrapper ran exactly once
    xnu_live_pce_caller  = 0x8000cf24 = cpu_idle+248, the bl at 0x8000cf20
    xnu_live_pce_up      = 1          up_style_idle_exit, read with the cache ON
    xnu_live_pce_ncpu    = 1          real_ncpus, read with the cache ON
    xnu_live_pce_datap   = 0x8051a000 getCpuDatap(), read with the cache ON - BootCpuData
    xnu_live_pcx_seq     = absent     the exit wrapper never ran, in either run
    xnu_live_wfi_seq     = absent     the wfi wrapper never ran, in either run
    sleh_far = 0x130     = absent     the store 514's two runs died on: not in this log
    sleh_pc  = 0x80045290 = absent
    sleh_pc  = 0x80017058  the first record of the final series: ml_get_timebase+36
    sleh_far = 0x58        with r3 = 0: 0 + CPU_BASE_TIMEBASE_LOW
    sleh_lr  = 0x8001aae8  inside fleh_irq_handler, in the entry image's locore_fleh_irq
    sleh_cpsr = 0x60000093 SVC mode, I and F set - the interrupted context
    sleh_thr  = 0xc056c480 the interrupted thread
    xnu_live_irq_spurious_count = 1   one interrupt, and no client claimed it

`cpu_idle`'s own body confirms the shape rather than the claim: `bl __wrap_platform_cache_idle_enter` is at
`0x8000cf20`, then `__wrap_cpu_idle_wfi` at `0x8000cf30`, then `__wrap_platform_cache_idle_exit` at
`0x8000cf34` — so a run that publishes `pce` and neither of the other two has taken its fault **inside the
enter call**, between `caches.c:406`'s `platform_cache_disable()` and the pair's second call.

**Why the reading `pce_up = 1` is a reading and not a claim.** It is taken in the wrapper, one instruction
before the kernel disables the cache, so it is the CPU's own value and not what a cache-off read would
return — and this matters here for a specific, measured reason: the *kernel's* own test at `caches.c:414`
is a cache-off read of the same word, and it also took the up arm (that is why `0x130` is gone). So the
branch moved under both readings, and the second is the one that counts, because it is the one the code
being repaired actually makes.

### The first record of the final series says where the boot now stops

`pc = 0x80017058` is `ml_get_timebase+36`, and the instruction there is

    80017054: ldr  r3, [ip, #1484]     ; 0x5cc    getCpuDatap()
    80017058: ldr  r2, [r3, #88]       ; 0x58     CPU_BASE_TIMEBASE_LOW   <-- fault, fsr 0x005

i.e. `osfmk/arm/machine_routines_asm.s:988-989` of Apple's own `ml_get_timebase`. `lr = 0x8001aae8`
places the call inside the entry image's `locore_fleh_irq`, at the point where Apple's own IRQ prologue
stirs the entropy pool:

    locore.s:1421   mrc  p15, 0, r9, c13, c0, 4     // Reload r9 from TPIDRPRW
    locore.s:1422   bl   EXT(ml_get_timebase)       // get current timebase
    locore.s:1423   LOAD_ADDR(r3, EntropyData)

So the fault is not in this port's code at all. It is Apple's IRQ entry, running because an interrupt was
taken **inside the cache-off window**, reading the same expression 514 named — `getCpuDatap()`, whose
definition is `current_thread()->machine.CpuDatap` (`cpu_data.h:79`) — and getting 0. The record's own
`_sp` (`0x80518000`) is the CPU's interrupt stack, which the prologue reached through
`CPU_ISTACKPTR`, so the prologue's *earlier* reads of cpu_data answered; the one inside `ml_get_timebase`
did not. Then the abort handler's first load (`ml_at_interrupt_context+0x14`, `[NULL, #8]`) faults the same
way, and because the CPU is already in abort mode the second abort re-enters the handler: `sp` descends
0x248 a record and `xnu_live_sleh_seen` runs to its cap of **64**, exactly as in 514.

### The counter that recedes is the window, measured by this image's own numbers

`g_sleh_seq` (`0x80544afc`) has exactly one writer, `g_sleh_seq++` (`entry_stubs.c:1641`), and its live
records are published while the value is at most 8 (`SLEH_LIVE_MAX`) and its `_seen` key while it is at most
64. In **both** runs of this image the sequence is

    1 2 3 4 5 6 7 8      then      4 5 6 7 8 9 ... 64

and in **both** of 514's runs it was `1..8` then `1..64`. A counter with one writer cannot decrease — unless
the increment is a read-modify-write against a **different copy of the same line**, which is exactly what a
read with `SCTLR.C = 0` gets: the value that reached memory, not the value in the cache. The reading is
therefore two numbers about one line — the channel was publishing **8** with the cache on, and the abort
handler's read got **3** — and the three-point difference is where in the run the window opened. It also
dates the window's own clean: `CleanPoU_Dcache()` in the up arm (`caches.c:415`) would have written the
cached 8 back, so the first in-window abort would then have published **9**; it published 4, so the
interrupt arrived **between `platform_cache_disable()` and that clean**.

This is the hazard 514's design note described as a caution about its own `g_wfi_calls`; here it is not a
caution but a measurement, and it is a second, independent witness that the D-cache is off for the whole
storm — independent of `pcx`/`wfi` being absent, and independent of any inference from which fault did not
happen. The live channel itself keeps working through it because the payload's ram console is mapped
uncached, which is why these records exist at all.

## What is owed

* **516: measure the window from inside it, with instructions that do not need the cache.** Three readings
  of the *same* expression in three cache states — (a) the enter wrapper's, before the real call, cache on
  (**0x8051a000**, measured); (b) right **after** the real enter returns, cache still off, i.e. after the
  up arm's `CleanPoU_Dcache()`; (c) the exit wrapper's, after `caches.c:490` has set `SCTLR_DCACHE` again.
  (b) is the discriminator the step does not have: if it is `0x8051a000`, the clean does restore the field
  and the fault above is a race with a window a few instructions wide; if it is 0, the clean does not, and
  the port's `CleanPoU_Dcache` is the thing to look at. Alongside it, wrap `ml_get_timebase` and publish
  `TPIDRPRW`, `[TPIDRPRW + ACT_CPUDATAP]` and `SCTLR` — a coprocessor read needs no cache — so the
  IRQ-side read is taken the same way; and publish `TPIDRPRW` in the enter wrapper too, because that is
  the alternative explanation this step cannot exclude: that the read is 0 because TPIDRPRW no longer
  points at the thread whose field the wrapper read.
* **The counter recession is owed a check of its own.** `g_sleh_seq` receding from 8 to 3 is a *reading*
  about a cache line this step's instrument happens to share with nine record words. A step that reads one
  word twice — once with the cache on, once with it off, from a place that knows both — turns the
  interpretation used above into a measurement.
* **The halt is still not reached, and 514's falsifier is still negative.** `xnu_live_wfi_seq` with
  `_inst = 0xe320f003` and `_ticks > 0`, then `platform_cache_idle_exit`, `ClearIdlePop` and `cpu_idle_exit`,
  and the park's `poll` returning (`xnu_live_poll_seq` 3 and `xnu_live_poll_over`) — none of it. `poll_seq`
  is 1 and 2 in both runs, the OS console block still holds **three** `mini4:` lines and none of the five
  that are printed after the park's real call returns, and the run still ends in the storm. 515 moved the
  frontier from *four instructions before the `wfi`* to *inside the window the `wfi` sits in*; it did not
  cross it.
* **Carried, unchanged**: 513's other two repairs (install a handler in the IPI slot at
  `machine_routines.c:605`, or accept the spin) are still the alternatives if the idle cannot be made safe;
  514's own content is still unmeasured (the park window's `w_calls`/`w_exits`/`w_sip`/`w_wfi`/`w_sleep`,
  whose totals are `0x8000`, `0x8000`, 1, 0, 0); the captured console's census question is unchanged
  (`xnu_live_ostext_chars` is 1006 with `_heals = 1` and `_at = 0x000483cc` in both of this step's runs);
  the OS's own reboot path (`reboot_kernel` → `host_reboot` → `halt_all_cpus` → `PEHaltRestart`, with
  `PE_halt_restart` an unfilled `.bss` slot) is still unwired; 512's list (the telemetry copy loop on pid 1's
  own thread, `_cpsr = 0x10` on the AST records, `_entry_hi`), 508's a-record-that-cannot-be-lost, 507/506's
  `p->p_xstat`, the empty `xnu_entry_why` and the `trap record:` gate, 505's corpse-path slot `0x802933b4`,
  504's `mdevadd_base`/`mdevopen`/second `read`, 503's leeway row, 502's long list.

## The image

`.text` 5,292,096 → **5,296,832** (+4736: the two wrappers, their sixteen record words and the argument's
own literal), entry image **5,519,996** bytes — unchanged, and *not* an error: the copied span is
`[0x80000000, __bss_start)` and `__bss_start` is pinned at **0x80543a80** in both images, so growth in
`.text` eats the fill below `.data` (0x80510000) rather than moving the end. `.bss` `0x80543a80` …
`0x8059c530` = **363184** bytes (`xnu_entry_bss_bytes = 0x00058ab0`, +64 for the sixteen 4-byte words),
entry point `0x80000074`, `xnu_entry_copied_bytes = 0x00543a7c`, device tree at `0x806e0000` + `0x744c`,
boot_args at `0x8059e000`, `topOfKernelData` `0x80700000`, headroom 1,456,848 bytes. Wrap census **75**.
The symbols this step reads: `up_style_idle_exit` **0x8054d104** (one 32-bit B) and `real_ncpus`
**0x80520378** (one 32-bit D holding 1), `platform_cache_idle_enter` `0x80046238`,
`platform_cache_idle_exit` `0x800462d4`, `cpu_idle` `0x8000ce2c`, `ml_get_timebase` `0x80017034`,
`cpu_idle_wfi` `0x8001683c`; the wrappers at `0x8047c590`, `0x8047c5d8`, `0x8047c544`. Payload sha256
`5b24486a…` (`stage90-qcdt.img`), `6a996e07…` (`stage90.bin`), `ebf45b73…` (`stage90.elf`), `52bc9c35…`
(`stage90_fixture.macho`). 129 fixture mutations refused; falsifiers negative (`xnu_entry_failures = 0`, no
`trap record:`, no `pid 1 exited`, no panic, `No errors detected`).

## Safety

Two runs, both non-persistent `fastboot boot` through `stages/stage90/preflight_boot_check.sh
--allow-xnu-entry` and `stages/stage90/run_and_capture.sh --allow-xnu-entry`, nothing flashed, gate and run
both exiting 0, and **the device back on Android on its own both times** (`MI 4LTE`, release 10, `adb
devices` reporting `4a2fe00b device`) — which is the property that matters for a step whose run ends in a
fault storm rather than in a `platform_reboot`: the storm is in the kernel's abort path, and the hardware
watchdog is the net that covers it, exactly as the gate's own text says.

    python3 tools/check_experiment_index.py    # ok: 492 row(s) across 91 stage column(s)
