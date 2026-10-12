# Xiaomi Mi 4 iOS 7 (Darwin 13) — XNU bring-up

An owner-controlled research project to run **iOS 7** on a Xiaomi Mi 4 LTE (`cancro`, Qualcomm
MSM8974, ARMv7). The kernel is **Darwin 13 XNU** (`MasterVersion 13.0.0`, the line Apple shipped in
iOS 7), built by this repository's own harness and booted on the phone's hardware with
`fastboot boot`. The root filesystem it mounts is the real, decrypted **iOS 7.1.2** HFSX volume,
served off the phone's own storage through a copy-on-write overlay.

This is a bring-up, not a ROM port: every rung is one switch, one arm, one run and one reading, and
the reading is taken out of the kernel's own console rather than out of the source. A large part of
the work is making each claim *checkable* — a build that refuses, an arm whose bytes are recorded, a
capture that says which stop the boot reached.

## The goal

The operator's goal, restated 2026-10-07:

> 「我希望能够把xnu正常加载，暂时不考虑图形界面的问题，起码要能进入操作系统，把基础驱动跑起来……我要的是彻底能直接开机就运行xnu，并且能够保持在xnu里，可以通过usb进行调试，这样才能接着做iOS。」

In order:

1. XNU loads, enters the OS, and the basic drivers run. The graphical interface is explicitly out of
   scope until then.
2. **彻底能直接开机就运行xnu** — a plain power-on boots XNU, and it is **resident** in XNU.
3. **可以通过usb进行调试** — the running kernel is debuggable over USB. This is what makes the iOS
   work that follows possible at all.
4. The real iOS userspace on top of that kernel (a genuine `launchd`, not the synthetic fixture the
   earlier line execs), and only then the graphical interface.

And two acceptance clauses on the hardware itself:

> 「安装好的xnu，能够正确识别xiaomi 4的3GB内存，16GB/32GB存储」

XNU must correctly **recognise** the Mi 4's 3 GB of RAM and its 16 GB / 32 GB storage. Both are
clauses about what the kernel reports. The storage half is served by the card's own EXT_CSD count
(`0x01d5a000` sectors ≈ 15.76 GB, i.e. the 16 GB part). The RAM half is the larger port: the two
banks are `[0, 0x60000000) ∪ [0x80000000, 0xe0000000)` = exactly 3.000 GiB, while ARM32 XNU's
kernel virtual space is ~2 GiB — so both banks *cannot* be linearly mapped at once. What is in the
tree today makes the kernel report the device total (through `/defaults hw.memsize`); the
region-list port that would let it own the low bank is still owed.

## Where it stands (2026-10-12)

The boot runs XNU's `bsd_init()` to completion. The 2026-10-12 run (arm `armed-d13-2df6333a`,
commit `cb2dd66`, experiment 987) printed the entire BSD init sequence — `Added memory device
md0/rmd0` → `BSD root: md0, major 4, minor 2` → `bsd_init: vfs_mountroot` → **`bsd_init: done`**
(55 `bsd_init:` lines) — against the real 896 MiB iOS 7.1.2 HFSX volume. The mount is read-write
through the copy-on-write overlay, with **the card's base never written** (`write_served` advancing,
`CARD_COW=1`, `HDD_WRITE=0`). The IOKit time path (`cal_enter`/`cal_exit`, `iokitinit_exit`) and the
ARM timer tick both ran and returned, retiring the walls the 984–986 rungs were built against.

**The frontier is experiment 988**: the first statement *after* `bsd_init()` returns —
`osfmk/kern/startup.c:408` `OSKextRemoveKextBootstrap()` — NULL-derefs inside
`OSKext::removeKextBootstrap()` (the `#if CONFIG_KXLD` teardown in `libkern/c++/OSKext.cpp`),
because `getsegbyname("__LINKEDIT")` finds no such segment in our kernel and the result is
dereferenced unguarded. Ours is an ELF-linked kernel whose synthetic Mach-O header declares only
`__TEXT`/`__DATA`/`__PRELINK_TEXT`; Apple's teardown assumes the segment set of a real Mach-O
kernel.

That paragraph is a **pointer, not a permanent claim**. The authority on what the current arm is and
what it was built to answer is the newest set at the bottom of `records/revert-set.txt`; the
authority on what it actually did is the newest capture in `out/stage90/captures/`.

Two numbers worth carrying:

- The payload hands XNU a **484 MiB** entry window (`STAGE90_XNU_ENTRY_WINDOW=0x1e400000`), not the
  16 MiB default. The real `launchd` links libSystem out of a **301 MiB** dyld shared cache, which
  cannot fit in the old window.
- The console of a resident rung is read **live over the USB ladder** (`tools/usb_console_read.py`,
  the ChipIdea OTG controller), not from `/proc/last_kmsg` — a run that stays in XNU has no next boot
  to recover a log from. For a run that did not return, `scripts/recover_last_kmsg.sh` reads the
  previous boot's RAM console before any power cycle clears it.

## Two branches — build the right one

| Branch | What it is |
| --- | --- |
| **`master`** | **The live line: Darwin 13 / iOS 7.** Everything described in this file is here. |
| `4570` | Frozen at `a89cfdc`. The earlier Darwin-17 (`xnu-4570.1.46`) attempt. It reached what *that* line's goal was — XNU mounts the device's eMMC and execs a launchd fixture — but a Darwin-17 kernel cannot present the iOS 7.1.2 userspace ABI, which is why the base moved down. It is a snapshot, not a work line: do not commit to it. |

The pivot to Darwin 13 (2026-10-08) was forced by three measurements: the live build base was
Darwin 17, not iOS 6; the `/sbin/launchd` the old line execs was a 0x2000-byte synthetic ARM Mach-O
fixture rather than Apple code; and **Apple never published an iOS 6/7 ARM kernel** — every iOS
6-era public tree has no `osfmk/arm/` at all. The Darwin-13 tree here comes from the HD2 iOS 7 lab's
*generic* ARM XNU core (winocm darwin-on-arm lineage, APSL 2.0); that board's own files carry no
reuse license and are inspected, not copied.

## Where things are

| Path | Contents |
| --- | --- |
| `src/` | The live tree — one evolving tree, not one directory per stage. The payload's 67 `.c` files are flat here (the build names objects by basename, so subdirectories would collide), with `src/entry/` (the ARM entry-image builder and the XNU entry stubs), `src/platform/`, `src/supply/`, `src/regions/`, `src/shims/`, `src/shims_arm/`, `src/firehose/` and `src/targets/` beside them. |
| `scripts/` | What builds and fires it: `build.sh`, **the gate** `preflight_boot_check.sh`, **the runner** `run_and_capture.sh`, `preflight_storage_write.sh`, `recover_last_kmsg.sh`, `stage_boot_p19.sh`, the `xnu_*.sh` compile-graph helpers, and the per-rung launchers `build_<n>.sh` / `park_<n>.sh` / `press_<n>.sh`. All of them resolve the repository root themselves, so they work from any working directory. |
| `records/` | The arm record `records/revert-set.txt` (every parked arm, its 12 file hashes and what it was built to answer), `records/baseline-readings.txt`, and `records/tool-images/`. |
| `tools/` | Host-side helpers. The press path: `verify_press_ready.sh` (the five things that must be true at the moment of a press), `resolve_arm_set.sh` (which set the bytes in `out/` are), `verify_revert_set.sh`, `gate_flags_for_arm.sh`. The kernel harness: `build_xnu_arm_kernel.sh`, `assemble_arm_layer.sh`, `gen_kernel_source_manifest.py`, `check_d13_*.py`, `patch_d13_*.py`, `stage_d13_*.sh`. The readers: `usb_console_read.py`, `decode_armv7_descriptor.py`, `read_storage_commands.py`, `xnu_dt_walk.py`. |
| `docs/` | All documentation, indexed in [`docs/README.md`](docs/README.md): `reference/`, `experiments/` (one log per rung, chronological), `status/`, `history/`. Note that `status/roadmap.md` predates the 2026-10-08 pivot and describes the 4570 line; the newest experiment logs are the current account. |
| `archive/stages/stage85` … `archive/stages/stage89` | The snapshots the retired snapshot-per-stage model left behind (`stage0` … `stage84` live at the tag `stage-archive-base`). See [`archive/stages/README.md`](archive/stages/README.md). |
| `Makefile` | Build/list/restore/check convenience targets. `make check` runs the source-half checks; the first is `tools/check_stage_paths.sh`, which refuses a tracked file that still names the retired snapshot layout. |
| `external/` | The XNU checkouts (ignored by git). **`external/xnu-hd2-darwin13/xnu` is the tree we build — not a reference.** `external/xnu-4570.1.46` belongs to the frozen line. |
| `out/stage90/` | Build products (ignored by git): the payload, the entry image, `frozen/` (one directory per parked arm), `captures/` (every run log). |
| `xiaomi4-cancro-backup-20260604-112053/` | Device partition backups (ignored by git, never committed). Also supplies the QCDT blob `boot-unpacked/dt.img` the payload build packs. |

