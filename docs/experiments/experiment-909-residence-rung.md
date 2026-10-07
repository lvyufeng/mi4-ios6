# 909 — the residence rung: a normal-slot boot that does not end

Date: 2026-10-07. Status: **pressed twice. Arm 1 (`armed-storage-e61ce673`) FAULTED at the pet (R7);
arm 2 (`armed-storage-104b10ce`) fixed the fault but the pet's install was REFUSED every call — the
watchdog `0xf9017000` and the GIC `0xf9000000` share one 1 MB block, so `xnu_live_wdt_map=0` ×4 and the
pet never petted (R9). The run was ALSO not resident: it wedges in the deep-idle loop after ~4 passes,
~13.4 s in — the same structural point 908 reached, without 908's deliberate ending. The residence
clause is NOT met; see R9.** The goal's last clause is 「彻底能直接开机就运行xnu」 — 908 proved a plain
power-on *enters* the OS; 909 set out to prove one that *stays*, and R9 shows what still blocks that.

## What 908 left open, stated exactly

908's run (`out/stage90/captures/908-normal-20261007-022629-last_kmsg.txt`) entered the OS — kernel,
`BSD root: md0` (card unit), `/sbin/launchd` exec, pid 1 in user mode, a sustained idle loop — and
then **ended**. The ending is the image's **own deliberateness**, not a fault in what it booted:

- `xnu_live_post_end_calls=0x00000004` — the ending fired on the 4th idle pass.
- `entry_seam_end_run` then stored `RESTART_REASON` at `0x0fa0065c`, which is **not mapped** under
  XNU's post-jump tables → the store faulted:
  `panic(cpu 0 caller 0x804a9908): kernel abort type 4: fault_type=0x3, fault_addr=0xfa0065c` →
  `Attempting system restart...MACH Reboot`.
- The payload's SoC watchdog (armed 25 s bark / 3 s bite, `stage90_hw_watchdog_arm`) was still
  counting behind that store; the deliberate ending came first, so the watchdog never bit.

So there are **two** endings in the image, and a residence rung must neutralize **both together**:

1. **The deliberate ending** — the `entry_seam_end_run` call, reached by
   `STAGE90_XNU_POST_END_TICKS` (an elapsed-ticks deadline) / `STAGE90_XNU_POST_END_RUN` (a pass
   count) at the exit wrapper's tail, or by `STAGE90_XNU_SEAM_END_RUN` before the `pop`.
2. **The hardware watchdog** — armed unconditionally (`STAGE90_HW_WATCHDOG_ARMED`), never petted,
   bites 28 s after `xnu_entry`.

**Removing the deadline alone is not residence.** With `POST_END_TICKS=0` the deliberate ending is
gone, but the 25 s watchdog then resets the phone — a longer run, not a permanent one. The rung must
address the watchdog too, and the safe way is to **pet it, not to disarm it**: a pet is a *write that
fails safely* (an unmapped `0xf9017000` is a fault at the pet site, not a lost power-hold net),
whereas clearing `WDT_EN` removes the one net that rescues the phone from a hung XNU. A5 below
derives the pet address; ops 1-4 (disarm) are deliberately **not** used.

## The rung: `STAGE90_XNU_RESIDENT` (default 0)

A new arm switch, threaded through `build_entry.sh` (`ENTRY_ARM_KEYS`, record, compile define) like
`STAGE90_XNU_HDD_WRITE`. When `=1`:

### R1 — no deliberate ending
The arm's switch set carries `POST_END_TICKS=0`, `POST_END_RUN=0`, `SEAM_END_RUN=0`. With all three
0, `entry_seam_end_run` is never called and nothing in the image ends the run.

### R2 — pet the watchdog, and publish the countdown
The exit wrapper's tail already runs on every idle pass (60931 times in 908) and already hosts the
storage line's post-jump instrument. Extend the **existing** `entry_post_clock` site (guarded by
`STAGE90_XNU_RESIDENT`) to do two things, keeping the wrapper's frame at 8 bytes (all state lives in
`entry_post_clock`'s own frame — the 690 rule):

- **publish** `xnu_live_wdt_sts` (the raw `WDT_STS`) and `xnu_live_wdt_countdown`
  (`(sts >> 1) & 0xfffff`) on `entry_seam_publish`'s power-of-two schedule, so the log carries the
  countdown **over time** — a run whose countdown is seen to *fall and then jump back to the bark
  value* is a run the pet is keeping alive;
- **pet** — when the countdown has fallen below half the bark (a fixed `wdt_bark/2` threshold), write
  `MSM8974_WDT_REG_RST` (`0xf9017004`) = 1, the same word the arm site (`hw_watchdog.c:231`) already
  uses. The pet is a bounded RMW-free single store; a wrong/unmapped address faults at the pet and is
  read from the log, exactly as the ending's store is.

### R3 — the mapping, proven before it is trusted
`0xf9017000` falls inside the 1 MB region 908's storage line **already mapped post-jump**
(`entry_mmio_section(0xf9824000, …)`, the eMMC controller) — different megabyte, same mechanism, and
908's log shows the storage install succeeding after the jump (`xnu_live_storage_blk_*` keys). So
the residence arm does `entry_mmio_section(0xf9017000, 0xf9017000, …)` once, beside the storage
install, before the first pet. Its refusal (return 0) is published, and a pet is skipped when the
Section is not installed — the pet cannot fault through an install that was refused.

### R4 — build refusals (the property is structural, not a comment)
`build_entry.sh` gains, in the same shape as the `POST_END_TICKS` block:
- `STAGE90_XNU_RESIDENT=1` **requires** `POST_END_TICKS=0 && POST_END_RUN=0 && SEAM_END_RUN=0`; any
  nonzero is a refusal naming the clause (an image that carries both ends anyway, and the record
  would name the resident arm it is not).
