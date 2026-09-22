# 547: the pop's load succeeds and its value is wrong, and the ledger says the wrapper's write kills the recovery

A host-side reading of 520's own register dump - the run that returned - against 546's mechanism and
against the phase's four-cell ledger. No device and no build. It produces one new fact from the dump, one
reconciliation the ledger needed, and a sharpened prediction for 533 that can be wrong in a useful way.

## 1. What the dump says, field by field

`/tmp/cancro-last_kmsg.txt`, the archived 520 log, at the death:

```
panic(cpu 0 caller 0x80454584): sleh_abort: prefetch abort in kernel mode: fault_addr=0x4b79074
r11:  0x04b79075   sp: 0x8054fed0   lr: 0x800462dc   pc: 0x04b79074
cpsr: 0x800000b3   fsr: 0x00000005   far: 0x04b79074
```

Five things are readable off it, and the first two are new to this document:

| field | reading |
| --- | --- |
| `lr = 0x800462dc` | the return address pushed by the exit's `bl FlushPoU_Dcache` (`0x800462d8`). `pop {fp, pc}` does not write `lr`, so this is the last `bl` executed - and the next `bl` in the function is at `0x80046304`. Execution is therefore **at or after the exit's final `pop {fp, pc}`** (`0x8004633c`), which is exactly where the frontier says it dies |
| **the abort is a prefetch abort, at `far = pc = 0x04b79074`** | the faulting access is an **instruction fetch**, not a data access. **Both loads of the `pop` completed.** This is the fact that matters: the stack address was mapped, the load succeeded, and the *value* it brought back was wrong |
| `fsr = 0x00000005` | translation fault, level 1 - the fetched address has no translation at all. So the value was not a valid-but-wrong address; it was not an address |
| `cpsr = 0x800000b3` | mode `0x13` = supervisor, and bit 5 set = **Thumb state**. The core branched with bit 0 of the popped `pc` set, i.e. it entered Thumb because the value said to, and the fetch went to `value & ~1` |
| `r11 = 0x04b79075` | the popped `fp`. The payload's own instrument captured the same number, `xnu_live_slot_rtcpre_pop = 0x04b79075` - it is `cpu_data->rtcPop`, the deadline the idle loop computed (the reading 520's run was built to produce) |

So `r11 = 0x04b79075` and the fetch was at `0x04b79074 = 0x04b79075 & ~1`: **the pop loaded the same
counter value into both `fp` and `pc`** (or two words agreeing to the bit that was masked off - the dump
cannot separate those two). What it was supposed to load is the `{fp, lr}` the exit's `push {fp, lr}` at
`0x800462d4` wrote, whose `lr` would be an address inside `cpu_idle`.

## 2. The address-arithmetic family is eliminated, and this needs no cache hypothesis at all

`platform_cache_idle_exit` is `push {fp, lr}` at `0x800462d4` and `pop {fp, pc}` at `0x8004633c` with
**nothing between them that touches `sp`**. So the store and the load address the same location *by
construction*, and the two words differ only in fp-vs-lr. The dump says the load returned neither.

That rules out, without invoking caches: a wrong stack pointer, a frame-size mismatch, a misaligned or
unmapped stack, and "the push never executed". Every one of those predicts a failure the dump does not
show - a data abort on the load, an address in `far` that is a stack address, or an `lr`/`sp` pair that
disagrees. **The load succeeded from the right address and the memory gave back the wrong value.** The
defect is in the memory system between the push and the pop, and 546's window reading (§1 of that
document: the push with `SCTLR.C` = 0, the pop 24 bytes after the cache comes back on, and no L2
invalidation anywhere in between) is the description of that window.

## 3. The reconciliation the ledger needed

The phase's four cells, from 533's own opening table:

| | exit-side `FlushPoC_Dcache` | enter-side `SCTLR.C` re-enable | capture | returned? |
| --- | --- | --- | --- | --- |
| 518 / 519 / 520 | off | off | 520's shape | **yes, dying at the pop** |
| 521 | **on** | off | 521's repair | no - power press |
| 522 | off | **on** | 521's repair | no - power press |
| 526 | off | **on** | none | no - power press |
| **533** | off | **off** | none | *this arm* |

The table's own conclusion is that the enable is the difference - and that is where 546's mechanism and
the ledger looked like they disagreed, because 522's doc states the mechanism's fix as "make the push's
store update the cache, which is what happens when the D-cache is on", i.e. **the enable-on write is the
fix** - and the two cells that have it did not come back.

**They are not in conflict, and the dump is what separates them.** With the enable on, the wrapper's
`entry_idle_cache_enable()` (`entry_stubs.c:6560-6569`: `SCTLR |= 4`, `dsb`, `isb`) runs at the end of the
enter wrapper, so `SCTLR.C` is on for the WFI and for the exit's push - **the mechanism's precondition, a
store the cache did not see, is absent in those cells.** Their pop should read the right words. So whatever
killed 521/522/526, it is *not* 546's death, and **the ledger's enable-on rows are not evidence against the
mechanism; they are rows where the mechanism should not fire and no log exists to say what happened
instead.** (521 is the weaker row: it changes the flush *and* the capture, so its death is not attributable
to one thing.)

