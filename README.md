# Xiaomi Mi 4 iOS 6 / Darwin Experiment

This repository tracks an experimental, owner-controlled research project around the Xiaomi Mi 4 LTE (`cancro`) and the feasibility of running iOS 6 / Darwin / XNU-like components on non-Apple ARMv7 hardware.

## Where things are

| Path | Contents |
| --- | --- |
| `stages/stage85` … `stages/stage90` | The six retained stage snapshots. `stage90` is the current tip. See [`stages/README.md`](stages/README.md). |
| `docs/` | All documentation, indexed in [`docs/README.md`](docs/README.md): `reference/`, `experiments/`, `status/`, `history/`. |
| `tools/` | Host-side helpers: `stage-archive.sh` for the archived stages, `decode_armv7_descriptor.py` for page-table entries, and the boot-image and fixture generators. |
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

cd stages/stage90 && ./preflight_boot_check.sh   # verify + gate a hardware run
sudo fastboot boot out/stage90/stage90-qcdt.img  # non-persistent validation
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

The handoff target is **not** XNU code, and no public XNU object has ever been linked into or executed by any payload — the public-XNU compile graph (`targets/*.objects`) is a host-only linkability proof, and the Mach-O fixture is inert by construction. The jump target `0x80008000` is the fixture's `LC_UNIXTHREAD` PC, i.e. its Mach-O header; the fixture's `__TEXT,__text` holds the ASCII string `"ST90-TEXT-NOEXEC"`. The first handoff attempt jumped into that non-code and the device hung: no log output, no automatic recovery, manual power-cycle required. With zero post-jump visibility, Stage90 is now bisecting the failure instead of re-attempting it, under two independent switches in `stages/stage90/stage90.h`. (An earlier revision of this section described three 0/1 switches; they shadowed each other, and the bisect they drove faked its results, so both were replaced.)

`STAGE90_HANDOFF_MODE` — mutually exclusive, `#error` on any other value. Selects how far the handoff itself goes:

| Mode | Effect |
| --- | --- |
| `FULL` (0) | Install the candidate L1, arm the PC-sampling watchdog, jump to the Stage-owned high-VA target. Before jumping it refuses any target whose first word is the fixture's `__TEXT` marker. |
| `PREFLIGHT_WATCHDOG_ONLY` (1) | No candidate L1, no high-VA branch, no jump: everything stays under the original known-good mapping. Arms the *same* watchdog and spins in a bounded identity-mapped loop, to prove the interrupt → sample-dump → `platform_reboot()`/PS_HOLD warm-reboot path on its own. The loop is bounded (4× `INTERVAL × MAX`); if the watchdog never fires, control returns and the stage reports the failure and reboots visibly instead of hanging. |
| `HARD_SKIP` (2) — **default** | Stop before the boundary entirely: no candidate L1, no watchdog, no IRQ, no jump. |

The default is `HARD_SKIP` because the `PREFLIGHT_WATCHDOG_ONLY` run on 2026-09-16 left the
device hung with a manual power-button hold needed ([`docs/experiments/experiment-93-stage90-phase0-preflight-watchdog.md`](docs/experiments/experiment-93-stage90-phase0-preflight-watchdog.md)).
Stepping back up to `PREFLIGHT_WATCHDOG_ONLY` and then `FULL` is now a deliberate per-run
decision, enforced by `stages/stage90/preflight_boot_check.sh`:
it verifies the image against `SHA256SUMS.txt`, checks the payload references no storage
symbols, and refuses a run whose image was built with a mode the caller has not explicitly
allowed (`--allow-preflight` / `--allow-full` / `--allow-selftest` /
`--allow-attr-normal-nc` / `--allow-hw-watchdog-selftest`).

Build a switch variant without editing `stage90.h`:

```bash
STAGE90_EXTRA_CFLAGS='-DSTAGE90_EXCLUSIVE_PROBE=1' ./build.sh
```

Those flags land in `CFLAGS`, so `build.sh` records them in
`out/stage90/stage90-build-config.txt` and the gate sees what the image was *actually* built
with rather than what the header is assumed to say. Rebuild without the variable to return to
the default. The concrete run sequence is in
[`docs/status/roadmap.md`](docs/status/roadmap.md) §Phase 0.

