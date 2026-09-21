# Experiment 497 — a record the image does not have

Date: 2026-09-21
Hardware: Xiaomi Mi 4 (cancro), non-persistent `fastboot boot`, `/proc/last_kmsg` captured once
Artifacts: `stages/stage90/xnu_arm_boot/entry_irq.c`,
`stages/stage90/xnu_platform/MSM8974GIC.cpp`, `tools/check_irq_routing.py`

**Result: the step's artifact is byte-identical to 496's, and that is the measurement.** `.text`
5266528, image 5487228, `.bss` `0x8053ba80..0x80594388` 362760, 27 undefined, 59 wraps - 496's six
numbers - and the entry image hashes to
`0934bbe9d268fd09e2920cc1484bcce4f005bd6daba9075620b6f807e82486d0` both when built from 497's sources
and when 496's sources are put back (`git stash push` of the two files, rebuild, `cmp`); the three
payload images hash to `77fd1327…` / `53cd8c7e…` / `e8da7170…`, **which are the three hashes 496
recorded**. So every store this step removes was already absent from the artifact, and 496's two runs
are runs of exactly this binary.

That is what 496 only half-saw. Its own report ended with a list, and the first item of that list was a
plan: *"replace each `entry_live_write("key", local)` with the variable it is supposed to be the record
of"*. 497 did that first, and the result is the actual finding:

**The plan was applied to the six records that had a value to read, and two of the six were removed by
the compiler anyway.** `g_irq_cli_last` was given a reader on the next line
(`IRQ_LIVE("xnu_live_irq_cli_last", g_irq_cli_last)`) and disappeared; so did `g_irq_icfgr_word`. The
other four survived - `g_irq_first_iar`, `g_irq_last_iar`, `g_gic_isr_last`, `g_irq_icfgr_shift` - and
they survived for a reason that has nothing to do with being records: between each store and its key
there happen to be other calls. **A record whose only reader is the key beside its store is not a
record**: the compiler forwards the value across the one line and the storage goes, and whether it does
so is a fact about inlining and interruption, not about the source.

So the answer is not "the key must read the record". It is:

> **A file-scope record earns its storage by being read where its value is not already known.**

The seven became zero. Each key publishes the value it holds at the moment it is taken, and each
declaration is gone, with the reason written where the declaration was:

    entry_irq.c       g_irq_first_iar     ->  IRQ_LIVE("xnu_live_irq_first_iar", iar)
                      g_irq_last_iar      ->  IRQ_LIVE("xnu_live_irq_other_iar", iar)   (the stop path)
                      g_irq_icfgr_word    ->  IRQ_LIVE("xnu_live_irq_icfgr_word", word)
                      g_irq_icfgr_shift   ->  IRQ_LIVE("xnu_live_irq_icfgr_shift", shift)
                      g_irq_cli_last      ->  IRQ_LIVE("xnu_live_irq_cli_last", intid)
    MSM8974GIC.cpp    g_gic_isr_last      ->  entry_live_write("xnu_live_gicdrv_isr_last", intid)
                      g_gic_isr_done      ->  entry_live_write("xnu_live_gicdrv_isr_done", 1u)

The seventh is the one that needed a different argument rather than a different reader: `g_gic_isr_done`
was written `1u` and published on the next line, so its value is defined by the **program point** and
not by the machine. A value that cannot vary has no business in `.bss`, and the honest source says so -
the key is a literal, and what keeps it honest is *where* it is written, which is now a clause of the
claim (published inside the handler, after the withdraw block, so a path that returned early or kept
the line cannot reach it).

## The claim: nine claims and fifty-six mutations

