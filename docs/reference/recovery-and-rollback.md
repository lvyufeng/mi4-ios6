# Recovery and Rollback Procedure

This document defines the safety baseline for Xiaomi Mi 4 LTE (`cancro`) boot experiments in this repository.

The connected phone is an owner-controlled experimental device. Even so, bootloader and partition writes can permanently brick a Qualcomm device. Treat this document as the required checklist before any persistent write.

## Current recovery assets

A verified local backup exists at:

```text
xiaomi4-cancro-backup-20260604-112053/
```

The backup is intentionally ignored by git and is expected to stay local to the host that created it.

The backup includes:

- Boot chain / firmware-ish partitions: `sbl1`, `rpm`, `tz`, `DDR`, `ssd`, `dbi`, `aboot`
- Android boot partitions: `boot`, `recovery`
- Radio/calibration/persist partitions: `modem`, `modemst1`, `modemst2`, `fsg`, `fsc`, `persist`
- Misc/logo/reserved partitions: `misc`, `logo`, `bk1`..`bk5`
- `SHA256SUMS.txt`

Before relying on the backup, verify hashes from inside the backup directory:

```bash
sha256sum -c SHA256SUMS.txt
```

## Golden rules

1. Prefer tests that do not write to the phone.
2. Prefer `fastboot boot <image>` over `fastboot flash <partition> <image>`.
3. Do not write boot chain or radio/calibration partitions during normal experiments.
4. Do not erase partitions as a troubleshooting step.
5. Do not flash an image unless the target partition, image origin, and rollback path are explicit.
6. Confirm fastboot mode works before making a persistent boot/recovery change.
7. Keep a known-good recovery path available before testing an experimental boot image.

## Partitions to avoid writing

Avoid writing these unless there is a very specific reason and a device-specific unbrick plan:

```text
sbl1
rpm
tz
DDR
ssd
dbi
aboot
modem
modemst1
modemst2
fsg
fsc
persist
```

These can affect bootloader execution, TrustZone, radio firmware, calibration, and device identity. Mistakes here may require EDL/JTAG-style recovery or may be unrecoverable with normal fastboot/ADB.

## Lower-risk experimental targets

The least bad persistent targets are:

```text
boot
recovery
```

Even these can temporarily brick normal boot/recovery if flashed incorrectly. Prefer non-persistent tests first.

## Non-destructive preflight checks

From Android/ADB mode:

```bash
sudo adb devices -l
sudo adb shell 'id; getprop ro.product.device; getprop ro.hardware; uname -a'
sudo adb shell 'ls -l /dev/block/platform/msm_sdcc.1/by-name/boot /dev/block/platform/msm_sdcc.1/by-name/recovery'
```

From fastboot mode:

```bash
fastboot devices -l
fastboot getvar product
fastboot getvar secure
fastboot getvar unlocked
fastboot getvar all
```

`fastboot getvar all` is read-only, but it may print device serials or identifiers. Avoid pasting full output publicly without review.

## Preferred experimental order

### 1. Local-only image inspection

Parse backed-up images locally:

```bash
./tools/parse_android_bootimg.py xiaomi4-cancro-backup-20260604-112053/boot.img
./tools/parse_android_bootimg.py xiaomi4-cancro-backup-20260604-112053/recovery.img
```

### 2. Local-only unpack/repack round trip

Before changing anything, prove that the toolchain can unpack and repack an image while preserving expected header fields. Compare parser output before and after.

### 3. Fastboot reachability test

Reboot to bootloader and check visibility:

```bash
sudo adb reboot bootloader
fastboot devices -l
```

Return to Android without flashing:

```bash
fastboot reboot
```

### 4. Non-persistent boot test

Only after fastboot reachability is confirmed, test a known-good image non-persistently:

```bash
fastboot boot path/to/known-good-recovery-or-boot.img
```

If it fails, reboot/power-cycle. Do not immediately flash.

For a Stage payload, gate the run first:

```bash
cd stages/stage90 && ./build.sh && ./preflight_boot_check.sh \
    [--allow-preflight|--allow-full|--allow-selftest|--allow-attr-normal-nc|--allow-hw-watchdog-selftest]
```

The gate verifies the image against `SHA256SUMS.txt`, checks the payload references no
storage symbols, and refuses a mode the caller has not explicitly allowed. It never runs
fastboot itself.

### 4a. When a payload hangs — the device does not come back

This is the expected failure mode of an unbootable Stage payload, and it has happened
(2026-09-16, `PREFLIGHT_WATCHDOG_ONLY`; and again 2026-09-21, experiment 517 — see the note at the
end of this section). It is not a brick. **It is also not always recoverable without you**, which
this section used to imply and now says plainly: the nets below have fired many times and have also
failed once, so treat a hang as costing a power press until the nets say otherwise.

