# Experiment 496 — a driver owns a line and writes its device

Date: 2026-09-21
Hardware: Xiaomi Mi 4 (cancro), non-persistent `fastboot boot`, `/proc/last_kmsg` captured twice
Artifacts: `stages/stage90/xnu_platform/MSM8974GIC.cpp`, `stages/stage90/xnu_arm_boot/entry_irq.c`,
`stages/stage90/xnu_arm_boot/entry_gic.h`, `tools/check_driver_catalogue.py`,
`tools/check_irq_routing.py`, `tools/check_gic_routing.py`

**Result: a driver in this image owns an interrupt line and writes its device.** 495 made the OS call a
driver back on a timeout the *OS* owns, through objects the OS created. This step is the driver's own:
`MSM8974GIC::start` writes `GICD_ISENABLER0` through the mapping 494 made, hands the entry image a
function of its own file with `entry_irq_register_client`, asks for the line by writing `GICD_SGIR` with
the target-self encoding, and the kernel calls *that function* from the exception path - three times,
the second and third asked for from inside the handler:

    MSM8974GIC::start                     the enable, the registration, the request
    _drive_compiled  = 1                  the switch's own value, published in the same run
    _own             = 0                  SGI 0
    _en_before       = 0x00107fff         the enable register before the driver's write
    _en_after        = 0x00107fff         ... and after it: bit 0 was already set
    _pend_before     = 0x20400000         bits 22 and 29 pending, this line not
    _mapvaddr        = 0xc2196000         the mapping 494 made - the address the ISR writes through
    _cli_rc          = 1                  the registry's answer, not a return code of the driver's
    _isr             = 0x8027facc         the handler the driver handed over, in the image
    _isr_self        = 0xc0648b00         and the refCon the kernel hands back to it
    _isr_ask         = 3                  how many requests from the ISR's own body

    the kernel's calls                  call 1       call 2       call 3
    _isr_calls                         = 1          2            3
    _isr_last                          = 0          0            0
    _isr_pend_after_ack                = 0x20400000 0x20400000   0x20400000
    _isr_pend_final                    = 0x20400000 0x20400000   0x20400000
    _isr_pends                         = 1          2            -            requested from inside
    _isr_sgir                          = 0x02000000 0x02000000   -            TARGET_SELF | SGI 0
    _isr_guard                         = -          -            1            the line was quiet
    _isr_unregs                        = -          -            1
    _isr_unreg_rc                      = -          -            1
    _isr_done                          = -          -            1

    after the request, in process context
    _pends           = 1                  the driver asked for its own line
    _sgir            = 0x02000000         ... by writing GICD_SGIR itself

    the registry, entry side
    _cli_seq = 1, _cli_slot = 0, _cli_intid = 0, _cli_handler = 0x8027facc,
    _cli_refcon = 0xc0648b00, _cli_capacity = 4,
    _cli_calls_total / _cli_calls0 = 1 -> 2 -> 3, _cli_unregistered = 1, _cli_unreg_intid = 0

Both runs agree on **all 60 `gicdrv` keys** except the seven that are heap addresses
(`_prov`, `_devmem`, `_map`, `_mapvaddr`, `_isr_refcon`, `_isr_self`, `_cli_refcon`), which differ run to
run because this image allocates its objects from the kernel's pool and the addresses depend on the
order the allocator happens to hand them out - the same split every IOKit step in this line of work has
had since 492 first made the OS create objects for a driver. **Two readings of the same two registers by
two different callers agree to the bit in the same run**: the driver's `_en_before`/`_en_after` are both
`0x00107fff`, which is 483's `xnu_live_irq_isenabler0` (the entry image's own read of the enable
register, taken before any of this), and the driver's `_pend_before` is 483's
`xnu_live_irq_pend_before`, `0x20400000`. So the idempotence of the driver's enable write is not an
inference from one caller: the line was already enabled when the *payload* armed it, and the driver's
write changed nothing.

**The stop path this step exists to keep the boot out of was not taken, and the run says so by
absence.** `xnu_live_irq_other_count` is **absent** from both logs, there is no `stub_hit=` line in
either, and no panic: the driver's line went through the client scan, not through the dispatcher's
unexpected-line stop. That is the property the whole design is built around, because an intid that
reaches `entry_irq_handler` with **no** client is a stop - so a driver that owns a line and then gives it
back has to know the line is quiet first, which is what `_isr_guard` is.

## The three orders, and each is the whole safety property of its own mechanism

