# Experiment 481 — the decrementer gets an owner, at Apple's own point in `arm_init`

**480 closed with the sentence this step is the execution of**: "**The timer.** `ml_init_timebase` plus an
MSM8974 `tbd_ops_t` over the GPT at `0xf9020000` (19.2 MHz, IRQ 19), and `IOCPUInterruptController`
behind it." The GPT and the interrupt controller are 482's; the registration is this step's, and it went
in at the one point Apple's own armv7 kernel registers a timebase — inside `PE_init_platform`, from
`arm_init_cpu`, with `args` = `BootCpuData`.

Four kinds of reading are in the log for the first time, and they are of four different kinds:

  - **The registration was measured, not asserted.** `rtclock_timebase_func` has been zero for the whole
    of this walk because the call that fills it is not in the object at all; this step's log has the
    three pointers this image handed to `ml_init_timebase`, the argument it was handed as, and the call's
    *order* in the linked image (`__wrap_PE_init_platform` → `cpu_timebase_init` →
    `__wrap_fiq_context_init`), which is the order Apple's source has and no comment can prove.
  - **The four `struct cpu_data` words Apple's own copy wrote were read back at the first
    `fiq_context_init`, and three of them have predicted values**: `0x80007840`, `0x80007868`,
    `0x80007848` — the same three addresses the registration was given — and `0x7fffffff` at
    `CPU_DECREMENTER`. A non-zero would have been a claim; these are the values.
  - **The hardware counted.** `CNTV_TVAL` was written with `0x00100000`, read, spun on, read again:
    `0x000ffffa` → `0x000ffccd`, **down 813**, while `CNTVCT` went `0x069f343a` → `0x069f3767`, **up
    813**. Two registers, one rate, and the rate is what says the compare register is this counter's.
  - **The chain end to end**: the first `setPop` carried `EndOfAllTime`, returned `0x7fffffff` and the
    value actually written to `CNTV_TVAL` was that same `0x7fffffff`; from then on `dec_count` and
    `setpop_count` agree at every sample (2, 4, 8, `0x10`), which is the statement that **every** `setPop`
    in this boot reached the decrementer and nothing else wrote it.

The interrupt is **deliberately still masked** on every write (`CNTV_CTL = ENABLE|IMASK`, measured
`0x00000003`): a deadline can now be armed and counted, and nothing can be delivered. That is 482's
decision to make, and the reason it is not this step's is in "The virtual timer, the mask, and 143"
below.

Everything else in the run is the machine where 480 left it: the OS console is **byte-identical to
480's** (1290 bytes, including the `md0` line), the block set is unchanged (**70 entered / 13 returned /
57 parked**), no trap report, no `xnu_live_undef_*`, no panic, no `pid 1 exited`.

## The missing call, and the `nm -u` that says it is absent

Apple's armv7 kernel gets its timebase in exactly one way: `pexpert/arm/pe_identify_machine.c`'s
`pe_arm_init_timer` calls `ml_init_timebase(args, tbd_funcs, eoi_addr, eoi_value)` at `:666`, and
`osfmk/arm/machine_routines.c:436`'s `ml_init_timebase` copies that table into `rtclock_timebase_func`
under one guard — `(cpu_data_ptr == &BootCpuData) && (rtclock_timebase_func.tbd_fiq_handler == NULL)`.
Everything downstream reads *that*: `cpu_timebase_init` (`osfmk/arm/cpu.c:452`) copies its three pointers
into `cpu_data`, and `ml_get_decrementer` / `ml_set_decrementer` branch through them on every call.

