# 632: the clause that decides the witness printed a false sentence when its own extractor missed

Rung 1b is half the witness the owed press is pre-registered against: `poll_timeout_ms >=
ENTRY_PARK_MIN_MS`, "the poll that came back is the *park* and not a stray ask". Its threshold is read
out of `entry_trace.c` **rather than written into the runner a second time**, and that is correct - a
second copy of that constant is this project's most repeated defect (593's class). This step is about
the two ways that read could fail, both measured.

## 1. The extractor required exactly one space in two places, and four ordinary spellings defeated it

The pattern, verbatim, was:

```sh
sed -n 's/^#define ENTRY_PARK_MIN_MS \([0-9][0-9]*\).*/\1/p' …
```

Measured against six spellings of the one line (isolated in a scratch directory on purpose: the real
`entry_trace.c` is a build input and must not be mutated):

| spelling in `entry_trace.c` | old `park_min` | file readable | the sentence the clause printed |
| --- | --- | --- | --- |
| `#define ENTRY_PARK_MIN_MS 1000` (today) | `1000` | yes | - |
| `#define ENTRY_PARK_MIN_MS   1000` (value column aligned) | **empty** | **yes** | `ENTRY_PARK_MIN_MS could not be read from <path>` - **false** |
| `#define ENTRY_PARK_MIN_MS\t1000` (tab) | **empty** | **yes** | same - **false** |
| `#define ENTRY_PARK_MIN_MS 1000 /* ms */` | `1000` | yes | - |
| `#define ENTRY_PARK_MIN_MS (1000u)` | **empty** | **yes** | same - **false** |
| `#define  ENTRY_PARK_MIN_MS 1000` (two spaces after `#define`) | **empty** | **yes** | same - **false** |

**Four of six emptied `park_min` on a file that reads fine.** A reformat - the kind of edit that looks
like nothing, and that a future hand aligning a value column would make without a second thought - would
have turned rung 1b from PASS/FAIL into UNREAD on the morning of a press, with nothing in the output
distinguishing a reformatted define from an absent one.

## 2. And the sentence was false in that case, which is the sharper half

`park_min` is empty in **two different states**, and the clause printed one sentence for both:

```
UNREAD  and whether it is the park is unread: ENTRY_PARK_MIN_MS could not be read from
        /mnt/data/mi4-ios6/stages/stage90/xnu_arm_boot/entry_trace.c, and the largest recorded
        timeout (2000) is not compared against a number written here
```

In the first state - the file is not readable - that is true. In the second - the file was read and the
expected form was not found in it - it names a failure of *reading* about a file the operator can open,
in which the definition is plainly visible. That is **615's class** (a refusal naming a fact about a
different thing: a whole refusal printed about `$PWD` when the claim was about a 597 KB file sitting
right there) arriving in the clause whose number decides the witness - and it is the same shape 629
found in the write gate's `--help`, where an error message answered a `--help`.

## 3. The repair: a tolerant extractor, and two sentences where there was one

```sh
if [[ -r $park_src ]]; then
  park_min=$(sed -n 's/^#[[:space:]]*define[[:space:]][[:space:]]*ENTRY_PARK_MIN_MS[[:space:]][[:space:]]*\([0-9][0-9]*\).*/\1/p' \
               "$park_src" | head -1 || true)
fi
```

`[[:space:]][[:space:]]*` in both gaps, so any run of spaces or tabs is accepted. Measured: the same
six spellings now read `1000` in **five**, and the sixth - `(1000u)` - is still refused, correctly,
because a parenthesised value is a genuinely different form and not a reformatting. The real file reads
`1000` both ways.

And the refusal is split, so each sentence is true of the state that prints it:

| state | sentence |
| --- | --- |
| the file could not be read at all | "`<path>` could not be **READ** at all, so this clause has no threshold to compare the largest recorded timeout (`<n>`) against - and it will not write a number here instead, because a second copy of `entry_trace.c`'s own constant is this project's most repeated defect" |
| the file was read and carries no readable definition | "`<path>` **IS readable** and carries no `#define ENTRY_PARK_MIN_MS <digits>` line this clause could read … **This is a statement about the definition's FORM and not about the file's presence** - open it and look: a reformat (a second space, a tab, a parenthesised value) is the likely cause, and 632 measured four such spellings that this clause used to refuse as if the file were unreadable" |

Both branches still set `verdict_ok=0`, so neither is a clearance - the repair changes what the operator
is *told*, not what the clause decides. **A threshold is still never invented here.**

## 4. The falsifications

| run | runner | result |
| --- | --- | --- |
| mutant A: the extraction yields nothing | `park_min=""` forced, one line | section A **20 ok / 0 failed**, section B **11 ok / 4 failed**, `REFUSING: the reader has a state it cannot read.`, exit 1 |
| mutant B: the source is unreadable | `park_src` pointed at a nonexistent file | (measured on a single log - see below) |
| the anchor | the repaired real runner | section A 20 / 0, section B 15 / 0, section C 4 / 0, exit 0 |

