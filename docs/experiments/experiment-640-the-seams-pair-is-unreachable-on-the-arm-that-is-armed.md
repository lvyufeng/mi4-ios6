# 640: the seam's pair is unreachable on the arm that is armed, and the arm's own table does not carry it

638 pre-registered a reading: the seam's `_b1`/`_a1` pair, taken inside `platform_cache_idle_exit`,
decides between 597's two candidate repairs, and "the owed run" is the log that decides it. This step
takes the one question 638 never asked - **can the armed image produce that pair at all** - and the
answer is no, by the same proof that says the parked arm is the right arm to send.

Nothing was built, nothing was run, no device was touched. Every figure below is a read of the armed
record, the armed ELF's instruction stream and call sites, and Apple's own `caches.c`.

**Numbering, and how this step got it wrong once.** This step was first committed as **639**, and 639 was
already taken: the step numbers are shared with the *gate-only* steps, which take a number and no doc
(`622 (gate)` is `b0c1c02`, `639 (gate)` is `128b66a`, `627 (gate)` is `9cbad2b`), so `grep '^# 639'` in
`docs/` and `git log --oneline | grep ' 639 '` name two different steps. **One number, two definitions** -
this project's most-repeated class - arriving in the record rather than in the code, and the fix is the
one this file has: 640 was free in both, so the doc, its references and the README row were renumbered in
the follow-up commit that carries this note. The rule a future step needs is the one the collision
implies: **a step number is taken if any commit subject carries it, doc or not.**

## 1. Which arm is armed, read off the record rather than remembered

`out/stage90/xnu_arm_entry-config.txt` (the record that binds by hash to the armed entry bin):

```
STAGE90_XNU_IDLE_NO_SLEEP=1     STAGE90_XNU_SEAM_MEASURE=1     STAGE90_XNU_SEAM_POC=0
STAGE90_XNU_IDLE_CACHE_ENABLE=0 STAGE90_XNU_EXIT_POC_FLUSH=0   STAGE90_XNU_IDLE_STACK=1
```