`STAGE90_ENTRY_LADDER_LEVEL` (0–4) — the entry stub genuinely calls the first *N* stages of the `arm_init`-shaped ladder and returns; levels above *N* are not called. `0` boot-args only, `1` +early pmap, `2` +`PE_init_platform(FALSE,args)`, `3` +post-PE bootstrap, `4` (**default**) +`arm_vm_init` live pmap and the high-VA handler windows, which is the only level that reaches the handoff. The loader preflight reads the level back out of the stub result and requires only the stages that actually ran — previously it required all five unconditionally, so every level below `FULL` failed structurally before the handoff was ever reached.

### Dead-man reset

The same dump-then-`platform_reboot()` mechanism is now armed as a **dead-man** at the end of
`kernel_entry`'s GIC validation — before the loader preflight, the whole ladder and the
handoff, which is all the unproven code. Every normal exit from the payload already ends in
`platform_reboot()`, so on the happy path the dead-man never fires; if the payload stops
making progress for 60 s (`INTERVAL_US × SAMPLES`), the IRQ handler dumps the interrupted PC
and reboots through PS_HOLD instead of leaving the device hung. The code before that point
has a 90-stage track record, so it is deliberately left with its original IRQ behaviour.

The budget is generous on purpose. Its job is to catch a *hang*, and a hung device is hung
forever — so a minute costs nothing next to a manual power cycle, while a tight budget risks
firing on a slow but healthy payload and turning a good run into a spurious reboot. The
interval is coarse (100 ms, ~10 interrupts/second) for the same reason: the handoff re-arms
its own finer 500 µs interval when it needs PC resolution, and the dead-man only needs to
know the payload stopped moving.

`STAGE90_DEADMAN_SELFTEST=1` spins forever right after arming, making the dead-man the only
route back to Android.

### Hardware watchdog — the last-resort reset

The dead-man depends on the GIC, the ARM timer, IRQ delivery and the vector table all
working, and on IRQs being unmasked. **If a hang happens with any of those broken, or with
IRQs masked, it cannot fire** — which is exactly the failure the 2026-09-16 run produced.

So the payload now also arms the **MSM8974's own hardware watchdog**
([`stages/stage90/hw_watchdog.c`](stages/stage90/hw_watchdog.c), `STAGE90_HW_WATCHDOG`,
**on by default**). It is a hardware counter: when it expires the SoC resets whatever the CPU
is doing, with no software involvement at all. Nothing here is guessed — the base address
`0xf9017000` is from the cancro device tree (`arch/arm/boot/dts/msm8974.dtsi`,
`qcom,wdt@f9017000`), and the register offsets, clock rate and programming order are from that
kernel's own driver (`arch/arm/mach-msm/msm_watchdog_v2.c`). It is also the mechanism Android
itself relies on: a kernel panic on this phone ends in a watchdog bite, and that bite is what
produces a readable `/proc/last_kmsg`. The path being relied on is therefore the device's
normal crash path, not an invention.

It is used two ways:

| | |
| --- | --- |
| **Arm** | At the top of `stage90_main`, before anything that can hang. Bark at 30 s, bite at 33 s — the vendor driver's own split. The payload's normal run is ~1 s, so a good boot never sees it. |
| **Bite now** | `platform_reboot()` forces an immediate bite *after* its PS_HOLD write, so the reboot no longer depends on the PMIC. If PS_HOLD did not take effect — one reading of the 2026-09-16 hang — the SoC still resets. |

MMIO only, no new mapping (`0xf9017000` is inside the already-mapped `0xf9000000` MMIO
section, so it works under both the identity table and the candidate L1), and the register
state is lost on power cycle. The arming is verified by *liveness*, not just by read-back:
`WDT0_STS` holds a live countdown on this part (that is how the vendor driver's `pet` path
works out its slack), so the payload samples it twice, requires it to have moved, and requires
it to sit just under the bark value it programmed — which also distinguishes its own arming
from aboot's 20 s one still running from before. One run therefore settles whether this net is
real. `STAGE90_HW_WATCHDOG=0` builds without it; `STAGE90_HW_WATCHDOG_SELFTEST=1` arms it
and then spins forever, making the hardware countdown the only route back — which is worth
doing **first**, because once it is proven every later run stops costing a manual power cycle.

