# 592: a PASS on `b1` is not a clearance of the `pop`, and the pairing it is read by

Two things came out of checking one claim the peer session made about 590: **the claim's substance
holds and its pairing does not.** What holds is that the seam's four words are read with the caches
off while the `pop` runs with them on, so the one key this arm predicts can refute the frame and
cannot clear the `pop`. What does not hold is which of the two words the `pop` takes as `pc` - it is
`b1`, the same word the prediction is about, and not `b0`.

Both halves are decided by addresses in the frozen entry image (`151425c4…`, untouched, unrun) and by
nothing else. No device, no build, no arm: one reader edit, `--summarise` over logs already on disk,
and the gate. The frozen pair is byte-identical, the phone is off the bus, and the boot still waits
only on the user's power press.

## 1. What the peer's item 4 said, and what checking it found

The claim was that `b0`/`b1` are read before Apple re-enables caching and the `pop` runs after, so
`b1 == 0x8047c990` says DRAM held the frame's word, not that the `pop` sees it - and that the pairing
to look for is therefore `b0` against the run's `xnu_live_sleh_pc`, because "the word the `pop` takes
as pc is b0".

The first half is exactly right and is the reason this experiment exists. The second half is inverted,
and it matters: it would send a failure to the wrong word, and it is the reading a careful person
arrives at by counting the two *words* instead of the two *registers*. Three statements in the tree
say otherwise, and the one that decides it is the disassembly:

| what | where | what it says |
| --- | --- | --- |
| the seam's base | `__wrap_FlushPoU_Dcache`, `0x8047cb90` | `mov r0, sp` - and a `bl` does not move `sp`, so the base is the exit's `sp` *as its `push {fp, lr}` left it* |
| the push | `platform_cache_idle_exit`, `0x800462d4` | writes `fp` at `[seam_sp]` and `lr` at `[seam_sp+4]` |
| the pop | the same function, `0x8004633c` | `pop {fp, pc}` - ascending register list, lowest register from the lowest address |

So `b0 = [seam_sp]` is the pushed `fp`, `b1 = [seam_sp+4]` is the pushed `lr`, and `pc <- [seam_sp+4]`
: **`b1` is the `pop`'s own `pc` word.** The other two statements are the reader's own: its header has
said "`b1` is therefore a **return address**" since 582, and its `STALE LINE, WRITTEN OUT` arm
compares `a1` against `xnu_live_slot_rtcpre_pop` - which is only coherent if `a1` is the post-flush
value of the word the `pop` loads as `pc`. Nothing was changed on account of the pairing except to say
it once, in the header, so that the inverted form is not the one a later reader re-derives.

## 2. The mechanism, at instruction level

`entry_seam_flush` (`0x8047c9c4`) is entered through `__wrap_FlushPoU_Dcache` (`0x8047cb90`) from the
exit's own `bl` at `0x800462d8`, and it reads the pair twice: `b0`/`b1` from `[r0]`/`[r0+4]` **before**
its `bl FlushPoU_Dcache`, `a0`/`a1` from the same two addresses **after** it. It contains exactly one
coprocessor instruction - `mrc p15, 0, sl, c1, c0, 0` at `0x8047ca64`, the `SCTLR` read, taken after
the `a` pair and before the return - and no `mcr` at all, so `SCTLR` cannot change inside the seam and
that one reading is `C`'s value for all four words.

Apple turns `C` back on afterwards, in `platform_cache_idle_exit` itself: `0x8004631c` reads `SCTLR`,
`0x80046320` ORs `#4` (the `C` bit), `0x80046324` writes it back and `0x80046328` is `isb`. The `pop`
is at `0x8004633c`, sixteen bytes later. (The `0x8004630c`-`0x80046318` block that precedes it is
`ACTLR` - `c1, c0, {1}` - and not `SCTLR`, so it is a different register and not part of this.)

Therefore: **all four seam words are readings of DRAM; the `pop`'s lookup is not.** A stale L1/L2 line
answering that lookup is exactly the mechanism 546 section 3 proposes and is *inside* what this key
cannot see - which is the sentence the run's reading needed and did not have.

## 3. The repair: three changes, and the C bit read rather than asserted

