# 672: the wait asked a question of its own — the armed window could end on a state the verdict calls transient

671 cleaned the record the press will write. This step asked the next question in the same wait — **what
does the launcher do while it waits, and what ends the window** — and the answer is that the launcher had
its own copy of a definition it already had a tool for, and that its response to the tool's refusal was
to give up.

Nothing was sent, no device was addressed, no byte under `out/` was written, and the gate and the runner
were not touched.

## 1. Two answers to one question, measured on five device states

The launcher's wait loop breaks on a predicate of its own:

```
if ! printf '%s' "$fb" | grep -q '33e80afe'; then                     # the neighbour is gone, and
  if printf '%s\n%s' "$fb" "$ad" | grep -q '^4a2fe00b'; then          # this phone is on the bus
```

and the verdict that actually authorises a fire is `tools/verify_press_ready.sh`'s row 5, `the press
would be caught`, which the launcher consults *after* the wait. So there are two predicates for one
question. They are not the same, and the difference was measured with the **real** tool on the real tree
with the device lists supplied by a stub (read-only in every direction; the tool writes nothing):

| the device state | row 5's verdict | v4's trigger |
| --- | --- | --- |
| `fastboot` lists `4a2fe00b` **alone** | **0** — a run fired now clears the ambiguity guard | broke the wait ✓ |
| `adb` lists `4a2fe00b` as **device** | **0** — `usable()` is true, the run takes the adb branch | broke the wait ✓ |
| `adb` lists `4a2fe00b` as **unauthorized** | **1** — *"Accept the 'Allow USB debugging' prompt"* | **broke the wait** ✗ |
| `fastboot` lists `4a2fe00b` **plus a third device** | **1** — the ambiguity guard would refuse and boot nothing | **broke the wait** ✗ |
| neither list names the phone | 1 | kept waiting ✓ |

The two rows that diverge share a shape the file's own header names: **one value, two definitions.** The
launcher re-derived "would a press be caught" from the two device lists it happens to have in hand,
instead of asking the tool whose job that is — the fifth time in this project that a definition copied
into a second place went stale against the copy that acts (670 §2's table, extended by one row whose
stale-against is not an arm but a *state*).

## 2. What the divergence cost: the window, not the press

`v4`'s response to a red verdict was `exit 1`. So on either divergent state the launcher **ended the
armed window without a press having been spent**, and one of the two is *transient*: `adb ...
unauthorized` clears by itself when the operator accepts the prompt on the phone's screen — which is
exactly what the tool's own message tells them to do. The honest reading of that state is "not yet", and
the launcher read it as "never".

The other half of the same conflation is the give-up message, and it is a second defect in the same
loop:

```
GIVING UP: 33e80afe is still in fastboot after 21600s. Nothing was sent; both phones untouched.
```

`cleared` is set only when *both* halves hold, so when the phone simply never appeared the message still
printed a claim **about the neighbour** — a claim it had not read. This is the family 670 §1 and m679
live in: a record that names a cause it did not measure, and here the named cause is one the operator
would act on (unplugging a phone that was never plugged in).

## 3. The repair: the authority decides, and a refusal is not the end

`press-on-clear.v5.sh` (sha256 `7f23cae5…`). The loop now reads:

* **the cheap poll decides only *when to ask*.** `33e80afe still in fastboot` and `4a2fe00b in neither
  list` are liveness messages; the moment the phone is on the bus, the launcher asks the verdict.
* **readiness decides *whether to fire*.** It is run before the gate exactly as 661 R1 requires, and it
  still blocks the fire — nothing is sent while it says no. What changed is what happens next.
* **a refusal ends the fire, not the window.** The launcher says so in those words, names where the
  reasons are (the five rows it just printed, each of which says what to do about it), and goes back to
  waiting.
* **the verdict is cached on the device lists**, because readiness row 5 is a function of exactly those
  lists and the tree is frozen by the armed closure — so re-asking an unchanged state cannot change the
  answer. It is re-asked when the lists change, and anyway every 180 polls, so a tree that moved under an
  armed launcher is still caught.
* **the arm declaration is re-read inside that path too**, so the R4 pin still holds on every attempt
  rather than once at the end.

And the give-up message measures both halves before it names either:

```
GIVING UP after 21600s. Nothing was sent; both phones untouched. Measured at the deadline:
  neighbour 33e80afe: not in the fastboot list
  this phone 4a2fe00b: in neither list - it was never put into fastboot (or was not reachable).
