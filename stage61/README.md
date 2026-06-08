# Stage61 — XNU pmap multi-window page-granular dry-run contract

Stage61 keeps the no-XNU-runtime safety envelope, preserves the bounded public pexpert object/link proof, preserves the Stage56 bootstrap mapping contract, preserves the Stage57 pmap/bootstrap allocation contract, preserves the Stage58 local section-table dry-run, preserves the Stage59 page-granular coarse/L2 dry-run, preserves the Stage60 exact pmap cache/MMU attribute dry-run, and adds a Stage-owned **XNU pmap multi-window page-granular dry-run contract** for Xiaomi Mi 4 cancro / MSM8974 ARMv7.

The new proof advances beyond Stage60 by reusing the exact Stage-owned pmap PTE templates and ARMv7 short-descriptor attribute model across three independent 4 MiB coarse/L2 mapping windows: a kernel/workspace RAM window, a RAM-console window, and a device/GIC-style window. The contract builds and validates only local Stage-owned L1/L2 scratch buffers. It does not write the proposed pmap workspace, does not install live XNU/pmap tables, does not write TTBR/TTBCR/DACR/SCTLR for proposed tables, does not invalidate TLBs for proposed tables, does not change caches, and does not execute public VM/pmap runtime code.

## Public object subset

The public object subset remains intentionally stable:

```text
external/xnu-upstream/pexpert/gen/device_tree.c
external/xnu-upstream/pexpert/gen/bootargs.c
external/xnu-upstream/pexpert/gen/pe_gen.c
external/xnu-4570.1.46/pexpert/arm/pe_bootargs.c
external/xnu-4570.1.46/pexpert/arm/pe_consistent_debug.c
stage61/xnu_object_shims.c
```

Public ARM VM/pmap sources remain reference-only. Stage61 still does **not** compile, link, or execute public `arm_vm_init.c` or `pmap.c`.

## Safety model

Stage61 still does **not** run XNU or iOS. It does not build a full public `mach_kernel`, does not execute public-XNU object code, does not execute public ARM pexpert/platform runtime code, does not execute public ARM VM/pmap runtime code, does not execute the generated Mach-O fixture, does not jump to XNU `_start` / `arm_init`, does not write proposed XNU physical load addresses, does not write proposed TTE/pmap workspace addresses, does not install live proposed XNU/pmap tables, does not mutate external public checkouts, does not write persistent storage, and does not enable or change caches.

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
- Public-XNU graph/object/link outputs stay ignored under `out/stage61/`.
- Public source checkouts stay ignored under `external/`.

The Stage61 completion message is intentionally explicit:

```text
Stage61 XNU execution disabled: bootstrap mapping, pmap/bootstrap allocation, local pmap section-table dry-run, local page-granular coarse/L2 dry-run, exact pmap cache/MMU attribute dry-run, and local multi-window page-granular dry-run contracts proved from stage-owned loader/TTE/highVA/safe-table/TTBR and allocator/workspace snapshot facts; public arm_vm_init/pmap references are arithmetic-only and compile/link/execute counts are zero; pexpert/platform compile graph classified, bounded public pe_gen plus ARM pe_bootargs and ARM consistent_debug objects compiled, and controlled ARM ELF link proof recorded; inert MH_PRELOAD fixture carries metadata only; no full mach_kernel build, no public-XNU execution, no platform runtime execution, no Mach-O execution, no proposed physical/workspace writes, no live pmap table install, no TLB invalidate for pmap install, no cache change, no persistent writes
```

## What Stage61 adds

Stage61 keeps the Stage60 exact pmap cache/MMU attribute dry-run stable, and adds:

- a standalone `stage61/` payload with `0x61000001` success status and `MI4IOS6_STAGE61` logs,
- command-line markers for `xnu-pmap-multiwindow-dryrun-contract` and `pmap-multiwindow-local-dryrun-only`,
- a Stage-owned `struct stage61_xnu_pmap_multiwindow_dryrun_contract` ABI in `stage61.h`,
- `stage61/xnu_pmap_multiwindow_dryrun_contract.c`,
- a loader satisfied bit `STAGE61_LOADER_SAT_XNU_PMAP_MULTIWINDOW_DRYRUN_CONTRACT`,
- loader preflight roll-up fields for `xnu_pmap_multiwindow_dryrun_contract`,
- a local 16 KiB L1 buffer plus a local 12 KiB L2-bank buffer for three 4 MiB windows,
- kernel/workspace RAM, RAM-console, and device/GIC-style software translation checks,
- imported exact Stage60 PTE templates for RWX/RWNX/device-style IO mapping,
- public XNU `CACHE_ATTRINDX_*` modeling:
  - writeback `0`, write-combine `1`, write-through `2`, disabled/posted `3`, inner-writeback `4`, default `0`,
- public XNU `AP_*` modeling:
  - `AP_RWNA=0`, `AP_RWRW=1`, `AP_RONA=2`, `AP_RORO=3`,
- public ARMv7 short-descriptor attribute macro modeling:
  - PTE attr index bits `CB[3:2]` and `TEX0[6]`,
  - section/TTE attr index bits `CB[3:2]` and `TEX0[12]`,
  - PTE AP bits `AP[5]`/`APX[9]`,
  - section AP bits `AP[11]`/`APX[15]`,
- public `wimg_to_pte()`-style WIMG mapping for default/copyback, IO/posted, write-combine, write-through, and inner-writeback,
- public `arm_vm_page_granular_*` template modeling for RWX/RWNX/ROX/RONX page templates,
- readback cross-checks against the inherited Stage58/Stage59 table/page dry-run and Stage60 attribute words,
- target-side proof that public pmap compile/link/execute counts remain zero,
- target-side proof that no proposed workspace was written and no live pmap table was installed,
- target-side proof that TTBR/TTBCR/DACR/SCTLR write counts, TLB invalidation counts, persistent-write counts, and cache-change counts remain zero.

## Selected public XNU baselines

Stage61 expects:

```text
external/xnu-upstream      xnu-2050.22.13  cc8a9b0c  MasterVersion 12.3.0
external/xnu-4570.1.46     xnu-4570.1.46   76e12aa   later public ARM reference only
```

The public 2050 tree remains the Darwin 12 / iOS 6-era context. The 4570 tree is used only as a public ARM implementation reference for bounded compile/link proof objects and for blocked/reference-only source classification.

## Public VM/pmap reference-only surface

Stage61 reads/models these later-public ARM files only as public reference material:

```text
external/xnu-4570.1.46/osfmk/arm/arm_vm_init.c
external/xnu-4570.1.46/osfmk/arm/pmap.c
external/xnu-4570.1.46/osfmk/arm/pmap.h
external/xnu-4570.1.46/osfmk/arm/proc_reg.h
external/xnu-4570.1.46/osfmk/vm/pmap.h
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

Modeled Stage59 ARMv7 short-descriptor coarse-table/small-page constants remain:

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

The inherited Stage59 PTE attribute value corresponds to the public early small-page template shape:

```text
ARM_PTE_TYPE | ARM_PTE_AF | ARM_PTE_SH |
ARM_PTE_ATTRINDX(CACHE_ATTRINDX_DEFAULT) |
ARM_PTE_AP(AP_RWNA)
```

## Compile graph, object subset, and link proof

`stage61/xnu_compile_graph_scan.py` still classifies twenty candidates and allows only the five bounded public pexpert sources. ARM VM/pmap sources remain reference-only or blocked runtime inputs.

Successful graph/object/link markers from local and target validation:

```text
stage61_xnu_compile_graph_status=0x61000001
stage61_xnu_compile_graph_required_mask=0x1fffffff
stage61_xnu_compile_graph_satisfied_mask=0x1fffffff
stage61_xnu_compile_graph_failure_mask=0x00000000
stage61_xnu_compile_graph_candidate_count=0x00000014
stage61_xnu_compile_graph_allowed_compile_count=0x00000005
stage61_xnu_compile_graph_allowed_link_count=0x00000005
stage61_xnu_compile_graph_forbidden_count=0x0000000f
stage61_xnu_compile_graph_pmap_reference_mask=0x0000001f
stage61_xnu_compile_graph_pmap_runtime_blocked_mask=0x00000003
stage61_xnu_compile_graph_pmap_reference_count=0x00000005
stage61_xnu_compile_graph_pmap_public_compile_count=0x00000000
stage61_xnu_compile_graph_pmap_public_link_count=0x00000000
stage61_xnu_compile_graph_pmap_reference_only=0x00000001
stage61_xnu_object_subset_status=0x61000001
stage61_xnu_object_count=0x00000006
stage61_xnu_object_duplicate_symbol_count=0x00000000
stage61_xnu_link_status=0x61000001
stage61_xnu_link_object_count=0x00000006
stage61_xnu_link_undefined_symbol_count=0x00000000
```

`arm-none-eabi-nm -u out/stage61/stage61.elf` and `arm-none-eabi-nm -u out/stage61/xnu-link/stage61-xnu-link.elf` both report no undefined symbols.

## Bootstrap and pmap/bootstrap prerequisite contracts

Stage61 retains the Stage56/Stage57 contracts as prerequisites:

```text
stage61_xnu_bootstrap_contract_status=0x61000001
stage61_xnu_bootstrap_contract_required_mask=0x7fffffff
stage61_xnu_bootstrap_contract_failure_mask=0x00000000
loader_xnu_bootstrap_contract_status_rollup=0x61000001

