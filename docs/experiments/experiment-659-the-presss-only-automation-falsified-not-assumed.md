# 659: the press's only automation, falsified rather than assumed — and its success was silent

The press is blocked on one physical thing (the neighbour `33e80afe` parked in fastboot), and a watcher is
armed to fire it the moment that clears. That watcher is now the **whole** of the press's automation: if it
is wedged, nothing fires, no log says so, and the operator's unplug looks like a no-op. So it is measured
here rather than trusted — the same standard §3 of 658 applied to the envelope, applied to the automation.

Nothing was built, nothing was run against the device, no file the watcher fires was edited, and the watcher
itself was **not** touched (it is a running `bash` script: editing it in place is 620's hazard, because bash
reads its own script incrementally).

## 1. The falsification, and it needed two readings

`/tmp/r654/press-on-clear.sh` (pid **1951993**, `setsid nohup`, budget 604800 s from
2026-09-25 00:52:49). Sampled four times, 12 s apart:

| time (UTC) | its `sleep` child | `voluntary_ctxt_switches` |
| --- | --- | --- |
| 01:10:42 | 2536144 | 928 |
| 01:10:54 | **2536212** | 937 |
| 01:11:06 | **2537154** | 945 |
| 01:11:18 | **2537292** | 962 |

**Both columns advance and they are independent evidence.** The `sleep` child's pid changes every cycle (a
new `sleep 10` per iteration, so each poll completes and the loop goes around), and the context-switch
counter — which is monotonic per process and which the loop cannot fake — climbs by ~8 per iteration, which
is the two `sudo` forks plus their children. A loop wedged on its first `sudo` would show a *frozen* pid and a
frozen counter. It shows neither.

**Why the second reading was worth taking:** the pid alone is weak — at a random instant the loop is asleep
about 99 % of the time, so *any* snapshot of a live-looking loop will usually have a `sleep` child. The
counter is the reading that cannot be satisfied by a single lucky sample, and it is the one quoted above.

## 2. The defect the falsification exposed: the watcher's success is silent

The watcher's log held **two** lines, both from its own start, after 19 minutes and ~114 polls:

```
[00:52:49] watcher started (budget 604800s, poll 10s): waiting for 33e80afe to leave the bus
[00:52:49]   RE-ARM: the previous watcher expired its 1 h budget at 20:06:32 with the neighbour still parked
```

and that is **by design**: the script prints on a *condition changing* (the neighbour gone, the phone
missing, the budget expiring), so a poll that finds nothing new prints nothing. Which is fine for the
script and fatal for the artifact, because `press.log` is what the operator reads while waiting — and it
cannot distinguish

* polling every 10 s and waiting, from
* wedged on the first `sudo` since 00:52:49.

Both states produce exactly these two lines. **The liveness evidence existed only in `ps`, which nobody runs
while waiting** — [[mi4-silence-is-a-reading-only-if-success-is-silent]], in the one file the press's
automation is judged by. (630's rule is one step further out than usual here: the *check* is a process, not
an assertion, but the shape is identical — a check whose success is silent cannot be told from one that never
ran.)

## 3. The monitor: passive by construction, and selftested so both verdicts are reachable

`/tmp/r654/heartbeat.sh` (pid **2537421**, interval 60 s, launched 01:11:39). Once a minute it appends one
line to `/tmp/r654/heartbeat.log`: the cycle verdict, the context-switch counter's movement, the `sleep`
child's pid movement, and the watcher's own last line. Verdicts are `ALIVE`, `STUCK`, `GONE` (the watcher
exited) and a `presslog +N` note when the press log grows (the fire).

**It cannot fire anything, and that is a design constraint and not a side effect.** No `sudo`, no
`fastboot`, no `adb`, no runner — the script's whole interaction with the world is reading `/proc/<pid>` and
the two logs. A monitor that *could* fire would be a second press, and exactly one press is the standing
rule; so the monitor is deliberately unable to act.

**And its verdict function is selftested, because a check whose failure direction is unreachable is the
defect m663 recorded one step earlier this session.** `--selftest` runs the verdict against four frozen
snapshots — a frozen counter *and* frozen pid (must say `STUCK`), a moved counter (must say `ALIVE`), a moved
pid alone (must say `ALIVE`), and both moved — printing how many it exercised and refusing if any disagrees:

```
$ bash /tmp/r654/heartbeat.sh --selftest
selftest: 4 case(s) exercised, 0 failed
live reader check: vol(this shell)=9 sleep(child)=
```

**Measured, against the real target** — the first line it wrote after its baseline, which is the reading that
matters because it is the one that could have said `STUCK`:

```
[2026-09-25T01:12:40Z] ALIVE vol=980->1032 sleeppid=2537413->2538216 last_press_line=[[00:52:49]…]
```

52 context switches in 60 s ≈ 6 poll iterations × 8, and a new `sleep` child. So the watcher is polling, and
the log now says so in a file that can be read after the fact.

## 4. What this does not do

* **It does not advance 「起码要能进入操作系统，把基础驱动跑起来」.** No boot was observed; `poll_seq` still
  stops at 2 and `slot_post_calls` is still absent. The press remains the only step that can move it.
* **It does not arm, replace, or edit the watcher.** The armed script is untouched, there is still exactly
  one thing that can press, and no window was opened in which none is armed (the 655b lesson: no `pkill`).
* **It does not make the press happen.** It makes the watcher's state *readable*, which is a different job.
* **It does not detect a watcher that is polling but reading the bus wrongly.** Two shapes, named because a
  monitor that is trusted more broadly than it is checked is this project's most expensive defect class: a
  **total** `sudo` failure keeps the loop's counter moving (the heartbeat says `ALIVE`) but makes both lists
  empty, so the watcher prints *"neighbour gone, but 4a2fe00b is in neither list yet - waiting"* into its own
  log every ~5 minutes, which is visible; a **partial** failure (fastboot fails, adb works) makes the
  neighbour look absent, so the watcher would fire against a bus that still holds it — and that run is
  **refused at the runner's own ambiguity guard before anything is booted** (`FB_AMBIG_WAIT`, exit 1), so it
  costs the attempt and **not** the phone, and the press would then be owed again with the watcher spent.
  That is why the heartbeat's `presslog +N` note and the watcher's last line are both in every heartbeat
  line: the failure is visible in the two logs it quotes, and the remedy is a re-arm, not a re-think.
* **TWRP-to-storage stays withheld** — 「如果os已经能进去了的话」 is unmet.

## 5. Safety

No device action: no `fastboot`, no `adb`, nothing sent anywhere, **nothing written to storage**. The
readings are `ps`, `/proc/<pid>/status` and two `/tmp` logs; the only file written is
`/tmp/r654/heartbeat.log` (plus the monitor's own launch output). No source, no image, no byte of `out/`, and
**no edit to the watcher, the runner, the gate or the readiness tool** — all of them are fired or read by the
armed watcher by path, and the watcher is a running script besides. The heartbeat is a `/tmp` tool and this
document is its record. `fastboot boot` only, never `flash`.
