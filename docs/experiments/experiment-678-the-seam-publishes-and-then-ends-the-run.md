# 678: the seam publishes its pair and then ends the run on purpose — the reading stops depending on a return

677 spent the owed press, the device came back, and the payload lived **at most 22 seconds**. That is below
the hardware watchdog's 28-second bite and far below the self-test arm's 90-second bounded spin, so the arm's
own two candidate times were never reached and its question came back unanswered a second time — this time
not because the machine hung but because **the run ended early**. The consequence for the frontier is sharper
than the missing answer: the pair `entry_seam_flush` publishes is the diagnosis of the `pop {fp, pc}`, and
every seam arm so far has gone back to the idle exit to publish it — the very step that dies. The log lives in
DRAM and only a return can reach it, so an arm that must return to report **cannot report about a hang**.

This step builds the arm that does not return: **step 4 of 663 §2's five-step table**, with step 5 as its
consequence. It is a *smaller* change than 663 specified, because 674 §1 and 673 §3 each removed half of it.

**No device was touched, nothing was flashed, nothing was written to storage.** The arm is built, parked,
recorded and verified; the gate refuses it on the peer lane's half alone and the refusal is measured below.
**No press is armed for this arm.**

## 1. What changed, and it is one switch on the operation arm

`STAGE90_XNU_SEAM_END_RUN=1`, on top of `STAGE90_XNU_SEAM_POC=1 SEAM_MEASURE=0 SLOT_NULL=1 IDLE_NO_SLEEP=0`.
The seam does everything the 653 arm does — reads `b0`/`b1`, runs `__real_FlushPoU_Dcache`, runs 535's
operation over the slot's eight bytes, reads `a0`/`a1`, restores the slot, reads `SCTLR`, publishes the pair —
and then, where the 653 arm returns, it ends the run:

```c
RESTART_REASON (0x0fa0065c) <- 0x78665501u ;
dsb sy ;
PSHOLD        (0xfc4ab000) <- 0u ;
dsb sy ;
for (;;) { wfe }
```

Three constants, one definition: new `xnu_arm_boot/entry_reset.h` holds the three addresses/values, because
`entry_stubs.c`'s three `#define`s were invisible to `entry_trace.c` and this is now a second writer. That
header is the reason the entry source manifest went **25 files -> 26** (`xnu_arm_entry-sources.txt` 2104 ->
2184 bytes).

The switch is **refused in both wrong directions**, in `entry_trace.c` as `#error`s and in `build_entry.sh` as
`die`s, and both refusals were falsified (each fires, at `entry_trace.c:514` and `:517`):

* `END_RUN` without `SEAM_POC` — the measurement arm and the no-operation arm are booted on the claim that
  their body cannot change what the machine does, and ending a run is the largest such change;
* `END_RUN` with `SEAM_MEASURE` — ending the run is the one change the control cell exists to rule out.

The arm is therefore a **modifier of the operation arm**, not a fourth interception. The whole ending is 12
instructions and it is the only code this step adds to a boot path.

## 2. What 663 asked for and what this step actually needed — two measurements did the work

663 §3 specified (1) a new literal for the reset-reason word, (2) an `entry_mmio_section(0xfc400000, ...)`
install to make PS_HOLD reachable, (3) a switch, (4) an ending in `platform_reboot`'s shape, and (5) the
hardware-watchdog bite as a second, PMIC-independent net.

* **(1) and (2) are not needed, and 674 §1 measured why.** The payload already maps both megabytes into
  `stage90_candidate_l1` — the L1 the handed-off kernel runs under (`xnu_arm_vm_init_full_pmap.c:410-416`):
  `0xf9000000` (the GIC megabyte, which holds the bite `0xf9017014`) and `0xfc400000` (PS_HOLD at
  `0xfc4ab000`). No new literal and no new section install are required, and the disassembly below confirms
  both stores are plain `movw`/`movt` pairs into already-mapped megabytes.
