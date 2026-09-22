# 566: the runner's whole path is rehearsed, and both producers of exit 3 are exercised

A host-side rehearsal of `run_and_capture.sh` against a **stubbed `sudo`**, no device and no build. 564
repaired three defects in step 5 and added step 2b, and verified them by sourcing the extracted block in
an isolated harness (states A-H). That test covers step 5's *body*; it does not run the script - and
`--dry-run` does not either, because every branch under `DRY_RUN=1` is a `say` line. So until now the
real path from step 1 to step 5 had never executed end to end on any host, which is the same gap 564
exists to close one level down: **the code that runs once, after the boot is spent, had no rehearsal.**

## 1. The rehearsal, and why a stub `sudo` makes it safe by construction

`PATH=/tmp/e2e/bin:$PATH LOGFILE=/tmp/e2e/log ./run_and_capture.sh --allow-xnu-entry`, where
`/tmp/e2e/bin/sudo` answers `dmesg`, `adb` and `fastboot` and does nothing else. The runner calls `sudo`
**bare** (never `/usr/bin/sudo`), so PATH decides - and the stub's trace is the evidence that every device
call was intercepted:

```
[stub sudo] adb devices
[stub sudo] adb -s 4a2fe00b reboot bootloader
[stub sudo] fastboot devices
[stub sudo] fastboot boot /mnt/data/mi4-ios6/out/stage90/stage90-qcdt.img
[stub sudo] dmesg
[stub sudo] adb devices
[stub sudo] adb -s 4a2fe00b exec-out cat /proc/last_kmsg
```

and the two properties that make it safe rather than merely convenient: `LOGFILE` is overridden to a
scratch path, so the real capture was never a candidate (its sha256 is `f0285b0f…` before and after), and
**a stub that failed to intercept could not have booted the phone anyway** - the script refuses to reach
`fastboot boot` unless the serial is found in `adb`/`fastboot` first, and the device is off the bus.
`flash` is not used anywhere in the script.

The gate inside the rehearsal is the **real** gate, run read-only, exit 0.

## 2. The success path, first: exit 0 and a staged capture

With the log absent, the runner printed step 2b's `no log at /tmp/e2e/log yet - so after this run, the
name existing at all is the first check`, then booted, waited, captured, and exited **0**, writing 399
bytes. `out.txt`'s last line is the payload summary. No `$LOGFILE.new` survived, which is the staging
property from 564 §3 - the temp is renamed into place, not left behind.

## 3. Both producers of exit 3, each with the *other* one ruled out

564 §1's defect was that step 5 called `die` (exit 1) for the state the header defines as 3. The repair
gives exit 3 **two producers** - section 4 and section 5 - and a code with two producers is exactly the
site that stays uninspected, so each was rehearsed in a state where the other cannot fire:

| variant | stubbed behaviour | where it must exit | measured |
| --- | --- | --- | --- |
| 1 | previous log present; `exec-out` always fails; adb lists the serial | **step 5**, exit 3 | exit **3**; name **absent**; `$LOGFILE.prev` present with the previous sha256 **unchanged**; step 5's message ("REFUSING to call this a hang") |
| 2 | adb stops listing the serial after the boot; the enumeration appears promptly | **step 5**, exit 3 | exit **3**; name absent; parked intact |
| 3 | as 2, but the enumeration appears **only after** the bounded wait expires | **section 4**, exit 3 | exit **3**; `bounded wait expired after 3s` with `adb: not listed` and `host log: 1 -> 2`; section 4's message, count 1, step 5's message count **0** |

Variant 3 is the one worth naming: it is the state the phone actually entered on 2026-09-22 (`2717:0368`
for 18 s, dropping before `18d1:4ee7`), and it is the case 551 added exit 3 for. Reaching it required the
stub to be *late*, not merely failing - with the enumeration prompt, section 4 returns and step 5 becomes
the producer instead, which is what variants 1 and 2 measured. **A test that only makes the device fail
tests the wrong producer.**

## 3b. And the reading procedure for the coming run, rehearsed on the real log

The strongest available rehearsal of the run that is owed is not a synthetic log: it is **520's real
capture with only the one pair 533's arm adds**, appended.

```
cp /tmp/cancro-last_kmsg.txt /tmp/t566-real520.txt     # 3931 payload lines, real brackets, real storm counts
printf '..._slot_cwe_calls=0x00000001\n..._cwe_win=0x00c50830\n..._cwe_set=0x00c50830\n' >> ...
```

520's log carries **none** of the `cwe_*` keys (verified: `grep -ao 'xnu_live_slot_cwe_[a-z]*='` is empty), so
that pair is exactly what 533's image adds and the appended lines are the only invented thing in the file.
The reader then runs its whole block on real data: clause (1) **PREDICTED** (`sleh_lr=0x800462dc`, the exit's
own `pop`), clause (2) **PASS** then **`ARM 533's arm`** (`_win` = `_set` = `0x00c50830`, C clear - the
agreement that is this arm's expected reading), clause (3) **DIED IN THE EXIT**
(`pre_calls=0x00000001`, `rtcpre_calls=0x00000001`, `post_calls` absent), and the closing line *"the arm's
prediction arrived"*.