- `STAGE90_XNU_RESIDENT=1` **requires** `STAGE90_HW_WATCHDOG=ARMED` — a residence arm on a
  watchdog-disabled image would be a run with no net at all, and the pet would have nothing to read.
- A **linked-image clause** (the `nm`/disassembly kind this project uses, `mi4-linked-code-order-is-not-source-order`):
  asserts `entry_mmio_section` is called from the wrapper's own body once for `0xf9017000`, and that
  the pet site's store targets `0xf9017004` **by value**, bound to the anchor's base register.

### R5 — the fixture does not end the run, and needs no change (corrected from the 908 log)
The first draft of this section asserted 908's pid-1 fixture did `exit(3)`. **The 908 run falsifies
that**, and the correction is the reason R5 is closed with no code change:

- The log's `exit 1 call(s), pid 0x00000002, rval 0x00000003` is the **child**, pid **2** — the
  `fork`/`exit`/`wait4` triple the fixture has run since 505/506/508 to prove `wait4` reaps a
  `W_EXITCODE`. The parent's `wait4` reads that status back (`p_xstat = 0x300`); it is a measurement the
  fixture *requires*, not a defect.
- **pid 1 never exits.** The log says so directly at line 4003:
  `pid 1 parked in poll for 2000 ms (caller 0x8028e5b8)`, and the idle path was entered `60931` times
  while parked. The fixture's own tail is `park: poll(NULL,0,PARK_MS); b park` — an unbounded park loop
  (`entry_ramdisk.s`, `park` at +288..+308). A kernel whose init has exited cannot also be parked in
  `poll`, so the ending 908 saw was **R1's deliberate ending**, exactly as this doc's opening says.

So there is **no fixture change**: the resident arm's pid-1 fixture already parks rather than ends, and
R5 is a statement about the fixture that the image already satisfies. What matters is only that the
ending keys (`xnu_live_post_end_calls`, the `fault_addr=0xfa0065c` panic) are absent — R1 — while the
park evidence (`pid 1 parked in poll`, the sustained idle count) is present. If a future press shows
`xnu_live_wdt_*` moving AND the park line, the run is resident; the fixture is not the variable.

## The open unknown, named before the press

**Is `0xf9017000` readable after the jump at all?** If XNU's post-jump L1 does not cover it and the
`entry_mmio_section` install is refused (or its slot is in a table the hardware ignores), the pet's
read `WDT_STS` faults and the run dies at the pet — which the log shows (`xnu_live_wdt_sts` present
= mapped and read; its absence + a fault at the pet site = not mapped). 908 proves the *mechanism*
(a device section was installed and read post-jump), not this *address*. The rung's first press is
therefore also its mapping probe, and its negative cell is a bounded, readable fault, not a hang.

**The second unknown is the watchdog's IRQ.** The bark raises SPI 3 (intid 35); nothing in the image
handles it. A pet that resets the countdown before the bark means the bark never fires — but if a
pet is late, the bark fires and the payload's generic IRQ path EOIs it (`hw_watchdog.c`'s own
comment), which costs one interrupt and no more. So a late pet is survivable; a missing pet is not.

## How the arm is read (the completion bar)

- **kmsg residence trace**: `xnu_live_wdt_countdown` published over many passes, each value **below
  bark** and **bounded** (never reaching 0), with the raw `xnu_live_wdt_sts` moving — the pet keeps
  the countdown pinned low. A run that reaches a countdown of 0 with no reset is the *falsifier* of
  a working pet only if the phone is later found reset (a bite).
- **no ending keys**: `xnu_live_post_end_calls` **absent** (the ending never fired), and no
  `panic(cpu 0 … fault_addr=0xfa0065c)`.
- **the phone stays dark and, at the operator's leisure, a later physical VolDown+Power reaches
  TWRP** — i.e. the device did not reset-loop and did not power off. Duration is bounded only by how
  long it is left; the countdown trace proves the pet ran for the whole of that window.
- **no brick**: the recovery door (VolDown+Power → fastboot → non-persistent TWRP) is verified before
  the press; p19's before-image is retained for `restore_boot_from_xnu.sh --execute`.

## Hazards, stated because this is the first rung that can strand the phone

- **A resident XNU runs no adbd and does not re-init USB.** Once it is resident the *only* exit is a
  physical VolDown+Power into fastboot → TWRP, then restore p19 — the same door 908 used.
- **Deleting the ending is the point, but it is also the one place the project's shared ending
  exists to rescue a hung run.** The pet is chosen over disarm precisely so this rung keeps the
  watchdog net; a pet that is wrong fails *readable* (a fault at the pet), not silent.
- **This arm must not be the only thing armed.** The before-image + verified TWRP are committed to
  disk before the write, and the press is one-way until the operator resets.

## Out of scope

- Disarming the watchdog (`WDT_RST` verification needs a physical power cycle; ops 1-4 deferred) —
  the pet is sufficient for residence and safer.
- A writable resident root (that is the `HDD_WRITE` line, 905/906); this rung is the read-only root
  908 already mounts.
- Any userspace beyond the pid-1 fixture.

## Build order (each a commit)

1. This doc.
2. `STAGE90_XNU_RESIDENT` plumbing in `build_entry.sh` + the three refusals (R4), inert (`=0`
   default) so no existing arm moves — prove byte-identical with `cmp` on the built image.
