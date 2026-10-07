# 909 — the residence rung: a normal-slot boot that does not end

Date: 2026-10-07. Status: **built and parked (`armed-storage-e61ce673`), gate-green, `make check` 0;
not yet pressed — PRESS IS THE OPERATOR'S.** This closes the last open clause of the goal
「彻底能直接开机就运行xnu」 — 908 proved a plain power-on *enters* the OS; this proves one that *stays*.

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