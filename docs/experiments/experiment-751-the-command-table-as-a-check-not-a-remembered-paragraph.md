# 751: 749's correction becomes a check — the ladder's per-command table, printed from the capture with the CMD line bit decoded

**Host-side only. No device action of any kind** — no `fastboot`, no `adb`, no press, no gate, no runner.
Nothing under `src/`, `scripts/` or `out/` is touched, and `armed-storage-46fe6737` sits exactly as it was
left: **ARMED AND NOT PRESSED.**

**The one-line finding, and it is about a correction rather than a device.** 749 found that
`inhibit_seen = 0` has **two producers** and that 747 §7's branch 1 — **rung 21's whole reading** — is
satisfied by both. Its repair was a paragraph in a document: *a reader has to remember it at press time.*
**The project's own rule is that a claim in a comment is not a check.** `tools/read_storage_commands.py`
prints the table instead, from the capture, and on the five archived command-carrying captures it
**reproduces 749's finding independently** — CMD1's CMD line LOW in all five, CMD2's HIGH in all four that
carry it, from the same `inhibit_seen = 0`.

## 0. Why a tool and not another paragraph

749 §4's refinement is four conjuncts: branch 1 requires `_nidx_inhibit_seen = 0` **and**
`_nidx_resp_moved = 0` **and** bit 24 set in both `_nidx_inhibit_last` and `_nidx_ps_after`. Written as
prose, that is a sentence a reader reconstructs from a `grep` at the moment of a press — which is exactly
the arrangement 747 §7 was, and §7 is the thing that was wrong. The project has paid for this shape
repeatedly and names it directly: *prefer a check that stops the build over a true sentence in a comment*
([[mi4-a-claim-in-a-comment-is-not-a-check]]).

**And the reading is mechanical**, which is what makes it worth a tool rather than a habit: every input it
needs is already a published cell, so nothing here is an inference the capture does not license.

## 1. What the tool prints, and the two columns that were being dropped

Per command family present in the capture: `op`, `word`, `sent`, `inhibit_after`, `inhibit_seen`,
**`inhibit_last` and its bit 24**, **`ps_before` and `ps_after` and their bit 24**, `polls`, `status_any`,
`complete`. Then a **shape** per row, then two blocks that the hand-built tables never carried.

**Bit 24 is the whole of the first repair.** `inhibit_last` is published as the **whole
`PRESENT_STATE 0x24` word** (`r->inhibit_last = st_read32(... ST_SDHCI_PRESENT_STATE)`, the sampler inside
`st_send_command`'s poll), and every table written from it decoded **bit 0 alone**. Bit 24 is the SDHCI
spec's *CMD Line Signal Level* — a second producer of the row's meaning, sitting unused inside a number
the record had already printed. **A decode that keeps one bit throws away the rest of the read, and the
read cost the same either way** (`m760`).

**The exit kind is the second thing the hand-built tables never carried, and it decides comparability.**
`inhibit_last` is **one sample**. On a row whose poll exited on `INT_STATUS` that sample is the moment of
the **completion**; on a row that ran the whole bound it is inside a still-open transmission. So the tool
prints, per row, *which exit it took* — and only bounded rows may be compared with each other. 749 rested
on exactly this (CMD1 against CMD2, both bounded) and said so; a reader rebuilding the table by hand had
no line telling them which rows were like.

**And `UNREAD` is not `0`.** An absent cell prints `UNREAD`. That is `m720`'s discipline — *an absent key
has more than one producer* — applied to a reader, because a tool that defaulted an absent
`inhibit_seen` to zero would be inventing the reading it claims to report. The rows that show it are real:
CMD2's capture carries no `inhibit_after` and no `ps_before` (the gap 746 §4b already named), and rung 18's
`rb` prefix is a read-only body that publishes no `sent` at all and is labelled *not a command body* rather
than being classified as a command that did nothing.

## 2. What it refuses to print, and that is the point

**It prints no verdict for an ambiguous row.** For `inhibit_seen = 0` it prints one of two **shapes**:

| shape | the reading it is derived from |
| --- | --- |
| `LINE MOVED INSIDE THE WINDOW  (the CMD1 shape)` | the CMD line is LOW in `inhibit_last` or in `ps_before`/`ps_after` |
| `LINE NEVER MOVED  (the CMD2 shape)` | the CMD line is HIGH at every published moment and the word was in the register |

and for the first it appends, in the same line: *749 records that the archive holds **two candidate
readings** of this shape and that they disagree.* **It cannot print "declined"**, because 749 established
that the archive does not license it — 733 and 740 read CMD1 as *the card answered* and 746 §4b read it as
*NEVER STARTED*, and neither cites the other. **The tool therefore refuses to do the thing 747 §7 did.**

## 3. The verification, which is the substance: it reproduces 749 from the archive

Run over the five captures that carry commands, the CMD-line column is flat and the split is total:

| capture | `cmd0` | **`cmd1`** | `cid` | `rca` | `nrsp` |
| --- | --- | --- | --- | --- | --- |
| rung 15 (the first with CMD1) | HIGH | **LOW** | — | — | — |
| rung 17 (the first with CMD2) | HIGH | **LOW** | HIGH | — | — |
| rung 18 | HIGH | **LOW** | HIGH | — | — |
| rung 19 (CMD3 R1) | HIGH | **LOW** | HIGH | HIGH | — |
| rung 20 (CMD3 no-response) | HIGH | **LOW** | HIGH | — | HIGH |

**`cmd1:LOW` in five independent boots and `cid:HIGH` in four, out of the same `inhibit_seen = 0`** — 749
§2's table, derived here by a tool rather than by four hand searches, and the `0x00f80000` value it rests
on is the same one 749 found by `grep`.

**And the shapes come out as 749 says**, in rung 19's and rung 20's own logs: CMD0 and CMD3-no-response
`TAKEN -> COMPLETED`; rung 19's CMD3-R1 `TAKEN -> STUCK AT THE BOUND` (`inhibit_seen = 0x400`, the inhibit
bit still SET at the last sample — 1024 of 1024); CMD1 `LINE MOVED INSIDE THE WINDOW`; CMD2
`LINE NEVER MOVED`. The exit-kind block shows CMD0 and CMD3-no-response **not comparable** with the
bounded rows, which is §1's rule applied to the same numbers.

