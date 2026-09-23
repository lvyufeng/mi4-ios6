# 634: two files decide one question about the fastboot list, and nothing checked that they agree

The owed press runs one command that can leave the phone parked in fastboot having booted nothing, and
two files decide whether that command is allowed to act: `run_and_capture.sh`'s
`fastboot_pinned_only()`, which is the guard, and `press-watcher.sh`'s `ambiguous()`, which is the
catcher's pre-check and the sentence the operator is shown. They are not the same predicate. The
catcher's narration, one screen above its own use of it, *states* that they agree -

> A press now cannot produce a run: the runner refuses at its FB_AMBIG_WAIT guard

- which is a claim about a **different file**, written in a comment, next to no check. That is 604's
class and 626's (a claim about a peer's file is a claim about that file's future), arriving on the
press path itself, where the cost of being wrong is a spent press.

**Measured: they agree on every shape this host can produce, and diverge on six it cannot.** The
divergence is real, latent, and now checked rather than asserted.

## 1. The two predicates, verbatim

```sh
# run_and_capture.sh - the guard that decides whether the boot step acts
fastboot_pinned_only() {
  local list=$1 n
  n=$(printf '%s\n' "$list" | grep -c . || true)
  [[ ${n:-0} -eq 1 && $list == "$SERIAL"$'\t'* ]]
}

# press-watcher.sh - the catcher's pre-check
ambiguous() {
  local fb other
  fb=$(sudo -n fastboot devices 2>/dev/null)
  other=$(printf '%s\n' "$fb" | grep -v "^$SERIAL" | grep -c '[^[:space:]]')
  [ "$other" -gt 0 ]
}
```

One **counts lines and demands the tab**; the other **counts offending lines**. They are the same
question ("is the list this phone alone?") asked two ways, and the ways differ wherever
"one line" and "no other line" are not the same set.

## 2. The matrix, measured

Both functions were extracted from the real files (awk counting braces, not `sed` ranges - the
watcher's `fastboot_has()` is a one-line function whose `}` is not at the start of a line, so a range
extractor swallows the rest of the file, which is a defect the watcher's own fixture already records)
and evaluated over ten `fastboot devices` strings with `sudo` stubbed. `usable()` is the third
column, because the catcher fires only when `usable && !ambiguous`.

| `fastboot devices` says | runner pinned? | watcher ambiguous? | usable? | verdict |
| --- | --- | --- | --- | --- |
| phone alone | yes | no | yes | **agree** - fire, runner boots |
| neighbour alone (**today, 2026-09-23**) | no | yes | no | agree - hold |
| both, phone first | no | yes | yes | agree - hold |
| both, neighbour first | no | yes | yes | agree - hold |
| empty list | no | no | **no** | diverge - *inert*: the catcher cannot fire |
| phone, the same line twice | no | no | yes | **diverge - press-spending** |
| phone + a serial carrying the phone's prefix | no | no | yes | **diverge - press-spending** |
| only a serial carrying the phone's prefix | no | no | yes | **diverge - press-spending** |
| phone, no state column | no | no | yes | **diverge - press-spending** |
| phone + a whitespace-only line | no | no | yes | **diverge - press-spending** |

**Five of the six divergences are in the expensive direction**: `usable()` is true and `ambiguous()`
is false, so the catcher fires - and the runner then refuses. And they agree on all four shapes this
host actually produces, which is why nothing has ever noticed.

Two of the row labels are load-bearing and are not decoration:

* **"a serial carrying the phone's prefix"** is `4a2fe00bX`. `usable()` is true for it because the
  watcher's `fastboot_has()` is `grep -q "^$SERIAL"` - a **prefix** test - so the catcher would treat
  a device that is not this phone as the phone. The runner's `$list == "$SERIAL"$'\t'*` is anchored on
  the tab and refuses, which is why this is a narration defect and not a wrong boot: the guard that
  actually boots is the stricter one. It is also 619's shape (`grep -q "^$SERIAL"` matching every
  state column) one field over.
