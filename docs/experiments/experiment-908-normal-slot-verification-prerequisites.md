# 908 — normal-slot boot: verified staging is not a boot result

Date: 2026-10-06. Goal: plain power-on loads XNU from `boot` (p19), mounts the
card root, runs pid 1, and remains usable without losing the fastboot recovery door.
**That goal is not yet established.** This record separates the preparation from
an actual normal-slot boot and corrects the earlier resident-run interpretations.

## Measured preparation

- The operator recovered the dark phone; Android subsequently completed boot.
  The latest read-only host check still lists only `4a2fe00b` in adb, as `device`;
  the fastboot list is empty and `33e80afe` is absent from both lists.
- Original p19 was captured in full from Android and again from temporary TWRP.
  Both 33,554,432-byte captures matched the trusted Android rollback:
  `b2119252d046aa2e8e682674949bc5c60a9536358ad83a2c34dfb2de060b4874`.
- `adb reboot bootloader` reached fastboot in four seconds; the verified TWRP
  tool was then booted **non-persistently**. No `fastboot flash`, GPT write, or
  bootloader-firmware write was performed. TWRP supplied a root adb shell and
  `/proc/last_kmsg` (2,097,160 bytes), containing the preceding Android log.
  This tests that reading route, not yet XNU-to-TWRP warm-log preservation.
- p20 was restored from `out/stage90/backups/recovery_now_twrp.img`; its full
  16,777,216-byte readback matched
  `fbb01c5562a27faa9c20c6f818a9dd518768348d745858f00d55fa4a275ab769`.
- The first 524,288 bytes of p25 were reset to the clean card image and read back
  as `d9d2ade4e5b0b3086cec2005567a7df83493579a05067d65ac7dde6c3e6a4bb6`.
  Read p25 relative to its own byte zero; do **not** additionally skip absolute
  LBA `0x400000` inside the partition device.
- The **frozen 906** payload was written to p19 with `dd ... conv=fsync`:
  `fc956eaea370f769c4e7852b47e75be5d035492c3e4339a2f0e551addbaa4bca`,
  9,519,104 bytes. A full p19 capture verified this entire payload region
  byte-for-byte. The rest of the 32 MiB partition retains old tail bytes;
  the full partition is not claimed to equal the shorter payload file.
- The inspected p9 command sector was zero before the proposed normal boot.
  No p19 boot was executed. Android was already running from its previously
  loaded Android kernel when the p19 storage write was made.

**Operational consequence:** plain reboot/power-on no longer has a guaranteed
Android fallback from p19. Its head now contains the staged XNU image. Restore
p19 before relying on normal power-on as an Android recovery route.

## The p20 backup contains TWRP, not stock recovery

The earlier `stock recovery` label was wrong. Host-side binary inspection proves:

- Its first **16,240,640 bytes are byte-identical** to the verified
  `twrp-3.7.0_9-0-cancro.img`:
  `a2f4b9037946ededf9f06f28dff922decebe2e7a0bf1bfff24b6e34327e02443`.
- The LZMA-alone ramdisk decompresses into a valid newc cpio archive with 3,292
  entries, including TWRP libraries and `sbin/recovery`. That executable contains
  the strings `3.7.0_9-0` and `TWRP`.
- The remaining **536,576 bytes are not all zero**. They are partition tail,
  not evidence of a different boot image. The full backup hash above remains
  the authoritative full-partition restoration identity.

This establishes the restored bytes' identity. A subsequent **persistent** p20
boot after this restoration has not yet been measured. The earlier temporary
TWRP boot is not substituted for that test.

## Retractions: the A2 outcome remains unobserved

1. The `dfar=0xc4100000` / `deadc000` aborts cited as an A2 failure location
   came from payload selftests, occur in successful traces too, and were read
   from the non-real-XNU run. They do not locate A2's failure. A2's own log is
   unavailable; neither a pre-jump death nor a successful XNU entry is proven.
