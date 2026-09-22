# 567: the two keys the stop is named by are the two that cannot be read

A host-side change to `stages/stage90/preflight_boot_check.sh` only. No device, no build, nothing
under `out/` read or written, no file outside this one and the gate. The frozen pair is untouched:
`1daaf44e624563694e…` (boot image) / `f202f2465886aba6…` (entry image), and the gate runs
`--allow-xnu-entry` to **exit 0**.

It exists because of a mistake of mine, and the mistake is worth more than the clause: **the death's
reason is not absent from the phase's only capture. It is there twice, and both copies are keys that
cannot be read.** I had read the capture as carrying no reason at all, and the reading that produced
that sentence was a grep for the reason *strings*. The keys published for exactly this case
(`xnu_entry_why`, `xnu_entry_why_byte`) were in the file the whole time. The gate's reader clauses
named `xnu_live_sleh_lr`, `EXIT_POP_LR_LITERAL`, 547 §4's bracket and the arm's five variant keys -
and never these two - so nothing in the phase's instrument ever sent a reader to them.

## 1. The capture's terminal report block, in full

`/tmp/cancro-last_kmsg.txt`, `596665 bytes`, mtime `2026-09-22 02:55:16`,
sha256 `f0285b0f6e22eb0a…` (unchanged before and after this work; it is the phase's only capture and is
not reproducible). Its report ends at line 8374:

```
MI4IOS6_STAGE90_XNU real XNU entry: 47      <- the epilogue's preamble, printing g_why
 xnu_entry_kv_written=0x00001fea            <- 8164 bytes the probes recorded
 xnu_entry_kv_in_dram=0x00000000
 xnu_entry_kv_dropped=0x000092b3            <- 37555 records the 8192-byte buffer refused
 xnu_entry_why=0x00000000
 xnu_entry_why_byte=0x00000034
 xnu_entry_stub_caller_v=0x00000000
 xnu_entry_stub_caller_digits=0x00000000
 xnu_entry_stub_hit_count=0x00000000
```

and every `xnu_entry_abort_*` key after it is zero too. The header line has **no leading space** and
the key lines all do - the ram console's prose and its ` key=0x%08x` records share one writer and one
size field, and the two shapes are distinguishable at a glance. That detail is not cosmetic: it is the
reason a grep anchored at `xnu_entry_why` returns nothing on a file that contains it, which is defect
(b) in §5.

Two facts about this block make it a *reading* and not a fragment:

- **The header printed once, not twice.** The heading is written at two sites in the fixture - the
  preamble above, and again immediately before the results buffer - so a complete report carries the
  literal twice. This file carries it once, and that is the buffer's absence explained in §4.
- **`xnu_entry_kv_written = 0x1fea` while `xnu_entry_kv_in_dram = 0`.** The fixture's own note on the
  pair says what that is for: `_written` "is what the probes recorded, read from a register, so it is
  right even if the transfer to DRAM was not", `_in_dram` "is what the transfer actually produced -
  they differ only when it failed". **They differ here**, so this is a report whose small-`.bss`
  family is the failure and whose register-held fields are the readings. That is 272's account, and
  272 wrote the rule this block is read by: "a field is only as good as its second road."

## 2. What the two keys say, and why they are two roads to one bad read

`xnu_entry_why` is `(uint32_t)(uintptr_t)g_why` - the pointer's value. `xnu_entry_why_byte` is
`(uint32_t)(uint8_t)g_why[0]` - the first byte at where it points. In this capture those are `0` and
`0x34`, and `0x34` is `'4'`, **the first character of the two characters the header printed**. So the
header's read and the byte's read are the same read through the same bad pointer, and neither is
independent evidence about the other.

The header line is the fixture's own known artifact, recorded in its note on `g_why` long before this
run, and quoting it is the shortest honest way to say what happened:

> What the run says is that the *slot* is wrong by the time it is read: the line comes out as
> `MI4IOS6_STAGE90_XNU real XNU entry: ` with nothing after it, i.e. a pointer to a nul byte, **and in
> the storm run the same line read `47`**. Both are what a clobbered stack slot looks like, and neither
> is what the string says. So the report uses this copy, taken before anything is torn down, and prints
> the pointer itself beside it - **a value in `.bss` cannot be clobbered by whatever is going on with
> the stack**, and the pair (pointer, text) says which of the two readings the next run should trust.