3. The `entry_post_clock` extension (R2) + the mmio install (R3), gated on `RESIDENT`.
4. The linked-image clauses (R4) + `tools/test_resident_guard.py` (host fakes).
5. The fixture variant (R5).
6. Arm config, park the 11 members, `verify_press_ready.sh`, `make check`, then the operator press.

## R6 — the gate's watchdog-page clause, and why it had to be amended rather than bypassed

The press gate (`scripts/preflight_boot_check.sh`) has a pre-existing clause that scans the entry
image for an address in the watchdog's 1 MB page `[0xf9010000, 0xf901ffff]` in three encodings (a
literal-pool word, a `movt rD, #0xf901` pair, a `mov`/`mvn` immediate), and **refuses** if any code
carries one. Its premise is exactly the residence rung's inverted one: it assumes a page reach is a
run "whose only net is the one the image can touch" — a reach nobody can rescue. R2's pet deliberately
materialises `0xf9017000`, so the clause fired on the parked residence arm `armed-storage-e61ce673`,
and the honest reading is that the clause was right and the arm is the exception it did not know about.

The amendment is **grounded on the artifact, not on a switch the gate remembers** (the same rule the
config-keys block already uses for `HFS_ROOT_MEDIA`/`ROOT_FROM_CARD`/`HDD_WRITE`):

- **The exemption is the pet SYMBOL.** `entry_wdt_pet` is compiled ONLY under `#if STAGE90_XNU_RESIDENT`
  (`entry_trace.c`), so an entry ELF that **defines** it (`readelf -s`, a `FUNC` named `entry_wdt_pet`) IS
  the residence arm and nothing else is. The gate reads that symbol's bin span and excludes it from the
  refusable census. `entry_mmio_section` is deliberately **not** exempt: it links into every image and
  carries no page address of its own, so exempting it would let a future build hide a real reach in the
  one function every image has.
- **The clause is two-sided for the residence arm.** An image that defines the pet must carry the page
  (a pet that reaches nothing cannot feed the net); `entry_wdt_pet`'s four `movt rD, #0xf901` sites are
  the evidence. A non-resident image defines no pet, its exemption set is empty, and the clause for it is
  exactly the one it was: no code may carry the page.
- **The refusal message and the ceiling paragraph change with the arm.** For the residence arm the
  "nothing pets the net, so the run is CAPPED" paragraph is false and is replaced by the countdown-trace
  reading (`xnu_live_wdt_countdown` below the bark and never zero, `xnu_live_post_end_calls` absent).
  Both branches are keyed on the same `_entry_resident_arm` read the config-keys block uses.

Verified in four directions on the parked arm: (1) the residence image passes with `movt 0` refusable and
`movt 4` exempt; (2) the non-resident 908 image (`armed-storage-54d5c585`) is silent as before, `0` in
both counts; (3) an image whose pet words are zeroed is refused (the pet must reach); (4) an image with a
page word injected outside the pet is refused (the original clause, unchanged).

## R7 — the first press, and the defect it found

The press ran, and it **faulted at the pet on the pet's first call**. The log is unambiguous; the cause
is one argument, and it is repaired here.

### What the log says

`out/stage90/captures/909-resident-20261007-0530-last_kmsg.txt`, 16084 lines. The run is *better* than
908 in exactly the way R1 asked and *worse* in one way R2 did not foresee:

- **R1 held.** `xnu_live_seam_end_run=0`, `xnu_live_seam_post_end_run=0`,
  `xnu_live_seam_post_end_ticks=0`; no `xnu_live_post_end_calls`, no `fault_addr=0xfa0065c`. The image
  no longer ends itself.
- **The boot reached the OS.** `BSD root: md0, major 4, minor 2`; `/sbin/launchd` was attempted, did
  not fail; `mini4: the AST is done — pid 1's thread is at 0x10e0 for user mode`. So XNU was up and
  pid 1 was in user mode — 908's whole result, reproduced.
- **Then it died, at the pet:**
  `panic(cpu 0 caller 0x804a9908): kernel abort type 4: fault_type=0x3, fault_addr=0x0`, with
  `pc: 0x80002684`, `lr: 0x80804000`, `r0: 0x00000001  r1: 0xf9017000  r2: 0x00000000  r3: 0x00000000`,
  `r4: 0xf901040e  r5: 0xf9017000  r6: 0x0000000c`, `fsr: 0x805`, `far: 0x0`. `0x80002684` is inside
  `entry_mmio_section` (bin `0x800025f0`, +0x94 — the `str r4, [r2]`), and `r1 = 0xf9017000` is the pet's
  VA. The `xnu_live_gic_*` keys 908 published are absent, so this is the same run's **first**
  `entry_mmio_section` call.

### Why — one argument, and the refusal that could not happen

`entry_mmio_section` ends in `entry_section_install(va, pa, l1, g_live_attr, slot_before_out, desc_out)`
(`entry_stubs.c:2248`), and that function writes its first output **before it can refuse**:

```c
before = *slot;
*slot_before_out = before;                    /* line 2121 — unconditionally, before the test */
if ((before & LIVE_TTE_TYPE_MASK) != 0u)
    return 0u;                                /* the refusal the design relies on */
...
*desc_out = desc;
```

The pet passed `0` for both outputs: `entry_mmio_section(STAGE90_WDT_BASE, STAGE90_WDT_BASE, 0, 0)`. So
`*slot_before_out` is a store to address `0` — the `fault_addr=0x0` panic — and it happens **before** the
`before`-test, so the "a refused install publishes `xnu_live_wdt_map=0` and skips the pet" cell the design
rests on is unreachable: the run cannot observe a refusal, because the store that would have carried the
refusal's own reading is the fault.