## Build and boot

The kernel pool, the entry image and the payload are three separate builds, and the per-rung
launchers (`scripts/build_<n>.sh`) are what actually run them — they re-export the previous arm's
switch set verbatim rather than retyping it, because a retyped switch set is how the wrong arm gets
built.

```bash
# 1. The D13 kernel pool: osfmk / bsd / libkern / pexpert objects for this tree and config.
XNU_TREE=$PWD/external/xnu-hd2-darwin13/xnu XNU_KERNEL_CONFIG=STAGE90_XNU \
XNU_MASTER_LOCAL=$PWD/tools/xnu_config/boot/STAGE90_XNU.local \
    tools/build_xnu_arm_kernel.sh

# 2. The entry image: XNU's own osfmk/arm/start.s linked with the entry stubs, at the base the
#    payload jumps to.  -> out/stage90/xnu_arm_entry.{bin,elf,h}
( cd src/entry && ./build_entry.sh )

# 3. The payload: the image `fastboot boot` sends.
#    ** STAGE90_XNU_ENTRY is 0 by default; a payload built without it never jumps into XNU. **
STAGE90_EXTRA_CFLAGS='-DSTAGE90_XNU_ENTRY=1' ./scripts/build.sh

sha256sum -c out/stage90/SHA256SUMS.txt          # per-build manifest (absolute paths: run it here)

# 4. Gate, then press — one gate, one runner, never a raw fastboot.
./scripts/preflight_boot_check.sh --allow-xnu-entry
./scripts/run_and_capture.sh --allow-xnu-entry --expect-arm=<set>
```

`run_and_capture.sh` does the whole cycle — gate, boot, wait for the device, capture the console,
summarise the markers — so the commands are not retyped at the moment they matter most. It never
flashes. For a **resident** arm, exit 2 (the device did not come back) is the expected outcome, not
a failure: start `tools/usb_console_read.py --wait` **before** the press and the console is captured
live either way.

**`--expect-arm` is required, and it is the one argument to type from memory rather than from this
file.** A run sends exactly one image, and that image *is* the instrument its reading is taken with,
so a run that does not say which arm it is for cannot be attributed afterwards — and a launcher left
pointing at an arm that has since been superseded boots the wrong one with no symptom at all. The
flag's value is a recorded set name from `records/revert-set.txt`;
`tools/resolve_arm_set.sh out/stage90` prints the one the bytes currently in `out/` are, and
`tools/verify_press_ready.sh` prints it too, along with the gate flags that arm needs. The runner
resolves the same thing itself and refuses if the two disagree, if the image is not a recorded set,
or if the image moves between the gate's verdict and the send.

Booting is deliberately not a `make` target: `fastboot boot` is a per-operation decision and it
never writes to the device.

## The method

This is the discipline the rungs are held to, and it is the part worth keeping when everything
above has moved on.

- **One rung, one switch set.** A rung changes one thing. The switch set is written into
  `out/stage90/xnu_arm_entry-config.txt` and `out/stage90/stage90-build-config.txt` by the build
  itself, and the gate reads it back out of the artifact rather than trusting the header.
- **An arm is a name, a park and a record.** The arm is `armed-d13-<sha8 of
  out/stage90/xnu_arm_entry.bin>`; it is parked as a self-contained directory of 12 files under
  `out/stage90/frozen/<arm>/`; and it is recorded, with hashes and the question it answers, in
  `records/revert-set.txt`. `frozen/` is a **record, not a queue** — a parked arm is not an armed
  one.
- **A claim in a comment is not a check.** Every claim is verified **by value** against the linked
  artifact: a symbol resolved out of `nm`, an instruction decoded out of linked bytes, a `bl` count
  that counts call sites, a hash that is recomputed. Where a check can be made to stop the build, it
  is. The gate refuses a run whose image was built with a switch the caller has not explicitly
  allowed, and it refuses a kernel whose source has drifted from the recorded content manifest
  (`tools/gen_kernel_source_manifest.py`) — a preflight that judged freshness by mtime over `src/`
  alone once let a stale object through.