`tools/check_irq_routing.py` grew from **8 claims / 50 mutations** to **9 / 56**, all refused, and it
runs inside `build_entry.sh` in both modes. The new claim 8 (`claim_static_storage`) is the only claim
in this file that cannot be made from the source at all:

  * **every non-`const` file-scope `static` object either file declares must be in the linked image**,
    with C++ names looked up under the name the linker gives them (`_ZL<len><name>` - the length is what
    makes the lookup exact rather than a prefix match, and `MSM8974GIC.cpp`'s nine records are all
    `_ZL…`). 29 records are checked in this image: 20 in `entry_irq.c`, 9 in the driver.
  * **the conditional matters and is evaluated**: nine of the driver's records are inside
    `#if MSM8974_GIC_DRIVE_SGI`, and with the switch at 0 they are not compiled at all, so demanding
    them would be wrong in exactly the state the switch exists to create. `compiled_text()` evaluates
    the file's own `#if`/`#else`/`#endif` over its own `#define`s - verified in both directions, by
    reading `file_scope_statics(compiled_text(driver, switch = 0))` and finding `g_gic_starts` alone,
    and by finding 20 records in `entry_irq.c` whichever way its three conditionals are taken.
  * **and the marker's position**, above.

Why this is not a source reading: reproducing the answer from the source would mean reproducing the
optimizer on a file this check does not compile. One name looked up in the linked image is one
instruction and no model - and it is the only form that can name *which* declaration is wrong, which is
the whole difference between a check and a note.

**The check was shown to refuse a real artifact, not only its mutations.** Built against 496's sources
it fails with the seven names, and one of the messages is the step's thesis in one line:

    496's source, this check:
      entry_irq.c:65 declares the file-scope record `g_irq_first_iar` and the image has no
      `g_irq_first_iar`: the record is written and never read, or read only where its value is
      already known, so the optimizer removed it - the source describes storage the machine does not
      have, every key that names it publishes something else, and the run still reads correctly.
      Either the record is a record (read it where its value is not already known) or it should not
      be declared

