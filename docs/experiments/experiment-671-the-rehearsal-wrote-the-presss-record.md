# 671: the rehearsal wrote the press's own record — a stub's line at the production path, and a pin a hash could not have made

The press is armed and waiting for the operator's hands (670 §4: the launcher is live from 06:55:41 UTC
to ≈12:55:41). This step asks the question the wait makes askable and nothing in the press path asks:
**what is at the paths the press will write its record to, right now?** The answer was a rehearsal's
stub output, and the reason is the same one 668, 669 and 670 kept finding in different files — a value
with two definitions, and the copy that gets written down is the one that goes stale.

Nothing was fired, nothing was sent, no device was touched, and **no byte under `out/` was written**.

## 1. The measurement

```
$ stat -c '%n %s %y' /tmp/r654/gate.log /tmp/r654/run.log
/tmp/r654/gate.log 9  2026-09-25 06:13:38.374182626 +0000
/tmp/r654/run.log  11 2026-09-25 06:13:38.382182410 +0000
$ od -c /tmp/r654/gate.log ; od -c /tmp/r654/run.log
0000000   g   a   t   e       r   a   n  \n
0000000   r   u   n   n   e   r       r   a   n  \n
```

Nine bytes and eleven. Those are not a gate and a runner — they are the **scratch stubs'** two lines,
`printf 'gate ran\n'` and `printf 'runner ran\n'` (`rehearse-v3.sh`'s `mkstate`), sitting at the exact
paths the real gate and the real runner are redirected to (the launcher's lines 173/175/177 and
186/190), written **42 minutes before the launcher was armed** (06:13:38 vs 06:55:41).

The mechanism is three lines of the launcher. It named **one** of its three log paths by env:

```
LOG=${PRESS_LOG:-/tmp/r654/press.log}          # overridable (line 52)
... > /tmp/r654/gate.log 2>&1                  # HARD-CODED (173, 175, 177)
... > /tmp/r654/run.log  2>&1                  # HARD-CODED (186, 190)
```

and the harness overrode exactly the one that was overridable (`PRESS_LOG=$ROOT/state/press.log`), so
**every one of the 20 scratch states wrote the press's production record** with its stubs' output. A
rehearsal was able to write the thing it is a rehearsal *of*.

## 2. Why this is worth a step, when the real fire truncates both files anyway

It is, and that is the argument *for* the fix rather than against it: `>` truncates, so the press is
never at risk. What is at risk is **the reader**, in the window before the fire and after a deadline
that passes with none. Someone opening `gate.log` to ask *"did the gate run?"* gets a nine-byte file
that answers **yes**. A real gate's answer is 39 000 bytes of five checks, and a real runner's refusal
is a paragraph — but a 9-byte line is a *plausible* gate output, and nothing in it says "stub".

That is m679's shape, one artifact over: there the report's FAIL reason quoted the gate's **passing**
line, so a red row read as a green one; here a rehearsal's stdout reads as a production result. Both
are *a record that names the wrong producer*, and in both the fix is to make the producer structural
rather than to add a sentence.

## 3. The fix: all three paths by env, and printed

`press-on-clear.v4.sh` (sha256 `0981d8f6…`, scratch, staged — v3 is the armed one and its bytes did not
move, `254c69b1…` unchanged). Diff v3 → v4, code lines only:

```
-mkdir -p "$(dirname "$LOG")" 2>/dev/null || true
+GATE_LOG=${GATE_LOG:-/tmp/r654/gate.log}
+RUN_LOG=${RUN_LOG:-/tmp/r654/run.log}
+for _d in "$LOG" "$GATE_LOG" "$RUN_LOG"; do mkdir -p "$(dirname "$_d")" 2>/dev/null || true; done
+say "  press log $LOG"
+say "  gate log  $GATE_LOG"
+say "  run log   $RUN_LOG"
... > "$GATE_LOG" 2>&1 / > "$RUN_LOG" 2>&1        (the five hard-coded sites)
```

The defaults are unchanged, so the live behaviour is identical; what changed is that a *test* can point
them somewhere else. **And the start block prints all three**, because a launcher whose log went
somewhere else must say so in the one place a reader always looks.

**A defect this diff caught in itself.** The first attempt applied the substitution by plain
`str.replace` over the whole file — which also rewrote the two default lines it had just inserted,
producing `GATE_LOG=${GATE_LOG:-"$GATE_LOG"}`: under `set -u` an unbound self-reference that would have
made every arming die at line 62. The check that caught it was reading the diff rather than the exit
status (`bash -n` passed; so did the first file copy). An edit on a prefix deleting the suffix, in the
same sitting as the edit — [[mi4-an-edit-on-a-prefix-deletes-the-suffix]] with the prefix and the
suffix on adjacent lines.

## 4. The row, and the pin that a hash could not make

The harness now pins the two production paths **before any case runs** and re-reads them after the
whole battery, and the row is `Z1-press-record-untouched`. The signature is a **nanosecond mtime and a
hash**, and the reason is measured rather than argued: on the first run against v3, the two paths were
rewritten with the *byte-identical* content they already had —

```
before: /tmp/r654/gate.log ... 06:13:38.374182626 ... c44e985dc848500f...
after:  /tmp/r654/gate.log ... 07:07:22.663230709 ... c44e985dc848500f...
```

same size, same sha, only the clock moved. **A hash-only pin would have certified that write as "no
change"** — a verdict taken across a write, which is m662's rule and its second measured instance. The
row measures *"was this file written"*, because that is the claim.

And a second row, `A1-wrote-its-own-siblings` (`Z2` in the harness's comment), asserts the scratch
siblings **were** written with the stubs' lines. Without it, `Z1` green would be a silence: "the
production paths did not move" is only evidence if the launcher wrote its siblings *somewhere*.

## 5. The falsification: one row moves, and only that row

Same harness, same file under test as a parameter, two launchers, and the script's sha printed in both
runs (669 §4's lesson):

| driven | result | the two rows |
| --- | --- | --- |
| `press-on-clear.v3.sh` (hard-coded) | **25 ok, 2 failed**, exit 1 | `Z1` **FAIL**: the paths went `ABSENT` → created (07:07:47.947251196). `A1-wrote-its-own-siblings` **FAIL**: the scratch siblings were never written |
| `press-on-clear.v4.sh` (env-named) | **27 ok, 0 failed**, exit 0 | `Z1` **ok**: both paths `ABSENT` before and after. `A1-wrote-its-own-siblings` **ok**: `gate ran;runner ran;` in the scratch state dir |

The 25 rows that were green before are green in both, so the change moved exactly the two rows it is
about — and the two are the *same* defect seen from its two sides (the launcher wrote the wrong file /
the harness wrote the right one).

## 6. What was cleared, and what is left

The two production paths are now **absent** — `rm -f` after the v3 run proved they held recorded-stub
content and nothing else. Absence is unambiguous where a nine-byte line is not: `ls` failing is *"no
gate has run"*, and the first thing the press writes will create them. The evidence for the file's
earlier content is quoted verbatim in §1 and kept in `/tmp/g668/evidence-671/`
(`prod-paths-before-v3-run.txt`, `harness-v3.txt`, `harness-v4.txt`).

**v4 is staged, not installed**, with 669 §6's reasoning unchanged: the arming command names a path, and
the armed launcher is v3. The next arming should use v4; the diff in §3 plus v3's recorded sha
(`254c69b1…`) is the whole change, so the bytes are re-derivable if the scratch directory is lost.

## 7. Safety, and what this does not do

Host-side only, and deliberately so while the launcher is armed: the rehearsal's only `sudo`,
`fastboot` and `adb` are the stubs in `/tmp/g668/fake/bin`, asserted to resolve there before any case
runs; case `C1` reads the real tree's real tools and real bytes and stops in the wait; **nothing under
`out/` was written, the gate and the runner were not edited, `xnu_arm_boot/**` was not touched** and no
device was addressed — both device lists were empty throughout (`fastboot devices` `[]`, `adb devices`
header only), so the neighbour's serial `33e80afe` was untouched. `fastboot boot` only, never `flash`;
nothing written to storage.

**It does not advance 「起码要能进入操作系统，把基础驱动跑起来」.** The frontier is where 652 left it —
XNU reaches pid 1, runs the userland phase, dies at the idle exit's `pop {fp, pc}` — and this step
produces no boot and no reading of the device. It protects the *reading the owed press will produce*,
which is the prerequisite 663 §3.1 set and the one item of the goal that is inside this lane's reach
while the other two wait on the operator and on the peer. **TWRP-to-storage stays withheld**, because
「如果os已经能进去了的话」 is unmet.
