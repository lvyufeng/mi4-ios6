# Xiaomi Mi 4 iOS 6 / Darwin Experiment

This repository tracks an experimental, owner-controlled research project around the Xiaomi Mi 4 LTE (`cancro`) and the feasibility of running iOS 6 / Darwin / XNU-like components on non-Apple ARMv7 hardware.

## Where things are

| Path | Contents |
| --- | --- |
| `stages/stage85` … `stages/stage90` | The six retained stage snapshots. `stage90` is the current tip. See [`stages/README.md`](stages/README.md). |
| `docs/` | All documentation, indexed in [`docs/README.md`](docs/README.md): `reference/`, `experiments/`, `status/`, `history/`. |
| `tools/` | Host-side helpers, including `stage-archive.sh` for the archived stages. |
| `Makefile` | Build/list/restore convenience targets. |
| `out/stageNN/` | Build products (ignored by git). |
| `external/` | Public XNU checkout used by the compile-graph and link-proof helpers (ignored by git). |
| `xiaomi4-cancro-backup-20260604-112053/` | Device partition backups (ignored by git, never committed). |

## Build and boot

```bash
make                       # build the newest snapshot -> out/stage90/
make list                  # the retained snapshots
make stage89               # build one specific stage
cd stages/stage90 && ./build.sh          # what `make` runs

sha256sum -c out/stage90/SHA256SUMS.txt  # per-stage build manifest

sudo fastboot boot out/stage90/stage90-qcdt.img   # non-persistent validation
```

Every snapshot resolves the repository root itself, so its `build.sh` works from any working directory. Booting is deliberately not a `make` target: flashing is a per-operation decision, and `fastboot boot` never writes to the device.

## Current device

Connected device observed via ADB:

- Model: Xiaomi MI 4LTE
- Device codename: `cancro`
- Hardware: Qualcomm / `qcom`
- CPU ABI: `armeabi-v7a`
- Current Android: Android 10 userdebug-style build
- Kernel: Linux 3.4.113, ARMv7
- ADB shell: root-capable (`uid=0` observed)

## Important technical reality

The goal is experimental research, not a normal ROM port.

A stock Apple iOS 6 image cannot simply be flashed to this phone. iOS depends on Apple-specific hardware, boot chain, device tree, XNU platform support, IOKit drivers, graphics stack, code-signing infrastructure, and proprietary userland components. Xiaomi Mi 4 uses a Qualcomm MSM8974-family platform and Android/Linux boot images.

A realistic research path is incremental:

1. Keep the original device recoverable.
2. Understand the existing Android boot image and partition layout.
3. Prove that custom boot/recovery images can be booted safely.
4. Study whether an open-source Darwin/XNU-derived or XNU-like minimal kernel experiment can be adapted to the Qualcomm platform.
5. Treat full iOS 6 userspace as a separate, much harder problem because SpringBoard/UIKit/CoreAnimation and related Apple frameworks are proprietary and platform-specific.

## Local backup status

Before any write/flash operation, key boot-critical partitions were backed up locally from the connected phone.

Backup directory currently present in the working tree but intentionally not committed:

```text
xiaomi4-cancro-backup-20260604-112053/
```

The backup contains images such as:

- `sbl1.img`
- `rpm.img`
- `tz.img`
- `aboot.img`
- `boot.img`
- `recovery.img`
- `persist.img`
- `modem.img`
- `SHA256SUMS.txt`

The SHA256 manifest was verified successfully after backup.

## Observed boot image layout

Backed-up `boot.img` is a standard Android boot image:

- Page size: 2048
- Kernel load address: `0x8000`
- Ramdisk load address: `0x2000000`
- Tags address: `0x1e00000`
- Device tree blob / Qualcomm QCDT present in the boot image (`dt_size=2521088`)
- Command line includes `androidboot.hardware=qcom` and `androidboot.bootdevice=msm_sdcc.1`
- The cancro bootloader requires the legacy Android v0 `dt_size`/QCDT field for custom `fastboot boot` payloads (`dtb not found` without it)

Backed-up `recovery.img` is also a standard Android boot image and uses a serial-console-oriented command line (`console=ttyHSL0,115200,n8`).

## Current stage: Stage90 — XNU handoff

Stage0 through Stage84 built the runtime up to a full kernel virtual address space with the loader still in control; Stage85 through Stage89 moved Stage-owned code to the high virtual alias at `0x80000000` (code execution, IRQ handler, data-abort handler, undefined-instruction handler) and added a Mach-O kernel loader. The chronological record is in [`docs/experiments/`](docs/experiments/README.md), and the Stage0 – Stage81 milestone log in [`docs/history/milestones.md`](docs/history/milestones.md).