* **(3) and (4) are this step.** 673 §3 measured that no existing switch ends a run at the seam —
  `entry_seam_flush` publishes and returns, and all three seam arms return to the idle exit that dies — so the
  ending sits behind a new switch, about ten lines.
* **(5) is already carried, and it is not this arm's subject.** The countdown is armed unconditionally by the
  payload at `stage90_main.c:1205` before anything that can hang, and the payload's switch dump for this arm
  reads `STAGE90_HW_WATCHDOG_SELFTEST 0u` — this arm does not run the self test. The net is armed because
  every arm arms it; what this arm exercises is the **PS_HOLD** ending, which is also the order 674 §2 derived:
  **PS_HOLD first, then the bite** — the payload's own order, whose comment keeps the two together *"precisely
  because either one alone has an unknown failure mode"*. The consequence is stated there and it applies here:
  a no-return on this arm is evidence against PS_HOLD-first, and a bite that never fires says nothing about the
  frontier at all.

**And the ending is reached from its own failure**, which is why PS_HOLD is the right choice rather than a new
mechanism: `entry_epilogue`'s tail is exactly these two stores (`entry_stubs.c:4229`), and `fleh_dabort` reaches
it on both exits, so this arm ends through the one reset this project has measured to work — proven by **every
returning run this project has ever had** ([[mi4-the-run-ends-at-entry-epilogue]]). A fault on the
`RESTART_REASON` store would land in `fleh_dabort` and take the same ending; and the address is not
speculative, because `entry_epilogue` writes it on every run that has ever come back.

## 3. The build, and the byte-identity measurement that keeps the tree honest

Entry image built `EXIT=0`; payload built `EXIT=0`.

| artifact | sha256 | bytes |
| --- | --- | --- |
| `xnu_arm_entry.bin` | `88972ba93ea34b30f4e586a73458512bd1fee401b79c209ea76f7fbacdcc0ac3` | 5,519,996 |
| `xnu_arm_entry.elf` | `a0d07bcbda63a3455040b4c5ca53052f65cc858ab3e822d4bfc565f6ed740c0d` | 6,698,696 |
| `xnu_arm_entry-config.txt` | `224810c58fcbf5bb5a6084a9c7a4d7809f3da7294cfd6a8746d8032965b4990a` | 783 |
| `xnu_arm_entry-sources.txt` | `d2b8b6e37f11c197db986375bb33aa68953354355c6d80597849745e1ff1e1c0` | 2,184 (26 files) |
| `stage90.bin` | `9fd8509309ec72a420b1de6e97046bc829ca4941ad243fe6aa4a829c0e76b504` | 6,015,492 |
| `stage90.elf` | `aff819304249b6a6b4d108818d0fe91c2729000b4444b6de9bad8bf0245cfeb9` | 6,077,564 |
| `stage90.img` | `8cc3b8d0b2294fb29058b20794f731b270504743fb03851177841b9fbf6050c1` | 6,019,072 |
| `stage90-qcdt.img` | `94c95342821abe709e046260f4ca9f14a027d0846290e8529e7cc5c772d03555` | 8,540,160 |
| `stage90-build-config.txt` | `6c2b6038d3bcbe30da60084c571682100946a15f191437161dbf1ed1385b547b` | 682 |
| `SHA256SUMS.txt` | `a99a8a6206e9613a7a4e5f2063f01df1dffbac51fa79851c87bba7de82f46b90` | 560 |

**Two payload-source edits ride in this commit that change zero bytes of this arm, and that is measured
rather than asserted.** `cache_ops.c`'s and `entry_stubs.c`'s L2 comment still said "a 4096-set, 8-way,
128-byte description (4 MB)" where the register word `0xf0ffe03b` decodes to **2048 sets of 8 ways of 128-byte
lines, 2 MiB** (676); and `stage90_selftest_bounded_spin` gained a one-second tick, which is behind
`STAGE90_HW_WATCHDOG_SELFTEST` and that switch is 0 here. With the three payload sources (`cache_ops.c`,
`stage90_main.c`, `stage90.h`) stashed, the rebuild is **byte-identical** to the build with them applied:

```
9fd8509309ec72a420b1de6e97046bc829ca4941ad243fe6aa4a829c0e76b504  stage90.bin      (stashed == applied)
94c95342821abe709e046260f4ca9f14a027d0846290e8529e7cc5c772d03555  stage90-qcdt.img (stashed == applied)
```

A suspicion that `stage90.h` had changed payload codegen was raised by `stage90.bin` being **136 bytes larger**
than the parked 653 arm's (6,015,492 against 6,015,356) and by symbol sizes moving in `delay_us`,
`gic_timer_selftest`, `run_timebase_selftest`, `stage90_arm_pc_sampling_watchdog` and `timebase_elapsed_us`
with a new `timebase_usec_to_ticks`. It was ruled out by a controlled compile: `timebase.c` built against the
current `stage90.h` and against `HEAD`'s is **byte-identical**. The real cause is **commit 666's repair of
`timebase_elapsed_us`** (the 32-bit product that could not exceed 44,739,242us against a 90,000,000us
deadline), which landed *after* the 653 payload was built. The difference belongs to 666, not to this step.

The payload's own switch dump is **byte-identical** to the 653 arm's and the sleeper's — `6c2b6038...`, 682
bytes — because the entry switches are not in it. That is why this set is named for the **entry** token and not
the payload's: the payload record cannot name this arm at all.

### 3.1 A defect the build's own new clause caught, and it is the reason the clause is written that way

The first `build_entry.sh` run for this arm ended `EXIT=1`: `entry_seam_end_run contains 29 str instruction(s)
and not two`. The clause bounded the disassembly with `next_global "$seam_endpoc"`, but `entry_seam_end_run` is
**`static`** (`t` in `nm`), so the next *global* symbol is far past the function and the range ran through
`entry_str8`. The fix caps the stop address at `+256` and stops at objdump's second `<...>:` label line. It is
[[mi4-an-edit-on-a-prefix-deletes-the-suffix]] one layer out: a window defined by a symbol table answered about
a different range than the one it was written for, and the disassembler's own labels are what it has to be
written from.

**And a second clause was added because the same class had already fired once in this project.** The record
writer is the one site a name-keyed edit does not visit, and the second build exited 0 with
`STAGE90_XNU_SEAM_END_RUN` **missing from `xnu_arm_entry-config.txt`** — 594's defect exactly. The writer now
has the `echo` line *and* an after-the-fact `comm` comparison of the record against `ENTRY_ARM_KEYS` **in both
directions**, with `layout_fail` on either difference. Third build: `EXIT=0` and the record carries the key.

## 4. What the entry image now contains — read out of the disassembly, not out of the record

```
8047cb04: bl 8047b47c <entry_seam_end_run>     <- the ending, shared by both publish paths
...
8047cbb8: b  8047cb04                          <- the publish path falls through to it
8047b47c <entry_seam_end_run>:
8047b47c: movw r3, #21761 ; movt r3, #30822    ; r3 = 0x78665501
8047b480: mov  r2, #262144000                  ; r2 = 0x0fa00000
8047b488: str  r3, [r2, #1628]                 ; 0x0fa0065c <- 0x78665501
8047b48c: dsb  sy
8047b490: movw r3, #49151 ; movt r3, #64586    ; r3 = 0xfc4abfff
8047b494: mov  r2, #0
8047b49c: str  r2, [r3, #-4095]                ; 0xfc4ab000 <- 0
8047b4a0: dsb  sy
8047b4a4: wfe ; b 8047b4a4                     ; for (;;) wfe
```

12 instructions, **2 stores, 1 wfe, 2 `dsb`** — the shape 674 §3 required, asserted by the build's own clause
(`exactly 2 str`, `>= 1 wfe`). Both addresses are `movw`/`movt` pairs, so neither appears as a literal (m672),
and this is why the gate's register-literal scan is unaffected.

