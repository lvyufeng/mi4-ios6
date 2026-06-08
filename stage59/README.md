# Stage59 — XNU pmap page-granular dry-run contract

Stage59 keeps the no-XNU-runtime safety envelope, preserves the bounded public pexpert object/link proof, preserves the Stage56 bootstrap mapping contract, preserves the Stage57 pmap/bootstrap allocation contract, preserves the Stage58 local section-table dry-run, and adds a Stage-owned **XNU pmap page-granular dry-run contract** for Xiaomi Mi 4 cancro / MSM8974 ARMv7.

The new proof advances beyond Stage58's 1 MiB L1 section descriptor dry-run by modeling the ARMv7 short-descriptor coarse-table path: four L1 table descriptors point into a local Stage-owned 4 KiB L2 page, and 1024 small-page PTEs cover a 4 MiB kernel/window span. The contract builds and validates only local Stage-owned buffers. It does not write the proposed pmap workspace, does not install live XNU/pmap tables, does not write TTBR/TTBCR/DACR/SCTLR for the proposed tables, does not invalidate TLBs for the proposed tables, and does not execute public VM/pmap runtime code.

## Public object subset

The public object subset remains intentionally stable:

```text
external/xnu-upstream/pexpert/gen/device_tree.c
external/xnu-upstream/pexpert/gen/bootargs.c
external/xnu-upstream/pexpert/gen/pe_gen.c
external/xnu-4570.1.46/pexpert/arm/pe_bootargs.c
external/xnu-4570.1.46/pexpert/arm/pe_consistent_debug.c
stage59/xnu_object_shims.c
```

Public ARM VM/pmap sources remain reference-only. Stage59 still does **not** compile, link, or execute public `arm_vm_init.c` or `pmap.c`.

## Safety model

Stage59 still does **not** run XNU or iOS. It does not build a full public `mach_kernel`, does not execute public-XNU object code, does not execute public ARM pexpert/platform runtime code, does not execute public ARM VM/pmap runtime code, does not execute the generated Mach-O fixture, does not jump to XNU `_start` / `arm_init`, does not write proposed XNU physical load addresses, does not write proposed TTE/pmap workspace addresses, does not install live proposed XNU/pmap tables, does not mutate external public checkouts, does not write persistent storage, and does not enable or change caches.

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
- Public-XNU graph/object/link outputs stay ignored under `out/stage59/`.
- Public source checkouts stay ignored under `external/`.

The Stage59 completion message is intentionally explicit:

```text
Stage59 XNU execution disabled: bootstrap mapping, pmap/bootstrap allocation, local pmap section-table dry-run, and local page-granular coarse/L2 dry-run contracts proved from stage-owned loader/TTE/highVA/safe-table/TTBR and allocator/workspace snapshot facts; public arm_vm_init/pmap references are arithmetic-only and compile/link/execute counts are zero; pexpert/platform compile graph classified, bounded public pe_gen plus ARM pe_bootargs and ARM consistent_debug objects compiled, and controlled ARM ELF link proof recorded; inert MH_PRELOAD fixture carries metadata only; no full mach_kernel build, no public-XNU execution, no platform runtime execution, no Mach-O execution, no proposed physical/workspace writes, no live pmap table install, no TLB invalidate for pmap install, no persistent writes, caches unchanged
```

## What Stage59 adds

Stage59 keeps the Stage58 section-table dry-run stable, and adds:

- a standalone `stage59/` payload with `0x59000001` success status and `MI4IOS6_STAGE59` logs,
- command-line markers for `xnu-pmap-page-dryrun-contract` and `pmap-page-local-l2-dryrun-only`,
- a Stage-owned `struct stage59_xnu_pmap_page_dryrun_contract` ABI in `stage59.h`,
- `stage59/xnu_pmap_page_dryrun_contract.c`,
- a 4096-entry, 16 KiB-aligned local dry-run L1 array,
- a 1024-entry, 4 KiB-aligned local dry-run L2 page,
- four L1 coarse/table descriptors covering a 4 MiB page-granular window,
- 1024 ARMv7 small-page PTEs with modeled default attributes `0x00000412`,
- descriptor and PTE readback checks,
- software translation checks for kernel, workspace, first-window, and last-window addresses,
- loader preflight roll-up fields for `xnu_pmap_page_dryrun_contract`,
- target-side proof that no proposed workspace was written and no live pmap table was installed,
- target-side proof that TTBR/TTBCR/DACR/SCTLR write counts and TLB invalidation counts for the proposed pmap path remain zero,
- target-side proof that public pmap compile/link/execute counts remain zero.

## Selected public XNU baselines

Stage59 expects:

```text
external/xnu-upstream      xnu-2050.22.13  cc8a9b0c  MasterVersion 12.3.0
external/xnu-4570.1.46     xnu-4570.1.46   76e12aa   later public ARM reference only
```

