# 635: the live catcher's header says a press is spent, its own loop says the press is held

The owed press has one precondition that is not met, and the operator's decision - press now, or unplug
`33e80afe` first - turns on a question the armed waiter answers two different ways **in its own two
places**. This step measures the blocker (it is worse than the last reading said) and records the
contradiction rather than resolving it by experiment, because the experiment's cheapest form has a side
effect that would disarm the catcher.

## 1. The blocker, measured now (2026-09-23 ~21:52, ~8 h after the last reading)

| what | how | reading |
| --- | --- | --- |
| the neighbour `33e80afe` | `sudo fastboot devices` | **the whole list**: `33e80afe<TAB>fastboot` |
| ...and `adb devices` | `sudo adb devices` | nothing attached |
| **how long it has been there** | its enumeration at `usb 3-3`, `18d1:d00d`, at dmesg 2255527 s against an uptime of 2268459 s | **~12,931 s = 3 h 35 m, continuously** |
| the phone `4a2fe00b` | `usb 3-10`'s last event, a `05c6:f006` disconnect at 2236289 s | **~32,169 s = 8 h 56 m ago** - port idle, PRECONDITION 2 met |

**The dwell is the finding.** The host log's measurement of this neighbour is *thirteen* fastboot
windows, **eleven of them under 14 s** and the longest 152.9 s, plus one that "began 2026-09-23 10:59:41
and was still open 47 min later". A 3 h 35 m window is **4.6× the longest previously recorded**, and it
is still open. So the rule the guard's own comment is written around - *the other device is usually in
fastboot for seconds* - is true of the population and false of the state the catcher is in right now,
and **waiting for it to clear is not a plan**. A press taken while it is there is a press taken against
a list that will not settle on its own.

## 2. And the two answers, in the same file

The armed waiter is `press-watcher.sh` v7 (`840da45d…`, the sha its own log recorded at arming). It
answers "may I fire while the neighbour is in fastboot?" twice:

**Its loop says: no, and nothing is spent.** The branch that runs is

```
elif ambiguous; then
  say "HELD and usable, but NOT firing: the fastboot list is ambiguous, so a run would be refused."
  show_fb
  say "      Unplug $NEIGHBOUR or wait for it to leave fastboot. The phone is held and nothing"
  say "      has been spent; this watcher fires the moment the list settles."
```

and `_was_amb` exists only to stop the narration repeating: **it is not part of any condition**, so the
loop re-evaluates `ambiguous()` every 30 s poll and fires as soon as the list is `$SERIAL` alone.

**Its header says: the press is spent.** In v4's rationale for putting the preconditions before the
press instruction:

> An operator reading top-down presses with the ambiguity still live, and **the press is spent** on a
> run the guard refuses.

**Both sentences are about the same file's own behaviour and only one can be true.** The loop is the
behaviour; the header sentence describes the state the v3 arm-time text left the operator in, written
in a section explaining why the text was reordered - and it was left behind when the hold branch made
it false. That is 604's class (*a claim in a comment is not a check*) with the comment's subject being
the file it heads, and it is 626's shape one layer in: the claim and the thing that decides it live in
the same file and still disagree.

**Which way it truly is, is a code reading and is recorded as one.** I did not test it, for the reason
in § 3. On the reading: **the press is held, not spent** - and the operator-facing text agrees with the
loop (the arm-time lines say "This watcher will NOT fire while that device is in fastboot - it keeps
waiting, spending nothing"), so the false sentence is in the rationale that only a reader of the source
sees. That is the low-severity direction, and it is still worth removing: the header is the paragraph
the next person reads when deciding what the hold branch is for.

## 3. Why it was not tested, and the side effect that makes the obvious test unsafe

The obvious test is a copy of the watcher in `/tmp`, with `sudo` stubbed and the list changed under it.
**Both of the cheap ways to build it are unsafe here:**

* **`STAGE` cannot be overridden.** It is a hard assignment (`STAGE=$REPO/stages/stage90`), not
  `${STAGE:-…}`, so an environment override does nothing and a copy fires the **real** gate and the
  **real** `run_and_capture.sh`.
* **The `sudo` stub is not enough of a net on its own.** With `sudo` stubbed, no `fastboot boot` can
  issue - but the real runner still runs, and it **writes `/tmp/cancro-last_kmsg.txt`**. That is
  exactly the marker `press-watcher-relay.sh`'s `spent()` reads: the relay would see it appear, exit 0
  with "a run was fired by some other means", and **never arm another catcher**. A test run for a
  comment would have disarmed the automatic catch, and the press this phase is waiting for would then
  need a hand-fired run.

So the resolution is a code reading, labelled as one, and the repair is deferred: the file must not be
edited while the catch it holds is armed (**620** - a running process reads the file at start, so an
edit produces a log that narrates a revision that is not the one in the memory).

## 4. The advice, which does not depend on resolving it

**Unplug `33e80afe`, then Vol-Down + Power.** Under the loop's reading the unplug is unnecessary (the
catcher would hold and fire when the list settles) and harmless. Under the header's reading it is
required. So the same action is correct on both readings, and a press taken on a disagreement between a
program and its own comment is a press spent on a coin.

And the catcher is in a state where it will not do the unplug's job for you: it holds, spending
nothing, and a hold has a deadline - the watcher's own bound is **6 h** (`for i in $(seq 1 720)` with
`sleep 30`) from its 19:51:24 arming, i.e. **2026-09-24 01:51:24**, and the relay's `REARM_MAX=16` gives
about four days from 14:24:53 before it prints `STOPPING` and nothing listens.

## 5. What this does not do

* **It does not repair the sentence.** The watcher is live and 620 keeps it that way until the press is
  spent; the repair is one deleted clause, and it is owed for after.
* **It does not test the hold.** See § 3 - the test's side effect is worse than the question.
* **It does not change the runner, the gate, the arm or any parked byte.** `out/stage90/stage90-qcdt.img`
  is still `60063c47…`, still **UNRUN**, and the record chain that says it is the sleepless arm was
  re-read: `xnu_arm_entry-config.txt` carries `STAGE90_XNU_IDLE_NO_SLEEP=1` and the entry hash
  `696a0f39…`, and the gate - which refuses a record whose hash is not the entry bin it is about -
  accepts at **exit 0**. The newest commit touching `xnu_arm_boot/entry_trace.c` is `6153ed4` (594, the
  sleepless arm itself) and the arm was built after it, so the armed bytes carry every repair in the
  tree. **597's two candidate repairs are still not built**, by 597's own reasoning: a rebuild now
  would replace the arm the press is for.
* **It does not advance 「起码要能进入操作系统，把基础驱动跑起来」.** The device is off the bus and the
  one event that can move the goal is physical.

## 6. Safety

No device action, no boot, no build, no `fastboot`, no `adb` **that acts on anything**, nothing written
to storage. The device commands were `fastboot devices` and `adb devices` - enumerations, the same two
reads the armed catcher makes on its own 30 s poll - plus `dmesg` and `/proc/uptime` for the dwell. No
copy of the watcher was run, for the reason in § 3; nothing in the session's job directory was
modified, and both catchers were confirmed alive (pid 1344846 watcher, pid 4067419 relay) after every
command. `fastboot boot` only - never `flash` - so no outcome of this step can write to storage.
