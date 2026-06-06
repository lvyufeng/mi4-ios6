# Experiment 54 — Stage51 Public-XNU Object Subset Compile

Date: 2026-06-06

Goal: complete the accelerated transition from a public-XNU workspace scaffold to the first real public Apple OSS XNU object-subset compile for cancro/MSM8974 ARMv7. Stage51 compiles a tiny dependency-light subset from the selected public Darwin 12/iOS 6-era baseline, records the compile result in target-side status, and preserves the Stage50 no-XNU/no-link/no-proposed-write/no-cache-change safety boundary.

Stage51 still does **not** run XNU or iOS. It does not build a full public `mach_kernel`, does not link a public-XNU Mach-O, does not execute public-XNU object code, does not execute the generated Mach-O fixture, does not jump to XNU `_start` / `arm_init`, does not write proposed XNU physical load or TTE workspace addresses, does not use the proposed XNU TTE workspace as a live TTBR table, does not mutate external public checkouts, does not write persistent storage, and does not enable or change caches.

## Safety model

- Non-persistent `fastboot boot` only.
- No partition flash/erase/write.
- No bootloader or partition changes.
- No parsed Mach-O entrypoint execution.
- No generated Mach-O fixture execution.
- No real XNU `_start` / `arm_init` handoff.
- No public-XNU object execution.
- No full public `mach_kernel` build attempt.
- No public-XNU Mach-O link.
- No mutation of `external/xnu-upstream` or `external/xnu-4570.1.46`.
- No writes to proposed XNU physical load addresses.
- No writes to proposed XNU TTE workspace physical addresses.
- No use of the proposed XNU TTE workspace as a live TTBR table.
- Inherited controlled TTBR0 round-trip remains Stage-owned and restored.
- Original TTBR0/TTBCR/DACR/SCTLR state remains restored after the inherited selftest.
- Caches remain disabled/unchanged.
- `ram_console`, PS_HOLD, GIC, timer, and abort handlers remain available.
- Public-XNU object outputs stay ignored under `out/stage51/xnu-objects/`.
- Public source checkouts stay ignored under `external/`.

The Stage51 completion message is intentionally explicit:

```text
Stage51 XNU execution disabled: minimal public-XNU object subset compiled only; selected baseline xnu-2050.22.13 recorded; device_tree.c/bootargs.c object compile proved with Stage51 shims; no full mach_kernel build, no public-XNU Mach-O link, no public-XNU object execution, no Mach-O execution, no proposed physical/workspace writes, no persistent writes, caches unchanged
```

## What Stage51 adds over Stage50

Stage50 validated the public-XNU workspace and cancro target scaffold but deliberately did not compile public-XNU code. Stage51 keeps the Stage50/Stage49 runtime proof and adds the first public-only object-subset compile:

- a new standalone `stage51/` payload copied from Stage50,
- Stage51 status/log prefixes (`0x51000001`, `MI4IOS6_STAGE51`),
- command-line markers for:
  - `public-xnu-object-subset`,
  - `stage51-object-compile`,
  - `no-public-xnu-macho-link`,
  - inherited `public-xnu-workspace`, `ttbr0-roundtrip`, `recovery-table`, `cache-bits-preserved`, `no-public-xnu-exec`, `no-macho-exec`, `no-proposed-phys-write`, `no-proposed-tte-write`, `no-persist-write`,
- Stage51-owned compatibility shim headers under `stage51/shims/`,
- host-side object subset compiler `stage51/xnu_object_subset_compile.sh`,
- shim support source `stage51/xnu_object_shims.c`,
- target-side object subset ABI/logs in `stage51/xnu_object_subset.h` / `.c`,
- loader roll-up integration for the object-subset compile result.

## Selected public XNU baseline

Stage51 continues to validate the selected public iOS 6 / Darwin 12-era baseline read-only:

```text
path: external/xnu-upstream
selected ref: xnu-2050.22.13
commit: cc8a9b0ce917bb7115f5c97a78b38db871557db0
config/MasterVersion: 12.3.0
compact commit32: 0xcc8a9b0c
compact master version: 0x000c0300
```

The ignored checkout was not mutated. `external/` remains ignored by git.

## Public-XNU object subset

Stage51 compiles the first two dependency-light public 2050-era source files from the Stage50 manifest:

```text
external/xnu-upstream/pexpert/gen/device_tree.c
external/xnu-upstream/pexpert/gen/bootargs.c
```

A Stage51-owned shim support object is also compiled:

```text
stage51/xnu_object_shims.c
```

The shim layer exists because public `xnu-2050.22.13` lacks a complete iOS ARMv7 XNU tree and its `pexpert/boot.h` / machine headers are not directly usable for this ARMv7 target. The shims are kept under `stage51/shims/` and are intentionally minimal:

```text
stage51/shims/pexpert/boot.h
stage51/shims/pexpert/protos.h
stage51/shims/pexpert/pexpert.h
stage51/shims/kern/kalloc.h
stage51/shims/kern/kern_types.h
stage51/shims/mach/mach_types.h
stage51/shims/mach/boolean.h
stage51/shims/mach/kern_return.h
stage51/shims/mach/vm_types.h
stage51/shims/mach/machine/vm_types.h
stage51/shims/sys/appleapiopts.h
stage51/shims/sys/types.h
```

The important boundary is that these shims make the public sources compile as ARMv7 objects only. They do not create a real pexpert, real IOKit environment, real XNU allocator, full kernel link, or runtime execution path.

## Host object-subset compile result

`stage51/xnu_object_subset_compile.sh` runs during `stage51/build.sh` and writes ignored outputs under:

```text
out/stage51/xnu-objects/
out/stage51/xnu-object-subset-status.txt
out/stage51/xnu-object-subset-manifest.txt
out/stage51/xnu_object_subset_generated.h
```

Successful compile status:

```text
stage51_xnu_object_subset_status=0x51000001
stage51_xnu_object_subset_required_mask=0x00003fff
stage51_xnu_object_subset_satisfied_mask=0x00003fff
stage51_xnu_object_subset_failure_mask=0x00000000
stage51_xnu_object_source_mask=0x00000003
stage51_xnu_object_shim_mask=0x0000001f
stage51_xnu_object_count=0x00000003
stage51_xnu_object_device_tree_bytes=0x00000f28
stage51_xnu_object_bootargs_bytes=0x00000bf4
stage51_xnu_object_device_tree_sha32=0xc507eca7
stage51_xnu_object_bootargs_sha32=0x2f9e47cb
stage51_xnu_object_public_only=0x00000001
stage51_xnu_object_no_full_xnu_build=0x00000001
stage51_xnu_object_no_macho_link=0x00000001
stage51_xnu_object_no_public_xnu_exec=0x00000001
stage51_xnu_object_no_external_mutation=0x00000001
stage51_xnu_object_outputs_ignored=0x00000001
```

Object hashes:

```text
c507eca7dfc5c1edbe829b04a2aaf556141b31aa505642491803d1e6b196ed75  out/stage51/xnu-objects/device_tree.o
2f9e47cbfd5fcf8dd0507e42edb29d62a0935b0da1417b53f63f236c039a1840  out/stage51/xnu-objects/bootargs.o
7368087470b89b13ddb3cfbb2362979367f536fac34ed56485d179cf8473d3cf  out/stage51/xnu-objects/xnu_object_shims.o
```

Object sizes:

```text
   text   data    bss    dec    hex filename
   2025      0     12   2037    7f5 out/stage51/xnu-objects/device_tree.o
   1674      0      0   1674    68a out/stage51/xnu-objects/bootargs.o
    140    256   4104   4500   1194 out/stage51/xnu-objects/xnu_object_shims.o
```

Symbol surface confirms the intended compile-only dependency shape:

```text
device_tree.o: DTInit / DTLookupEntry / DTGetProperty plus unresolved kalloc/kfree
bootargs.o: PE_parse_boot_argn / PE_get_default / PE_imgsrc_mount_supported plus unresolved DT*/PE_boot_args
xnu_object_shims.o: PE_boot_args / kalloc / kfree / IODTGetDefault
```

These objects are not linked into a public-XNU Mach-O and are not executed on hardware.

## Target-side object-subset ABI

The target payload imports the generated host facts through `xnu_object_subset_generated.h` and publishes them through `struct stage51_xnu_object_subset`.

Hardware-proven object-subset roll-up:

```text
stage51_xnu_object_subset_status=0x51000001
stage51_xnu_object_subset_satisfied_mask=0x00003fff
stage51_xnu_object_subset_failure_mask=0x00000000
stage51_xnu_object_subset_checksum=0x771f37c3
stage51_xnu_object_count=0x00000003
stage51_xnu_object_device_tree_sha32=0xc507eca7
stage51_xnu_object_bootargs_sha32=0x2f9e47cb
stage51_xnu_object_no_macho_link=0x00000001
```

## Inherited workspace and TTBR/control safety proof

Stage51 preserves the public-XNU workspace validation:

```text
stage51_xnu_workspace_status=0x51000001
stage51_xnu_workspace_satisfied_mask=0x0007ffff
stage51_xnu_workspace_failure_mask=0x00000000
stage51_xnu_workspace_checksum=0xd8f6992b
```

Stage51 also preserves the controlled no-XNU TTBR0 round-trip behavior:

```text
stage51_ttbr_roundtrip_status=0x51000001
ttbr_rt_l1_base=0x00068000
ttbr_rt_restored_ttbr0=0x0006c000
ttbr_rt_cache_bits_before=0x00000000
ttbr_rt_cache_bits_during=0x00000000
ttbr_rt_cache_bits_after=0x00000000
ttbr_rt_caches_changed=0x00000000
```

The Stage-owned recovery L1 table remains at `0x00068000`, the original live TTBR0 is restored to `0x0006c000`, and caches remain unchanged.

## Loader roll-up

Stage51 extends the loader safety mask to include no public-XNU Mach-O link and adds a loader satisfied bit for the public-XNU object subset.

Hardware-proven loader values:

```text
loader_xnu_workspace_status=0x51000001
loader_xnu_object_subset_status=0x51000001
loader_xnu_object_subset_status_rollup=0x51000001
loader_safety_mask=0x0007ffff
loader_satisfied_mask=0x007fffff
loader_status=0x51000001
```

A first hardware run exposed a stale Stage50 short marker check (`ST50-TEXT` / `ST50-DATA` / `ST50-PRELINK-TEXT`) in the copied Stage51 `macho_probe.c`. The generated fixture correctly contained `ST51-*` markers, so the first run had:

```text
macho_staging_marker_mask=0x00000000
macho_staging_failure_mask=0x01000000
loader_satisfied_mask=0x007ffeff
loader_status=0xd1000000
```

Fixing those checks to `ST51-*` restored materialization success:

```text
macho_staging_marker_mask=0x00000007
macho_staging_failure_mask=0x00000000
macho_staging_status=0x51000001
```

## Built image

```bash
/mnt/data/mi4-ios6/stage51/build.sh
```

Successful local build:

```text
out/stage51/stage51-qcdt.img
sha256=8630e2e609429a79e2eb7eeda3c9c929d0c87f4b3e91717e55c889b54ed705ac
```

Build hashes:

```text
ea72884fdf1fe1ebd0567a86df263fbc38c91a04ee12d9c4c18866fcd1c2840f  out/stage51/stage51_fixture.macho
6edfcdc4c00b7b7a882f2a6bddbefab5782dfbed7fa6d94cd854a7a0b06b5e5a  out/stage51/stage51.elf
c542c8df7db4d95113c122d3c84b49916cc5a5bf5c704266dafe3a73d1e1cab7  out/stage51/stage51.bin
635898c35aca24595afaa3a7e0c6bfd2cf98ae90f55825a019e7197d89d7f645  out/stage51/stage51.img
8630e2e609429a79e2eb7eeda3c9c929d0c87f4b3e91717e55c889b54ed705ac  out/stage51/stage51-qcdt.img
```

Size:

```text
text=169204 data=0 bss=250368 dec=419572 hex=666f4
```

Boot image parse highlights:

```text
magic=ANDROID!
page_size=2048
kernel_size=169204 (0x294f4)
kernel_addr=32768 (0x8000)
ramdisk_size=0 (0x0)
ramdisk_addr=33554432 (0x2000000)
second_size=0 (0x0)
second_addr=15728640 (0xf00000)
tags_addr=31457280 (0x1e00000)
dt_size=2521088 (0x267800)
cmdline=stage51 mi4ios6=stage51 c-runtime apple-dt macho-fixture load-plan materialize highva-dryrun safe-table stage-owned-tables ttbr0-roundtrip recovery-table cache-bits-preserved public-xnu-workspace public-xnu-object-subset cancro-target-scaffold stage51-object-compile no-full-xnu-build no-xnu-jump no-public-xnu-exec no-public-xnu-macho-link no-macho-exec no-proposed-phys-write no-proposed-tte-write no-persist-write
part=kernel offset=0x800 size=169204 sha256=c542c8df7db4d95113c122d3c84b49916cc5a5bf5c704266dafe3a73d1e1cab7
part=dt.img offset=0x2a000 size=2521088 sha256=c8faf487909b668fcebddfb2dcbd793bfb94d0411286c1f5c588bc0015120efe
```

Important symbols:

```text
000080a0 T stage51_vectors
0000c11c T stage51_loader_preflight_run
0001e34c T stage51_xnu_workspace_selftest
0001e5a0 T stage51_xnu_workspace_result
0001e5ac T stage51_xnu_object_subset_selftest
0001e814 T stage51_xnu_object_subset_result
0001e820 T kernel_entry
0001eb84 T test_kernel_entry
0001ed1c T stage51_main
0003c000 b stage51_loader_preflight_block
00068000 b stage51_ttbr0_roundtrip_l1
00070000 b g_stage51_xnu_workspace
00070060 b g_stage51_xnu_object_subset
00072000 B __stage51_image_end
```

Generated output and external checkout policy was verified:

```text
external/ is ignored
out/ is ignored
out/stage51/xnu-objects/device_tree.o is ignored
external/xnu-upstream status --short: clean
external/xnu-4570.1.46 status --short: clean
```

## Hardware run result

Hardware run succeeded using the required non-persistent path.

Command:

```bash
sudo adb reboot bootloader
sudo fastboot boot /mnt/data/mi4-ios6/out/stage51/stage51-qcdt.img
```

Fastboot output:

```text
Sending 'boot.img' (2630 KB)                       OKAY [  0.084s]
Booting                                            OKAY [  0.005s]
Finished. Total time: 0.095s
```

Recovered persistent log:

```bash
sudo adb exec-out 'cat /proc/last_kmsg' > /tmp/cancro-stage51-last_kmsg.txt
```

The recovered log was 117517 bytes and contained:

```text
1793 MI4IOS6_STAGE51 markers
1767 MI4IOS6_STAGE51_XNU markers
```

## Key recovered markers

Object-subset gates pass:

```text
MI4IOS6_STAGE51_XNU stage51_xnu_object_subset_status=0x51000001
MI4IOS6_STAGE51_XNU stage51_xnu_object_subset_satisfied_mask=0x00003fff
MI4IOS6_STAGE51_XNU stage51_xnu_object_subset_failure_mask=0x00000000
MI4IOS6_STAGE51_XNU stage51_xnu_object_subset_checksum=0x771f37c3
MI4IOS6_STAGE51_XNU stage51_xnu_object_count=0x00000003
MI4IOS6_STAGE51_XNU stage51_xnu_object_device_tree_sha32=0xc507eca7
MI4IOS6_STAGE51_XNU stage51_xnu_object_bootargs_sha32=0x2f9e47cb
MI4IOS6_STAGE51_XNU stage51_xnu_object_no_macho_link=0x00000001
```

Workspace gates pass:

```text
MI4IOS6_STAGE51_XNU stage51_xnu_workspace_status=0x51000001
MI4IOS6_STAGE51_XNU stage51_xnu_workspace_satisfied_mask=0x0007ffff
MI4IOS6_STAGE51_XNU stage51_xnu_workspace_failure_mask=0x00000000
```

Inherited TTBR round-trip gates pass:

```text
MI4IOS6_STAGE51_XNU stage51_ttbr_roundtrip_status=0x51000001
MI4IOS6_STAGE51_XNU ttbr_rt_l1_base=0x00068000
MI4IOS6_STAGE51_XNU ttbr_rt_restored_ttbr0=0x0006c000
MI4IOS6_STAGE51_XNU ttbr_rt_cache_bits_before=0x00000000
MI4IOS6_STAGE51_XNU ttbr_rt_cache_bits_during=0x00000000
MI4IOS6_STAGE51_XNU ttbr_rt_cache_bits_after=0x00000000
MI4IOS6_STAGE51_XNU ttbr_rt_caches_changed=0x00000000
```