The pet is the only caller in the tree that passes NULL for the outputs — `entry_gic.c:393`,
`entry_storage.c:5261/10306/10332` all pass real locals — and it is the arm's *first* call on the first
idle pass, which is why the log shows no `xnu_live_wdt_*` key of any kind.

### The repair

Two lines in `entry_trace.c`: declare `slot_before`/`desc` in `entry_wdt_pet`'s own frame (which is where
690 already puts all of the pet's state) and pass their addresses. This is the shape every other caller
uses. With it, a refused install stores its `before` into a local, returns 0, and the pet publishes
`xnu_live_wdt_map=0` and returns — the bounded negative cell R3 promised.

### R8 — the clause that should have caught it, and did not

`tools/test_resident_guard.py`'s `claim_pet_installs_before_it_reads` asserted
`if (mapped == 0u) return;` and the install-before-read order, so the *design* passed — but neither it
nor the build's linked-image clause ever asked **whether the install call can be refused at all**, which
is the whole of R3. Three new clauses close it:

1. **Source:** the pet's `entry_mmio_section(...)` call must pass a real `&`-output for both
   `slot_before_out` and `desc_out`; a numeric `0` there is a refusal.
2. **Linked image:** `entry_wdt_pet`'s body must materialise a **frame address** into `r2` before its
   `bl entry_mmio_section` — `sub`/`add rX, sp, #N`, or `mov r2, sp` — because a NULL is a `mov r2, #0`.
   The existing `bl` target check did not look at `r2`, which is why the fault got through a green build.
3. **Selftest:** the mutation that restores `, 0, 0)` must be refused.

The lesson is the one `test_resident_guard.py`'s own header states and this press re-paid for: it asserted
the *shape* of the refusal path and never the *reachability* of it. A refusal a call cannot take is a
comment.
## R9 — the second press: the pet's install was REFUSED, because the watchdog and the GIC share one 1 MB block

