# 585: one value, two runs, and the pair the pop actually read

576 section 3 said 520's log "holds both sides of the mechanism measured on one pass", and quoted the
pop's pair as `{0x33f1c1b5, 0x33f1c1b5}`. **Those numbers are 533's.** `0x33f1c1b5` occurs zero times in
520's capture, and 520's own dump reads two *different* values. Correcting it turns a two-run coincidence
into the sharpest reading of the death this project has: **the value the pop lost is still live in `lr` at
the abort, so the frame's correct word is known to have existed and the pop demonstrably did not read it.**

Host-side only: no device action, no build, no arm. The frozen 574 arm is byte-identical
(`151425c4…` / `3bc72605…`), the gate is green, and the phone is off the bus.

## 1. The correction, and how it was found

Reading 520's capture for what its fatal abort actually left in the registers:

```
panic(cpu 0 caller 0x80454584): sleh_abort: prefetch abort in kernel mode: fault_addr=0x4b79074
r8:   0x80553520  r9: 0xc0573df0 r10: 0x800ba588 r11: 0x04b79075
r12:  0xde58b701  sp: 0x8054fed0  lr: 0x800462dc  pc: 0x04b79074
```

and 520's instrumentation publishes `xnu_live_slot_rtcpre_pop=0x04b79075`. So `fp = 0x04b79075` and
`pc = 0x04b79074`: **consecutive integers, differing by one in the low byte, and neither is a stack or
code address.** The value 576 §3 quoted is not in this file at all - it is 533's, whose dump has
`r4: 0x33f1c1b5`, `r11: 0x33f1c1b5`, `pc: 0x33f1c1b4`, `xnu_live_slot_rtcpre_pop=0x33f1c1b5`, and
`xnu_live_slot_ab_m8 = ab_m4 = 0x33f1c1b5` (an *abort-time* capture of the slot, both words equal - the
thing 576 §3 described, true of 533 and false of 520).

**The source had the right pair the whole time**: `entry_stubs.c:6491` writes
``xnu_live_slot_rtcpre_pop = 0x04b79075` against `pc = 0x04b79074``. So this was a document quoting the
wrong capture - the "a conclusion about one artifact drawn from another" class, with the two artifacts
being two runs of the same experiment four hours apart.

## 2. What the corrected pair shows, in two independent runs

| run | `rtcpre_pop` (from `cpu_data`) | the pop's `fp` | the pop's `pc` | `lr` at the abort |
| --- | --- | --- | --- | --- |
| 520 (2026-09-22) | `0x04b79075` | `0x04b79075` | `0x04b79074` | `0x800462dc` |
| 533 (2026-09-23) | `0x33f1c1b5` | `0x33f1c1b5` | `0x33f1c1b4` | `0x800462dc` |

Two runs, two images, four hours apart, and the same structure:

- **`fp` is exactly the value the exit wrapper publishes as `rtcpre_pop`** - which the code reads from
  `cpu_data + STAGE90_CPU_RTCPOP` (`entry_stubs.c:6454-6455`), the idle loop's own computed deadline. Not
  a stack word.
- **`pc` is that value minus one.**
- **Neither is the frame.** And `lr` at the abort is `0x800462dc` - the address the exit's own
  `bl FlushPoU_Dcache` returns to, i.e. **the very value the `push` stored into the second word of the
  frame**, still intact in the register because the `pop {fp, pc}` does not write `lr`.

**So the death is measurable without a cache theory**: the push's second word is a value the machine
still holds in `lr`, memory received it (the store went out with the cache off), and the `pop` into `pc`
got something else - twice, deterministically across two builds. 546's mechanism is not needed to *state*
the failure; it is needed to explain where the other value came from, and the pair above constrains that
explanation more than 546 does: **what the pop read was not stale stack content at all** (no stack address
appears), but a pair of `cpu_data`-derived values one apart.

**(What that provenance is, this step does not claim.** Two candidates are consistent with the numbers and
this document does not choose between them: the line's cache copy was made while some code was writing the
WFI deadline pair into those addresses with the cache on, or the two words were written into the frame by
the loop that computes the deadline. What is settled is only what the pop read and that it read it in both
runs.)

## 3. What this does to 574's reading, and to one branch of the reader

**The prediction is unaffected and now better supported.** 582 predicted that 574's arm reads
`b1 = 0x8047c990` at the seam - the exit's own `bl` return address, read from memory with the cache off.
The table above is two runs in which memory demonstrably received the push's `lr` (it survives in the
register) while the *cache* did not agree. `b1` reads memory, so it should be that address.

**And the reader's 535 branch is a hypothesis wearing a comparison's clothes.** Its middle case fires when
`a1 == xnu_live_slot_rtcpre_pop` and reads as "the dirty line's copy of the slot's `lr` word is the deadline
that pass read". The comparison is now *supported* - in both runs the stale content at the slot was
exactly `rtcpre_pop` - but the sentence's explanation ("the dirty line's copy of the slot's `lr` word") is
not what was measured, and calling a `cpu_data` field "the slot's `lr` word" is the name-versus-body
mistake this project keeps finding. The branch is left as it is, and the sentence now says which of the
two it is a measurement of; the next run is what decides the provenance.

## 4. The synthetic rehearsal inputs carried the wrong run's value

The 574-shaped log used to rehearse 581/582/583 (`581-SYNTHETIC-NOT-A-CAPTURE-574-shaped-log.txt` and the
files derived from it) carries the seam keys with `0x33f1c1b5` in `b0`/`b1`/`a0`/`a1` - and **520's image
predates the seam, so no capture could have produced those keys at all**; they are invented placeholders.
Their values were taken from 533's run, which is how the same wrong value reached the reader rehearsals.
The file is renamed to say so and every copy of it is labelled; a synthetic input is allowed to be
invented, but not to look like a reading.

## 5. Safety

No device action, no `fastboot`, no `adb`, nothing written to storage, no build input, no arm: two
`grep`s and two `sed`s over logs already parked, one read of a source comment, and edits to prose plus a
file name. The frozen arm is untouched and unrun; the phone is still off the bus.

TWRP stays withheld: 「如果os已经能进去了的话」 is unmet.
