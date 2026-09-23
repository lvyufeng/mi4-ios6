# 574: the measurement arm is built - the same seam with nothing behind it, and the gate refuses it in the intended direction

572 section 6's next arm is built and parked: **535's interception, and no operation behind it.** It
exists because 535's run did not come back and left no log (572 section 1), so "the operation cost the
return" and "the interception did" are still joined. This arm cannot change what the machine does - that
is a property of its shape, asserted by the build - and both of its outcomes are decisive without a log,
which is the point of choosing it. Nothing has been run: the device is still hung from 535 and needs the
power press 572 section 8 asks for. **(Numbering: this step was written as 573 and the peer session's
gate repair took that number in the same window (`c486cd2`); the doc and the artifacts are renumbered
574 and nothing else changed - the citation inside `entry_trace.c` was corrected to match before the
build that is described here.)**

## 1. What the arm is, read out of the built image rather than described

| what | value | where it comes from |
| --- | --- | --- |
| switch | `STAGE90_XNU_SEAM_MEASURE=1`, `STAGE90_XNU_SEAM_POC=0` | the record, twelve keys, entry `151425c4…` |
| the seam | the exit's own `bl FlushPoU_Dcache`, `0x800462d8`, returning to `0x800462dc` | the gate derives the same address; the build compares its own constant against the image |
| the interception | `--wrap=FlushPoU_Dcache`: **4** sites redirected, **0** direct | the build's census over the whole disassembly |
| the wrapper | `__wrap_FlushPoU_Dcache` `0x8047cb90`, 12 B, three instructions, no frame | `mov r0, sp` / `mov r1, lr` / `b entry_seam_flush` |
| the body | `entry_seam_flush` `0x8047c9c4`, **0x1CC bytes** against 535's 0x1D8 | 12 bytes *smaller*: the operation and the restore are gone |
| its contents | `dsb`, the two words, `__real_FlushPoU_Dcache`, the same two words, the readings | **14 loads, 0 `mcr` (coprocessor *write*) and 1 `mrc` (the compiler's SCTLR read, present in both arms), 0 calls to `FlushPoC_DcacheRegion`** |
| what it publishes | the same keys as 535 plus `xnu_live_seam_op=0` | the reader's clause (5) branches on it |

## 2. Why it cannot change the machine, and what is *not* asserted

The claim is: with `SCTLR.C` clear, both loads are non-cacheable reads of two words of the caller's own
frame; the arm writes no control register; and the body performs no cache maintenance, so it cannot move
a line, clean one, discard one, or change memory. The build asserts the parts of that which are
properties of the image:

- **0 `mcr` in the body** - a coprocessor *write* is the instruction class every cache operation in the
  ARM is spelled with, and a write is the only way this body could move a line or change a control
  register. (Written as "0 coprocessor instructions" until 580; see the update below.)
- **0 calls to `FlushPoC_DcacheRegion`** - 535's operation specifically, so a build that kept the call
  and dropped only the restore stops here;
- **>= 2 loads** - the pair is the arm's whole content, so a body that could not produce it is not this
  arm.

**(577 update: this paragraph is now out of date - the store IS asserted, at build time, by
identifying the slot's registers from the body itself and refusing any store that uses one. Measured on
both parked arms: this image 2 slot register(s) / 0 stores to them, 535's body the same registers and 2
stores - the falsification. See `experiment-577-*.md`.)**

**(580 update: and the count in the first bullet was a *pattern* answer that read as a count of the
class. The body does contain one coprocessor instruction - `8047ca64: mrc 15, 0, sl, cr1, cr0, {0}`, the
compiler's inline read of `SCTLR` - and **every arm of this seam contains it** (535's parked body has
the same instruction at `8047ca80`). `^mcr` never matched a `mrc`, so the refusal was right about what
matters and wrong about what it said: a read cannot change cache state, a write can. The build clause now
counts the pair apart (`mcr=0 mcr7=0 mrc=1 mrc7=0`, printed in the arm's narration), refuses any `mcr`
and any `mrc` naming `cr7`, and the property that actually separates this arm from 535's is the **callee**
(`bl FlushPoC_DcacheRegion`: 0 here, 1 there) and not any count. See
`experiment-580-the-census-that-names-its-opcodes-and-the-three-outcomes-of-section-4.md`.)**

**What 574 did not assert from the image, named rather than left implicit: that the body contains no store
to the slot.** The restore lives inside `#if STAGE90_XNU_SEAM_POC`, so with the switch at 0 the compiler
has no such store to emit - but the clause counts instructions, and a *store* is not distinguishable from
the frame's own pushes and the live-channel writes by opcode alone. The residual risk is nil in effect
rather than by assertion: the arm's second read has already put the slot's two words in registers, so a
stray store to that address would write back the value just read from it, with the cache off - the same
bytes, through the same address. That is the honest statement of the check's reach, and it is why the
safety argument rests on the two counts above rather than on a third one.

## 3. Two switches, and the gate refuses the new one in the direction 569 section 5 predicted

`SEAM_POC` and `SEAM_MEASURE` select one interception with two bodies, so the build refuses both at once
(`build_entry.sh:421`, and `entry_trace.c`'s `#error` says the same thing one level down) and records
both keys **always, 0 included** - a record carrying one and not the other would name an image by half of
what makes it that image. `SEAM_ON=$(( SEAM_POC | SEAM_MEASURE ))` is the single variable the link and
every clause read, so "is the seam hooked" cannot be answered from one switch and be wrong about the
other.

And the new key is refused by the gate **before any device action**, exactly as 569 section 5 said it
would be:

```
REFUSING: out/stage90/xnu_arm_entry-config.txt carries key(s) this gate does not print:
STAGE90_XNU_SEAM_MEASURE - a switch recorded on the build side and not shown here is a switch the next
run would go out with unread; add it to ENTRY_CFG_KEYS above
```

Recorded verbatim at `out/stage90/captures/574-gate-api-refusal.txt`. That refusal is the *intended*
signal and not a defect: a gate that printed the arm's readings while not knowing which arm the build
made would be the 570 defect with the sign changed. The gate's `ENTRY_CFG_KEYS`/variant narration is the
peer session's file (570 is the precedent), and the boot waits for it.

## 4. The 535 -> 574 image difference, decomposed the way 571 section 3 decomposed the last one

Same size (5519996 B), 529,284 differing bytes (132,321 words) - and this time **the difference is a
subtraction**, so the interesting question is whether anything moved that should not have:

| measurement | value | reading |
| --- | --- | --- |
| `.text` | **5307112 in both** | the 12 bytes the body lost are absorbed by the alignment of the next object, so the section does not shrink |
| Apple's own symbols | `panic 0x8003c5ec`, `sleh_abort 0x80454168`, `arm_init 0x8000cff8`, `platform_cache_idle_exit 0x800462d4`, `FlushPoU_Dcache 0x80045874`, `FlushPoC_DcacheRegion 0x8004589c` - **identical in both** | no Apple code moved |
| symbols **below** the seam objects | **none changed at all** | the entry geometry is untouched this time, because the section did not grow: `__entry_*` is identical in both images |
| symbols at or above the first seam object after the body | 1,216, deltas `-0xC`/`-0x10` (840/109) and `+0x10`/`+0x4` (264/3) | the relaid-out entry objects; the mixed signs are re-padding at two different alignments |

So the image is 535's with the operation removed and the entry's own objects relaid out after it, and
nothing else: the arms differ in the body and in the addresses that follow it.

## 5. The reader now branches on the arm, and the rehearsal that found it had to

**The pair means opposite things in the two arms.** With the operation, an unequal `_a`/`_b` is the
clean's write-back; without it, an unequal pair is Apple's own `FlushPoU_Dcache` writing the slot's line
back, and an equal pair is the arm working as designed rather than "the line was clean". A reader that
held only the pair would apply the wrong rule to one of the two arms, so clause (5) reads
`xnu_live_seam_op` and dispatches three ways - 535's rule, 572's rule, and **no interpretation at all**
when the key is absent.

The rehearsal is what caught the third branch, and it is worth recording because it is this project's
recurring shape: the first version of the change *printed* the UNREAD and then fell through to 535's rule
anyway, so a log that did not say which arm it was got a confident `CLEAN LINE` beside the sentence
saying the arm was unread. Measured on the real 568 log with only the arm's keys appended:

| variant | what was changed | measured output |
| --- | --- | --- |
| M1 | `op=0`, pair unchanged | `ARM 572 …` then `AS DESIGNED`, and **0** FAIL/UNREAD verdicts in the whole block |
| M2 | `op=0`, `a1` changed | `THE L1 FLUSH WRITES IT BACK` - the reading 535's run could not produce |
| M3 | `op` absent | `UNREAD …` **and** `the pair is NOT interpreted` (this is the branch the first version got wrong) |
| M4 | `op=1`, `a1` = the same pass's `rtcpre_pop` | `ARM 535 …` then `STALE LINE, WRITTEN OUT` - 535's rule still applies to 535's logs |

568's own log, unmodified, still gives 0 FAIL/UNREAD verdicts: clause (5) is self-selecting and stays
silent for an image that does not carry the arm.

## 6. What the run decides, pre-registered

| outcome | reading |
| --- | --- |
| **the device returns** | the interception is harmless and the operation is what cost 535's return: 572 section 5's candidates become the frontier, and the first of them is the one the arm's own arithmetic missed - `FlushPoC_DcacheRegion(slot, 8)` walks **64-byte lines**, so the clean wrote back all sixteen words of the line while the restore covered two |
| the device returns **and the pop still dies** | the same, plus the payoff: `xnu_live_seam_*` is in the log at last, with `op=0`, so the pair says whether Apple's own L1 flush writes the slot's line back - the control reading that makes 535's `_a`/`_a1` interpretable |
| **the device does not return** | the interception itself is implicated, before any more cache work, and every operation-shaped arm is refuted cheaply. This is the expensive cell (a power press) and it is the one this arm exists to make *unambiguous*: 572 could not separate the two because its run left no log |

No log means no reading for the pair, so the middle row is the one that needs the capture to work; the
first and third need only the runner's exit code, which is why the arm is worth a boot even if the
capture fails.

## 7. A count of mine the peer measured, recorded as this project's own defect class

In asking the peer for the gate fix in 572 section 7 I wrote that the old pattern
(`bl … <FlushPoU_Dcache>`, no wrapper prefix) "reads **2** on this image, and those two are *inside*
`entry_seam_flush`". Measured now, on 535's parked ELF: it matches **1** line - `8047ca58`, the seam
path's `bl __real_FlushPoU_Dcache` - and it is inside `entry_seam_flush`, so the inference (the callers
are not in that set) was right and the number was not. With `--wrap=X` in the link "calls to X" is two
disjoint sets and any single count is a count of one spelling; the peer's repair prints both
(`FLUSH_SITES` any prefix, `FLUSH_WRAP` the wrapper exactly), which is the right shape. The claim was
made from the shape of the clause rather than from the disassembly, which is precisely what
[[mi4-measurement-defects]] is a file about.

## 8. Safety

No device action in this step: an entry build, a payload build, one **read-only** gate run (which
correctly exited 1 at the new key's refusal), and host-side rehearsals against a log in `/tmp`. Nothing
was flashed and nothing was written to storage. **The device is still hung from 535's run and needs the
power press** (hold Power ~10-15 s, release, press Power normally); the next boot waits for both that and
the gate's key. The arm built here is the one that cannot hang by construction, which is deliberate after
535: if the *measurement* costs a windows, we learn that before spending another operation.

## 9. State, and what is owed

- **Parked:** `out/stage90/captures/574-*` - entry `151425c4…` (5519996 B), elf `3bc72605…`, qcdt
  `914f45ac…` (8540160 B), config `bcacf065…`, the build log, and the gate's refusal. 535's, 533's and
  520's artifacts are untouched beside them.
- **535's pair is parked, not lost:** its run is spent and its readings are 572's, so nothing is owed to
  it; its image stays in `captures/` for the diff in section 4.
- **Owed to the peer session:** `STAGE90_XNU_SEAM_MEASURE` in the gate's `ENTRY_CFG_KEYS` plus a
  `=1`/`=0` narration naming *which arm the build made* (that the seam's two arms are one interception
  with two bodies is the sentence that has to be there, or the gate's green would not say which arm it
  was green about). Their `_pop_lr_from_elf`/`FLUSH_N` fix landed as `c486cd2` in this same window.
- **Owed when the user presses power:** the run - `preflight_boot_check.sh --allow-xnu-entry` then one
  `run_and_capture.sh --allow-xnu-entry` - which waits for the peer's key, because a gate that does not
  print the key refuses the record by design.
- **TWRP stays withheld**: 「如果os已经能进去了的话」 is unmet, and 535 took it further from met rather than
  closer (572 section 8).
