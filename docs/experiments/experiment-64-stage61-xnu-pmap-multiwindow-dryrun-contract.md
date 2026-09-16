# Experiment 64 — Stage61 XNU pmap multi-window page-granular dry-run contract

Date: 2026-06-08

## Goal

Stage61 advances the cancro/MSM8974 XNU bring-up path by adding a Stage-owned XNU pmap multi-window page-granular dry-run contract on top of the Stage56 bootstrap mapping contract, Stage57 pmap/bootstrap allocation contract, Stage58 pmap section-table dry-run contract, Stage59 pmap page-granular dry-run contract, and Stage60 pmap cache/MMU attribute dry-run contract.

Stage60 proved exact public ARM XNU cache/AP/WIMG/PTE/TTE attribute arithmetic in Stage-owned code. Stage61 keeps that prerequisite stable and adds the next pmap blocker: prove that the exact inherited PTE templates can be applied across several independent local coarse/L2 windows without touching proposed XNU/pmap workspace memory or installing live tables.

The Stage61 contract models three 4 MiB ARMv7 short-descriptor coarse/L2 windows:

1. a kernel/workspace RAM window,
2. a RAM-console window,
3. a device/GIC-style window.

## Safety boundary

Stage61 still does not run XNU or iOS. It does not build a full public `mach_kernel`, does not execute public-XNU object code, does not execute public ARM pexpert/platform runtime code, does not execute public ARM VM/pmap runtime code, does not execute the generated Mach-O fixture, does not jump to XNU `_start` / `arm_init`, does not write proposed physical load addresses, does not write proposed TTE/pmap workspace addresses, does not install live proposed XNU/pmap tables, does not write TTBR/TTBCR/DACR/SCTLR for proposed pmap tables, does not invalidate TLBs for proposed pmap tables, does not change cache policy, does not mutate external public checkouts, and does not write persistent storage.

Hardware validation remains non-persistent `fastboot boot` only.

## Implementation

Stage61 adds:

- `stage61/xnu_pmap_multiwindow_dryrun_contract.c`
- new pmap multi-window dry-run ABI in `stage61/stage61.h`
- a new loader satisfied bit:
  - `STAGE61_LOADER_SAT_XNU_PMAP_MULTIWINDOW_DRYRUN_CONTRACT`
- new loader roll-up fields:
  - `xnu_pmap_multiwindow_dryrun_contract`
  - `xnu_pmap_multiwindow_dryrun_contract_status`
  - `xnu_pmap_multiwindow_dryrun_contract_satisfied_mask`
  - `xnu_pmap_multiwindow_dryrun_contract_failure_mask`
  - `xnu_pmap_multiwindow_dryrun_contract_checksum`
  - `xnu_pmap_multiwindow_dryrun_contract_status_rollup`
- build source inclusion for `xnu_pmap_multiwindow_dryrun_contract.c`
- boot-image command-line markers:
  - `xnu-pmap-multiwindow-dryrun-contract`
  - `pmap-multiwindow-local-dryrun-only`
- fixed-size public ARM boot-args markers for the multi-window stage:
  - `stage61/boot_args.c`
  - `stage61/stage61_main.c`
  - `stage61/xnu_object_shims.c`

The contract imports only Stage-owned/generated facts:

- Stage61 pmap section-table dry-run contract status/masks/checksum
- Stage61 pmap page-granular dry-run contract status/masks/checksum
- Stage61 pmap cache/MMU attribute dry-run contract status/masks/checksum
- Stage61 XNU bootstrap mapping contract status
- Stage61 XNU pmap/bootstrap allocation contract status
- Stage61 pmap/bootstrap snapshot status
- Stage61 cache-preservation status
- Stage61 public-XNU compile graph status
- Stage61 public-XNU object-subset status
- Stage61 controlled host-only ARM ELF link-proof status
- Stage61 TTBR0 round-trip negative safety counters

## Public VM/pmap reference-only inputs

