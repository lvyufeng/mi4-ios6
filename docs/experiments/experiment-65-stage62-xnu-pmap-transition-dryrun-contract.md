# Experiment 65 — Stage62 XNU pmap safe live-table transition prerequisite dry-run contract

Date: 2026-06-08

## Goal

Stage62 advances the cancro/MSM8974 XNU bring-up path by adding a Stage-owned XNU pmap safe live-table transition prerequisite dry-run contract on top of the Stage56 bootstrap mapping contract, Stage57 pmap/bootstrap allocation contract, Stage58 pmap section-table dry-run contract, Stage59 pmap page-granular dry-run contract, Stage60 pmap cache/MMU attribute dry-run contract, and Stage61-style pmap multi-window page-granular dry-run contract.

Stage61 proved that exact inherited public ARM XNU PTE templates can be composed into three independent local coarse/L2 windows. Stage62 keeps that prerequisite stable and adds the next blocker: prove that the local multi-window descriptor plan can be mirrored into a separate candidate L1 table, that representative translations still resolve correctly, that a future TTBR0/TTBCR/DACR/SCTLR transition plan can be recorded read-only, and that recovery continuity remains intact before any live table install is attempted.

The Stage62 transition dry-run models:

1. a candidate L1 table for the kernel/workspace RAM window,
2. a candidate L1 table view of the RAM-console window,
3. a candidate L1 table view of the device/GIC-style window,
4. proposed TTBR0/TTBCR/DACR/SCTLR values for a future transition,
5. restored/recovery table facts proving the current execution path remains recoverable.

## Safety boundary

Stage62 still does not run XNU or iOS. It does not build a full public `mach_kernel`, does not execute public-XNU object code, does not execute public ARM pexpert/platform runtime code, does not execute public ARM VM/pmap runtime code, does not execute the generated Mach-O fixture, does not jump to XNU `_start` / `arm_init`, does not write proposed physical load addresses, does not write proposed TTE/pmap workspace addresses, does not install live proposed XNU/pmap tables, does not write TTBR/TTBCR/DACR/SCTLR for proposed pmap tables, does not invalidate TLBs for proposed pmap tables, does not change cache policy, does not mutate external public checkouts, and does not write persistent storage.

Hardware validation remains non-persistent `fastboot boot` only.

## Implementation

Stage62 adds:

- `stage62/xnu_pmap_transition_dryrun_contract.c`
- new pmap transition dry-run ABI in `stage62/stage62.h`
- a new loader satisfied bit:
  - `STAGE62_LOADER_SAT_XNU_PMAP_TRANSITION_DRYRUN_CONTRACT`
- new loader roll-up fields:
  - `xnu_pmap_transition_dryrun_contract`
  - `xnu_pmap_transition_dryrun_contract_status`
  - `xnu_pmap_transition_dryrun_contract_satisfied_mask`
  - `xnu_pmap_transition_dryrun_contract_failure_mask`
  - `xnu_pmap_transition_dryrun_contract_checksum`
  - `xnu_pmap_transition_dryrun_contract_status_rollup`
- build source inclusion for `xnu_pmap_transition_dryrun_contract.c`
- boot-image command-line markers:
  - `xnu-pmap-transition-dryrun-contract`
  - `pmap-transition-local-dryrun-only`
- fixed-size public ARM boot-args markers for the transition stage:
  - `stage62/boot_args.c`
  - `stage62/stage62_main.c`
  - `stage62/xnu_object_shims.c`

The contract imports only Stage-owned/generated facts:

- Stage62 multi-window page-granular dry-run contract status/masks/checksum
- Stage62 XNU pmap/bootstrap allocation contract status
- Stage62 TTBR round-trip status
- Stage62 restored TTBR/control-register status
- Stage62 cache-preservation status
- Stage62 public-XNU compile graph status
- Stage62 public-XNU object-subset status
- Stage62 controlled host-only ARM ELF link-proof status
- Stage62 multi-window local L2-bank base/limit and descriptor windows
- Stage62 negative safety counters for public pmap execution, proposed workspace writes, live table install, control-register writes, TLB invalidations, cache changes, generated Mach-O execution, XNU entry execution, and persistent writes