The `.bss` copy `g_why` exists *because* the parameter's slot was untrustworthy, and its stated purpose
is the pair this clause reads. In the 2026-09-22 capture **both halves of the pair are wrong together**
- `47` and `0x34`/`0` - which is the case 272 describes and not a new failure mode: the copy is in the
same small-`.bss` family the pair's `_in_dram` has already convicted. The two characters are not a
reason string, and the byte is not a telegraph:

- 340's rule for `why_byte` is that it "is the first byte of the `why` string, which makes it a
  one-character telegraph" of the kind of stop (`0x61` = 'a', a stub; `0x65` = 'e', an exception).
- The first bytes of this image's `why` strings are **`5f 61 65 70`** - `_start ran to completion…`,
  `a symbol this image does not provide was called`, `exception: …`, `panic() - XNU rejected
  something` - and that set is *derived from the fixture* by the gate now, not written down here.
- `0x34` is in none of them, so the rule does not apply to this capture and the byte classifies
  nothing. **The honest reading is "the reason is unreadable", and it is a different statement from
  "there is no reason in the log"** - the one I made, from a grep for the strings.

## 3. Why this matters beyond one capture

`entry_epilogue` publishes this pair **unconditionally**, in the same function that writes the header,
so there is no run in which the keys are missing *because nothing happened*: their absence means the
lines did not reach DRAM, and their presence with a bad value means the read did not. Both readings are
useful. Losing them by not looking is not - and the phase came within one sentence of doing exactly
that: the goal's own question ("what is the stop's reason?") was being answered from a grep over reason
strings, in a file that carries the answer twice in the form the fixture built for it.

The second-order point is the one this project keeps re-learning: **the keys published for a failure
case are the ones nobody looks at**, because the failure case is rare and the reader's habits form on
the common one. The gate now names them ([[mi4-a-claim-in-a-comment-is-not-a-check]] is the general
form: a register of what to read is only as good as the fields it names).

## 4. The results buffer's absence is "not dumped", which is not "empty"

The heading count above is not decoration. The fixture's own doctrine, written for the *trap record's*
buffer, is that printing each buffer under its own heading is "also what makes the **absence** of one
of them readable". The results buffer is a counter-example inside the same function: its heading is
guarded on `if (g_kv_len != 0u)` - **the same small `.bss` global that `xnu_entry_kv_in_dram`
publishes**. So a failed transfer suppresses the buffer *and* the evidence of the failure with it, and
the two absences (never-recorded vs never-dumped) become indistinguishable in the printed report.

What the reader gets instead is the register-held `xnu_entry_kv_written`, which does not go through
`.bss` at all, and it says `0x1fea`. So in this file: **the probes recorded 8164 bytes, the buffer was
not printed, and `g_kv_len` - the guard - reads 0.** The buffer's contents are unknown; its emptiness
is not established. That distinction is what the clause prints, because "the buffer was empty" is the
reading that would make someone conclude no probes ran.

## 5. What the gate does now, and the three defects in my first version of it

Inserted after the log-bracket block, before the reader-address clause: one printed block, **no new
refusal** (the state it describes - the predicted bracket in the parked file - is the normal state of
this tree, and a refusal would stop an armed boot over a *reading*). It derives every number rather
than pinning it:

| printed | derived from | if it cannot be derived |
| --- | --- | --- |
| what each key is published from - a `.bss` global or a register-held local | the fixture's own call: `entry_write_kv("<key>", <arg>)`, and whether `<arg>` names a `g_` global | the key prints `NOT FOUND in this fixture` |
| the first bytes of the image's `why` strings | every `entry_epilogue("<c>` literal in the fixture, sorted, hex | `UNREAD` - and it says the reader must read them out of the image by hand |
| the pair's verdict | `_written` / `_in_dram`, from `$LOG` at gate time | "the pair is not here in full" |
| the report's completeness - `occ < sites` means the second heading was skipped | the fixture's `entry_write(...)` sites for that literal, against the occurrence count in `$LOG` | `UNREAD` |

