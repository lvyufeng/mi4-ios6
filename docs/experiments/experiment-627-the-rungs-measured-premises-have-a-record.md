# 627: the rungs' measured premises were a claim in a comment, on logs no copy can be checked against

The sleeper clause of `run_and_capture.sh` prints a ladder whose thresholds and premises are **measured
on four archived captures**, and it says so in its own comments:

```
  "520 and 533 both stop at 2, with timeouts 5 ms and 40 ms"
  "both logs stop at exactly 16 records ending at 0x8000"
  "513's image - the one that predates the repair - reaches 0x1000000 here, 512 times this threshold"
  "neither log is capped"
  "`xnu_live_slot_cwe_*` has 3 in 533"
```

Every one of those numbers is load-bearing for the reading the owed run is pre-registered against -
rung 0's test is `door_max > 32768`, and *32768 is the last door record of both baselines* is a fact
about two files rather than about the instrument - and **nothing could check any of them**. That is
604's shape (a claim in a comment is not a check) with a sharper edge here than usual, because the
logs are **outside version control**: `out/` is gitignored and carries no capture, and 513's pair lived
only in `/tmp`. So the numbers were unverifiable from the tree in two ways at once - no record of them,
and no way to re-derive them on another host.

**This step changes no boot, no build and no reading.** It records the numbers, gives them a derivation
a program runs, and reports that the audit found **no error**. Host-side only: one record, one tool, one
document, one index row. No device, no `fastboot`, no `adb`, **nothing written to storage**; the arm and
both parks were hashed and never modified. **TWRP stays withheld.**

## 1. The audit: every premise re-derived, and the result

Each of the four logs hashed in place, and each premise's number derived with the same primitive the
runner itself reads the key with (`maxhex` takes the largest value, `keyval` the last - the clause's own
comment explains why it reads a counter that way):

| premise the clause prints | derived | verdict |
| --- | --- | --- |
| rung 0's threshold: `door_max > 32768` | 520 and 533 both `0x00008000` = **32768**, 513's pair both `0x01000000` = **16777216** | **exact** - and the strict `>` is right: `>=` would be satisfied by both baselines |
| "both logs stop at exactly 16 records ending at `0x8000`" | 520: 16 door records, max `0x8000`; 533: same | **exact** |
| "513's image reaches `0x1000000` here, 512 times this threshold" | `0x01000000 / 0x8000` = `0x200` = **512** | **exact** |
| rung 1: "520 and 533 both stop at 2, with timeouts 5 ms and 40 ms" | both: `poll_seq` 1, 2; timeouts `0x00000005`, `0x00000028` | **exact** |
| rung 1b: the park is the ask above `ENTRY_PARK_MIN_MS` (1000) | 520/533 max ask `0x28` = **40** (below); 513's pair max `0x7d0` = **2000** (above) | **exact** - the threshold separates them |
| "neither log is capped" | all four: 0 `xnu_live_capped` records; each publishes `xnu_live_cap=0x00002000` | **exact** - and both signals agree |
| 598's clause: 520 has **no** `slot_cwe_` key, 533 has 3 | 520: 0; 533: 3 | **exact** |
| 598's clause: `slot_pre_calls` = 1 | 520 and 533 both `0x00000001` | **exact** |
| 600's counterexample: 513's pair has no window family at all | both: 0 `slot_cwe_`, 0 `repair_seq` records, 25 door records to `0x01000000`, `poll_seq` to 4 | **exact** |

**So the audit's result is that there was nothing to fix** - which is itself the finding worth recording,
because the previous eleven instances in this class each ended in a correction. The premises hold; what
was missing was the ability to *tell*.

## 2. The record, and why its kinds are the runner's own readers

`stages/stage90/baseline-readings.txt` follows `revert-set.txt`'s convention: **written by hand, in the
step that measured it, never by the build or by the verifier** - a record its own subject writes agrees
with itself and constrains nothing. It names the four logs with hash and size, then 38 readings:

```
log=533   sha256=734062592d6f6a4efebd09ca4f252d2587ab4ac9e49311f8565417226b3f2c6d bytes=597641 file=533-2026-09-23-last_kmsg.txt role=baseline-arm-capture-2026-09-23
reading=slot_cwe_count  log=533   kind=count:xnu_live_slot_cwe_          value=3          cites=598-the-window-instrument-published-three-keys
```

**The `kind:` field is one of four primitives, and they are the runner's own two readers spelled out** -
`maxhex`, `keyval`, `first`, `count`. That is deliberate: writing the record as a table of numbers with
no derivation beside it would be one value with two definitions, the record saying what some step once
computed and the verifier saying what its own code computes. The `cites=` field names which premise
rests on the reading, so a reader can see the mapping.