## Public VM/pmap reference-only inputs

Stage62 keeps public ARM VM/pmap files reference-only:

```text
external/xnu-4570.1.46/osfmk/arm/arm_vm_init.c
external/xnu-4570.1.46/osfmk/arm/pmap.c
external/xnu-4570.1.46/osfmk/arm/pmap.h
external/xnu-4570.1.46/osfmk/arm/proc_reg.h
external/xnu-4570.1.46/osfmk/vm/pmap.h
external/xnu-4570.1.46/osfmk/mach/arm/vm_param.h
```

These files are not compiled into the object subset, not linked into the proof ELF, and not executed on target hardware.

The allowed public object subset remains stable:

```text
external/xnu-upstream/pexpert/gen/device_tree.c
external/xnu-upstream/pexpert/gen/bootargs.c
external/xnu-upstream/pexpert/gen/pe_gen.c
external/xnu-4570.1.46/pexpert/arm/pe_bootargs.c
external/xnu-4570.1.46/pexpert/arm/pe_consistent_debug.c
stage62/xnu_object_shims.c
```

## Transition dry-run contract markers

Confirmed target-side contract markers:

```text
stage62_xnu_pmap_transition_dryrun_contract_status=0x62000001
stage62_xnu_pmap_transition_dryrun_contract_required_mask=0x00ffffff
stage62_xnu_pmap_transition_dryrun_contract_satisfied_mask=0x00ffffff
stage62_xnu_pmap_transition_dryrun_contract_failure_mask=0x00000000
stage62_xnu_pmap_transition_dryrun_contract_checksum=0x9baa0bd1
loader_xnu_pmap_transition_dryrun_contract_status_rollup=0x62000001
```

Source prerequisite imports:

```text
stage62_xnu_pmap_transition_dryrun_source_multiwindow_status=0x62000001
stage62_xnu_pmap_transition_dryrun_source_multiwindow_checksum=0x263daa78
stage62_xnu_pmap_transition_dryrun_source_pmap_bootstrap_status=0x62000001
stage62_xnu_pmap_transition_dryrun_source_ttbr_roundtrip_status=0x62000001
stage62_xnu_pmap_transition_dryrun_l1_entry_count=0x00001000
stage62_xnu_pmap_transition_dryrun_l1_bytes=0x00004000
stage62_xnu_pmap_transition_dryrun_l2_bank_base=0x00099000
stage62_xnu_pmap_transition_dryrun_l2_bank_limit=0x0009c000
```

Candidate L1 markers:

```text
stage62_xnu_pmap_transition_dryrun_candidate_l1_base=0x000a4000
stage62_xnu_pmap_transition_dryrun_candidate_l1_limit=0x000a8000
stage62_xnu_pmap_transition_dryrun_candidate_l1_zero_checksum=0xd4b3b000
stage62_xnu_pmap_transition_dryrun_candidate_l1_populated_checksum=0xd560300c
stage62_xnu_pmap_transition_dryrun_candidate_l1_write_count=0x0000000c
stage62_xnu_pmap_transition_dryrun_candidate_kernel_l1_first_index=0x00000800
stage62_xnu_pmap_transition_dryrun_candidate_kernel_l1_last_index=0x00000803
stage62_xnu_pmap_transition_dryrun_candidate_ram_console_l1_first_index=0x00000de4
stage62_xnu_pmap_transition_dryrun_candidate_ram_console_l1_last_index=0x00000de7
stage62_xnu_pmap_transition_dryrun_candidate_device_l1_first_index=0x00000c00
stage62_xnu_pmap_transition_dryrun_candidate_device_l1_last_index=0x00000c03
stage62_xnu_pmap_transition_dryrun_candidate_l1_descriptor_kernel_first_word=0x00099001
stage62_xnu_pmap_transition_dryrun_candidate_l1_descriptor_ram_console_first_word=0x0009a001
stage62_xnu_pmap_transition_dryrun_candidate_l1_descriptor_device_first_word=0x0009b001
stage62_xnu_pmap_transition_dryrun_candidate_l1_descriptor_type_mask_seen=0x00000001
stage62_xnu_pmap_transition_dryrun_candidate_l1_descriptor_attr_mask_seen=0x00000001
```

