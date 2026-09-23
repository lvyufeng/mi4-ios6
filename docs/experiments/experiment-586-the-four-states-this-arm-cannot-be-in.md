# 586: the four states this arm cannot be in, and the silence that read as agreement

583 built a four-row table on `xnu_live_slot_pre_m4` and told the coming run it would be measured in
**two independent keys**. It will not be measured in that key at all. The frozen 574 arm's own config says
`STAGE90_XNU_SLOT_NULL=1`, and every arm in that state calls `entry_slot_null_note`, which publishes the
pass count and **neither `sp` nor the four words** - so the reader's whole `pre_m8`/`pre_m4` block, and the
`slot_pre_sp` comparison above it, print **nothing** on the arm that is about to run, and nothing in a
block where every neighbouring absence prints a line reads exactly like agreement.

Found by the peer session (`run-experiment-526`), who measured it by execution rather than by reading and
made no edit to this repository's runner. Verified here from the artifacts, then repaired in two places:
the reader now says the absence out loud, and the gate says the run will say it.

This is a reader-and-gate change plus a document correction. No device action, no build, no arm: the entry
image is still the frozen 574 arm (`151425c4…`, `914f45ac…`), the gate is green on it (`EXIT=0`, 0 UNREAD),
and the phone is off the bus.

## 1. `SLOT_NULL=1` is a fact of the image, in five places

| # | source | what it says |
| --- | --- | --- |
| 1 | `out/stage90/xnu_arm_entry-config.txt:8` | `STAGE90_XNU_SLOT_NULL=1` - and the same line in `533-…-config.txt` and `535-…-config.txt`, so this is not one odd arm |
| 2 | `entry_stubs.c`, `entry_slot_null_note` | "What it deliberately does not do is publish the four words, or `sp`"; its body writes `k->calls` and the live key `k->k_calls` and nothing else |
| 3 | the wrapper's disassembly | `8047c978: bl 8000766c <entry_slot_null_note>` - the **null** note, in the artifact, at the very site that would publish the words |
| 4 | 533's real capture | `xnu_live_slot_pre_calls` **present**; `slot_pre_sp`, `_pre_m16`, `_pre_m12`, `_pre_m8`, `_pre_m4` **all absent** |
| 5 | 520's real capture, for contrast | all seven present - that arm is `SLOT_NULL=0` |

So 520's log is not the shape of a 574 run, and the four-state rehearsal built on it (583 §1) is a
statement about an instrument this image does not carry.

## 2. What the reader did, and why it was the defect

Two comparisons read those keys, and both were guarded on them being present:

```sh
if [[ -n $slot_pre_sp && -n $seam_sp ]]; then … else … fi      # no else - prints nothing
if [[ -n $seam_m8 && -n $seam_m4 ]]; then … fi                 # no else - prints nothing
```

Measured on a 533-shaped input with the seam keys present - i.e. exactly what the coming boot will leave -
the seam section reads: header, the two words, the op line, `PASS seam_lr=…`, `UNREAD xnu_live_seam_other
is absent…`, `PASS seam_sp=… is the exit's frame slot`, and then **straight to** `PASS b1=0x8047c990`.
Nothing between. The line one above that silence is an UNREAD *for an absent key*, so the file already
knows the shape; the two capture comparisons were the exception, and the arm publishes the discriminator
that distinguishes the two reasons: `slot_pre_calls` present with the words absent means the site ran and
published no words, both absent means the site did not run.

**And the block above had the mirror of the defect the `b1` line below records.** 582's fix put the
"could not derive" case first in the `b1` line because its fallback printed a comparison that had not
happened. The `m8`/`m4` block, written after that fix, tested the derived comparison first - so with no
readable entry ELF the empty `caller_lr` fell through to the fallback and printed
`m4=0x8047c990 - equal to b0/b1 when the frame did not move between two passes` **one line above** the
`b1` line's own `UNREAD … the return site could not be derived`. Two lines in one block, asserting
incompatible things about whether the comparison happened. Fixed the same way: the empty-derivation test
runs first.

## 3. The four states, and the two that replaced the silence

The block now has six reachable outcomes, all rehearsed (`586-pair-six-states.txt`):