Symptom, from the host kernel log on the phone's USB port:

```text
USB disconnect, device number NN           <- leaving Android for the bootloader
new high-speed USB device NN
  idVendor=18d1 idProduct=d00d ... SerialNumber=4a2fe00b    <- fastboot, as expected
USB disconnect, device number NN           <- fastboot boot handed off to the payload
```

and then nothing: no further enumeration, `adb devices` and `fastboot devices` both empty
for minutes. Ignore unrelated devices that appear in `lsusb` on other buses.

What it means: the payload either hung, or rewrote PS_HOLD and powered the phone off.
**Nothing was written to storage** — `fastboot boot` does not persist, and the Stage payloads
touch only MMIO, IMEM and PS_HOLD — so the device is recoverable.

Recovery:

1. Hold Power ~10–15 s to force a power-off, then release.
2. Press Power normally. Android boots as before, because the boot partition was never
   modified.
3. Confirm it is back:

   ```bash
   sudo adb devices -l                     # 4a2fe00b ... device:cancro
   sudo adb -s 4a2fe00b shell cat /proc/uptime   # small value = fresh boot
   ```

4. Capture the payload's log before anything else reboots the phone:

   ```bash
   sudo adb -s 4a2fe00b exec-out 'cat /proc/last_kmsg' > /tmp/cancro-last_kmsg.txt
   grep -a -n 'MI4IOS6_STAGE90' /tmp/cancro-last_kmsg.txt | tail -40
   ```

   The ram_console buffer lives at the top of DRAM and survives a warm reboot, which is how
   earlier experiments recovered 100–180 KB of payload log this way.

5. If `/proc/last_kmsg` has no `MI4IOS6_STAGE90` lines from the run, the power cycle
   reinitialised DRAM and the log is gone. That loss is exactly what the payload's recovery
   nets exist to prevent — see below — and it means the run produced no information at all.
   **The power press is the long hold is the PMIC reset**, so this is the normal outcome of a
   hang that needed one: the nets below preserve the log by *rebooting* the phone; a power
   press does not.

The payload has **two** recovery nets designed to make this procedure unnecessary, and they
fail in different ways, which is why there are two:

- The **MSM8974 hardware watchdog** (`stages/stage90/hw_watchdog.c`, on by default) is armed
  at the top of `stage90_main`: **bark at 25 s, bite at 28 s**, following the vendor driver's
  own split (`STAGE90_HW_WATCHDOG_TIMEOUT_S` / `_BITE_GAP_S` in `stage90.h`; both registers are
  20 bits, which is why the timeout is 25 s and not 30 s). It is a hardware counter — no GIC,
  no timer, no IRQ delivery, no vector table, no unmasked IRQs — so it does not depend on the
  *payload's* state. `platform_reboot()` also forces an immediate bite after its PS_HOLD write,
  so a reboot does not depend on the PMIC write landing. Its arming is logged with a liveness
  check, not just a read-back: `WDT0_STS` **counts up from zero** on this part, so the payload
  samples it twice and requires it to have moved *and* to sit near zero rather than near
  aboot's own 20 s arming. One caveat, from the vendor bindings and the driver itself: the bite
  is a **secure-mode interrupt mediated by TrustZone**, so "independent of the payload" is the
  accurate claim, not "independent of any software".
- The **software dead-man** (60 s) is armed at the end of `kernel_entry`'s GIC validation: the
  timer IRQ handler dumps the interrupted PC ring and reboots through PS_HOLD.

The hardware net is the faster and stronger of the two and is the one to rely on first. Its
register programming comes from the cancro device tree and the cancro kernel's own
`msm_watchdog_v2.c`, and it is the same mechanism Android's panic path uses to produce a
readable `last_kmsg` — so "bite → reset → last_kmsg" is the device's normal crash path.

When the hardware watchdog is armed, `last_kmsg` contains:

```text
stage90 hw_watchdog: armed and counting; the SoC will reset itself if the payload stops
```

and when the software dead-man is the one that fired:

```text
pc_samples: begin
pc_sample_total=...
pc_sample_watchdog_fired=0x00000001
stage90 pc-sampling watchdog: rebooting after sample dump
MI4IOS6_STAGE90 platform_reboot entered
```

Absence of all of that after a hang means neither net fired, which is itself the finding for
that run. Both can be tested directly, and the hardware one should be tested first:
`STAGE90_HW_WATCHDOG_SELFTEST=1` arms the hardware counter and spins forever (~28 s to
recovery), while `STAGE90_DEADMAN_SELFTEST=1 -DSTAGE90_HW_WATCHDOG=0` does the same for the
software dead-man alone, so a success is attributable to one mechanism rather than to
"one of them worked".

