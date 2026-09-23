# 596: the goal's own criterion had no reader, and `0x8000` was never where the machine died

Two findings, and they are the same shape from opposite directions. The first is a *reader* that was
missing: this project has measured 「起码要能进入操作系统，把基础驱动跑起来」 since 504, and
`grep -n 'xnu_live_open' run_and_capture.sh` returned **nothing** — so a run that survived the park
would still have been judged against the death point, with the goal's own evidence sitting unread in
the same file. The second is a *sentence* that was wrong: rung 0 called `0x8000` "where the machine
died", and 533's own capture shows the baseline running on past that record for the whole remainder
of the boot.

Neither is a device action and neither changes the arm. The frozen/sleepless payload is untouched:
no build, no `fastboot`, no `adb`, nothing written to storage. **TWRP stays withheld** —
「如果os已经能进去了的话」 is unmet until a boot says otherwise.

## 1. What the archived baseline already carries, and nobody had read

The reading is not new; it is 504's, and `entry_ramdisk.s`'s own header states it. What this step
adds is that the **archived pair confirms it is still true** and that the runner never said so.

Measured on `out/stage90/captures/533-2026-09-23-last_kmsg.txt`, and identical in shape in
520 (only the thread pointers differ), in the fixture's own record order:

| reading | 533's log | the fixture's own criterion |
| --- | --- | --- |
| `open` #1 | `error=0`, `fd=0`, `path=0x121c` | `open("/dev/rmd0")` — 504: "the first call in this image that a *driver* answers" |
| `read` | `fd=0`, `nbytes=4`, `error=0`, `ret_lo=4`, `word_before=0x00102000` → `word_after=0xfeedface` | the driver's `uiomove64` moved this file's own `MH_MAGIC` into the page `mmap` returned — and the page held that address a moment earlier, so the pair is self-checking |
| `open` #2 | `error=2` | the control: a name devfs has no node for, `ENOENT`, the same errno XNU's loader printed three lines earlier |
| `getpid` | `value=1`, `error=0` | pid 1's identity read back through the *Unix* dispatcher |
| `exit` | `pid=2`, `rval=3` | the forked child's death |
| `wait` | `done_seq=2`, `status=0x0300`, `error=0x0a` | pid 1 reaped it |
| `ast` | 2 records, the second `pid=1`, `sp=0x00101efc` | the kernel taking a *user* thread's AST and putting it back |

So the OS reached user mode, ran a program, and a character driver answered an `open` and a `read`
from it — in both archived runs. Everything the goal's first half asks for is in the log this phase
has been reading for days, and it was read as *context* for the death rather than as the goal.

## 2. The reader, placed so it cannot be silent for the log it is for

`summarise_log` scored the log entirely against the death point: the 594 arm's ladder, the baseline's
window family, the exit bracket. None of those reads a driver. So a block was added at the **end of
`summarise_log`, outside the arm branch**:

```sh
  # ... sits outside the arm branch on purpose: the userland phase precedes the pop, so these
  # keys are published by both arms and by every archived baseline ...
  local g_open_n g_read_n ... g_magic="" g_driver=0 g_pair=0
```

and it is deliberately *not* gated on the arm signature. The userland phase happens **before** the
`pop`, so both arms publish these keys and every archived baseline carries them; a block inside a
branch would be silent for exactly the logs most likely to matter. That is 564's defect and 594's
missing `else` one layer out — the same lesson, applied before rather than after it bit.

**The one state it does not run in, found by looking for it rather than by assuming it prints always:**
against a log with **zero** `MI4IOS6_STAGE90` lines it prints nothing, because `summarise_log` returns
at its own `n -eq 0` test with "NONE. The log has no payload output from this run". That is the one
silence here that is deliberate and named, and it is the right answer — with no payload output at all
there are no readings, goal included, and a table of `absent`s would be worse than the sentence already
on screen. The difference is testable and was tested: `syn-F` is the same shape with one payload line,
and the block prints its UNREAD there. Both facts are stated in the block's own comment, because a
future reader who tries the block against a truncated log and sees nothing would otherwise have to
rediscover which silence is which.

### The reader's own third helper, and why two were not enough

`keyval` takes the last occurrence and `maxhex` the largest; both collapse a key's history to one
value, which is right for a counter and wrong for a **pair of calls**. The fixture's driver reading
*is* a pair, and 504 states the criterion in its own words:

