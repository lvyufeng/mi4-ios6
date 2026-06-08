# Stage62 — XNU pmap safe live-table transition prerequisite dry-run contract

Stage62 keeps the no-XNU-runtime safety envelope, preserves the bounded public pexpert object/link proof, preserves the Stage56 bootstrap mapping contract, preserves the Stage57 pmap/bootstrap allocation contract, preserves the Stage58 local section-table dry-run, preserves the Stage59 page-granular coarse/L2 dry-run, preserves the Stage60 exact pmap cache/MMU attribute dry-run, preserves the Stage61-style multi-window page-granular dry-run, and adds a Stage-owned **XNU pmap safe live-table transition prerequisite dry-run contract** for Xiaomi Mi 4 cancro / MSM8974 ARMv7.

The new proof advances beyond the multi-window local mapping model by mirroring only the already-proved coarse/L2 descriptor plan into a separate Stage-owned candidate L1 buffer and by recording the proposed TTBR0/TTBCR/DACR/SCTLR values that a future pmap transition would need. Stage62 validates candidate table alignment, descriptor import, representative software translations, recovery-table continuity, and read-only control-register planning. It does not install the candidate L1, does not write proposed TTBR/TTBCR/DACR/SCTLR, does not invalidate TLBs for a proposed pmap install, does not change cache policy, does not write proposed pmap workspace memory, and does not execute public VM/pmap runtime code.

## Public object subset

The public object subset remains intentionally stable:

```text
external/xnu-upstream/pexpert/gen/device_tree.c
external/xnu-upstream/pexpert/gen/bootargs.c
external/xnu-upstream/pexpert/gen/pe_gen.c
external/xnu-4570.1.46/pexpert/arm/pe_bootargs.c
external/xnu-4570.1.46/pexpert/arm/pe_consistent_debug.c
stage62/xnu_object_shims.c
```

Public ARM VM/pmap sources remain reference-only. Stage62 still does **not** compile, link, or execute public `arm_vm_init.c` or `pmap.c`.

## Safety model

Stage62 still does **not** run XNU or iOS. It does not build a full public `mach_kernel`, does not execute public-XNU object code, does not execute public ARM pexpert/platform runtime code, does not execute public ARM VM/pmap runtime code, does not execute the generated Mach-O fixture, does not jump to XNU `_start` / `arm_init`, does not write proposed XNU physical load addresses, does not write proposed TTE/pmap workspace addresses, does not install live proposed XNU/pmap tables, does not mutate external public checkouts, does not write persistent storage, and does not enable or change caches.

Retained boundaries:

- Non-persistent `fastboot boot` only for hardware validation.
- No partition flash/erase/write.
- No bootloader or partition changes.
- No parsed Mach-O entrypoint execution.
- No generated Mach-O fixture execution.
- No public-XNU object execution on hardware.
- No public ARM pexpert/platform runtime execution.
- No public ARM `arm_vm_init.c` / `pmap.c` execution.
- No real XNU `_start` / `arm_init` handoff.
- No full public `mach_kernel` build attempt.
- No dependency-heavy ARM bring-up source inclusion (`start.s`, `arm_init.c`, `arm_vm_init.c`, `pmap.c`).
- No mutation of `external/xnu-upstream` or `external/xnu-4570.1.46`.
- No writes to proposed XNU physical load addresses.
- No writes to proposed XNU TTE/pmap workspace physical addresses.
- No use of the proposed XNU TTE/pmap workspace as a live TTBR table.
- No live proposed XNU/pmap table install.
- No TTBR/TTBCR/DACR/SCTLR writes for proposed pmap install.
- No TLB invalidation for proposed pmap install.
- Controlled TTBR0 round-trip remains Stage-owned and restored.
- Original TTBR0/TTBCR/DACR/SCTLR state remains restored after inherited selftests.
- Caches remain disabled/unchanged.
- `ram_console`, PS_HOLD, GIC, timer, and abort handlers remain available.
- Public-XNU graph/object/link outputs stay ignored under `out/stage62/`.
- Public source checkouts stay ignored under `external/`.

The Stage62 completion message is intentionally explicit about the new boundary:

```text
safe live-table transition prerequisite dry-run contracts proved; no control-register writes for proposed pmap
```

## What Stage62 adds

Stage62 keeps the multi-window page-granular dry-run stable and adds:

- a standalone `stage62/` payload with `0x62000001` success status and `MI4IOS6_STAGE62` logs,
- command-line markers for `xnu-pmap-transition-dryrun-contract` and `pmap-transition-local-dryrun-only`,
- a Stage-owned `struct stage62_xnu_pmap_transition_dryrun_contract` ABI in `stage62.h`,
- `stage62/xnu_pmap_transition_dryrun_contract.c`,
- a loader satisfied bit `STAGE62_LOADER_SAT_XNU_PMAP_TRANSITION_DRYRUN_CONTRACT`,
- loader preflight roll-up fields for `xnu_pmap_transition_dryrun_contract`,
- a separate local 16 KiB candidate L1 buffer distinct from the recovery L1, the proposed workspace L1, and the inherited multi-window L1,
- descriptor import from the inherited three-window local L2-bank dry-run tables,
- proposed TTBR0/TTBCR/DACR/SCTLR planning facts for a future pmap transition,
- recovery continuity checks against the restored Stage-owned TTBR state,
- kernel, workspace, RAM-console, and device/GIC-style software translation checks through the candidate L1 plus inherited local L2-bank,
- target-side proof that public pmap compile/link/execute counts remain zero,
- target-side proof that no proposed workspace was written and no live pmap table was installed,
- target-side proof that TTBR/TTBCR/DACR/SCTLR write counts, TLB invalidation counts, persistent-write counts, and cache-change counts remain zero.

## Selected public XNU baselines

Stage62 expects:

```text
external/xnu-upstream      xnu-2050.22.13  cc8a9b0c  MasterVersion 12.3.0
external/xnu-4570.1.46     xnu-4570.1.46   76e12aa   later public ARM reference only
```

The public 2050 tree remains the Darwin 12 / iOS 6-era context. The 4570 tree is used only as a public ARM implementation reference for bounded compile/link proof objects and for blocked/reference-only source classification.

## Public VM/pmap reference-only surface

Stage62 reads/models these later-public ARM files only as public reference material:

```text
external/xnu-4570.1.46/osfmk/arm/arm_vm_init.c
external/xnu-4570.1.46/osfmk/arm/pmap.c
external/xnu-4570.1.46/osfmk/arm/pmap.h
external/xnu-4570.1.46/osfmk/arm/proc_reg.h
external/xnu-4570.1.46/osfmk/vm/pmap.h
external/xnu-4570.1.46/osfmk/mach/arm/vm_param.h
```

They are not compiled into the public object subset, not linked into the proof ELF, and not executed on target hardware.

## Compile graph, object subset, and link proof

`stage62/xnu_compile_graph_scan.py` still classifies twenty candidates and allows only the five bounded public pexpert sources. ARM VM/pmap sources remain reference-only or blocked runtime inputs.

Successful graph/object/link markers from local and target validation:

```text
stage62_xnu_compile_graph_status=0x62000001
stage62_xnu_compile_graph_required_mask=0x1fffffff
stage62_xnu_compile_graph_satisfied_mask=0x1fffffff
stage62_xnu_compile_graph_failure_mask=0x00000000
stage62_xnu_compile_graph_candidate_count=0x00000014
stage62_xnu_compile_graph_allowed_compile_count=0x00000005
stage62_xnu_compile_graph_allowed_link_count=0x00000005
stage62_xnu_compile_graph_forbidden_count=0x0000000f
stage62_xnu_compile_graph_pmap_reference_mask=0x0000001f
stage62_xnu_compile_graph_pmap_runtime_blocked_mask=0x00000003
stage62_xnu_compile_graph_pmap_reference_count=0x00000005
stage62_xnu_compile_graph_pmap_public_compile_count=0x00000000
stage62_xnu_compile_graph_pmap_public_link_count=0x00000000
stage62_xnu_compile_graph_pmap_reference_only=0x00000001
stage62_xnu_object_subset_status=0x62000001
stage62_xnu_object_count=0x00000006
stage62_xnu_object_duplicate_symbol_count=0x00000000
stage62_xnu_link_status=0x62000001
stage62_xnu_link_object_count=0x00000006
stage62_xnu_link_undefined_symbol_count=0x00000000
```