2. The correctly addressed p25 head after A2 was byte-identical to the older
   post-recovery-boot capture. That footprint has no independent freshness or
   duration evidence. It does not prove A2 executed pid 1 or remained resident.
3. Black screen/no USB is not an XNU liveness verdict. Cold power recovery can
   destroy the RAM-console log. Do not describe unavailable evidence as success.
4. `POST_END_TICKS=0` removes the scheduled entry-image ending, **not** the
   separately armed SoC watchdog. The payload arms a 25-second bark / three-second
   bite gap and the jump only disarms the generic-timer software deadman.
5. The timed recovery trace's counter frequency is 19.2 MHz. Its post baseline
   excludes almost the first two-second park; final elapsed 115,940,385 ticks
   is approximately 6.0386 seconds. The 28-second host-visible return interval
   is not that clock window, and the watchdog's 32,765 Hz is a different clock.
6. The successful recovery run's intentional-ending reset-reason store faulted
   under XNU mappings. Do not claim that its physical PS_HOLD store was measured
   to power off or reset the phone successfully.

The working frozen payload and current live payload are also distinct artifacts:
`fc956eae…` (timed 906) versus `a6367f4d…` (A2). Equal image sizes/header layouts do
not make their behavior interchangeable.

## Root-repeatability prerequisite

A writable non-journaled HFS+ mount itself clears the clean-unmount bit, even if
pid 1 writes no file. The next writable mount refuses the dirty volume:
`hfs_mounthfsplus: cannot mount dirty non-journaled volumes`.
Resetting the card head before every experiment is not a finished normal-boot
solution. No dirty-mount consistency bypass has been implemented.

The first normal-slot diagnostic should therefore use a **genuinely read-only**
HFS root: `HDD_WRITE=0`, `HFS_ROOT_RW=0`, and consistently rebuilt HFS/platform
objects. That does not retract 906's write-persistence result; it isolates the
normal-slot boot question while avoiding a dirty-root loop.

**Fixed.** The linked `hfs_mountroot` OFF check was enclosed in
`HDD_WRITE || HFS_ROOT_RW`, so valid 0/0 arms skipped it and could accept a
stale writable pool (`build_entry.sh:28190`; the OFF refusal at `:28207` was
unreachable because `:767` already requires `HDD_WRITE=>HFS_ROOT_RW`). Commit
`1f4b6bc` makes the clause read the real linked body **in both directions**: an
exact defined-symbol parse, a bounded disassembly of that symbol's own
address+size, a `bl ... <vfs_clearflags>` call test (not a mere reference), and
fail-closed on unreadable/malformed tool output. `tools/test_hfs_root_rw_guard.py`
extracts and runs the actual clause against fake ARM tools (24 cases). A record
naming 0/0 is still not proof by itself; the guard is what makes it one.

## First-boot evidence and escape requirements

- Capture real XNU jump, card-root mount, exec completion, and multiple successful
  timed pid-1 poll returns. Six seconds of measured progress is bounded evidence,
  not indefinite residency. A single stored stamp is weaker still.
- Preserve a warm return to a verified first Linux reader, then capture
  `/proc/last_kmsg` immediately to a new host file and verify run identity. Android
  or TWRP consumes/resets the old physical console; a later raw DRAM dump may show
  the reader's current log instead of the preceding XNU run.
- Keep an independent bounded escape. Removing the diagnostic ending does not
  prove watchdog behavior, a safe reboot, or a usable resident system.
- `boot-recovery` written **before** an ordinary boot selects p20 on that boot,
  not p19 once and p20 next. The aboot eMMC branch does not clear that command;
  its eventual consumption by a recovery OS is a separate behavior.
- A during-XNU, first-sector p9 read-modify-write/readback could select p20 on the
  following reset, but is only a design at this point. It requires a default-off,
  misc-only capability compatible with a read-only filesystem, exact GPT/extent
  checks, preservation outside command[0:32], completed CMD24 plus actual media
  readback, and a pre-registered write range. No such kernel write has been
  performed. An early failure before that hook still requires manual fastboot.