**And the guard is wider than one cell - wider than this step predicted when it started.** The four red
cells under mutant A are:

```
FAIL  predicted        the reader did not say: this arm did what it was built to do at the point that matters
FAIL  small-timeout    the reader did not say: the largest recorded timeout is 40 ms
FAIL  no-poll-over     the reader did not say: this arm did what it was built to do at the point that matters
FAIL  slot-cwe-only    the reader did not say: this arm did what it was built to do at the point that matters
```

The three arm-clause cells go red because `verdict_ok` is set to 0 by the UNREAD branch, so the whole
positive paragraph that depends on rung 1b disappears - the *proxy* 630 measured, doing useful work in
the other direction for once. And `small-timeout` goes red because the branch it asserts (`FAIL … below
the park's own threshold`) is unreachable when there is no threshold to be below. So a `park_min` that
cannot be read takes out **four** assertions, one of them the rung-1b PASS text itself and two of them
the ladder's own reading of the witness.

The two mutants were also measured on a single log with `--summarise` (no harness, seconds rather than
minutes), which is how the two sentences were confirmed reachable and true before the long runs:

* **mutant A** printed `… IS readable` / "carries no `#define ENTRY_PARK_MIN_MS <digits>` line this
  clause could read" / "the largest recorded timeout (2000) is not compared against a number written
  here".
* **mutant B** printed `… no-such-file.c could not be READ at all`.
* the real runner on the same log printed `PASS  and it is the park and not a stray ask: the largest
  recorded timeout is 2000 ms, at or above this tree's own park threshold, 1000 ms`, and **zero** UNREAD
  lines.

**So the structural check 604 asks for already exists, and this step measured it rather than adding
one.** The tempting move when this step began was to add a `tools/verify_park_min.sh`; the measurement
showed the guard was already in place one layer up, four cells wide - and that it is the same machinery
630 built for the ladder's success direction.

## 5. What this does not do

* **It does not touch the device, the arm, the payload or any parked byte.** `out/stage90/stage90-qcdt.img`
  is still the sleeper arm (`60063c47…`), still **UNRUN**. Verified in the same session, immediately
  before this step: the live arm is **byte-identical** (`cmp`) to the copy in the park at
  `out/stage90/frozen/armed-sleepless-696a0f39/`, both parks verify against the record
  (`--set=armed-sleepless-696a0f39` and `--set=frozen-574`, **11 files each**), and `IMAGE` resolves to
  that exact file (`OUT=$REPO_ROOT/out/stage90`, `IMAGE=$OUT/stage90-qcdt.img`). So the bytes the press
  will boot are the recorded bytes, checked rather than assumed.
* **It does not change what rung 1b decides.** PASS/FAIL/UNREAD are the same three branches with the same
  `verdict_ok` handling; only the extraction's tolerance and the two sentences changed.
* **It does not fix the assumption underneath.** Rung 1b compares the log's timeout against **this
  tree's** constant, which is an assumption about which build the log came from - stated in the clause's
  own comment, measured (`1000` at every commit that carries it), and out of scope here.
* **It does not make the extractor total.** `(1000u)` and any other genuinely different form still read
  as empty; the repair makes that state *legible* rather than impossible, which is the honest limit.
* **It does not advance 「起码要能进入操作系统，把基础驱动跑起来」.** The device is off the bus; the only
  event that can move the goal is the user's **Vol-Down + Power**.

## 6. Safety

No device action, no `fastboot`, no `adb`, **nothing written to storage**, no boot, no build. One file
edited (`stages/stage90/run_and_capture.sh`, this session's lane). The six spelling tests ran in a
scratch directory and never touched `entry_trace.c`. The harness runs go through the stub assertion
(`sudo`, `adb`, `fastboot` must resolve into the harness's own stub directory before any state runs), and
the `--summarise` runs read a fixture log and issue no device call at all. The runner's bytes were
asserted **identical before and after the batch** (`9b2b5ba0…` both times), so every number in section 4
belongs to one revision. `fastboot boot` only - never `flash` - so no outcome of any of this can write to
storage.

**One thing this step got wrong about its own hygiene, recorded because it is the second time in this
session.** The batch script that ran the two harness runs had **no cleanup trap**, so
`stages/stage90/.632-mut-empty.sh` was **still in the shared checkout** after the batch ended - an
uncommitted, unowned file in a tree two sessions work in, which is exactly what the peer flagged in 628.
It was caught by reading `git status` rather than by assuming the batch had tidied up, and removed by
hand; its hash at launch was
`53f7baa275ee68e58fcde4eeccae6b5d9b77d762943dbcba018deb8baa750a13`. The lesson is narrow and worth
keeping: a batch that asserts a *frozen input* does not thereby clean up its own outputs, and
"the trap removes it" was a belief about a trap this batch did not have. A mutant's reuse risk is not
only that a cell measures the wrong bytes - it is that the live path could fire them.