`tools/verify_baseline_readings.sh` re-derives every line and refuses on a missing log, a wrong hash, a
wrong size, or a wrong value. It resolves a relative `--dir` against the **caller's** directory and says
so (615's defect), and a key that is absent yields the empty string, which matches no recorded value -
so a log that lost a key is a FAIL and not a silent zero (569's absent-is-not-zero, applied to the
verifier).

**513's pair was copied into `out/stage90/captures/` by this step.** It lived only in `/tmp`, and a
`/tmp` path is a path this record cannot be checked against on the next host. The copy does not make the
artifacts version-controlled - nothing can, `out/` is gitignored and the convention is that the repo
carries no capture - which is exactly why the record is the identity and the copies are only copies
(611's rule, applied here).

## 3. The verifier had two defects and a third found by falsifying it green

All three were found by **running** it, and the third is the one worth writing down.

**(a) `kind=` was not stripped before the `case`.** The record's words are `key=value`, so `$kind`
arrives as `kind=maxhex:xnu_live_door_seq`, and the derivation's patterns are `maxhex:*`. Nothing
matched, and the first run reported **38 FAILs on a correct record** - a verifier that refuses
everything is exactly as uninformative as one that accepts everything (614's rule).

**(b) A backslash inside a single-quoted `printf`.** `'…the rungs\' measured premises…'` ends the string
at the quote, leaving the rest unterminated. `bash -n` reports it; bash had already executed the whole
script by then, so the run printed its table and then exited 2. The line was reworded rather than
escaped.

**(c) The falsification that came back green: a record with no readings at all verified.** With the
expected counts derived from the record, deleting every `reading=` line gave *zero logs, zero readings,
zero failures, exit 0.* A check that succeeds by finding nothing is the one property a check that never
ran also has (613), and deriving the completeness criterion from the thing it is meant to bound is 612's
defect one level down. The record now pins its own size:

```
expect_logs=4
expect_readings=38
```

and the verifier refuses when the set it read is smaller, or when either field is missing - because a
record too old to carry the field is not a record this tool has been shown to bound. A deleted line is
therefore a red run and not a smaller green one.

## 4. The falsifications

Eight cases, each in a scratch directory with a scratch record, so `out/stage90/captures/` is never
written to:

| case | result |
| --- | --- |
| the committed pair, untouched | exit 0, **42 ok lines**, 38 readings, 0 FAIL |
| one record **value** changed | exit **1**, that one line red, 41 ok |
| one **log absent** | exit **1**, the log line and that log's 8 readings red, 33 ok |
| one log's **bytes** changed, hash left alone | exit **1** on the hash, its readings not derived at all |
| one log's bytes changed **and the hash updated to match** | exit **1** on the **value**: `got [0x00004000], record says [0x00008000]` - the reading is *derived* and not merely hash-checked, which is the case the hash alone would have hidden |
| a record with **no readings at all** | exit **1** on the pinned size |
| **one reading line deleted** | exit **1**: "the record names 4 log(s) and 37 reading(s); it says 4 and 38" |
| a record too old to carry the size fields | exit **1** |

The fourth and fifth rows are a pair on purpose: they separate "the bytes moved" from "the value moved",
and only the second shows the derivation doing work.

## 5. What this does not do

* **It does not touch the device, the arm, the payload or any parked byte.** `out/stage90/stage90-qcdt.img`
  is still the sleeper arm (`60063c47…`), still **UNRUN**, and the press is still the user's.
* **It does not make the numbers true.** It makes them *checkable*, and it reports that they hold today.
  A future build that changes `ENTRY_PARK_MIN_MS`, or an instrument that starts publishing past
  `0x8000`, would make a rung's premise stale without touching these logs - and this tool cannot see
  that, because it checks the logs and not the clause. The clause's own comments cite these readings;
  this record is what makes that citation auditable.
* **It does not verify anything the ladder computes from these readings.** The rungs' arithmetic is in
  the runner; the record holds the facts the arithmetic operates on.
* **It does not advance 「起码要能进入操作系统，把基础驱动跑起来」.** The device is off the bus; the only
  event that can move the goal is the user's **Vol-Down + Power**.

## 6. Safety

No device action, no `fastboot`, no `adb`, **nothing written to storage**, no boot, no build. Files
added: `stages/stage90/baseline-readings.txt` (the record), `tools/verify_baseline_readings.sh` (the
check), and two copies of 513's pair into `out/stage90/captures/` (copies - the `/tmp` originals are
untouched). Read-only device calls: `adb devices` and `fastboot devices`, both empty. `fastboot boot`
only - never `flash` - so no outcome of any of this can write to storage.
