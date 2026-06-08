# Stage58 — XNU pmap table population dry-run contract

Stage58 keeps the no-XNU-runtime safety envelope, preserves the bounded public pexpert object/link proof, preserves the Stage57 bootstrap and pmap/bootstrap allocation contracts, and adds a Stage-owned **XNU pmap table population dry-run contract** for Xiaomi Mi 4 cancro / MSM8974 ARMv7.

The new proof constructs, populates, reads back, translates, and checksums a simulated ARMv7 short-descriptor L1 section table in a **local Stage-owned scratch buffer only**. It derives the table plan from the Stage57/Stage58 bootstrap tuple and the Stage-owned pmap/bootstrap snapshot, but it does not write the proposed pmap workspace and does not install any live XNU/pmap table.

## Public object subset

The public object subset remains intentionally stable:

```text
external/xnu-upstream/pexpert/gen/device_tree.c
external/xnu-upstream/pexpert/gen/bootargs.c
external/xnu-upstream/pexpert/gen/pe_gen.c
external/xnu-4570.1.46/pexpert/arm/pe_bootargs.c
external/xnu-4570.1.46/pexpert/arm/pe_consistent_debug.c
stage58/xnu_object_shims.c
```

Public ARM VM/pmap sources remain reference-only. Stage58 still does **not** compile, link, or execute public `arm_vm_init.c` or `pmap.c`.

## Safety model

Stage58 still does **not** run XNU or iOS. It does not build a full public `mach_kernel`, does not execute public-XNU object code, does not execute public ARM pexpert/platform runtime code, does not execute public ARM VM/pmap runtime code, does not execute the generated Mach-O fixture, does not jump to XNU `_start` / `arm_init`, does not write proposed XNU physical load addresses, does not write proposed TTE/pmap workspace addresses, does not install live proposed XNU/pmap tables, does not mutate external public checkouts, does not write persistent storage, and does not enable or change caches.

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
- Public-XNU graph/object/link outputs stay ignored under `out/stage58/`.
- Public source checkouts stay ignored under `external/`.

The Stage58 completion message is intentionally explicit:

```text
Stage58 XNU execution disabled: bootstrap mapping, pmap/bootstrap allocation, and local pmap table population dry-run contracts proved from stage-owned loader/TTE/highVA/safe-table/TTBR and allocator/workspace snapshot facts; public arm_vm_init/pmap references are arithmetic-only and compile/link/execute counts are zero; pexpert/platform compile graph classified, bounded public pe_gen plus ARM pe_bootargs and ARM consistent_debug objects compiled, and controlled ARM ELF link proof recorded; inert MH_PRELOAD fixture carries metadata only; no full mach_kernel build, no public-XNU execution, no platform runtime execution, no Mach-O execution, no proposed physical/workspace writes, no live pmap table install, no TLB invalidate for pmap install, no persistent writes, caches unchanged
```

## What Stage58 adds

Stage58 keeps the Stage57 bounded public object/link proof and pmap/bootstrap allocation contract stable, and adds:

- a standalone `stage58/` payload with `0x58000001` success status and `MI4IOS6_STAGE58` logs,
- command-line markers for `xnu-pmap-table-dryrun-contract` and `pmap-table-local-dryrun-only`,
- a Stage-owned `struct stage58_xnu_pmap_table_dryrun_contract` ABI in `stage58.h`,
- `stage58/xnu_pmap_table_dryrun_contract.c`,
- a 4096-entry, 16 KiB-aligned local dry-run L1 array,
- descriptor readback and software translation checks for low RAM, kernel VA, pmap workspace, and ram_console sections,
- loader preflight roll-up fields for `xnu_pmap_table_dryrun_contract`,
- target-side proof that no proposed workspace was written and no live pmap table was installed,
- target-side proof that public pmap compile/link/execute counts remain zero.

## Selected public XNU baselines

Stage58 expects:

```text
external/xnu-upstream      xnu-2050.22.13  cc8a9b0c  MasterVersion 12.3.0
external/xnu-4570.1.46     xnu-4570.1.46   76e12aa   later public ARM reference only
```

The public 2050 tree remains the Darwin 12 / iOS 6-era context. The 4570 tree is used only as a public ARM implementation reference for bounded compile/link proof objects and for blocked/reference-only source classification.

## Public VM/pmap reference-only surface

Stage58 reads/models these later-public ARM files only as public reference material:

```text
external/xnu-4570.1.46/osfmk/arm/arm_vm_init.c
external/xnu-4570.1.46/osfmk/arm/pmap.c
external/xnu-4570.1.46/osfmk/arm/pmap.h
external/xnu-4570.1.46/osfmk/arm/proc_reg.h
external/xnu-4570.1.46/osfmk/mach/arm/vm_param.h
```

They are not compiled into the public object subset, not linked into the proof ELF, and not executed on target hardware.

Modeled public constants/facts include:

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

## Compile graph, object subset, and link proof

`stage58/xnu_compile_graph_scan.py` still classifies twenty candidates and allows only the five bounded public pexpert sources. ARM VM/pmap sources remain reference-only or blocked runtime inputs.

