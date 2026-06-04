# Xiaomi Mi 4 iOS 6 / Darwin Experiment

This repository tracks an experimental, owner-controlled research project around the Xiaomi Mi 4 LTE (`cancro`) and the feasibility of running iOS 6 / Darwin / XNU-like components on non-Apple ARMv7 hardware.

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

## Confirmed milestones

- Non-persistent `sudo fastboot boot <img>` works on this device.
- USB-only debugging works through Android `/proc/last_kmsg` / `ram_console`; no teardown/UART was required for Stage0/Stage1.
- Stage0 executed a raw non-Linux ARMv7 payload at `0x00008000`, wrote `MI4IOS6_STAGE0` into persistent RAM at `0xde500000`, and reset the phone through MSM8974 PS_HOLD at `0xfc4ab000`.
- Stage1 executed a boot-wrapper payload, set up stack and `.bss`, built a public-XNU-style ARM `boot_args` structure, attached a minimal Apple flattened device tree stub, called `test_kernel_entry(boot_args*)`, validated the handoff, logged `MI4IOS6_STAGE1 handoff ok`, and reset back to Android.
- Stage2 entered a freestanding C runtime, built a fuller Apple-style device tree (`/chosen`, `/memory`, `/cpus`, `/msm8974-io`, `/interrupt-controller`, `/timer`), walked/validated the tree, logged `apple_dt selftest ok` and `handoff ok`, and reset back to Android.
- Stage3 performed read-only hardware probes from the C runtime: CP15 state, ARM generic timer (`CNTFRQ=19.2 MHz`, `CNTPCT` advancing), and GIC distributor/CPU-interface IDs at `0xf9000000` / `0xf9002000`, then reset back to Android.
- Stage4 installed a custom ARMv7 exception vector table at VBAR `0x000080a0`, implemented a `CNTPCT/CNTFRQ` timebase with `delay_us`, verified 1000us/5000us delays, caught a deliberate undefined-instruction exception, logged LR/SPSR, and reset through PS_HOLD.
- Stage5 split the payload into a boot-wrapper path and `kernel_entry(struct boot_args*)`, added `MI4IOS6_STAGE5_XNU` skeleton logs, implemented pexpert-like Apple-DT discovery for memory/CPUs/GIC/timer, initialized XNU-like `ml_*` timebase stubs, and returned success before PS_HOLD reset.
- Stage6 added a `PE_state`-like platform state block populated from `boot_args`/Apple-DT, validated memory/CPU/GIC/timer/vector facts, then deliberately triggered and recovered from a data abort through the custom exception handler.
- Stage7 added a read-only MSM8974 GIC driver skeleton, captured distributor/CPU-interface state, enable/pending/priority/target registers for the first IRQ group, validated IRQ count 288 and 4 CPU interfaces, and returned through `kernel_entry` successfully.
- Stage8 installed a returnable IRQ vector path, generated SGI0 to the current CPU through `GICD_SGIR`, handled it by reading `GICC_IAR` / writing `GICC_EOIR`, logged interrupt ID 0, and returned through `kernel_entry` successfully.
- Stage9 programmed the ARM generic physical timer, enabled the local timer PPI path, handled timer interrupt ID 19 through the same IRQ/EOIR path, masked/disabled the timer again, and returned through `kernel_entry` successfully.
- Stage10 built an ARMv7 section identity map, enabled `SCTLR.M` with caches still disabled, verified ram_console/IMEM/GIC/timer MMIO access under translation, then re-ran SGI and timer IRQ selftests successfully.
- Stage11 added high virtual aliases for the low payload section, ram_console, and GIC/timer MMIO, validated alias-vs-identity data/vector/MMIO reads with caches disabled, and re-ran SGI/timer IRQ selftests successfully.

## Repository contents

