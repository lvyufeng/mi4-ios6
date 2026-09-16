# Experiment 62 — Stage59 XNU pmap page-granular dry-run contract

Date: 2026-06-08

## Goal

Stage59 advances the cancro/MSM8974 XNU bring-up path by adding a Stage-owned XNU pmap page-granular dry-run contract on top of the Stage56 bootstrap mapping contract, Stage57 pmap/bootstrap allocation contract, and Stage58 pmap section-table dry-run contract.

Stage58 proved that a local Stage-owned 16 KiB L1 table simulation can model ARMv7 short-descriptor 1 MiB section descriptors for low RAM, kernel, workspace, and ram_console ranges without writing the proposed pmap workspace and without installing live XNU/pmap tables. Stage59 keeps that prerequisite and adds the next blocker: model the ARMv7 short-descriptor coarse L1 descriptor plus L2 small-page PTE path for a bounded 4 MiB page-granular window.

The Stage59 contract verifies that L1 coarse/table descriptors and L2 small-page PTEs can be planned, populated, read back, translated, checksummed, and bounded using only local Stage-owned buffers.

## Safety boundary

Stage59 still does not run XNU or iOS. It does not build a full public `mach_kernel`, does not execute public-XNU object code, does not execute public ARM pexpert/platform runtime code, does not execute public ARM VM/pmap runtime code, does not execute the generated Mach-O fixture, does not jump to XNU `_start` / `arm_init`, does not write proposed physical load addresses, does not write proposed TTE/pmap workspace addresses, does not install live proposed XNU/pmap tables, does not invalidate TLBs for a proposed pmap install, does not mutate external public checkouts, does not write persistent storage, and does not enable or change caches.

Hardware validation must remain non-persistent `fastboot boot` only.

## Implementation

Stage59 adds:

- `stage59/xnu_pmap_page_dryrun_contract.c`
- new pmap page-granular dry-run contract ABI in `stage59/stage59.h`
- a Stage-owned local dry-run L1 buffer:
  - `4096` entries
  - `0x4000` bytes
  - `0x4000` alignment
- a Stage-owned local dry-run L2 page:
  - `1024` PTE entries
  - `0x1000` bytes
  - `0x1000` alignment
- four L1 coarse/table descriptors covering a 4 MiB VA window
- 1024 small-page PTEs covering the same 4 MiB window
- readback checks for L1 descriptors and PTEs
- software translation checks for four representative addresses
- new loader roll-up fields:
  - `xnu_pmap_page_dryrun_contract`
  - `xnu_pmap_page_dryrun_contract_status`
  - `xnu_pmap_page_dryrun_contract_satisfied_mask`
  - `xnu_pmap_page_dryrun_contract_failure_mask`
  - `xnu_pmap_page_dryrun_contract_checksum`
  - `xnu_pmap_page_dryrun_contract_status_rollup`
- a new loader satisfied bit:
  - `STAGE59_LOADER_SAT_XNU_PMAP_PAGE_DRYRUN_CONTRACT`
- boot-image command-line markers:
  - `xnu-pmap-page-dryrun-contract`
  - `pmap-page-local-l2-dryrun-only`
  - inherited `xnu-pmap-table-dryrun-contract`
  - inherited `pmap-table-local-dryrun-only`
  - inherited no-execution/no-proposed-write/no-live-install/no-TLB/no-cache/no-persist markers

The contract imports only Stage-owned/generated facts:

- Stage59 XNU bootstrap mapping contract status/masks/checksum
- Stage59 XNU pmap/bootstrap allocation contract status/masks/checksum
- Stage59 section-table dry-run status/masks/checksum
- Stage-owned pmap/bootstrap snapshot status/masks/checksum
- TTE dry-run status
- safe-table materialization status
- stage-owned table status
- TTBR0 round-trip/restore status
- cache-preservation status
- public-XNU compile graph status
- public-XNU object-subset status
- controlled host-only ARM ELF link-proof status

## Public VM/pmap reference-only inputs

