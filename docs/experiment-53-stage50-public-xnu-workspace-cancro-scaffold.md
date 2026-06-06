# Experiment 53 — Stage50 Public-XNU Workspace and Cancro Scaffold

Date: 2026-06-06

Goal: begin the accelerated public-XNU compile migration by creating a tracked cancro/MSM8974 ARMv7 target scaffold and a read-only public-XNU workspace validator, while preserving the Stage49 no-XNU runtime safety boundary. Stage50 validates source references, records the selected public baseline, acknowledges the public 2050 ARM source gap, and prepares the Stage51 minimal object-subset plan. It does **not** build a full public `mach_kernel`, does **not** compile the Stage51 subset yet, and does **not** execute public-XNU code on hardware.

Stage50 still does **not** run XNU or iOS. It does not branch to parsed Mach-O entry metadata, does not execute the generated Mach-O fixture, does not jump to XNU `_start` / `arm_init`, does not execute any public-XNU object code, does not write proposed XNU physical load or TTE workspace addresses, does not use the proposed XNU TTE workspace as a live TTBR table, does not mutate external public checkouts, does not write persistent storage, and does not enable or change caches.

## Safety model

- Non-persistent `fastboot boot` only.
- No partition flash/erase/write.
- No bootloader or partition changes.
- No parsed Mach-O entrypoint execution.
- No generated Mach-O fixture execution.
- No real XNU `_start` / `arm_init` handoff.
- No public-XNU object execution.
- No full public `mach_kernel` build attempt.
- No mutation of `external/xnu-upstream` or `external/xnu-4570.1.46`.
- No writes to proposed XNU physical load addresses.
- No writes to proposed XNU TTE workspace physical addresses.
- No use of the proposed XNU TTE workspace as a live TTBR table.
- Inherited controlled TTBR0 round-trip remains Stage-owned and restored.
- Original TTBR0/TTBCR/DACR/SCTLR state remains restored after the inherited selftest.
- Caches remain disabled/unchanged.
- `ram_console`, PS_HOLD, GIC, timer, and abort handlers remain available.
- Build outputs and generated validation artifacts stay ignored under `out/stage50/`.
- Public source checkouts stay ignored under `external/`.

The Stage50 completion message is intentionally explicit:

```text
Stage50 XNU execution disabled: public-XNU workspace and cancro target scaffold validated only; selected baseline xnu-2050.22.13 recorded; public 2050 ARM gap acknowledged; Stage51 minimal object plan prepared; no full mach_kernel build, no public-XNU object execution, no Mach-O execution, no proposed physical/workspace writes, no persistent writes, caches unchanged
```

## What Stage50 adds over Stage49

Stage49 proved a controlled live TTBR0 round-trip using a Stage-owned recovery L1 table. Stage50 keeps that hardware/runtime proof and adds the first source/build scaffold for public-XNU migration:

- a new standalone `stage50/` payload copied from Stage49,
- Stage50 status/log prefixes (`0x50000001`, `MI4IOS6_STAGE50`),
- command-line markers for:
  - `public-xnu-workspace`,
  - `cancro-target-scaffold`,
  - `stage51-object-plan`,
  - `no-full-xnu-build`,
  - `no-public-xnu-exec`,
  - inherited `ttbr0-roundtrip`, `recovery-table`, `cache-bits-preserved`, `no-macho-exec`, `no-proposed-phys-write`, `no-proposed-tte-write`, `no-persist-write`,
- tracked target scaffold files:
  - `stage50/targets/cancro.mk`,
  - `stage50/targets/cancro.stage51.objects`,
  - `stage50/xnu_workspace.h`,
  - `stage50/xnu_workspace.c`,
  - `stage50/xnu_workspace_validate.sh`,
  - `stage50/README.md`,
- generated ignored workspace validation outputs under `out/stage50/`,
- target-side workspace status ABI and logs,
- loader roll-up integration for workspace, cancro target identity, and Stage51 plan readiness.

## Selected public XNU baseline

Stage50 validates the selected public iOS 6 / Darwin 12-era baseline read-only:

```text
path: external/xnu-upstream
selected ref: xnu-2050.22.13
commit: cc8a9b0ce917bb7115f5c97a78b38db871557db0
config/MasterVersion: 12.3.0
compact commit32: 0xcc8a9b0c
compact master version: 0x000c0300
```

The ignored checkout was not mutated. `external/` remains ignored by git.

## Public 2050 ARM source gap

Stage50 records this as a known limitation rather than a failure:

```text
public 2050 ARM build tree incomplete: acknowledged; Stage50 scaffold only; Stage51 will compile a minimal selected subset with explicit public ARM reference inputs
```

Known missing/incomplete public 2050 ARM paths recorded by the validator include:

```text
external/xnu-upstream/config/MASTER.arm
external/xnu-upstream/osfmk/arm
external/xnu-upstream/bsd/arm
external/xnu-upstream/pexpert/arm
external/xnu-upstream/pexpert/pexpert/arm/boot.h
```

Interpretation: `external/xnu-upstream` is the selected Darwin 12/iOS 6-era public baseline, but it is not a complete public iOS ARMv7 XNU source release. Stage50 therefore prepares a constrained public-only object-subset migration rather than pretending a full ARM `mach_kernel` build is available.

## Later public ARM reference

Stage50 validates later public ARM reference files under `external/xnu-4570.1.46` read-only. This checkout is not iOS 6-era code; it is used only as public ARM implementation reference material.

Required reference files validated include:

```text
external/xnu-4570.1.46/config/MASTER.arm
external/xnu-4570.1.46/osfmk/arm/start.s
external/xnu-4570.1.46/osfmk/arm/arm_init.c
external/xnu-4570.1.46/osfmk/arm/arm_vm_init.c
external/xnu-4570.1.46/osfmk/arm/pmap.c
external/xnu-4570.1.46/osfmk/arm/arm_timer.c
external/xnu-4570.1.46/pexpert/arm/pe_init.c
external/xnu-4570.1.46/pexpert/arm/pe_bootargs.c
external/xnu-4570.1.46/pexpert/arm/pe_identify_machine.c
external/xnu-4570.1.46/pexpert/pexpert/arm/boot.h
```

## Cancro target scaffold

`stage50/targets/cancro.mk` declares the target identity:

```text
STAGE50_TARGET := cancro
STAGE50_ARCH := armv7
STAGE50_CPU := cortex-a15
STAGE50_MACHINE := msm8974
STAGE50_PLATFORM := xiaomi-mi4-cancro
STAGE50_XNU_BASELINE := xnu-2050.22.13
STAGE50_XNU_BASELINE_COMMIT := cc8a9b0ce917bb7115f5c97a78b38db871557db0
STAGE50_XNU_MASTER_VERSION := 12.3.0
STAGE50_ARM_REFERENCE := xnu-4570.1.46
STAGE50_PUBLIC_ONLY := 1
STAGE50_NO_FULL_MACH_KERNEL := 1
STAGE50_NO_XNU_EXECUTION := 1
STAGE50_NO_PUBLIC_XNU_EXECUTION := 1
STAGE50_NO_EXTERNAL_MUTATION := 1
```

This is deliberately declarative. It is not a full XNU makefile and does not patch or copy public XNU trees.

## Stage51 object plan

`stage50/targets/cancro.stage51.objects` records the first public-only candidates for the next stage:

```text
external/xnu-upstream/pexpert/gen/device_tree.c
external/xnu-upstream/pexpert/gen/bootargs.c
external/xnu-upstream/pexpert/pexpert/device_tree.h
external/xnu-upstream/libkern/libkern/OSByteOrder.h
```

Later public ARM references are recorded only to guide the ARMv7 migration boundary:

```text
external/xnu-4570.1.46/pexpert/pexpert/arm/boot.h
external/xnu-4570.1.46/pexpert/arm/pe_bootargs.c
external/xnu-4570.1.46/pexpert/arm/pe_identify_machine.c
external/xnu-4570.1.46/osfmk/arm/start.s
external/xnu-4570.1.46/osfmk/arm/arm_init.c
external/xnu-4570.1.46/osfmk/arm/arm_vm_init.c
external/xnu-4570.1.46/osfmk/arm/pmap.c
```