On this machine that call never happens, and the reason is one step stronger than "no branch matches".
The branches are `#if defined(ARM_BOARD_CLASS_S5L8960X)` / `T7000` / `S7002` / `S8000` / `T8002` /
`T8010` / `T8011`, and this configuration defines **none** of them — no `ARM_BOARD_CONFIG_*` is set, so
`pexpert/pexpert/arm/board_config.h` defines no class. The preprocessor therefore removes every
`if (!strcmp(gPESoCDeviceType, ...))` arm of the chain and leaves `return 0;` at `:663` as the function's
*first* statement after two assignments, so the `ml_init_timebase` call is unreachable by construction and
at `-O2` it is not emitted at all. The measurement is `arm-none-eabi-nm -u` on
`out/xnu_obj/pexpert_arm_pe_identify_machine.o`: **no undefined `ml_init_timebase`** — and the object
declares only `pe_identify_machine` and `pe_arm_init_interrupts`, `pe_arm_init_timer` having been inlined
into the latter. That object is what `tools/check_timebase_registration.py` re-reads before every link.

The consequences have been in this project's logs for a while and are what 481 has to move:
`ml_set_decrementer` takes its `#else` path — the assembled body is `ldr r2, [r3, #112]` / `cmp r2, #0` /
`bxne r2`, then `msr CPSR_c, #0xd1` and `str ip, [r8, #104]`, so with the pointer NULL it stores the value
into `cpu_data->cpu_decrementer` in FIQ mode and programs no hardware at all — and 57 threads sit parked
on wakeups whose deadlines are queue entries with nothing behind them. The one number to carry forward is
that this path is what leaves `cpu_decrementer == 0x7FFFFFFF`: `cpu_timebase_init` sets exactly that
(`osfmk/arm/cpu.c:465`) and nothing moves it again, so the read-back below has a *value* to predict.

`stages/stage90/xnu_arm_boot/entry_timebase.h` exists for the four offsets this image dereferences, for
the same reason `entry_saved_state.h` exists for the six frame offsets: the image cannot include Apple's
headers, four transcribed numbers is this project's most repeated defect class, and a wrong offset here
reports a plausible *pointer* that is some other word of the structure. `tools/check_saved_state_offsets.py`
now compares both sets against `out/xnu_assym/$CONFIG/assym.s` in both directions and refuses the link when
they disagree.

## The registration, read out of `arm_init_cpu`'s own order

**Where the call has to go is `osfmk/arm/arm_init.c:443-469`, and the order there is the whole argument:**

    cpu_timebase_init(FALSE);          /* :443 - copies rtclock_timebase_func into cpu_data  */
    serial_init();                     /*       - which is why this file says "before serial_init" */
    PE_init_platform(TRUE, NULL);      /* :456 - the registering call, vm_initialized TRUE, args NULL */
    commpage_update_timebase();        /* :461 */
    fiq_context_init(TRUE);            /* :467 - loads cpu_data's handler and programs CNTV_CTL */
    cpu_data_ptr->rtcPop = EndOfAllTime;
    timer_resync_deadlines();          /* :469 - the first setPop */

so a registration that has to be *seen* by `cpu_timebase_init`'s copy must happen after the copy and
before `fiq_context_init`; the only hook between them is `PE_init_platform`. `arm_init`'s own call to
`PE_init_platform` is the `vm_initialized = FALSE` one and is call 1; the wrapper fires on call **2**,
which is this one, and the log says so: `xnu_live_timebase_seq = 0x00000002`, with
`xnu_live_timebase_args = 0x8050a000` = `BootCpuData`.

The instrument is `__wrap_PE_init_platform` (`stages/stage90/xnu_arm_boot/entry_timebase.c`), whose
predicate is `g_registered == 0 && vm_initialized != 0 && args == (void *)BootCpuData` — the three facts
the hook needs, in the hook. It writes its six records and calls

    ml_init_timebase(args, &stage90_tbd_ops, 0u, 0u);

with `stage90_tbd_ops` = `{stage90_tbd_fiq_handler, stage90_tbd_get_decrementer,
stage90_tbd_set_decrementer}` — `struct tbd_ops` in Apple's order, which
`tools/check_timebase_registration.py` re-parses out of `osfmk/arm/machine_routines.h:242` rather than
trusting. The measured table:

    xnu_live_timebase_registered = 0x00000001     the predicate fired
    xnu_live_timebase_args       = 0x8050a000     BootCpuData, checked against the symbol
    xnu_live_timebase_seq        = 0x00000002     the TRUE call, i.e. arm_init_cpu's
    xnu_live_timebase_get_dec    = 0x80007840
    xnu_live_timebase_set_dec    = 0x80007868
    xnu_live_timebase_fiq        = 0x80007848