```

One more line, from 669 §4's lesson one file over: the start block now prints **the launcher's own
sha256**, so a log says which bytes armed it and not only which tree. The swap in §5 is the first log
that carries it.

## 4. Falsifications

| what | how | what it printed |
| --- | --- | --- |
| the whole branch table | `rehearse-v3.sh` (the 27-row harness) against **v5** | **29 ok / 0 failed**, exit 0 — the same 27 rows as before plus two new `A6` rows |
| the new rows are about the change | the same harness against **v4** | **26 ok / 3 failed**: `A6-readiness-refuses` (did not say *this window stays armed*), `A6-window-survived`, and `A6-old-refusal-gone` (v4 still prints the old sentence) |
| the five device states | `rehearse-wait.sh` against **v5** | **10 ok / 0 failed**: S1/S3 fire (gate and runner called once each, exit 0), S2/S4 do not fire and the window survives, S5 never asks |
| the same five, per state | the same harness against **v4** (its record mode) | **8 ok / 0 failed**, and the two rows that differ are the finding: S2 and S4 print `window=no`, and S5 prints the false neighbour claim |

**The harness was wrong first, in the shape this file keeps finding.** Its first run reported
`window=yes` for v4 on S2 — the opposite of the truth — because the launcher appends to its log
(`tee -a`) and the per-state log had not been cleared between the v5 run and the v4 run, so v4 was read
with v5's line still in it. A row *taken across a write*, and the fix is one `rm -f` before each drive.
It is recorded here because the harness's own bug produced a **green** row for the launcher that has the
defect, which is the direction that survives review.

## 5. The swap, and a near miss worth writing down

`v3` was stopped and `v5` armed, both host-side and with nothing on the bus (both device lists empty
immediately before):

```
[killed]  pid 3744476 (+ child 3744479), the v3 launcher, by explicit pid
[armed]   pid 426955, 2026-09-25 07:21:30 UTC, budget 21600 s -> deadline 13:21:30 UTC
          /tmp/g668/press-on-clear.v5.sh sha256 7f23cae5fb4edf21159d6241606f203c4320eed59641d207fb9a641ec6c485e6
```

**And the near miss.** Clearing a stray rehearsal process, this lane ran `pkill -f
'rehearse-v3.sh'` and then `pkill -f 'press-on-clear'` — and `pkill -f` matches any process whose
**command line** contains the pattern, including the shell that is running the `pkill`. The shell's own
command line contained the pattern, so the shell was signalled and died before the second `pkill` ran:
exit 144, no output, and the armed launcher (pid 3744476 then) was **never signalled**. It survived by
the order the signals happened to land in, not by design — `pkill -f 'press-on-clear'` is a pattern that
matches the armed firer, and one that had run would have ended the window silently. **Kill by explicit
pid.** The stray itself was inspected before it was killed (`/proc/<pid>/environ`: `MI4_REPO` pointed at
the scratch tree, all three logs under `/tmp/g668/state/`, and its `sudo`/`fastboot`/`adb` were the
stubs), which is why it was safe to kill and safe to have left running for the two minutes it lived.

## 6. Safety, and what this does not do

Host-side only. The measurement of §1 ran the **real** `verify_press_ready.sh` with a stub on `PATH`
serving the two device lists: the tool writes nothing (checked: no redirection, no `mktemp`), the boot
device was never addressed, and both device lists were empty on the live bus throughout. The rehearsals
ran a scratch tree whose gate and runner are stubs that record being called, with `sudo`, `fastboot` and
`adb` asserted to resolve into `/tmp/g672/fake/bin` before any case ran. **No byte under `out/` was
written, the gate and the runner were not edited, `xnu_arm_boot/**` was not touched.** `fastboot boot`
only, never `flash`; nothing written to storage; the neighbour's serial `33e80afe` untouched.

**It does not advance 「起码要能进入操作系统，把基础驱动跑起来」.** The frontier is where 652 left it — XNU
reaches pid 1, runs the userland phase, dies at the idle exit's `pop {fp, pc}` — and this step produces no
boot, no reading and no arm. It keeps the *reading the owed press will produce* reachable, which is what
the window being armed is for, and it removes two ways the window could end without one. **TWRP-to-storage
stays withheld**, because 「如果os已经能进去了的话」 is unmet.