`arm-none-eabi-nm -u out/stage62/stage62.elf` and `arm-none-eabi-nm -u out/stage62/xnu-link/stage62-xnu-link.elf` both report no undefined symbols.

## Prerequisite pmap contracts

Stage62 retains all prior XNU mapping/pmap prerequisites as fail-closed source facts:

```text
stage62_xnu_bootstrap_contract_status=0x62000001
stage62_xnu_pmap_bootstrap_contract_status=0x62000001
stage62_xnu_pmap_table_dryrun_contract_status=0x62000001
stage62_xnu_pmap_page_dryrun_contract_status=0x62000001
stage62_xnu_pmap_attr_dryrun_contract_status=0x62000001
stage62_xnu_pmap_multiwindow_dryrun_contract_status=0x62000001
```

The inherited pmap/bootstrap arithmetic remains:

```text
stage62_xnu_pmap_gVirtBase=0x80008000
stage62_xnu_pmap_gPhysBase=0x80000000
stage62_xnu_pmap_gPhysSize=0x5e500000
stage62_xnu_pmap_boot_ttep=0x8000c000
stage62_xnu_pmap_cpu_ttep=0x80010000
stage62_xnu_pmap_initial_avail_start=0x80016000
stage62_xnu_pmap_avail_end=0xde500000
stage62_xnu_pmap_vstart=0xc0400000
stage62_xnu_pmap_virtual_space_end=0xfffeffff
stage62_xnu_pmap_workspace_base=0x80000000
stage62_xnu_pmap_workspace_limit=0x80100000
stage62_xnu_pmap_workspace_l1_table_phys=0x0007c000
stage62_xnu_pmap_workspace_l1_table_virt=0xc007c000
stage62_xnu_pmap_workspace_cache_policy=0x00000000
```

## Multi-window page-granular pmap dry-run prerequisite

`stage62/xnu_pmap_multiwindow_dryrun_contract.c` remains the Stage61-style three-window local dry-run prerequisite. It imports the table/page/attribute dry-run prerequisites, zeroes Stage-owned local L1 and L2-bank buffers, creates three independent 4 MiB ARMv7 short-descriptor coarse/L2 windows, populates 3072 small-page PTEs, verifies descriptor/PTE readback, performs four local software translations, checksums the local buffers and contract, and logs all fields through `xnu_log_kv32`.

Confirmed target-side prerequisite markers:

```text
stage62_xnu_pmap_multiwindow_dryrun_contract_status=0x62000001
stage62_xnu_pmap_multiwindow_dryrun_contract_required_mask=0x1fffffff
stage62_xnu_pmap_multiwindow_dryrun_contract_satisfied_mask=0x1fffffff
stage62_xnu_pmap_multiwindow_dryrun_contract_failure_mask=0x00000000
stage62_xnu_pmap_multiwindow_dryrun_contract_checksum=0x263daa78
loader_xnu_pmap_multiwindow_dryrun_contract_status=0x62000001
loader_xnu_pmap_multiwindow_dryrun_contract_satisfied_mask=0x1fffffff
loader_xnu_pmap_multiwindow_dryrun_contract_failure_mask=0x00000000
loader_xnu_pmap_multiwindow_dryrun_contract_checksum=0x263daa78
loader_xnu_pmap_multiwindow_dryrun_contract_status_rollup=0x62000001
```

Local-only L1/L2-bank markers:

```text
stage62_xnu_pmap_multiwindow_dryrun_local_l1_base=0x0009c000
stage62_xnu_pmap_multiwindow_dryrun_local_l1_limit=0x000a0000
stage62_xnu_pmap_multiwindow_dryrun_local_l2_bank_base=0x00099000
stage62_xnu_pmap_multiwindow_dryrun_local_l2_bank_limit=0x0009c000
stage62_xnu_pmap_multiwindow_dryrun_local_l1_zero_checksum=0x409e4000
stage62_xnu_pmap_multiwindow_dryrun_local_l2_zero_checksum=0x0b66f000
stage62_xnu_pmap_multiwindow_dryrun_local_l1_populated_checksum=0x417eb804
stage62_xnu_pmap_multiwindow_dryrun_local_l2_populated_checksum=0x2f686000
stage62_xnu_pmap_multiwindow_dryrun_local_l1_write_count=0x0000000c
stage62_xnu_pmap_multiwindow_dryrun_local_l2_pte_write_count=0x00000c00
```