The public 2050 tree remains the Darwin 12 / iOS 6-era context. The 4570 tree is used only as a public ARM implementation reference for bounded compile/link proof objects and for blocked/reference-only source classification.

## Public VM/pmap reference-only surface

Stage59 reads/models these later-public ARM files only as public reference material:

```text
external/xnu-4570.1.46/osfmk/arm/arm_vm_init.c
external/xnu-4570.1.46/osfmk/arm/pmap.c
external/xnu-4570.1.46/osfmk/arm/pmap.h
external/xnu-4570.1.46/osfmk/arm/proc_reg.h
external/xnu-4570.1.46/osfmk/mach/arm/vm_param.h
```

They are not compiled into the public object subset, not linked into the proof ELF, and not executed on target hardware.

Modeled Stage58 section-table constants/facts remain:

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

Stage59 additionally models ARMv7 short-descriptor coarse-table and small-page constants from public ARM references:

```text
ARM_PGSHIFT=12
ARM_PGBYTES=0x00001000
ARM_PGMASK=0x00000fff
ARM_TT_L1_SIZE=0x00100000
ARM_TT_L1_PT_SIZE=0x00400000
ARM_TT_L2_SIZE=0x00001000
ARM_TT_L2_OFFMASK=0x00000fff
ARM_TT_L2_SHIFT=12
ARM_TT_L2_INDEX_MASK=0x000ff000
ARM_TTE_TYPE_TABLE=0x00000001
ARM_TTE_TYPE_MASK=0x00000003
ARM_TTE_TABLE_MASK=0xfffffc00
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

## Compile graph, object subset, and link proof

`stage59/xnu_compile_graph_scan.py` still classifies twenty candidates and allows only the five bounded public pexpert sources. ARM VM/pmap sources remain reference-only or blocked runtime inputs.

Successful graph/object/link markers from local and target validation:

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
stage59_xnu_object_subset_status=0x59000001
stage59_xnu_object_count=0x00000006
stage59_xnu_object_duplicate_symbol_count=0x00000000
stage59_xnu_link_status=0x59000001
stage59_xnu_link_object_count=0x00000006
stage59_xnu_link_undefined_symbol_count=0x00000000
```

`arm-none-eabi-nm -u out/stage59/stage59.elf` and `arm-none-eabi-nm -u out/stage59/xnu-link/stage59-xnu-link.elf` both report no undefined symbols.

## Bootstrap and pmap/bootstrap prerequisite contracts

Stage59 retains the Stage57/Stage58 contracts as prerequisites:

```text
stage59_xnu_bootstrap_contract_status=0x59000001
stage59_xnu_bootstrap_contract_required_mask=0x7fffffff
stage59_xnu_bootstrap_contract_failure_mask=0x00000000
loader_xnu_bootstrap_contract_status_rollup=0x59000001

stage59_xnu_pmap_bootstrap_contract_status=0x59000001
stage59_xnu_pmap_bootstrap_contract_required_mask=0x7fffffff
stage59_xnu_pmap_bootstrap_contract_satisfied_mask=0x7fffffff
stage59_xnu_pmap_bootstrap_contract_failure_mask=0x00000000
stage59_xnu_pmap_public_compile_count=0x00000000
stage59_xnu_pmap_public_link_count=0x00000000
stage59_xnu_pmap_public_execute_count=0x00000000
loader_xnu_pmap_bootstrap_contract_status_rollup=0x59000001
```

Modeled pmap/bootstrap arithmetic remains:

```text
stage59_xnu_pmap_gVirtBase=0x80008000
stage59_xnu_pmap_gPhysBase=0x80000000
stage59_xnu_pmap_gPhysSize=0x5e500000
stage59_xnu_pmap_boot_ttep=0x8000c000
stage59_xnu_pmap_cpu_ttep=0x80010000
stage59_xnu_pmap_initial_avail_start=0x80016000
stage59_xnu_pmap_avail_end=0xde500000
stage59_xnu_pmap_vstart=0xc0400000
stage59_xnu_pmap_virtual_space_end=0xfffeffff
stage59_xnu_pmap_workspace_base=0x80000000
stage59_xnu_pmap_workspace_limit=0x80100000
stage59_xnu_pmap_workspace_l1_table_phys=0x00074000
stage59_xnu_pmap_workspace_l1_table_virt=0xc0074000
stage59_xnu_pmap_workspace_l1_section_descriptor=0x00010c02
stage59_xnu_pmap_workspace_cache_policy=0x00000000
```

## Section-table dry-run prerequisite

`stage59/xnu_pmap_table_dryrun_contract.c` remains the Stage58 section-table dry-run, now renumbered and retained as a prerequisite. It imports the pmap/bootstrap allocation contract and snapshot facts, zeroes a Stage-owned local L1 buffer, simulates section descriptor population into that buffer, reads descriptors back, performs local software translations, and logs all fields through `xnu_log_kv32`.