- **A reading names its stop.** The frontier is read out of the capture, not reasoned about:
  `scripts/run_and_capture.sh --summarise` over a log, or `tools/usb_console_read.py` live.
- **The tree moves only by explicit path.** `git add` by path (never `-A`), no `--force`, and the
  refs verified after every push.

## The device and its backups

Connected device observed via ADB:

- Model: Xiaomi MI 4LTE; codename `cancro`; Qualcomm MSM8974; CPU ABI `armeabi-v7a`
- Stock Android: Android 10 userdebug-style build, Linux 3.4.113, root-capable shell
- eMMC: `mmcblk0` = 15,388,672 KB; `userdata` is p25 (13,610,499,072 B ≈ 13.6 GB) — the partition
  whose head carries the card image a run mounts
- The card's own EXT_CSD says 30,777,344 sectors (≈ 15.76 GB) — the 16 GB part

Before any write/flash operation, key boot-critical partitions were backed up locally from the
connected phone, in `xiaomi4-cancro-backup-20260604-112053/` (intentionally not committed):
`sbl1.img`, `rpm.img`, `tz.img`, `aboot.img`, `boot.img`, `recovery.img`, `persist.img`,
`modem.img`, and a verified `SHA256SUMS.txt`. The backup's unpacked `dt.img` is also a build input.

The backed-up `boot.img` is a standard Android boot image, and the payload is packed into the same
shape (`tools/mkbootimg_v0_qcdt.py`):

- Page size 2048; kernel offset `0x8000`; ramdisk offset `0x02000000`; second offset `0x00f00000`;
  tags offset `0x01e00000`
- Qualcomm QCDT device tree attached **as the legacy Android v0 `dt_size` field** — the cancro
  bootloader refuses a custom `fastboot boot` payload without it (`dtb not found`)

## Archive policy

The working tree keeps the five most recent stage snapshots that were still in it, under
`archive/stages/`. Each is a complete, self-contained copy of its predecessor plus that stage's
delta, so any of them can be built and bisected as-is. Stages older than that (`stage0` …
`stage84`) were removed from the tree and live on in git at the tag `stage-archive-base`:

```bash
tools/stage-archive.sh list                      # what is archived, what is present
tools/stage-archive.sh show 50 xnu_workspace.c   # read a file without restoring
tools/stage-archive.sh restore 50                # -> ./stage50/, at the root so it builds
```

Restored snapshots land at the repository root, because their scripts predate the `archive/stages/`
layout and resolve `../out` / `../external` from that depth. `make restore STAGE=50` is the
shorthand. The snapshot-per-stage model itself was retired on 2026-09-26 — there is no next stage to
archive for — and what the remaining snapshots are is described in
[`archive/stages/README.md`](archive/stages/README.md).

## Safety rules for this repo

Four properties are load-bearing on every device command, in this order:

1. **`fastboot boot` only — never `fastboot flash`.** A run is non-persistent; nothing is written to
   the device's own partitions by this project's boot path.
2. **The target is serial `4a2fe00b` (`cancro`).**
3. **The other phone, `33e80afe`, must be UNPLUGGED** before any device command. Every device-side
   launcher — the runner, the recovery reader, the `press_<n>.sh` scripts — checks for it and refuses
   if it is attached.
4. **`fastboot` must stay reachable.** The operator has relaxed the storage envelope — repartitioning
   is permitted and TWRP/Android are expendable — but only against "can fastboot still be entered if
   this lands wrong", and the answer must be yes. **Do not brick the device.**

And the standing rules that follow from them:

- The current root is mounted read-write **through a copy-on-write overlay**: the card's base is
  never written, and every press is reversible from the host. The one persistent write in the
  project's history (TWRP into `recovery`) was separately authorized and is recorded in `docs/`.
- Writing XNU into the device's own `boot` partition (`scripts/stage_boot_p19.sh`) is a distinct,
  later step, not taken by a rung. It carries a **bootloop hazard**: once XNU is in p19, a watchdog
  bite reboots back into XNU rather than into Android, and XNU brings up no `adbd`. The escape is
  VolDown + Power + USB → fastboot.
- Prefer `fastboot boot` or other non-persistent tests before any persistent write.
- Never include personal data or large partition backup images in normal source commits; keep
  large checkouts under the ignored `external/` and outputs under the ignored `out/`.
- Keep recovery instructions and hashes close to any experimental boot image work. Open-source,
  owned-device research only; do not rely on leaked proprietary Apple code.