Stage59 keeps the same public reference-only VM/pmap surface as Stage58:

```text
external/xnu-4570.1.46/osfmk/arm/arm_vm_init.c
external/xnu-4570.1.46/osfmk/arm/pmap.c
external/xnu-4570.1.46/osfmk/arm/pmap.h
external/xnu-4570.1.46/osfmk/arm/proc_reg.h
external/xnu-4570.1.46/osfmk/mach/arm/vm_param.h
```

These files are not compiled into the object subset, not linked into the proof ELF, and not executed on target hardware.

Modeled ARMv7 short-descriptor page-table constants include:

```text
PAGE_SIZE=0x00001000
L1_ENTRY_COUNT=4096
L1_BYTES=0x00004000
ARM_TTE_TYPE_TABLE=0x00000001
ARM_TTE_TYPE_MASK=0x00000003
ARM_TTE_TABLE_MASK=0xfffffc00
ARM_TT_L2_SIZE=0x00001000
ARM_TT_L2_OFFMASK=0x00000fff
ARM_TT_L2_SHIFT=12
ARM_TT_L2_INDEX_MASK=0x000ff000
ARM_PTE_TYPE=0x00000002
ARM_PTE_TYPE_MASK=0x00000002
ARM_PTE_PAGE_MASK=0xfffff000
ARM_SMALL_PAGE_SIZE=0x00001000
PTE_ATTR_DEFAULT=0x00000412
```

The modeled PTE attribute value corresponds to the public early small-page template shape:

```text
ARM_PTE_TYPE | ARM_PTE_AF | ARM_PTE_SH |
ARM_PTE_ATTRINDX(CACHE_ATTRINDX_DEFAULT) |
ARM_PTE_AP(AP_RWNA)
```

Modeled public ARM VM/pmap arithmetic remains inherited from Stage57/Stage58:

```text
gVirtBase = virtBase
gPhysBase = physBase
gPhysSize = memSize
boot_ttep = topOfKernelData
cpu_ttep = boot_ttep + 4 * PAGE_SIZE
initial_avail_start = cpu_ttep + 6 * PAGE_SIZE
avail_end = physBase + memSize
vstart = (virtBase + MEM_SIZE_MAX + 0x3fffff) & 0xffc00000
virtual_space_end = VM_MAX_KERNEL_ADDRESS
```

## Compile graph stability

Stage59 keeps the allowed public object set stable:

```text
external/xnu-upstream/pexpert/gen/device_tree.c
external/xnu-upstream/pexpert/gen/bootargs.c
external/xnu-upstream/pexpert/gen/pe_gen.c
external/xnu-4570.1.46/pexpert/arm/pe_bootargs.c
external/xnu-4570.1.46/pexpert/arm/pe_consistent_debug.c
```

It explicitly keeps the pmap/VM reference-only files out of compile/link allow-lists and fails closed if public pmap/VM runtime files become compile/link allowed.

Final graph markers:

```text
stage59_xnu_compile_graph_status=0x59000001
stage59_xnu_compile_graph_required_mask=0x1fffffff
stage59_xnu_compile_graph_satisfied_mask=0x1fffffff
stage59_xnu_compile_graph_failure_mask=0x00000000
stage59_xnu_compile_graph_candidate_count=0x00000014
stage59_xnu_compile_graph_allowed_compile_count=0x00000005
stage59_xnu_compile_graph_allowed_link_count=0x00000005
stage59_xnu_compile_graph_forbidden_count=0x0000000f
stage59_xnu_compile_graph_pmap_reference_mask=0x0000001f
stage59_xnu_compile_graph_pmap_runtime_blocked_mask=0x00000003
stage59_xnu_compile_graph_pmap_reference_count=0x00000005
stage59_xnu_compile_graph_pmap_public_compile_count=0x00000000
stage59_xnu_compile_graph_pmap_public_link_count=0x00000000
stage59_xnu_compile_graph_pmap_reference_only=0x00000001
```