Loader/final success markers:

```text
MI4IOS6_STAGE51_XNU loader_xnu_workspace_status=0x51000001
MI4IOS6_STAGE51_XNU loader_xnu_object_subset_status=0x51000001
MI4IOS6_STAGE51_XNU loader_xnu_object_subset_status_rollup=0x51000001
MI4IOS6_STAGE51_XNU loader_safety_mask=0x0007ffff
MI4IOS6_STAGE51_XNU loader_satisfied_mask=0x007fffff
MI4IOS6_STAGE51_XNU loader_status=0x51000001
MI4IOS6_STAGE51_XNU Stage51 Mach-O/XNU loader preflight ok
MI4IOS6_STAGE51_XNU kernel_entry ok
MI4IOS6_STAGE51 kernel_entry returned success
MI4IOS6_STAGE51 attempting MSM8974 PS_HOLD reset
```

Negative checks passed: no data abort, no undefined abort, no watchdog-style hang marker, no public-XNU execution marker, no public-XNU Mach-O link marker, no generated Mach-O execution marker, no persistent-write marker, and no cache-bit changes.

## Interpretation

Stage51 is still not a real XNU boot, but it removes the first public-XNU compile blocker. The project now has a reproducible public-only ARMv7 object-subset compile from the selected Darwin 12/iOS 6-era XNU baseline. The compiled public objects are intentionally dependency-light pexpert/device-tree/boot-argument code, not `start.s`, `arm_init.c`, `pmap.c`, or a full kernel.

The important boundary is that Stage51 compiles public-XNU objects but does not link them into a public-XNU Mach-O and does not execute them on hardware. The bootable Stage51 payload only imports generated compile facts and logs them through the target-side ABI. The inherited no-XNU TTBR0 round-trip, Mach-O parser/materialization, local TTE/high-VA/safe-table preflight, SGI/timer IRQ retests, ram_console logging, and PS_HOLD reset path all remain intact.

## Success criteria — met

1. Stage51 directory and build outputs created: yes
2. Stage51 command line includes public-XNU object-subset markers: yes
3. Stale Stage50 source markers removed from Stage51 runtime code: yes after fixing `ST50-*` marker checks to `ST51-*`
4. Host object-subset compiler added: yes
5. Public `device_tree.c` compiled as ARMv7 object: yes
6. Public `bootargs.c` compiled as ARMv7 object: yes
7. Stage51-owned shim support object compiled: yes
8. Object subset status returned `0x51000001`: yes
9. Object subset satisfied mask reached `0x00003fff`: yes
10. Object subset failure mask stayed zero: yes
11. No full public `mach_kernel` build attempted: yes
12. No public-XNU Mach-O link attempted: yes
13. No public-XNU object execution occurred: yes
14. External checkouts remain ignored and clean: yes
15. Target-side object-subset ABI added: yes
16. Loader object-subset status returned `0x51000001`: yes
17. Loader safety mask reached `0x0007ffff`: yes
18. Loader satisfied mask reached `0x007fffff`: yes
19. Loader preflight returned status `0x51000001`: yes
20. Inherited TTBR round-trip still returned `0x51000001`: yes
21. Original TTBR0 restored to `0x0006c000`: yes
22. Cache bits before/during/after stayed `0x00000000`: yes
23. Generated Mach-O fixture execution remained zero: yes
24. Proposed physical load writes remained zero: yes
25. Proposed physical workspace writes remained zero: yes
26. Persistent write attempt remained zero: yes
27. Bootloader accepted `stage51-qcdt.img`: yes
28. `kernel_entry` returned success: yes
29. Payload reset through PS_HOLD: yes

## Next stage

Stage52 should perform a linkable minimal public-XNU Mach-O experiment while preserving the Stage51 safety boundary:

- keep consuming `external/xnu-upstream` at `xnu-2050.22.13` read-only,
- keep using Stage51-owned shims or a tracked minimal compatibility layer,
- link only a tiny public-XNU-derived object subset into a controlled artifact,
- do not build a full public `mach_kernel`,
- do not jump into XNU or execute public-XNU object code on hardware yet,
- do not install proposed XNU page tables yet,
- keep hardware validation non-persistent via `sudo fastboot boot`,
- preserve no-proposed-write/no-cache-change/TTBR-restore guarantees.