The two `0u`s are the interrupt-acknowledge pair, and they are **zero on purpose**: on Apple's hardware
`tbd_fiq_handler` consumes them as `str r11, [r10]` (the AIC's status register and the timer's bit), and
on this machine that interrupt is not routed yet. A guess here would put an unmeasured number in the
image at exactly the place a later step reads it as a measurement; zero is a value a reader can
distinguish. 482 measures them.

**The call order was read out of the linked image, not out of the source.**
`tools/check_timebase_registration.py` locates the triple *by shape* — `__wrap_PE_init_platform`
followed by `cpu_timebase_init` followed by `__wrap_fiq_context_init` — and only then reads the `r0`/`r1`
built before the call and compares `r1` against the ELF's own `BootCpuData`. Measured:
`arm_init: __wrap_PE_init_platform -> cpu_timebase_init -> __wrap_fiq_context_init at 0x80008124 with
r0=1, r1=BootCpuData(0x8050a000)`. The shape-first rule is not stylistic: an earlier version matched
`PE_init_platform` by printed name and picked `arm_init`'s *first* (FALSE) call once both were wrapped.

## The four words, at the first `fiq_context_init`

`__wrap_fiq_context_init` reads the four words one instruction after Apple's copy has written them, and
the run's first call (`xnu_live_timebase_fiq_ctx_calls = 0x00000001`) reports:

    xnu_live_timebase_cpu_get_dec = 0x80007840    CPU_GET_DECREMENTER_FUNC (108)
    xnu_live_timebase_cpu_set_dec = 0x80007868    CPU_SET_DECREMENTER_FUNC (112)
    xnu_live_timebase_cpu_fiq     = 0x80007848    CPU_GET_FIQ_HANDLER      (116)
    xnu_live_timebase_cpu_dec     = 0x7fffffff    CPU_DECREMENTER          (104)

Three pointers identical to the three the registration was handed, and the fourth exactly
`STAGE90_CPU_DECREMENTER_INITIAL` (`osfmk/arm/cpu.c:465`, `cdp->cpu_decrementer = 0x7FFFFFFFUL`) — the
number `entry_timebase.c` passes to the comparison *as that constant*, so the value the run predicts and
the value the check reads out of `cpu_timebase_init`'s own object code are literally the same. It also
says Apple's guard accepted the registration: `cpu_timebase_init`'s copy is a *struct copy* guarded on
`cpu_get_fiq_handler == NULL`, so had `ml_init_timebase` refused (its own guard is
`tbd_fiq_handler == NULL`), all four words would be zero.

The offsets are the ones the assembled kernel loads — `ml_get_decrementer`'s `ldr r2, [r3, #108]`,
`ml_set_decrementer`'s `ldr r2, [r3, #112]`, and `fiq_context_init`'s `ldr r9, [r2, #116]` — which is the
check's fifth claim, and it is the one that matters: a header that agrees with `assym.s` and disagrees with
the object is 468's defect class, so the object is what gets read.

## The virtual timer, the mask, and 143

