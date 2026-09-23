# 594: the arm that never sleeps, and a guard whose comment counted two of three statements

593 section 4 named a lever that does not have to win the `pop`: skip 514's one-shot repair and the
window is never entered, so `pop {fp, pc}` is never executed. This is that lever built — the switch,
its plumbing into the arm record, the clause that refuses a record and a body that disagree, and the
**reader** that has to be able to read the run before the run happens.

Building it also produced this iteration's defect, and it is the kind this project keeps paying for:
a comment that counts a set, and a set that had one more member than the comment did.

The frozen 574 arm is untouched (`914f45ac…` / `151425c4…` / `3bc72605…`) and unrun. TWRP stays
withheld: 「如果os已经能进去了的话」 is unmet.

## 1. The switch, and why its default is the arm that came back

`STAGE90_XNU_IDLE_NO_SLEEP`, 0 or 1, default **0**. At 1 the `#if` around 514's repair in
`entry_trace.c` compiles the call out:

```c
#if !STAGE90_XNU_IDLE_NO_SLEEP
        rb = entry_counter();
        cpu_signal_handler_internal(0);
        entry_note_repair(caller, rb, entry_counter());
#endif
```

The default is the baseline for a reason 594 did not have to invent: **a build that forgets the
variable is the arm every run so far has used**, not the one under test. An arm whose default is the
untested state is an arm that can be reached by an omission, and this phase has already spent runs on
an image that was not the one the record described.

The declaration of `rb` sits inside the same guard, because `-Wall -Wextra -Werror` is on and on this
arm nothing reads it. That sentence is load-bearing and it is also where the defect in section 3
came from.

## 2. The plumbing, and the half the first draft missed

| site | change |
| --- | --- |
| `build_entry.sh:369` | `IDLE_NO_SLEEP=${STAGE90_XNU_IDLE_NO_SLEEP:-0}`, with a `case` that refuses anything but 0 or 1 |
| `ENTRY_ARM_KEYS` | twelfth → **thirteenth**, and the prose that counts the list updated in the four places it counts |
| the arm-character check | `case` arm added, so a wrong value is refused before anything in `$OUT` is written |
| the `-D` list for `entry_trace.c` | `-DSTAGE90_XNU_IDLE_NO_SLEEP="$IDLE_NO_SLEEP"` |
| **the record writer** | `echo "STAGE90_XNU_IDLE_NO_SLEEP=$IDLE_NO_SLEEP"` |
| 514's clause in the build's own checks | three numbers that were pinned to `1` are now `$(( 1 - IDLE_NO_SLEEP ))` or the switch's own set |

**The record writer is the half the first draft missed, and the peer session caught it.** The key was
in `ENTRY_ARM_KEYS`, in `STUB_DEFINES` and in the `-D` list — and not in
`xnu_arm_entry-config.txt`'s writer, which is the only thing `preflight_boot_check.sh` reads. The
switch would have reached no record at all; the gate could not have gone loud; and it would have
printed a narration about an arm whose window is never entered over an image that never entered it.
That is 591's defect one layer out, and unlike 591's it is not even *recordable* — the arm-key list is
the build's own change-detector, and the gate can only catch what the build writes. A switch is not
plumbed until the artefact that describes the image carries it.

The three numbers in 514's clause are worth naming because each one is a different way for the record
and the body to disagree, and pinning them to `1` would have refused the arm this step exists to
build — a criterion pinned as a literal, which is 554/555's defect one layer down:

* `sigirefs` — which relocation kinds reference `cpu_signal_handler_internal` in the link pool. With
  the repair in, this image contributes the pool's only **`R_ARM_CALL`** beside the kernel's two
  `R_ARM_JUMP24` tail branches; with it out, the kernel's tail branches are the whole set.
* `sigicalls` — the count of `R_ARM_CALL` references. **This is the one number in the link that tells
  the two arms apart.**