None of these modes is a shipped feature; they exist to find the failure.

Two further switches are off by default and affect nothing unless set:
`STAGE90_DEADMAN_SELFTEST` (see above) and `STAGE90_EXCLUSIVE_PROBE`, which runs the
roadmap Phase 1 `LDREX`/`STREX` baseline
([`stages/stage90/exclusive_probe.c`](stages/stage90/exclusive_probe.c)) — it changes no
mapping and no cache bit, operating on one word of the payload's own `.bss`.

`STAGE90_PMAP_ATTR_MODE` is the Phase 1a mapping change: `SO_ONLY` (**default**,
byte-for-byte the behaviour of every stage so far) or `NORMAL_NC`, which makes DRAM
Normal/Non-cacheable while MMIO stays Strongly-ordered — the smallest change that makes
`LDREX`/`STREX` architecturally defined. It is deliberately non-cacheable, so it needs no
cache maintenance anywhere; enabling the caches is a separate, later step. The per-PA
reasoning, and why the page tables are allowed to become Normal, is in
[`docs/reference/pmap-attribute-map.md`](docs/reference/pmap-attribute-map.md).

## Next milestones

The current plan is [`docs/status/roadmap.md`](docs/status/roadmap.md), which re-plans the
project after Stage90 and supersedes this section. In short:

1. **Phase 0 — make failure visible and recoverable.** The dead-man reset and the
   non-executable-entry guard are in the tree; the `PREFLIGHT_WATCHDOG_ONLY` run on
   2026-09-16 did **not** self-recover, so the mode was demoted to non-default and the
   recovery net was widened to cover the ladder. Next: re-establish the known-good baseline
   with `HARD_SKIP`, then prove the dead-man with `STAGE90_DEADMAN_SELFTEST=1`.
2. **Phase 1 — cacheable memory policy.** Every mapping in the project is
   Strongly-Ordered, non-cacheable; there is no Normal-memory descriptor anywhere. ARMv7
   `LDREX`/`STREX` are only defined on Normal memory, so this is the prerequisite for the
   kernel taking a single lock — not a performance tweak.
3. **Phase 2 — the iBoot-equivalent handoff contract** (`boot_args`, Apple-format device
   tree, bootstrap page tables in `topOfKernelData`), which is what
   `external/xnu-4570.1.46/osfmk/arm/start.s` actually expects.
4. Only after 1–3: revisit what the handoff target may do.

Two findings that reshape the plan: there is **no public iOS 6-era ARM XNU** (`xnu-2050`
has no `osfmk/arm`; only `xnu-4570.1.46` has ARM code), and the Stage90 jump target is an
inert fixture entry, not kernel code. The realistic target is a 4570-derived ARM kernel on
MSM8974; see the roadmap for the tiers and for the explicit non-goals.

Standing constraints, unchanged from earlier stages:

- Hardware validation stays non-persistent (`sudo fastboot boot`). No flashing without explicit per-operation confirmation.
- Preserve the identity/recovery mappings for ram_console, PS_HOLD, GIC, timer, abort logging and early recovery paths.
- Keep large source checkouts under the ignored `external/` and build outputs under the ignored `out/`.
- Use `xnu-4570.1.46` as the public ARM implementation reference. `xnu-2050.22.13` is iOS 6 / Darwin 12-era context only — it contains no ARM code at all (see `docs/status/roadmap.md` §3).
- No persistent storage writes, in any phase.

The boundaries that earlier stages held closed are now **phase-gated rather than
permanent**, because Phases 1 and 2 of the roadmap require them:

| Boundary | Closed until |
| --- | --- |
| Change cache policy (any Normal/cacheable mapping) | Phase 1 validates an attribute map and an `LDREX`/`STREX` test on hardware |
| Execute public-XNU object code | Phase 3 adds objects one at a time, each with an explicit shim list and a hardware run |
| Enter public XNU `_start` / build a full `mach_kernel` | Phase 4, on top of Phase 1–3 results |
| Execute generated Mach-O bytes | Excluded. The fixture is a fault-injection test case, not a target — see Phase 0 |

Anything not yet at its phase stays closed, with the Stage90 fail-closed switches as the
mechanism.

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