## Pmap page-granular dry-run contract markers

Confirmed target-side contract markers:

```text
stage59_xnu_pmap_page_dryrun_contract_status=0x59000001
stage59_xnu_pmap_page_dryrun_contract_required_mask=0x00ffffff
stage59_xnu_pmap_page_dryrun_contract_satisfied_mask=0x00ffffff
stage59_xnu_pmap_page_dryrun_contract_failure_mask=0x00000000
loader_xnu_pmap_page_dryrun_contract_status=0x59000001
loader_xnu_pmap_page_dryrun_contract_satisfied_mask=0x00ffffff
loader_xnu_pmap_page_dryrun_contract_failure_mask=0x00000000
loader_xnu_pmap_page_dryrun_contract_checksum=0xa050cddf
loader_xnu_pmap_page_dryrun_contract_status_rollup=0x59000001
```

Page-table constant markers:

```text
stage59_xnu_pmap_page_dryrun_l1_table_type=0x00000001
stage59_xnu_pmap_page_dryrun_l1_table_mask=0xfffffc00
stage59_xnu_pmap_page_dryrun_l2_page_bytes=0x00001000
stage59_xnu_pmap_page_dryrun_l2_coarse_table_bytes=0x00000400
stage59_xnu_pmap_page_dryrun_l2_pte_count=0x00000400
stage59_xnu_pmap_page_dryrun_pte_attr_default=0x00000412
```

Local-only buffer markers:

```text
stage59_xnu_pmap_page_dryrun_local_l1_base=0x00088000
stage59_xnu_pmap_page_dryrun_local_l1_limit=0x0008c000
stage59_xnu_pmap_page_dryrun_local_l2_base=0x00085000
stage59_xnu_pmap_page_dryrun_local_l2_limit=0x00086000
stage59_xnu_pmap_page_dryrun_local_l1_write_count=0x00000004
stage59_xnu_pmap_page_dryrun_local_l2_pte_write_count=0x00000400
```

Window markers:

```text
stage59_xnu_pmap_page_dryrun_window_virt_base=0x80000000
stage59_xnu_pmap_page_dryrun_window_virt_limit=0x80400000
stage59_xnu_pmap_page_dryrun_window_phys_base=0x80000000
stage59_xnu_pmap_page_dryrun_window_phys_limit=0x80400000
stage59_xnu_pmap_page_dryrun_l1_first_index=0x00000800
stage59_xnu_pmap_page_dryrun_l1_last_index=0x00000803
stage59_xnu_pmap_page_dryrun_l1_count=0x00000004
stage59_xnu_pmap_page_dryrun_l2_pte_first_index=0x00000000
stage59_xnu_pmap_page_dryrun_l2_pte_last_index=0x000003ff
stage59_xnu_pmap_page_dryrun_l2_pte_count=0x00000400
```

Descriptor and PTE readback markers:

```text
stage59_xnu_pmap_page_dryrun_l1_descriptor_first_word=0x00085001
stage59_xnu_pmap_page_dryrun_l1_descriptor_last_word=0x00085c01
stage59_xnu_pmap_page_dryrun_l1_descriptor_type_mask_seen=0x00000001
stage59_xnu_pmap_page_dryrun_l1_descriptor_attr_mask_seen=0x00000001
stage59_xnu_pmap_page_dryrun_pte_first_word=0x80000412
stage59_xnu_pmap_page_dryrun_pte_kernel_word=0x80008412
stage59_xnu_pmap_page_dryrun_pte_workspace_word=0x80000412
stage59_xnu_pmap_page_dryrun_pte_last_word=0x803ff412
stage59_xnu_pmap_page_dryrun_pte_type_mask_seen=0x00000002
stage59_xnu_pmap_page_dryrun_pte_attr_mask_seen=0x00000412
```

Translation markers:

```text
stage59_xnu_pmap_page_dryrun_translation_kernel_va=0x80008000
stage59_xnu_pmap_page_dryrun_translation_kernel_pa=0x80008000
stage59_xnu_pmap_page_dryrun_translation_workspace_va=0x80000000
stage59_xnu_pmap_page_dryrun_translation_workspace_pa=0x80000000
stage59_xnu_pmap_page_dryrun_translation_window_first_va=0x80000000
stage59_xnu_pmap_page_dryrun_translation_window_first_pa=0x80000000
stage59_xnu_pmap_page_dryrun_translation_window_last_va=0x803fffff
stage59_xnu_pmap_page_dryrun_translation_window_last_pa=0x803fffff
stage59_xnu_pmap_page_dryrun_translation_case_count=0x00000004
```

Safety markers:

```text
stage59_xnu_pmap_page_dryrun_public_pmap_compile_count=0x00000000
stage59_xnu_pmap_page_dryrun_public_pmap_link_count=0x00000000
stage59_xnu_pmap_page_dryrun_public_pmap_execute_count=0x00000000
stage59_xnu_pmap_page_dryrun_proposed_workspace_written=0x00000000
stage59_xnu_pmap_page_dryrun_live_pmap_tables_installed=0x00000000
stage59_xnu_pmap_page_dryrun_ttbr_written=0x00000000
stage59_xnu_pmap_page_dryrun_ttbcr_written=0x00000000
stage59_xnu_pmap_page_dryrun_dacr_written=0x00000000
stage59_xnu_pmap_page_dryrun_sctlr_written=0x00000000
stage59_xnu_pmap_page_dryrun_tlbs_invalidated=0x00000000
stage59_xnu_pmap_page_dryrun_public_arm_vm_init_executed=0x00000000
stage59_xnu_pmap_page_dryrun_xnu_start_executed=0x00000000
stage59_xnu_pmap_page_dryrun_generated_macho_executed=0x00000000
stage59_xnu_pmap_page_dryrun_persistent_write_attempted=0x00000000
stage59_xnu_pmap_page_dryrun_caches_changed=0x00000000
stage59_xnu_pmap_page_dryrun_fail_closed=0x00000001
loader_status=0x59000001
```

## Local validation

Local build passed:

```bash
/mnt/data/mi4-ios6/stage59/build.sh
```

Final local image hashes:

```text
8c5399b4c9f3a055a633ce636f512adbb7057b1bec0cfad374b5e197921b9fda  out/stage59/stage59_fixture.macho
972782b5be8108041dcfa935e15d79e4f8b406394dff317c36962ea6a200142f  out/stage59/stage59.elf
efcd9b8d86b8da13d7b2e905f21387a3bb73e652f42be6594c630e2d3a0ee20e  out/stage59/stage59.bin
4b5d71106d90f581d45c47a5899871b1e95bcebbc7b0e83a0040fb536dd18070  out/stage59/stage59.img
6a79d281de5d242c3851bb06120951690ecfdff4931d7c88ffcabff6a034bc21  out/stage59/stage59-qcdt.img
```

Size summary:

```text
text=218792 data=0 bss=315712 dec=534504 hex=827e8
```

Boot-image parse passed for both images. QCDT image highlights:

```text
magic=ANDROID!
page_size=2048
kernel_size=218792 (0x356a8)
kernel_addr=32768 (0x8000)
ramdisk_size=0 (0x0)
ramdisk_addr=33554432 (0x2000000)
second_size=0 (0x0)
second_addr=15728640 (0xf00000)
tags_addr=31457280 (0x1e00000)
dt_size=2521088 (0x267800)
cmdline=stage59 mi4ios6=stage59 ... xnu-bootstrap-contract xnu-pmap-bootstrap-contract xnu-pmap-table-dryrun-contract xnu-pmap-page-dryrun-contract pmap-table-local-dryrun-only pmap-page-local-l2-dryrun-only ... no-proposed-pmap-write no-live-pmap-install no-tlb-invalidate no-cache-policy-change no-persist-write no-external-mutation
part=kernel offset=0x800 size=218792 sha256=efcd9b8d86b8da13d7b2e905f21387a3bb73e652f42be6594c630e2d3a0ee20e
part=dt.img offset=0x36000 size=2521088 sha256=c8faf487909b668fcebddfb2dcbd793bfb94d0411286c1f5c588bc0015120efe
```