Stage51 should start with a tiny dependency-light public-XNU-derived object subset or compatibility wrappers around device-tree / boot-argument-adjacent code. It should not start by compiling full `start.s`, `arm_init.c`, or `pmap.c`.

## Host workspace validation result

`stage50/xnu_workspace_validate.sh` runs before the target payload is compiled. It emits only ignored outputs under `out/stage50/`:

```text
out/stage50/xnu-workspace-status.txt
out/stage50/xnu-workspace-manifest.txt
out/stage50/stage51-object-plan.txt
out/stage50/xnu_workspace_generated.h
```

Successful validation output:

```text
stage50_xnu_workspace_status=0x50000001
stage50_xnu_workspace_required_mask=0x0007ffff
stage50_xnu_workspace_satisfied_mask=0x0007ffff
stage50_xnu_workspace_failure_mask=0x00000000
stage50_xnu_baseline_commit32=0xcc8a9b0c
stage50_xnu_master_version=0x000c0300
stage50_xnu_arm_reference=0x45700146
stage50_target_cancro=0x00000001
stage50_target_armv7=0x00000001
stage50_target_msm8974=0x00000001
stage50_public_2050_arm_gap_recorded=0x00000001
stage50_stage51_plan_ready=0x00000001
stage50_no_full_xnu_build=0x00000001
stage50_no_xnu_exec=0x00000001
stage50_no_macho_exec=0x00000001
stage50_no_proposed_phys_write=0x00000001
stage50_no_proposed_tte_write=0x00000001
stage50_no_cache_change=0x00000001
stage50_no_persist_write=0x00000001
stage50_external_checkout_mutated=0x00000000
```

## Target-side workspace ABI

The target payload imports the generated host facts through `xnu_workspace_generated.h` and publishes them through `struct stage50_xnu_workspace`.

Hardware-proven workspace roll-up:

```text
stage50_xnu_workspace_status=0x50000001
stage50_xnu_workspace_required_mask=0x0007ffff
stage50_xnu_workspace_satisfied_mask=0x0007ffff
stage50_xnu_workspace_failure_mask=0x00000000
stage50_xnu_workspace_checksum=0xd9f6992b
stage50_xnu_baseline_commit32=0xcc8a9b0c
stage50_xnu_master_version=0x000c0300
stage50_xnu_arm_reference=0x45700146
stage50_target_cancro=0x00000001
stage50_target_armv7=0x00000001
stage50_target_msm8974=0x00000001
stage50_public_only=0x00000001
stage50_public_2050_arm_gap_recorded=0x00000001
stage50_stage51_plan_ready=0x00000001
stage50_no_full_xnu_build=0x00000001
stage50_no_xnu_exec=0x00000001
stage50_no_macho_exec=0x00000001
stage50_no_proposed_phys_write=0x00000001
stage50_no_proposed_tte_write=0x00000001
stage50_no_cache_change=0x00000001
stage50_no_persist_write=0x00000001
stage50_external_checkout_mutated=0x00000000
```

## Inherited TTBR/control safety proof

Stage50 preserves the controlled no-XNU TTBR0 round-trip behavior:

```text
stage50_ttbr_roundtrip_status=0x50000001
stage50_ttbr_roundtrip_satisfied_mask=0x0000ffff
stage50_ttbr_roundtrip_failure_mask=0x00000000
stage50_ttbr_roundtrip_checksum=0x51df4f5c

ttbr_rt_l1_base=0x00068000
ttbr_rt_restored_ttbr0=0x0006c000
ttbr_rt_cache_bits_before=0x00000000
ttbr_rt_cache_bits_during=0x00000000
ttbr_rt_cache_bits_after=0x00000000
ttbr_rt_caches_changed=0x00000000
```

The Stage-owned recovery L1 table remains at `0x00068000`, the original live TTBR0 is restored to `0x0006c000`, and caches remain unchanged.

## Loader roll-up

