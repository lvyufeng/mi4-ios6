# 778: the counts the "never started" claim rests on were transcribed, not read — and 771, the step that named that class, got one of them wrong in both halves

**HOST-SIDE ONLY. NO DEVICE ACTION, NO PRESS, NO GATE AGAINST A DEVICE, NO RUNNER, NO FIRER, AND NOTHING
BUILT.** `out/` was not touched: this step edits no entry source, so the armed rung-28 arm
(`armed-storage-d5d98738`, `STAGE90_XNU_STORAGE_PROBE=27`) and every parked arm are byte-identical before
and after it. One press spent by this step: **none**.

## 1. Every poll count in the archive, computed from the captures

Each capture reports a cell as a hexadecimal word. Everything below is read out of
`out/stage90/captures/*-last_kmsg.txt` by the step's own script and not from any document — the point of
the step is that those two are not the same thing.

| capture | `_cmd0_polls` | `_cmd1_polls` | `_cid_polls` | `_nidx_polls` | `_rca_polls` |
| --- | --- | --- | --- | --- | --- |
| `rung15-enable-20260926-101331` | `0x0000021B` = 539 | `0x004DB000` = 5,091,328 | - | - | - |
| `rung17-cid-20260926-144313` | `0x0000021B` = 539 | `0x004DA800` = 5,089,280 | `0x004DA800` = 5,089,280 | - | - |
| `rung18-rb-20260926-155937` | `0x0000021A` = 538 | `0x004DA400` = 5,088,256 | `0x004DA800` = 5,089,280 | - | - |
| `rung19-rca-20260926-180922` | `0x0000021B` = 539 | `0x004DA800` = 5,089,280 | `0x004DA400` = 5,088,256 | - | `0x004DA400` = 5,088,256 |
| `rung20-nrsp-20260927-012235` | `0x0000021A` = 538 | `0x004DB000` = 5,091,328 | `0x004DAC00` = 5,090,304 | - | - |
| `rung21-noidx-20260927` | `0x0000021A` = 538 | `0x004DA000` = 5,087,232 | `0x004DA400` = 5,088,256 | `0x004DA000` = 5,087,232 | - |
| `rung22-dllcensus-20260927-080441` | `0x0000021B` = 539 | `0x004DB400` = 5,092,352 | `0x004DB400` = 5,092,352 | `0x004DB800` = 5,093,376 | - |
| `rung24-cmdline-20260927-090636` | `0x00000219` = 537 | `0x004DAC00` = 5,090,304 | `0x004DB000` = 5,091,328 | `0x000006FD` = 1,789 | - |

Rung 14's own capture (`rung14-completion-20260926-074001`) carries **no `_cmd1_` key at all** — the publish
block that prints CMD1's row and the sampler that fills it arrive at rung 15 — so the first capture in the
archive that can answer a question about `_cmd1_polls` is rung 15's. And only rung 19's capture carries
`_rca_` cells at all: the RCA block is in the arm rung 19 pressed, so the `_rca_` sentences elsewhere in
this repository can only be about that press, and one of them is repaired in §3 on exactly that ground.

**Every large value in this table — and there are seven distinct ones across the ten captures — is a
multiple of 1,024.** §2 shows why that is a property of the loop and not of the samples.

## 2. Three facts fall out of the table, each of them a defect somewhere — and a fourth the table does not show, which is stronger

1. **`0x004DB000` is 5,091,328.** It is never 5,088,000 and never 5,088,256.
2. **`0x004DA400` is 5,088,256** — the decimal that had been attached to `0x004DB000` in two places.
3. **The two counts swap between presses.** Rung 20: CMD1 `0x4DB000`, CMD2 `0x4DAC00`. Rung 24: CMD1
   `0x4DAC00`, CMD2 `0x4DB000`. That is the whole mechanism: a pair remembered from one capture is
   *correct* against that capture and *inverted* against the next, so a mis-quotation is invisible in the
   press where it was made and obvious in the press after it. It is also why two documents can each look
   internally consistent and still contradict each other.

**And `5,088,000` is no capture's count at all — and it is not merely absent.** The second half of that
sentence is a measurement this step did not expect to get, and it is the strongest thing in it.

The ten archived rung captures print **exactly seven** distinct large `*_polls` values:

| | | | | | | |
| --- | --- | --- | --- | --- | --- | --- |
| 5,087,232 | 5,088,256 | 5,089,280 | 5,090,304 | 5,091,328 | 5,092,352 | 5,093,376 |