stage61_xnu_pmap_bootstrap_contract_status=0x61000001
stage61_xnu_pmap_bootstrap_contract_required_mask=0x7fffffff
stage61_xnu_pmap_bootstrap_contract_satisfied_mask=0x7fffffff
stage61_xnu_pmap_bootstrap_contract_failure_mask=0x00000000
stage61_xnu_pmap_public_compile_count=0x00000000
stage61_xnu_pmap_public_link_count=0x00000000
stage61_xnu_pmap_public_execute_count=0x00000000
loader_xnu_pmap_bootstrap_contract_status_rollup=0x61000001
```

Modeled pmap/bootstrap arithmetic remains:

```text
stage61_xnu_pmap_gVirtBase=0x80008000
stage61_xnu_pmap_gPhysBase=0x80000000
stage61_xnu_pmap_gPhysSize=0x5e500000
stage61_xnu_pmap_boot_ttep=0x8000c000
stage61_xnu_pmap_cpu_ttep=0x80010000
stage61_xnu_pmap_initial_avail_start=0x80016000
stage61_xnu_pmap_avail_end=0xde500000
stage61_xnu_pmap_vstart=0xc0400000
stage61_xnu_pmap_virtual_space_end=0xfffeffff
stage61_xnu_pmap_workspace_base=0x80000000
stage61_xnu_pmap_workspace_limit=0x80100000
stage61_xnu_pmap_workspace_l1_table_phys=0x00074000
stage61_xnu_pmap_workspace_l1_table_virt=0xc0074000
stage61_xnu_pmap_workspace_l1_section_descriptor=0x00010c02
stage61_xnu_pmap_workspace_cache_policy=0x00000000
```

## Section-table dry-run prerequisite

`stage61/xnu_pmap_table_dryrun_contract.c` remains the Stage58 section-table dry-run, now renumbered and retained as a prerequisite. It imports the pmap/bootstrap allocation contract and snapshot facts, zeroes a Stage-owned local L1 buffer, simulates section descriptor population into that buffer, reads descriptors back, performs local software translations, and logs all fields through `xnu_log_kv32`.

Confirmed target-side prerequisite markers:

```text
stage61_xnu_pmap_table_dryrun_contract_status=0x61000001
stage61_xnu_pmap_table_dryrun_contract_required_mask=0x01ffffff
stage61_xnu_pmap_table_dryrun_contract_satisfied_mask=0x01ffffff
stage61_xnu_pmap_table_dryrun_contract_failure_mask=0x00000000
stage61_xnu_pmap_table_dryrun_contract_checksum=0x04263dca
stage61_xnu_pmap_table_dryrun_section_descriptor=0x00010c02
stage61_xnu_pmap_table_dryrun_descriptor_attr_mask_seen=0x00010c02
stage61_xnu_pmap_table_dryrun_translation_case_count=0x00000004
loader_xnu_pmap_table_dryrun_contract_status_rollup=0x61000001
```

## Page-granular pmap dry-run prerequisite

`stage61/xnu_pmap_page_dryrun_contract.c` imports the bootstrap/pmap prerequisites, zeroes Stage-owned local L1 and L2 buffers, creates four ARMv7 short-descriptor L1 coarse/table descriptors, populates 1024 small-page PTEs, verifies descriptor/PTE readback, performs local software translations, checksums the local buffers and contract, and logs all fields through `xnu_log_kv32`.

Confirmed target-side prerequisite markers:

```text
stage61_xnu_pmap_page_dryrun_contract_status=0x61000001
stage61_xnu_pmap_page_dryrun_contract_required_mask=0x00ffffff
stage61_xnu_pmap_page_dryrun_contract_satisfied_mask=0x00ffffff
stage61_xnu_pmap_page_dryrun_contract_failure_mask=0x00000000
stage61_xnu_pmap_page_dryrun_contract_checksum=0x9950cddf
loader_xnu_pmap_page_dryrun_contract_status=0x61000001
loader_xnu_pmap_page_dryrun_contract_satisfied_mask=0x00ffffff
loader_xnu_pmap_page_dryrun_contract_failure_mask=0x00000000
loader_xnu_pmap_page_dryrun_contract_checksum=0x9950cddf
loader_xnu_pmap_page_dryrun_contract_status_rollup=0x61000001
```

Public page-table constants recorded by the prerequisite:

```text
stage61_xnu_pmap_page_dryrun_l1_table_type=0x00000001
stage61_xnu_pmap_page_dryrun_l1_table_mask=0xfffffc00
stage61_xnu_pmap_page_dryrun_l2_page_bytes=0x00001000
stage61_xnu_pmap_page_dryrun_l2_coarse_table_bytes=0x00000400
stage61_xnu_pmap_page_dryrun_l2_pte_count=0x00000400
stage61_xnu_pmap_page_dryrun_pte_attr_default=0x00000412
```

Local-only L1/L2 buffer markers:

```text
stage61_xnu_pmap_page_dryrun_local_l1_base=0x00088000
stage61_xnu_pmap_page_dryrun_local_l1_limit=0x0008c000
stage61_xnu_pmap_page_dryrun_local_l2_base=0x00085000
stage61_xnu_pmap_page_dryrun_local_l2_limit=0x00086000
stage61_xnu_pmap_page_dryrun_local_l1_write_count=0x00000004
stage61_xnu_pmap_page_dryrun_local_l2_pte_write_count=0x00000400
```

Page-granular 4 MiB window markers:

```text
stage61_xnu_pmap_page_dryrun_window_virt_base=0x80000000
stage61_xnu_pmap_page_dryrun_window_virt_limit=0x80400000
stage61_xnu_pmap_page_dryrun_window_phys_base=0x80000000
stage61_xnu_pmap_page_dryrun_window_phys_limit=0x80400000
stage61_xnu_pmap_page_dryrun_l1_first_index=0x00000800
stage61_xnu_pmap_page_dryrun_l1_last_index=0x00000803
stage61_xnu_pmap_page_dryrun_l1_count=0x00000004
stage61_xnu_pmap_page_dryrun_l2_pte_first_index=0x00000000
stage61_xnu_pmap_page_dryrun_l2_pte_last_index=0x000003ff
stage61_xnu_pmap_page_dryrun_l2_pte_count=0x00000400
```

L1 descriptor and PTE readback markers:

```text
stage61_xnu_pmap_page_dryrun_l1_descriptor_first_word=0x00085001
stage61_xnu_pmap_page_dryrun_l1_descriptor_last_word=0x00085c01
stage61_xnu_pmap_page_dryrun_l1_descriptor_type_mask_seen=0x00000001
stage61_xnu_pmap_page_dryrun_l1_descriptor_attr_mask_seen=0x00000001
stage61_xnu_pmap_page_dryrun_pte_first_word=0x80000412
stage61_xnu_pmap_page_dryrun_pte_kernel_word=0x80008412
stage61_xnu_pmap_page_dryrun_pte_workspace_word=0x80000412
stage61_xnu_pmap_page_dryrun_pte_last_word=0x803ff412
stage61_xnu_pmap_page_dryrun_pte_type_mask_seen=0x00000002
stage61_xnu_pmap_page_dryrun_pte_attr_mask_seen=0x00000412
```

Software translation markers:

```text
stage61_xnu_pmap_page_dryrun_translation_kernel_va=0x80008000
stage61_xnu_pmap_page_dryrun_translation_kernel_pa=0x80008000
stage61_xnu_pmap_page_dryrun_translation_workspace_va=0x80000000
stage61_xnu_pmap_page_dryrun_translation_workspace_pa=0x80000000
stage61_xnu_pmap_page_dryrun_translation_window_first_va=0x80000000
stage61_xnu_pmap_page_dryrun_translation_window_first_pa=0x80000000
stage61_xnu_pmap_page_dryrun_translation_window_last_va=0x803fffff
stage61_xnu_pmap_page_dryrun_translation_window_last_pa=0x803fffff
stage61_xnu_pmap_page_dryrun_translation_case_count=0x00000004
```

## Pmap cache/MMU attribute dry-run prerequisite

`stage61/xnu_pmap_attr_dryrun_contract.c` imports the table and page dry-run prerequisites, models public `proc_reg.h`, `pmap.h`, `pmap.c`, and `arm_vm_init.c` attribute arithmetic, verifies exact ARMv7 short-descriptor bit placements, checks the inherited section/PTE readbacks, and logs all fields through `xnu_log_kv32`.

Confirmed target-side prerequisite markers:

```text
stage61_xnu_pmap_attr_dryrun_contract_status=0x61000001
stage61_xnu_pmap_attr_dryrun_contract_required_mask=0x00ffffff
stage61_xnu_pmap_attr_dryrun_contract_satisfied_mask=0x00ffffff
stage61_xnu_pmap_attr_dryrun_contract_failure_mask=0x00000000
stage61_xnu_pmap_attr_dryrun_contract_checksum=0xfd77fdcd
loader_xnu_pmap_attr_dryrun_contract_status=0x61000001
loader_xnu_pmap_attr_dryrun_contract_satisfied_mask=0x00ffffff
loader_xnu_pmap_attr_dryrun_contract_failure_mask=0x00000000
loader_xnu_pmap_attr_dryrun_contract_checksum=0xfd77fdcd
loader_xnu_pmap_attr_dryrun_contract_status_rollup=0x61000001
```

Public cache/AP/WIMG constants recorded by the contract:

```text
stage61_xnu_pmap_attr_dryrun_cache_writeback=0x00000000
stage61_xnu_pmap_attr_dryrun_cache_writecomb=0x00000001
stage61_xnu_pmap_attr_dryrun_cache_writethru=0x00000002
stage61_xnu_pmap_attr_dryrun_cache_disable=0x00000003
stage61_xnu_pmap_attr_dryrun_cache_innerwriteback=0x00000004
stage61_xnu_pmap_attr_dryrun_cache_posted=0x00000003
stage61_xnu_pmap_attr_dryrun_cache_default=0x00000000
stage61_xnu_pmap_attr_dryrun_ap_rwna=0x00000000
stage61_xnu_pmap_attr_dryrun_ap_rwrw=0x00000001
stage61_xnu_pmap_attr_dryrun_ap_rona=0x00000002
stage61_xnu_pmap_attr_dryrun_ap_roro=0x00000003
stage61_xnu_pmap_attr_dryrun_vm_wimg_default=0x00000002
stage61_xnu_pmap_attr_dryrun_vm_wimg_copyback=0x00000002
stage61_xnu_pmap_attr_dryrun_vm_wimg_innerwback=0x00000012
stage61_xnu_pmap_attr_dryrun_vm_wimg_io=0x00000007
stage61_xnu_pmap_attr_dryrun_vm_wimg_posted=0x00000027
stage61_xnu_pmap_attr_dryrun_vm_wimg_wthru=0x0000000b
stage61_xnu_pmap_attr_dryrun_vm_wimg_wcomb=0x00000006
```

Attribute macro and template markers:

```text
stage61_xnu_pmap_attr_dryrun_pte_attr_writeback=0x00000000
stage61_xnu_pmap_attr_dryrun_pte_attr_writecomb=0x00000004
stage61_xnu_pmap_attr_dryrun_pte_attr_writethru=0x00000008
stage61_xnu_pmap_attr_dryrun_pte_attr_disable=0x0000000c
stage61_xnu_pmap_attr_dryrun_pte_attr_innerwriteback=0x00000040
stage61_xnu_pmap_attr_dryrun_pte_attr_roundtrip_mask=0x0000001f
stage61_xnu_pmap_attr_dryrun_tte_attr_writeback=0x00000000
stage61_xnu_pmap_attr_dryrun_tte_attr_writecomb=0x00000004
stage61_xnu_pmap_attr_dryrun_tte_attr_writethru=0x00000008
stage61_xnu_pmap_attr_dryrun_tte_attr_disable=0x0000000c
stage61_xnu_pmap_attr_dryrun_tte_attr_innerwriteback=0x00001000
stage61_xnu_pmap_attr_dryrun_tte_attr_roundtrip_mask=0x0000001f
stage61_xnu_pmap_attr_dryrun_pte_template_rwx_word=0x00000412
stage61_xnu_pmap_attr_dryrun_pte_template_rwnx_word=0x00000413
stage61_xnu_pmap_attr_dryrun_pte_template_rox_word=0x00000612
stage61_xnu_pmap_attr_dryrun_pte_template_ronx_word=0x00000613
stage61_xnu_pmap_attr_dryrun_section_template_word=0x00010c02
```

WIMG mapping markers:

```text
stage61_xnu_pmap_attr_dryrun_wimg_default_pte_bits=0x00000400
stage61_xnu_pmap_attr_dryrun_wimg_io_pte_bits=0x0000000d
stage61_xnu_pmap_attr_dryrun_wimg_posted_pte_bits=0x0000000d
stage61_xnu_pmap_attr_dryrun_wimg_wcomb_pte_bits=0x00000005
stage61_xnu_pmap_attr_dryrun_wimg_wthru_pte_bits=0x00000408
stage61_xnu_pmap_attr_dryrun_wimg_innerwback_pte_bits=0x00000440
```

Prior-readback cross-check markers:

```text
stage61_xnu_pmap_attr_dryrun_prior_table_section_word=0x00010c02
stage61_xnu_pmap_attr_dryrun_prior_table_attr_seen=0x00010c02
stage61_xnu_pmap_attr_dryrun_prior_page_pte_word=0x00000412
stage61_xnu_pmap_attr_dryrun_prior_page_attr_seen=0x00000412
```

Negative safety markers:

```text
stage61_xnu_pmap_attr_dryrun_public_pmap_compile_count=0x00000000
stage61_xnu_pmap_attr_dryrun_public_pmap_link_count=0x00000000
stage61_xnu_pmap_attr_dryrun_public_pmap_execute_count=0x00000000
stage61_xnu_pmap_attr_dryrun_public_arm_vm_init_executed=0x00000000
stage61_xnu_pmap_attr_dryrun_proposed_workspace_written=0x00000000
stage61_xnu_pmap_attr_dryrun_live_pmap_tables_installed=0x00000000
stage61_xnu_pmap_attr_dryrun_ttbr_written=0x00000000
stage61_xnu_pmap_attr_dryrun_ttbcr_written=0x00000000
stage61_xnu_pmap_attr_dryrun_dacr_written=0x00000000
stage61_xnu_pmap_attr_dryrun_sctlr_written=0x00000000
stage61_xnu_pmap_attr_dryrun_tlbs_invalidated=0x00000000
stage61_xnu_pmap_attr_dryrun_caches_changed=0x00000000
stage61_xnu_pmap_attr_dryrun_persistent_write_attempted=0x00000000
stage61_xnu_pmap_attr_dryrun_xnu_start_executed=0x00000000
stage61_xnu_pmap_attr_dryrun_generated_macho_executed=0x00000000
stage61_xnu_pmap_attr_dryrun_local_only=0x00000001
stage61_xnu_pmap_attr_dryrun_fail_closed=0x00000001
```

## Multi-window page-granular pmap dry-run contract

`stage61/xnu_pmap_multiwindow_dryrun_contract.c` imports the table/page/attribute dry-run prerequisites, zeroes Stage-owned local L1 and L2-bank buffers, creates three independent 4 MiB ARMv7 short-descriptor coarse/L2 windows, populates 3072 small-page PTEs, verifies descriptor/PTE readback, performs four local software translations, checksums the local buffers and contract, and logs all fields through `xnu_log_kv32`.

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

Local multi-window constants and imported templates:

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

Local-only L1/L2-bank markers:

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

Modeled 4 MiB window markers:

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

Negative safety markers:

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

## Built image

```bash
/mnt/data/mi4-ios6/stage61/build.sh
```

Final local build hashes:

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

Boot-image parse highlights:

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

## Validation status

Current local validation completed:

- `stage61/build.sh` succeeds.
- `tools/parse_android_bootimg.py out/stage61/stage61.img` succeeds.
- `tools/parse_android_bootimg.py out/stage61/stage61-qcdt.img` succeeds and confirms the cancro-required QCDT `dt_size=2521088`.
- `arm-none-eabi-nm -u out/stage61/stage61.elf` prints no undefined symbols.
- `arm-none-eabi-nm -u out/stage61/xnu-link/stage61-xnu-link.elf` prints no undefined symbols.
- Static stale-reference scan finds no stale Stage60 code markers under `stage61/` outside generated or historical context.
- `external/xnu-upstream` and `external/xnu-4570.1.46` remain ignored/untouched.
- `out/stage61/` and `external/` remain ignored by git.
- Fixed public ARM boot-args command lines remain under the 256-byte `CommandLine` field and keep the bounded-copy/final-NUL rule:
  - `stage61/boot_args.c`: `192/256` including NUL.
  - `stage61/stage61_main.c`: `192/256` including NUL.
  - `stage61/xnu_object_shims.c`: `175/256` including NUL.

Hardware validation completed with non-persistent boot only:

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

Stage61 completes the broader page-granular multi-window pmap dry-run blocker. The new contract proves that the Stage-owned model can compose exact inherited public ARM XNU PTE templates into separate local coarse/L2 windows for kernel/workspace RAM, RAM-console, and device/GIC-style mappings while preserving all no-execution, no-proposed-write, no-live-table-install, no-control-register-write, no-TLB-invalidate, no-cache-change, and no-persistent-write boundaries.

The next safe direction is a still-local proof for safe live-table transition prerequisites, MSM8974 pexpert/timer/interrupt hooks, IOKit/platform-driver scaffolding, or another bounded public-XNU proof while continuing to avoid a full public `mach_kernel` build or any XNU runtime handoff.