**What the nets' record actually is, so this section is not a promise it cannot keep.** Every
run from experiment 506 to 515 returned on the hardware watchdog's bite — including 512/513 with
the OS parked in the kernel's own `WFI` idle path and 514/515 in abort storms inside the
exception path — and 516's two runs came back on XNU's own `Attempting system restart...MACH
Reboot`, i.e. on the kernel's panic path with the watchdog behind it. Then **experiment 517's
first run (2026-09-21) did not come back at all**: `fastboot boot` reported `Booting OKAY`, the
phone disconnected 3 s later as the payload took over, and there was no enumeration on that USB
port for ~15 minutes — no `adb`, no `fastboot`, no other Xiaomi/Google device on the bus. It
needed a power press, which reinitialised DRAM, so no log was recovered. See
`docs/experiments/experiment-517-the-step-was-two-changes-and-its-first-run-did-not-come-back.md`.

Two things follow, and both are about how to read a hang rather than about the device:

- A net's *arming* is proved by its arm-time liveness record; a *bite* is proved by nothing but a
  phone that came back. So "the nets are proved" is a claim about a history, and this is the
  counterexample in it.
- The device is never at risk of a brick — `fastboot boot` does not persist and the Stage payloads
  touch only MMIO, IMEM and PS_HOLD — but a run can cost you a power press and its own log. Say
  which single change an image carries before booting it, so a run that does not come back is
  attributable; 517's carried two and is not.

### 5. Persistent flash only with explicit approval

**This step now has a gate: `stages/stage90/preflight_storage_write.sh`.** It refuses by default, runs
no fastboot, touches no device, and clears exactly one form of the write. Use it before any write this
document describes; the section below is the reasoning it encodes.

Its four preconditions, and what each one is protecting:

1. **The OS has been observed staying up**, not merely reached. The evidence is a capture log, and the
   criterion is the *runner's own two clauses* in one capture — the goal block's PASS (user mode reached
   and a driver answering) **and** the arm's clause PASS (the machine stayed up past the park). The
   first alone is a floor that the archived baselines 520 and 533 also meet: they got to pid 1 and ran
   the whole userland fixture, and then died at the idle exit's `pop {fp, pc}`. A gate that accepted the
   floor would clear a persistent write on the strength of a boot that died. If the phrases are not in
   `run_and_capture.sh` any more, the gate refuses rather than reading a phrase nothing prints.
2. **The rollback verifies.** The whole of `xiaomi4-cancro-backup-20260604-112053/SHA256SUMS.txt` must
   check out AND the target's own stock image must be present. A rollback whose hashes have moved is a
   promise, not a path.
3. **The target is `boot` or `recovery`.** Anything else is refused by name, with the partition list
   this document forbids.
4. **TWRP is booted, never flashed** — and it is *the* TWRP this tree has a record for. See below.

#### The write the gate clears, and why it flashes nothing

The tempting reading of "use TWRP to write storage" is "flash TWRP". It is not needed, and the gate
will not print it. TWRP is a **tool** here: boot it non-persistently, then write the target from inside
it.

```bash
sudo fastboot devices                       # must list 4a2fe00b
sudo fastboot boot twrp-3.7.0_9-0-cancro.img   # NOT a flash; nothing is written by this

sudo adb -s 4a2fe00b shell 'cat /proc/partitions; ls -l /dev/block/by-name/boot'
sudo adb -s 4a2fe00b push payload.img /tmp/write.img
sudo adb -s 4a2fe00b shell 'dd if=/tmp/write.img of=/dev/block/by-name/boot bs=4096'
sudo adb -s 4a2fe00b shell 'sync'
sudo adb -s 4a2fe00b reboot
```

That leaves **exactly one** persistent change — the intended one — instead of two, and it keeps
`fastboot flash` out of the picture entirely for the tool. The only `fastboot flash` in the gate's
output is the rollback, which is a recovery action and is labelled as one.

#### Which tool image, and how it is known

`--boot-image` is not "any Android boot image". **The gate will only boot an image this tree has a
record for.** The record is `stages/stage90/tool-images.txt` — one line per image, keyed by sha256, with
the source page, the fetch date, the size, an md5 and the signer — and the check is
`tools/verify_tool_image.sh`, which the gate runs before it prints a single command:

```bash
tools/verify_tool_image.sh twrp-3.7.0_9-0-cancro.img --require-role=twrp
```

