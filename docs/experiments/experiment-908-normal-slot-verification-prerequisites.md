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
- **The 906 "safe ending" is unexplained.** Its epilogue store faulted
  (`fault_addr=0xfa0065c`), and rec2 logs the SoC watchdog armed and counting,
  never petted across the jump (~28 s). The returning 906 run is consistent with
  a watchdog reset, not a clean self-end — yet the resident run that should have
  been reset by that same watchdog stayed dark for 19 minutes. **This
  contradiction is on the critical path and must be resolved before any
  "self-ending" run is trusted.**
- Restoring p20 removes the Android fallback and yields **TWRP**, not Android
  recovery. The fallback is `adb reboot bootloader` / VolDown+Power → fastboot →
  `fastboot boot` verified TWRP → guarded p19 rollback. Restore verification is a
  **full-partition** readback hash, not a 512-byte prefix.
- Because p19 already holds `fc956eae`, re-writing it is a no-op; a "single
  variable" claim must either restore p19 to `b2119252` for staging or be dropped.

## Completion bar

Preparation and a verified write are not the goal. A completed normal-boot stage
needs an actual p19 boot result with retained evidence and fastboot recovery still
reachable. A completed resident stage additionally needs repeatable root mounting,
measured ongoing kernel/userspace progress, and explicitly solved watchdog/reboot
behavior. None is claimed here merely from the staged p19 bytes or A2 darkness.