* **"phone, no state column"** is why the runner's tab matters at all. `fastboot devices` on this
  host prints `SERIAL<TAB>fastboot`, but the runner's own earlier presence checks
  (`grep -q "^$SERIAL"`) would pass on a bare serial while `fastboot_pinned_only()` refuses it.

**What makes a divergence cost a press, exactly.** The catcher writes its commitment line
(`gate green; booting`) **before** it invokes the runner, and that line is the relay's permanent stop
condition. So a fired-and-refused run does not just waste a run: it latches the relay, no successor
catcher is armed, and the press that bought it has produced a phone parked in fastboot.

**And none of the six is reachable on this host today**, which is the finding's honest size. This is a
latent divergence in two definitions of one value - the defect class this project has paid for most
often - not a hole that has been firing.

## 3. The check

`tools/check_ambiguity_predicates.sh` extracts both predicates from the real files and asserts the
whole matrix: `agree` on the four shapes this host produces, `diverge` on the six it does not. A
change to either predicate that moves the boundary stops the check instead of being discovered on the
morning of a press, and the divergence count in its tail is **counted, not written down** (it was
hard-coded in the first draft, which is this document's own subject arriving in the tool).

It refuses rather than passes when it cannot see: an unreadable runner or catcher, a function the
extractor cannot find, an extractor that ran past its last function, and - the one that matters most
here - an empty stub record, because a green table produced by predicates that never consulted the
stub is not a reading.

| falsification | mutation | result |
| --- | --- | --- |
| A | the runner's predicate widened past the tab (`$SERIAL*`) | **2 FAIL cells, REFUSING, exit 1** |
| B | the watcher's `ambiguous()` deleted | `REFUSING: could not extract ambiguous`, exit 1 |
| C | the watcher path does not exist | `REFUSING: … is not readable … It is not a pass.`, exit 1 |
| D | the watcher's `ambiguous()` widened (the empty list becomes ambiguous) | **1 FAIL cell, REFUSING, exit 1** |
| E | a watcher whose functions reach no `sudo` at all | `REFUSING: the sudo stub was never consulted`, exit 1 |
| anchor | the real files | **10 cells, exit 0** |

A and D are the two directions that matter: the check is sensitive on **both** sides, so it is not a
test of the runner wearing a comment about the watcher.

## 4. The press cycle, measured end to end while this was done

The step was worth taking only if the press still works, so the whole path was read at the same time,
with nothing touched:

| what | how it was read | reading |
| --- | --- | --- |
| the gate | `preflight_boot_check.sh --allow-xnu-entry` | **exit 0** - it accepts |
| the gate is inert | the same run with `sudo`/`adb`/`fastboot` stubbed on `PATH` | **the stub was never called**; the output is **byte-identical** (49,345 bytes) to the unstubbed run, so the gate issued no device command of any kind and the comment that says so is now measured |
| the gate writes nothing | `git status` before/after | unchanged |
| the catcher | `ps` + the arm lines in `press-watcher.log` | relay pid 4067419 (armed 14:24:53), **catch 1 of 16** armed 19:51:24 as pid 1344846 |
| the catcher's own bound | `for i in $(seq 1 720)` with `sleep 30`, from the watcher's **own** start | 6 h -> **2026-09-24 01:51:24**; the relay's `REARM_MAX=16` gives ~4 days of cover |
| the revision running | the sha the log recorded at arming vs the file on disk | relay `0b957358…`, watcher `840da45d…` - both **identical**, so the running processes are the revisions the log names |
| the press is unspent | `/tmp/cancro-last_kmsg.txt` | **absent**, so the relay's `spent()` is in its strongest form (if it appears, a run made it) |
| the previous catch | the log | `TIMEOUT: 4a2fe00b never appeared usable in 6h. Nothing was touched.` at 19:51:04 |

**And the one blocker, read the same way:** `sudo -n fastboot devices` lists **`33e80afe` alone**, and
`adb devices` lists nothing. So the catcher's PRECONDITION 1 is not met and its own narration says it
will hold and spend nothing - and if it did fire, the runner's FB_AMBIG guard would refuse after 60 s.
**A press taken now produces no boot, and the fix is physical: unplug `33e80afe`.**

## 5. A second thing found while looking: the index's own order checker is red

`tools/check_experiment_index.py` asserts that within each stage column the experiment numbers are
strictly unimodal, and it exits 1 saying "Every violation is printed with its line number, so the fix
is mechanical". **It is red on `master`: six violations, at `docs/experiments/README.md:504-513`**
(the `stage90` rows `568,567,570,571,578,577,576,574,572,569`, which descend and then ascend again
before the peak).

Two things were measured about it rather than assumed:

* **It pre-dates this step.** Lines 504-513 are byte-identical to `f24bc64`, and the same six
  violations reproduce at every one of the last thirty commits that touch the file.
* **Nothing runs it.** No script, no build step and no fixture invokes it; the only references are
  experiment checklists that quote its output as `ok: NNN row(s)`. So it is a check that is run by
  hand, was last recorded as green at 517 (`ok: 494 row(s) across 91 stage column(s)`), and has been red since without anyone acting on it - which is
  `[[mi4-silence-is-a-reading-only-if-success-is-silent]]` from the other side: not a check whose
  success is silent, but a **failure** that was.

**It is recorded here and deliberately not fixed.** The mechanical fix is not local: the column's
numbers run `…566 568 567 570 571 578 577 576 574 572 569 573 575 579 581…`, so restoring strict
unimodality means permuting a run of historical index rows, and the descending half is *history* -
the record of which row was prepended when. Rewriting the order of the record to make a tool green is
a change the next reader could not distinguish from a retcon, and it is not this step's subject. The
new row added here was placed so that it adds **no** violation.

## 6. What this does not do

* **It does not fix either predicate.** The runner is the tighter side and is the one that boots, so
  the divergence costs a fired-and-refused run and never a wrong boot. Repairing the watcher's
  `ambiguous()` to the runner's shape would be a change to a **live catcher** - the file the armed
  process was started from, and the file the relay re-reads at every re-arm - which 620 forbids while
  the catch is armed. It is a repair for after the press.
* **It does not make the two definitions one.** They live in two files that cannot share a function
  without one of them sourcing the other, so the honest end state is a **check**, not a merge.
* **It does not cover the watcher's own fixture.** `wtest.sh` (in the session's job directory, not in
  this repository) already crosses six adb states with three fastboot-list shapes; this check is the
  first thing that reads *both sides at once*, and it is version-controlled where `wtest.sh` is not -
  which is also its weakness, since its subject's revision is not pinned by anything here.
