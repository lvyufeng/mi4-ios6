# 556: one address, three definitions, and the gate binds the reader's to the image

A host-side change to `stages/stage90/preflight_boot_check.sh`, no device and no build. It does two
things, and they are the same defect seen from two sides.

**(a) The 526-era narration told the reader what the ending would mean, when the arm is what decides
it.** Its checklist item (4) said *"the same `sleh_storm 9` at the same `pc` as 520 would say the
mechanism is neither the capture nor the re-enable, and any later ending is progress."* That sentence
was written before 547 §4 was committed, and 547 §4 **pre-registers** the death for the enable-off arm:
the pass reaches the exit, the push runs with `C` clear, the pop loads the stale words, the prefetch
abort panics. So the narration would have read the predicted death as the negative control - and the
arm the gate is about to run is the enable-off arm.

**(b) The one address the result reader keys on was defined in three places and compared in none.**
`0x800462dc` is the return address of the exit's own `bl FlushPoU_Dcache` at `0x800462d8`. It appears

| where | how |
| --- | --- |
| the entry image's own bytes | `bl 0x80045874 <FlushPoU_Dcache>` at `0x800462d8`; `0x800462dc` is the next instruction |
| `run_and_capture.sh` | `EXIT_POP_LR_LITERAL=0x800462dc` (`:82`), the fallback `exit_pop_lr_addr()` uses when the ELF cannot be read |
| the experiment prose | 554, 555, and the paragraphs above |

The reader **derives** the address from `out/stage90/xnu_arm_entry.elf` at run time, so on a normal
host the literal is never consulted. It is consulted on exactly the runs where the ELF is not there -
a cleaned `out/`, or a `--summarise` in another tree - and on those runs a log read through the
fallback attributes the death by a number that is not in the image. The fallback is *labelled*
(`*** PINNED LITERAL - the entry ELF could not be read, so this criterion is NOT derived ***`), which
is the right direction, but **a label is not a check**. And at summarise time the ELF is precisely
what was not readable, so the comparison cannot happen there. It can happen here: this gate already
reads that ELF, on a host where it exists, before the run.

## 1. Why (b) is not a style point

554's shape test needs the address to do its job. With the wrong address the test lands on its FAIL
branch for a *correct* run, and the FAIL branch is the one that makes a human look - which is the
whole defect 554 was written to remove. So (b) is the same failure mode one level down: 554 repaired
the reader's *criterion*, and this repairs the *provenance* of the number the criterion is expressed
in. `mi4-one-value-two-definitions` is the class; this is its thirty-second instance, and the newest.

There is a second reason it belongs in the gate rather than in the runner: **who writes the value.**
The runner's literal is written by hand; the image's address is written by the linker; and the bin
embedded in the frozen pair is written by the build. If they disagree, this gate cannot tell which of
the two is stale, so it does not presume - it names the test and refuses. Nothing is rebuilt by that
refusal: the frozen pair embeds the entry image, so rebuilding would spend the freeze the gate exists
to protect.

## 2. The change, clause by clause

**(a) Item (4) of the 526 narration.** Replaced with the arm-decides-the-meaning text. It states 547
§4's prediction, states the **falsifier** - *a return with no pop death at all*, which is the one
reading that forbids 535 as designed - and then hands the judgement to `--summarise`, which is what
actually derives the address and separates the cells by the log's published `xnu_live_sleh_lr`. The
narration therefore restates no rule; a death read here by eye is read *that way* and not by this
sentence.

**(a2) The same item gains what the arm costs and which key localises the death**, both read off 520's
own log rather than assumed, and both from the peer's 557 reading, re-verified here against the log
before being written into a check. The per-pass publishers have **exactly one record each** -
`xnu_live_slot_pre_calls`, `xnu_live_slot_rtcpre_calls`, `xnu_live_sip_seq` and the
`xnu_live_pce_seq`/`_after_seq` pair - and every one of them publishes on
`n <= 4u || (n & (n-1)) == 0`, so one record means `n = 1`, i.e. one call; a second call would have
published `n = 2`. The door counter's last published record is `32768`, so the bypass ran at least
that many times while the **cache window itself was entered once** - and that one entry is the death.
Two consequences are now in the gate: a run of this arm gets **one pass at the exit**, so a
non-return costs a power press and nothing else, which is why this phase is expensive in **boots** and
not in passes; and the diagnostic item is the **bracket**, not the ending - `pre` and `rtcpre`
published with `xnu_live_slot_post_calls` absent is what puts the death inside
`platform_cache_idle_exit`, with no second pass to confuse it.

**And a trap that is now named rather than inherited.** The ram console has **two writers in two
blocks** - the records append at the buffer's size field, while the console text takes
`ENTRY_OS_BLOCK` reserved once - so a log line's *order* is chronological within its own block and
meaningless across blocks. 520's log holds the fatal death twice, as console text and as records, and
reading one as "before" the other is a wrong first reading the peer actually made. Any line number
quoted from that log therefore now says which block it is in.

**(b) A new closing section, `== the address run_and_capture.sh's shape test compares against ==`.**
It derives the address from the ELF, reads the runner's literal, and compares. Five branches:

- **the ELF is absent** → UNREAD, and the text says the reader will fall back and label it, and that
  the ELF is a build product whose bin is embedded in the frozen pair, so it can be read back.