Three defects in the first draft, all found by **running the branches rather than the default** (563's
lesson, and 562's backtick bug is why it is a rule):

- **(a) A completeness verdict on a file with no report.** `occurrences < sites` fired on a log
  carrying no report at all, and printed the loud "** So the buffer is not empty in this file, it was
  not dumped" over it - a claim about a file with no buffer, no heading and no run. This is 543/552's
  defect exactly: a count whose scope is unstated, used as a broader claim than it was measured for.
  Repaired with a `_hasreport` guard derived from the four keys, and the no-report state now says the
  completeness check "says nothing about the run that wrote it".
- **(b) A sentence that contradicted the two lines under it.** The pair-absent branch said "Nothing in
  it is classified by family below", and the very next two lines classified the *fixture's* sources -
  true, but a different claim than the sentence denied. Repaired by naming whose sources those are.
- **(c) A `sed` that replaced one space.** The hex byte set printed as `5f-61 65 70`: `s/ /-/` without
  `g`. The membership test still worked (a two-hex-character needle can only lie inside a byte group),
  but the display was a lie about the set's shape, and a display that is *nearly* right is how a set
  that is actually wrong survives. Now `s/ /-/g`, and the set prints `5f-61-65-70`.
- **(d) An edit anchored at a line's *prefix* deleted the line's remainder - and the line was not mine.**
  Landing the row above, the anchor was the opening of the 549 row, and that row is a **single
  physical line** ~2.5 kB long in a table where one record = one line. Everything after the anchor was
  in neither `old_string` nor `new_string`, so it was deleted, and the row survived only as an
  orphaned tail beginning ` — a host-side reading (no device, no build)…` - a fresh instance of the
  class this project keeps paying for, with the difference that here the *instrument* was the edit
  itself. It also read as clean: `README.md | 3 ++-` looks like a one-line row landing. **Repaired by
  rebuilding the file as `HEAD` + exactly one inserted line** rather than by patching the wound, and
  proved by `diff HEAD → new` being **one added line, 0 removed**, and by the added line being
  byte-identical to the row the tree already carried. The general form: when a record is one long
  line, an edit must be anchored on the **whole line**, or on a boundary the edit re-emits - a prefix
  anchor plus a replacement is a deletion of the suffix, whatever the intent.

**The one defect in the reading itself, and it is mine:** a grep for `xnu_entry_why=0x00000000` with
the pattern anchored at `xnu_entry_why` returns **nothing**, because every key line in the ram console
carries a leading space. That is the second instance this session of the class
[[mi4-measurement-defects]] collects - "before concluding a value is absent, establish that the
extractor could have seen it" - and it is the same shape as 552's prose-counting grep. The first
instance is §1's mistake: concluding the reason was absent from a grep over the reason strings.

## 6. Measurement

- `bash -n` clean.
- The whole gate with `--allow-xnu-entry`: **exit 0** in the default state.
- **Seven states exercised**, five of them through `LOGFILE=` overrides over synthetic files under the
  job's `tmp/`: no report at all; a report with `why` but no pair; the pair agreeing with a byte in
  the image's set; the pair agreeing with a byte outside it; both sides zero; a complete report (both
  headings, 2 = 2, printing "the results buffer is in this file"); and the real capture, which is the
  `THE PAIR DISAGREES` branch. The complete-report state is the negative control: without it, a test
  that always fired would be indistinguishable from one that discriminates.
- A comment-only edit after the first run was proved by `diff` of the gate's output: **identical**.
  (The project's own rule for a comment-only change - build and compare - applied to a gate instead of
  a linker.)
- The real `/tmp/cancro-last_kmsg.txt` was **read and never written**, `596665 bytes`, sha256
  `f0285b0f…` identical before and after.
- The working tree's whole diff, `git diff --numstat`: `stages/stage90/preflight_boot_check.sh` **154
  insertions, 0 deletions** (the clause), `docs/experiments/README.md` **1 insertion, 0 deletions**
  (this document's row, after the repair in §5(d) - the intermediate state read `2 ++-`, which is
  what made the deleted 549 row visible), plus this file as the one new, untracked path.
- **Not touched**: `run_and_capture.sh`, everything under `xnu_arm_boot/`, everything under `out/`, any
  criterion (547 §4's bracket, the arm's variant keys, `READ_CODES`), and no new code value.
- No device action, no build, no `flash`, no `adb`, no `fastboot`. **533 remains the frozen, UNRUN arm
  and still needs the physical power press** - Vol-Down + Power to hold the phone in fastboot - and
  this change does not run it. TWRP-to-storage stays withheld: 「如果os已经能进去了的话」 is unmet.