* `sigbl` — calls to it from *inside* `__wrap_poll`'s address range, and calls from outside. The
  property is unchanged (when the repair is made it is made once and from the wrapper that also takes
  the window's snapshot); what is now the switch's value is *whether* it is made.

## 3. The defect: the comment counted two statements and the set was three

The guard around the repair was written with this note:

> *Declared inside the guard, because on 594's arm nothing reads it: `-Wall -Wextra -Werror` is on, and
> an unused declaration would be a build that refuses the arm rather than one that reports it — the
> declaration has to move with the two statements that use it.*

Moving "the two statements" moved the declaration, the call and the note — and **left
`rb = entry_counter();` behind, inside the guard, where it was deleted.** The build of the arm this
step is about could not see it: on `IDLE_NO_SLEEP=1` the whole block is `#if 0`, so the arm-on image
compiles every one of those statements out and the defect is invisible to it. It was the **switch-off**
build — the one whose whole purpose was to reproduce the baseline — that refused:

```
entry_trace.c:1288:9: error: 'rb' may be used uninitialized in this function [-Werror=maybe-uninitialized]
```

Three things are worth keeping from this, and only the first is about `rb`:

1. **A comment that counts a set is a claim about that set.** This one said two; the set was three.
   The repair is not "a call and a note", it is "a counter read either side, a call, and a note" —
   which the *next* comment up the file already says in its own words ("The counter is read either
   side so the repair's own cost is on the record rather than in a claim"), so the file held both the
   right description and the wrong count, four lines apart.
2. **A defect on the `#if 0` side of an arm switch is invisible to the arm that runs.** The build
   which produced the image under test was green; the failure only appears in the build that
   reproduces the baseline. So the restore build is not bookkeeping — it is the only compile that
   sees the whole file, and it is why the arm cannot be landed by building it and stopping there.
3. The comment is now the enumeration, and it names the three statements so the count cannot be read
   past again.

## 4. The reader had to exist first, and it had to be widened before it could be

This is 564's rule applied to a whole clause block: **the step that runs once the boot is already
spent is the least tested and the most expensive.** So the reader is written before the arm is
bootable, and its own first draft had the defect this repository has paid for most often.

`run_and_capture.sh`'s clause block was gated on one key:

```sh
if grep -a -q 'xnu_live_slot_cwe_' "$log"; then
```

and every publisher its clauses score is inside the window 593 measured — `slot_cwe_*` (the enter
wrapper's note), the exit wrapper's three bracket notes, the seam, and the idle's own `sip`/`pce`/
`wfi` counters. **On this arm none of them publishes**, so the block would have printed nothing at
all for the one log that matters, and *a reader silent for a reason and a reader silent because it
broke look identical from outside*.

So the arm is decided before the gate, from keys the arm does not silence, and the gate accepts
either:

```sh
idle_no_sleep_arm=0
if ! grep -a -q 'xnu_live_slot_cwe_' "$log" \
   && grep -a -q 'xnu_live_door_seq=' "$log" \
   && ! grep -a -q 'xnu_live_repair_seq=' "$log" \
   && ! grep -a -q 'xnu_live_sip_seq=' "$log" \
   && ! grep -a -q 'xnu_live_pce_seq=' "$log" \
   && ! grep -a -q 'xnu_live_wfi_seq=' "$log"; then
  idle_no_sleep_arm=1
fi
```

`xnu_live_door_seq` is published by `__wrap_Idle_load_context`, so its presence proves `cpu_idle` ran
at all; the other four are the base arm's own evidence, absent. Each of those four *alone* would be
weaker — their absence is also what an image that never reached `cpu_idle` looks like — which is why
the presence of `door_seq` is required in the same conjunction.

**And the obvious marker is the wrong one.** The park's console group reads like the arm's own
statement, because the line carries `cpu_signal_handler_internal(FALSE) called %d time(s)` and that
count is 1 on the baseline and 0 here. It cannot be the marker, and it is not there to be read:

> the four `mini4:` lines are printed **after** `__real_poll` returns (their own comment says so —
> "printed *after* it", and both counts are "read here, at the park's return"), and on the baseline
> the park's poll **never returns** — so the group is in 520's log and 533's log neither one.
> Verified, not argued: `grep -c 'mini4: the repair --'` is 0 on both archived captures, and only
> three `mini4:` lines of any kind are in each (all from `arm_init`, before user mode).

That is measurement defect 511's shape — a prediction whose evidence cannot exist on the arm it is
predicted for — caught here by looking for the group in the baseline before writing it into the
reader as a marker. It stays in the clause as the **strongest witness** instead, and in the safe
direction: the baseline cannot print it, so its presence is progress.

### The ladder the clause reads

593 section 4's pre-registered falsifier is *not an absence* — a live spin is on the bus and returns
exactly like a working kernel — so the reading has to be something that advances only if pid 1's
thread runs past the old death point. Weakest rung first:

| rung | the reading | why it cannot be on the frozen arm |
| --- | --- | --- |
| 1 | `xnu_live_poll_seq` **> 2** | `entry_note_poll` publishes `g_poll_calls + 1` only *after* `__real_poll` returns, and the park is the third poll: the boot dies inside that call, so a published 3 *is* the park having returned. The frozen arm's two archived runs both stop at 2 (timeouts 5 ms and 40 ms — neither is a park) |
| 1b | the largest `xnu_live_poll_timeout_ms` ≥ this tree's `ENTRY_PARK_MIN_MS` | says the poll that came back is the park and not a stray ask. The threshold is **read out of `entry_trace.c`** by the reader, not written down again; unreadable is UNREAD |
| 2 | `mini4: the repair --` present, and its count reads **0** | printed only after that same return; the baseline cannot print it at all (above) |
| 3 | `xnu_live_poll_over` present | a fifth poll published: pid 1 ran on and asked again |
| 4 | more than 3 `xnu_live_sleh_user=0x1` records | 520 had exactly 3; more means further into user mode than any run |

The verdict is rung 1 (corroborated by 1b); rungs 2–4 are printed as further progress. Rung 1's FAIL
is the pre-registered falsifier and the clause says so in its own words, including that it is **not a
hang and not a brick**: the device returns either way, so the runner's exit code cannot separate the
two, and this line is the only thing that can.

`maxhex` was added beside `keyval` rather than reusing it, and the reason is one value with two
readings: `keyval` takes the **last** occurrence, which is right for a key published once, and wrong
for `poll_timeout_ms` — the first four polls each publish their own, the park is the **third**, and a
fourth short ask after the park would leave `keyval` reporting the fourth's timeout as if it were the
park's. `maxhex` asks the question actually being asked. Both are defined above the branch, because a
second copy of one reader is the defect this repository keeps meeting.

### The reader's own defect, found by running it

The first draft of the new clause printed, in a `say "..."`:

```
the park is the third poll and  runs only *after* 
returns
```

— because the two symbol names in it were in **backticks**, and backticks inside a double-quoted
string are command substitution: bash ran `entry_note_poll` and `__real_poll` as commands, reported
them missing on stderr, and substituted nothing. The sentence still read as though it said what it
meant. It was found by running the clause against a synthetic log rather than by reading the diff,
and it is the same shape as everything else in this file's history — a claim that is not what the
artifact does.

It is now checked structurally rather than left to review, because the check is total and free: the
source *is* the artifact, and every string the script prints is a `say`/`step`/`die` argument written
in it.

```sh
_bad=$(grep -n -E '^[[:space:]]*(say|step|die)[[:space:]]+"' "$_self" \
       | sed 's/\\`//g' | grep -E '`' || true)
```

Both halves of that line were earned. `\`` is a *literal* backtick inside double quotes and the file
already uses it (the two reading lines that quote `` adb devices ``), so the escaped form is stripped
first — without that, **the guard refuses the script it is guarding**, which is what its first draft
did, caught by running it against the real file. And the guard was then shown to fire by planting a
line and watching the script exit 1 with that line's number, then restoring the file byte-identically
(`5b6d03b3…` either side) — because a check whose success is silent cannot be told from one that never
ran.

## 5. What the arm's log must not contain, named one by one

Each of these is a publisher that only runs inside the window. Listing them is the point: an absence
that is *named* can be told from an absence caused by a truncated log.

* `xnu_live_repair_seq` / `_caller` / `_before` / `_after` — the note went inside the `#if` with the
  call, so on this arm the kernel's own clear is not made and nothing is published for it. Baseline:
  all four present, `_seq=1`, `_before != _after`.
* `xnu_live_sip_seq` / `_true` — `__wrap_SetIdlePop` is reached only by a pass past `cpu_idle`'s
  first test, which is exactly what the skipped repair would make false.
* `xnu_live_pce_seq`, `xnu_live_wfi_seq` — the enter wrapper and the WFI.
* `xnu_live_seam_*` (535/572's instrument), `xnu_live_slot_cwe_*`, `xnu_live_slot_pre|rtcpre|post_*`.
* `panic ... sleh_abort` at the exit's pop. **Its absence is not evidence here** and nothing scores
  it: the pop is not reached, so a panic-free log is the arm working and a panic would be a new fault.

And the one key that *must* be there: `xnu_live_door_seq` reaching `0x8000` — at least 32768 passes
that left `cpu_idle` by its first door. That is 513's door-1 count with the third door at zero, where
the baseline has exactly 1 (593 section 1).

## 6. What this does not do

* **The arm is not bootable yet.** The payload (`out/stage90/stage90-qcdt.img`) embeds
  `xnu_arm_entry.bin` as it was built, and a `build.sh` run cannot be undone (408) — so making this
  arm bootable is a payload rebuild that spends the freeze the frozen 574 arm is holding. That is the
  user's decision, not this step's, and nothing here has made it.
* It does not explain the `pop`'s death, and it does not make the idle correct. 593 section 4's trade
  stands: a bring-up stopgap for a boot, reversible with the same switch. The CPU spins hot.
* The reader's ladder is a reading of **progress past the death point**, not of "the OS is up". The
  readings that decide that are the ones after this point — item (4)'s user-mode records, the console,
  and the fixture's own syscalls in the live channel — and the clause says so where it prints.

## 7. Safety

No device action, no `fastboot`, no `adb`, nothing written to storage, no boot. Two entry-image builds
(both `fastboot boot`-side artefacts only; the payload was **not** rebuilt), reads of the two archived
captures, and edits to `run_and_capture.sh`, which is host-side and is not in
`xnu_arm_entry-sources.txt` (that manifest covers `xnu_arm_boot/` by content; the runner lives in
`stages/stage90/`). **The frozen 574 arm is unchanged and unrun**; `fastboot boot` only — never
`flash` — so no outcome of any of this can write to storage, and a power press returns the device from
any state the payload can reach.
