# Experiment 59 — Stage56 XNU bootstrap mapping contract

Date: 2026-06-07

## Goal

Stage56 advances the cancro/MSM8974 XNU bring-up path by adding a Stage-owned bootstrap mapping contract instead of expanding into another runtime-heavy public ARM pexpert object.

Stage55 already proved the bounded public pexpert/platform object set through later-public ARM `pe_consistent_debug.c`. Stage56 keeps that object/link set stable and makes the next progress point explicit: a target-side contract over the proposed XNU bootstrap tuple and mapping facts.

## Safety boundary

Stage56 still does not run XNU or iOS. It does not build a full public `mach_kernel`, does not execute public-XNU object code, does not execute public ARM pexpert/platform runtime code, does not execute the generated Mach-O fixture, does not jump to XNU `_start` / `arm_init`, does not write proposed physical load addresses, does not write proposed TTE workspace addresses, does not use the proposed XNU TTE workspace as a live TTBR table, does not mutate external public checkouts, does not write persistent storage, and does not enable or change caches.

Hardware validation, when performed, must remain non-persistent `fastboot boot` only.

## Implementation

Stage56 adds:

- `stage56/xnu_bootstrap_contract.c`
- a `struct stage56_xnu_bootstrap_contract` ABI in `stage56/stage56.h`
- loader roll-up fields:
  - `xnu_bootstrap_contract`
  - `xnu_bootstrap_contract_status`
  - `xnu_bootstrap_contract_satisfied_mask`
  - `xnu_bootstrap_contract_failure_mask`
  - `xnu_bootstrap_contract_checksum`
  - `xnu_bootstrap_contract_status_rollup`
- a new loader satisfied bit:
  - `STAGE56_LOADER_SAT_XNU_BOOTSTRAP_CONTRACT`
- a boot-image command-line marker:
  - `xnu-bootstrap-contract`

The contract imports only Stage-owned/generated facts from the current preflight:

- inert Mach-O parse/load-plan status
- local BSS staging/materialization status
- TTE dry-run status
- high-VA mapping/translation status
- safe-table local-only materialization status
- TTBR0 round-trip/restore status
- cache-preservation status
- public-XNU compile graph status
- public-XNU object-subset status
- controlled host-only ARM ELF link-proof status

It records the proposed XNU tuple:

```text
virtBase
physBase
memSize
loaded physical base/end
loaded virtual base/end
loaded file end
topOfKernelData
TTE workspace base/size/limit
avail_start
avail_end
```

It also records page/section facts:

```text
page size
page alignment mask
loaded image page count
loaded file page count
zero-fill page count
workspace page count
identity L1 first/last indices
high-VA L1 first/last indices
page coverage mask
section coverage mask
```

And safety facts:

```text
no public-XNU execution
no public platform runtime execution
no Mach-O execution
no proposed physical load write
no proposed TTE workspace write
no persistent write
no cache-policy change
fail-closed graph/object/link behavior
```

## Compile graph update

Stage56 keeps the allowed public object set stable:

```text
external/xnu-upstream/pexpert/gen/device_tree.c
external/xnu-upstream/pexpert/gen/bootargs.c
external/xnu-upstream/pexpert/gen/pe_gen.c
external/xnu-4570.1.46/pexpert/arm/pe_bootargs.c
external/xnu-4570.1.46/pexpert/arm/pe_consistent_debug.c
```

It explicitly blocks the runtime-heavy public ARM pexpert sources:

```text
external/xnu-4570.1.46/pexpert/arm/pe_kprintf.c
external/xnu-4570.1.46/pexpert/arm/pe_serial.c
external/xnu-4570.1.46/pexpert/arm/pe_identify_machine.c
external/xnu-4570.1.46/pexpert/arm/pe_init.c
```

Current graph markers:

```text
stage56_xnu_compile_graph_status=0x56000001
stage56_xnu_compile_graph_required_mask=0x1fffffff
stage56_xnu_compile_graph_satisfied_mask=0x1fffffff
stage56_xnu_compile_graph_failure_mask=0x00000000
stage56_xnu_compile_graph_candidate_count=0x00000011
stage56_xnu_compile_graph_allowed_compile_count=0x00000005
stage56_xnu_compile_graph_allowed_link_count=0x00000005
stage56_xnu_compile_graph_forbidden_count=0x0000000c
stage56_xnu_compile_graph_arm_pe_kprintf_blocked=0x00000001
stage56_xnu_compile_graph_arm_pe_serial_blocked=0x00000001
stage56_xnu_compile_graph_arm_pe_identify_machine_blocked=0x00000001
stage56_xnu_compile_graph_arm_pe_init_blocked=0x00000001
stage56_xnu_compile_graph_bootstrap_contract_selected=0x00000001
stage56_xnu_compile_graph_platform_reference_count=0x00000006
stage56_xnu_compile_graph_blocked_runtime_count=0x00000004
```

## Local validation

Local build passed:

```bash
/mnt/data/mi4-ios6/stage56/build.sh
```

Final local image hashes:

```text
23f6ad9276e1f9a597f5c5aacd8a12dc5ef0a0b5b6a48d3df4edd6c8dd5a4683  out/stage56/stage56_fixture.macho
0175bf6119bea8505166e589636818f8e4ecbc86749cb0a96f1ddcdbc807e449  out/stage56/stage56.elf
be75a4dbca5fbac4103fad8722a5e84ceedce26b24699b41db83bc191818be3d  out/stage56/stage56.bin
2eb7bb4400997ef6fa51c6ce74dd73b779f99460d869a1ae75afa458a6d78993  out/stage56/stage56.img
79322ef80cdcfc79df5e2911b03d5a80b4b397d6af005fe22fa1bdaae4b78319  out/stage56/stage56-qcdt.img
```

Boot-image parse passed for both images. QCDT image highlights:

```text
magic=ANDROID!
page_size=2048
kernel_size=184280 (0x2cfd8)
kernel_addr=32768 (0x8000)
ramdisk_size=0 (0x0)
ramdisk_addr=33554432 (0x2000000)
second_size=0 (0x0)
second_addr=15728640 (0xf00000)
tags_addr=31457280 (0x1e00000)
dt_size=2521088 (0x267800)
cmdline=stage56 mi4ios6=stage56 ... xnu-bootstrap-contract ... no-public-xnu-exec no-platform-runtime-exec no-macho-exec no-proposed-phys-write no-proposed-tte-write no-cache-policy-change no-persist-write no-external-mutation
part=kernel offset=0x800 size=184280 sha256=be75a4dbca5fbac4103fad8722a5e84ceedce26b24699b41db83bc191818be3d
part=dt.img offset=0x2d800 size=2521088 sha256=c8faf487909b668fcebddfb2dcbd793bfb94d0411286c1f5c588bc0015120efe
```

Undefined-symbol checks passed:

```bash
arm-none-eabi-nm -u /mnt/data/mi4-ios6/out/stage56/stage56.elf
arm-none-eabi-nm -u /mnt/data/mi4-ios6/out/stage56/xnu-link/stage56-xnu-link.elf
```

Both commands produced no undefined symbols.

Static stale-reference scan under `stage56/` is clean for stale Stage55 identifiers.

## Expected target markers

The target-side preflight should log:

```text
MI4IOS6_STAGE56_XNU stage56_xnu_compile_graph_status=0x56000001
MI4IOS6_STAGE56_XNU stage56_xnu_compile_graph_bootstrap_contract_selected=0x00000001
MI4IOS6_STAGE56_XNU stage56_xnu_object_subset_status=0x56000001
MI4IOS6_STAGE56_XNU stage56_xnu_link_status=0x56000001
MI4IOS6_STAGE56_XNU stage56_xnu_bootstrap_contract_status=0x56000001
MI4IOS6_STAGE56_XNU loader_xnu_bootstrap_contract_status_rollup=0x56000001
MI4IOS6_STAGE56_XNU loader_status=0x56000001
MI4IOS6_STAGE56_XNU kernel_entry ok
MI4IOS6_STAGE56 kernel_entry returned success
```

Expected negative safety markers remain:

```text
MI4IOS6_STAGE56_XNU ttbr_rt_xnu_entry_executed=0x00000000
MI4IOS6_STAGE56_XNU ttbr_rt_macho_bytes_executed=0x00000000
MI4IOS6_STAGE56_XNU ttbr_rt_proposed_phys_load_written=0x00000000
MI4IOS6_STAGE56_XNU ttbr_rt_proposed_tte_workspace_written=0x00000000
MI4IOS6_STAGE56_XNU ttbr_rt_persistent_write_attempted=0x00000000
MI4IOS6_STAGE56_XNU ttbr_rt_caches_changed=0x00000000
```

## Hardware validation

Stage56 hardware validation completed with non-persistent boot only:

```bash
sudo adb -s 4a2fe00b reboot bootloader
sudo fastboot -s 4a2fe00b boot /mnt/data/mi4-ios6/out/stage56/stage56-qcdt.img
sudo adb -s 4a2fe00b exec-out 'cat /proc/last_kmsg' > /tmp/cancro-stage56-last_kmsg.txt
```

Recovered log facts:

```text
stage56_last_kmsg_bytes=0x0001fceb
stage56_marker_count=0x000007a8
stage56_xnu_marker_count=0x0000078e
stage56_hardware_validation=ok
```

Confirmed key markers:

```text
MI4IOS6_STAGE56_XNU stage56_xnu_compile_graph_status=0x56000001
MI4IOS6_STAGE56_XNU stage56_xnu_compile_graph_bootstrap_contract_selected=0x00000001
MI4IOS6_STAGE56_XNU stage56_xnu_object_subset_status=0x56000001
MI4IOS6_STAGE56_XNU stage56_xnu_link_status=0x56000001
MI4IOS6_STAGE56_XNU stage56_xnu_bootstrap_contract_status=0x56000001
MI4IOS6_STAGE56_XNU stage56_xnu_bootstrap_contract_required_mask=0x7fffffff
MI4IOS6_STAGE56_XNU stage56_xnu_bootstrap_contract_satisfied_mask=0x7fffffff
MI4IOS6_STAGE56_XNU stage56_xnu_bootstrap_contract_failure_mask=0x00000000
MI4IOS6_STAGE56_XNU stage56_xnu_bootstrap_contract_checksum=0x80255575
MI4IOS6_STAGE56_XNU stage56_xnu_bootstrap_contract_proposed_virtBase=0x80008000
MI4IOS6_STAGE56_XNU stage56_xnu_bootstrap_contract_proposed_physBase=0x80000000
MI4IOS6_STAGE56_XNU stage56_xnu_bootstrap_contract_proposed_topOfKernelData=0x8000c000
MI4IOS6_STAGE56_XNU stage56_xnu_bootstrap_contract_proposed_avail_start=0x80016000
MI4IOS6_STAGE56_XNU stage56_xnu_bootstrap_contract_proposed_avail_end=0xde500000
MI4IOS6_STAGE56_XNU stage56_xnu_bootstrap_contract_page_coverage_mask=0x0000001f
MI4IOS6_STAGE56_XNU stage56_xnu_bootstrap_contract_section_coverage_mask=0x0000001f
MI4IOS6_STAGE56_XNU loader_xnu_bootstrap_contract_status_rollup=0x56000001
MI4IOS6_STAGE56_XNU loader_status=0x56000001
MI4IOS6_STAGE56_XNU kernel_entry ok
MI4IOS6_STAGE56 kernel_entry returned success
```

Confirmed negative safety markers:

```text
MI4IOS6_STAGE56_XNU ttbr_rt_xnu_entry_executed=0x00000000
MI4IOS6_STAGE56_XNU ttbr_rt_macho_bytes_executed=0x00000000
MI4IOS6_STAGE56_XNU ttbr_rt_proposed_phys_load_written=0x00000000
MI4IOS6_STAGE56_XNU ttbr_rt_proposed_tte_workspace_written=0x00000000
MI4IOS6_STAGE56_XNU ttbr_rt_persistent_write_attempted=0x00000000
MI4IOS6_STAGE56_XNU ttbr_rt_caches_changed=0x00000000
```

The device returned to Android after the PS_HOLD reset path; no persistent flash/erase/write was performed.
