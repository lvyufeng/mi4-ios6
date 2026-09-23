# 626: the arm was decided by four keys that only say the window was not reached

`run_and_capture.sh --summarise` decides which arm's clause to print from the log's own keys. That
decision was one conjunction over six keys:

```sh
  if ! grep -a -q 'xnu_live_slot_cwe_' "$log" \
     && grep -a -q 'xnu_live_door_seq=' "$log" \
     && ! grep -a -q 'xnu_live_repair_seq=' "$log" \
     && ! grep -a -q 'xnu_live_sip_seq=' "$log" \
     && ! grep -a -q 'xnu_live_pce_seq=' "$log" \
     && ! grep -a -q 'xnu_live_wfi_seq=' "$log"; then
    idle_no_sleep_arm=1
  fi
```

**Exactly one of those six keys is absent on the sleeper arm *by construction*.** `entry_note_repair`
- the publisher of `xnu_live_repair_seq` - sits inside `#if !STAGE90_XNU_IDLE_NO_SLEEP` in
`entry_trace.c` (`:1283`, the call at `:1295`), so the image that skips the repair cannot publish that
key whatever the machine does. The other four are **behavioural**: their publishers are compiled into
both images and are reached only by a pass that gets into the window. So a log that *does* reach the
window failed the conjunction on behaviour, and the branch it then took was the baseline arm's -
**the sleeper arm's own reading printed as the other arm's ordinary death**, with the rung lines
simply absent and nothing anywhere saying why.

The gate session measured that (622, `b0c1c02`): appending ONE `xnu_live_slot_cwe_win=` line to a
533-derived log that had scored rung 1 and rung 1b PASS turned the whole reading into the baseline
arm's death block. It also found the converse in its own file - the arm block answered the 0 arm by
printing nothing - and fixed that half message-only. This step is the runner's half.

**Host-side only:** one script edited, one rehearsal extended, one document, one index row. No boot,
no build, no device, no `fastboot`, no `adb`, **nothing written to storage**; the arm and both parks
were hashed and never modified. **TWRP stays withheld.**

## 1. The measurement: which key is structural, which are behavioural, and the one that is neither

Every publisher, read out of this tree rather than recalled:

| key | publisher and call site | what its absence means |
| --- | --- | --- |
| `xnu_live_door_seq` | `entry_note_door`, `entry_trace.c:1824`, in `__wrap_Idle_load_context` | **reach guard** - present on *both* arms, so its absence means `cpu_idle` was never entered at all |
| `xnu_live_repair_seq` | `entry_note_repair`, `entry_trace.c:1295`, **inside `#if !STAGE90_XNU_IDLE_NO_SLEEP`** (`:1283`) | **STRUCTURAL** - the sleeper image cannot publish it, whatever the machine does |
| `xnu_live_slot_cwe_*` | `entry_window_note`, `entry_trace.c:1973`, in `__wrap_platform_cache_idle_exit` | behavioural - a pass got into the window |
| `xnu_live_sip_seq` | `entry_note_setidlepop`, `entry_trace.c:1835`, in `__wrap_SetIdlePop` | behavioural - a pass got past `cpu_idle`'s first test |
| `xnu_live_pce_seq` | `entry_note_pce`, `entry_trace.c:1912`, in `__wrap_platform_cache_idle_enter` | behavioural - the enter wrapper ran |
| `xnu_live_wfi_seq` | `entry_note_wfi`, `entry_trace.c:1882`, in `__wrap_cpu_idle_wfi` | behavioural - the WFI ran |

`sip_seq`, `pce_seq` and `wfi_seq` are the arm's *effect*, not its identity. The switch removes the
repair, so `SIGPdisabled` stays set and `cpu_idle` leaves by its first door on every pass, so none of
those four is ever published - which is true, and is also true of a log whose machine simply never
got that far. **An absence that two different states both produce is not a test**, and the old
conjunction scored four of them as one.

## 2. What the old conjunction did, measured on the peer's own fixture