| case | `b1` | `pre_m4` | what the reader printed |
| --- | --- | --- | --- |
| `both` | `0x8047c990` | `0x8047c990` | **PASS** - in memory and the frame did not move |
| `moved` | `0x8047c990` | `0x8047c974` | the frame **MOVED** between two passes |
| `stale` | `0x8047c990` | `0x80553520` | same - `b1` is right, so the pre-push word is a different pass's |
| `wrong` | `0x80017330` | `0x8047c990` | **READING** - right before the push, wrong after it: the window, not the loop |
| `neither` | `0x80017330` | `0x80553520` | the pre-push word is not comparable, and `b1` is not the site |
| no decoder | any | any | **UNREAD** - and now so is the block above it |

The `wrong` row is new in this step and is the one state 583's three-row table could not name: the word at
the slot held the derived site *before* this pass's push and does not hold it *after*, which is 546 §1's
premise failing in the window seen from the side that needs no assumption about where the previous pass's
frame was.

And on the frozen arm's own shape, where none of those six can fire, the two absence lines do
(`586-armshape-summary.txt`):

```
UNREAD  xnu_live_slot_pre_calls=0x00000001 is published and xnu_live_slot_pre_sp is not:
      this arm takes the SLOT_NULL path (STAGE90_XNU_SLOT_NULL=1 - the wrapper calls
      entry_slot_null_note, which writes the count and no words), so the wrapper's own
      capture cannot be compared with the seam's sp on this boot. …
UNREAD  m8/m4 are the second half of the same absent capture (xnu_live_slot_pre_calls=
      0x00000001 says the wrapper's site ran; SLOT_NULL says it publishes no words), so on
      this boot b1's address has only the seam's own reading behind it …
```

Two lines where there was a gap, each naming the reason, and each sitting between the `seam_sp` PASS and
the `b1` PASS that used to be adjacent. The rehearsal input is `586-armshape-SYNTHETIC.txt` - **533's real
capture unmodified as the body**, with only the eleven `xnu_live_seam_*` lines invented (533 predates the
seam), labelled as such; the `SLOT_NULL` shape is native to it rather than simulated by deleting keys.

## 4. What this costs the coming run's reading

**582's `b1 = 0x8047c990` prediction stands, and it is now the only one.** 583's second key is not
reachable on this arm, so the run's verdict on the frame rests on one word rather than two, and the
pre-registration has to say so. 583 §1 is corrected in place with the correction block, and its title and
header no longer promise the second key.

**The verdict's shape does not change**: the `b1` line is unaffected by any of this, the exit's one-caller
census (583 §3) is unaffected, and the `pre_m4` cross-check was never load-bearing for the prediction - it
was a second reading of the same word, and the arm does not publish it.

## 5. The gate now says the run will say it, and derives that from the file

A claim about another file's mechanics is a claim about that file's future (563), so the gate's
`SLOT_NULL=1` branch does not assert what the runner prints - it greps the runner for the key name and
prints one of two sentences on the answer. Both branches were exercised
(`586-gate-clause-two-branches.txt`): on the tree it prints "So a run of this arm prints two UNREAD lines
for the slot's own capture, naming `xnu_live_slot_pre_calls` - that is this switch and not a disagreement",
and on a symlink farm whose runner has that string renamed it prints "**… does not name
`xnu_live_slot_pre_calls` anywhere**, so the summary of a run of this arm says NOTHING about the slot's own
capture … Fix the reader before spending a boot on this arm". The farm exists because the gate sets
`STAGE_DIR=$PWD`; nothing in the repository was touched to run it.

## 6. Regression, and safety

- **The six states of section 4 / step 5, re-run against the final reader**: `fail 1`, `silent 1`,
  `fall 2`, `none 2`, `late3 3`, `late2 3` - identical to 582's and 584's recordings, code for code
  (`586-six-states-and-baseline.txt`).
- **520's real log: exit 0, `0 FAIL`, `0 UNREAD`** - unchanged; its image is `SLOT_NULL=0`, so the new
  absence branches cannot fire on it, and 564's gating keeps the seam block out of that image by design.
- **The gate is green**: `EXIT=0`, 23 sections, 0 UNREAD (`586-gate-green.txt`).
- No device action, no `fastboot`, no `adb`, nothing written to storage, no build input touched: two
  reader edits, one gate clause, five rehearsals in `/tmp` and one in `out/stage90/captures/586-rehearsal/`,
  and two `--summarise` reads of logs already on disk. **The frozen arm is byte-identical and unrun.**

**The boot still waits only on the user's power press.** TWRP stays withheld:
「如果os已经能进去了的话」 is unmet.