The architected timer has two countdown registers, `CNTV_TVAL` (virtual) and `CNTP_TVAL` (physical), each
with its own control word, over one free-running counter (`CNTPCT`, which `mach_absolute_time` has read
since long before this step, `__ARM_TIME_TIMEBASE_ONLY__` being 1 in this build). This step writes
`CNTV_TVAL` / `CNTV_CTL` — `mcr/mrc p15, 0, r, c14, c3, 0` and `c14, c3, 1` — for two reasons:

  - **the payload owns `CNTP`.** It arms `CNTP_CTL` for its own dead-man, and that dead-man is one of the
    two recovery nets that keep this device from needing a person. A kernel that reprogrammed `CNTP_TVAL`
    would be writing a compare register a recovery net is using; `CNTV_TVAL` cannot disturb it — separate
    registers, one counter behind them.
  - **it is the register Apple's own `__ARM_TIME__` code writes.** `ml_set_decrementer` is
    `mcr p15, 0, r0, c14, c3, 0` under `__ARM_TIME__` and `fiq_context_init` writes `c14, c3, 1`, so this
    step drives the timer the tree's own configuration would, and 482's job becomes a change of *routing*
    rather than of mechanism. (In this build `__ARM_TIME__` is off — `fiq_context_init`'s assembled body
    is the `#else` path, `msr CPSR_c, #209` and offsets 116/120/124 — so the triples are compared against
    Apple's *source* in `machine_routines_asm.s` and `fiq_context_init`'s disassembly, which is what the
    check does rather than comparing them against this build's own code.)

**And every write is masked.** `STAGE90_CNTV_ARM_MASK = ENABLE|IMASK`, so the countdown runs and
`CNTV_CTL.ISTATUS` rises when the deadline passes with no interrupt asserted. That is the one state in
which a deadline can be *measured* without anything being able to happen, and the reason is a measurement
this project already has: **experiment 143 measured that MSM8974 delivers no FIQ to non-secure PL1**, with
the Group 0 configuration accepted at both ends and the timer measurably reaching `ISTATUS`. This image's
vector page puts Apple's decrementer handler in slot 7 — the FIQ slot — and the IRQ slot holds `fleh_irq`,
which calls the platform's `interrupt_handler` through `cpu_data` and has none installed, so a timer
interrupt delivered as a group-1 IRQ today would branch to a NULL handler. **482's job is that routing
decision**, and it cannot be taken until the interrupt's arrival is something observable rather than
something that stops the boot. This run's negative control held: **no `xnu_live_fiq_handler_entered`
record exists**, and the handler is in the table precisely so that "expected never" is measured rather
than assumed.

## The hardware counted, and the chain behind it

The first call of `stage90_tbd_set_decrementer` — which is `setPop`'s own tail — does the one thing this
step can do that no earlier step could: it makes the timer count and watches it.

    stage90_cntv_ctl_write(ENABLE|IMASK);      xnu_live_dec_ctl   = 0x00000003
    stage90_cntv_tval_write(0x00100000);       xnu_live_cntv_tval_a = 0x000ffffa
                                               xnu_live_cntvct_a    = 0x069f343a
    <bounded spin, STAGE90_CNTV_SPIN = 20000 empty asm statements>
                                               xnu_live_cntv_tval_b = 0x000ffccd
                                               xnu_live_cntvct_b    = 0x069f3767

`0x00100000 - 0x000ffffa` is 6 counts between the write and the read, and `_b - _a` is **0x32d = 813**:
`CNTV_TVAL` went down 813 while `CNTVCT` went up 813. That agreement is the reading — the count 813 is a
result of a bounded spin, not a target, and the claim it supports is that the register this step programs
is the compare register for the counter `mach_absolute_time` reads, at 1:1.

Then the chain, all four numbers agreeing:

    xnu_live_dec_written         = 0x7fffffff
    xnu_live_dec_readback        = 0x7ffffff9
    xnu_live_setpop_deadline_lo  = 0xffffffff
    xnu_live_setpop_deadline_hi  = 0xffffffff
    xnu_live_setpop_returned     = 0x7fffffff

`setPop` (`osfmk/arm/rtclock.c:343`) calls `ml_set_decrementer` unconditionally and returns
`deadline_to_decrementer(time, now)`, which saturates at `DECREMENTER_MAX` = `0x7FFFFFFF` for a deadline
at infinity. The deadline it was handed was `2^64 − 1` — `EndOfAllTime` — so this is the "no deadline yet"
call: `rtclock_timer.deadline` seeded to `EndOfAllTime` (`osfmk/arm/cpu.c:208`) resynced with `rtcPop`
forced to `EndOfAllTime` (`arm_init.c:468`). The value written to the register is the value `setPop`
returned, and the read-back is 6 counts lower — the same 6 counts the sample above measures as one
instruction's worth of work, which is the rate of the register showing up in a second place.

Past the first call the two counters move together, and that is a second reading:

    dec_count / setpop_count:  0x2 / 0x2,  0x4 / 0x4,  0x8 / 0x8,  0x10 / 0x10

**Every `setPop` reached the decrementer and nothing else wrote it.** The records are written at powers of
two, so the last pair is a floor: at least 16 deadlines were armed and 16 countdowns programmed before the
log ended. The 57 parked threads are not a contradiction — a deadline that is armed and masked still
cannot wake anyone, which is exactly the state 482 has to leave.

## The console, the layout, and the keys

The OS's own console is **byte-identical to 480's**: 1290 bytes, 24 carriage returns, 25 line feeds,
`diff` of the captured regions empty, `Added memory device md0/rmd0 (02000000/0D000000) at
0000000080501000 for 0000000000002000` unchanged. The text did not move because nothing it is computed
from moved: the copied image still ends at `0x533a74` = 5454452 bytes and `__bss_start` is still
`0x80533a80`.

**Two console-capture numbers did move, and they moved for a reason worth stating.** `xnu_live_ostext_tank`
went **0x262 (610) → 0**, and `xnu_live_ostext_at` **0x49cd0 → 0x48367**; `_chars` (0x3ee), `_heals` (1)
and `_block` (0x20000) are unchanged. Both are *position* values, not text values: the console capture
takes one 128 KB block at the ram console's cursor when its first character arrives, publishing
`size = base + ENTRY_OS_BLOCK` **before** writing its own records (`entry_stubs.c:2056-2070`) — which is
why `ostext_at`/`ostext_block` print *above* the captured text in the log and `ostext_tank` below it — and
`_tank` is `g_os_tank_n`, the number of console characters that had to be held in this image's own `.bss`
before the live channel was installed (`entry_stubs.c:2100-2110`).

The mechanism is measured, not inferred: in 474, 475, 479 and 480 the first live record after the install
header is `xnu_live_unser_caller` — IOKit's un-serialiser, i.e. long after the console had printed 610
characters — while in 481 it is `xnu_live_timebase_registered`, written by the new `PE_init_platform`
wrapper at `arm_init`. So 481's live channel came up earlier in the boot than 480's, and the consequence is
visible in the log's shape: the early-boot records that sit *below* 480's console text (`unser_caller`,
`walk_seq`, `dtplane`, `block_enter`) sit *above* it in 481's. The captured text is identical because the
tank contents are flushed into the block either way.

The layout numbers, all from the linked image after the run:

    .text            5232512   (480: 5231040)   .data base 0x80500000 + 0x326b0
    .bss             0x80533a80 .. 0x8058b5c8  (0x57b48, 480: 0x57b08)
    image bytes      5454452   (unchanged)      headroom 1526328 (480: 1526392; the 64 is the .bss)
    boot_args        0x8058d000                 topOfKernelData 0x80700000
    g_stage90_ramdisk 0x80501000 + 0x2000       undefined 26

and the key census, which needs one careful paragraph because the numbers depend on where the boundary is
drawn. **25 lines in this log begin with `MI4IOS6_STAGE90_XNU xnu_entry`** — the same 25 in 474, 479 and
480 — and they are not all report keys: `xnu_entry_va=0x80008000` (line 3515) is the payload's pre-jump
line, `xnu_entry: image, bss and boot_args written back…` (3906) is a sentence that happens to start with
the prefix, and `xnu_entry_entering_at` (3929) is written after the report. The epilogue block itself is
**22 keys**, lines 3896-3905 and 3908-3919. 480's document publishes "24 boot keys"; the same `grep`
on 480's and 481's logs gives 25, and 24 is what neither the prefixed lines nor the epilogue block
count — the number was published without its boundary, which is the same class of defect that document
records against 479 for the same prefix (defect 212). Trap-report keys (` xnu_entry_*` with a leading
space) are **0**, as in 479 and 480 (478: 473).

The rest of the channel is the machine's state, unchanged and read as such: `xnu_live_block_seq = 0x46`
(70 blocks entered) with `_returns = 0x0d` (13 returned), so 57 threads are still parked on a wakeup that
needs a clock; 1101 `xnu_live` records (480: 1072, and the 29 new ones are exactly the eleven
`xnu_live_timebase_*`, the twelve countdown/setPop records above, and the three power-of-two pairs that
continue them); 0 `stub_hit=`, 0 `xnu_live_undef_*`, 0 `xnu_entry_panic_*`, no `pid 1 exited`,
`No errors detected` at the last line.

## The check, and the three defects this step's own work caught

`tools/check_timebase_registration.py` is new and asserts five claims about the linked image plus two
sub-checks: (1) `rtclock_timebase_func`'s window is unoccupied — re-read from `nm -u` on the pexpert
object, no undefined `ml_init_timebase`; (2) `ml_init_timebase`'s call site and guard in
`machine_routines.c:436-447`; (3) `pe_arm_init_timer` and the `ARM_BOARD_CLASS_*` chain that makes the call
unreachable here; (4) the CNTV/CNTP triples, compared against the `c14` instructions in
`machine_routines_asm.s` and `fiq_context_init`'s disassembly rather than against this build's own
`__ARM_TIME__`-off code; (5) the four `assym.s` offsets against the offsets the *assembled* kernel loads.
It identifies the registration by **shape** in the linked image (see above), reads `r0`/`r1` from the
`movw`/`movt` pair before the call, and its `--selftest` refuses **all 12 mutations**, three of them aimed
at the linked image's own symbol table.

`tools/check_saved_state_offsets.py` grew the four `cpu_data` offsets into the same comparison as the six
frame offsets and the two `thread`/`vm_map` ones, in both directions, plus the property that the four are
consecutive words; **all 17 mutations are refused**, and its `ok:` line is now what a reader should quote
for either set:

    DECREMENTER = 104, GET_DECREMENTER_FUNC = 108, SET_DECREMENTER_FUNC = 112,
    GET_FIQ_HANDLER = 116, four consecutive words

Three defects are worth recording, and two of them were caught by this step's own machinery:

  - **The premise was imprecise, and `nm -u` is what corrected it** (a documentation defect with a
    measurement). `entry_timebase.h` and `entry_timebase.c` first said the class list was
    `S5L8960X/T8002/T8010/T8011` and the reason for the missing call was "MSM8974 matches none of them".
    Measured: `T7000` and `S8000` are in the list too, and the real mechanism is stronger — **no
    `ARM_BOARD_CONFIG_*` is set, so no `ARM_BOARD_CLASS_*` is defined**, the whole chain is preprocessed
    away, and the call is absent from *every* configuration of this build rather than merely not matching
    this SoC. The corrected claim is now claim 3 of the check, where a reader can re-run it.
  - **Pass 1 ran without the `--wrap` flags, and the generator's stand-ins beat the linker** (a build
    defect that would have made the whole step a no-op while still producing a log). The first link pass
    (`arm-none-eabi-ld` → `xnu_arm_entry_undef.txt` → generated stubs) must carry the **same** `--wrap`
    flags as the final link; without them `__real_PE_init_platform` and `__real_fiq_context_init` were
    reported undefined and the generator emitted *ordinary definitions*, which beat the linker's `__real_`
    rewrite — so `__wrap_PE_init_platform` called `entry_stub_hit` and the registration never happened.
    The fix is `PASS1_LDFLAGS` plus a new build step that reads pass 1's undefined set and stops the build
    if **any** `--wrap`ped name, or its `__real_` stand-in, is in it. The rule for the future: only the
    wraps whose `__wrap_` lives in a pre-pass-1 object may go on pass 1.
  - **The offsets check compared the wrong configuration by default** (a check that answers about another
    file). `XNU_KERNEL_CONFIG` defaults to `RELEASE` when unset, and `CPU_DECREMENTER` is **104 in
    `STAGE90_XNU` and 88 in `RELEASE`** — `struct cpu_data` is a different size in the two — so running
    the check by hand printed a four-number FAIL about the RELEASE layout with the file's name nowhere in
    the message. The build never saw it (`XNU_KERNEL_CONFIG` is exported by the four-step build), which is
    exactly why it survived: the check is run by hand when a reader wants to know what it says. Fixed by
    defaulting `--config` to the configuration this image is built with and by naming the file in the FAIL
    line — a comparison against the wrong configuration is a fact about the path, and it cannot be seen in
    the numbers.