Stage61 keeps public ARM VM/pmap files reference-only:

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
stage61/xnu_object_shims.c
```

## Multi-window dry-run contract markers

Confirmed target-side contract markers:

```text
stage61_xnu_pmap_multiwindow_dryrun_contract_status=0x61000001
stage61_xnu_pmap_multiwindow_dryrun_contract_required_mask=0x1fffffff
stage61_xnu_pmap_multiwindow_dryrun_contract_satisfied_mask=0x1fffffff
stage61_xnu_pmap_multiwindow_dryrun_contract_failure_mask=0x00000000
stage61_xnu_pmap_multiwindow_dryrun_contract_checksum=0x252caa78
loader_xnu_pmap_multiwindow_dryrun_contract_status=0x61000001
loader_xnu_pmap_multiwindow_dryrun_contract_satisfied_mask=0x1fffffff
loader_xnu_pmap_multiwindow_dryrun_contract_failure_mask=0x00000000
loader_xnu_pmap_multiwindow_dryrun_contract_checksum=0x252caa78
loader_xnu_pmap_multiwindow_dryrun_contract_status_rollup=0x61000001
```

Constants and imported exact Stage60 templates:

```text
stage61_xnu_pmap_multiwindow_dryrun_window_count=0x00000003
stage61_xnu_pmap_multiwindow_dryrun_window_size=0x00400000
stage61_xnu_pmap_multiwindow_dryrun_l1_entry_count=0x00001000
stage61_xnu_pmap_multiwindow_dryrun_l2_pte_count=0x00000400
stage61_xnu_pmap_multiwindow_dryrun_pte_template_kernel=0x00000412
stage61_xnu_pmap_multiwindow_dryrun_pte_template_workspace=0x00000412
stage61_xnu_pmap_multiwindow_dryrun_pte_template_ram_console=0x00000413
stage61_xnu_pmap_multiwindow_dryrun_pte_template_device=0x0000001f
stage61_xnu_pmap_multiwindow_dryrun_pte_expected_attr_mask=0x0000041f
```

Local-only buffer markers:

```text
stage61_xnu_pmap_multiwindow_dryrun_local_l1_base=0x00098000
stage61_xnu_pmap_multiwindow_dryrun_local_l1_limit=0x0009c000
stage61_xnu_pmap_multiwindow_dryrun_local_l2_bank_base=0x00095000
stage61_xnu_pmap_multiwindow_dryrun_local_l2_bank_limit=0x00098000
stage61_xnu_pmap_multiwindow_dryrun_local_l1_zero_checksum=0x409e4000
stage61_xnu_pmap_multiwindow_dryrun_local_l2_zero_checksum=0x0b66f000
stage61_xnu_pmap_multiwindow_dryrun_local_l1_populated_checksum=0x416cb804
stage61_xnu_pmap_multiwindow_dryrun_local_l2_populated_checksum=0x2f686000
stage61_xnu_pmap_multiwindow_dryrun_local_l1_write_count=0x0000000c
stage61_xnu_pmap_multiwindow_dryrun_local_l2_pte_write_count=0x00000c00
```

Window markers:

```text
stage61_xnu_pmap_multiwindow_dryrun_workspace_base=0x80000000
stage61_xnu_pmap_multiwindow_dryrun_workspace_limit=0x80100000
stage61_xnu_pmap_multiwindow_dryrun_kernel_window_virt_base=0x80000000
stage61_xnu_pmap_multiwindow_dryrun_kernel_window_phys_base=0x80000000
stage61_xnu_pmap_multiwindow_dryrun_kernel_window_l1_first_index=0x00000800
stage61_xnu_pmap_multiwindow_dryrun_kernel_window_l1_last_index=0x00000803
stage61_xnu_pmap_multiwindow_dryrun_ram_console_window_virt_base=0xde400000
stage61_xnu_pmap_multiwindow_dryrun_ram_console_window_phys_base=0xde400000
stage61_xnu_pmap_multiwindow_dryrun_ram_console_window_l1_first_index=0x00000de4
stage61_xnu_pmap_multiwindow_dryrun_ram_console_window_l1_last_index=0x00000de7
stage61_xnu_pmap_multiwindow_dryrun_device_window_virt_base=0xc0000000
stage61_xnu_pmap_multiwindow_dryrun_device_window_phys_base=0xf9000000
stage61_xnu_pmap_multiwindow_dryrun_device_window_l1_first_index=0x00000c00
stage61_xnu_pmap_multiwindow_dryrun_device_window_l1_last_index=0x00000c03
```

Descriptor/PTE readback markers:

```text
stage61_xnu_pmap_multiwindow_dryrun_l1_descriptor_kernel_first_word=0x00095001
stage61_xnu_pmap_multiwindow_dryrun_l1_descriptor_ram_console_first_word=0x00096001
stage61_xnu_pmap_multiwindow_dryrun_l1_descriptor_device_first_word=0x00097001
stage61_xnu_pmap_multiwindow_dryrun_l1_descriptor_type_mask_seen=0x00000001
stage61_xnu_pmap_multiwindow_dryrun_l1_descriptor_attr_mask_seen=0x00000001
stage61_xnu_pmap_multiwindow_dryrun_pte_kernel_first_word=0x80000412
stage61_xnu_pmap_multiwindow_dryrun_pte_ram_console_first_word=0xde400413
stage61_xnu_pmap_multiwindow_dryrun_pte_device_first_word=0xf900001f
stage61_xnu_pmap_multiwindow_dryrun_pte_type_mask_seen=0x00000002
stage61_xnu_pmap_multiwindow_dryrun_pte_attr_mask_seen=0x0000041f
```

Software translation markers:

```text
stage61_xnu_pmap_multiwindow_dryrun_translation_kernel_va=0x80008000
stage61_xnu_pmap_multiwindow_dryrun_translation_kernel_pa=0x80008000
stage61_xnu_pmap_multiwindow_dryrun_translation_workspace_va=0x80000000
stage61_xnu_pmap_multiwindow_dryrun_translation_workspace_pa=0x80000000
stage61_xnu_pmap_multiwindow_dryrun_translation_ram_console_va=0xde500000
stage61_xnu_pmap_multiwindow_dryrun_translation_ram_console_pa=0xde500000
stage61_xnu_pmap_multiwindow_dryrun_translation_device_va=0xc0000000
stage61_xnu_pmap_multiwindow_dryrun_translation_device_pa=0xf9000000
stage61_xnu_pmap_multiwindow_dryrun_translation_case_count=0x00000004
```

Safety markers:

```text
stage61_xnu_pmap_multiwindow_dryrun_public_pmap_compile_count=0x00000000
stage61_xnu_pmap_multiwindow_dryrun_public_pmap_link_count=0x00000000
stage61_xnu_pmap_multiwindow_dryrun_public_pmap_execute_count=0x00000000
stage61_xnu_pmap_multiwindow_dryrun_public_arm_vm_init_executed=0x00000000
stage61_xnu_pmap_multiwindow_dryrun_public_pmap_runtime_executed=0x00000000
stage61_xnu_pmap_multiwindow_dryrun_proposed_workspace_written=0x00000000
stage61_xnu_pmap_multiwindow_dryrun_live_pmap_tables_installed=0x00000000
stage61_xnu_pmap_multiwindow_dryrun_ttbr_written=0x00000000
stage61_xnu_pmap_multiwindow_dryrun_ttbcr_written=0x00000000
stage61_xnu_pmap_multiwindow_dryrun_dacr_written=0x00000000
stage61_xnu_pmap_multiwindow_dryrun_sctlr_written=0x00000000
stage61_xnu_pmap_multiwindow_dryrun_tlbs_invalidated=0x00000000
stage61_xnu_pmap_multiwindow_dryrun_caches_changed=0x00000000
stage61_xnu_pmap_multiwindow_dryrun_persistent_write_attempted=0x00000000
stage61_xnu_pmap_multiwindow_dryrun_xnu_start_executed=0x00000000
stage61_xnu_pmap_multiwindow_dryrun_generated_macho_executed=0x00000000
stage61_xnu_pmap_multiwindow_dryrun_local_only=0x00000001
stage61_xnu_pmap_multiwindow_dryrun_fail_closed=0x00000001
loader_status=0x61000001
```

## Local validation

Local build passed:

```bash
/mnt/data/mi4-ios6/stage61/build.sh
```

Final local image hashes:

```text
5967348354de481c52c8963879c9b7a08b22f39cb2d8c89d1fc7517e97eb9043  out/stage61/stage61_fixture.macho
51fe6dead0174739d306e79fc9d526fe4c2b2b3eade03fb450a2bfa70a54476e  out/stage61/stage61.elf
55cf56ca1c6cbed6e7c70bd36acb789ce9c83d98618d0643c461f193b4fa8d73  out/stage61/stage61.bin
eacd2a9bf5107e46c4bfc5e1d3b50dffabbbeb6cb33dd31c16b0f838c9b3d614  out/stage61/stage61.img
304b0f9fb60c37841f77bf6a69f553f21bb22ac3f5a74dc6b62ccd9936326839  out/stage61/stage61-qcdt.img
```

Size summary:

```text
text=239368 data=0 bss=364864 dec=604232 hex=93848
```

Boot-image parse passed for both images. QCDT image highlights:

```text
magic=ANDROID!
page_size=2048
kernel_size=239368 (0x3a708)
kernel_addr=32768 (0x8000)
ramdisk_size=0 (0x0)
ramdisk_addr=33554432 (0x2000000)
second_size=0 (0x0)
second_addr=15728640 (0xf00000)
tags_addr=31457280 (0x1e00000)
dt_size=2521088 (0x267800)
cmdline=stage61 mi4ios6=stage61 ... xnu-pmap-page-dryrun-contract xnu-pmap-attr-dryrun-contract xnu-pmap-multiwindow-dryrun-contract pmap-table-local-dryrun-only pmap-page-local-l2-dryrun-only pmap-attr-local-dryrun-only pmap-multiwindow-local-dryrun-only ... no-proposed-pmap-write no-live-pmap-install no-tlb-invalidate no-cache-policy-change no-persist-write no-external-mutation
part=kernel offset=0x800 size=239368 sha256=55cf56ca1c6cbed6e7c70bd36acb789ce9c83d98618d0643c461f193b4fa8d73
part=dt.img offset=0x3b000 size=2521088 sha256=c8faf487909b668fcebddfb2dcbd793bfb94d0411286c1f5c588bc0015120efe
```

Undefined-symbol checks passed:

```bash
arm-none-eabi-nm -u /mnt/data/mi4-ios6/out/stage61/stage61.elf
arm-none-eabi-nm -u /mnt/data/mi4-ios6/out/stage61/xnu-link/stage61-xnu-link.elf
```

Both commands produced no undefined symbols.

Static stale-reference scan under `stage61/` is clean for stale Stage60 code markers outside generated or historical context (`stale_marker_violations=0`). `external/xnu-upstream` and `external/xnu-4570.1.46` remain ignored/untouched, and `out/stage61/` remains ignored.

Fixed public ARM boot-args command lines remain under the 256-byte `CommandLine` field:

```text
stage61/boot_args.c 192/256 including NUL
stage61/stage61_main.c 192/256 including NUL
stage61/xnu_object_shims.c 175/256 including NUL
```

## Hardware validation

Stage61 hardware validation completed with non-persistent boot only:

```bash
sudo adb -s 4a2fe00b reboot bootloader
sudo fastboot -s 4a2fe00b boot /mnt/data/mi4-ios6/out/stage61/stage61-qcdt.img
sudo adb -s 4a2fe00b exec-out 'cat /proc/last_kmsg' > /tmp/cancro-stage61-last_kmsg.txt
```

Recovered log facts:

```text
stage61_last_kmsg_bytes=159522 (0x00026f22)
stage61_marker_count=2333 (0x0000091d)
stage61_xnu_marker_count=2307 (0x00000903)
stage61_hardware_validation=ok
```

Confirmed hardware markers after non-persistent boot:

```text
MI4IOS6_STAGE61_XNU stage61_xnu_compile_graph_status=0x61000001
MI4IOS6_STAGE61_XNU stage61_xnu_compile_graph_pmap_public_compile_count=0x00000000
MI4IOS6_STAGE61_XNU stage61_xnu_compile_graph_pmap_public_link_count=0x00000000
MI4IOS6_STAGE61_XNU stage61_xnu_object_subset_status=0x61000001
MI4IOS6_STAGE61_XNU stage61_xnu_link_status=0x61000001
MI4IOS6_STAGE61_XNU stage61_xnu_bootstrap_contract_status=0x61000001
MI4IOS6_STAGE61_XNU stage61_xnu_pmap_bootstrap_contract_status=0x61000001
MI4IOS6_STAGE61_XNU stage61_xnu_pmap_table_dryrun_contract_status=0x61000001
MI4IOS6_STAGE61_XNU stage61_xnu_pmap_page_dryrun_contract_status=0x61000001
MI4IOS6_STAGE61_XNU stage61_xnu_pmap_attr_dryrun_contract_status=0x61000001
MI4IOS6_STAGE61_XNU stage61_xnu_pmap_multiwindow_dryrun_contract_status=0x61000001
MI4IOS6_STAGE61_XNU stage61_xnu_pmap_multiwindow_dryrun_contract_required_mask=0x1fffffff
MI4IOS6_STAGE61_XNU stage61_xnu_pmap_multiwindow_dryrun_contract_satisfied_mask=0x1fffffff
MI4IOS6_STAGE61_XNU stage61_xnu_pmap_multiwindow_dryrun_contract_failure_mask=0x00000000
MI4IOS6_STAGE61_XNU loader_xnu_pmap_multiwindow_dryrun_contract_status_rollup=0x61000001
MI4IOS6_STAGE61_XNU loader_satisfied_mask=0x7fffffff
MI4IOS6_STAGE61_XNU loader_status=0x61000001
MI4IOS6_STAGE61_XNU kernel_entry ok
MI4IOS6_STAGE61 kernel_entry returned success
```

The device returned to Android after the PS_HOLD reset path. No persistent flash/erase/write was performed.

## Result

Stage61 completes the broader page-granular multi-window pmap dry-run blocker. The new contract proves that the Stage-owned model can apply exact inherited public ARM XNU PTE templates across separate local coarse/L2 windows for kernel/workspace RAM, RAM-console, and device/GIC-style mappings while preserving all no-execution, no-proposed-write, no-live-table-install, no-control-register-write, no-TLB-invalidate, no-cache-change, and no-persistent-write boundaries.

The next safe direction is a still-local proof for safe live-table transition prerequisites, MSM8974 pexpert/timer/interrupt hooks, IOKit/platform-driver scaffolding, or another bounded public-XNU proof while continuing to avoid a full public `mach_kernel` build or any XNU runtime handoff.