Successful graph/object/link markers from local and target validation:

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
stage58_xnu_object_subset_status=0x58000001
stage58_xnu_object_count=0x00000006
stage58_xnu_object_duplicate_symbol_count=0x00000000
stage58_xnu_link_status=0x58000001
stage58_xnu_link_object_count=0x00000006
stage58_xnu_link_undefined_symbol_count=0x00000000
```

`arm-none-eabi-nm -u out/stage58/stage58.elf` and `arm-none-eabi-nm -u out/stage58/xnu-link/stage58-xnu-link.elf` both report no undefined symbols.

## Bootstrap and pmap/bootstrap prerequisite contracts

Stage58 retains the Stage57 contracts as prerequisites:

```text
stage58_xnu_bootstrap_contract_status=0x58000001
stage58_xnu_bootstrap_contract_required_mask=0x7fffffff
stage58_xnu_bootstrap_contract_failure_mask=0x00000000
loader_xnu_bootstrap_contract_status_rollup=0x58000001

stage58_xnu_pmap_bootstrap_contract_status=0x58000001
stage58_xnu_pmap_bootstrap_contract_required_mask=0x7fffffff
stage58_xnu_pmap_bootstrap_contract_satisfied_mask=0x7fffffff
stage58_xnu_pmap_bootstrap_contract_failure_mask=0x00000000
stage58_xnu_pmap_public_compile_count=0x00000000
stage58_xnu_pmap_public_link_count=0x00000000
stage58_xnu_pmap_public_execute_count=0x00000000
loader_xnu_pmap_bootstrap_contract_status_rollup=0x58000001
```

Modeled pmap/bootstrap arithmetic remains:

```text
stage58_xnu_pmap_gVirtBase=0x80008000
stage58_xnu_pmap_gPhysBase=0x80000000
stage58_xnu_pmap_gPhysSize=0x5e500000
stage58_xnu_pmap_boot_ttep=0x8000c000
stage58_xnu_pmap_cpu_ttep=0x80010000
stage58_xnu_pmap_initial_avail_start=0x80016000
stage58_xnu_pmap_avail_end=0xde500000
stage58_xnu_pmap_vstart=0xc0400000
stage58_xnu_pmap_virtual_space_end=0xfffeffff
stage58_xnu_pmap_workspace_base=0x80000000
stage58_xnu_pmap_workspace_limit=0x80100000
stage58_xnu_pmap_workspace_l1_table_phys=0x00074000
stage58_xnu_pmap_workspace_l1_table_virt=0xc0074000
stage58_xnu_pmap_workspace_l1_section_descriptor=0x00010c02
stage58_xnu_pmap_workspace_cache_policy=0x00000000
```

## Pmap table population dry-run contract

`stage58/xnu_pmap_table_dryrun_contract.c` imports the pmap/bootstrap allocation contract and snapshot facts, zeroes a Stage-owned local L1 buffer, simulates section descriptor population into that buffer, reads descriptors back, performs local software translations, and logs all fields through `xnu_log_kv32`.

Confirmed target-side contract markers:

```text
stage58_xnu_pmap_table_dryrun_contract_status=0x58000001
stage58_xnu_pmap_table_dryrun_contract_required_mask=0x01ffffff
stage58_xnu_pmap_table_dryrun_contract_satisfied_mask=0x01ffffff
stage58_xnu_pmap_table_dryrun_contract_failure_mask=0x00000000
stage58_xnu_pmap_table_dryrun_contract_checksum=0x3c263dca
```

Local L1 dry-run buffer markers:

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

Imported pmap arithmetic markers:

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

Negative safety markers:

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

## Built image

```bash
/mnt/data/mi4-ios6/stage58/build.sh
```

Final local build hashes:

```text
8ea34d09cfcb4239c7cad62b079d87f4b53f223a4b4bfe7b7894b8b731066ede  out/stage58/stage58_fixture.macho
884503d5115b42ae1d039c980c2476fbc483ff6f528c34b9142765b54a8d0a60  out/stage58/stage58.elf
73932e6b0cf3ba258fb27d119a80b9dd4838503dc37cf13666bb2fdfba6dcd1d  out/stage58/stage58.bin
4c76aa98898e927973f3e7b482ef138c99d8b865ee1634c81762f6241d56f59e  out/stage58/stage58.img
3d35a5af065c779cac4d5cd43dd74806e95145531523c582966a07df574ea7f5  out/stage58/stage58-qcdt.img
```

Size summary:

```text
text=205824 data=0 bss=299328 dec=505152 hex=7b540
```

Boot-image parse highlights:

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

## Validation status

Current local validation completed:

- `stage58/build.sh` succeeds.
- `tools/parse_android_bootimg.py out/stage58/stage58.img` succeeds.
- `tools/parse_android_bootimg.py out/stage58/stage58-qcdt.img` succeeds and confirms the cancro-required QCDT `dt_size=2521088`.
- `arm-none-eabi-nm -u out/stage58/stage58.elf` prints no undefined symbols.
- `arm-none-eabi-nm -u out/stage58/xnu-link/stage58-xnu-link.elf` prints no undefined symbols.
- Static stale-reference scan finds no stale Stage57 code markers under `stage58/`.
- `external/xnu-upstream` and `external/xnu-4570.1.46` remain clean.
- `out/stage58/` and `external/` remain ignored by git.
- Fixed public ARM boot-args command lines remain under the 256-byte `CommandLine` field and keep the bounded-copy/final-NUL rule.

Hardware validation completed with non-persistent boot only:

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