`IDLE_NO_SLEEP=1` is 594 section 1's switch: `#if !STAGE90_XNU_IDLE_NO_SLEEP` compiles out 514's one
repair, the only clearing of `SIGPdisabled` in the image. So the owed press sends the arm whose idle
never sleeps - and **that arm has two names in this tree.** The runner prints it as `SLEEPER ARM (594's
switch)` (`run_and_capture.sh:846`), and the docs that use a name use that one (598's table, 630, 631,
632, 634, and 638's own section 1), while 594's title calls it "the arm that never sleeps" and 595's
"sends the sleepless arm". One arm, two names, and the substantive question is not which name is right
but what the arm can do: section 2. (638's section 1 sentence "the owed press sends the sleeper arm" is
therefore the runner's name for the right arm, not a second arm - this is not 638's error. Its error is
the one section 4 names.)

## 2. The chain: the acting site is inside the window, and the window has no route in

The seam's whole acting branch hangs on one comparison - `lr != STAGE90_XNU_SEAM_LR` - and the constant
is the return address of one `bl`:

```
platform_cache_idle_exit @ 0x800462d4
  0x800462d4  e92d4800  push {fp, lr}
  0x800462d8  eb10da24  bl   8047cb70 <__wrap_FlushPoU_Dcache>     <- the seam's only acting site
  0x800462dc            (+4 = STAGE90_XNU_SEAM_LR, entry_trace.c:2227)
  ...
  0x8004631c-28         mrc/orr #4/mcr c1,c0,0 - SCTLR.C back to 1
  0x8004633c  e8bd8800  pop {fp, pc}
```

`platform_cache_idle_exit` is therefore the function the pair is a reading *of*, and the whole question
is whether that function runs. Read out of the image's call sites, it has exactly **one** caller in the
entire link, and that caller has exactly one:

```
0x8047c944 <__wrap_platform_cache_idle_exit>          <- the wrapper
  0x8047c96c  bl 800462d4 <platform_cache_idle_exit>  <- its only call of the real exit
0x8000da3c  bl 8047c944 <__wrap_platform_cache_idle_exit>   in cpu_idle
```

and 599 section 3 already measured what stands between `cpu_idle` and that call - the gate, whose first
test is on the same bit the repair clears:

```
0x8000d960  ldr r0, [r5, #0x28]     ; cpu_data->cpu_signal
0x8000d964  cmn r0, #1
0x8000d968  ble  0x8000d978         ; SIGPdisabled set -> the first door, Idle_load_context
0x8000d96c  bl   __wrap_SetIdlePop
0x8000d974  bne  0x8000d980         ; the window
```

So on this arm `SetIdlePop` is not even called, the window is never entered, and the real exit never
runs. That is re-derived here rather than cited: `tools/check_idle_window_unreachable.py
out/stage90/xnu_arm_entry.elf` exits 0 with

```
== (D) the clearer: every `bl cpu_signal_handler_internal` in the image ==
  (none)
VERDICT: the window is UNREACHABLE in this image.
```

**The seam's `lr` test is inside a function this image cannot execute.** That is the whole finding, and
it needs no cache reasoning: the pair is published by the acting branch
(`entry_trace.c:2301-2314`), the acting branch is entered only at `0x800462d8`, and `0x800462d8` is in
a function whose only entry is behind the dead gate.

## 3. What the arm *can* publish, site by site

`--wrap=FlushPoU_Dcache` redirects every *direct* call in the link to one wrapper, so the image's four
`bl __wrap_FlushPoU_Dcache` sites are the complete set of places the seam can run:

| site | enclosing function | the seam's branch there | reachable on the armed arm? |
| --- | --- | --- | --- |
| `0x80045d08` | `cache_xcall` (`0x80045b7c`) | **other** - publishes `xnu_live_seam_other` | only if the cache cross-call path runs. Its callers in this image are `flush_dcache_syscall` (tail, `0x80045b78`), `dcache_incoherent_io_flush64` `0x80045f3c`, `dcache_incoherent_io_store64` `0x80046088` and `platform_cache_flush_wimg` `0x80046378`; whether any of them runs inside the 28 s is a reading the log takes (`xnu_live_seam_other`) |
| `0x80046284` | `platform_cache_idle_enter` (`0x80046238`) | **other** | **no** - the window (section 2) |
| **`0x800462d8`** | **`platform_cache_idle_exit`** (`0x800462d4`) | **acting** - publishes the pair | **no** - the window (section 2) |
| `0x800463bc` | `cache_xcall_handler` (`0x80046380`) | **other** | **no** - it is the cache IPI handler, called only from `cpu_signal_handler_internal` (`0x80013034`, `0x80013050`), and 599 section 4 proves no IPI is delivered on this port |

So the expected seam reading of the owed run is **no `xnu_live_seam_*` key at all**, and the only one it
could carry is `xnu_live_seam_other` (plus its `_other_lr`) if one of the cache cross-call paths happens
inside the 28 s. That reading is an *other-site* reading either way: the seam reporting that the hook ran
and rejected the site, which is 526's distinction, and it is not a pair.

**And the instrument, because a reader will re-derive this table and one spelling of `objdump` cannot.**
On this host, bare `objdump -d out/stage90/xnu_arm_entry.elf` produces **62 bytes of output** — the file
header and nothing else, no disassembly body at all — and exits **0**; `objdump -f` says what the reason
is (`architecture: UNKNOWN!` against `arm-none-eabi-objdump -f`'s `architecture: armv3m`). So the grep
over its output is **zero** for every address, and that zero is not "fewer sites than there are" — **the
instrument produced no text.** It is the same first-ranked shape as a blind filter (an extractor that
cannot see the state it is looking for) and this spelling is worse in one way: **nothing in the output
says "I could not read this", and the exit status is 0.** Every address above came from
`arm-none-eabi-objdump`, and the four sites reproduce under a pattern that names no symbol at all
(`grep -E '\b(bl|b|blx)\s+8047cb70\b'` → the same four), so the pattern is sound exactly because the
tool that produced the text can read the ELF. That blindness was found by `run-experiment-526` while
re-deriving this table in their own lane, and it is a real instance of a defect class this project
documents rather than a hypothetical one.

**And that is exactly what the gate's own idle narration says**, in the words of the file the press
runs (`preflight_boot_check.sh:576-580`):

> idle sleep (IDLE_NO_SLEEP=1): ... the window whose `pop {fp, pc}` is this phase's frontier IS NEVER
> ENTERED. **Everything below about the slot, the bracket, the window's two ends and the seam is
> narration about an arm this one is NOT**, and on this arm those keys are expected ABSENT rather than
> zero - absent because the call inside the window is never made

## 4. The correction to 638

638's physics is untouched: `FlushPoU_Dcache` is a cleanflush (638 section 1), `_b1` is the word the
`pop` takes as `pc` (section 2), and the two candidate repairs are still separated by an unequal pair
(section 3). What this step corrects is **which run can read it**:

* **638 section 1's premise about which arm is armed is right; what it never asks is whether that arm
  can produce the reading its own section 3 registers.** The armed record says `IDLE_NO_SLEEP=1` (the
  arm the runner's log calls `SLEEPER ARM`), and 638's `SEAM_MEASURE=1, SEAM_POC=0` is read off the same
  record. The step from the arm to the pair is the one that does not hold: it is the arm 594 built *so
  that* the `pop` and everything inside the exit is unreachable (595, 599), which is why it was chosen
  and why it cannot publish `_b1`/`_a1`.
* **638 section 3's table is the reading of an arm that enters the window.** Both of its rows are
  statements about `_b1`/`_a1`, which only the acting branch publishes. On the armed arm neither row
  applies, and a log with no pair must **not** be scored as "`a1 == b1` ⇒ candidate (B) is provably
  useless" - the absence has a third cause the table does not carry (the arm cannot reach the site), and
  it is the cause in force.
* **638 section 4's self-check is vacuous on this arm for a second reason.** It opens "if the run dies
  at the `pop`", and the armed arm's whole point is that it does not: 599 section 8 says the death at
  the `pop` cannot be what stops it.
* **The arm that would read the pair is one no build has carried.** It needs the seam *and* the gate
  that opens: `IDLE_NO_SLEEP=0` together with `SEAM_MEASURE=1`. The only seam arm ever built was 535's,
  which was `SEAM_POC=1` (572), and its run left no log (572 section 1) - which is why 638 section 3
  could say "no run has ever produced one of these pairs alongside a return". So 638's pre-registration
  is not falsified; it is **unread**, and stays pre-registered for the next seam arm rather than for
  this press.

The operative harm is a reader's: 638 sends the owed run's reader to a pair, the gate's seam block says
the key that would carry it "is present", and the log will not have it. A reader who concludes "535's
operation cost the return" from that silence would be concluding it from an arm that never made the
call - the mistake the gate's idle narration spends its own paragraph warning about for the window keys.

## 5. Two readers state one absence with one cause, and this arm has two

The armed arm makes the seam keys absent by **construction**; an arm without the seam makes them absent
by **absence of the instrument**. Both files that read them say, in one place, that there is one cause:

| file | the sentence | what it is wrong about |
| --- | --- | --- |
| `preflight_boot_check.sh:882-883` | "They are told apart by whether the `xnu_live_seam_*` keys were written at all - **for this arm `xnu_live_seam_calls` is present**" | the same file's `:576-580` says those keys are *an expected absence on this arm*; both print in one run (measured: `./preflight_boot_check.sh --allow-xnu-entry` exits 0 and prints both blocks) |
| `run_and_capture.sh:1699-1702`, clause (5) preamble | "a log without the keys is a log from an image that **does not carry the arm**, and silence is the right reading for it" | the armed image *does* carry the arm - its record says `SEAM_MEASURE=1` and the gate's own clause refuses a record whose ELF lacks the symbol (`:2480`) - so the stated cause is the one cause that is false here |

And the runner already has the machinery that gets this right, **one table away**: `run_and_capture.sh:833-841`
prints, for every run, an `arm:` table that lists six key families with the KIND of each absence -
"a key whose publisher the arm's switch removes is absent by CONSTRUCTION, and a key published inside
the window is absent only because the window was not reached". The seam's family is published inside the
window, is missing from that table, and its absence is described in the one place the table's own
distinction exists to prevent.

**And the runner's clause (5) is worse than a wrong sentence in the preamble — it is a wrong sentence
with no second chance.** The whole block - the arm branch, the `sctlr` FINDING/UNREAD, the `lr` PASS/FAIL
and the pair comparisons - sits behind one gate, `:1752`:

```sh
if [[ $seam_calls =~ ^0x[0-9a-f]+$ ]] && (( seam_calls >= 1 )); then
```

On a keyless log `seam_calls` is absent, the gate is false, and **every sentence inside is unreachable -
including its own `UNREAD` branch for an absent `seam_op`**. So the preamble's "a log from an image that
does not carry the arm" is not merely the wrong sentence for this log: it is the **only** sentence this
file says about a keyless seam block. The same `seam_calls` test gates four other prints in the same
file (`:1466`, `:1523`, `:1565`, `:2071` - the 535/572/597 context blocks and the death-shape block), so
a keyless seam changes what those say too, and none of them is told why. **The repair is therefore not one
sentence but the predicate**, and the arm the predicate needs is already derived in the same file
(`idle_no_sleep_arm`, `:826-828`).

**That makes an owed-LOG defect a separate item from this owed-PRESS one.** The press's log is not
archived: `run_and_capture.sh` writes `/tmp/cancro-last_kmsg.txt`, which the *next* run overwrites (the
project's convention is `out/stage90/captures/NNN-…-last_kmsg.txt`, and 526's and 533's are copies of
that file taken by hand, in the same directory as their own arms). So unless the press's log is copied
into `captures/` promptly after the run, the evidence this step says not to misread will not exist when
the repair lands - and the repair is precisely about how that log is read.

Neither file was edited. Both are read by the armed catch: the gate is the file the catcher fires and
the runner is the file it fires (620), so the repair is owed for after the press - and the gate is
`run-experiment-526`'s lane, so the finding is sent to them rather than edited here (as 637's was).
The repairs are one clause each: the gate's seam block needs the idle block's caveat named in it, and
the runner's clause (5) preamble needs the second cause plus the arm key that separates them - which
the same file already derives (`idle_no_sleep_arm`, `:826-828`).

**The lane's owner reproduced the finding independently and staged a repair on a copy** - not landed,
and it will not be while the catch is armed. Their run through this tree's own method (re-derive rather
than accept, as 637 §6 recorded) gave the same four sites, the same single caller chain
(`0x8047c96c` into `platform_cache_idle_exit`, whose only caller is `0x8000da3c` in `cpu_idle`), the same
reachability verdict, and the bare-`objdump` blindness section 3 records. Their repair is shaped better
than the one-clause version this step proposed: instead of attaching a caveat to the `op=0` sentence and
the "its proof is the death's shape" sentence, **they move both under `if [[ $V_IDLE_NO_SLEEP -eq 1 ]]`** -
the same variable the idle block keys on - so the claim becomes a *condition* and the two blocks cannot
disagree again by construction. Measured on the extracted block under both arms (38 lines with the
false presence-claim gone at `IDLE_NO_SLEEP=1`, 36 and byte-identical at `=0`), `patch --dry-run` clean,
and independent of 637's repair in either order (`ad698429998fd898…`).

## 6. What this does not do

* **It does not read the owed run, and it does not predict it.** It says which of 638's readings that
  run *cannot* produce. Whether the arm in `out/` gets past the idle is 595/599's pre-registered ladder,
  and it is the run's question.
* **It does not falsify 638's physics.** The flush's semantics, the identity of `_b1` with the `pop`'s
  `pc`, and the two-repair decision all stand; section 4 restricts them to an arm that enters the window.
* **It does not fix either file.** Section 5's two sentences are still in the tree, in the two files the
  press depends on, and section 5 names why that is deliberate.
* **It does not change the arm, the park, the gate's verdict or any byte on disk.**
  `out/stage90/stage90-qcdt.img` is still `60063c47…`, still **UNRUN**, and the gate still exits 0.
* **It does not advance 「起码要能进入操作系统，把基础驱动跑起来」.** The device is off the bus; the one
  event that can move the goal is physical and the user's: **unplug `33e80afe`, then Vol-Down + Power**
  (635). TWRP-to-storage stays withheld: 「如果os已经能进去了的话」 is unmet.

## 7. The catch's cover has two deadlines, and only one of them is the cover

Not the step's subject, but it was written down wrong twice on the way here and the right answer is in a
file, so it is recorded. The armed catch's own log has two lines that both look like "how long something
watches":

```
press-watcher.log:141  [2026-09-23 19:51:34] catch 1 armed (pid 1344846)
press-watcher.log:86   [2026-09-23 14:16:11] relay armed: will keep exactly one catcher at
                                              …/press-watcher.sh alive; poll 30s; at most 16 re-arms
```

The **catch's** line gives a 6 h bound from the *catch's* own start (`:98`), so catch 1 covers a press
taken up to **2026-09-24 01:51:34** — and that is a statement about catch 1, **not** about the cover.
The **relay's** line gives the cover, and the loop behind it is exact: `arms=0` (`:179`),
`arms=$(( arms + 1 ))` (`:206`) before each arm, and the stop at `:199` is `[ "$arms" -ge "$REARM_MAX" ]`
— so `REARM_MAX=16` is **sixteen** catches numbered 1..16, which is what the file's own comment says
(`:55`, "16 x ~6 h = ~4 days of continuous cover"). The cover therefore ends about **2026-09-27 19:51**
(96 h from catch 1, plus a few minutes of ARM_SETTLE, not hours).

**Why it matters here:** a reader asking "is anything still catching a press on the 26th?" who takes the
catch's bound for the cover concludes the watch lapses tomorrow, and one who counts the initial arm twice
concludes it ends several hours later than it does. Two lines, one log, two deadlines — the same shape as
the rest of this step, in the record rather than in the code, and the one to quote is the relay's.

## 8. Safety

No device action, no boot, no build, no `fastboot`, no `adb`, **nothing written to storage**. The
commands were `nm`/`objdump` on the frozen ELF, one re-run of `tools/check_idle_window_unreachable.py`
(host-only, exit 0), reads of `caches.c` and of the armed record, and one run of
`preflight_boot_check.sh --allow-xnu-entry` for the section 5 claim - the gate is host-only and was
measured inert in 634 (stubbed and unstubbed runs byte-identical, the stub never called), and it writes
nothing. Nothing in `stages/`, `tools/` or `out/` was modified: `git status` is clean
and `sha256sum out/stage90/stage90-qcdt.img` is still `60063c47…`. `fastboot boot` only - never
`flash` - so no outcome of this step can write to storage.

## 9. Corrigendum (2026-09-24): "no build has carried" is true of a **run**, not of a build

§4's fifth bullet says **"The arm that would read the pair is one no build has carried. It needs the
seam *and* the gate that opens: `IDLE_NO_SLEEP=0` together with `SEAM_MEASURE=1`. The only seam arm ever
built was 535's, which was `SEAM_POC=1` (572)."** The first sentence and the third are false about
builds; the paragraph's *conclusion* is right, and its own next clause is what shows why — it goes on to
talk about a **run** ("its run left no log", "no run has ever produced one of these pairs").

A build carried `IDLE_NO_SLEEP=0` together with `SEAM_MEASURE=1` and `SEAM_POC=0` on 2026-09-23 — entry
bin `151425c4…`, entry ELF `3bc72605…`, payloads `stage90.bin` `0f108392…` / `stage90.img` `8274b1c4…` /
`stage90-qcdt.img` `914f45ac…` — and `experiment-594` records it as **unrun**. It is not 535's arm and
it is not a `SEAM_POC` arm. It is parked, with a verified manifest, at
`/mnt/data/mi4-ios6-export/arm-574-idle-no-sleep-0/`.

**So 638's pre-registration is unread, not unreachable, and the read costs a press rather than a
build.** This matters beyond bookkeeping: the sentence as written sends the next reader to
`build_entry.sh`, and a build **with the catcher armed silently swaps the armed image** (636), so
following it literally spends the owed press on nothing.

**Everything in §2, §3 and §4's other bullets stands as measured** — the acting site (`0x800462d8`
inside `platform_cache_idle_exit`), its single route through `cpu_idle` behind the `SIGPdisabled` gate
208 bytes earlier, the four `bl __wrap_FlushPoU_Dcache` sites, and the fact that the **armed** arm
publishes no `xnu_live_seam_*` key. The corrigendum is about the existence of a build, not about this
arm's reachability.

One further measured detail, since §3 lists sites by `bl`: `__wrap_FlushPoU_Dcache` is **three
instructions and a tail branch** — `mov r0, sp; mov r1, lr; b <entry_seam_flush>` — so a search for
`bl entry_seam_flush` finds **zero** sites while the hook is in fact live. `entry_seam_flush` is 115
instructions and byte-for-byte the same body in both arms (shifted by `0x20`); what differs between the
two arms is reachability, not the instrument. A `bl`-only census of hooks misses every tail-called one,
which is the mirror of the rule this project already carries — *hooks live at calls, and a hook is not
always a `bl`*.
