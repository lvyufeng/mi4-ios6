# 670: the catcher cannot fire the arm the press is owed for — the fourth copy of one missing definition, and the first that costs the press

668 pinned the arm at the runner and 669 made the next press's answer a number. Both were about *my* side of
the press. This step asked the other question — **what fires the press** — and the answer is that the one
program on this host that fires it cannot fire the arm that is owed one.

Two lines, both measured host-side on the live tree, neither touching a device. This is the fourth instance of
one family: *the `--allow-*` set a run must be given is a function of the arm's own switches, and the arm's
name is a property of the bytes.* Three copies went stale harmlessly; this one is the file that spends the press.

## 1. The two lines, as they fire

`press-watcher.sh` (`/home/lvyufeng/.claude/jobs/ddfef593/tmp/`, `run-experiment-526`'s lane — read, never
edited) fires the gate and then exactly one runner:

```
:287        if ! ./preflight_boot_check.sh --allow-xnu-entry >>"$LOG" 2>&1; then
:288          say "GATE REFUSED - nothing booted, nothing spent. Read the gate output above."
:289          exit 1
:291        say "gate green; booting"
:292        ./run_and_capture.sh --allow-xnu-entry >>"$LOG" 2>&1
```

Run as written, on `out/` as it stands (`armed-selftest-wdog-ef0361a2`, 666's arm):

```
$ ./preflight_boot_check.sh --allow-xnu-entry
GATE EXIT=1
REFUSING: the hardware-watchdog SELFTEST skips the normal boot path and spins until a net reboots it;
          needs --allow-hw-watchdog-selftest

$ ./run_and_capture.sh --allow-xnu-entry
RUNNER EXIT=1
the bytes to be sent are the recorded set 'armed-selftest-wdog-ef0361a2'
run_and_capture: --expect-arm=<set> was not given, and this run would SEND an image. …
```

**The first line ends the watcher.** `:287` is an `if !` around the gate, and the branch it takes is
`exit 1` — so a press arriving now is answered with `GATE REFUSED` and the watcher is **gone**. Nothing is
sent (the refusal is before any boot), so the *press* is not spent; but the cover is, and the owed arm cannot
be fired by this catcher at all. That is not a hold: `ambiguous()` and the adb-state arm hold and re-fire, and
the gate arm exits. The distinction is the one 651/m658 drew for the *relay*, and here it is in the watcher.

**The second line is the one that would spend the cover.** It sits after `say "gate green; booting"` at `:291`,
and 668 made the runner refuse to send anything without a declared arm. So on a tree where the gate *did*
pass, `:292` would be refused before the gate — after the commitment line — which is the one ambiguity class
that spends the cover rather than preserving it.

Neither line is wrong about a *device*. Both are wrong about *this arm*, which is why no gate clause, no
readiness row and no rehearsal caught them: they are correct for the arm the catcher was last edited against.

## 2. Why this is the fourth, and why only this one mattered

| the copy | the arm it went stale against | what it cost |
| --- | --- | --- |
| `tools/verify_press_ready.sh` (repaired 667) | 666's self-test arm | its gate row was **red on a good tree**, and its FAIL reason quoted the gate's *passing* line (m679) |
| `tools/rehearse_live_path.sh` (repaired 668) | 666's self-test arm | **0 ok / 20 failed** — and uniformly red, so it read as "the tool is stale" and stopped being read, which is what let the third one survive |
| the armed scratch launcher (repaired, not installed, 668/669) | 666's self-test arm | a press fired by it is spent on a gate refusal; its readiness call also named the **superseded** acting arm |
| **`press-watcher.sh` `:287`/`:292`** | 666's self-test arm | **the watcher exits 1 on a gate refusal**, so the owed press cannot be taken at all — and its runner line would consume the cover if the gate were ever satisfied |

The single definition they were all missing is now two tools, and both were already committed before this
step found the fourth caller: `tools/gate_flags_for_arm.sh DIR` (which flags this arm demands, with the gate's
own vocabulary as the bound) and `tools/resolve_arm_set.sh DIR` (which recorded set these bytes are, by hash).
So the repair for the catcher is not a new constant — it is two calls:

```
FLAGS=$(tools/gate_flags_for_arm.sh out/stage90)          # one flag per line
ARM=$(tools/resolve_arm_set.sh out/stage90 | cut -f1)
./preflight_boot_check.sh $FLAGS
./run_and_capture.sh      $FLAGS --expect-arm=$ARM
```

For the arm in `out/` that expands to `--allow-xnu-entry --allow-hw-watchdog-selftest` and
`--expect-arm=armed-selftest-wdog-ef0361a2`.

## 3. The sweep, so this is the last one rather than the next one

Finding a fourth copy raises the question of a fifth, so every file on this host that could fire the gate or
the runner was enumerated and classified by whether it **invokes** them or merely **mentions** them:

| what the sweep found | verdict |
| --- | --- |
| `press-watcher.sh` `:287`, `:292` | **invokes both — the defect above** |
| `press-watcher.sh` `:296` (`--summarise`) | invokes; correct, and unaffected (the reader needs no arm) |
| `press-watcher.v5.sh`, `v6.sh`, `v6.frozen.sh`, `loose.sh`, `parked-ops-20260923/press-watcher.sh` | superseded copies, same two stale lines — **not fired by anything**, and left alone. A superseded copy sitting beside a live one is a hazard of its own (the lesson of the 644 bundle), but these live in the peer's job directory and deleting another lane's files is not this lane's call; they are *named* here so a later reader does not patch the wrong one |
| `harness2.sh`, `t_pin.sh`, `unread_test.sh`, `armblock2.sh`, `nomarkers.sh`, `relay-nomark.sh`, `watch-phone.sh`, `after-press-report.sh` | mention the runner in prose or in a `--summarise` instruction; **invoke nothing** |
| `apply-gate-repairs.sh:52` | invokes the gate with `--allow-xnu-entry` — a *staging applier*, not the press path, and it is the peer's tooling; it carries the same staleness and is recorded here for completeness, not raised |
| `~/.claude/scheduled_tasks.json` | **absent**; the live job is held by the session's scheduler and not by a file at that path (637's third item, re-measured) |