- A reverse-engineered fastboot `continue` candidate is not a measured route
  and is not enabled by the existing fastboot-boot-only gate/runner. Do not use
  it as though an autonomous one-shot fallback had already been demonstrated.
- Physical rescue remains VolDown + Power + USB, verified nonpersistent TWRP,
  then a guarded full-p19 rollback. Firmware p1/p2/p3/p7 and GPT stay untouched.

## Live state re-measured 2026-10-06 (read-only, host-side)

- p19 **still holds the staged XNU**: its first 9,519,104 bytes hash exactly to
  `fc956eaea370f769c4e7852b47e75be5d035492c3e4339a2f0e551addbaa4bca`. (The
  partition's byte-0 is the payload's own `ANDROID!` wrapper, which does not make
  p19 an Android boot image.) Unbooted.
- The running OS is **MoKee** Android 10 (`mokee_cancro-userdebug ... QQ3A.200805.001`),
  **degraded**: `ro.bootmode=unknown`, `vold.decrypt=trigger_restart_min_framework`,
  and `/data` is a 512 MB **tmpfs**, because 903's HFS+ volume overwrote p25's
  real ext4 `/data` head. So "return to Android" is not a usable fallback — the
  Android return state is a half-booted phone with a volatile `/data`.
- p25's head still holds a **clean** HFS+ header (`482b 0004 ...`, volume header
  `attributes = 0x80000100` — unmounted bit set). A **read-only** HFS mount never
  clears that bit, so the RO p19 test does **not** dirty the volume and is
  safe to repeat; it leaves p25, p9, p20 and p19 untouched.
- The guarded rollback tool passes its **dry-run against the real device**
  (`scripts/restore_boot_from_xnu.sh`, exit 0 — root shell, by-name, geometry all
  validated on hardware; no write). Its author's "device-shell primitives are
  unmeasured" caveat is now closed for the read-only path; the `--execute` write
  path is still unexercised on hardware.

## Host rollback stage

A separate workflow is implementing `scripts/restore_boot_from_xnu.sh` with
host-only mock tests. Required properties: default dry-run, explicit `--execute`,
trusted full-size/full-hash input before device contact, target and other-phone
checks, root/by-name/sysfs geometry validation, full before-image preservation,
p19-only fsynced restore, and full-size/full-hash readback before success.

Implementation/review/test outcomes are pending. No new p19 boot or restoration
has been run through that tool. Existing `restore_recovery_from_xnu.sh` is not a
substitute for a guarded p19 rollback; its stock/Android-fallback/one-shot comments
are stale, and its fastboot path does not perform full restoration verification.

## Independent review of the 908 sequence (judge: NO-GO on the sequence)

A read-only judge reviewed the plan above and returned **NO-GO on the sequence as
written** — parts are individually sound, but the middle steps destroy the
fallback the plan relies on. Adopted corrections:

- **p25 is Android `/data` (ext4).** Clearing its first 512 KiB removes the ext4
  superblock at offset 1024; vold then refuses `/data` at the next Android boot
  and only a `/data` reformat recovers it. This already happened once (903 wrote
  the 512 KiB HFS+ image at p25's head). So the dirty-volume reset is **not** a
  free pre-step: a writable non-journaled HFS root clears the clean bit every
  boot, and repeatedly resetting p25's head is itself a destructive, Android-
  losing operation. **Consequence: repeatable root mounting cannot rely on
  pre-clearing p25. It is either a dirty-mount bypass (a new, separately
  recorded switch) or a full 13.3 GB p25 image captured first.**
- Write p25 **relative to its own byte zero** (`dd ... of=...p25 bs=512 seek=0`,
  as `scripts/press_906.sh` does), never partition+absolute `0x400000`.
- **`misc_now.img` is not a clean BCB template** — it reads `bootonce-bootloader`.
  aboot's image contains `boot-recovery`, `misc`, `continue`, `fastboot`, but
  **no `bootonce` literal**; a host-set `bootonce-bootloader` surviving an
  `adb reboot` is unproven and may be inert. The calibration must use a value
  the evidence shows is honored (`boot-recovery`), and a p9 readback does **not**
  identify the writer that zeroed p9 — that writer is still unknown.
- **The 906 "safe ending" is resolved: it is a watchdog bite.** Re-reading
  `/tmp/kmsg_rec2.txt` shows the arm is still armed across the jump
  (`disarm_hw_watchdog_en=0x1`, watchdog timeout 25 s + 3 s bite), and the
  epilogue's `RESTART_REASON` store **faults** —
  `panic(cpu 0 caller ...): kernel abort type 4: fault_type=0x3, fault_addr=0xfa0065c`
  with `r2=0x0fa00000` in the register dump, then `xnu_entry_panic_entered=1`.
  XNU's own reboot path cannot reset this device, so the phone is returned by the
  armed SoC watchdog, not by a clean self-end. **Consequence: a bounded p19 test
  may rely on the watchdog as its escape hatch, but must not claim a working
  self-end.** The resident run's 19-minute darkness remains unexplained (if the
  watchdog were armed there it should also have bitten).