* **It does not touch the device, the arm, the payload or any parked byte.** `out/stage90/stage90-qcdt.img`
  is still the sleeper arm (`60063c47…`), still **UNRUN**.
* **It does not change the index checker's verdict**, which is still red for the reason in § 5.
* **It does not advance 「起码要能进入操作系统，把基础驱动跑起来」.** The device is off the bus; the only
  event that can move the goal is the user's **unplug `33e80afe`, then Vol-Down + Power**.

## 7. Safety

No device action, no boot, no build, no `fastboot`, no `adb`, **nothing written to storage**. Every
command run was a read: two shell files, `git`, `ps`, the watcher log, and `fastboot devices` /
`adb devices` as enumerations - the same two reads the armed catcher makes on its own 30 s poll, and
neither acts on a device. The gate was run twice, once with `sudo`/`adb`/`fastboot` stubbed and once
without: the two outputs are byte-identical and the stub was never called, which is the measurement
that makes the second run safe to have made. The arm at `out/stage90/stage90-qcdt.img` was hashed
(`60063c47…`) and is unchanged, as is its park copy under `out/stage90/frozen/armed-sleepless-696a0f39/`.
The one file added is `tools/check_ambiguity_predicates.sh`; `git add` by explicit path. `fastboot
boot` only - never `flash` - so no outcome of any of this can write to storage.