> a pair of opens whose second is also answered positively would mean the first told us nothing; a
> pair in which both fail would mean the path shape is wrong rather than the driver missing.

So `ordered()` was added beside its two siblings, returning a key's occurrences in publication order,
and the block reads `open_error` **as a pair** — `0x00000000`, then `0x00000002`. A reader that
reported one `xnu_live_open_error` would report the **control's** while claiming to report the
driver's, because the control is published second and `keyval` takes the last. That is 558's defect
("the number was right and the question was wrong") in a third place, and the fix is the same: ask
the question the pair asks.

**And that insertion order is a *measured* property, not an assumption** — it is the one thing this
reader rests on, so it is stated where the helper is defined. 533's capture runs `poll_seq=1`,
`door_seq=0x8000`, `poll_seq=2`, `read`, control `open`, `exit`, `wait`, `wait_done`, then the park's
`repair_seq=1`, `sip_seq=1`, `pce_seq=1`, `wfi_seq=1` and finally the fatal `sleh` — the fixture's own
program order (`+96`, `+116`, `+140`, `+156`, `+176`, `+188`, `+212`, `+232`, `+276`, `+300`) followed
by 593's causal chain, in one uninterrupted run at the end of the file.

The threshold is read out of the tree rather than written down again: the magic comes from
`entry_ramdisk.s`'s own `.equ MH_MAGIC`, the same rule rung 1b follows for `ENTRY_PARK_MIN_MS`. And
the read is scored as a *relation* — `ret_lo == nbytes` and `word_after == MH_MAGIC` — so neither
number is a literal here.

### That it is a floor, said where it prints

The block's PASS text says so in its own words:

> **And it is the floor, not this run's progress**: all of it is in 520 and 533 too, because the whole
> userland phase happens before the death. Losing it would be a regression; having it says only that
> the OS still boots to pid 1's syscalls.

and a closing banner names the ceiling:

> and this criterion does not decide whether the machine stayed up. Every reading above is a floor
> that 520 and 533 also meet; the ceiling is the arm's own clause, and the run that matters is the one
> whose log has both.

Without that sentence the block would be a new way to over-read a good run — a driver's `read`
returned and the report calls it a boot. It is the *conjunction* that is the reading.

## 3. The defect the new `say` strings had, found by `bash -n`

The block's first draft printed its FAIL branch with an apostrophe **inside a parameter expansion**:

```sh
    say "        as well ..., or the read did not leave"
    say "        ${g_magic:-the fixture's magic} in the buffer. ..."
```

`bash -n` refused the whole file: `line 1524: unexpected EOF while looking for matching '}'`. The
`'` in `fixture's` opens a quoted string *inside* the expansion word, the closing `}` is then inside
that quote and does not close the expansion, and the unmatched `{` is reported at the line that
opened it. **A default value in `${var:-...}` is not inside the double-quoted string the way the rest
of the argument is.** The fix is wording, not escaping — `${g_magic:-the magic the fixture defines}` —
because the expansion is doing the same job a written-down default would and the file's rule is that
a number is read out of the tree rather than restated.

This is the same class as 594's backtick defect one operator over: a `say` argument that bash parses
as something other than a string, in a sentence that still reads as though it said what it meant. The
backtick half is now structural (594's guard runs at script start and the file executes it on every
invocation, so the new lines were checked by it without being asked). The `${:-}` half has no such
guard and is checked only by `bash -n`, which is why every closure of this step was syntax-checked
before it was run.

## 4. The other half: `0x8000` is where the publisher last spoke

Rung 0's narration, and 594 §5's correction, and 595 §7's rung-0 row and §9(a) all said `0x8000` is
*where the machine died*. **The same log that supplied the number falsifies it.**

533's capture is one uninterrupted run in insertion order, and the `door_seq=0x8000` record sits at
line **8133** — after which the same file still carries:

```
8071  xnu_live_poll_seq=0x00000001
8133  xnu_live_door_seq=0x00008000
8141  xnu_live_poll_seq=0x00000002
8151  xnu_live_open_seq=0x00000001          (the driver's open)
8158  xnu_live_read_seq=0x00000001
8170  xnu_live_open_seq=0x00000002          (the control)
8186  xnu_live_wait_seq=0x00000001
8226  xnu_live_exit_seq=0x00000001
8294  xnu_live_wait_seq=0x00000002
8301  xnu_live_wait_done_seq=0x00000002
8307  xnu_live_repair_seq=0x00000001       (514's repair, from the park's poll)
8319  xnu_live_sip_seq=0x00000001          (SetIdlePop, TRUE - the window's first arrival)
8324  xnu_live_pce_seq=0x00000001
8345  xnu_live_wfi_seq=0x00000001
8363  xnu_live_sleh_pc=0x33f1c1b4          (the fatal abort)
8364  xnu_live_sleh_lr=0x800462dc
```