`mk-sleeper.py` (the gate session's, handed over) drops exactly the seven records whose absence the
conjunction tests, from 533's archived capture - a real artifact, not a synthesised log. On
`rung1-sleeper` (the drop, plus `poll_seq` 3 / `poll_timeout_ms` 1024) and on the same log **with one
`xnu_live_slot_cwe_win=` line appended**:

| fixture | old reader | new reader |
| --- | --- | --- |
| `rung1-sleeper` | arm ladder, rung 1 **PASS** | arm ladder, rung 1 **PASS** (unchanged) |
| `rung1-sleeper` + one `slot_cwe_win` | **baseline death block**, rung 1 PASS count **0** | arm ladder, rung 1 **PASS**, and the disagreement printed |

The right-hand cells are the peer's defect and its repair: the second row is the same log as the
first, with one record added, and under the old test that one record deleted the arm's entire
reading.

## 3. The repair: the decision is the structural key, and the table is printed in every state

Two changes, and the second is the one the gate session asked for.

**The decision is the structural key plus the reach guard.** `door_seq` present with `repair_seq`
absent is the sleeper arm; `door_seq` present with `repair_seq` present is the baseline arm; no
`door_seq` is the third state. The four behavioural keys are **reported and not scored** - which is
598's widening read one level further in: an image older than one of their instruments is still its
arm, and 520 is the measured case (it carries `repair_seq`/`sip_seq`/`pce_seq`/`wfi_seq` and **no**
`slot_cwe_` key at all, because its image predates that instrument).

**And every state prints the arm, with each key's kind, before the branch.** The branch choice was
the thing answered by silence - a reader could not tell "this log is the sleeper arm" from "one
behavioural key was present and the ladder was skipped" except by noticing that the rung lines were
missing, which is 609's shape at the level of the reader's own output. The table is six lines and the
verdict is one, and both the branch and the table are computed from the same six counts, so a table
that says one thing and a branch that does another is not expressible in this file.

```
  arm: which arm this log reads as, and on which keys. Each state is a count from this log, and
  the KIND is what an absence means: ...
    xnu_live_door_seq          present  reach guard (published on BOTH arms)
    xnu_live_repair_seq        absent   STRUCTURAL (entry_trace.c:1283)
    xnu_live_slot_cwe_*        present  behavioural (the exit wrapper)
    xnu_live_sip_seq           absent   behavioural (past cpu_idle's first test)
    xnu_live_pce_seq           absent   behavioural (the enter wrapper)
    xnu_live_wfi_seq           absent   behavioural (the WFI)
  => SLEEPER ARM (594's switch), decided on the two keys that can decide it: ...
     **AND A DISAGREEMENT: a behavioural key IS present in this log, and on this arm nothing
     gets into the window** ...
```

**The disagreement is a named state and not a silent branch choice.** A behavioural key present with
`repair_seq` absent is a log on which the two definitions disagree: the structural key says this
image skips the repair, the behavioural key says the window ran anyway. That can hold only if
`SIGPdisabled` was cleared by something other than the skipped repair, which this phase has never
seen - so the reader does not decide it, and says what it did instead: **the ladder still runs**,
because its witnesses (`door_seq`, `poll_seq`, `poll_timeout_ms`, the park's console group) come from
sites compiled into *both* arms, so they are readings of the machine and not of the image; and the
baseline arm's pop-death block is **withheld on that log**, with the pop's own keys named as the
thing that establishes which reading applies.

**And one printed claim had to be conditionalised, which is the sub-defect this repair would have
introduced.** The sleeper branch's opening paragraph asserts "SIGPdisabled stayed set, `cpu_idle` left
by its first door on every pass, and the window ... was never entered" - which is exactly what a
behavioural key falsifies. On the disagreement log the reader would have asserted the opposite of a
record it had printed two paragraphs above. That sentence is now made only when no behavioural key is
present; the disagreement state prints what it does establish instead. This is the project's
most-repeated shape (600: the fix to a printed claim left the comment above it asserting the old
thing) caught before it shipped rather than after.

## 4. The validation

**Every file on this host that mentions a live-channel key, through both readers** (`--summarise`,
classified by each reader's own words: the old one has no verdict line, so its branch is read off the
paragraph it prints; the new one prints a `=> ... ARM` verdict in every state):

| | files | same branch | reached the arm block |
| --- | --- | --- | --- |
| the whole candidate set | **183** | **181** | 176 |
| the two that differ | `/tmp/n.txt`, `/tmp/o.txt` - **this reader's own output saved to a file** (their first line is `== summarising /tmp/r604-bad-values.log ==`), not captures of a boot | old: baseline, silently; new: the table and the disagreement | 2 |

So **no log of a boot on this host changes branch**, and the change is not invisible either: on the
two files that are not logs the new reader prints the table and names the disagreement where the old
one took the baseline branch without saying so. The six archived captures with a live channel in them
are exact - 520 and 533 and the 576 pair take the baseline arm, 513's two take the sleeper arm - each
by both readers.

**The peer's fixture, on a real artifact**: §2's table, four readings. The two left-hand cells are
the defect reproduced on the generator the gate session handed over; the two right-hand cells are the
repair.

**The rehearsal** (`tools/rehearse_live_path.sh`): two fixtures and three rows, one per side of the
decision plus the defect itself.

* `slot-cwe-only` - the sleeper signature plus ONE behavioural key. The reader must print the
  disagreement, must keep the ladder's rung PASS (so the arm's reading is not lost), and **must not**
  print the baseline death block. That last half needed a new capability: a row can now carry
  `forbid:` expectations, and a row whose every expectation is a forbidding one is **refused**,
  because a reader that failed to run at all also "did not say" the thing (the guard the path cells
  carry, 615/616).
* `baseline-arm` - the same signature plus the STRUCTURAL key, where the death block *is* the reading.
* `predicted` gained a third expectation, the verdict line `=> SLEEPER ARM`, so the base case asserts
  the arm it takes and not only what it prints afterwards.

Battery **38 ok / 0 failed** (19 live-path + 15 reading + 4 path), exit 0, 0 stderr, on the frozen
bytes (`run_and_capture.sh` `8aad5d12…`, `rehearse_live_path.sh` `894c67bb…`) - and the harness is
thirty-eight cells because this step added two.

**And the new cells bind, measured by breaking each half of the repair on a copy** (the copy sits in
`stages/stage90/` behind `RUNNER_OVERRIDE`, which refuses to resolve to the live path, and the live
runner's sha was asserted `8aad5d12…` after every step, so the path a press fires was never the mutant
at any instant):

| mutation | result |
| --- | --- |
| the decision back to the six-term conjunction, table and all | live path **19 ok / 0 failed**, the reading **14 ok / 1 failed**: `slot-cwe-only` on **both** halves - *"the reader did not say: this arm did what it was built to do at the point that matters"* (the ladder was skipped, which is the defect) **and** *"said what this state must NOT say: from the pair of SCTLR readings the entry wrapper publishes"* (the death block ran instead, which is the harm) |
| the disagreement block deleted, decision unchanged | live path **19 ok / 0 failed**, the reading **14 ok / 1 failed**: `slot-cwe-only` on *"the reader did not say: AND A DISAGREEMENT: a behavioural key IS present in this log"* |

The live-path half is **unchanged in both**, which is itself the reading: this step moves the reader
and not the run. **The counts are section-scoped** - a section whose cells fail stops the harness
before the next one, so section C's four cells did not run under either mutation and the two lines
cannot be quoted as "the rest stayed green"; that was measured once, in the 38/0 baseline.

## 5. A finding the census made on the way: the fixture's live records were not the artifact's shape

Chasing the population for §4 turned up something about the harness rather than the reader. A live
record in a **real capture** is ` xnu_live_poll_seq=0x00000001` - a leading space, the key, the value,
and no `MI4IOS6_STAGE90` anywhere in the line - because `entry_live_write` hands the key straight to
`entry_write_kv`, which writes exactly that (`entry_stubs.c`: `:2419` and `:2499`). Only the payload's
own *result* records carry the `MI4IOS6_STAGE90_XNU loader_` prefix. The rehearsal's fixtures prefixed
the live records too, and **it never mattered**: every pattern in the reader is a loose substring
match, so both shapes read the same.

**It very nearly did matter.** This step's first design asked whether the payload *record* carried
each arm key - a stricter pattern, which looks like the more careful reading and is the opposite. On
that form the fixtures stay green (their live records carry the prefix) while **every real capture**
- 520, 533 and 513's pair all carry the keys bare - would have lost `door_seq` and been read as "the
boot never reached the idle", the third branch, on every log this project has. The fixtures are now
built in the writer's shape, and the reason they were not is written beside them, because a fixture
that is not the artifact's shape cannot falsify a pattern change - it can only agree with itself.

## 6. What this does not do

* **It does not touch the device, the arm, the payload or any parked byte.** `out/stage90/stage90-qcdt.img`
  is still the sleeper arm, the press is still the user's, and nothing here writes to storage.
* **It does not make the log able to identify the image.** The arm is still the gate's reading
  (`out/`'s build record) and this block reads the machine; a log cannot tell the sleeper arm from an
  image that predates the repair, and the branch's own text says so.
* **It does not decide the disagreement state.** It names it, keeps the ladder running, and withholds
  the death block. A run that produces one is a run this phase has never seen, and reading it is the
  next step's problem rather than this one's.
* **It leaves the gate's narration of the runner's test stale, and that is the gate session's file.**
  `preflight_boot_check.sh` quotes the six-term conjunction and the runner's `:779-784` in its
  `which arm the entry image in out/ is` block (622's own sentences). After this step the test is two
  keys and the lines have moved. **Reported to that file's owner rather than edited here** - and the
  gate itself is unchanged by this step: re-run on the parked arm it is `EXIT=0`, 610 lines, 0 stderr,
  the same reading as 620 took, because its census is over the runner's exit vocabulary and this step
  adds no exit.
* **It does not advance 「起码要能进入操作系统，把基础驱动跑起来」.** The device is off the bus (`adb
  devices` and `fastboot devices` both empty); the one event that can move the goal is the user's
  **Vol-Down + Power**, and what this step changes is what the run after it is allowed to conclude.

## 7. Safety

No device action, no `fastboot`, no `adb`, **nothing written to storage**, no boot, no build. One
script edited (`stages/stage90/run_and_capture.sh` - the arm test, the table, the disagreement state,
the conditionalised paragraph, and the four comments that described the old conjunction), one
rehearsal extended (`tools/rehearse_live_path.sh` - two fixtures, three rows, `forbid:` expectations
with their refusal, and the live records' shape). Read-only device calls only: `preflight_boot_check.sh
--allow-xnu-entry` (`EXIT=0`, 610 lines, 0 stderr) and `adb devices` / `fastboot devices` (both
empty). `fastboot boot` only - never `flash` - so no outcome of any of this can write to storage.