**It also agrees with the record on the thing that is *not* 749**: CMD0's `polls = 0x21a` and CMD3's
`0x21b` against CMD1's `0x004db000` and CMD2's `0x004dac00`, and `_rb_resp_zero`'s row — the pre-command
read body — labelled *not a command body*.

## 4. The sweep, including every capture where it refuses

The tool was run over **all 30 archived captures**, in tree and in the subdirectories:

| | captures | what it printed |
| --- | --- | --- |
| exit 0, a storage family present | **25** | the table and the shapes |
| exit 1, `no xnu_live_storage_* cell` | **5** — 520, 533, 650, 677, 684 | the honest refusal: these predate the storage line |

**The refusal is deliberate and is the m720 shape**: a reader that printed an empty table for a non-storage
log would be telling its caller that the block said nothing, when the truth is that the question does not
apply to that capture. The exit status distinguishes them; the message names which.

## 5. What this does and does not change about rung 21

**It does not move the arm, the payload, `out/`, or the record.** No rebuild, no re-park, no re-hash — the
parked arm's identity is its entry image's hash and nothing here is compiled.

**What it changes is the next press's reading.** When `armed-storage-46fe6737` is pressed, the run's
`_nidx_*` cells give the tool a seventh family for free, and the four conjuncts of 749 §4 become a printed
shape instead of a remembered paragraph:

    _nidx_inhibit_seen = 0  AND  _nidx_resp_moved = 0  AND  bit24(_nidx_inhibit_last) SET
                                                        AND  bit24(_nidx_ps_after) SET
        -> LINE NEVER MOVED  (the CMD2 shape)  -- the shape 747 section 7 called "the rule is CONFIRMED"

    bit24 CLEAR anywhere, or _nidx_resp_moved = 1
        -> LINE MOVED INSIDE THE WINDOW  (the CMD1 shape)  -- a DIFFERENT outcome 746b merged,
           whose subject is 730's INT_ENABLE mask and not the INDEX bit

**And the tool names both, in the same words, for CMD1 and CMD2 today** — so a reader of the rung-21 log
sees the new row in a table whose other rows are already labelled, rather than deciding in the abstract
what "declined" was supposed to mean.

## 6. What this document does not say

- **It does not say CMD1 was answered, or that it was declined.** §2 is explicit: the tool prints shapes
  and refuses the verdict, for 749 §3's reason. Nothing here takes the side 749 declined to take.
- **It does not say the ladder's five-row table was wrong.** 746 §4b's rows are right about what
  `inhibit_seen` and `inhibit_after` are; what was wrong was the **merge** of two shapes under one name,
  and §3 shows the tool naming both from the same bytes.
- **It does not arm, move, press, or rebuild anything.** `armed-storage-46fe6737` is **ARMED AND NOT
  PRESSED**, no firer is armed, `out/` is untouched, and **no press may be spent without the operator's
  authorization.**
- **It does not advance 「挂载存储」.** No storage, no filesystem, no mount. It makes the next press's
  reading mechanical instead of remembered, and that is the whole of what it claims.

**The goal is still NOT met.** 「起码要能进入操作系统，把基础驱动跑起来」 is partially met (user mode and the
`memdev.c` fixture, per 745); the SDHCI storage driver does not exist; 「让os可以正常启动并且挂载存储」 is
not reached, so **TWRP-to-storage stays withheld.**