Two smaller ones, for completeness: `STAGE90_CPU_DECREMENTER_INITIAL` was defined twice in
`entry_timebase.h` (the second copy removed and given its own comment), and a comment block was placed
inside the `LINK_OBJS=( … )` array literal in `build_entry.sh`, which is a shell syntax error — the array
is now closed before the timebase object is appended, which is also where the traced-only condition lives.

## What 482 has to do

**The registration is in and the decrementer is under the kernel's control; what is missing is the
delivery, and that is a routing decision with exactly one unmeasured fact in it.** The step's own
placeholder says where: `int_address`/`int_value` are passed as `0u, 0u` to `ml_init_timebase`, and on
Apple those two words are the FIQ's acknowledge pair — `tbd_fiq_handler` consumes them as
`str r11, [r10]`. So 482 has to:

  - **measure which physical interrupt line the virtual timer appears on for MSM8974** and where its
    acknowledge lives — the GPT at `0xf9020000` (19.2 MHz, IRQ 19) or the architected timer's own PPI —
    and then hand real values to `ml_init_timebase`;
  - **decide who owns the slot**: Apple's decrementer handler in the FIQ slot (which 143 says receives
    nothing on this SoC) or `fleh_irq`, which today would branch to a `NULL` `interrupt_handler` through
    `cpu_data`. `IOCPUInterruptController` is the piece that makes the second one real;
  - **only then lift the mask** — `STAGE90_CNTV_ARM_MASK` is what makes this step's measurement safe, and
    it should not be relaxed before a handler exists that can clear the source.