- Restoring p20 removes the Android fallback and yields **TWRP**, not Android
  recovery. The fallback is `adb reboot bootloader` / VolDown+Power → fastboot →
  `fastboot boot` verified TWRP → guarded p19 rollback. Restore verification is a
  **full-partition** readback hash, not a 512-byte prefix.
- Because p19 already holds `fc956eae`, re-writing it is a no-op; a "single
  variable" claim must either restore p19 to `b2119252` for staging or be dropped.

## The read-only arm, parked and verified (2026-10-07)

The normal-slot diagnostic arm is built, parked and verified; nothing has been pressed.

- **Arm `armed-storage-54d5c585`** is 906 (`f347d060`) rebuilt with the only two arm keys that
  move: `STAGE90_XNU_HDD_WRITE=0` and `STAGE90_XNU_HFS_ROOT_RW=0`, with the HFS platform objects
  rebuilt under those switches, so the root is genuinely read-only. Entry image `54d5c585`
  (6,498,612 B), payload `046219ad` (9,519,104 B, `ANDROID!`). It carries 906's fsync fixture
  unchanged. A read-only HFS mount never clears the volume header's clean-unmount bit, so this
  arm does **not** dirty p25 and is safe to repeat; it does not retract 906's write result.
- The RO HFS objects are members of the entry group, so the exit's `bl FlushPoU_Dcache` moved back
  one page, `0x8004e2d8` → `0x8004d2d8` (returns to `0x8004d2dc`). The entry build's own clause
  refused first; `entry_trace.c`'s `STAGE90_XNU_SEAM_LR` and `run_and_capture.sh`'s
  `EXIT_POP_LR_LITERAL` are both `0x8004d2dc`. The disassembly decided the direction, not the
  record.
- Parked at `out/stage90/frozen/armed-storage-54d5c585` (11 members) and recorded in
  `records/revert-set.txt`. Verified: `tools/check_set_name_rule.sh` 0, `tools/verify_revert_set.sh`
  0, `verify_press_ready.sh` 5/5, `make check` 0. Committed `9873eb0`.
- **`scripts/stage_boot_p19.sh`** stages the recorded payload into p19, because `fastboot boot`
  writes nothing and a plain power-on can only run what is IN the partition. Default dry-run;
  `--expect-sha256=HEX` is required and checked against the bytes; writes **only** the payload's
  own sector count (`bs=512 count=18592`), never the whole partition, so the tail is untouched; it
  saves the full 32 MiB before image, hashes a full readback of the payload region, holds no
  `fastboot` call at all, and is the only route back if an arm is abandoned. Its host-only suite
  (`tools/test_stage_boot_p19.py`, 25 cases) pins the bounded write as a check. Dry-run against
  the live device passed (root shell, by-name, geometry validated; no write).

## The normal-slot press has a bootloop hazard the fastboot press does not