Arm 2 (`armed-storage-104b10ce`, entry bin `104b10ce`) was staged into p19 and plain-booted
2026-10-07 06:08Z. The escape (operator's VolDown+Power → verified TWRP) captured the RAM console at
`out/stage90/captures/909-resident-2nd-20261007-070047-last_kmsg.txt` (872 535 B, 15 770 lines). The
pet's fault from arm 1 is **gone**: no `fault_addr=0x0`, no panic, and `xnu_live_wdt_map` **is**
published. But it is published **four times, and it reads `0` every time**:

```
xnu_live_wdt_map=0x00000000     (lines 15501, 15593, 15656, 15744 — one per pet call)
```

and **no other** `xnu_live_wdt_*` key appears — no `_pets`, no `_countdown`, no `_sts`, no `_bark`,
no `_slot_before`, no `_desc`. The pet returned at its first branch on all four calls. It never petted.

### The cause, exactly: a 1 MB block cannot hold two devices

`entry_mmio_section` installs a **1 MB section** and `entry_section_install` indexes the L1 by
`va >> 20` (`entry_stubs.c:2118`), refusing when that slot already carries a type bit
(`entry_stubs.c:2122`). The **GIC probe** (`entry_gic.c:393`, `entry_mmio_section(0xf9000000, …)`) runs
**after** the jump and installs the whole `0xf90` megabyte into the live table:

```
xnu_live_gic_map=0x00000001
xnu_live_gic_l1=0x80804000
xnu_live_gic_l1_moved=0x00000001     -> the post-jump table, not the console's latch
```

The watchdog is at `0xf9017000` — **the same `0xf90` megabyte** (`0xf9000000 >> 20 == 0xf9017000 >> 20
== 0xf90`). By the time the pet runs (idle pass 1, after the GIC's install), that slot is occupied, so
`before & 3 != 0` and the install returns 0 *before writing anything*. The refusal is the designed,
bounded cell — R3's `xnu_live_wdt_map=0` — not a fault. The pet's `return` on refusal is correct; the
**address it was handed is what is wrong**. It cannot map one device's 4 KB into a megabyte a neighbour
already owns.

### Why this is the *same* defect as R3, one address over

R3 chose `0xf9017000` and reasoned "the pet installs the watchdog's own 1 MB region"; the install is
per-**megabyte**, and `0xf9000000` (the GIC) is in that megabyte. The pet's fix (arm 2) made the
refusal *reachable*; this reading shows the refusal is *being taken*. So the arm's negative cell is
now measured — and the positive cell (a pet) still needs an install that can land.

### The wrong premise this press retires: "a resident XNU stays dark for 52 minutes"

The run was reported as resident because the phone sat dark for ~52 minutes. The log dates that
claim. The run's own clock (`xnu_live_tmr_dl_now`, the absolute physical-timer count at 19.2 MHz,
`CNTFRQ=0x0124F800`) tops out at **`0x0f48ff6b` = 256 442 219 ticks = 13.36 s**. 908's tops out at
`0x0cfc628d` = 217 866 893 = **11.35 s**. Both runs stop at the **same structural point**:

| key | 908 | 909 |
|---|---|---|
| `xnu_live_poll_seq` | 1–4 | 1–4 |
| `xnu_live_pcx_seq` | 1, 2, 4 | 1, 2, 4 |
| `xnu_live_slot_post_calls` | 1–4 | 1–4 |
| `xnu_live_post_end_calls` | 4 (the deliberate ending) | **absent** |
| max absolute tick | 217 866 893 (11.35 s) | 256 442 219 (13.36 s) |

The wrapper's own counter — `xnu_live_slot_post_calls`, the deep-idle exit's **return count**, published
on the 1,2,3,4,8,16,… schedule by `entry_slot_note(&g_slot_post, sp)` on every exit — reaches **4 and
stops** in both (earlier runs in `out/stage90/captures/` reach `8`, so 4 is a real stop, not the schedule's
ceiling). 908 then *chose* to end at 4 (`STAGE90_XNU_POST_END_RUN=4`); 909 removed that ending and
**still** stops at exactly 4. A **second, independent** counter says the same thing: the payload's user
fixture ends in an **infinite** `b park` loop (`entry_ramdisk.s:74-78` — `poll(NULL, 0, 2000)` then branch
back forever), yet `xnu_live_poll_seq` also reaches only **4** (timeouts 5, 40, 2000, 2000 ms = the two
one-shot calls plus two park iterations). So the run is not resident: it **stops executing** a few
iterations into an infinite loop, ~13.4 s in — after `mini4: the OS has nothing to run — pid 1 parked in
poll`, before the watchdog's bark (25 s), which is why nothing resets. The 52 dark minutes are
post-stop wall-clock, not run lifetime. (The log is complete: 872 KB < the 2 MB console bound and
11 796 records < the 16 384 cap, **no** `xnu_live_capped`.) **Correction (R10): the single arm block
does NOT rule out a reboot loop.** The payload's `log_init` (`src/ram_console.c:11`) resets
`rc->size = 0` on **every** payload entry, at the same base `0xde500000` the live channel appends to
(`entry_write_kv`), so a warm reset **clears** the buffer — a boot-loop reads as one complete session,
and a read always shows the *current* iteration. The halt-vs-loop question is therefore **open**.

### What arm 3 must do, and the third finding it also carries

1. **Give the watchdog its own megabyte.** `0xf9017000` cannot be installed while the GIC owns
   `0xf90`. The pet must map a section whose `>> 20` is free — and the GIC already mapped the
   watchdog's own 4 KB when it mapped `0xf9000000`, so the watchdog may be **readable at
   `0xf9017000` through the GIC's existing block with no install at all**. Arm 3 either drops the
   install and reads directly (proving the address via the GIC's mapping), or installs a **different**
   free megabyte and proves *that* lands. The build clause must refuse an install whose `va >> 20`
   equals a device section another probe already installs.
2. **The wedge at idle pass 4 is a separate frontier and it is the real blocker.** The pet cannot fix a
   run that stops taking idle exits within seconds — even a landed pet feeds a watchdog the wedge no
   longer outlives. Before a further pet arm, the ~13.4 s wedge must be explained: both 908 and 909 stop
   after 4 deep-idle exits with no fault, no panic, and no further publishes. That is a hang, and a hang
   is what the watchdog's bite exists to break — but the bite (~28 s) never came either, which needs its
   own answer.

### The honest status of the goal

「彻底能直接开机就运行xnu」 is **not met**. A plain power-on enters XNU and reaches the OS's idle loop,
then wedges at ~13.4 s (dark), not resident. 909's pet does not yet land (block collision), and even
landed it would feed a run that has already stopped. The residence clause needs both the block fix and
the wedge answered.

## R10 — the stop, discriminated: the wfi returns; the re-arm is what fails; and the willingness to loop is open

An read-only investigation of the ~13.4 s stop produced five verdicts. Two confirm R9; two narrow it;
and one corrects a claim R9's addendum pushed.

### Ruled out

- **A software dead-man.** It is armed only in the payload as a GIC timer PPI
  (`stage90_arm_pc_sampling_watchdog`, `src/gic.c:519`) and **disarmed immediately before the jump**
  by `stage90_disarm_deadman_timer()` (`src/xnu_entry_jump.c:265`, body `src/gic.c:630-690`). The log
  confirms: `disarm_isenabler0_before=0x000c7fff → after=0x00007fff`, `disarm_cntp_ctl_before=0x5 →
  after=0x2`. Nothing in the payload runs after the jump, so it has no post-jump kicker.
- **A stuck `wfi`.** This is the report's own discriminator and it decides the question.
  `entry_note_wfi` (`src/entry/entry_stubs.c:5443`) publishes `xnu_live_wfi_before` and
  `..._after` from the same call, so a run that *parks* inside the `wfi` publishes `before` and no
  `after`. 909's log has **four complete pairs** —

  ```
  before=0x0616a6e9 after=0x0862e620  (Δ 0x024c3f37 = 2 406 199 ticks = 2.007 s)
  before=0x08637ec0 after=0x0aafd339  (Δ 0x024c5479)
  before=0x0aaffcfa after=0x0cfc58e0  (Δ 0x024c5be6)
  before=0x0cfc8a12 after=0x0f48ec3f  (Δ 0x024c622d)
  ```

  every `after` present, each window ~2.007 s (the fixture's `poll(NULL,0,2000)` park,
  `entry_ramdisk.s:192-197`). The `wfi` returned all four times. **So the run does not park in the
  `wfi`**; it stops *between* the fourth `wfi`'s return and the fifth. The one-shot `CNTP_TVAL`
  wakeup works, and the failure is in what happens at the wakeup — the re-arm into the next `cpu_idle`
  pass — not in the sleep itself.

### Narrowed, and the new top candidate

- **The watchdog is the only physical timer still counting.** `CNTFRQ=0x0124f800` = 19.2 MHz;
  the pet never fed it (R9). Computed from the log's own words (`hw_watchdog_hz=0x7ffd` = 32765,
  `src/hw_watchdog.c:103`): bark `0x000c7fb5` = 817 077 ticks = **24.94 s**, bite `0x000dffac` =
  917 420 ticks = **27.99 s**. The stop is at 13.36 s — **~14 s before the bite** — so a bite cannot be
  the ~13.4 s stopper. But because `log_init` clears on re-entry (correction above), a bite at ~28 s
  **would reboot into the payload and clear the buffer**, so the run's *terminal* mechanism (halt vs
  loop) is **not yet decided** — the last complete transition is the 4th wfi return at ~13.36 s, and
  what follows it is the open cell.