**The owed list, unchanged except that 480's first item is now done:** which of `arm_fast_fault`/`vm_fault`
serviced 480's write fault (`--wrap=arm_fast_fault`); why the `VM_FLAGS_FIXED` stack allocation at
`0x26E00000` is refused (`--wrap=mach_vm_allocate_kernel`); 474's `thread->map = 0` *moment*; 448's `_bad`
slots as a named pair of keys; the pthread table's other ~34 slots; `osfmk/kperf/kperfbsd.c`; the untraced
build's `entry_stubs.c` compile errors; `thread_bootstrap_return`.

**Measured:** gate passed, exit 0, device back on Android on its own, log 461190 bytes / 5060 lines /
1101 `xnu_live` records, `No errors detected` on the last line, no `pid 1 exited`, no `xnu_live_undef_*`,
no `xnu_entry_panic_*` and no trap-report key, with 25 prefixed report lines (the same 25 as 480) of which
22 are the epilogue block's keys; the SoC watchdog the only reset source (bite `0x000dffac` ticks at
`0x00007ffd` Hz = 28.0 s, bark `0x000c7fb5` = 25.0 s, nothing disarmed it); block census unchanged at 70
entered / 13 returned / 57 parked; console byte-identical to 480's (1290 bytes, 24 CR, 25 LF);
`.text` 5232512, `.bss` 0x80533a80..0x8058b5c8 (0x57b48), image 5454452, headroom 1526328, boot_args
0x8058d000, undefined 26, **49 `--wrap`ped symbols** (44 reached by a branch, 1 same-object-only
`_ZN9IOService12matchPassiveEP12OSDictionaryj`, 1 never called here `sleep`, 3 by address only `vcputc
getpid mmap` — the same classes 480 published, with three more wraps and the same three by-address), and
**the entry image rebuilt byte-identical after the run** (`cmp` clean, `xnu_arm_entry.bin` sha256
`95e9114775cd1e607ca038ec6c352b7e49547bf7c15e3b6e7f76a9a90c65353d`), so the run above is the run the
committed tree produces. Payload `stage90-qcdt.img` 8474624 sha256
`0a3e17476890696273b452825700950baacefde180cc90358300a2aaf33c06e5`.