1. **The header** now carries the derivation above (base, both words, the `pop`'s register order) and
   the `C=0`/`C=1` asymmetry, so the two facts 582 and 590 stated separately are stated together.
2. **A new clause reads the `C` bit out of `xnu_live_seam_sctlr`** - `(( (seam_sctlr >> 2) & 1 ))` -
   instead of the prose asserting that the reads were uncached. That is the whole point: on the run
   that matters, "with `SCTLR.C` clear" was a claim in a comment about a value the log already carries.
   The key is in the frozen image (grep of its own ELF strings), so this is a guard and not the
   expected state, and the guard has three outcomes, all rehearsed:

   | `xnu_live_seam_sctlr` | line | what it costs |
   | --- | --- | --- |
   | `C=0` | `PASS` | none - and the line says the `pop` is still outside it |
   | `C=1` | `FINDING` then `UNREAD` | the pair is a reading of the cache, not of the frame |
   | absent | `UNREAD` | same cost, for the reason the key is not in this log |

   The `C=1` case prints **two** lines because it is two claims: the fact (`FINDING`, 546 section 1's
   premise for this cell failing) and what it costs (`UNREAD` - the four words and every comparison
   below them are not evidence). Both set `verdict_ok=0`, so a block whose comparisons were all made
   against cache contents cannot be summarised as passing.
3. **The `b1` PASS line** now says what it establishes and stops there: that `b1` is the `pop`'s own
   `pc` word, that a wrong `b1` refutes the frame, and that a right one does not clear the `pop`.

One thing deliberately *not* changed: the `b1` prediction itself. It is still `0x8047c990`, and the
coming run is still the one that answers it.

## 4. Rehearsals, and the regression

| what | how reached | result |
| --- | --- | --- |
| `sctlr` `C=0`, `b1` right | 586's synthetic log, unchanged | `PASS` `C=0` + `PASS` `b1` (`c0`) |
| `sctlr` `C=1` | the same log, the byte at bit 2 flipped | `FINDING` + `UNREAD`, as designed (`c1`) |
| `sctlr` absent | the same log, the key deleted | its own `UNREAD` (`none`) |
| four `b1` states | 582's four synthetic logs, re-run | good / stale / other / junk - each still on its own arm |
| 520's real capture | `--summarise` | **exit 0, 0 FAIL, 0 UNREAD** - the block is gated out by design on that image (564) |
| the gate | `preflight_boot_check.sh --allow-xnu-entry` | **EXIT=0**, the same 23 sections, 0 real `UNREAD` |

The gate's output differs from 588's capture in **two hunks and no others**: its exit census, whose
line numbers moved `1214/1246/1256 -> 1278/1310/1320` - the edit's own line count, printed at gate
time because the census reads the runner rather than a copy of it (563) - and the `.prev` paragraph
that 589 landed after 588's capture was taken. The `ok` line for the seam's body is unchanged, and so
is the arm it describes. (`EXIT=0`; the gate prints no `UNREAD` tally of its own - the one `UNREAD`
in its output is the sentence naming the two lines the coming run will print, so "0 UNREAD" is a
statement about the run's log and not something this file can be read as saying.)

Captures: `out/stage90/captures/592-rehearsal/` (`592-README.txt` indexes them).

## 5. What this changes for the coming run, and what it does not

It does not change the prediction, the arm, the image, or the gate's verdict. It changes **how a PASS
on `b1` will be read**: as "memory held the frame's word while the caches were off", not as "the `pop`
is fine". That distinction is the difference between the next arm being designed against the *writer*
at `[seam_sp+4]` - which 590 named by address, and which is our own wrapper's `strd r4, [sp, #-12]!` -
and the next arm being designed against nothing because the run looked clean.

What is still open, and is not answered by anything here: whether the `pop`'s lookup actually misses
the frame's line, which is a question about a cache the log cannot see. `b1` right plus a death at the
`pop` is *consistent* with 546 section 3's mechanism and does not confirm it.

## 6. Safety

No device action, no `fastboot`, no `adb`, nothing written to storage, no build input, no arm: one
reader edit, four synthetic logs and one real capture re-read, and the gate. The frozen arm is
untouched - boot image `914f45ac…`, entry binary `151425c4…`, entry ELF `3bc72605…`, all three
re-measured after the edit - and unrun. **TWRP stays withheld**: 「如果os已经能进去了的话」 is unmet.