**The publish path is intact, and the check is the key set rather than a size.** The gate reads
`entry_seam_flush`'s body and now reports **122 instructions / 24 loads** for the 653 arm and **114 / 14** for
this one — a 32-byte *shrink* on an edit whose only in-function addition is a call. Read out of the image:
this arm publishes **exactly the 653 arm's twelve keys plus one** —

```
xnu_live_seam_other  xnu_live_seam_other_  xnu_live_seam_calls  xnu_live_seam_op
xnu_live_seam_lr     xnu_live_seam_sp      xnu_live_seam_sctlr  xnu_live_seam_b0
xnu_live_seam_b1     xnu_live_seam_a0      xnu_live_seam_a1     xnu_live_seam_end_run
```

— and the shrink is the **collapse of two epilogues into one**: on the 653 arm both exits must return, so the
compiler emits the callee-saved restore twice; on this arm the publish path never returns, so that epilogue is
dead and is dropped. 11 `bl entry_live_write` -> 12, plus 1 `bl entry_seam_end_run`. Nothing was removed from
the publish path, and the two `FlushPoU_Dcache`/`FlushPoC_DcacheRegion` calls are unchanged
([[mi4-an-edit-on-a-prefix-deletes-the-suffix]] is the rule this paragraph exists to obey).

`xnu_live_seam_end_run` is published **with** the pair and not left to the config record, for the same reason
`xnu_live_seam_op` is: on the operation arm that returns, an absent pair is a seam that was never reached; on
this arm an absent pair *and* no return say the operation hung the machine between the two reads. The two
states are told apart only by that key.

## 5. Parked, recorded, verified — three copies, 11 files each

`out/stage90/frozen/armed-seam-endrun-88972ba9/` (plain `cp -r`, fresh mtimes), the same files in
`/mnt/data/mi4-ios6-export/armed-seam-endrun-88972ba9/`, and the live `out/stage90/`. The record in
`stages/stage90/revert-set.txt` was written **by hand from `sha256sum` on the park's own files**, and
`tools/verify_revert_set.sh` returns **exit 0** on all three:

```
VERIFIED: 11 file(s) of armed-seam-endrun-88972ba9 matched the record ... and 6 manifest-member check(s) agree
```

`tools/resolve_arm_set.sh out/stage90` resolves the live arm to `armed-seam-endrun-88972ba9` /
`94c95342...` / 8540160 — so the runner's `--expect-arm=` can name it, and a press cannot be spent on a stale
caller.

Nine of the eleven files differ from `armed-seam-poc-a43304f2`; the two that agree are the payload's own switch
dump and `stage90_fixture.macho`. The naming follows the rule the three earlier sets follow — **the token is
the artifact that changed, and here that is the entry image** — not the exception 666 broke, and the record
says so, because getting that wrong is the one way this record's own text can mislead a reader into sending the
wrong park.

## 6. The gate: it refuses this arm, and the refusal is one line on the peer lane

Run against the unmodified gate (`preflight_boot_check.sh --allow-xnu-entry`), the arm is refused at
**exit 1, 69 lines**, on exactly one clause:

```
REFUSING: out/stage90/xnu_arm_entry-config.txt carries key(s) this gate does not print:
STAGE90_XNU_SEAM_END_RUN - a switch recorded on the build side and not shown here is a switch the next run
would go out with unread; add it to ENTRY_CFG_KEYS above
```

That is the cross-lane pair ([[mi4-file-lanes]]): `ENTRY_ARM_KEYS` (build) and `ENTRY_CFG_KEYS` (gate) are
compared for equality in both directions, deliberately, and the gate's half belongs to the lane that owns
`preflight_boot_check.sh`. The message went to `run-experiment-526`; **no press may be armed for this arm
until the gate is green on it.**

**And the cost of the peer's half was measured rather than estimated, by rehearsing the peer's file instead of
guessing at it.** A copy of the gate at `stages/stage90/.g678-gate.sh` (a `.sh` copy, invisible to the gate's
own freshness scan — the same device `tools/rehearse_live_path.sh` uses) with **only**
`STAGE90_XNU_SEAM_END_RUN` added to `ENTRY_CFG_KEYS` and nothing else gives **exit 0, 550 lines**. The four
changes the first message asked for reduce to **one**, and the refinement was sent. The copy was deleted
before the tree was read for anything else.