- **The idle machinery stops making progress after the 4th pass** (the strongest structural
  signature). `xnu_live_idle_seq` (powers of two, `src/entry/entry_stubs.c:5159`) tops out at
  **`0x8000` = 32768**, while the `mini4:` console reports **60931** idle entries at the park's return
  (`entry_trace.c:1607`; the summary prints only when `poll` returns, `:1565`). Since 32768 is the
  highest power of two ≤ 60931, the kernel made **almost no further `cpu_idle` entries after ~2 s**.
  And `mini4:`'s `platform_cache_idle_enter entered … 1` against `xnu_live_slot_post_calls=4` says the
  cache-window **enter ran once while its exit returned four times** — at least three of the four
  "exits" were not on the normal enter→wfi→exit path. Both runs share the 1-vs-4 mismatch.

### The cheapest discriminating arm

**Land the pet's install** (arm 3's block fix) so it publishes `xnu_live_wdt_map=1` and, with it,
`xnu_live_wdt_countdown`/`_sts`/`_pets`/`_base`/`_bark`. The register read the pet already attempts
(`(STS >> 1) & 0xfffff`, `entry_trace.c:2475`) is exactly the discriminator: a countdown that **rises
toward the bark with no pet** means the watchdog is still counting and will bite; a countdown **frozen**
means the register is dead and the stop is purely in-kernel. It is one refusal away from working (the
code is already in the image), needs no new hook, and its negative cell is currently the only thing the
log cannot answer. A zero-build cross-check, if the phone were still dark, would be to read `0xf9017008`
(`WDT_EN`) out of TWRP — still 1 after the run means no bite occurred.

### What R10 changes about R9

R9 answered "the run stops at 13.36 s, one arm block, no reboot". R10 keeps the 13.36 s and the stop
**but withdraws the "no reboot" half**: a reboot clears the buffer, so the single arm block is not
evidence against it. The stop's *last recorded transition* is real (the 4th wfi returned); whether the
run then halted or looped-until-a-bite is the next thing to measure.

## R11 — arm 3: read the watchdog through the GIC's own block (built, parked, NOT pressed)

Arm 2 answered R9's cheapest question with a **refusal**: `xnu_live_wdt_map=0x00000000` on all four
pet calls and **no other `xnu_live_wdt_*` key** — the pet ran, asked for the watchdog's mapping, was
refused, and (as designed by R3) published nothing it could not vouch for. The cause is structural and
was named in R9: `entry_mmio_section` installs a **1 MB section** and `entry_section_install` indexes
L1 by `va >> 20`, so `0xf9000000` (the GIC, installed post-jump) and `0xf9017000` (the watchdog) are
the **same** megabyte `0xf90`. The GIC probe got there first; the pet's install found the slot occupied
and returned 0.

**The fix is not a new mapping — it is the realisation that none is needed.** The GIC's own section
already maps `0xf9000000..0xf9100000` **at the same VA**, so `0xf9017000` is readable *through* it.
But "readable through it" is only true if the descriptor in that slot is the 1 MB block the GIC
installed, so the pet does not assume it: `entry_section_install` copies the pre-existing descriptor
out to `*slot_before_out` **before** its refusal test (`entry_stubs.c:2121`), and the new predicate

    wdt_desc_maps_the_block(desc) := (desc & 0x3) == 0x2  &&  (desc & 0xfff00000) == (0xf9017000 & 0xfff00000)

accepts the fallback only when that slot holds a **section** (type 2) whose PA base is the watchdog's
own megabyte. A page-table pointer, a different descriptor type, or a different megabyte all fail the
predicate and the pet returns without reading a byte — the same "stay bounded" posture R3 established,
now applied to a slot the pet did not install itself.

**What arm 3 publishes**: `xnu_live_wdt_via` (1 = the pet's own install succeeded, 2 = the watchdog is
read through the GIC's block), and `slot_before`/`desc` are now published **unconditionally** (they are
plain stores into the pet's own frame — arm 1's fault was passing `0, 0`, not publishing them). The
expected reading on this arm is `xnu_live_wdt_map=0` (the install still refuses — that cell is
unchanged) **with `xnu_live_wdt_via=2`** and, if the register is live, `xnu_live_wdt_countdown` present.
That count is what R9's discriminating arm wanted: rising toward `0x0c7fb5` with no pet ⇒ the watchdog
is counting; frozen ⇒ the register is dead and the ~13.4 s stop is purely in-kernel.

**The guard grew a predicate, not a comment** (`tools/test_resident_guard.py`):
`claim_descriptor_guard_is_a_real_predicate` binds `STAGE90_WDT_BLOCK_MASK == 0xFFF00000` and
`STAGE90_WDT_TTE_BLOCK == 2` **by value**, checks the guard tests *both* the type and the PA base, and
five new mutations (`the_fallback_guard_is_dropped`, `..._drops_the_type_test`, `..._drops_the_base_test`,
`..._uses_the_wrong_type`, `..._uses_a_wrong_mask`) each turn it red — 32 mutations total.