**Six mutations, two of them new shapes.** `a_record_the_image_does_not_have` (written, never read),
`a_record_that_is_read_but_never_written` (read, never written - a compile-time constant, so the read
folds and the storage goes), `a_driver_record_the_image_does_not_have`,
`the_driver_marker_becomes_a_variable_again` (496's shape for the seventh, restored),
`the_marker_is_published_before_the_withdraw`, `the_marker_is_not_published`. The mutations introduce
the defect's *shape* rather than reverting one of the seven names, because a mutation written against
the names a check was built for tests the names and not the rule. A seventh was written and **refused
nothing**: `the_driver_record_is_declared_outside_its_switch` puts the switch at 0 and moves a
declaration out of its block, expecting the claim to demand a record nothing uses - but a mutation can
only change the *source* while the symbols come from one real build, and taking a record out of the
compiled set leaves it present in that image. It was removed rather than shipped as a mutation that
tests nothing, and the conditional's verification is the direct reading above. That is this session's
own defect, recorded below.

## One run, and what it adds

**One** hardware run, through the gate, exit 0, `/tmp/stage90-497-run1.log` 555319 bytes, ending
`No errors detected` with the device back on Android on its own. Its point is narrower than 496's and
worth stating: the claim is about the **image**, and the run is the reading that no *key* was lost with
the variables - all seven subjects are still published, with 496's values:

    xnu_live_irq_first_iar    = 0x00000014      20 = the timer line, one record
    xnu_live_irq_icfgr_word   = 0x00000c04      the `GICD_ICFGR<n>` offset for it
    xnu_live_irq_icfgr_shift  = 0x00000008      its field in that word
    xnu_live_irq_cli_last     = 0x00000000      published once per client call, three times
    xnu_live_gicdrv_isr_last  = 0x00000000      likewise, three times
    xnu_live_gicdrv_isr_done  = 0x00000000 then 0x00000001
    xnu_live_irq_other_iar    = ABSENT          the stop path was not taken - see below

`xnu_live_irq_other_iar` is the one that no run can show while the boot is healthy: it is published
only on the unexpected-line stop, which has never been taken (`xnu_live_irq_other_count` is absent
again, no `stub_hit`, no `sleh` stop). "The key is still there" is therefore a claim about the source
for that one and about the log for the other six, and the run says which is which.

The key **set** is otherwise 496's: the only names present in one run and not the other are
`xnu_live_dec_same`/`xnu_live_dec_other`, which are published on powers of two of a counter and which
496's own run 2 also lacked - a run-to-run difference, not a step difference. Everything the boot
decides is unmoved: the console block is byte-identical to 496's (1289 characters by the endpoint pair
used here; 1288 from the byte after the marker's newline, sha256 `a0593af0…`), the fault counters are
495's and 496's (`sleh_seen = 0x20`, `_armed = 0x1c`, `_redirected = 0x1a`, `_storm = 9`), the GIC
driver's readings are 496's (`_drive_compiled = 1`, `_own = 0`, `_cli_rc = 1`,
`_en_before = _en_after = 0x00107fff`, `_pend_before = 0x20400000`, `_isr_calls` 1 -> 2 -> 3,
`_isr_pends` 1 -> 2, `_isr_unregs = _isr_unreg_rc = 1`), the registry's are 496's
(`_cli_slot = 0`, `_cli_handler = 0x8027facc`, `_cli_capacity = 4`, `_cli_calls_total` 1 -> 2 -> 3),
`irq_late_count = 0` with `irq_timer_count` to `0x800`, and 495's three timer latencies reproduce
again - **101.040729 / 101.044687 / 101.101250 ms** and inter-fire gaps **101.046979 / 101.051770 ms**,
which is the third boot in which the same two numbers come back within 60 µs.

## What is owed

  * **A line owned by a peripheral.** Unchanged from 496 and still the top of the list: SGI 0 is the
    driver's own device, and no real device asserting a line has yet been routed to a driver that
    cleared it. The distributor's enable for a peripheral interrupt, the `AckC` question and the
    handler's return path into `rtclock_intr`'s world come with that step.
  * **The conditional clause of claim 8 has no mutation of its own**, and the reason is structural
    rather than an omission: this selftest changes the source while the symbol table comes from one
    real build, so it can add a name the image lacks and cannot take away a name the image has. A
    future step that builds two images from two sources - which the `MSM8974_GIC_DRIVE_SGI` switch
    already makes possible - could test it; until then it is verified by a direct reading of the
    filter, and `the_driver_record_is_declared_outside_its_switch` is not shipped pretending otherwise.
  * **The unregister's guard is the client's, and a stop and a hang look the same in the log.** An
    intid that reaches the dispatcher with no client is a stop, so a driver that gives its line back
    early and a machine that stopped look alike from inside; `_isr_guard` and `_cli_unregistered` are
    separate keys for that reason.
  * A timeout that outlives its asker, `/timer`'s second definition, **the other device nodes**
    (`/arm-io`, `/cpus`, …), **the two services the catalogue answers with nothing** (their class),
    `MSM8974RootResource`'s `state0 = 0`, a name/class reader wider than eight characters, the release
    as a reading, `vm_fault` as a caller-side record, 488's deferred flag-list derivation, 490's
    distinct-`(pc, lr)` frames band, and `xnu_live_dec_same`.

## Safety

One run, through `preflight_boot_check.sh --allow-xnu-entry` then
`run_and_capture.sh --allow-xnu-entry`, non-persistent `fastboot boot` only, never a flash. Exit 0,
ending `No errors detected`, device back on Android on its own. The artifact the run booted is
byte-identical to 496's, so the step adds no risk to the machine: it changes which names the source
declares and adds a check, and the two things it could have broken - a key that stops being published,
and a record that stops existing - are exactly what the run and the check measure. The watchdog keys are
495's (`_enabled = 1`, `_timeout_s = 0x19`, `_bite_after = 0x000dffac`, `_readback_ok = 1`,
`_counter_running = 1`, `_countdown_plausible = 1`, `_bite_truncated = 0`), so the net was armed, was
running, and did not bite - and no persistent write of any kind was made.