Two things this buys that the nine-key synthetic did not. The counts are **real**: `storm=0x00000009`,
`episodes_seen=0x00000009`, and 3 user-mode fault records - so the reader's `tail -1` key discipline (555) is
exercised against a log whose two blocks carry both the fatal episode and an earlier one (557), not against
a file where every record is the only record. And it is the **predicted shape** of the next run: 547 §4's
row (i) is "dies at the pop, log present, device returns", which is precisely this file's bracket. So if the
run returns, the reader has already been shown to print the right verdict for the log it will produce - the
remaining unknown is the device, not the reading.

## 4. The gate's narration and the runner now agree, measured rather than argued

562/563/563b have been rewriting the gate's sentences about where the log lives after a non-return, and
563b's point was that a correction sweep must run **by predicate**: it found that 562's "leaves before that
step and `$LOG` is untouched" was made false by step 2b, and its repair prints the parked path in every
state. That repair is visible in the rehearsal output, above the branch:

```
  And if the name is gone after the run, the PREVIOUS log was parked rather than lost: step 2b
```

and the runner's own behaviour in variants 1-3 is exactly that sentence: the name is gone, the bytes are
at `$LOGFILE.prev`, and step 5's message names that path. **The two files' stories about one state now
match, and the match was measured by running the runner with the gate in front of it** - which is the only
way that pair can be checked, since neither file can see the other's code path.

## 5. What this does not cover, stated so it is not read as coverage

- The stub's `dmesg` is a two-line `printf`, so nothing here tests `serial_enum_count`'s parsing of real
  kernel output, and nothing tests the host's `fs.protected_regular` behaviour on a sticky `/tmp` (511,
  512) - the failed-park branch of step 2b can only be exercised on this host by an identity that does not
  own the file.
- The rehearsal says nothing about the **device**: whether 533 survives the idle pass is still the run's
  question, and 547 §4's three rows are still the table it will be read against.
- `CAPTURE_WAIT`/`RETURN_TIMEOUT` were shortened in the variants (5-15 s) to keep the rehearsal short; the
  defaults (90/180) are unchanged in the file.

## 6. Safety

No device action: a stub `sudo` in `PATH`, `LOGFILE` redirected to `/tmp/e2e/`, one real read-only gate run
(exit 0), no build, `flash` not used. The real capture is byte-identical
(`f0285b0f6e22eb0a097b5756cc6a5e11f827e1d6b20be03c9eb0b1a0510b2612`), the frozen pair is untouched
(`1daaf44e624563694e…` / `f202f2465886aba6…`), and nothing was written outside `/tmp/e2e` and `docs/`.