What the enable-on cells do share is the *shape of their failure*: no log at all. The enable-off cells
produce a log, a panic at the pop, and XNU's own `MACH Reboot`. **So the real discriminator in this ledger
is not "dies at the pop" versus "does not" - every cell that logs dies at the pop - but "dies with a log and
a recovered reboot" versus "dies with nothing".** And the thing the enable-on cells share is a write that
re-enables allocation while the core is **outside the coherency domain**: Apple's enter clears `ACTLR` bit 6
("Leave the coherency domain", `caches.c:434`) at `0x800462b8`-`0x800462c0`, and the wrapper's write follows
`platform_cache_idle_enter`'s return. **That is the one place in this phase where a control register is
written in a state Apple's own code never writes it in**, and it is the leading explanation for a death
that takes the log with it - recorded here as a hypothesis, not a finding.

## 4. The prediction for 533, sharpened and falsifiable

533 removes the enter-side write, so 533 is 520's cell with a different capture. Combining §2's mechanism
with §3's reconciliation:

- **the pass reaches the exit, the push runs with `C` = 0, the pop loads the stale words, the prefetch
  abort panics at `pc = the popped value & ~1`, and the device comes back on XNU's `MACH Reboot`** - a
  return, with the log carrying the same panic shape, `lr = 0x800462dc`, and `r11`/`pc` a counter value;
- `slot_pre_calls` and `slot_rtcpre_calls` published with `slot_post_calls` absent (the death is inside
  `platform_cache_idle_exit`, which is what 540 §3's localization prints);
- `slot_cwe_win` and `slot_cwe_set` both `C` clear (533's arm's shape).

> **Correction (559, before the run): the second key is `rtcpre`, not `rtcab`.** Written here as
> `slot_rtcab_calls` and corrected above in place; §4's prediction - the same three sites, the same
> order, the death at the `pop` - is unchanged, and this section is still the pre-registration the gate
> cites for the enable-off arm. **The key name is an instrument fact and it was wrong**: the exit
> wrapper's second publisher is `g_slot_rtcpre` (`entry_trace.c:1973`,
> `entry_slot_rtc_note(&g_slot_rtcpre, entry_tpidrprw())`), while `g_slot_rtcab` belongs to the **abort**
> path (`entry_stubs.c:1792`, inside `entry_note_sleh`) - so `rtcab_calls` is a *storm* count and not a
> per-pass one (520's log: 5 records, `1,2,3,4,8`, against exactly one `rtcpre`). The difference is not
> cosmetic for a prediction: "`rtcab` present, `post` absent" is satisfied by any pass that took an
> abort, so the localization this section pre-registers would have been printed for a death *outside*
> the exit. Corrected in place rather than rewritten, because the run has not happened and a
> pre-registration that changes silently is not a pre-registration. The peer session found the same name
> in `run_and_capture.sh`'s clause (3) and in 533/540 (its 558); 546 §5, this section, 554 §1 and 557 §4c
> were outside that enumeration. See
> [560](experiment-560-the-same-wrong-key-in-four-more-files.md) (and, for the gate's own copy of
> the bracket, [559](experiment-559-the-brackets-middle-key-was-named-wrong-in-the-one-block-that-governs.md)).

**The falsifier is worth stating as a table, because the two outcomes mean different things and both are
useful:**

| 533's result | reading |
| --- | --- |
| dies at the pop, **log present**, device returns | the enable is confirmed as the difference between a recovered death and a hang, and 546's mechanism stands for the enable-off cells |
| dies at the pop, **no log**, no return | the enable is *not* the discriminator; the capture's shape is back in play (533 differs from 520's cell by the capture alone), and the next bisection is 520's publisher shape with the enable off |
| **returns with no pop death at all** | 546's mechanism is wrong for the enable-off cells too - the first outcome that should stop 535 being built as designed |

## 5. What this says about 535

Unchanged in its core and sharper in one place. The operation has to be a **PoC invalidate of the L2, in
the window, after the push and before the re-enable** (546 §4: the only `bl` reachable there is the exit's
`FlushPoU_Dcache` at `0x800462d8`, identified by return address `0x800462dc`), because what the pop reads
is a line the push never updated. **And §3 adds a constraint to the arm's own shape: it must not
re-enable allocation outside the coherency domain.** 522's wrapper write did, and the two cells that
carried it produced no log - so "set `SCTLR.C` in the wrapper" is not available as part of 535's design,
whatever else it does.

## 6. What this does not decide

- **Whether the line the pop reads is stale in the L2 or absent from it** - the two stories predict the
  same wrong value; nothing here separates "a valid stale copy" from "no copy at all, and the memory
  read returned something else". Both are repaired by the same invalidate.
- **Whether the counter value in the slot is the previous occupant's leftover or a live value the region
  holds.** The dump says the two popped words agree (to one bit); it cannot say why.
- **Why the enable-on cells produce no log.** The coherency-domain hazard is a hypothesis, and 521's row
  is confounded.
- **533's arm**, unchanged, frozen, unrun: `1daaf44e624563694e…` / `f202f2465886aba6…`.
- **535** and the storage line and TWRP, unchanged: still withheld.

## 7. Safety

No device action in this step: `sed`/`grep` over the captured log in `/tmp`, and over
`entry_stubs.c`/`entry_trace.c`. Nothing written outside `docs/` and the memory files, no build run, no
file under `out/` touched, `flash` not used, frozen pair untouched (`1daaf44e624563694e…` /
`f202f2465886aba6…`). The device is off the bus and owes a power press before 533 can run.