and those seven are **exactly the seven multiples of 1,024 in `[0x004DA000, 0x004DB800]`** — `0x004DA000 +
k·0x400`, `k = 0..6`. That is not a coincidence of sampling, and the reason is in the ladder's own source:

```c
for (steps = 0u; steps < (ST_CMD_DONE_TICK_BUDGET / ST_CMD_DONE_INNER); steps++) {
    for (i = 0u; i < ST_CMD_DONE_INNER; i++) {
        r->polls++;
        ...
        if ((r->status_after & ST_SDHCI_INT_CMD_MASK) != 0u)
            goto cmd_done;
    }
    if ((uint32_t)stage90_cntvct_read() - t0 >= ST_CMD_DONE_TICK_BUDGET) {
        r->timed_out = 1u;
        break;
    }
}
```

`ST_CMD_DONE_INNER` is `1024u` (`entry_storage.c:2216`) and the clock is read **once per outer step**, so
the poll has two exits and only two: the mask bit inside the inner block, which can land anywhere, and the
budget test after it, which lands on a multiple of 1,024. **A command that latched nothing can only leave
by the second**, and CMD1, CMD2 and rung 19's CMD3 are exactly that by the archive's own cells
(`_cmd1_status_any = 0`, `_cid_status_any = 0`, `_rca_status_any = 0`, `_rca_complete = 0` in every
capture that carries them). So:

> **`5,088,000` is not off the lattice by accident: `0x004DA300` is not a multiple of `0x400`, and no
> command that latched nothing can leave this loop on a value that is not.** It is a decimal that was
> rounded in prose — 5,088,256 to three significant figures is 5,088,000 — and then copied, into the
> ladder's own `#error` text (`entry_storage.c:116`), into its two other comments (`:3054`, `:3861`), into
> this repository's readiness narration (`tools/verify_press_ready.sh:791`), into one row of the experiment
> index and four experiment logs — with two further sites quoting it in order to refute it, which is a
> correction that had itself gone wrong (§4).

**The same test disposes of the two other counts that are not any capture's reading**: `5,088,768`
(`0x004DA600`, half a step) and `5,081,600` (`0x004D8A00`, 106 steps below the lowest value the loop has
ever produced here). Neither is on the lattice, and both were quoted as readings. The inventory, measured
with `git ls-files | xargs grep`, is in §3.

## 3. The sites, and what each one's repair is