Stage50 extends the loader safety mask to include no full public-XNU build, no public-XNU execution, and no external checkout mutation.

Hardware-proven loader values:

```text
loader_xnu_workspace_status=0x50000001
loader_xnu_workspace_satisfied_mask=0x0007ffff
loader_xnu_workspace_failure_mask=0x00000000
loader_xnu_workspace_checksum=0xd9f6992b
loader_cancro_target_status=0x50000001
loader_stage51_plan_status=0x50000001
loader_safety_mask=0x0003ffff
loader_satisfied_mask=0x003fffff
loader_status=0x50000001
```

## Built image

```bash
/mnt/data/mi4-ios6/stage50/build.sh
```

Successful local build:

```text
out/stage50/stage50-qcdt.img
sha256=91ec6aa3557b6896276beced6c0688ff244ea723456cfeccf55fc7f5b5e9e2fe
```

Build hashes:

```text
6e77cdf4035d6e41448a9d8f779cd0ec58a9005311e13a888e1c510f8b2aada8  out/stage50/stage50_fixture.macho
e55176eface5473238b255fc155af0afc5e3c367cee54562ec3ed25ce3c72499  out/stage50/stage50.elf
801afb121803fce7e922870675dbb63ab272331559c0c592e067587a71d6186e  out/stage50/stage50.bin
ce8ae8939ac884a3842a80dc39ebe3f7f70bd9909053253c03312e49d14e2980  out/stage50/stage50.img
91ec6aa3557b6896276beced6c0688ff244ea723456cfeccf55fc7f5b5e9e2fe  out/stage50/stage50-qcdt.img
```

Size:

```text
text=166020 data=0 bss=250272 dec=416292 hex=65a24
```

Boot image parse highlights:

```text
magic=ANDROID!
page_size=2048
kernel_size=166020 (0x28884)
kernel_addr=32768 (0x8000)
ramdisk_size=0 (0x0)
ramdisk_addr=33554432 (0x2000000)
second_size=0 (0x0)
second_addr=15728640 (0xf00000)
tags_addr=31457280 (0x1e00000)
dt_size=2521088 (0x267800)
cmdline=stage50 mi4ios6=stage50 c-runtime apple-dt macho-fixture load-plan materialize highva-dryrun safe-table stage-owned-tables ttbr0-roundtrip recovery-table cache-bits-preserved public-xnu-workspace cancro-target-scaffold stage51-object-plan no-full-xnu-build no-xnu-jump no-public-xnu-exec no-macho-exec no-proposed-phys-write no-proposed-tte-write no-persist-write
part=kernel offset=0x800 size=166020 sha256=801afb121803fce7e922870675dbb63ab272331559c0c592e067587a71d6186e
part=dt.img offset=0x29800 size=2521088 sha256=c8faf487909b668fcebddfb2dcbd793bfb94d0411286c1f5c588bc0015120efe
```

Important symbols:

```text
000080a0 T stage50_vectors
0000c114 T stage50_loader_preflight_run
00017724 T mmu_stage50_ttbr0_roundtrip_result
00017730 T mmu_stage50_ttbr0_roundtrip_selftest
0001dd84 T stage50_xnu_workspace_selftest
0001dfc4 T stage50_xnu_workspace_result
0001dfd0 T kernel_entry
0001e334 T test_kernel_entry
0001e4cc T stage50_main
0003c000 b stage50_loader_preflight_block
00064440 b stage50_ttbr0_roundtrip_block
00068000 b stage50_ttbr0_roundtrip_l1
00070000 b g_stage50_xnu_workspace
00072000 B __stage50_image_end
```

Generated output and external checkout policy was verified:

```text
external/ is ignored
out/ is ignored
external/xnu-upstream status --short: clean
external/xnu-4570.1.46 status --short: clean
```

## Hardware run result

Hardware run succeeded using the required non-persistent path.

Command:

```bash
sudo adb reboot bootloader
sudo fastboot boot /mnt/data/mi4-ios6/out/stage50/stage50-qcdt.img
```

Fastboot output:

```text
Sending 'boot.img' (2628 KB)                       OKAY [  0.084s]
Booting                                            OKAY [  0.005s]
Finished. Total time: 0.094s
```