- `docs/local-device-findings.md` — detailed local observations, partition map, backup status, and parsed boot/recovery image fields.
- `docs/recovery-and-rollback.md` — required recovery checklist and rollback procedure before any persistent write.
- `docs/boot-tooling.md` — local boot image tooling plan and no-op round-trip results.
- `docs/cancro-platform.md` — Xiaomi Mi 4 / MSM8974 platform source pointers and bootloader notes.
- `docs/darwin-xnu-research.md` — open Darwin/XNU research notes and milestone framing.
- `docs/source-baseline.md` — external source checkout baseline for cancro kernel/device tree and public XNU references.
- `docs/experiment-01-cmdline.md` — first successful experiment: custom kernel cmdline via non-persistent boot.
- `docs/no-teardown-debugging.md` — USB-only debugging channels (`/proc/last_kmsg`, `/dev/kmsg`, ramoops) that avoid soldering a UART.
- `docs/experiment-02-usb-log-loop.md` — verified printk/kmsg markers survive reboot into `/proc/last_kmsg`.
- `docs/msm8974-xnu-porting-map.md` — concrete MSM8974 ↔ XNU platform interface and work-package map.
- `docs/stage0-payload-plan.md` — plan for first non-Linux ARMv7 payload executed via `fastboot boot`.
- `docs/experiment-03-stage0-bare-metal.md` — successful Stage0 bare-metal payload execution and ram_console/PS_HOLD proof.
- `docs/stage1-boot-wrapper-plan.md` — Stage1 boot-wrapper design for XNU-style `boot_args` and Apple-DT handoff.
- `docs/experiment-04-stage1-boot-wrapper.md` — successful Stage1 boot-wrapper hardware test and recovered persistent log.
- `stage0/` — tiny bare-metal ARMv7 payload that writes a ram_console marker and attempts MSM8974 reset.
- `stage1/` — ARMv7 boot-wrapper payload that builds a public-XNU-style `boot_args` structure and validates `test_kernel_entry(boot_args*)` handoff.
- `stage2/` — freestanding C runtime payload that builds and walks a fuller Apple-style device tree for the MSM8974 bring-up contract.
- `stage3/` — C runtime payload that performs read-only CP15, ARM generic timer, and GIC probes on the MSM8974 hardware.
- `stage4/` — C runtime payload with custom ARMv7 exception vectors, early timebase, and deliberate exception recovery test.
- `stage5/` — XNU-adjacent skeleton with `kernel_entry(struct boot_args*)`, pexpert-like Apple-DT discovery, and `ml_*` timebase stubs.
- `stage6/` — XNU-adjacent skeleton with `PE_state`-like platform state and deliberate data-abort recovery test.
- `stage7/` — XNU-adjacent skeleton with a read-only MSM8974 GIC driver/state snapshot.
- `stage8/` — XNU-adjacent skeleton with controlled SGI0 delivery and a returnable GIC IRQ handler path.
- `stage9/` — XNU-adjacent skeleton with ARM generic timer one-shot IRQ delivery.
- `stage10/` — XNU-adjacent skeleton with first ARMv7 MMU identity-map enable.
- `stage11/` — XNU-adjacent skeleton with controlled high virtual aliases over the identity map.
- `docs/ios-613-oss-baseline.md` — notes on Apple OSS `distribution-iOS@ios-613` and public XNU baseline implications.
- `docs/experiment-05-stage2-c-runtime.md` — successful Stage2 C runtime + Apple-DT builder/walker hardware test.
- `docs/experiment-06-stage3-hardware-probes.md` — successful Stage3 read-only CP15/timer/GIC hardware probes.
- `docs/experiment-07-stage4-timebase-vectors.md` — successful Stage4 VBAR/vector + `CNTPCT` timebase/delay test.
- `docs/experiment-08-stage5-xnu-skeleton.md` — successful Stage5 XNU-adjacent kernel-entry skeleton test.
- `docs/experiment-09-stage6-pe-state-abort.md` — successful Stage6 PE_state-like platform state and data-abort recovery test.
- `docs/experiment-10-stage7-gic-skeleton.md` — successful Stage7 read-only GIC driver skeleton test.
- `docs/experiment-11-stage8-sgi-irq.md` — successful Stage8 controlled SGI0 IRQ delivery test.
- `docs/experiment-12-stage9-timer-irq.md` — successful Stage9 ARM generic timer IRQ delivery test.
- `docs/experiment-13-stage10-mmu-identity.md` — successful Stage10 ARMv7 MMU identity-map enable test.
- `docs/experiment-14-stage11-high-alias.md` — successful Stage11 high virtual alias mapping test.
- `tools/parse_android_bootimg.py` — dependency-free parser/extractor for Android boot image v0/v1-style files.
- `tools/patch_bootimg_cmdline.py` — surgical editor that changes only the kernel command line, preserving kernel/ramdisk/QCDT and the boot `id`.
- `tools/mkbootimg_v0_qcdt.py` — legacy Android boot image v0 packer that populates the Qualcomm QCDT `dt_size` field required by cancro bootloader.

Example parser usage:

```bash
./tools/parse_android_bootimg.py xiaomi4-cancro-backup-20260604-112053/boot.img
```

The backup directory is ignored by git, so this command only works on a host where the local backup exists.

## Safety rules for this repo

- Do not flash or erase partitions without an explicit confirmation for that specific operation.
- Prefer `fastboot boot` or other non-persistent tests before persistent writes.
- Never include personal data or large partition backup images in normal source commits.
- Keep recovery instructions and hashes close to any experimental boot image work.
- Focus on open-source, owned-device research. Do not rely on leaked proprietary Apple code.

## Next milestones

Stage0 through Stage11 are complete. The next practical target is Stage12: a tiny high-virtual call/data bootstrap experiment.

Near-term Stage12 work:

- Keep using non-persistent `sudo fastboot boot`; do not flash without explicit per-operation confirmation.
- Preserve identity mappings for ram_console, PS_HOLD, GIC, timer, and early recovery paths.
- Add high aliases for code and data and call a tiny function through its high virtual alias.
- Use a high virtual pointer for a small state block and validate writes through both identity and alias paths.
- Keep caches disabled while alias execution behavior is tested.
- Return safely to the identity-mapped path and preserve SGI/timer IRQ retests plus abort logging.
