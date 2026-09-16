# Experiment 61 — Stage58 XNU pmap table population dry-run contract

Date: 2026-06-08

## Goal

Stage58 advances the cancro/MSM8974 XNU bring-up path by adding a Stage-owned XNU pmap table population dry-run contract on top of the Stage57 bootstrap mapping and pmap/bootstrap allocation contracts.

Stage57 proved the public-style pmap bootstrap tuple and allocator/workspace facts (`gVirtBase`, `gPhysBase`, `gPhysSize`, `boot_ttep`, `cpu_ttep`, `initial_avail_start`, `avail_end`, `vstart`, and `virtual_space_end`) while keeping public ARM `arm_vm_init.c` and `pmap.c` reference-only. Stage58 keeps those facts and adds the next blocker: simulate the early ARMv7 L1 section table population in a local Stage-owned scratch buffer only.

The Stage58 contract verifies that section descriptors can be planned, populated, read back, translated, checksummed, and bounded without writing the proposed pmap workspace and without installing live XNU/pmap tables.

## Safety boundary

Stage58 still does not run XNU or iOS. It does not build a full public `mach_kernel`, does not execute public-XNU object code, does not execute public ARM pexpert/platform runtime code, does not execute public ARM VM/pmap runtime code, does not execute the generated Mach-O fixture, does not jump to XNU `_start` / `arm_init`, does not write proposed physical load addresses, does not write proposed TTE/pmap workspace addresses, does not install live proposed XNU/pmap tables, does not invalidate TLBs for a proposed pmap install, does not mutate external public checkouts, does not write persistent storage, and does not enable or change caches.

Hardware validation must remain non-persistent `fastboot boot` only.

## Implementation

Stage58 adds:

- `stage58/xnu_pmap_table_dryrun_contract.c`
- new pmap table dry-run contract ABI in `stage58/stage58.h`
- a Stage-owned local dry-run L1 buffer:
  - `4096` entries
  - `0x4000` bytes
  - `0x4000` alignment
- new loader roll-up fields:
  - `xnu_pmap_table_dryrun_contract`
  - `xnu_pmap_table_dryrun_contract_status`
  - `xnu_pmap_table_dryrun_contract_satisfied_mask`
  - `xnu_pmap_table_dryrun_contract_failure_mask`
  - `xnu_pmap_table_dryrun_contract_checksum`
  - `xnu_pmap_table_dryrun_contract_status_rollup`
- a new loader satisfied bit:
  - `STAGE58_LOADER_SAT_XNU_PMAP_TABLE_DRYRUN_CONTRACT`
- boot-image command-line markers:
  - `xnu-pmap-table-dryrun-contract`
  - `pmap-table-local-dryrun-only`
  - `no-proposed-pmap-write`
  - `no-live-pmap-install`
  - `no-tlb-invalidate`

The contract imports only Stage-owned/generated facts:

- Stage58 XNU bootstrap mapping contract status/masks/checksum
- Stage58 XNU pmap/bootstrap allocation contract status/masks/checksum
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

Stage58 keeps the same public reference-only VM/pmap surface as Stage57:

```text
external/xnu-4570.1.46/osfmk/arm/arm_vm_init.c
external/xnu-4570.1.46/osfmk/arm/pmap.c
external/xnu-4570.1.46/osfmk/arm/pmap.h
external/xnu-4570.1.46/osfmk/arm/proc_reg.h
external/xnu-4570.1.46/osfmk/mach/arm/vm_param.h
```

These files are not compiled into the object subset, not linked into the proof ELF, and not executed on target hardware.

Modeled public constants/facts:

```text
PAGE_SIZE=0x00001000
MEM_SIZE_MAX=0x40000000
VM_MIN_KERNEL_ADDRESS=0x80000000
VM_MAX_KERNEL_ADDRESS=0xfffeffff
L1_ALIGNMENT=0x00004000
L1_ENTRY_COUNT=4096
L1_BYTES=0x00004000
SECTION_SIZE=0x00100000
SECTION_DESC_SO=0x00010c02
DESC_TYPE_SECTION=0x00000002
DESC_BASE_MASK=0xfff00000
```