Software translation markers:

```text
stage62_xnu_pmap_multiwindow_dryrun_translation_kernel_va=0x80008000
stage62_xnu_pmap_multiwindow_dryrun_translation_kernel_pa=0x80008000
stage62_xnu_pmap_multiwindow_dryrun_translation_workspace_va=0x80000000
stage62_xnu_pmap_multiwindow_dryrun_translation_workspace_pa=0x80000000
stage62_xnu_pmap_multiwindow_dryrun_translation_ram_console_va=0xde500000
stage62_xnu_pmap_multiwindow_dryrun_translation_ram_console_pa=0xde500000
stage62_xnu_pmap_multiwindow_dryrun_translation_device_va=0xc0000000
stage62_xnu_pmap_multiwindow_dryrun_translation_device_pa=0xf9000000
stage62_xnu_pmap_multiwindow_dryrun_translation_case_count=0x00000004
```

## Safe live-table transition prerequisite dry-run contract

`stage62/xnu_pmap_transition_dryrun_contract.c` imports the multi-window dry-run output and Stage-owned TTBR roundtrip/restored state. It mirrors only the L1 coarse-table descriptor plan into a separate Stage-owned candidate L1 buffer, continues to use the already-local L2-bank tables for software translations, records proposed control-register values for a later transition, and proves the candidate is distinct from both the recovery L1 and the proposed pmap workspace L1. It remains local/read-only: no control-register writes, no live pmap table install, no TLB invalidation, no cache change, no public VM/pmap execution, no generated Mach-O execution, and no persistent write.

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

Negative safety and read-only markers:

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
```

Final loader roll-up:

```text
loader_satisfied_mask=0xffffffff
loader_status=0x62000001
```

## Built image

```bash
/mnt/data/mi4-ios6/stage62/build.sh
```

Final local build hashes:

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

Boot-image parse highlights for the QCDT image:

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

The non-QCDT image also parses successfully with the same kernel size and `dt_size=0`; the cancro hardware-validation image is `stage62-qcdt.img` because the bootloader requires the legacy QCDT `dt_size=2521088` field.

## Validation status

Current local validation completed:

- `stage62/build.sh` succeeds.
- `tools/parse_android_bootimg.py out/stage62/stage62.img` succeeds.
- `tools/parse_android_bootimg.py out/stage62/stage62-qcdt.img` succeeds and confirms the cancro-required QCDT `dt_size=2521088`.
- `arm-none-eabi-nm -u out/stage62/stage62.elf` prints no undefined symbols.
- `arm-none-eabi-nm -u out/stage62/xnu-link/stage62-xnu-link.elf` prints no undefined symbols.
- Static stale-reference scan found and fixed stale Stage61 Mach-O fixture marker prefixes; the final stale-marker scan reports no stale Stage61 code markers under `stage62/` outside generated or historical context.
- `external/xnu-upstream` and `external/xnu-4570.1.46` remain ignored/untouched.
- `out/stage62/` and `external/` remain ignored by git.
- Fixed public ARM boot-args command lines remain under the 256-byte `CommandLine` field and keep the bounded-copy/final-NUL rule:
  - `stage62/boot_args.c`: `215/256` including NUL.
  - `stage62/stage62_main.c`: `215/256` including NUL.
  - `stage62/xnu_object_shims.c`: `198/256` including NUL.

Hardware validation completed with non-persistent boot only:

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

The next safe direction is still-local MSM8974 pexpert/timer/interrupt hook readiness, IOKit/platform-driver scaffolding, broader high-virtual pmap behavior, or another bounded public-XNU proof while continuing to avoid a full public `mach_kernel` build, XNU runtime handoff, public VM/pmap runtime execution, live proposed table install, TLB invalidation, cache change, and persistent writes.
