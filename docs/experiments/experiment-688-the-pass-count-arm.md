# 688: the pass-count arm — how far past the `pop` the boot gets, asked as a number

687 pressed 686's arm and its `pop {fp, pc}` returned, so the frontier moved past the exit — and the press that
moved it **also ended the run at the first opportunity it had**, so it could not say how far past the exit the
boot goes. This step asks that with the same site and one changed constant: `STAGE90_XNU_POST_END_RUN=2` makes
`__wrap_platform_cache_idle_exit` count its own returns and call the same `entry_seam_end_run` only when
`g_slot_post.calls` reaches 2. One whole idle pass then completes before the run ends, and the log's own
`xnu_live_slot_post_calls` stops being a yes/no and becomes a **progress counter**.

The arm is built, parked, recorded and gated. **No press has been fired for it**: no firer is armed, and the
press is the next step (§7 states its pre-registration and its one cost).

## 1. 687 §7's next arm was wrong, and the measurement that says so is in this image

687 §7 proposed placing the ending at `__wrap_cpu_idle_wfi` (`0x8047c8b4`) "on a counted pass", on the reasoning
that it is the first instrument this project has after the `pop`. **That is off by one pass, and `cpu_idle`'s own
call order is what refutes it**:

```
8000da28  bl __wrap_platform_cache_idle_enter        <-- the window opens
8000da38  bl __wrap_cpu_idle_wfi       (0x8047c8b4)  <-- BEFORE this pass's pop
8000da3c  bl __wrap_platform_cache_idle_exit         <-- the pop, and the post publish
8000da44  bl ClearIdlePop
          b cpu_idle_exit  ...  and the loop re-enters at 8000da28
```

The `wfi` of a pass is called **before** that pass's exit, so an ending placed there on the second pass would
end the run on the *near* side of the second `pop` — it would answer "does the boot reach the next `wfi`" and
would not answer "does the next `pop` return". Counting returns **inside the exit wrapper** is the earliest site
on the far side of the exit, and it covers strictly more boot: `ClearIdlePop`, `cpu_idle_exit`, the loop, the
next pass's early tests, the enter wrapper, the `wfi`, and the exit wrapper's own return. One site, one ending,
one definition; only the number moved.

## 2. The sources

| file | change |
| --- | --- |
| `xnu_arm_boot/entry_trace.c` | the ending is now `if (g_slot_post.calls >= (uint32_t)STAGE90_XNU_POST_END_RUN)`; a `#error` refuses `0..4`-outside, and the long comments state the pass count, why the count is the site's *own* field, why `>=` on the stored count and not the published one, and why the pass count rather than the `wfi` wrapper (§1) |
| `xnu_arm_boot/build_entry.sh` | the switch's domain is `0\|1\|2\|3\|4`, with the `-gt 0` refusals (no `SEAM_POC`, no second ending) and a `SEAM_POST_END_ON` for `SEAM_ON`; a new `-gt 1` narration branch with an ordinal; **the give-back clause now counts bytes**; a new clause reads the *number* the wrapper compares against; the wrapper `awk` captures `incb`/`cimm`/`cbr`/`caddr` |
| `stages/stage90/run_and_capture.sh` | `seam_post_end` is converted once into `post_end_n`, and the three reading blocks branch on `post_end_n >= 1/2` with a new PASS branch comparing `post_calls >= post_end_n` |
| `tools/verify_press_ready.sh` | row 4's fourth arm name (`wpse >= 2`), and the 686 branch's tail now records that 686's arm was pressed and its `pop` returned |
| `stages/stage90/revert-set.txt` | the new set (§6) |

**No key was added anywhere.** The switch's *domain* widened and its *name* did not, so `ENTRY_ARM_KEYS` (15
names) is unchanged, the record still writes 15 arm keys plus the two artifact keys, and the peer lane's
`ENTRY_CFG_KEYS` (17 names) needs nothing — this step has **no cross-lane half**, which is the first step in this
sequence that can say that.

## 3. The build, and what its own clauses caught

Four defects, three of them in the new clauses and one in the tree:

* **A new counter object was refused, correctly, for a reason I had not thought of.** The first design added a
  `uint32_t entry_post_end_passes` of its own. `build_entry.sh` refused the arm: the compiler materialized it at
  a base of its own choosing and the resulting `str r2, [r3, #36]` matched the clause that counts stores into a
  slot **table** — offset 36 is one of the four staged `pend_*` words — so an object of this step's was being
  read as a capture that was not there. The fix removes the object entirely and reads `g_slot_post.calls`, which
  is the post site's own counter: one definition, already in the image, and the number the log publishes.
* **The give-back clause counted instructions, and the returning epilogue is two of them.** With a counted
  ending the wrapper *returns* on the passes before the last, and gcc encodes that epilogue as
  `add sp, sp, #4` + `pop {pc}` — two encodings, one 8-byte give-back. The clause now sums **bytes** and
  compares the total with the frame, and keeps the zero allowed only on `POST_END_RUN=1` (where nothing returns
  and the compiler drops it — 686's measured shape).
* **The new clause's own `awk` had 17 conversions for a 19-name `read`.** `cimm` was silently swallowed and
  `caddr` read the immediate, so a correct `cmp r3, #1` / `bhi` body was refused as "no compare followed by an
  unsigned-higher branch". Fixed by adding the missing conversion; the clause then asserts the *number*.
* **The manifest went stale between the build and the record.** `build_entry.sh` was edited a second time (a
  `say` string) *after* the first 688 entry build, so that build's `xnu_arm_entry-sources.txt` named a
  `build_entry.sh` the tree no longer had — and the gate's entry-source clause compares by content, so it would
  have refused this tree **before the device was touched**. The entry image was rebuilt with
  `STAGE90_ENTRY_ARM_CHANGE=1`, and **the two invocations produced the same bytes** (`57d441cc…`, elf and
  manifest included) — 686's reproducibility bullet holding at a different switch value. The payload from the
  first build was kept, and only after the thing was measured rather than argued (§5).

## 4. The image, measured in the ELF

```
8047c9e8  movw r3, #248
8047c9ec  movt r3, #0x8051        r3 = &g_slot_post
8047c9f0  ldr  r3, [r3, #24]      g_slot_post.calls
8047c9f4  cmp  r3, #1
8047c9f8  bhi  8047ca08           unsigned calls > 1, i.e. >= 2
8047c9fc  ldr  r4, [sp]
8047ca00  add  sp, sp, #4         the returning path's 8 bytes, in two
8047ca04  pop  {pc}               encodings - see §3
8047ca08  bl   8047b47c <entry_seam_end_run>
```

**The 686 arm had no test at all**: its source called `entry_seam_end_run` unconditionally (the switch was used
as a flag), and its wrapper's body is `... bl 80006f2c <entry_note_pcx>` then
`8047c9e8: bl 8047b47c <entry_seam_end_run>` — no load, no `cmp`, no branch anywhere. That is not a curiosity: it
is how 687's `slot_post_calls=0x00000001` should be read. On an arm that ends at the first return, **1 is the
only value the counter could ever have printed**, so that press measured the `pop` returning and could not have
measured a pass count. The count becomes a reading at N=2 and not before — and `entry_slot_publish`'s schedule
(`<= 4`, then powers of two) means the published values 1, 2, 3, 4 are all printed exactly, so up to 4 the
counter is a *pass* progress count and not merely a floor.

## 5. The payload half, measured directly

The 686 record established its payload claim by a diff of diffs (the two difference lists equal after a uniform
shift). This step measured the thing itself:

```
$xnu_arm_entry.bin occurs verbatim, and exactly once, inside out/stage90/stage90-qcdt.img
  at offset 496284, all 5519996 bytes, sha256 57d441cc… on both sides
stage90.bin      vs the 686 park: 529487 differing bytes, EVERY one inside the blob span 494236..6014232
stage90.img      vs the 686 park: 529507, the extra 20 at file offsets 576..595
stage90-qcdt.img vs the 686 park: 529507, the same 20 at the same offsets
```

The 20 extra bytes are inside the boot image's **command line** (cmdline offset 514..533), a 20-byte digest the
build writes into it that follows the entry image. It is not this step's: the same 20 bytes at the same offsets
differ between 686's park and `armed-seam-endrun-88972ba9`. **No payload code differs.** Sizes are identical to
the 686 park's in all eleven files (the compare immediate changed, nothing grew).

## 6. The record, the park, the gate

* park `out/stage90/frozen/armed-post-endrun2-57d441cc/` (11 files, `cp -p`, each verified against the live
  tree); the set is recorded by hand in `stages/stage90/revert-set.txt`.
* `tools/verify_revert_set.sh out/stage90 --set=armed-post-endrun2-57d441cc` → **exit 0**, 11 files matched and
  all 6 criterion-B member checks agree. (Without `--set` the tool iterates *all* sets and exits 1 by design,
  naming the live directory as "a different arm" for the other six; that output is not a failure of this set.)
* `tools/verify_press_ready.sh` → **exit 0, 5 of 5**, including `the press would be caught` — the row that was
  red from 667 until the phone came back on the bus, and that the 687 press also saw green at 13:46. The tool
  found the set by hashing the live `stage90-qcdt.img`
  (no `--set`), printed the flags `--allow-xnu-entry` and the two exact commands, and row 3 ran the gate
  itself: **exit 0 under `--allow-xnu-entry`**.
* Nine of the eleven differ from the 686 park; the two that agree are again `stage90-build-config.txt` and
  `stage90_fixture.macho`, the two files that do not contain the entry image.

## 7. The pre-registration, and the arm's one cost

Read the coming log with 687's rules, which the runner now selects off `xnu_live_seam_post_end_run`:

| `xnu_live_slot_post_calls` | reading |
| --- | --- |
| `0x2` (the arm's own number) | the boot completed **one whole idle pass** and reached the ending: the exit returned, `ClearIdlePop` and `cpu_idle_exit` ran, `cpu_idle` was re-entered, its early tests passed, the enter wrapper opened the window, the `wfi` halted, and the exit returned again. **The OS stays alive past the exit and keeps idling** — the first statement this phase can make about the boot after the frontier. |
| `0x1` | the boot **died between the passes**: the first exit returned (687) and the second one did not, with the counter's last publication being 1. This is the cell that says the operation's repair is a one-shot, which is a finding about the pop and not about the ending. |
| absent | the boot never reached the first post site in *this* run — the measure arm's cell, and the least likely here. |

**Both cells come back with a log** (the ending's first store is the abort that resets the phone — 684 — and a
death at the pop reaches the same epilogue), **except one**: if the second pass hangs rather than faults, the
ending never fires and this run is a **no-return (exit 2)**, the same cost as the five hangs before 686, paid
with a power press and not with the device. That is this arm's one cost and it is stated here so that an exit 2
is not read as the runner failing. It is not bounded by anything in these bytes: the net that would bound it is
666's hardware-watchdog arming (whose bite has never been observed to fire, and is measured by the interval
669 added) or a CNTVCT-deadline ending — neither is in this image, and either is the step after this one if the
count comes back short.

## 8. What this does not do

**It does not enter the OS in the sense the goal needs, and it does not move the goal's criterion.** A boot that
idles is a boot that has *stopped doing anything else*; the reading says the machine survives its idle loop, not
that XNU's own kernel threads, drivers or storage stack are running. And it does not bound the boot's life: N is
2, so the log will say "at least one full pass", not "indefinitely".

**The storage condition is still unmet.** 「如果os已经能进去了的话」 asks for a boot that is *observed* entering
the OS and staying there, and this arm ends its run on purpose after a counted number of passes. **TWRP-to-
storage stays withheld.**

**The step after this one is a window with a clock in it.** A count answers "how many passes"; what the goal
wants is "how much other work does the boot do in a bounded time", and the instrument for that is an ending
gated on a **CNTVCT deadline** at this same site — the wrapper already reads the virtual counter
(`mrc 15, 0, r1, cr13, cr0, {4}`, the value it passes to `entry_slot_rtc_note`), so a deadline ending needs no
new mapping and no new call site. It is a bigger edit than a compare immediate, which is why it is not this
step.

## 9. State

The arm in `out/` is **`armed-post-endrun2-57d441cc`** (11 files, recorded, parked, gate green, readiness 5/5).
**No firer is armed**, so 660's closure has released and a build may touch the tree. The 686 arm remains parked
and revertible, and its press is recorded in 687. Nothing was sent to the device in this step: no `fastboot`,
no `adb`, no flash, nothing written to storage.