Modeled public ARM VM/pmap arithmetic remains:

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

Stage58 keeps the allowed public object set stable:

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
stage58_xnu_compile_graph_status=0x58000001
stage58_xnu_compile_graph_required_mask=0x1fffffff
stage58_xnu_compile_graph_satisfied_mask=0x1fffffff
stage58_xnu_compile_graph_failure_mask=0x00000000
stage58_xnu_compile_graph_candidate_count=0x00000014
stage58_xnu_compile_graph_allowed_compile_count=0x00000005
stage58_xnu_compile_graph_allowed_link_count=0x00000005
stage58_xnu_compile_graph_forbidden_count=0x0000000f
stage58_xnu_compile_graph_pmap_reference_mask=0x0000001f
stage58_xnu_compile_graph_pmap_runtime_blocked_mask=0x00000003
stage58_xnu_compile_graph_pmap_reference_count=0x00000005
stage58_xnu_compile_graph_pmap_public_compile_count=0x00000000
stage58_xnu_compile_graph_pmap_public_link_count=0x00000000
stage58_xnu_compile_graph_pmap_reference_only=0x00000001
```

## Pmap table dry-run contract markers

Confirmed target-side contract markers:

```text
stage58_xnu_pmap_table_dryrun_contract_status=0x58000001
stage58_xnu_pmap_table_dryrun_contract_required_mask=0x01ffffff
stage58_xnu_pmap_table_dryrun_contract_satisfied_mask=0x01ffffff
stage58_xnu_pmap_table_dryrun_contract_failure_mask=0x00000000
stage58_xnu_pmap_table_dryrun_contract_checksum=0x3c263dca
```

Local-only L1 buffer markers:

```text
stage58_xnu_pmap_table_dryrun_local_l1_base=0x00080000
stage58_xnu_pmap_table_dryrun_local_l1_limit=0x00084000
stage58_xnu_pmap_table_dryrun_local_l1_zero_checksum=0x726ac000
stage58_xnu_pmap_table_dryrun_local_l1_populated_checksum=0x1928a50e
stage58_xnu_pmap_table_dryrun_local_l1_write_count=0x000005e9
stage58_xnu_pmap_table_dryrun_workspace_l1_table_phys=0x00074000
stage58_xnu_pmap_table_dryrun_workspace_l1_table_virt=0xc0074000
stage58_xnu_pmap_table_dryrun_workspace_l1_table_local_distinct=0x00000001
```

Imported arithmetic markers:

```text
stage58_xnu_pmap_table_dryrun_gVirtBase=0x80008000
stage58_xnu_pmap_table_dryrun_gPhysBase=0x80000000
stage58_xnu_pmap_table_dryrun_gPhysSize=0x5e500000
stage58_xnu_pmap_table_dryrun_boot_ttep=0x8000c000
stage58_xnu_pmap_table_dryrun_cpu_ttep=0x80010000
stage58_xnu_pmap_table_dryrun_initial_avail_start=0x80016000
stage58_xnu_pmap_table_dryrun_avail_end=0xde500000
stage58_xnu_pmap_table_dryrun_vstart=0xc0400000
stage58_xnu_pmap_table_dryrun_virtual_space_end=0xfffeffff
```

Descriptor range/readback markers:

```text
stage58_xnu_pmap_table_dryrun_lowmem_l1_first_index=0x00000800
stage58_xnu_pmap_table_dryrun_lowmem_l1_last_index=0x00000de4
stage58_xnu_pmap_table_dryrun_lowmem_l1_section_count=0x000005e5
stage58_xnu_pmap_table_dryrun_kernel_l1_first_index=0x00000800
stage58_xnu_pmap_table_dryrun_kernel_l1_last_index=0x00000800
stage58_xnu_pmap_table_dryrun_kernel_l1_section_count=0x00000001
stage58_xnu_pmap_table_dryrun_workspace_l1_first_index=0x00000800
stage58_xnu_pmap_table_dryrun_workspace_l1_last_index=0x00000800
stage58_xnu_pmap_table_dryrun_workspace_l1_section_count=0x00000001
stage58_xnu_pmap_table_dryrun_ram_console_l1_first_index=0x00000de5
stage58_xnu_pmap_table_dryrun_ram_console_l1_last_index=0x00000de6
stage58_xnu_pmap_table_dryrun_ram_console_l1_section_count=0x00000002
stage58_xnu_pmap_table_dryrun_descriptor_type_mask_seen=0x00000002
stage58_xnu_pmap_table_dryrun_descriptor_attr_mask_seen=0x00010c02
stage58_xnu_pmap_table_dryrun_translation_case_count=0x00000004
```

Safety markers:

```text
stage58_xnu_pmap_table_dryrun_public_pmap_compile_count=0x00000000
stage58_xnu_pmap_table_dryrun_public_pmap_link_count=0x00000000
stage58_xnu_pmap_table_dryrun_public_pmap_execute_count=0x00000000
stage58_xnu_pmap_table_dryrun_proposed_workspace_written=0x00000000
stage58_xnu_pmap_table_dryrun_live_pmap_tables_installed=0x00000000
stage58_xnu_pmap_table_dryrun_ttbr_written=0x00000000
stage58_xnu_pmap_table_dryrun_ttbcr_written=0x00000000
stage58_xnu_pmap_table_dryrun_dacr_written=0x00000000
stage58_xnu_pmap_table_dryrun_sctlr_written=0x00000000
stage58_xnu_pmap_table_dryrun_tlbs_invalidated=0x00000000
stage58_xnu_pmap_table_dryrun_public_arm_vm_init_executed=0x00000000
stage58_xnu_pmap_table_dryrun_xnu_start_executed=0x00000000
stage58_xnu_pmap_table_dryrun_generated_macho_executed=0x00000000
stage58_xnu_pmap_table_dryrun_persistent_write_attempted=0x00000000
stage58_xnu_pmap_table_dryrun_caches_changed=0x00000000
stage58_xnu_pmap_table_dryrun_fail_closed=0x00000001
loader_xnu_pmap_table_dryrun_contract_status_rollup=0x58000001
loader_status=0x58000001
```

## Local validation

Local build passed:

```bash
/mnt/data/mi4-ios6/stage58/build.sh
```

Final local image hashes:

```text
8ea34d09cfcb4239c7cad62b079d87f4b53f223a4b4bfe7b7894b8b731066ede  out/stage58/stage58_fixture.macho
884503d5115b42ae1d039c980c2476fbc483ff6f528c34b9142765b54a8d0a60  out/stage58/stage58.elf
73932e6b0cf3ba258fb27d119a80b9dd4838503dc37cf13666bb2fdfba6dcd1d  out/stage58/stage58.bin
4c76aa98898e927973f3e7b482ef138c99d8b865ee1634c81762f6241d56f59e  out/stage58/stage58.img
3d35a5af065c779cac4d5cd43dd74806e95145531523c582966a07df574ea7f5  out/stage58/stage58-qcdt.img
```

Boot-image parse passed for both images. QCDT image highlights:

```text
magic=ANDROID!
page_size=2048
kernel_size=205824 (0x32400)
kernel_addr=32768 (0x8000)
ramdisk_size=0 (0x0)
ramdisk_addr=33554432 (0x2000000)
second_size=0 (0x0)
second_addr=15728640 (0xf00000)
tags_addr=31457280 (0x1e00000)
dt_size=2521088 (0x267800)
cmdline=stage58 mi4ios6=stage58 ... xnu-bootstrap-contract xnu-pmap-bootstrap-contract xnu-pmap-table-dryrun-contract pmap-table-local-dryrun-only ... no-proposed-pmap-write no-live-pmap-install no-tlb-invalidate no-cache-policy-change no-persist-write no-external-mutation
part=kernel offset=0x800 size=205824 sha256=73932e6b0cf3ba258fb27d119a80b9dd4838503dc37cf13666bb2fdfba6dcd1d
part=dt.img offset=0x33000 size=2521088 sha256=c8faf487909b668fcebddfb2dcbd793bfb94d0411286c1f5c588bc0015120efe
```

Undefined-symbol checks passed:

```bash
arm-none-eabi-nm -u /mnt/data/mi4-ios6/out/stage58/stage58.elf
arm-none-eabi-nm -u /mnt/data/mi4-ios6/out/stage58/xnu-link/stage58-xnu-link.elf
```

Both commands produced no undefined symbols.

Static stale-reference scan under `stage58/` is clean for stale Stage57 code markers. `external/xnu-upstream` and `external/xnu-4570.1.46` remain clean, and `out/stage58/` remains ignored.

## Hardware validation

Stage58 hardware validation completed with non-persistent boot only:

```bash
sudo adb -s 4a2fe00b reboot bootloader
sudo fastboot -s 4a2fe00b boot /mnt/data/mi4-ios6/out/stage58/stage58-qcdt.img
sudo adb -s 4a2fe00b exec-out 'cat /proc/last_kmsg' > /tmp/cancro-stage58-last_kmsg.txt
```

Recovered log facts:

```text
stage58_last_kmsg_bytes=0x000239f4
stage58_marker_count=0x00000872
stage58_xnu_marker_count=0x00000858
stage58_hardware_validation=ok
```

Confirmed hardware markers after non-persistent boot:

```text
MI4IOS6_STAGE58_XNU stage58_xnu_compile_graph_status=0x58000001
MI4IOS6_STAGE58_XNU stage58_xnu_compile_graph_pmap_public_compile_count=0x00000000
MI4IOS6_STAGE58_XNU stage58_xnu_compile_graph_pmap_public_link_count=0x00000000
MI4IOS6_STAGE58_XNU stage58_xnu_object_subset_status=0x58000001
MI4IOS6_STAGE58_XNU stage58_xnu_link_status=0x58000001
MI4IOS6_STAGE58_XNU stage58_xnu_bootstrap_contract_status=0x58000001
MI4IOS6_STAGE58_XNU stage58_xnu_pmap_bootstrap_contract_status=0x58000001
MI4IOS6_STAGE58_XNU stage58_xnu_pmap_table_dryrun_contract_status=0x58000001
MI4IOS6_STAGE58_XNU stage58_xnu_pmap_table_dryrun_contract_failure_mask=0x00000000
MI4IOS6_STAGE58_XNU loader_xnu_pmap_table_dryrun_contract_status_rollup=0x58000001
MI4IOS6_STAGE58_XNU loader_status=0x58000001
MI4IOS6_STAGE58_XNU kernel_entry ok
MI4IOS6_STAGE58 kernel_entry returned success
```

The device returned to Android after the PS_HOLD reset path. No persistent flash/erase/write was performed.

## Result

Stage58 completes the local-only pmap table population dry-run blocker. The new contract proves that the Stage-owned model can build and validate an ARMv7 section L1 table from the current pmap/bootstrap tuple while preserving all no-execution, no-proposed-write, no-live-table-install, no-TLB-invalidate, no-cache-change, and no-persistent-write boundaries.

The next safe direction is deeper page-granular pmap/XNU mapping modeling, exact XNU cache/MMU attributes, MSM8974 pexpert/timer/interrupt hooks, or another bounded public-XNU proof while continuing to avoid a full public `mach_kernel` build or any XNU runtime handoff.