Nothing in this step is a new *kind* of thing for the machine; what is new is that three orderings, each
of which is a claim about concurrency, are now written down and checked rather than implied.

  * **The registry's fields are written in one order, and that order is the concurrency contract.** The
    dispatcher runs with interrupts masked, but `entry_irq_register_client` is called from *process*
    context with them open (`entry_irq.c:241-243`), so a registration can be preempted by the very IRQ
    path that reads the table. A slot is taken only when its handler is zero; the intid and the `refCon`
    are stored first and **the handler last**, so a scan racing the write either does not match the intid
    (the slot is not yet that line's) or finds a zero handler and treats the line as unregistered -
    which is exactly what it was a moment earlier. The reverse order publishes a handler for an intid the
    scan has not been told about, which is not a variation on this state but a different, broken one.
  * **The unregister is the mirror, and it clears the handler first** (`:274-276`): the slot stops being
    live for the dispatcher before anything else about it changes, so a delivery racing this store is a
    line with no client - the case the record names - and not a call through half-cleared state.
  * **The acknowledgement is written before the client is called** (`:368`), for the timer line's reason:
    a line re-asserted during the client's work is already back in the distributor's hands. It also means
    the client cannot forget to acknowledge; its job is its *device*, which is the one thing this image
    cannot do for it.

## The first delivery happened inside the request

The live channel is appended in one order and the log preserves it, so the sequence of the twenty
keys this step adds is itself a reading. In both runs it is:

    the five zeroes (pends, isr_calls, isr_pends, isr_unregs, isr_done)
    _cli_rc = 1
    _isr_calls = 1, _isr_last = 0, _isr_pends = 1
    _isr_calls = 2, _isr_last = 0, _isr_pends = 2
    _isr_calls = 3, _isr_last = 0, _isr_guard = 1, _isr_unregs = 1, _isr_unreg_rc = 1, _isr_done = 1
    _pends = 1, _sgir = 0x02000000

`start`'s own request keys come **last**, after the whole handler sequence - and they are written
immediately after the store to `GICD_SGIR` (`MSM8974GIC.cpp:535-540`). So the machine took the SGI
*inside `start`*: the store pended the line, the `dsb sy` after it did not stop the delivery, and the
dispatcher, the registry lookup, call 1, its own re-request, call 2, call 3 and the unregister all
completed before `start` reached its next statement. The three calls are sequential rather than nested -
an SGI pended from inside the handler cannot be delivered until the handler's epilogue restores `CPSR` -
and the log says that too, in the order of its own keys.

This is the same behaviour 495 measured from the other side: a request made from process context is
delivered before the requester finishes. It is why the zeroes are published *before* the request rather
than after it (`:510-514`), which is what makes "asked, never called" a value in the log instead of keys
that are simply absent.

**`_isr_last` alone cannot tell the two machines apart, and the step's own pair is what does.** SGI 0 is
the line, so the zero published before the first call and the intid of all three calls are the same
number: a run whose `_isr_last` reads 0 may be a driver the kernel never called, or a driver called three
times. `_isr_calls` is the key that separates them, and the check requires both - a reading that is right
about the machine and cannot be *read* on its own is not a reading.

## Why the line is SGI 0 and the registry is this image's own

`GICD_SGIR` is the one interrupt in this machine a driver may raise itself: it belongs to no peripheral,
its pending state is cleared by the acknowledgement, and the payload has already measured the delivery
end to end from its own side - `gic_sgi_selftest` (`gic.c:345`, called from `xnu_kernel.c:199`), whose
`gic_sgi_sgi0_count = 1`, `gic_sgi_last_iar = 0`, `gic_sgi_spurious_count = 0` have been in every run
since the payload's first SGI test. So this is the second reader of a measurement taken on the other
side of the handoff, not a first guess at how the part behaves. Raising the timer's line instead would be
raising the OS's own clock source, and every number in the new block has a second definition in the
payload (`gic.c`'s register map, `entry_gic.h`'s transcription of it) which
`tools/check_driver_catalogue.py` compares row by row.

The route *not* taken is IOKit's own, and the reason is in the tree rather than in a preference:

  * `IOPlatformExpert::registerInterruptController` (`IOCPU.cpp:783`) plus
    `IOCPUInterruptController::registerInterrupt` (`:840`) is the framework-native registration, and its
    last statement block is `if (enabledCPUs != numCPUs) { assert_wait(this, THREAD_UNINT);
    thread_block(THREAD_CONTINUE_NULL); }` (`:875-880`). `numCPUs` is set by `:743` from
    `IOCPUInterruptController::start`'s own argument, and the only writer of `enabledCPUs` is
    `enableCPUInterrupt` (`:821-838`) - which **has no caller anywhere in this tree**: a whole-source
    grep finds exactly two occurrences, the declaration at `IOCPU.h:133` and the definition. On a machine
    where no CPU kext has run, that registration fills its vector and then blocks in `thread_block` and
    never returns. It is a route to a hang, and a hang here is indistinguishable from a stop.
  * `IOService::registerInterrupt` (`IOService.cpp:6337-6351`) is not an alternative to it: it looks the
    controller up with `lookupInterrupt` and calls the *same* `registerInterrupt`, so it reaches the same
    block - or, with no controller registered, returns before doing anything at all.

So the entry image keeps its own registry inside the payload's dispatcher, which is the one place that
already knows which line it is on, and `ml_install_interrupt_handler` - the boundary 483 installed the
payload's handler across - stays the only framework call involved.

## The build

Canonical four steps, all green. `.text` 5264672 -> **5266528** (+1856), image **5487228** (unchanged),
`.bss` `0x8053ba80 .. 0x80594388` **362760** (495: 362664, +96), 27 undefined, 59 wraps (53 reached by a
branch, 1 same-object-only, 1 never-called, 4 by-address). The two generators of build step 2 were re-run
for this record and name their own counts: `gen_assym.sh` **266 defines** from 571 lines of
`genassym.s`, `assemble_arm_layer.sh` **17 ok / 0 failed, 4 files translated, 26 symbols
de-underscored**. Build step 1 was not re-measured: this step's diff touches nothing outside
`stages/stage90/` and `tools/`, so the XNU-side pool is 495's - which is a claim about the diff and not a
remembered number, and `stages/stage90/xnu_arm_boot/assym.s` md5 `a4b403a6…` is unchanged by the
re-run.

Entry symbols: `entry_irq_handler` 0x8000a650, `entry_irq_register_client` 0x8000a9b8,
`entry_irq_unregister_client` 0x8000ab10, `msm8974_gic_isr` 0x8027facc (`_ZL15msm8974_gic_isrPvj` -
the address the driver registered and the address `_isr` publishes are the same number in the same run).
Payload `.bin` 5982532 / `.img` 5986304 / `.qcdt.img` 8507392 (unchanged in size from 495), sha256
`77fd1327…` / `53cd8c7e…` / `e8da7170…`.

The step's own `#define`s are `MSM8974_GICD_ISENABLER0_OFF 0x100`, `ISPENDR0_OFF 0x200`,
`ICPENDR0_OFF 0x280`, `SGIR_OFF 0xf00`, `SGIR_SELF (2u << 24)`, `OWN_INTID 0u`, `ASK_PENDS 3u`, and the
switch `MSM8974_GIC_DRIVE_SGI 1`; on the entry side `STAGE90_GICD_SGIR`, `STAGE90_GICD_SGIR_TARGET_SELF`,
`STAGE90_GIC_SGI0_ID`, `STAGE90_IRQ_CLIENTS 4u`, `STAGE90_IRQ_CLIENT_CAP 64u`. Both switches are
`#define`s in the source rather than flags on a command line, for the reason 483 established: the check
reads the *value* the linker used, so the build that ran and the build the check read cannot be two
different machines. `_drive_compiled = 1` is that value published into the same run, and
`xnu_live_irq_enable_line_compiled` is the other switch's.

## The check: eight claims and fifty mutations, seven and forty-eight, fifteen and eighty-one

Three checks grew, and all three run inside `build_entry.sh` in both modes:

  * `tools/check_irq_routing.py` **7 claims / 36 mutations -> 8 / 50**, all refused. Claim 4
    (`claim_handler`) now reads the *position* of the client scan - after the timer case, before the
    spurious case - the per-slot cap, the stop that follows it, the order of the acknowledgement against
    the call, `GICC_EOIR` written exactly **three** times in the body and `entry_epilogue` called exactly
    **twice** (the unexpected line and the storm). The new claim 7 (`claim_client_registry`) reads the
    registry's own contract: the handler stored last, the three unregister stores in the mirror order, a
    zero handler refused, a second registration for a live line refused, the capacity published from the
    header, the slot recorded on the successful path. Fourteen new mutations, one per clause -
    `client_handler_stored_first`, `unregister_clears_the_intid_first`, `unregister_keeps_the_handler`,
    `client_scan_after_the_spurious_case`, `client_eoir_after_the_call`, `client_cap_after_the_call`,
    `client_falls_into_the_stop`, `client_cap_removed_from_the_header`, `client_cap_is_zero`,
    `client_accepts_a_zero_handler`, `client_refuses_nothing`, `client_records_no_slot`,
    `client_guard_removed`, `unregister_is_silent_about_a_missing_line`.
  * `tools/check_gic_routing.py` **7 claims / 43 mutations -> 7 / 48**, all refused. Three new rows in
    the offset map (`GICD_ISENABLER0`, `ISPENDR0`, `SGIR`) and five new mutations, all about the one
    value the payload and the driver share: `sgir_offset_moved`, `sgi_intid_moved`,
    `sgir_target_filter_moved`, `sgir_define_dropped`, `self_test_write_removed`.
  * `tools/check_driver_catalogue.py` **14 claims / 57 mutations -> 15 / 81**, all refused. Claim 15
    (`claim_driver_line`) reads the driver's own half: the six register offsets and the intid agree with
    the header's spelling of the same values, the request is guarded on the registration having
    succeeded, the registration is for *this* line, the function registered is defined in this file, both
    the handler's address and the registration's result are published, the handler reads its own pending
    bit and asks for the line again and withdraws only after testing that bit, and one more: **every
    device store in the file lies inside the switch's `#if` blocks** - so `MSM8974_GIC_DRIVE_SGI 0` is a
    machine that cannot write this device through any name in the file. Twenty-four new mutations.

**Two of the new mutations were first written against the wrong thing and are the check's own defects.**
`the_request_comes_before_the_registration` carried a trailing blank line where the file has `}` followed
immediately by `#endif`, and `the_self_test_filter_disagrees_with_the_header` used five spaces where the
file has six - both needles matched nothing, and a mutation whose needle matches nothing is a mutation
that "passes" while testing nothing at all. The third is a repair of a claim that was blind to the
handler it was about: claim 15 computed the base address from `mapping["vaddr"]` (`mapvaddr`) while the
handler writes through the saved `g_gic_mapvaddr`, so it reported "the handler never reads its own
pending bit" against correct code; the claim now discovers the second base from the body and **requires
that it be assigned from a published variable**, which is itself a claim with its own mutation
(`the_handler_writes_through_an_unpublished_address`).

## What the build cannot find: seven variables the source declares and the image does not have

Measuring the `.bss` delta for this step - the entry side's nine new statics at
`0x80543f68..0x80543fc4` and the driver side's eight at `0x80582ea8..0x80582ec8`, 113 named bytes between
them against a section that grew 96 - `nm` shows something the source does not: **seven file-scope
variables of these two files are in neither file's symbol table.** They are not renamed and they are not
in another section; they are not in the image:

    entry_irq.c      g_irq_first_iar     written at :307, published from the local `iar` at :313
                     g_irq_last_iar      written at :405, published from the local `iar` at :407
                     g_irq_icfgr_word    written at :530, the key reads the local `word` at :462
                     g_irq_icfgr_shift   written at :531, the key reads the local `shift` at :463
                     g_irq_cli_last      written at :371, published from the parameter at :373   (496)
    MSM8974GIC.cpp   g_gic_isr_last      written at :246, published from the parameter at :258   (496)
                     g_gic_isr_done      written at :285, published at :286 and constant-folded  (496)

Two mechanisms, one observable. Six are **written and never read**, so the stores are dead, the variable
is removed, and the published value comes from the parameter or the local that holds the same number a
line earlier. The seventh is read but provably constant (`g_gic_isr_done = 1`, then published), so the
load is folded into an immediate. **Every key in both runs is still the right value** - that is why no
reading here is wrong, and why the runs do not need re-taking - but the artifact has no variable behind
the key, and a reader of the source would believe the image keeps a "last IAR" that it does not keep. It
is the third form of the storage hazard 436 closed: the first was a stand-in sized from `nm -S` and never
initialized, the second a real `.bss` global nothing writes, and this is **a global the source declares,
whose stores are dead, and which the compiler therefore never allocated at all**. Three of the seven
are new in 496 and four predate it, so the fix belongs to its own step: replace each `entry_live_write(
"key", local )` with the variable it is supposed to be the record of, and add the claim this sweep was
missing - **every non-`const` file-scope `static` a file declares is in the image's symbol table** -
which needs the linked image and is therefore the one thing a build-only check cannot do on its own.
The 113 against 96 is left unreconciled on purpose: a sum of symbol sizes and a section's span are
readings of two different things, and an arithmetic that is made to agree is exactly the kind of number
that has been wrong in this project before.

## The console, the fault counters and 495's readings

Nothing that the boot decided moved. The OS console block is byte-identical to 495's - 1288 characters
from the byte after the `[os-console-459]` marker's newline through the final `d` of
`load_init_program: attempting to load /sbin/launchd`, sha256
`a0593af02867eac3ddf107a6523292ea9e5016861188faa0a2c796d3ce54cc8d` - and it is the same span in all
eight logs of 493-496. The software dead-man's counters are 495's (`sleh_seen = 0x20`, `_armed = 0x1c`,
`_redirected = 0x1a`, `_storm = 9`), `irq_late_count = 0`, and the timer's own readings reproduce to the
tick: `timerdrv_fires` 0 -> 1 -> 2 -> 3 with `_fire_lat_ns` 101.102708 / 101.040677 / 101.042656 ms,
`_gap_ns` 101.047083 / 101.050104 ms, and `_due_lo - _arm_lo = 0x0732c1ce - 0x071575aa = 1 920 036` ticks
= 100.002 ms for the 100 ms asked for.

**The fingerprint is convention-sensitive by one character at each end, and that is worth writing
down.** Taking the same span *including* the marker's newline gives 1289 characters and `609abc39…`;
taking it from the marker's `[` gives 1305 and `b31113ad…`. Both have been reported in this project as
"the record is wrong", and both times the record was right: **when a hash comparison disagrees with a
recorded hash, the endpoints are a suspect before the data is**, in the smallest possible unit of this
project's oldest defect - a value with two definitions (defect 298, whose third convention is this
session's).

## What is owed

  * **The seven variables above**, and the claim that would have caught them. Until then, a reader of
    these two files cannot tell a record from a copy of a parameter.
  * **A line owned by a peripheral.** SGI 0 is the driver's own device - a self-inflicted interrupt that
    proves the registry, the enable and the write all work through the driver's mapping. What has *not*
    happened is a real device asserting a line and the OS routing it to a driver that cleared it; the
    distributor's enable for a peripheral interrupt, the `ISR`'s return path into `rtclock_intr`'s
    world, and the `AckC` question all come with that step.
  * **The unregister's guard is the client's, and a stop and a hang look the same in the log.** If a line
    were asserted after the client gave it back, the dispatcher's scan would find no client and stop the
    run; the log would end with a stop either way, and distinguishing "the driver withdrew too early"
    from "the machine hung" is not something this image can do from inside. That is why `_isr_guard` and
    `_cli_unregistered` are separate keys.
  * **A timeout that outlives its asker**, `/timer`'s second definition (nothing on the OS side is
    compared with the payload's own read of the GPT), **the other device nodes** (`/arm-io`, `/cpus`, …),
    **the two services the catalogue answers with nothing** (their class), `MSM8974RootResource`'s
    `state0 = 0`, a name/class reader wider than eight characters, the release as a reading, `vm_fault`
    as a caller-side record, 488's deferred flag-list derivation, 490's distinct-`(pc, lr)` frames band,
    and `xnu_live_dec_same`.

## Safety

Two runs, each through `preflight_boot_check.sh --allow-xnu-entry` then
`run_and_capture.sh --allow-xnu-entry`, non-persistent `fastboot boot` only, never a flash. Both exit 0,
both captured (`/tmp/stage90-496-run1.log` 555444 bytes, `/tmp/stage90-496-run2.log` 554959 bytes), both
ending `No errors detected`, both returning to Android on their own.

The step's new risk is a driver writing a device register and owning a line, and it is bounded three
ways: the line is an SGI, which has no peripheral behind it and whose pending state the acknowledgement
clears; every device store in the file is inside the switch's `#if` blocks, and the switch's value is
published in the run; and the driver's own guard withdraws the registration only when its line is quiet,
so no delivery can reach a scan with no client. The watchdog keys are 495's (`hw_watchdog_enabled = 1`,
`_timeout_s = 0x19`, `_bite_ticks_written = _bite_after = 0x000dffac`, `_readback_ok = 1`,
`_counter_running = 1`, `_countdown_plausible = 1`, `_bite_truncated = 0`), so the net was armed, was
running, and did not bite - and no persistent write of any kind was made.