So the arm's own half of the gate is clean: the entry image's blob offset, the 515 token appearing twice, the
seam-body clause (`entry_seam_flush` calls `FlushPoC_DcacheRegion` once), the register-literal scan, the
manifest, the freshness sweep and the device-state clauses all pass under this step's bytes.

## 7. A pre-existing FAIL in the rehearsal, recorded so it is not read as this step's

`tools/rehearse_revert_set.sh` ends **36 ok / 2 failed**, and the failing cell is

```
FAIL  record-covers-gate         the record does not cover: captures
```

`captures` is not a file of the set: it is the **directory** the gate reads a park log out of
(`preflight_boot_check.sh:735` is `-r $OUT/captures/$_f`), and the rehearsal's criterion A is a raw grep for
`$OUT/<name>`, which cannot tell a directory from a file. **It is pre-existing and not caused by this step**,
and that is measured, not argued: the gate source is byte-identical to `HEAD` (`git status` clean on it), the
derivation runs on that text and not on the record, and re-running the rehearsal with `HEAD`'s own record via
`--record=` reproduces the cell word for word, at the same `10 / 5 / union 12 / record 11` counts. Both
`verify_revert_set.sh` runs and the authoritative test (revert, then run the gate) are unaffected. It belongs
to this lane and is a reader's defect, not a hazard.

## 8. What this step does not do

* **It does not answer the frontier's question.** It builds the arm that can; the pair is published by a run
  that must still be fired, and **no press is armed** — the arm is not gated, because the gate refuses it, and
  arming a press on an ungated arm is what the standing rule forbids.
* **It does not falsify 666's numbers.** The 28-second countdown and the 90-second bounded spin are untouched;
  this arm's `STAGE90_HW_WATCHDOG_SELFTEST` is 0, so it does not run the self test at all.
* **It does not change the reset path's own question** ([[mi4-xnu-reboot-path-cannot-reset]]). XNU still
  cannot reset this device; the ending is a hardware write, which is what 663 §3.2 said it had to be.
* **It does not advance 「起码要能进入操作系统，把基础驱动跑起来」.** No boot, no device, no `fastboot`,
  nothing written to storage. **TWRP to storage stays withheld**, because 「如果os已经能进去了的话」 is not met:
  the OS is not observed entering.
* **The 2 MiB L2 comment correction and the self-test tick are carried, not tested.** Their only evidence is
  the byte-identity above plus the two new compile-time `#error`s in `stage90.h`; they are inert in this arm's
  bytes and are the *next* self-test arm's instrument. What *was* exercised is the runner's new reading: it
  prints its **absent** branch on 677's real capture (which predates the instrument) and its **present** branch
  on that capture with three tick lines appended (`selftest_tick_us = 3000000 us`, out of 3 tick(s), exit 0) —
  so both directions are reachable, and the absence is reported as *an image that predates the instrument* and
  not as "the payload lived under a second".

## 9. The next step

The order is 673 §3's, and it is now at its last item before a press:

```
edit -> build -> park -> record -> gate -> re-arm
        ^^^^^^^^^^^^^^^^^^^^  done     ^^^^ blocked on the peer lane's one line
```

When the peer lands `STAGE90_XNU_SEAM_END_RUN` in `ENTRY_CFG_KEYS` and the gate is green on
`armed-seam-endrun-88972ba9` (expect ~550 lines), the press launcher is re-armed for this arm. The reading the
run then produces is: **`xnu_live_seam_b1` against `xnu_live_seam_a1` with `xnu_live_seam_end_run=1` in the same
log**, read with 657's corrected cell table — `a1 == rtcpre_pop` is `STALE LINE, WRITTEN OUT`, `a1 == b1` is
`CLEAN LINE` and must be read *with* `slot_post_calls`/`poll_seq` beside it — and, unlike every seam arm
before it, the reading does not depend on the run getting back to the exit that kills it.