Stage90 covers the handoff itself: the Stage-owned `arm_init`-shaped entry stub runs the early-pmap / `PE_init_platform(FALSE,args)` / post-PE-bootstrap / `arm_vm_init` ladder, relocates the high-VA handlers, loads the Mach-O fixture, and then hands control to a high-VA target through the candidate L1.

The first handoff attempt jumped and **hung the device**: no log output, no automatic recovery, manual power-cycle required ([`stages/stage90/IMPLEMENTATION_STATUS.md`](stages/stage90/IMPLEMENTATION_STATUS.md)). With zero post-jump visibility, Stage90 is now bisecting the failure instead of re-attempting it. The switches in `stages/stage90/stage90.h` select how far the stage goes:

| Switch | Effect |
| --- | --- |
| `STAGE90_HANDOFF_HARD_SKIP=1` | Take the loader-prerequisite path, then return before the candidate-L1 install, the watchdog loop and the jump. Nothing crosses the boundary. |
| `STAGE90_HANDOFF_PREFLIGHT_WATCHDOG_ONLY=1` | Stay under the original known-good mapping: no candidate L1, no high-VA branch, no jump — just a bounded identity-mapped loop, to prove the timer IRQ still fires and `platform_reboot()`/PS_HOLD warm-reboots the device. |
| `STAGE90_ENTRY_STUB_BISECT_LEVEL=0` / `=1` | Return from the entry stub after the boot-args check / after the early-pmap step, to place the hang inside the `arm_init` ladder. |
| all zero | The full candidate-L1 high-VA sampling handoff — the configuration that hung. |

None of these modes is a shipped feature; they exist to find the hang. The current work is the watchdog → `platform_reboot()` → PS_HOLD warm-reboot path, which is why `platform_reboot()` now logs each step of the restart-reason and PS_HOLD sequence.

## Next milestones

- Get the device to recover on its own: prove the watchdog fires and PS_HOLD warm-reboots back to Android, so a failed handoff stops requiring a manual power-cycle.
- Re-attempt the high-VA handoff one increment at a time, with the bisection switches above, until the point that loses the device is identified.
- Only then revisit what the handoff target may do. Sequencing that isolates each step is worth more here than a faster jump.

Standing constraints, unchanged from earlier stages:

- Hardware validation stays non-persistent (`sudo fastboot boot`). No flashing without explicit per-operation confirmation.
- Preserve the identity/recovery mappings for ram_console, PS_HOLD, GIC, timer, abort logging and early recovery paths.
- Keep caches disabled unless a later stage explicitly validates a safe cache policy.
- Keep large source checkouts under the ignored `external/` and build outputs under the ignored `out/`.
- Use `xnu-2050.22.13` for iOS 6 / Darwin 12-era public context and `xnu-4570.1.46` as the public ARM implementation reference where the 2050 tree lacks ARMv7 files.
- Do not build a full public `mach_kernel`, enter public XNU `_start`, execute public-XNU pexpert/pmap/IOKit object code or generated Mach-O bytes, change cache policy, or write persistent storage.

## Archive policy

The working tree keeps the six most recent stage snapshots under `stages/`. Each snapshot is a complete, self-contained copy of its predecessor plus that stage's delta, so any of them can be built and bisected as-is. Stages older than that (`stage0` … `stage84`) were removed from the tree and live on in git at the tag `stage-archive-base`:

```bash
tools/stage-archive.sh list             # what is archived, what is present
tools/stage-archive.sh show 50 xnu_workspace.c   # read a file without restoring
tools/stage-archive.sh restore 50       # -> ./stage50/, at the root so it builds
```

Restored snapshots land at the repository root, because their scripts predate the `stages/` layout and resolve `../out` / `../external` from that depth. `make restore STAGE=50` is the shorthand. Adding a new stage, and which snapshot to archive when you do, is described in [`stages/README.md`](stages/README.md).

## Safety rules for this repo

- Do not flash or erase partitions without an explicit confirmation for that specific operation.
- Prefer `fastboot boot` or other non-persistent tests before persistent writes.
- Never include personal data or large partition backup images in normal source commits.
- Keep recovery instructions and hashes close to any experimental boot image work.
- Focus on open-source, owned-device research. Do not rely on leaked proprietary Apple code.