- **the runner has no `EXIT_POP_LR_LITERAL=0x...` line** → UNREAD, plus a *stray-text* note. The
  address can live in that file in two spellings, and only the assignment is a value: the other is the
  literal inside the `grep` pattern the reader used before the derivation existed (554 §2), which in
  the file's present shape is a **comment**. A check that read a comment would be reading prose, so
  the note prints what it found as what it is rather than comparing against it - and it prints at all
  so that a renamed or removed fallback cannot make this clause go quiet.
- **the disassembly yields nothing** → UNREAD with the instruction to read it by hand.
- **the derived value is not a 4-byte-aligned kernel address** → UNREAD. This is the parse asserting
  the shape of the line it took before comparing the line's value, which is 520's rule for the gate's
  literals.
- **the two disagree** → `fail`, exit 1, with the whole argument in the message: which branch of 554
  the fallback would corrupt, that this gate cannot name which value is stale, and that the repair,
  if the image's value is the right one, is a one-line change to a *variable* and not to the artifact.

**The parse accepts both disassemblers' shapes, and that is not defensive style.** GNU
`arm-none-eabi-objdump` prints the instruction line directly after the `bl`:

```
800462d8:	bl	80045874 <FlushPoU_Dcache>
800462dc:	e30101a4 	movw	r0, #4516
```

`llvm-objdump` prints a **symbol** line first, and `0x`-prefixes the target:

```
800462d8:  bl  0x80045874 <FlushPoU_Dcache> @ imm = #-2668
800462dc <platform_cache_idle_exit+0x8>:
```

A rule that requires a colon after the address would find nothing under the second spelling and would
report that as an absence - the failure shape 549 records, a decoder that answers by printing nothing.
So the address is the first line after the call that begins with one, colon **optional**, and the
function's own entry address is excluded: a parse that returned the entry would compare the reader's
criterion against the top of the function.

**The test is scoped to the function, not the callee.** The image reaches `FlushPoU_Dcache` from four
`bl` sites; only the caller separates this seam from the other three. The count is *printed* from the
image rather than asserted, and the printed line names which callee it counted - because this
project's notes carried **six** for it, having listed the two `FlushPoC_Dcache` references in the same
breath as the `FlushPoU_Dcache` sites. Measured: four sites to `FlushPoU_Dcache` (`0x80045874`),
two to `FlushPoC_Dcache` (`0x80045828`), a different function.

**One zero is deliberately not printed as a count.** If the derived address was read out of a line the
census then failed to match, the two cannot both be true, so the second branch says the count *did not
fit this decoder's spelling and is NOT reported*. "No site found" and "the pattern did not match" are
different readings, and printing the second as the first is how a wrong number gets quoted as a fact.

## 3. Tested against seven states before it is spent, as this file's own norm requires

Nothing below touched a device or a build; each state is a fixture under the job's tmp directory,
with the gate's `$RUNNER`/`$ENTRY_ELF`/`$_GATE_OD` pointed at it:

| state | what it is | gate |
| --- | --- | --- |
| A | the tree as it stands | `ok`, rc 0, `0x800462dc` derived, `4 site(s)` |
| B | the runner's literal moved to `0x8004c0ff` | `REFUSING …` rc 1, the full message |
| C | the literal's line deleted | UNREAD + the stray-comment note (it found `lr: 0x800462dc`) |
| D | no ELF | UNREAD, the build-product text |
| E | a derived address forced to a misaligned value | UNREAD, the misalignment text |
| F | a faux `llvm-objdump` printing the symbol-line shape | the address is accepted, same value |
| G | a decoder whose `bl` line the parse matches but the census's spelling does not | `ok` on the address, and the count reported as *not reported* rather than as `0` |

Also run: `bash -n`; the whole gate with `--allow-xnu-entry`, **exit 0**; and `diff` against the
pre-change output, whose only differences are the three intended passages - item (4), the re-derived
exit-site line numbers (`:418/:430/:440` → `:629/:641/:651`, which moved because the peer edited the
runner, which is why 552 made the gate compute them), and the new six-line section.

**The first run of this clause ended the gate at 141, and the value was correct.** The `awk` stops at
the instruction it wants (`print; exit`), the `objdump` feeding it is still writing, `pipefail` turns
that SIGPIPE into the pipeline's status, and `set -e` reads it as a failure of the gate. Fixed with
`DERIVED=$(… | tr … || true)` and a comment saying why the `|| true` is load-bearing - the same shape
as the reader's own `keyval || true`. A pipeline that reports a *reader's* success as the *pipeline's*
failure is the same defect class as a check pointed where it cannot fail, inverted.

## 4. What is not changed, and the drift reported rather than patched

- **No artifact is touched.** `out/stage90/stage90-qcdt.img` and `out/stage90/xnu_arm_entry.bin` are
  the frozen pair and are not rebuilt; `./build.sh` was not run. The image's own SHA is printed two
  screens up by the gate and is unchanged.
- **The reader is not edited from here.** `run_and_capture.sh` is another session's lane; its
  derivation, its labelled fallback and its exit contract are read, not modified. The `six`-for-four
  count is reported to that file's owner with the measurement, not patched across the boundary.
- **The narration's other item (4)-era sentences are left alone.** The section still describes the
  spent 526 arm in places, and that is reported rather than rewritten - the same discipline as 554 §5.
- **This clause checks a value, not a behaviour.** It proves the reader's fallback and the image agree
  *today*; it cannot prove the reader derives at run time, which is only observable on a host with the
  ELF absent. That is the honest limit, and it is why the UNREAD branch names the human step instead
  of passing quietly.

## 5. Safety

No device action, no build, no write to storage - `fastboot boot` remains the only device verb this
phase uses and this change does not run it. The change is to a preflight check, and its worst case is
a refusal to proceed.