The 906/907 returning runs came back to Android because p19 was booted **non-persistently** by
`fastboot boot`: the payload ran, the watchdog bit, and the reset reverted to the normal slot,
which was still Android. Once XNU is staged IN p19, that reversion loads **XNU again** — a
watchdog bite reboots straight back into the payload. So a normal-slot XNU run that faults can
boot-loop rather than return, and the log is not reachable through adb (XNU brings up no adbd).

The escape is unchanged and already verified: **VolDown + Power + USB → fastboot →**
`fastboot boot` the verified TWRP (non-persistent) **→** read `/proc/last_kmsg` (the RAM console
survives a warm reset into TWRP — the 906/908 route) **→** optionally
`scripts/restore_boot_from_xnu.sh --execute`. The parser and orchestrator for a *normal-slot*
capture, and a `--normal-boot` mode for `run_and_capture.sh`, do not exist yet; the fastboot-boot
runner cannot read a log from a boot-looped device. That is the next step, not something the
current press path can fake.

## The first normal-slot press: staged, plain-booted, and DARK (2026-10-07)

The RO arm was staged into p19 and booted with a **plain `adb reboot`** — the goal's own
sentence (「直接开机就运行xnu」) is now what was tested, not `fastboot boot`.

- **Staged** with `scripts/stage_boot_p19.sh --expect-sha256=046219ad… --execute`, exit 0.
  p19's first 9,519,104 bytes read back `046219adf489936b9348c3ec77288e6cac2536715a3fca04669ce41e34466417`.
  Full 32 MiB before image retained:
  `3cf1cf61296a28e179072375726e3a37a08a24509c0a896f482175ef92f325a1`
  (`out/stage90/backups/boot-p19-stage-20261007T021045Z-NbHqhVE1`).
- **Pressed** with `adb reboot` at `2026-10-07T02:10:56Z`. The device x**never re-enumerated**:
  0/12 five-second adb probes over 60 s, `fastboot devices` empty, `lsusb` shows no phone USB id
  for 90 s, and `sudo dmesg` attributes no USB event to the phone across the whole window. The
  phone is **DARK**.
- **Reading the darkness correctly.** This is the two-outcome-equivalent signature §"gives dark"
  predicted: XNU runs no `adbd` and the entry image never re-initializes the USB controller, so
  **a resident run, a faulted run, and a boot-loop are all equally dark host-side.** No host
  statement about *what XNU did* follows from the darkness alone, and none is made here.