Four sites are **owed**, and they are all one file: `src/entry/entry_storage.c` is a member of the entry
image's recorded source manifest, so editing a character of comment in it makes the gate refuse the arm a
press is waiting on (763 §5's measured cost). The other fourteen are corrected in place, each against the
capture that carries the cell, and each marked `(778)` at the site so the correction is traceable from
where it lands.

| site | written | the count is | repair |
| --- | --- | --- | --- |
| `src/entry/entry_storage.c:116` | `_cmd1_status_any = 0` over 5,088,000 polls (733's press) | **5,091,328** | **OWED — an entry source, see §8** |
| `src/entry/entry_storage.c:116` | `inhibit_seen = 0` over 5,088,256 and 5,088,000 samples | **5,091,328 / 5,090,304** | **OWED — an entry source, see §8** |
| `src/entry/entry_storage.c:3054` | `0x004db000` = 5,088,000 | **5,091,328** | **OWED — an entry source, see §8** |
| `src/entry/entry_storage.c:3861` | `5,088,256` / `5,088,000` | **5,091,328 / 5,090,304** | **OWED — an entry source, see §8** |
| `tools/verify_press_ready.sh:791` | `_cmd1_complete = 0` over 5,088,000 polls | 5,091,328 | corrected in place |
| `tools/verify_press_ready.sh:826` | 5,088,256 / 5,088,000 | 5,091,328 / 5,090,304 | corrected in place |
| `records/revert-set.txt:1675` | `0x06e0953c` = 115,318,588 ticks = 6006.2 ms | 115,381,564 = 6,009.5 ms | corrected in place |
| `records/revert-set.txt:3643` | `0x004da800` (5,088,256) | 5,089,280 | corrected in place |
| `records/revert-set.txt:4220` | `_rca_status_any = 0` across 5,081,600 polls | 5,088,256 | corrected in place |
| `records/revert-set.txt:4590`–`:4591` | 5,088,256 / 5,088,000 | 5,091,328 / 5,090,304 | corrected in place, see §5 |
| `records/revert-set.txt:5349` | `0x004da000` = 5,088,256 · `0x004da400` = 5,088,768 | 5,087,232 · 5,088,256 | corrected in place |
| `records/revert-set.txt:6160` | 771's own correction note | see §4 | rewritten where it stands |
| `docs/experiments/README.md:568` | the 771 index row | as §4 | corrected in place |
| `docs/experiments/README.md:580` | the 759 index row: 5,088,256 · 5,088,768 · 5,088,256 | 5,087,232 · 5,088,256 · 5,087,232 | corrected in place |
| `docs/experiments/README.md:592` | the 746 index row: 5,088,256 · 5,088,000 · 5,088,000 | 5,091,328 · 5,090,304 · 5,088,256 | corrected in place |
| `docs/experiments/experiment-691-…md:5` | `0x092a0bea` = 153,747,946 | 153,750,506 | corrected in place, marker added |
| `docs/experiments/experiment-707-…md:124` | `0x06e0953c` = 115,318,588 | 115,381,564 | corrected in place, marker added |
| `docs/experiments/experiment-737-…md:83` | `_cmd1_status_any = 0` over 5,088,000 polls | 5,091,328 | corrected in place, marker added |
| `docs/experiments/experiment-743-…md:30` | `_rca_status_any = 0` over 5,081,600 polls | 5,088,256 | corrected in place, marker added |
| `docs/experiments/experiment-746-…md:144` | `0` of 5,088,000 (CMD2) | 5,090,304 | corrected in place, marker added |
| `docs/experiments/experiment-746-…md:214` | `INT_STATUS` zero over 5,088,000 polls | 5,088,256 | corrected in place, marker added |
| `docs/experiments/experiment-747-…md:43` | `0` of 5,088,000 (CMD2) | 5,090,304 | corrected in place, marker added |
| `docs/experiments/experiment-749-…md:53` | `0` of 5,088,000 (CMD2) | 5,090,304 | corrected in place, marker added |
| `docs/experiments/experiment-752-…md:65`, `:68` | 5,088,256 · 5,088,000 | 5,089,280 · 5,088,256 | corrected in place, marker added |
| `docs/experiments/experiment-759-…md:92`–`:94` | three decimals | 5,087,232 · 5,088,256 · 5,087,232 | corrected in place, marker added |
| `docs/experiments/experiment-771-…md:40`, `:120`–`:125` | see §4 | see §4 | corrected in place, marker added |

**Which count belongs to which cell is a measurement, not an inference**, and it is the one part of this
table a reader can check in a minute: `grep -o 'xnu_live_storage_\w*polls=0x[0-9A-Fa-f]*' out/stage90/captures/*`
prints every one of them, and `§1`'s table is that command's output. The repairs follow it cell by cell —
so `README.md:580`'s three decimals are rung **21's** (`0x004DA000` / `0x004DA400` / `0x004DA000`), while
`README.md:592`'s are rung **20's** for CMD1 and CMD2 and rung **19's** for the `_rca_` cell it also quotes.
The two rows are adjacent in the index and their counts are not the same, which is §2's swap again: it is
why a reader comparing two rows cannot use one to check the other.

**The hexadecimal is the reading in every one of them.** Where the decimal and a *derived* figure disagreed
with it, both were recomputed from the hexadecimal and not the other way round: `0x06e0953c` = 115,381,564
ticks, and at this image's 19.2 MHz counter that is 6,009.5 ms, not the 6,006.2 ms the old decimal implied.
The device printed the hexadecimal; nothing printed the decimal.

## 4. 771 §5 is a correction that is wrong in both halves, and it is the step that named this class

771 §5 — the section titled *"a third correction, one number wide, and it is in the same sentence"* — reads:

> That paragraph writes CMD1's poll count as `_cmd1_polls = 0x004db000` = 5,088,000. The capture's
> `cmd1_polls` is **`0x004DAC00` = 5,090,304** — the hexadecimal `0x004db000` is CMD2's `cid_polls`
> (`0x004DB000` = 5,088,256, to which the decimal is also not equal).

Three things are wrong in it, and the first is arithmetic that a reader can do in their head:

1. **`0x004DB000` = 5,088,256 is false.** It is 5,091,328. The decimal 5,088,256 is `0x004DA400`.
2. **"the hexadecimal written is CMD2's `cid_polls`" is false for the sentence it corrects.** The sentence
   at `entry_storage.c:3054` is 733's press — the one `records/revert-set.txt:3290` names as
   `armed-storage-enable-953ad0f6`, fired 2026-09-26 10:12:21–10:13:31 UTC, whose capture is
   `rung15-enable-20260926-101331` — and that capture reads `cmd1_polls = 0x004DB000`, which is **CMD1's**
   count there. The
   other cells in the same sentence (`_cmd1_resp = 0x40ff8080`, `_cmd1_complete = 0`, `_cmd1_status_any = 0`)
   are rung 15's too. So the hexadecimal was **correctly attributed all along** and only its decimal was
   wrong.
3. **The comparison it rests on is across two presses.** `cmd1_polls = 0x004DAC00` is rung 24's count. §2's
   table is why that silently works: `0x004DB000` *is* CMD2's count in rung 24. 771 compared a sentence
   about one press against the capture of another, and the swap made the comparison look decisive.

**The shape is m-series and it is worse than the defect it corrected.** The wrong decimal was a
transcription error in a comment. The correction that replaced it asserted a mis-attribution that was not
there, quoted a second false equality as its evidence, and was then copied verbatim into
`records/revert-set.txt:6158-6162`, where a reader looking for the corrected number finds the wrong one
twice. **A correction is a claim like any other, and this one was never evaluated at both ends** — m776's
lesson one step over.

## 5. The 746 block's count came from the capture *before* it

`records/revert-set.txt:4590`–`:4591` is the three-way split 746b landed: CMD0 and CMD3 started, rung 19's
CMD3-R1 started and stuck, and CMD1 and CMD2 were never taken at all — with `_cmd1_inhibit_seen = 0` "over
**5,088,256** samples" and `_cid_inhibit_seen = 0` "over 5,088,000". The block's own header says *"four in
this capture and the fifth (rung 19's CMD3) in 742's"*, so *this capture* is rung 20's, whose counts are
**5,091,328** and **5,090,304**.

And **5,088,256 is rung 18's own CMD1 count** — the capture immediately before. So the number is not
invented; it is the *previous press's*, carried one block up, which is the same shape as the rest of this
document and the reason the check in §6 is worth having rather than a proof-reading pass.

## 6. The repair is a check, and it refused its own author

`tools/check_count_citations.py`, run by `make check`. It reads every tracked file outside `archive/` and
refuses a count quoted in two bases that disagree. The rule is not `hex == decimal`, because `A = B` in this
tree is not always one value in two bases — `src/entry/build_entry.sh` writes `offset (step number)` pairs,
the record writes a tick count beside a millisecond figure, and an index table writes
`experiment number (count)`. So it is a conjunction of four conditions, each fixed by measuring the tree it
runs on, and the count of conditions is four because three of them were found by the tool producing findings
that were not defects:

1. the pair is joined by `=` or `( … )` with at most whitespace and backticks between;
2. the decimal is comma-grouped — this repository writes counts ≥ 1000 with separators and step numbers
   without them, so the separator separates the two uses;
3. the parenthesised form needs **two** groups: the one-group form is the pervasive `offset (step)` idiom,
   and admitting it produces forty false findings;
4. `h / 1000 <= d <= h * 1000` — one value in two bases differs at most by rounding, and a pair three orders
   apart is two different quantities wearing one `=`.

It **refuses** on `tools/`, `scripts/`, `records/`, `Makefile` and `src/`; it is **exempt** on `src/entry/`
for a cost reason (§8); and it only **prints** what it finds under `docs/experiments/`, which is the
historical record and is superseded rather than rewritten (the restructure's own rule). **CORRECTED BY 779a**: the reading
below was taken while this tool was still UNTRACKED, and the scan walks `git ls-files` — so the file being
verified was excluded from its own verification. The commit that added it made three of its `SELFTEST` cells
— which ARE the defect they test — visible, and `make check` went red on master at `9a0291e`. The cells are
now one string split across two adjacent literals, and the honest reading is **108 counts, 0 disagreeing in
the refusing scope, 11 in the historical record** (this document's own deliberate quotations of the bad
pairs, in the scope that reports). The sentence as this step landed it read: *it reads 73 counts, with none
disagreeing in any scope* — and **it refused its own author twice**,
which is the strongest thing that can be said for it:

1. Against the first draft of `records/revert-set.txt`, where the correction note quoted the false equality
   in order to refute it. The note was rewritten **to quote the two halves apart**.
2. Against the first draft of this step's own `Makefile` comment — the block that documents the tool, which
   opened by naming the defect as `0x004db000` = 5,088,000 and was therefore, character for character, the
   defect. `make check` exited **2** on it. The comment now says the same thing in prose, with the two
   halves in separate sentences, and says why.

**Both refusals are the same finding, and it is not a limitation of the tool.** A citation of a defect and
the defect are the same characters, so a check that reads characters cannot separate them and the *writer*
has to — by not writing the bad pair adjacent. That is the honest way to cite a defect through such a check,
and it is why this document states the two halves of each bad pair on different lines rather than quoting
them joined.

The tool carries a self-test, for `read_storage_key_order.py`'s reason: it is a reference tool whose output
looks plausible when it is wrong. It asserts that its ten cells split the way they should, including the
four idioms above — and **`make check` runs it**, on its own line above the scan, because a self-test that
is never run is the same defect this step is about.

## 7. What this does not disturb, and it is the load-bearing part

**"CMD1 and CMD2 were never started" is unmoved.** That claim rests on `_cmd1_inhibit_after = 0` and
`_cmd1_inhibit_seen = 0` — a *bit* and a *count of samples in which a bit was set* — and not on how many
samples were taken. 5,087,232 and 5,091,328 are both "over the whole bound", and `_cmd1_any_polls = 0` and
`_cid_any_polls = 0` are the cells the 749/760 discipline actually uses. 759 §4's point — *"5,088,256 samples
is not zero samples"* — is a point about `any_polls`, and it survives its own number being wrong.

**The frontier is where 768 left it**: the controller is correct, the card is silent, and the next thing to
read is the pad register the armed arm reads. This step moved no cell the ladder measures.

## 8. Owed, and named rather than left to be inferred

- **Four entry-source sites, and they are the last of the `5,088,000`s.** `entry_storage.c:116` carries two
  of them — 733's press and the rung-20 split — and `:3054` and `:3861` the other two. They are entry
  sources: a member of the entry image's recorded source manifest, so editing a character of comment makes
  the gate refuse the arm a press is waiting on (763 §5's measured cost, applied by 768 §8). Owed for the
  **COST** reason and not a lane reason, and the next build should be an arm that carries the correction —
  which is why the check's refusing scope excludes `src/entry/` rather than pretending it is clean. The
  fix is four decimals and the two sentences they sit in, and the build that carries it must pass
  `tools/check_payload_config_entry.sh` like any other.
- **`tools/verify_press_ready.sh` is not linted for prose numbers that carry no hexadecimal.** Two sites
  there were fixed by hand (`:791` and `:826`) and **nothing checks either one** — the tool that reads that
  file reads a count quoted in two bases, and these two quote one base. Named, not fixed by rule.
- **A second check is pre-registered and not built: the lattice.** §2's fact is mechanically checkable and
  the check would be about a dozen lines — *a `*_polls` count above `ST_CMD_DONE_INNER` cited in this tree
  must be a multiple of 1,024, unless the sentence also fixes the command as one that latched a mask bit.*
  It was **not** built in this step for a reason worth stating rather than hiding: the exception clause is
  the whole difficulty, and a check whose exception is a judgement about prose is the kind of check this
  repository has already learned to distrust (m776, m779). What this step leaves behind instead is the
  measurement the check would rest on — the seven values, the two constants, and the loop shape in §2 —
  so that whoever builds it starts from a reading and not from a rule.
- **The seam-address class** — *a kernel address pinned in an entry source* — whose repair is a link order;
  and **`run_and_capture.sh`'s `EXIT_POP_LR_LITERAL` structural repair**: both unchanged from 724, and both
  in the peer's lane, with **no live addressee**: `ListAgents` at this step reports this session
  (`run-experiment-526`, the gate owner) and two sessions that are unrelated to this tree.

## 9. What this document does not say

- **It does not say the counts were ever used to decide anything.** Every one of them is a *size* in a
  sentence whose conclusion is a bit. That is exactly why nobody checked them, which is 771 §5's own
  sentence and the reason it is worth keeping.
- **It does not spend a press and it does not authorize one.** Reading archived captures and editing
  comments are host-side.
- **It does not advance 「挂载存储」.** No storage, no filesystem, no mount, no driver.

**The goal is still NOT met.** 「起码要能进入操作系统，把基础驱动跑起来」 is partially met (user mode and the
`memdev.c` fixture, per 745); the SDHCI storage driver does not exist — the block issues a
response-demanding command, drives the CMD line, and **times out because the card does not answer** — and
「让os可以正常启动并且挂载存储」 is not reached, so **TWRP-to-storage stays withheld.**