**Nothing moved that must not.** The pet grew `.text`, so the idle-exit wrapper moved `804d1de0` →
`804d1e20`, but the **SEAM_LR is unchanged at `0x8004d2dc`** — the entry-group page did not move, so
there is no two-copy pin (`mi4-entry-group-page-move-pins-two-copies`). The payload record is
**byte-identical** to 908/arm-2 (`6c2b6038…`) — `RESIDENT` is an entry switch, not a payload switch.
Arm 3 is parked as `armed-storage-a703257d` (11 members, all byte-identical to `out/`, `SHA256SUMS.txt`
560 B), recorded in `records/revert-set.txt`, `verify_press_ready.sh` **5/5**, `make check` exit 0.
It is **built and parked, NOT pressed** — the press is the operator's.

## R12 — the stop is in the 5th idle pass, and the publisher had sampled it away

Arm-2's log, read more carefully than R10 read it, carries two counters that disagree:

| key | value | how it publishes |
|-----|-------|------------------|
| `xnu_live_idlestack_calls` | **5** | unconditional, at `entry_trace.c:2190`, with the **D-cache on** (the enter wrapper's own top) |
| `xnu_live_slot_post_calls` | **4** | `entry_slot_publish`'s schedule (`<= 4`, then powers of two) |

`idlestack_calls` is taken **before** `__real_platform_cache_idle_enter` — with the cache on, in the
wrapper's own frame — so it is the true count and cannot be a stale read. `slot_post_calls` is the
**exit** wrapper's, two lines before the pet. So the boot **entered the 5th idle pass and never came
out of it**, and on arms 2/3 that fifth pass is invisible to *every* scheduled site, because
`STAGE90_SLOT_LIVE_MAX` is 4 and 5 is neither `<= 4` nor a power of two. The counter is right; the
schedule sampled away the one call the whole question is about — the same class of miss 520's ninth,
fatal abort was, where `STAGE90_SLOT_AB_MAX` was widened for exactly this reason.

**The corroborating traps (why R10 could not see it).** `xnu_live_wfi_seq` and
`xnu_live_pce_after_seq` both publish the value **1 four and five times** — because they are
incremented *inside* the cache-off window (`entry_note_wfi`/`entry_note_pce_after` run between the
real enter and exit), so their stored count is a stale DRAM read; the source says so. And
`xnu_live_pcx_entered` reads 4 — but `entry_note_pce` also publishes only on the powers of two, so 4
is the highest power of two ≤ the true 5. The single-boot check holds: one `xnu_entry_status` line,
one live header — this is two counters of **one** boot, not two boots.

**Arm 4 — `armed-storage-4f4111fa` (built, parked, NOT pressed).** The only source change from arm 3
is `STAGE90_SLOT_LIVE_MAX 4u -> 8u`, so the exit wrapper's own sites now publish count **5**. The
`pcx` gate is deliberately left at `n == 1 || power-of-two`, so `slot_post_calls=5` beside a
`pcx_seq=4` is itself the sub-gap's reading. Arm 3 was never pressed, so arm 4 keeps its watchdog
fallback unchanged and one press answers both questions:

- `xnu_live_slot_post_calls = 5` → the 5th pass reached the exit tail and died in the **2-instruction
  pcx/pet gap** (a `bl` to `entry_note_pcx` or to `entry_wdt_pet`).
- `xnu_live_slot_post_calls = 4` → it died **at or before** the real `platform_cache_idle_exit` — the
  known `pop {fp, pc}` frontier.
- `xnu_live_wdt_via = 2` and `xnu_live_wdt_map = 0` (the install still refuses) with
  `xnu_live_wdt_countdown` — rising toward `0x0c7fb5` ⇒ the watchdog is counting and will bite;
  frozen ⇒ the register is dead and the stop is purely in-kernel.

**Nothing moved that must not.** The switch set is identical to arm 3 (only the artifact hash differs,
so no `STAGE90_ENTRY_ARM_CHANGE`); the payload record is **byte-identical** to 908/arm-2/arm-3
(`6c2b6038…`); the entry bin is the same 6498612 bytes and the seam check passed, so `STAGE90_XNU_SEAM_LR`
is still `0x8004d2dc` (no entry-group page move). Parked as `armed-storage-4f4111fa` (11 members,
`SHA256SUMS.txt` 560 B), recorded in `records/revert-set.txt`, `verify_press_ready.sh` **5/5**,
`make check` exit 0.

## R13 — the cross-run idle census: the 5th pass is the longest idle this project has ever run

Arm 4's design rests on a claim that is checkable without the device: that the ~13.4 s stop is inside
the **5th** idle pass. The whole capture corpus makes that claim sharper than a single run can.
`xnu_live_idlestack_calls` (the idle **enter** wrapper's count, published unconditionally with the
D-cache on) and `xnu_live_slot_post_calls` (the **exit** wrapper's, sampled) over every `last_kmsg` in
`out/stage90/captures/`:

| runs | `idlestack_calls` | `slot_post_calls` | `post_end_calls` | how it ended |
|------|-------------------|-------------------|------------------|--------------|
| rungs 9–58, 690, 697 | **8** | 8 (full sample) | 8 | the deliberate ending |
| rung 30, 33–37, 39–42, 47–53, 56, 58 | 8 | 8 | 8 | the deliberate ending |
| rungs 12–16, 29, 31, 32, 38, 43–46, 690 | **7** | **4** | 7 | the deliberate ending |
| rungs 17–22, 24 | **6** | 4 | 6 | the deliberate ending |
| **909 arm 2** | **5** | **4** | *none* | **the stop under study** |
| 908 (both) | 4 | 4 | 4 | the deliberate ending |
| 689 | 2 | 2 | 2 | the deliberate ending |
| 687, 692, 909 arm 1 | 1 | 1 | 1 / none | the ending / the pet fault |

Three things fall out, none of which the single-run reading of R12 could show:

1. **No run has ever idled past 8 passes, and every run that idled more than once had its ending armed
   to fire at exactly its `idlestack` count.** The ending has been the thing that stops the idle path in
   *every* measured run but 909's — so 909-2nd's 5 passes with no ending is the **longest un-ending
   idle this project has ever observed**, and it is also the one that stopped.
2. **`slot_post_calls` is a floor, `idlestack_calls` is exact.** Rung 12 is the proof in one row:
   `idlestack=7` beside `slot_post=4`. So R12's "the 5th pass is invisible" is not a subtlety of one
   schedule — it is why every count of 5, 6, 7 reads as 4 in every log in this table.
3. **The stop is not at a count, it is at a time** — 13.36 s, and the fixture's own polls carry a
   2000 ms timeout (`xnu_live_poll_timeout_ms=0x7d0`, `poll_ticks≈0x024c7691`≈2.007 s), so the idle
   thread makes roughly one pass per poll period. The 5-pass census and the 13.4 s clock are the same
   number read two ways.

**What this changes about arm 4.** Its discriminator is unchanged and now better grounded: `slot_post_calls`
publishing **5** under the widened bound puts the death in the exit wrapper's own tail (the pcx/pet gap);
a **4** puts it at or before the real `platform_cache_idle_exit` — and either way the count is the first
honest census the idle path has had without an ending cutting it first.

**The honest risk.** If the idle path has always faulted at ~5 passes, then 908 did not prove "a plain
boot stays" either — its 28 s was an ending at 4 passes, not a survival. That would move the residence
rung's object from "remove the ending" (done) to "fix whatever the idle path does on its 5th pass", and
that is the object arm 4's press is meant to name.

## R14 — arm 5: the 5th pass is a 5-way bisection, and arm 4 was one gate short

Arm 4 widened `entry_slot_publish`'s bound (4 -> 8), so its press can say `slot_post_calls=5` (the 5th
pass reached the exit wrapper's tail) or `4` (it died at or before the real exit). Reading arm 2's log
one layer further shows arm 4 is **one gate short**, and that the death is narrower than either cell:

- **`xnu_live_idlestack_calls=5` is a HARD ceiling.** It is the idle **enter** wrapper's count, and
  `entry_idle_stack_note` publishes it **unconditionally** (`entry_stubs.c:6199`, no schedule gate), with
  the D-cache on. The live channel was only 72% full (11797 of 16384 records, `xnu_live_cap=0x4000`), so
  this is not truncation: the idle path was entered **exactly five times and never a sixth**, then the
  run wedges.
- **The 5th pass's only record is that enter.** In file order the tail is
  `... wfi(pass4) -> slot_pre=4 -> slot_post=4 -> pcx=4 -> pcx_after=0x0f48f615 -> (poll_over, tmr_dl, now=0x0f48ff6b)
  -> idlestack=5 -> EOF`. There is **no `wfi` record for the 5th pass** — the last `wfi`
  (`before=0x0cfc8a12`, `after=0x0f48ec3f`, pass 4) is the pass *before* it. So the death is **inside the
  5th `platform_cache_idle_enter`** (the cache-off window: `platform_cache_disable()` + `CleanPoU_Dcache()`,
  `IDLE_CACHE_ENABLE=0`) **or its `wfi`** — not at the exit wrapper arm 4 was aimed at.
- **Why the four other sites go silent at 5.** `entry_note_pce` (`:5545`), `entry_note_wfi` (`:5462`),
  `entry_note_pcx` (`:5580`) and `entry_note_pce_after` (`:5645`) all gate on `n & (n-1) == 0` (or
  `n==1 ||`). Powers of two: 1,2,4,8. **5,6,7 never publish.** Arm 4 widened only `entry_slot_publish`,
  so on arm 4 the 5th pass would appear at the exit wrapper's sites and remain invisible at the enter,
  the `wfi` and the exit's first act — exactly the sites that bisect it.

**Arm 5 = arm 4 + the four idle-path gates widened** (`armed-storage-b459a858`, entry bin
`b459a858…`, 6498612 B). One macro, `STAGE90_IDLE_PATH_LIVE_MAX 8u`, and
`STAGE90_IDLE_PATH_PUBLISH(n)` = `n <= 8 || power-of-two`, applied to the four gates above. One press
then reads, in file order, the last site the 5th pass reached:

| last key present | where the 5th pass died |
|---|---|
| `idlestack=5` only | before `entry_note_pce_after` — inside the real enter's *body* (the cache-off window) |
| `+ pce_after_seq=5` | the enter returned; died before the `wfi` |
| `+ wfi_seq=5` (`before` and no `after`) | **at the halt** — the CPU never came out of the `wfi` |
| `+ pcx_seq=5` | the real exit returned; died in the exit wrapper before the `pcx` note |
| `+ slot_post_calls=5` | reached the far side of the `pop` — the frontier 519/520 named |

Arm 5 keeps arm 4's `slot_post` bound **and** arm 3's watchdog fallback (`xnu_live_wdt_via`), so if the
pet succeeded in reading through the GIC's block the same press also returns `xnu_live_wdt_countdown`.

**Nothing that must not move.** No switch moved (entry-config identical to arms 3/4 but for the artifact
hash, so no `STAGE90_ENTRY_ARM_CHANGE`); the payload record is byte-identical (`6c2b6038…`, the seventh
time); the seam check passed, so `STAGE90_XNU_SEAM_LR` is still `0x8004d2dc`; 6498612 bytes as before
(the threshold is an immediate). Parked `armed-storage-b459a858`, `verify_revert_set` 11/11,
`verify_press_ready` 5/5, `make check` exit 0. **Built and parked, NOT pressed.**