Confirmed target-side prerequisite markers:

```text
stage59_xnu_pmap_table_dryrun_contract_status=0x59000001
stage59_xnu_pmap_table_dryrun_contract_required_mask=0x01ffffff
stage59_xnu_pmap_table_dryrun_contract_satisfied_mask=0x01ffffff
stage59_xnu_pmap_table_dryrun_contract_failure_mask=0x00000000
stage59_xnu_pmap_table_dryrun_contract_checksum=0x3c263dca
loader_xnu_pmap_table_dryrun_contract_status_rollup=0x59000001
```

## Page-granular pmap dry-run contract

`stage59/xnu_pmap_page_dryrun_contract.c` imports the bootstrap/pmap prerequisites, zeroes Stage-owned local L1 and L2 buffers, creates four ARMv7 short-descriptor L1 coarse/table descriptors, populates 1024 small-page PTEs, verifies descriptor/PTE readback, performs local software translations, checksums the local buffers and contract, and logs all fields through `xnu_log_kv32`.

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

Public page-table constants recorded by the contract:

```text
stage59_xnu_pmap_page_dryrun_l1_table_type=0x00000001
stage59_xnu_pmap_page_dryrun_l1_table_mask=0xfffffc00
stage59_xnu_pmap_page_dryrun_l2_page_bytes=0x00001000
stage59_xnu_pmap_page_dryrun_l2_coarse_table_bytes=0x00000400
stage59_xnu_pmap_page_dryrun_l2_pte_count=0x00000400
stage59_xnu_pmap_page_dryrun_pte_attr_default=0x00000412
```

Local-only L1/L2 buffer markers:

```text
stage59_xnu_pmap_page_dryrun_local_l1_base=0x00088000
stage59_xnu_pmap_page_dryrun_local_l1_limit=0x0008c000
stage59_xnu_pmap_page_dryrun_local_l2_base=0x00085000
stage59_xnu_pmap_page_dryrun_local_l2_limit=0x00086000
stage59_xnu_pmap_page_dryrun_local_l1_write_count=0x00000004
stage59_xnu_pmap_page_dryrun_local_l2_pte_write_count=0x00000400
```

Page-granular 4 MiB window markers:

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

L1 descriptor and PTE readback markers:

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

Software translation markers:

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

Negative safety markers:

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

## Built image

```bash
/mnt/data/mi4-ios6/stage59/build.sh
```

Final local build hashes:

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

Boot-image parse highlights:

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

## Validation status

Current local validation completed:

- `stage59/build.sh` succeeds.
- `tools/parse_android_bootimg.py out/stage59/stage59.img` succeeds.
- `tools/parse_android_bootimg.py out/stage59/stage59-qcdt.img` succeeds and confirms the cancro-required QCDT `dt_size=2521088`.
- `arm-none-eabi-nm -u out/stage59/stage59.elf` prints no undefined symbols.
- `arm-none-eabi-nm -u out/stage59/xnu-link/stage59-xnu-link.elf` prints no undefined symbols.
- Static stale-reference scan finds no stale Stage58 code markers under `stage59/` outside generated or historical context.
- `external/xnu-upstream` and `external/xnu-4570.1.46` remain clean.
- `out/stage59/` and `external/` remain ignored by git.
- Fixed public ARM boot-args command lines remain under the 256-byte `CommandLine` field and keep the bounded-copy/final-NUL rule.

Hardware validation completed with non-persistent boot only:

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
MI4IOS6_STAGE59_XNU stage59_xnu_pmap_page_dryrun_local_l1_write_count=0x00000004
MI4IOS6_STAGE59_XNU stage59_xnu_pmap_page_dryrun_local_l2_pte_write_count=0x00000400
MI4IOS6_STAGE59_XNU stage59_xnu_pmap_page_dryrun_translation_case_count=0x00000004
MI4IOS6_STAGE59_XNU loader_xnu_pmap_page_dryrun_contract_status_rollup=0x59000001
MI4IOS6_STAGE59_XNU loader_status=0x59000001
MI4IOS6_STAGE59_XNU kernel_entry ok
MI4IOS6_STAGE59 kernel_entry returned success
```

The device returned to Android after the PS_HOLD reset path. No persistent flash/erase/write was performed.

## Result

Stage59 completes the local-only page-granular pmap dry-run blocker. The new contract proves that the Stage-owned model can represent ARMv7 L1 coarse/table descriptors and L2 small-page PTEs for a bounded 4 MiB XNU-style mapping window while preserving all no-execution, no-proposed-write, no-live-table-install, no-control-register-write, no-TLB-invalidate, no-cache-change, and no-persistent-write boundaries.

The next safe direction is exact XNU cache/MMU attribute modeling, broader page-granular pmap windows, MSM8974 pexpert/timer/interrupt hooks, or another bounded public-XNU proof while continuing to avoid a full public `mach_kernel` build or any XNU runtime handoff.