Every line number above is read out of 533's capture, not written from memory — this file has been
corrected once already for a line number that had moved.

So both baselines **lived on past `0x8000` for the whole remainder of the boot** and died somewhere
in `[32768, 65536)` — below the next power of two, which is why neither published `0x10000`.

**The test does not change.** Reaching `0x10000` passes is still something no baseline run did, and
that is exactly what rung 0 measures. What changed is what the record is called: a reader sent to
"where the machine died" is sent to the wrong record, and the sentence was the kind this project
keeps paying for — a claim about an artifact that the artifact itself contradicts.

Six sites carried it, in three files, and `grep -rn 'where the machine died' docs/ stages/stage90/`
is the whole enumeration: two in `run_and_capture.sh` (the comment and the PASS text), one in 594,
two in 595 (the rung-0 row and §9(a)). **The sweep was by predicate, not by provenance** — 560's
lesson, and the reason 595 §9(a)'s row was found at all is that the grep did not care which step had
written it.

## 5. Verified on eight states, and what each one is

The new block was run against every log this phase has, plus two synthetics built for it:

| state | what it must say | said |
| --- | --- | --- |
| 533's capture (baseline, 594's reader) | goal block PASS with the pair and the magic | PASS — `0x00000000 0x00000002`, `word_after=0xfeedface`, `MH_MAGIC` read from the fixture as `0xfeedface` |
| 520's capture (baseline, no `slot_cwe_`) | the same PASS, **from a log that lands in the `else`** | PASS — which is the point of not gating it |
| `syn-A` (sleepless arm survived, rungs 0/1/1b/2/3 pass) | goal block UNREAD — a *shape*, carrying no syscalls | UNREAD |
| `syn-B` (died at 32768) | rung 0 FAIL with the corrected wording | FAIL, "stops at or below 0x8000, the last record 520 and 533 publish" |
| `syn-C` (survived, no thread progress) | rung 0 PASS, rung 1 FAIL | as specified |
| `syn-D` (no `door_seq`) | the unclassifiable-state UNREAD | UNREAD |
| `syn-E` (**new**: control open also answered 0, read word unchanged) | goal block FAIL, naming 504's "the first told us nothing" | FAIL |
| `syn-F` (**new**: survived but truncated before userland) | goal block UNREAD | UNREAD |
| `syn-G` (**new**: survived *and* the fixture's syscalls) | goal block PASS **beside** the arm's PASS — the conjunction the report asks for | PASS, both |

The synthetics are shapes and are labelled as such; 520 and 533 are artifacts. The pair of them is
what makes the block's two directions testable — a log that has the arm's reading without the goal's,
and a log that has the goal's without the arm's — and neither direction is producible from the
archived pair alone.

## 6. What this does not do

* It does not make the OS boot. It makes the runner *say* how far the OS got, which the goal asks for
  in its own words and which the report did not do before.
* It does not change the arm, the payload, the gate, or any prediction. `60063c47…` is still the
  image the next power press sends, and rung 0 and rung 1 still decide it.
* It does not decide 「能进入操作系统」. The block reads a *floor*; the ceiling is that the machine
  stays up, and that is still the idle exit's `pop`.
* It does not route a baseline-signature log into clauses (1)–(5). 595 §9(b) left that as its own
  step and it was still owed *when this step ran*; **598 landed it**, so the file's clause block now
  reads a 520-shaped log (112 lines → 151) and this sentence is the state 596 left rather than the
  state the runner is in.

## 7. Safety

No device action, no `fastboot`, no `adb`, **nothing written to storage**, no boot, no build. Reads of
two archived captures, reads of `entry_ramdisk.s` and `entry_trace.c`, and edits to
`run_and_capture.sh` — which is host-side and is not a manifest *entry* in `xnu_arm_entry-sources.txt` (that manifest
covers `xnu_arm_boot/` by content; the runner lives in `stages/stage90/`, where the gate's freshness
scan deliberately does not match `*.sh`). `fastboot boot` only — never `flash` — so no outcome of any
of this can write to storage, and the frozen 574 arm is unchanged and unrun.