Read-only proposed control-register plan and recovery facts:

```text
stage62_xnu_pmap_transition_dryrun_proposed_ttbr0=0x000a4000
stage62_xnu_pmap_transition_dryrun_proposed_ttbcr=0x00000000
stage62_xnu_pmap_transition_dryrun_proposed_dacr=0x00000003
stage62_xnu_pmap_transition_dryrun_proposed_sctlr=0x00c5487b
stage62_xnu_pmap_transition_dryrun_proposed_ttbr0_alignment=0x00000000
stage62_xnu_pmap_transition_dryrun_proposed_sctlr_cache_bits=0x00000000
stage62_xnu_pmap_transition_dryrun_restored_ttbr0=0x0007c000
stage62_xnu_pmap_transition_dryrun_restored_ttbcr=0x00000000
stage62_xnu_pmap_transition_dryrun_restored_dacr=0x00000003
stage62_xnu_pmap_transition_dryrun_restored_sctlr=0x00c5487b
stage62_xnu_pmap_transition_dryrun_restored_cache_bits=0x00000000
stage62_xnu_pmap_transition_dryrun_recovery_l1_base=0x00078000
stage62_xnu_pmap_transition_dryrun_recovery_l1_limit=0x0007c000
stage62_xnu_pmap_transition_dryrun_recovery_l1_distinct=0x00000001
stage62_xnu_pmap_transition_dryrun_workspace_base=0x80000000
stage62_xnu_pmap_transition_dryrun_workspace_limit=0x80100000
stage62_xnu_pmap_transition_dryrun_workspace_l1_table_phys=0x0007c000
stage62_xnu_pmap_transition_dryrun_candidate_workspace_distinct=0x00000001
```

Software translation markers through the candidate L1:

```text
stage62_xnu_pmap_transition_dryrun_translation_kernel_va=0x80008000
stage62_xnu_pmap_transition_dryrun_translation_kernel_pa=0x80008000
stage62_xnu_pmap_transition_dryrun_translation_workspace_va=0x80000000
stage62_xnu_pmap_transition_dryrun_translation_workspace_pa=0x80000000
stage62_xnu_pmap_transition_dryrun_translation_ram_console_va=0xde500000
stage62_xnu_pmap_transition_dryrun_translation_ram_console_pa=0xde500000
stage62_xnu_pmap_transition_dryrun_translation_device_va=0xc0000000
stage62_xnu_pmap_transition_dryrun_translation_device_pa=0xf9000000
stage62_xnu_pmap_transition_dryrun_translation_case_count=0x00000004
```

Safety markers:

```text
stage62_xnu_pmap_transition_dryrun_public_pmap_compile_count=0x00000000
stage62_xnu_pmap_transition_dryrun_public_pmap_link_count=0x00000000
stage62_xnu_pmap_transition_dryrun_public_pmap_execute_count=0x00000000
stage62_xnu_pmap_transition_dryrun_public_arm_vm_init_executed=0x00000000
stage62_xnu_pmap_transition_dryrun_public_pmap_runtime_executed=0x00000000
stage62_xnu_pmap_transition_dryrun_proposed_workspace_written=0x00000000
stage62_xnu_pmap_transition_dryrun_live_pmap_tables_installed=0x00000000
stage62_xnu_pmap_transition_dryrun_ttbr_written=0x00000000
stage62_xnu_pmap_transition_dryrun_ttbcr_written=0x00000000
stage62_xnu_pmap_transition_dryrun_dacr_written=0x00000000
stage62_xnu_pmap_transition_dryrun_sctlr_written=0x00000000
stage62_xnu_pmap_transition_dryrun_tlbs_invalidated=0x00000000
stage62_xnu_pmap_transition_dryrun_caches_changed=0x00000000
stage62_xnu_pmap_transition_dryrun_persistent_write_attempted=0x00000000
stage62_xnu_pmap_transition_dryrun_xnu_start_executed=0x00000000
stage62_xnu_pmap_transition_dryrun_generated_macho_executed=0x00000000
stage62_xnu_pmap_transition_dryrun_local_only=0x00000001
stage62_xnu_pmap_transition_dryrun_candidate_not_installed=0x00000001
stage62_xnu_pmap_transition_dryrun_control_register_plan_readonly=0x00000001
stage62_xnu_pmap_transition_dryrun_tlb_plan_readonly=0x00000001
stage62_xnu_pmap_transition_dryrun_fail_closed=0x00000001
loader_satisfied_mask=0xffffffff
loader_status=0x62000001
```