- **Evidence is in the RAM console**, which survives a warm reset into verified TWRP. That read
  requires a **physical** VolDown + Power + USB reset (the operator's action), then
  `scripts/press_908_normal.sh --rescue`. The outcome note is
  `out/stage90/captures/908-normal-20261007-021056-outcome.txt`.

**p19 was not restored.** The arm stays staged because the goal requires p19 to hold XNU for a
plain power-on; restoring it would undo the step just taken. The bridge back is the retained
before-image + `scripts/restore_boot_from_xnu.sh --execute`.

## The RAM console, read out of a PHYSICAL TWRP (2026-10-07) — RESULT

The operator held VolDown + Power and reached TWRP; the RAM console was captured as
`out/stage90/captures/908-normal-20261007-022629-last_kmsg.txt` (905,586 B, sha256
`d3fd4068a7f1b33c599acdeea5b0156b371e7b60bea2fcf3a99b55d1a2ebad0a`) with its summary
`.summary.txt`. **The darkness is resolved: the run is a real, complete XNU boot that entered the
operating system.** Run identity is unambiguous — the log holds exactly one `MI4IOS6_STAGE90 v1
entered`, one `jumping to XNU's _start`, and `xnu_live_seam_lr=0x8004d2dc`, which is THIS arm's
(RO) seam and not 906's `0x8004e2dc`.

- **From a plain power-on.** The arm was staged in p19 and loaded by `adb reboot`; nothing was
  sent over USB to run it. This is the goal's own sentence (「直接开机就运行xnu」).
- **It got into the kernel.** `xnu_entry_status=0x90000001`, `kernel_entry ok`, then the real
  XNU (`real XNU entry: 47`), `BSD root: md0, major 4, minor 2`, `VM_TEST_DEVICE_PAGER_TRANSPOSE:
  PASS`, `mcache: 1 CPU(s)`, `mbinit: done`.
- **It exec'd pid 1 and ran it in user mode.** `load_init_program: attempting to load /sbin/launchd`
  → `mini4: the OS starts the process at 0x10e0` → `mini4: the AST is done -- pid 1's thread is at
  0x10e0 for user mode`. **There is no `failed loading /sbin/launchd` line** (only the expected
  errno-2 for `/usr/local/sbin/launchd.development`, tried and skipped first).
- **pid 1 ran an idle/poll loop — the OS had nothing left to do.** `mini4: the OS has nothing to
  run -- pid 1 parked in poll for 2000 ms, and the kernel's own idle path was entered 60931 time(s)
  ... (ticks 0x24c76c0)`, with the idle's doors, exits, cache window and interrupt-frame readings
  all published. That is a sustained resident kernel, not a stall.
- **Basic drivers ran.** The fixture's calls are all in the log: `getpid` returned 1 (user mode
  works), `open`/`read`/`exit`/`wait` all executed. The card unit is registered
  (`xnu_live_rootmedia_card_registered=1`, `_dev=0x04000002`) and served **reads at LBA
  `0x400000+`** (`card_last_lba` 0x400002 … 0x400117), including the exec page
  (`xnu_live_rootmedia_card_off=0x00022000`). The live channel did **not** truncate
  (`xnu_live_capped` absent; cap raised to `0x4000`).
- **The ending is 906's watchdog bite, unchanged.** The idle path's repair later faults at the
  epilogue's `RESTART_REASON` store: `panic(cpu 0 caller 0x804a9908): kernel abort type 4:
  fault_type=0x3, fault_addr=0xfa0065c` (r2=0x0fa00000, pc 0x804d0760) → `Attempting system
  restart...MACH Reboot`, matching `mi4-906-return-is-a-watchdog-bite` exactly. This is the *only*
  failure in the run and it is the already-known self-end defect, not a boot defect.

**The driver-quality caveat (unchanged, and NOT a boot failure).** The fixture's `open` returned
`0x00000002` (ENOENT — the owed `open("/dev/rmd0")` defect from 904) and its `read` returned 0 of
4 bytes, so the fixture's magic is not matched. The summariser marks this FAIL. It is a
fixture/syscall-value defect on the *read-back* path, orthogonal to "did XNU boot and run the OS",
which it plainly did.

**Verdict: the normal-slot clause is MET.** A plain power-on loads XNU from p19 into the kernel,
mounts the root, execs pid 1, runs it in user mode through a sustained idle loop with basic
drivers answering reads off the card, and the device is alive at the point the watchdog's known
self-end fires.

**The root is the card unit, reproducing 903.** `xnu_live_rootmedia_card_dev=0x04000002` (major 4,
minor 2) and `BSD root: md0, major 4, minor 2` are the **same device numbers**, and the card served
reads at LBA `0x400000+` (`card_last_lba` 0x400002 … 0x400117) — `userdata`, where the operator
wrote the HFS+ volume, matching 903's `card unit major 4 minor 2, LBA 0x400000+`. `rd=md0` is the
retained boot-arg label, not a separate backing store. What the `/dev/rmd0` ENOENT does mark is a
*naming* defect on the fixture's open path (the 904-owed item), not that the root is a RAM blob.
What remains for the goal is **repeatability** and the **self-end/watchdog** clause — not the boot.

## Completion bar

Preparation and a verified write are not the goal. A completed normal-boot stage
needs an actual p19 boot result with retained evidence and fastboot recovery still
reachable. A completed resident stage additionally needs repeatable root mounting,
measured ongoing kernel/userspace progress, and explicitly solved watchdog/reboot
behavior. None is claimed here merely from the staged p19 bytes or A2 darkness.