It refuses if the file is absent, empty, unreadable, **not one of the recorded hashes**, or **not
recorded with `role=twrp`** — and it corroborates the bytes against the detached PGP signature, which is
what caught the one case a hash alone cannot: an image edited *and* re-recorded to match hashes to the
new bytes, and the signature disagrees. A bad signature is a refusal; a host without `gpg` prints
`NOT CHECKED` and still verifies the hash, because a check that succeeds by printing nothing cannot be
told from one that never ran.

The image currently recorded is **TWRP 3.7.0_9-0 for `cancro`**, fetched 2026-09-23:

| field | value |
| --- | --- |
| file | `twrp-3.7.0_9-0-cancro.img` (repository root, **not committed** — `*.img` is gitignored) |
| sha256 | `a2f4b9037946ededf9f06f28dff922decebe2e7a0bf1bfff24b6e34327e02443` |
| md5 / bytes | `525f8796b1e9fa22a5fb7be3b89e76f1` / 16240640 |
| source page | <https://dl.twrp.me/cancro/twrp-3.7.0_9-0-cancro.img.html> |
| signed by | `9570 7D42 307C 9D41 D09B F709 1D85 97D7 891A 43DF` (uid `TeamWin <admin@teamw.in>`), 2022-10-11 |

Both hashes equal the values TWRP publishes for that file, and the signature verifies against the key
kept in `stages/stage90/tool-images/` — so the fetch is reproducible and checkable offline. Two caveats,
stated rather than glossed: the fetch must go **through the html page** (TWRP's terms forbid linking
directly to their files; without the page's cookie and referer the same URL answers 200 with a 6,817-byte
interstitial, which is exactly how the first attempt here produced a "successful" download of the wrong
thing), and the signing key came from a public keyserver, so the *fingerprint* is worth confirming out of
band while the *bytes* are already pinned by TWRP's own published hashes.

To re-fetch (if the local copy is lost):

```bash
curl -sSL -c /tmp/tj.txt https://dl.twrp.me/cancro/twrp-3.7.0_9-0-cancro.img.html
curl -sSL -b /tmp/tj.txt -e https://dl.twrp.me/cancro/twrp-3.7.0_9-0-cancro.img.html \
     https://dl.twrp.me/cancro/twrp-3.7.0_9-0-cancro.img -o twrp-3.7.0_9-0-cancro.img
curl -sSL https://dl.twrp.me/cancro/twrp-3.7.0_9-0-cancro.img.sha256     # compare
tools/verify_tool_image.sh twrp-3.7.0_9-0-cancro.img --require-role=twrp
```

The image is booted with its own kernel and ramdisk and carries **no appended DTB** (`dt_size=0`), while
this project's own payload does; that is normal for TWRP builds of this era and is a fact to keep in
mind, not a defect to fix.

#### What is still missing, stated so it is not assumed

* **The gate cannot tell you what an image *is*.** It can verify that the file is the one this tree
  recorded, that it parses as an Android boot image, and that it is not the same file as the payload.
  What makes it *TWRP* is the record — a hash written down in the step that obtained it, from a page whose
  URL is part of the record. A file copied from a forum and renamed `twrp.img` does not verify.
* **Nothing here has been booted on the device.** The image is downloaded and verified on the host only.
* **No EDL path is documented for this phone** (see Emergency notes). The rollback in the gate is a
  fastboot rollback and it assumes fastboot still works.



A persistent flash should be a separate, explicitly approved step, for example:

```bash
fastboot flash recovery path/to/known-good-recovery.img
fastboot reboot
```

Do not flash boot chain partitions as part of this project unless the entire task is specifically about bootloader recovery and the risks are accepted.

## Rollback examples

If a boot image flash breaks normal Android boot but fastboot still works, restore the backed-up boot image:

```bash
fastboot flash boot xiaomi4-cancro-backup-20260604-112053/boot.img
fastboot reboot
```

If a recovery image flash breaks recovery but fastboot still works:

```bash
fastboot flash recovery xiaomi4-cancro-backup-20260604-112053/recovery.img
fastboot reboot
```

If fastboot does not work but ADB still works from Android, do not attempt random writes. Re-check the exact current state first.

## Emergency notes

Qualcomm devices may have EDL modes, but this repository does not currently document a verified EDL recovery path for this specific phone. Until an EDL path is verified, assume boot chain partition writes can be unrecoverable in this workflow.

## Before every future write

Record:

- Current mode: Android / recovery / fastboot
- `adb devices -l` or `fastboot devices -l` output
- Target partition
- Exact image path and SHA256
- Whether the image was built locally or obtained externally
- The rollback command

Then ask for explicit confirmation for that exact write.