So `press-watcher.sh` is the only firer of the press, and it is the only one that was still carrying the
constant.

## 4. What this changes about the press, and what it does not

* **It does not change the arm, the gate or the payload.** `out/` still holds `armed-selftest-wdog-ef0361a2`,
  the gate still exits 0 under the derived set, and nothing was sent, built or written — the two commands above
  are host-side reads of a tree the gate already reads.
* **It makes the press's blocker list longer by one item, and that item is the only one that was invisible.**
  The three known blockers — the neighbour `33e80afe` off the bus, a fresh arming (none alive since
  2026-09-24 17:24:31, and the re-arming waits on the peer lane's two catcher-side items) and the operator's
  power press — all presupposed that the arming *would work*. It would not have: the fresh arming of the file
  as it stands is a watcher that exits 1 the moment the phone is ready.
* **And the item is cheap now**, which is the point of having extracted the definition first: two command
  substitutions, in a file whose owner has the tools on disk and a message naming both lines.

## 5. Safety, and what this does not do

Host-side only. The two commands in §1 are the gate and the runner invoked **without any device present** (both
device lists are empty, and the readiness verdict is red on `the press would be caught`): the gate reads five
files, the runner refuses at the arm step before it reaches the gate at all, and neither sends anything. No
`fastboot`, no `adb` beyond the list reads the readiness tool has always made, no boot, **no byte under `out/`
written**, `fastboot boot` only and never `flash`, nothing written to storage, the neighbour `33e80afe`
untouched. The peer's file was read and not edited: the message carries the measurement and the replacement.

**It does not advance 「起码要能进入操作系统，把基础驱动跑起来」.** The frontier is where 652 left it — XNU
reaches pid 1, runs the userland phase, dies at the idle exit's `pop {fp, pc}` — and this step produces no boot
and no reading: it removes the reason the *next* press could not have happened, which is a prerequisite for the
reading and not the reading. **TWRP-to-storage stays withheld**, because 「如果os已经能进去了的话」 is unmet.