Recovered persistent log:

```bash
sudo adb exec-out 'cat /proc/last_kmsg' > /tmp/cancro-stage50-last_kmsg.txt
```

The recovered log was 115670 bytes and contained:

```text
1766 MI4IOS6_STAGE50 markers
1740 MI4IOS6_STAGE50_XNU markers
```

## Key recovered markers

Inherited high-root/MMU and IRQ success remains intact:

```text
MI4IOS6_STAGE50_XNU high_root_status=0x50000001
MI4IOS6_STAGE50_XNU mmu high bootstrap selftest ok
MI4IOS6_STAGE50_XNU gic SGI selftest ok
MI4IOS6_STAGE50_XNU gic timer selftest ok
```

Stage50 workspace gates pass:

```text
MI4IOS6_STAGE50_XNU stage50_xnu_workspace_status=0x50000001
MI4IOS6_STAGE50_XNU stage50_xnu_workspace_required_mask=0x0007ffff
MI4IOS6_STAGE50_XNU stage50_xnu_workspace_satisfied_mask=0x0007ffff
MI4IOS6_STAGE50_XNU stage50_xnu_workspace_failure_mask=0x00000000
MI4IOS6_STAGE50_XNU stage50_xnu_workspace_checksum=0xd9f6992b
MI4IOS6_STAGE50_XNU stage50_xnu_baseline_commit32=0xcc8a9b0c
MI4IOS6_STAGE50_XNU stage50_xnu_master_version=0x000c0300
MI4IOS6_STAGE50_XNU stage50_target_cancro=0x00000001
MI4IOS6_STAGE50_XNU stage50_target_armv7=0x00000001
MI4IOS6_STAGE50_XNU stage50_target_msm8974=0x00000001
MI4IOS6_STAGE50_XNU stage50_public_2050_arm_gap_recorded=0x00000001
MI4IOS6_STAGE50_XNU stage50_stage51_plan_ready=0x00000001
MI4IOS6_STAGE50_XNU stage50_no_full_xnu_build=0x00000001
MI4IOS6_STAGE50_XNU stage50_no_xnu_exec=0x00000001
MI4IOS6_STAGE50_XNU stage50_no_macho_exec=0x00000001
MI4IOS6_STAGE50_XNU stage50_no_proposed_phys_write=0x00000001
MI4IOS6_STAGE50_XNU stage50_no_proposed_tte_write=0x00000001
MI4IOS6_STAGE50_XNU stage50_no_cache_change=0x00000001
MI4IOS6_STAGE50_XNU stage50_no_persist_write=0x00000001
MI4IOS6_STAGE50_XNU stage50_external_checkout_mutated=0x00000000
```

Inherited TTBR round-trip gates pass:

```text
MI4IOS6_STAGE50_XNU stage50_ttbr_roundtrip_status=0x50000001
MI4IOS6_STAGE50_XNU stage50_ttbr_roundtrip_satisfied_mask=0x0000ffff
MI4IOS6_STAGE50_XNU stage50_ttbr_roundtrip_failure_mask=0x00000000
MI4IOS6_STAGE50_XNU ttbr_rt_l1_base=0x00068000
MI4IOS6_STAGE50_XNU ttbr_rt_restored_ttbr0=0x0006c000
MI4IOS6_STAGE50_XNU ttbr_rt_cache_bits_before=0x00000000
MI4IOS6_STAGE50_XNU ttbr_rt_cache_bits_during=0x00000000
MI4IOS6_STAGE50_XNU ttbr_rt_cache_bits_after=0x00000000
MI4IOS6_STAGE50_XNU ttbr_rt_caches_changed=0x00000000
```

Loader/final success markers:

```text
MI4IOS6_STAGE50_XNU loader_xnu_workspace_status=0x50000001
MI4IOS6_STAGE50_XNU loader_cancro_target_status=0x50000001
MI4IOS6_STAGE50_XNU loader_stage51_plan_status=0x50000001
MI4IOS6_STAGE50_XNU loader_safety_mask=0x0003ffff
MI4IOS6_STAGE50_XNU loader_satisfied_mask=0x003fffff
MI4IOS6_STAGE50_XNU loader_status=0x50000001
MI4IOS6_STAGE50_XNU Stage50 Mach-O/XNU loader preflight ok
MI4IOS6_STAGE50_XNU kernel_entry ok
MI4IOS6_STAGE50 kernel_entry returned success
MI4IOS6_STAGE50 attempting MSM8974 PS_HOLD reset
```

Negative checks passed: no data abort, no undefined abort, no watchdog-style hang marker, no public-XNU execution marker, no Mach-O execution marker, no persistent-write marker, and no cache-bit changes.

## Interpretation

Stage50 is still not a real XNU boot, but it changes the project from only runtime/loader preflight work into a concrete public-XNU compile-migration track. The selected public Darwin 12/iOS 6-era baseline is now validated reproducibly, the cancro/MSM8974 ARMv7 target identity is tracked in source, the public 2050 ARM gap is recorded instead of hidden, and Stage51 has a constrained public-only object-subset manifest.

The important boundary is that Stage50 did not compile or execute public-XNU object code. It only validated source presence and emitted target-side status facts. The inherited no-XNU TTBR0 round-trip, Mach-O parser/materialization, local TTE/high-VA/safe-table preflight, SGI/timer IRQ retests, ram_console logging, and PS_HOLD reset path all remain intact.

## Success criteria — met

1. Stage50 directory and build outputs created: yes
2. Stage50 command line includes public-XNU workspace/cancro scaffold markers: yes
3. Stale Stage49 source markers removed from Stage50 runtime code: yes
4. Host workspace validator added: yes
5. Validator checks selected `xnu-2050.22.13` commit and `MasterVersion`: yes
6. Validator records public 2050 ARM gap as known limitation: yes
7. Later public ARM reference files validated from `xnu-4570.1.46`: yes
8. `targets/cancro.mk` tracked: yes
9. `targets/cancro.stage51.objects` tracked: yes
10. Generated validation artifacts stay under ignored `out/stage50/`: yes
11. External checkouts remain ignored and clean: yes
12. Target-side workspace ABI added: yes
13. Target-side workspace status returned `0x50000001`: yes
14. Workspace satisfied mask reached `0x0007ffff`: yes
15. Workspace failure mask stayed zero: yes
16. Loader workspace status returned `0x50000001`: yes
17. Loader cancro target status returned `0x50000001`: yes
18. Loader Stage51 plan status returned `0x50000001`: yes
19. Loader safety mask reached `0x0003ffff`: yes
20. Loader satisfied mask reached `0x003fffff`: yes
21. Loader preflight returned status `0x50000001`: yes
22. Inherited TTBR round-trip still returned `0x50000001`: yes
23. Original TTBR0 restored to `0x0006c000`: yes
24. Cache bits before/during/after stayed `0x00000000`: yes
25. Generated Mach-O fixture execution remained zero: yes
26. Public-XNU object execution remained zero: yes
27. Proposed physical load writes remained zero: yes
28. Proposed physical workspace writes remained zero: yes
29. Persistent write attempt remained zero: yes
30. Bootloader accepted `stage50-qcdt.img`: yes
31. `kernel_entry` returned success: yes
32. Payload reset through PS_HOLD: yes

## Next stage

Stage51 has now completed the first minimal public-XNU object-subset compile. It consumed `external/xnu-upstream` at `xnu-2050.22.13` read-only, compiled public `pexpert/gen/device_tree.c` and `pexpert/gen/bootargs.c` as ARMv7 objects with Stage51-owned shims, kept outputs ignored under `out/stage51/xnu-objects/`, avoided a full public `mach_kernel` build, avoided public-XNU Mach-O link and execution, and preserved the non-persistent/no-proposed-write/no-cache-change safety boundary.

Stage52 should now create a linkable minimal public-XNU Mach-O experiment while still avoiding a full public `mach_kernel`, avoiding XNU execution on hardware, and preserving the Stage-owned TTBR restore/cache safety boundary.