## Local validation

Local build passed:

```bash
/mnt/data/mi4-ios6/stage62/build.sh
```

Final local image hashes:

```text
7fada93c4560d0b9aa03922f13180b0b7b7c83d85f668e31bb96d212833eeb92  out/stage62/stage62_fixture.macho
945637b97236c9436109e48323c8ba27cddf1772a604a7e4df62e67ac8841ff3  out/stage62/stage62.elf
a1dc0277873f777e87b1abc40a921006b408c4285eae64340535ba4df71f406e  out/stage62/stage62.bin
c6e3a5be360de684fe007fec7f2e4ba622d1e4ce3d21c6541924e707859a8b5f  out/stage62/stage62.img
6625f35e2f2740d9f5525ee26be8ba3d3989cb0e2a36408debf2cd4e02126327  out/stage62/stage62-qcdt.img
```

Size summary:

```text
text=249616 data=0 bss=397632 dec=647248 hex=9e050
```

Boot-image parse passed for both images. QCDT image highlights:

```text
magic=ANDROID!
page_size=2048
kernel_size=249616 (0x3cf10)
kernel_addr=32768 (0x8000)
ramdisk_size=0 (0x0)
ramdisk_addr=33554432 (0x2000000)
second_size=0 (0x0)
second_addr=15728640 (0xf00000)
tags_addr=31457280 (0x1e00000)
dt_size=2521088 (0x267800)
cmdline=stage62 mi4ios6=stage62 ... xnu-pmap-multiwindow-dryrun-contract xnu-pmap-transition-dryrun-contract ... pmap-multiwindow-local-dryrun-only pmap-transition-local-dryrun-only ... no-proposed-pmap-write no-live-pmap-install no-tlb-invalidate no-cache-policy-change no-persist-write no-external-mutation
part=kernel offset=0x800 size=249616 sha256=a1dc0277873f777e87b1abc40a921006b408c4285eae64340535ba4df71f406e
part=dt.img offset=0x3d800 size=2521088 sha256=c8faf487909b668fcebddfb2dcbd793bfb94d0411286c1f5c588bc0015120efe
```

Undefined-symbol checks passed:

```bash
arm-none-eabi-nm -u /mnt/data/mi4-ios6/out/stage62/stage62.elf
arm-none-eabi-nm -u /mnt/data/mi4-ios6/out/stage62/xnu-link/stage62-xnu-link.elf
```

Both commands produced no undefined symbols.

Static stale-reference scan initially found stale Stage61 Mach-O fixture marker prefixes in `stage62/macho_probe.c`; those were corrected to the Stage62 fixture prefixes (`ST62-TEXT`, `ST62-DATA`, and `ST62-PRELINK-TEXT`). The final stale-marker scan is clean for stale Stage61 code markers outside generated or historical context (`stale_marker_violations=0`). `external/xnu-upstream` and `external/xnu-4570.1.46` remain ignored/untouched, and `out/stage62/` remains ignored.

Fixed public ARM boot-args command lines remain under the 256-byte `CommandLine` field:

```text
stage62/boot_args.c 215/256 including NUL
stage62/stage62_main.c 215/256 including NUL
stage62/xnu_object_shims.c 198/256 including NUL
```

## Hardware validation

Stage62 hardware validation completed with non-persistent boot only:

```bash
sudo adb -s 4a2fe00b reboot bootloader
sudo fastboot -s 4a2fe00b boot /mnt/data/mi4-ios6/out/stage62/stage62-qcdt.img
sudo adb -s 4a2fe00b exec-out 'cat /proc/last_kmsg' > /tmp/cancro-stage62-last_kmsg.txt
```

Recovered log facts:

```text
stage62_last_kmsg_bytes=175285 (0x0002acb5)
stage62_marker_count=2509 (0x000009cd)
stage62_xnu_marker_count=2483 (0x000009b3)
stage62_hardware_validation=ok
```

Confirmed hardware markers after non-persistent boot:

```text
MI4IOS6_STAGE62_XNU stage62_xnu_compile_graph_status=0x62000001
MI4IOS6_STAGE62_XNU stage62_xnu_compile_graph_pmap_public_compile_count=0x00000000
MI4IOS6_STAGE62_XNU stage62_xnu_compile_graph_pmap_public_link_count=0x00000000
MI4IOS6_STAGE62_XNU stage62_xnu_object_subset_status=0x62000001
MI4IOS6_STAGE62_XNU stage62_xnu_link_status=0x62000001
MI4IOS6_STAGE62_XNU stage62_xnu_bootstrap_contract_status=0x62000001
MI4IOS6_STAGE62_XNU stage62_xnu_pmap_bootstrap_contract_status=0x62000001
MI4IOS6_STAGE62_XNU stage62_xnu_pmap_table_dryrun_contract_status=0x62000001
MI4IOS6_STAGE62_XNU stage62_xnu_pmap_page_dryrun_contract_status=0x62000001
MI4IOS6_STAGE62_XNU stage62_xnu_pmap_attr_dryrun_contract_status=0x62000001
MI4IOS6_STAGE62_XNU stage62_xnu_pmap_multiwindow_dryrun_contract_status=0x62000001
MI4IOS6_STAGE62_XNU stage62_xnu_pmap_transition_dryrun_contract_status=0x62000001
MI4IOS6_STAGE62_XNU stage62_xnu_pmap_transition_dryrun_contract_required_mask=0x00ffffff
MI4IOS6_STAGE62_XNU stage62_xnu_pmap_transition_dryrun_contract_satisfied_mask=0x00ffffff
MI4IOS6_STAGE62_XNU stage62_xnu_pmap_transition_dryrun_contract_failure_mask=0x00000000
MI4IOS6_STAGE62_XNU stage62_xnu_pmap_transition_dryrun_contract_checksum=0x9baa0bd1
MI4IOS6_STAGE62_XNU loader_xnu_pmap_transition_dryrun_contract_status_rollup=0x62000001
MI4IOS6_STAGE62_XNU loader_satisfied_mask=0xffffffff
MI4IOS6_STAGE62_XNU loader_status=0x62000001
MI4IOS6_STAGE62_XNU kernel_entry ok
MI4IOS6_STAGE62 kernel_entry returned success
```

The device returned to Android after the PS_HOLD reset path. No persistent flash/erase/write was performed.

## Result

Stage62 completes the safe live-table transition prerequisite dry-run blocker. The new contract proves that the Stage-owned model can mirror the inherited multi-window descriptor plan into a separate candidate L1, keep representative pmap translations coherent, record future TTBR0/TTBCR/DACR/SCTLR planning facts, and preserve recovery continuity while keeping all proposed table/control operations read-only.

The next safe direction is still-local MSM8974 pexpert/timer/interrupt hook readiness, IOKit/platform-driver scaffolding, broader high-virtual pmap behavior, or another bounded public-XNU proof while continuing to avoid a full public `mach_kernel` build or any XNU runtime handoff.