Undefined-symbol checks passed:

```bash
arm-none-eabi-nm -u /mnt/data/mi4-ios6/out/stage59/stage59.elf
arm-none-eabi-nm -u /mnt/data/mi4-ios6/out/stage59/xnu-link/stage59-xnu-link.elf
```

Both commands produced no undefined symbols.

Static stale-reference scan under `stage59/` is clean for stale Stage58 code markers outside generated or historical context. `external/xnu-upstream` and `external/xnu-4570.1.46` remain clean, and `out/stage59/` remains ignored.

## Hardware validation

Stage59 hardware validation completed with non-persistent boot only:

```bash
sudo adb -s 4a2fe00b reboot bootloader
sudo fastboot -s 4a2fe00b boot /mnt/data/mi4-ios6/out/stage59/stage59-qcdt.img
sudo adb -s 4a2fe00b exec-out 'cat /proc/last_kmsg' > /tmp/cancro-stage59-last_kmsg.txt
```

Recovered log facts:

```text
stage59_last_kmsg_bytes=153475 (0x00025783)
stage59_marker_count=2257 (0x000008d1)
stage59_xnu_marker_count=2231 (0x000008b7)
stage59_hardware_validation=ok
```

Confirmed hardware markers after non-persistent boot:

```text
MI4IOS6_STAGE59_XNU stage59_xnu_compile_graph_status=0x59000001
MI4IOS6_STAGE59_XNU stage59_xnu_compile_graph_pmap_public_compile_count=0x00000000
MI4IOS6_STAGE59_XNU stage59_xnu_compile_graph_pmap_public_link_count=0x00000000
MI4IOS6_STAGE59_XNU stage59_xnu_object_subset_status=0x59000001
MI4IOS6_STAGE59_XNU stage59_xnu_link_status=0x59000001
MI4IOS6_STAGE59_XNU stage59_xnu_bootstrap_contract_status=0x59000001
MI4IOS6_STAGE59_XNU stage59_xnu_pmap_bootstrap_contract_status=0x59000001
MI4IOS6_STAGE59_XNU stage59_xnu_pmap_table_dryrun_contract_status=0x59000001
MI4IOS6_STAGE59_XNU stage59_xnu_pmap_page_dryrun_contract_status=0x59000001
MI4IOS6_STAGE59_XNU stage59_xnu_pmap_page_dryrun_contract_required_mask=0x00ffffff
MI4IOS6_STAGE59_XNU stage59_xnu_pmap_page_dryrun_contract_satisfied_mask=0x00ffffff
MI4IOS6_STAGE59_XNU stage59_xnu_pmap_page_dryrun_contract_failure_mask=0x00000000
MI4IOS6_STAGE59_XNU loader_xnu_pmap_page_dryrun_contract_status_rollup=0x59000001
MI4IOS6_STAGE59_XNU loader_status=0x59000001
MI4IOS6_STAGE59_XNU kernel_entry ok
MI4IOS6_STAGE59 kernel_entry returned success
```

The device returned to Android after the PS_HOLD reset path. No persistent flash/erase/write was performed.

## Result

Stage59 completes the local-only page-granular pmap dry-run blocker. The new contract proves that the Stage-owned model can represent ARMv7 L1 coarse/table descriptors and L2 small-page PTEs for a bounded 4 MiB XNU-style mapping window while preserving all no-execution, no-proposed-write, no-live-table-install, no-control-register-write, no-TLB-invalidate, no-cache-change, and no-persistent-write boundaries.

The next safe direction is exact XNU cache/MMU attribute modeling, broader page-granular pmap windows, MSM8974 pexpert/timer/interrupt hooks, or another bounded public-XNU proof while continuing to avoid a full public `mach_kernel` build or any XNU runtime handoff.
