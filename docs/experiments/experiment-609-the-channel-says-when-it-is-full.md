# 609: the channel says when it is full, and three clauses were not asking

603 §7 named a sweep and left it owed; 604 §5, 606 §5, 607 §5 and 608 §6 each repeated it: **every
`FAIL` whose named cause may not be the only one** — a cause that belongs to the harness, the SoC or the
capture rather than to the boot. Four steps carried it as debt. This step takes it, and it found the
defect the sweep was for.

The finding, in one sentence: **the live channel publishes a record when it starts dropping records, and
no clause in the reader had ever read it.** Three clauses were reading absence as a fact about the
machine — rung 0's "this run did not get past 32768 passes", rung 1's "the park's poll has not come
back", and the goal block's "what is missing here is a POSITION" — and a full channel turns each of them
into a fact about the instrument.

Host-side only: one file edited (`run_and_capture.sh`), three rehearsal states added, reads of 51
archived captures. No boot, no build, no device, no `fastboot`, no `adb`, **nothing written to storage**.
**TWRP stays withheld.**

## 1. The key that was published and never read

`entry_live_write` (`entry_stubs.c`, the `ENTRY_LIVE_CAP` block) is the single writer behind every
`xnu_live_*` key, and it is finite:

```c
entry_write_kv("xnu_live_cap", ENTRY_LIVE_CAP);          /* at init: 8192, every boot */
...
if (g_live_records >= ENTRY_LIVE_CAP) {
    if (g_live_records == ENTRY_LIVE_CAP)
        entry_write_kv("xnu_live_capped", g_live_records);   /* once, at the first drop */
    g_live_records++;                                        /* and then: dropped, silently */
    return;
}
```

So a log can stop publishing **in the middle of a boot that is still running**, and every key's last
value then belongs to the machine's state *at the cap*. The channel says so twice — with `xnu_live_cap`
on every boot and with `xnu_live_capped` at the first drop — and before this step the reader mentioned
neither key except in prose about the *baseline* pair (`xnu_live_capped` is absent from both, 585/611/619).

**Measured, so the size of the risk is a number**: the channel carries **4,258–4,449** records in the
nine archived arm captures and the two 513 captures — 52–54% of capacity — and none of them is capped.
That is why no run so far has shown this, and it is also why the number is now printed on every log
rather than only on the bad state.

## 2. The three clauses, and why each was a claim about the machine

| clause | what it says | what a full channel makes it |
| --- | --- | --- |
| rung 0 (`door_seq` stops at or below `0x8000`) | "this run did not get past 32768 passes" | the series stopped because the *channel* did |
| rung 1 (the falsifier: `poll_seq` stops at 2) | "the park's poll has not come back" | the third poll may have returned with its record dropped |
| the goal block (`g_stop` non-empty) | "what is missing here is a POSITION and not a driver fault" | the call may have run with its note dropped |

Rung 1 is the one that matters most: it is the arm's **falsifier**, and a falsifier that fires on a full
channel is not a falsifier. The goal block's is the one most likely to be read first, because it answers
the question the user asked.

The goal block's own file already contained the caveat in one place and not the other: the branch below
the FAIL has always said *"the same absence is what a log truncated before userland looks like"* for a
log with **no** fixture record at all. What was missing is that the same caveat applies to a log with
**some** records, because the truncation can fall in the middle of the fixture's sequence.

## 3. What the reader does now

**One clause, printed on every log that has a live channel, in one of three states** — because "not
full" is itself a reading and a check whose success is silent cannot be told from one that never ran:

* `live channel: not full - 4407 record(s) of 0x00002000, and no xnu_live_capped record, so the channel
  published every record this boot made and an absent key below is an event that did not happen`;
* `live channel: **TRUNCATED** - …` when `xnu_live_capped` is present **or** when the count of records
  against the cap the log itself published reaches it (**two signals**, because the `capped` record is
  published at the first *drop* — a boot that filled the channel exactly and then stopped has none);
* `live channel: not in this log` / `records and NO xnu_live_cap` when there is no channel to read — the
  payload-only logs, and the state where the capacity line is missing while records are present, which
  is a capture that lost the channel's *beginning* (the console ring wraps from the start, the live
  channel truncates from the end — the two failure directions are opposite, which is why both are read).

**And the three clauses consult the flag.** On a truncated log they print `UNREAD` instead of their
`FAIL`, and each says which two readings it cannot separate. The summary's own last paragraph — the one
that said "the park's poll has not come back at all, which is the falsifier 593 section 4" — is guarded
too: a reader that prints `UNREAD` and then draws the conclusion anyway is worse than either half alone.

## 4. The sweep's other eight `FAIL` branches, each read and each named

The extraction is mechanical: every `FAIL` line, with the guard path that reaches it. Eleven branches;
three are above. Of the rest:

* **rung 1b** (`poll_seq` returns but the timeout is below the park threshold) — a **second** finding of
  the same class, one build out: the threshold is read from *this tree's* `entry_trace.c` while the log
  may come from an older image, so a changed constant would silently re-point every archived log's
  reading (593's class). Measured before calling it theoretical: the line has been `1000` at **every
  commit that carries it** (512 introduced it; 514 and every step since). The assumption is now stated
  in the file rather than left implicit.
* **clause (1)** (a `panic ... sleh_abort` whose `lr` is not the exit pop's return address) — names its
  candidate lists, including the two the seam added, and prints the **source** of the address it
  compares against (`pop_lr_src`), which is the disclosure rung 1b lacked.
* **clauses (2) and its agreement check** (`slot_cwe_win` not clear; the two publishers disagreeing) —
  single-cause by construction: the second is a check *between* two readings of one register, and a
  disagreement is the reading.
* **clause (5)'s three** (`seam_lr` not the seam's return address; `seam_sp` not `sleh_sp-8`;
  `slot_pre_sp` not `seam_sp+8`) — each compares two values **from the same log**, so a mismatch is a
  property of the log and not of the build reading it. 586 already fixed the silent-absence neighbour.
* **the goal block's values `FAIL`** (every call present, a value wrong) — reached only when every call
  is present, so its three named cases are exhaustive *for the state that reaches it*.

## 5. Validation

**Nothing moved on any archived log.** A same-depth oracle copy of the pre-609 runner was diffed against
the working one across **51 archived captures** (`*-kmsg.txt` and 533's A–J): 51 show a change, and
**zero of the changed lines is a verdict line** — every difference is the new channel block. The
same-depth copy is 595's lesson; a copy in `/tmp` resolves `$REPO_ROOT` differently and its output moves
for reasons that have nothing to do with the edit.

**Eight states, and a state per side.** The rehearsal gained a `-capped` modifier (its base is whatever
precedes the suffix, so the only difference between the two logs is the channel's own record) and three
rows, plus a **count-only** state that no `capped` record can express:

| state | what the reader must print |
| --- | --- |
| `predicted` | `not full - 94 record(s) of 0x00002000` + the arm's PASS |
| `door-max-0x8000` → `…-capped` | `FAIL and it stops at or below 0x8000` → `UNREAD` + `**TRUNCATED**` |
| `poll-seq-2` → `…-capped` | `FAIL no poll record past the second` → `UNREAD` + "Whether the park's poll came back is **UNREAD on this log**" |
| `goal-truncated` → `…-capped` | `FAIL … no record of the control open` → `UNREAD` |
| `cap-full` (8,192 records, no `capped` key) | `**TRUNCATED**` from the count alone |

## 6. What this does not do

* **It does not boot anything, and it changes no arm, payload, prediction or gate verdict.**
  `out/stage90/stage90-qcdt.img` is still `60063c47…`, the parked arm is still the sleeper
  (`STAGE90_XNU_IDLE_NO_SLEEP=1`, entry `696a0f39…`), and the press is still the user's.
* **It does not make the coming run's log truncated, and it does not expect one.** The archived captures
  sit at ~54% of capacity; the clause exists because the *next* run is the one that goes further, and
  because the reader should not answer a question about the channel with a sentence about the machine.
* **It does not advance 「起码要能进入操作系统，把基础驱动跑起来」.** The device is off the bus and no host
  action substitutes for a power press.
* **It does not change rung 1's witness or falsifier** on a log that is not truncated:
  `xnu_live_poll_seq=3` with `poll_timeout_ms >= 1000` is still the prediction, and `poll_seq` still 2 is
  still the falsifier. What changed is that the falsifier needs a log whose channel published to the end
  of the run — which is a *narrowing* of the falsifier and it is stated, not hidden.
* **It does not sweep the `UNREAD` branches** — the other nineteen, whose business is already saying
  "this is not a reading". The debt 603 §7 named is for the branches that *draw a conclusion*.

## 7. Safety

No device action, no `fastboot`, no `adb`, **nothing written to storage**, no boot, no build. One
host-side file edited (`stages/stage90/run_and_capture.sh` — narration and three guards, no new `exit`,
no new `die`) and one (`tools/rehearse_live_path.sh`), neither in `xnu_arm_entry-sources.txt`. Reads of
51 archived captures and of `entry_stubs.c`. One same-depth oracle copy of the pre-step runner, removed
after the diff. The boot gate was re-run afterwards → **EXIT=0 / 537 lines / 0 stderr**, unchanged, and
the live-path rehearsal end to end → **7 ok / 0 failed** and **13 ok / 0 failed**. The payload, the
parked frozen pair at `/tmp/r594/frozen-payload/` and the arm the next press sends are unmodified.
`fastboot boot` only — never `flash` — so no outcome of any of this can write to storage.
